#include "hook_flip.h"

#include <windows.h>
#include <d3d9.h>

#include "compat.h"
#include "format.h"
#include "fullscreen.h"
#include "geometry.h"
#include "logging.h"
#include "resize.h"
#include "shaders.h"

// Trampoline for the Flip-entry detour: original 7-byte prologue + jmp back.
extern "C" unsigned char g_entryTrampoline[16] = {0xCC};
extern "C" void* g_entryTrampolinePtr = nullptr;

namespace dm {

namespace {

bool g_resizeEnabled = false;
ResizeState g_resizeState;
DWORD g_lastFlipTick = 0;
// Set when the entry hook rolled a failed reset back: the shim must skip the
// rest of this Flip (return DD_OK like the wrapper's own reset path does)
// instead of running the frame on restored-but-unverifiable state.
long g_skipThisFlip = 0;

typedef long (__stdcall* VtMethodFn)(void* self);

// D3DX font vtable slots used around a device reset (established from the
// wrapper's own reset helper in the static research).
enum { kFontOnLostDevice = 16, kFontOnResetDevice = 17 };

struct AdaptConfig {
    bool active;
    ScaleMode mode;
    int integerScale;
    int customWidth;
    int customHeight;
    unsigned long filterOverride;      // 0 = keep the wrapper's filter
    unsigned long backgroundColor;     // ARGB
};

struct ObsState {
    unsigned char* base = nullptr;
    const DdrawProfile* profile = nullptr;
    HookConfig config = {8, 600};
    AdaptConfig adapt;

    unsigned long calls = 0;

    // Previous-frame snapshot for observation change detection.
    bool hasPrev = false;
    IDirect3DSurface9* prevSrc = nullptr;
    IDirect3DSurface9* prevDst = nullptr;
    unsigned long prevFilter = 0;
    unsigned long prevPpW = 0, prevPpH = 0;
    unsigned long prevBorderless = 0;
    void* prevDevice = nullptr;
    void* prevNative = nullptr;
    RECT prevRect = {};
    bool prevRectNull = true;

    // Surface description cache, refreshed when the surface pointers change
    // (also the post-reset recreation signal).
    bool haveDescs = false;
    unsigned long srcW = 0, srcH = 0, bbW = 0, bbH = 0;
};

ObsState g_obs;

unsigned long readU32(unsigned char* base, uint32_t rva) {
    return *reinterpret_cast<unsigned long*>(base + rva);
}

// (Re)creates the wrapper's two surfaces from a captured description.
// Returns null on failure. (Written to return the pointer rather than fill
// an out-param: this v141 build intermittently mis-parses
// `IDirect3Surface9*` declarations late in dmFlipEntryWork - see the build
// note in tools/build-xp.bat.)
struct Recreator {
    IDirect3DDevice9* dev;
    void* recreate(const D3DSURFACE_DESC& d) {
        compatSuspendTracking();
        void* out = nullptr;
        HRESULT hr;
        if (d.Usage & D3DUSAGE_RENDERTARGET) {
            hr = dev->CreateRenderTarget(
                d.Width, d.Height, d.Format, d.MultiSampleType,
                d.MultiSampleQuality, TRUE,
                reinterpret_cast<IDirect3DSurface9**>(&out), nullptr);
        } else {
            hr = dev->CreateOffscreenPlainSurface(
                d.Width, d.Height, d.Format, d.Pool,
                reinterpret_cast<IDirect3DSurface9**>(&out), nullptr);
        }
        if (FAILED(hr)) {
            DM_LOG("resize: surface create failed hr=%08lx", long(hr));
        }
        compatResumeTracking();
        return out;
    }
};

void storeSurface(unsigned char* base, uint32_t rva, void* p) {
    *reinterpret_cast<void**>(base + rva) = p;
}

// ---- Exclusive fullscreen state (driven from Flip entry; no WndProc) ----
bool g_fsEnabled = false;       // feature armed (config + no blocking mods)
bool g_fsHotkey = true;         // F11 toggle armed
long g_fsVSync = -1;            // -1 keep wrapper interval, 0 immediate, 1 one
bool g_fsExclusive = false;     // currently in exclusive fullscreen
bool g_fsBorderless = false;    // currently in OUR borderless fullscreen
bool g_fsUseBorderless = false; // config: Alt+Enter/F11 target mode
bool g_altEnterWasDown = false; // Alt+Enter edge detection
bool g_fsPending = false;       // enter/exit request queued
bool g_fsKeyWasDown = false;    // F11 edge detection
unsigned char g_fsSavedPp[kPp_Size];
LONG_PTR g_fsSavedStyle = 0;
LONG_PTR g_fsSavedExStyle = 0;
RECT g_fsSavedRect = {};
bool g_fsSavedWindowPlacement = false;

// Full font-safe reset to whatever parameters currently sit at ppRva.
// fallbackPp (may be null): parameter block restored into ppRva when the
// Reset fails, so the caller's rollback leaves the device consistent.
// Returns true when the reset and surface recreation succeeded. On failure
// the surfaces are recreated from their captured descriptions regardless,
// so the wrapper never runs on dangling pointers.
bool resetSequence(unsigned char* fallbackPp, const char* reason) {
    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9**>(
        g_obs.base + g_obs.profile->deviceRva);
    void* font =
        *reinterpret_cast<void**>(g_obs.base + g_obs.profile->fontRva);
    IDirect3DSurface9* staging = *reinterpret_cast<IDirect3DSurface9**>(
        g_obs.base + g_obs.profile->rendererObjectRva);
    IDirect3DSurface9* native = *reinterpret_cast<IDirect3DSurface9**>(
        g_obs.base + g_obs.profile->nativeSurfaceRva);
    if (!dev || !staging || !native) {
        static bool s_announced = false;
        if (!s_announced) {
            s_announced = true;
            DM_LOG("%s: device/surfaces missing (globals nulled by an "
                   "earlier failed switch); aborting quietly from now on",
                   reason);
        }
        return false;
    }
    D3DSURFACE_DESC dS = {}, dN = {};
    if (FAILED(staging->GetDesc(&dS)) || FAILED(native->GetDesc(&dN))) {
        DM_LOG("%s: GetDesc failed; aborted", reason);
        return false;
    }
    void*** fontVt = reinterpret_cast<void***>(font);
    Recreator recreator{dev};
    // The wrapper's staging/native are recreated below through the hooked
    // creation slots; they must never sit in the foreign list, or the next
    // switch force-releases freed pointers (use-after-free that corrupts
    // the device - the actual exit-death root cause).
    compatExcludeForeign(reinterpret_cast<void*>(staging),
                         reinterpret_cast<void*>(native));
    if (font && fontVt) {
        reinterpret_cast<VtMethodFn>(fontVt[0][kFontOnLostDevice])(font);
    }
    staging->Release();
    native->Release();
    storeSurface(g_obs.base, g_obs.profile->rendererObjectRva, nullptr);
    storeSurface(g_obs.base, g_obs.profile->nativeSurfaceRva, nullptr);

    // Shader pipeline owns default-pool resources too; and the compat layer
    // force-releases FOREIGN ones (training/netplay overlays) so the Reset
    // can succeed alongside them.
    shadersReleaseResources();
    const int foreignReleased = compatReleaseForeignResources();
    if (foreignReleased) {
        DM_LOG("compat: force-released %d foreign device resources",
               foreignReleased);
    }

    unsigned char* pp = g_obs.base + g_obs.profile->ppRva;
    HRESULT hr = dev->Reset(reinterpret_cast<D3DPRESENT_PARAMETERS*>(pp));
    for (int attempt = 0; attempt < 3 && FAILED(hr); ++attempt) {
        // Observed failure shape: Reset INVALIDCALL with the device left in
        // DEVICENOTRESET - the reset landed on a device mid-transition
        // (exclusive focus/mode events fire asynchronously). Retry a few
        // times with a short settle, sweeping any re-created foreign
        // resources between attempts.
        const int more = compatReleaseForeignResources();
        DM_LOG("%s: Reset failed hr=%08lx (TCL %08lx), retry %d (swept %d)",
               reason, long(hr), long(dev->TestCooperativeLevel()), attempt,
               more);
        Sleep(20);
        hr = dev->Reset(reinterpret_cast<D3DPRESENT_PARAMETERS*>(pp));
    }
    const bool ok = SUCCEEDED(hr);
    if (!ok) {
        // Roll back to the pre-switch parameters and RESET THE DEVICE BACK
        // TO THEM: the failed switch can leave the device in
        // DEVICENOTRESET, and in that state new default-pool allocations
        // fail too - without this recovery step the wrapper would be left
        // with null surfaces (dead rendering). A Reset with the params the
        // device was happily running before succeeds in this state (same
        // fact the device-recovery path relies on), and then recreation
        // below works.
        DM_LOG("%s: Reset hr=%08lx (TCL %08lx) - rolling back", reason,
               long(hr), long(dev->TestCooperativeLevel()));
        if (fallbackPp) memcpy(pp, fallbackPp, kPp_Size);
        HRESULT hrBack =
            dev->Reset(reinterpret_cast<D3DPRESENT_PARAMETERS*>(pp));
        DM_LOG("%s: rollback reset hr=%08lx", reason, long(hrBack));
        (void)hrBack;
    }
    void* s = recreator.recreate(dS);
    void* n = recreator.recreate(dN);
    if (!s || !n) {
        DM_LOG("%s: recreation failed (staging=%d native=%d, TCL %08lx)",
               reason, s ? 1 : 0, n ? 1 : 0,
               long(dev->TestCooperativeLevel()));
    }
    DM_LOG("%s: surface recreation staging=%d native=%d", reason, s ? 1 : 0,
           n ? 1 : 0);
    if (s) storeSurface(g_obs.base, g_obs.profile->rendererObjectRva, s);
    if (n) storeSurface(g_obs.base, g_obs.profile->nativeSurfaceRva, n);
    if (font && fontVt) {
        reinterpret_cast<VtMethodFn>(fontVt[0][kFontOnResetDevice])(font);
    }
    g_obs.haveDescs = false;
    if (ok) {
        IDirect3DDevice9* dev2 = reinterpret_cast<IDirect3DDevice9*>(dev);
        if (!shadersCreateResources(dev2, int(dS.Width), int(dS.Height))) {
            DM_LOG("shaders: unavailable after reset; using StretchRect");
        }
    }
    return ok;
}

// Adapter-resolved desktop mode for the window's monitor, mirroring the
// QOH99 reference: exclusive is only valid when the DEVICE'S OWN adapter
// owns the window's monitor (GetAdapterMonitor match), the mode comes from
// GetAdapterDisplayMode (D3D-side truth, includes the format), and a
// mismatch refuses exclusive instead of risking a wrong-adapter takeover.
bool adapterDesktopModeForHwnd(IDirect3DDevice9* dev, HWND hwnd,
                               DisplayMode* out) {
    IDirect3D9* d3d = nullptr;
    if (FAILED(dev->GetDirect3D(&d3d)) || !d3d) return false;
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    bool found = false;
    UINT adapterCount = d3d->GetAdapterCount();
    for (UINT a = 0; a < adapterCount && !found; ++a) {
        if (d3d->GetAdapterMonitor(a) != mon) continue;
        D3DDISPLAYMODE mode;
        if (SUCCEEDED(d3d->GetAdapterDisplayMode(a, &mode)) && mode.Width &&
            mode.Height) {
            out->w = long(mode.Width);
            out->h = long(mode.Height);
            out->refresh = long(mode.RefreshRate);
            out->format = mode.Format;
            found = true;
        }
    }
    d3d->Release();
    return found;
}

void restoreWindowedFrame(HWND hwnd) {
    if (!g_fsSavedWindowPlacement || !hwnd) return;
    SetWindowLongPtrA(hwnd, GWL_STYLE, g_fsSavedStyle);
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, g_fsSavedExStyle);
    SetWindowPos(hwnd, HWND_TOP, g_fsSavedRect.left, g_fsSavedRect.top,
                 g_fsSavedRect.right - g_fsSavedRect.left,
                 g_fsSavedRect.bottom - g_fsSavedRect.top,
                 SWP_NOACTIVATE | SWP_FRAMECHANGED);
    g_fsSavedWindowPlacement = false;
}

void enterExclusive(HWND hwnd) {
    DisplayMode mode;
    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9**>(
        g_obs.base + g_obs.profile->deviceRva);
    if (!dev || !adapterDesktopModeForHwnd(dev, hwnd, &mode)) {
        DM_LOG("fullscreen: device adapter does not own the window's "
               "monitor, or mode query failed; refusing exclusive");
        return;
    }
    unsigned char* pp = g_obs.base + g_obs.profile->ppRva;
    unsigned char fallback[kPp_Size];
    ppSave(fallback, pp);
    ppSave(g_fsSavedPp, pp);
    ppEnterExclusive(pp, mode, uintptr_t(hwnd));
    // VSync: -1 keeps the exclusive default (IMMEDIATE, low latency);
    // explicit 1 opts into vsync.
    if (g_fsVSync == 1) ppApplyVSync(pp, 1);

    // Window prep per the QOH99 reference: save style/exstyle/rect once,
    // then a clean popup covering the monitor so no caption flashes during
    // the mode transition.
    if (!g_fsSavedWindowPlacement) {
        g_fsSavedStyle = GetWindowLongPtrA(hwnd, GWL_STYLE);
        g_fsSavedExStyle = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
        GetWindowRect(hwnd, &g_fsSavedRect);
        g_fsSavedWindowPlacement = true;
    }
    SetWindowLongPtrA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (mon && GetMonitorInfoA(mon, &mi)) {
        SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top,
                     mi.rcMonitor.right - mi.rcMonitor.left,
                     mi.rcMonitor.bottom - mi.rcMonitor.top,
                     SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }

    DM_LOG("fullscreen: entering exclusive %ldx%ld@%ldhz fmt=%lu interval=%s",
           mode.w, mode.h, mode.refresh, mode.format,
           g_fsVSync == 1 ? "one" : "immediate");
    if (resetSequence(fallback, "fullscreen-enter")) {
        g_fsExclusive = true;
    } else {
        // resetSequence already restored the windowed parameters.
        restoreWindowedFrame(hwnd);
        DM_LOG("fullscreen: enter failed; staying windowed");
    }
}

void enterBorderless(HWND hwnd) {
    HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi;
    memset(&mi, 0, sizeof(mi));
    mi.cbSize = sizeof(mi);
    if (!mon || !GetMonitorInfoA(mon, &mi)) {
        DM_LOG("borderless: monitor query failed; staying windowed");
        return;
    }
    const long monW = mi.rcMonitor.right - mi.rcMonitor.left;
    const long monH = mi.rcMonitor.bottom - mi.rcMonitor.top;

    unsigned char* pp = g_obs.base + g_obs.profile->ppRva;
    unsigned char fallback[kPp_Size];
    ppSave(fallback, pp);
    ppSave(g_fsSavedPp, pp);
    // Windowed stays TRUE; only the backbuffer grows to the monitor so the
    // final blit letterboxes instead of the OS stretching a 4:3 backbuffer.
    *reinterpret_cast<long*>(pp) = monW;
    *reinterpret_cast<long*>(pp + 4) = monH;
    ppApplyVSync(pp, g_fsVSync);

    if (!g_fsSavedWindowPlacement) {
        g_fsSavedStyle = GetWindowLongPtrA(hwnd, GWL_STYLE);
        g_fsSavedExStyle = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
        GetWindowRect(hwnd, &g_fsSavedRect);
        g_fsSavedWindowPlacement = true;
    }
    SetWindowLongPtrA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
    SetWindowPos(hwnd, HWND_TOP, mi.rcMonitor.left, mi.rcMonitor.top, monW,
                 monH, SWP_NOACTIVATE | SWP_FRAMECHANGED);

    DM_LOG("borderless: entering %ldx%ld", monW, monH);
    if (resetSequence(fallback, "borderless-enter")) {
        g_fsBorderless = true;
    } else {
        restoreWindowedFrame(hwnd);
        DM_LOG("borderless: enter failed; staying windowed");
    }
}

void exitBorderless(HWND hwnd) {
    unsigned char* pp = g_obs.base + g_obs.profile->ppRva;
    unsigned char fallback[kPp_Size];
    ppSave(fallback, pp);
    ppRestoreWindowed(pp, g_fsSavedPp);
    ppApplyVSync(pp, g_fsVSync);
    restoreWindowedFrame(hwnd);
    DM_LOG("borderless: exiting to windowed");
    if (resetSequence(fallback, "borderless-exit")) {
        g_fsBorderless = false;
        g_resizeState = ResizeState();
    } else {
        DM_LOG("borderless: exit failed; staying borderless");
    }
}

void exitExclusive(HWND hwnd) {
    unsigned char* pp = g_obs.base + g_obs.profile->ppRva;
    unsigned char fallback[kPp_Size];
    ppSave(fallback, pp);
    ppRestoreWindowed(pp, g_fsSavedPp);
    // Explicit VSync choices also apply windowed; -1 keeps the wrapper's.
    ppApplyVSync(pp, g_fsVSync);
    // Reset FIRST, window frame after: touching the window
    // (SWP_FRAMECHANGED) while the device still owns exclusive mode can
    // trigger an asynchronous display-mode transition, and a Reset landing
    // mid-transition returns INVALIDCALL (intermittent exit failures).
    DM_LOG("fullscreen: exiting to windowed");
    if (resetSequence(fallback, "fullscreen-exit")) {
        restoreWindowedFrame(hwnd);
        g_fsExclusive = false;
        // Re-capture the resize baseline at the restored window size.
        g_resizeState = ResizeState();
    } else {
        // Parameters were rolled back to the exclusive block; still inside.
        // The window frame stays exclusive too.
        DM_LOG("fullscreen: exit failed; staying exclusive");
    }
}

}  // namespace

void setHookContext(unsigned char* ddrawBase, const DdrawProfile* profile,
                    const HookConfig& config) {
    g_obs.base = ddrawBase;
    g_obs.profile = profile;
    g_obs.config = config;
}

void setAdaptConfig(ScaleMode mode, int integerScale, int customWidth,
                    int customHeight, unsigned long filterOverride,
                    unsigned long backgroundColor) {
    g_obs.adapt.active = true;
    g_obs.adapt.mode = mode;
    g_obs.adapt.integerScale = integerScale;
    g_obs.adapt.customWidth = customWidth;
    g_obs.adapt.customHeight = customHeight;
    g_obs.adapt.filterOverride = filterOverride;
    g_obs.adapt.backgroundColor = backgroundColor;
}

void setResizeConfig(bool enabled) {
    g_resizeEnabled = enabled;
}

void setFullscreenConfig(bool enabled, bool hotkey, long vsync,
                         bool requestEnter, bool useBorderless) {
    g_fsEnabled = enabled;
    g_fsHotkey = hotkey;
    g_fsVSync = vsync;
    g_fsUseBorderless = useBorderless;
    g_fsPending = enabled && requestEnter;
}

unsigned char* entryTrampolineBuffer() {
    return g_entryTrampoline;
}

size_t entryTrampolineCapacity() {
    return sizeof(g_entryTrampoline);
}

void prepareEntryTrampoline() {
    g_entryTrampolinePtr = g_entryTrampoline;
}

unsigned long hookCallCount() {
    return g_obs.calls;
}

extern "C" void dmFlipEntryWork() {
    if (!g_obs.base || !g_obs.profile) return;
    IDirect3DDevice9* dev = *reinterpret_cast<IDirect3DDevice9**>(
        g_obs.base + g_obs.profile->deviceRva);
    if (!dev) return;
    HWND hwnd = reinterpret_cast<HWND>(
        *reinterpret_cast<void**>(g_obs.base + g_obs.profile->hwndRva));

    // Hotkeys first: a key pressed during a lost-device window must still
    // latch the request; it executes once the device is healthy again.
    // F11 and Alt+Enter both toggle the configured fullscreen mode.
    if (g_fsEnabled && g_fsHotkey) {
        const bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        const bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
        const bool enterKey = (GetAsyncKeyState(VK_RETURN) & 0x8000) != 0;
        const bool altEnter = alt && enterKey;
        if ((f11 && !g_fsKeyWasDown) || (altEnter && !g_altEnterWasDown)) {
            g_fsPending = true;
        }
        g_fsKeyWasDown = f11;
        g_altEnterWasDown = altEnter;
    }

    // Device-lost handling: in exclusive mode focus loss is routine. While
    // lost, skip the frame; when resettable, recover to the CURRENT
    // parameters (still exclusive until we choose otherwise).
    const HRESULT tcl = dev->TestCooperativeLevel();
    if (tcl == D3DERR_DEVICELOST) {
        static bool announced = false;
        if (!announced) {
            DM_LOG("device: D3DERR_DEVICELOST - skipping frames until "
                   "resettable");
            announced = true;
        }
        g_skipThisFlip = 1;
        return;
    }
    if (tcl == D3DERR_DEVICENOTRESET) {
        static bool announced2 = false;
        if (!announced2) {
            DM_LOG("device: D3DERR_DEVICENOTRESET - recovering");
            announced2 = true;
        }
    }
    // Any other TCL result clears the announcements next time around.
    const bool needRecovery = (tcl == D3DERR_DEVICENOTRESET);

    if (g_fsEnabled && g_fsPending && !needRecovery && hwnd) {
        g_fsPending = false;
        if (g_fsUseBorderless) {
            if (!g_fsBorderless) {
                enterBorderless(hwnd);
            } else {
                exitBorderless(hwnd);
            }
        } else if (!g_fsExclusive) {
            enterExclusive(hwnd);
        } else {
            exitExclusive(hwnd);
        }
        return;  // one switch per flip; evaluate sizes on the next one
    }

    if (needRecovery) {
        static int s_recoveryFailures = 0;
        IDirect3DDevice9* recDev = *reinterpret_cast<IDirect3DDevice9**>(
            g_obs.base + g_obs.profile->deviceRva);
        const bool ourSurfacesGone =
            *reinterpret_cast<void**>(g_obs.base +
                                     g_obs.profile->rendererObjectRva) == nullptr;
        if (!ourSurfacesGone && recDev &&
            resetSequence(nullptr, "device-recovery")) {
            DM_LOG("device: recovered after D3DERR_DEVICENOTRESET");
            s_recoveryFailures = 0;
        } else if (recDev) {
            // Our sequence cannot run (surfaces missing or failed). Let the
            // WRAPPER's own reset helper do the work: it is null-safe for
            // its surfaces, re-notifies its D3DX font, and recreates
            // staging/native from its own stored format/size - which also
            // restores the associations (palette, conversion state) its
            // emulated DirectDraw layer needs. Guarded on the font being
            // non-null: the helper dereferences it unconditionally.
            void* font =
                *reinterpret_cast<void**>(g_obs.base + g_obs.profile->fontRva);
            if (font) {
                typedef void (__thiscall* HelperFn)(void*, void*);
                HelperFn helper = reinterpret_cast<HelperFn>(
                    g_obs.base + g_obs.profile->resetHelperRva);
                helper(g_obs.base + g_obs.profile->rendererObjectRva,
                       g_obs.base + g_obs.profile->ppRva);
                if (s_recoveryFailures == 0) {
                    DM_LOG("device: delegated recovery to the wrapper's own "
                           "reset helper");
                }
            }
            if (s_recoveryFailures == 0 || s_recoveryFailures % 600 == 0) {
                DM_LOG("device: recovery still failing (attempt %d)",
                       s_recoveryFailures);
            }
            ++s_recoveryFailures;
            g_skipThisFlip = 1;
        } else {
            ++s_recoveryFailures;
            g_skipThisFlip = 1;
        }
        return;
    }

    // Windowed backbuffer resize (never while exclusive).
    if (!g_resizeEnabled || g_fsExclusive || g_fsBorderless || !hwnd) return;
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return;
    ResizeInput in;
    in.clientW = rc.right - rc.left;
    in.clientH = rc.bottom - rc.top;
    in.bbW = ppWidth(g_obs.base + g_obs.profile->ppRva);
    in.bbH = ppHeight(g_obs.base + g_obs.profile->ppRva);
    in.borderless = *reinterpret_cast<unsigned char*>(
        g_obs.base + g_obs.profile->borderlessRva);
    long newW = 0, newH = 0;
    if (evaluateResize(&g_resizeState, in, &newW, &newH) != ResizeAction::Reset) {
        return;
    }
    DM_LOG("resize: client %ldx%ld stable; backbuffer %ldx%ld -> %ldx%ld",
           in.clientW, in.clientH, in.bbW, in.bbH, newW, newH);

    unsigned char* pp = g_obs.base + g_obs.profile->ppRva;
    unsigned char fallback[kPp_Size];
    ppSave(fallback, pp);
    *reinterpret_cast<long*>(pp) = newW;
    *reinterpret_cast<long*>(pp + 4) = newH;
    ppApplyVSync(pp, g_fsVSync);
    if (!resetSequence(fallback, "resize")) {
        g_resizeEnabled = false;  // foreign device resources; stop attempting
        g_skipThisFlip = 1;
    }
}

extern "C" __declspec(naked) void dmFlipEntryShim() {
    __asm {
        call dmFlipEntryWork
        cmp dword ptr [g_skipThisFlip], 0
        jne skip_frame
        jmp dword ptr [g_entryTrampolinePtr]
    skip_frame:
        mov dword ptr [g_skipThisFlip], 0
        xor eax, eax        ; DD_OK
        ret 0xC             ; return from Flip(this, arg2, arg3) directly
    }
}

extern "C" void __stdcall dmFinalStretchHook(IDirect3DDevice9* dev,
                                             IDirect3DSurface9* src,
                                             const RECT* srcRect,
                                             IDirect3DSurface9* dst,
                                             const RECT* dstRect,
                                             unsigned long filter) {
    ++g_obs.calls;

    // Flip-stall detector: evidence of game-loop pauses (modal move/size
    // loops, focus loss, blocking waits). Cheap: one tick read per flip.
    const DWORD nowTick = GetTickCount();
    if (g_lastFlipTick != 0 && nowTick - g_lastFlipTick > 500) {
        DM_LOG("stall: %lu ms without a flip (modal move/size loop, focus "
               "loss, or blocking wait)",
               unsigned long(nowTick - g_lastFlipTick));
    }
    g_lastFlipTick = nowTick;

    // Renderer-object globals (verified admission guarantees the layout).
    const unsigned long ppW = readU32(g_obs.base, g_obs.profile->ppRva);
    const unsigned long ppH = readU32(g_obs.base, g_obs.profile->ppRva + 4);
    const unsigned long borderless =
        *reinterpret_cast<unsigned char*>(g_obs.base + g_obs.profile->borderlessRva);
    void* deviceGlobal =
        *reinterpret_cast<void**>(g_obs.base + g_obs.profile->deviceRva);
    void* nativeGlobal =
        *reinterpret_cast<void**>(g_obs.base + g_obs.profile->nativeSurfaceRva);

    // Refresh the surface description cache when the surfaces change.
    if (!g_obs.haveDescs || src != g_obs.prevSrc || dst != g_obs.prevDst) {
        D3DSURFACE_DESC sd = {}, dd = {};
        bool ok = true;
        if (src && SUCCEEDED(src->GetDesc(&sd)) && sd.Width && sd.Height) {
            g_obs.srcW = sd.Width;
            g_obs.srcH = sd.Height;
        } else {
            ok = false;
        }
        if (dst && SUCCEEDED(dst->GetDesc(&dd)) && dd.Width && dd.Height) {
            g_obs.bbW = dd.Width;
            g_obs.bbH = dd.Height;
        } else {
            ok = false;
        }
        g_obs.haveDescs = ok;
        if (ok && dm::log::enabled()) {
            DM_LOG("surfaces: native %ux%u, backbuffer %ux%u (src %p dst %p)",
                   g_obs.srcW, g_obs.srcH, g_obs.bbW, g_obs.bbH, src, dst);
        }
        if (ok && !shadersActive()) {
            // Lazily create the shader pipeline once real dims are known.
            IDirect3DDevice9* dev0 = reinterpret_cast<IDirect3DDevice9*>(
                deviceGlobal);
            if (dev0 && !shadersCreateResources(dev0, int(g_obs.srcW),
                                                int(g_obs.srcH))) {
                // Configure() said a filter was wanted but creation failed;
                // shadersConfigure is only called with None afterwards.
            }
        }
    }

    const bool rectNull = dstRect == nullptr;
    const bool rectSame = rectNull == g_obs.prevRectNull &&
                          (rectNull || (dstRect->left == g_obs.prevRect.left &&
                                        dstRect->top == g_obs.prevRect.top &&
                                        dstRect->right == g_obs.prevRect.right &&
                                        dstRect->bottom == g_obs.prevRect.bottom));
    const bool changed =
        !g_obs.hasPrev || src != g_obs.prevSrc || dst != g_obs.prevDst ||
        filter != g_obs.prevFilter || ppW != g_obs.prevPpW || ppH != g_obs.prevPpH ||
        borderless != g_obs.prevBorderless || deviceGlobal != g_obs.prevDevice ||
        nativeGlobal != g_obs.prevNative || !rectSame;
    const bool due = g_obs.calls <= unsigned(g_obs.config.logFirst) ||
                     (g_obs.config.logEvery > 0 &&
                      g_obs.calls % unsigned(g_obs.config.logEvery) == 0);

    if ((changed || due) && dm::log::enabled()) {
        char rectText[80];
        if (rectNull) {
            dm::formatString(rectText, sizeof(rectText), "%s", "NULL(full)");
        } else {
            dm::formatString(rectText, sizeof(rectText),
                             "(%ld,%ld,%ld,%ld) %ldx%ld", dstRect->left,
                             dstRect->top, dstRect->right, dstRect->bottom,
                             dstRect->right - dstRect->left,
                             dstRect->bottom - dstRect->top);
        }
        DM_LOG("flip #%u: dstRect=%s filter=%lu pp=%lux%lu borderless=%lu%s",
               g_obs.calls, rectText, filter, ppW, ppH, borderless,
               changed ? " <changed>" : "");
        if (g_obs.hasPrev &&
            (nativeGlobal != g_obs.prevNative || deviceGlobal != g_obs.prevDevice)) {
            DM_LOG("flip #%u: native/device pointer changed (post-reset "
                   "recreation)",
                   g_obs.calls);
        }
    }

    g_obs.hasPrev = true;
    g_obs.prevSrc = src;
    g_obs.prevDst = dst;
    g_obs.prevFilter = filter;
    g_obs.prevPpW = ppW;
    g_obs.prevPpH = ppH;
    g_obs.prevBorderless = borderless;
    g_obs.prevDevice = deviceGlobal;
    g_obs.prevNative = nativeGlobal;
    g_obs.prevRectNull = rectNull;
    if (!rectNull) g_obs.prevRect = *dstRect;

    // ---- Forwarding: adaptation or native passthrough ----
    if (!g_obs.adapt.active || !g_obs.haveDescs) {
        dev->StretchRect(src, srcRect, dst, dstRect,
                         static_cast<D3DTEXTUREFILTERTYPE>(filter));
        return;
    }

    long outW = 0, outH = 0;
    if (!computeOutputSize(int(g_obs.srcW), int(g_obs.srcH), int(g_obs.bbW),
                           int(g_obs.bbH), g_obs.adapt.mode,
                           g_obs.adapt.integerScale, g_obs.adapt.customWidth,
                           g_obs.adapt.customHeight, &outW, &outH)) {
        dev->StretchRect(src, srcRect, dst, dstRect,
                         static_cast<D3DTEXTUREFILTERTYPE>(filter));
        return;
    }

    long left = 0, top = 0;
    centerRect(g_obs.bbW, g_obs.bbH, outW, outH, &left, &top);
    RECT outRect = {left, top, left + outW, top + outH};

    const unsigned long effectiveFilter =
        g_obs.adapt.filterOverride != 0 ? g_obs.adapt.filterOverride : filter;

    if ((changed || due) && dm::log::enabled()) {
        DM_LOG("adapt: -> (%ld,%ld,%ld,%ld) %ldx%ld filter=%lu (was %lu)",
               outRect.left, outRect.top, outRect.right, outRect.bottom, outW,
               outH, effectiveFilter, filter);
    }

    // Fill the letterbox border when the image does not cover the output.
    if (outW < long(g_obs.bbW) || outH < long(g_obs.bbH)) {
        dev->ColorFill(dst, nullptr,
                       static_cast<D3DCOLOR>(g_obs.adapt.backgroundColor));
    }

    if (shadersActive() &&
        shadersDrawUpscaled(dev, src, dst, &outRect)) {
        return;
    }
    dev->StretchRect(src, srcRect, dst, &outRect,
                     static_cast<D3DTEXTUREFILTERTYPE>(effectiveFilter));
}

}  // namespace dm
