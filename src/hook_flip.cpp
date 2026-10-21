#include "hook_flip.h"
#include <windows.h>
#include <d3d9.h>
#include <intrin.h>
#include "format.h"
#include "fullscreen.h"
#include "geometry.h"
#include "logging.h"
#include "renderer_reset.h"
#include "resize.h"
#include "shaders.h"

extern "C" void* g_entryTrampolinePtr = nullptr;
extern "C" unsigned long g_dmUseFxsave = 0;

namespace dm {
namespace {
bool g_resizeEnabled = false;
bool g_lifecycleEnabled = false;
ResizeState g_resizeState;
ResizeState g_resizeBefore;
DWORD g_lastFlipTick = 0;
unsigned long g_lastGeneration = 0;

struct AdaptConfig {
    bool active;
    ScaleMode mode;
    int integerScale;
    int customWidth;
    int customHeight;
    unsigned long filterOverride;
    unsigned long backgroundColor;
};
struct ObsState {
    unsigned char* base = nullptr;
    const DdrawProfile* profile = nullptr;
    HookConfig config = {8, 600};
    AdaptConfig adapt;
    unsigned long calls = 0;
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
    bool haveDescs = false;
    unsigned long srcW = 0, srcH = 0, bbW = 0, bbH = 0;
};
ObsState g_obs;
unsigned long readU32(unsigned char* base, uint32_t rva) {
    return *reinterpret_cast<unsigned long*>(base + rva);
}

bool g_fsEnabled = false;
bool g_fsHotkey = true;
bool g_altEnterOwned = false;
long g_fsVSync = -1;
bool g_fsExclusive = false;
bool g_fsBorderless = false;
bool g_fsUseBorderless = false;
bool g_altEnterWasDown = false;
volatile LONG g_fsPending = 0;
bool g_fsKeyWasDown = false;
unsigned char g_fsSavedPp[kPp_Size];
struct WindowFrame {
    LONG_PTR style, exStyle;
    RECT rect;
    bool valid;
};
WindowFrame g_windowedFrame = {}, g_beforeFrame = {};
enum class WindowOperation { None, EnterExclusive, EnterBorderless, Exit, Resize };
WindowOperation g_operation = WindowOperation::None;
HWND g_operationWindow = nullptr;

WindowFrame captureFrame(HWND hwnd) {
    WindowFrame f = {};
    f.style = GetWindowLongPtrA(hwnd, GWL_STYLE);
    f.exStyle = GetWindowLongPtrA(hwnd, GWL_EXSTYLE);
    f.valid = GetWindowRect(hwnd, &f.rect) != FALSE;
    return f;
}
void restoreFrame(HWND hwnd, const WindowFrame& f) {
    if (!f.valid) return;
    SetWindowLongPtrA(hwnd, GWL_STYLE, f.style);
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, f.exStyle);
    SetWindowPos(hwnd, nullptr, f.rect.left, f.rect.top,
        f.rect.right - f.rect.left, f.rect.bottom - f.rect.top,
        SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

bool adapterDesktopModeForHwnd(IDirect3DDevice9* dev, HWND hwnd, DisplayMode* out) {
    D3DDEVICE_CREATION_PARAMETERS creation = {};
    if (FAILED(dev->GetCreationParameters(&creation))) return false;
    IDirect3D9* d3d = nullptr;
    if (FAILED(dev->GetDirect3D(&d3d)) || !d3d) return false;
    const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
    D3DDISPLAYMODE mode = {};
    const bool ok = d3d->GetAdapterMonitor(creation.AdapterOrdinal) == mon &&
        SUCCEEDED(d3d->GetAdapterDisplayMode(creation.AdapterOrdinal, &mode)) && mode.Width && mode.Height;
    if (ok) { out->w = mode.Width; out->h = mode.Height; out->refresh = mode.RefreshRate; out->format = mode.Format; }
    d3d->Release();
    return ok;
}

void completeWindowOperation(ResetOutcome result) {
    if (g_operation == WindowOperation::None || result == ResetOutcome::Pending) return;
    const bool accepted = result == ResetOutcome::RequestedReady;
    if (g_operation == WindowOperation::Resize) {
        if (!accepted) {
            g_resizeState = g_resizeBefore;
            g_resizeState.candidateCount = 0;
            g_resizeState.cooldown = 60;
        }
    } else if (accepted) {
        if (g_operation == WindowOperation::Exit) {
            // Keep the fullscreen frame and saved window placement until both
            // the device and required wrapper resources are really ready.
            restoreFrame(g_operationWindow, g_windowedFrame);
            g_windowedFrame.valid = false;
            g_fsExclusive = g_fsBorderless = false;
            g_resizeState = ResizeState();
        } else {
            g_fsExclusive = g_operation == WindowOperation::EnterExclusive;
            g_fsBorderless = g_operation == WindowOperation::EnterBorderless;
        }
    } else {
        restoreFrame(g_operationWindow, g_beforeFrame);
        if (g_operation == WindowOperation::EnterExclusive || g_operation == WindowOperation::EnterBorderless)
            g_windowedFrame.valid = false;
    }
    DM_LOG("window: transition %u completed result=%u", static_cast<unsigned>(g_operation), static_cast<unsigned>(result));
    g_operation = WindowOperation::None;
}

void toggleFullscreen(IDirect3DDevice9* dev, HWND hwnd) {
    if (!rendererResetCanRequest()) return;
    unsigned char desired[kPp_Size], previous[kPp_Size];
    ppSave(previous, g_obs.base + g_obs.profile->ppRva);
    ppSave(desired, previous);
    g_beforeFrame = captureFrame(hwnd);
    if (!g_beforeFrame.valid) return;
    if (g_fsExclusive || g_fsBorderless) {
        if (!g_windowedFrame.valid) return;
        ppRestoreWindowed(desired, g_fsSavedPp);
        ppApplyVSync(desired, g_fsVSync);
        g_operation = WindowOperation::Exit;
    } else {
        const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (!mon || !GetMonitorInfoA(mon, &mi)) return;
        if (g_fsUseBorderless) {
            *reinterpret_cast<uint32_t*>(desired) = mi.rcMonitor.right - mi.rcMonitor.left;
            *reinterpret_cast<uint32_t*>(desired + 4) = mi.rcMonitor.bottom - mi.rcMonitor.top;
            ppApplyVSync(desired, g_fsVSync);
            g_operation = WindowOperation::EnterBorderless;
        } else {
            DisplayMode mode;
            if (!adapterDesktopModeForHwnd(dev, hwnd, &mode)) {
                DM_LOG("fullscreen: device adapter does not own the window monitor; refused");
                return;
            }
            ppEnterExclusive(desired, mode, reinterpret_cast<uintptr_t>(hwnd));
            if (g_fsVSync == 1) ppApplyVSync(desired, 1);
            g_operation = WindowOperation::EnterExclusive;
        }
        ppSave(g_fsSavedPp, previous);
        g_windowedFrame = g_beforeFrame;
        SetWindowLongPtrA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
        SetWindowLongPtrA(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
        SetWindowPos(hwnd, nullptr, mi.rcMonitor.left, mi.rcMonitor.top,
            mi.rcMonitor.right - mi.rcMonitor.left, mi.rcMonitor.bottom - mi.rcMonitor.top,
            SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
    g_operationWindow = hwnd;
    completeWindowOperation(rendererResetRequest(desired, previous, "window transition"));
}
}

void setHookContext(unsigned char* base, const DdrawProfile* profile, const HookConfig& config) {
    g_obs.base = base; g_obs.profile = profile; g_obs.config = config;
    rendererResetConfigure(base, profile);
}
void setAdaptConfig(ScaleMode mode, int scale, int width, int height, unsigned long filter, unsigned long background) {
    g_obs.adapt = {true, mode, scale, width, height, filter >= 100 ? D3DTEXF_LINEAR : filter, background};
}
void setResizeConfig(bool enabled) { g_resizeEnabled = enabled; }
void setResetLifecycle(bool enabled) { g_lifecycleEnabled = enabled; rendererResetEnable(enabled); }
void setAltEnterOwned(bool owned) { g_altEnterOwned = owned; }
void setFullscreenConfig(bool enabled, bool hotkey, long vsync, bool enter, bool borderless) {
    g_fsEnabled = enabled; g_fsHotkey = hotkey; g_fsVSync = vsync; g_fsUseBorderless = borderless;
    if (enabled && enter) InterlockedExchange(&g_fsPending, 1);
}

bool prepareEntryTrampoline() {
    int cpu[4] = {};
    __cpuid(cpu, 1);
    const bool fxsr = (cpu[3] & (1 << 24)) != 0;
    const bool sse = (cpu[3] & (1 << 25)) != 0;
    const bool osSse = IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE) != 0;
    if (osSse && (!fxsr || !sse)) return false;
    g_dmUseFxsave = osSse && fxsr && sse ? 1 : 0;
    if (!g_entryTrampolinePtr)
        g_entryTrampolinePtr = VirtualAlloc(nullptr, 4096, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    return g_entryTrampolinePtr != nullptr;
}
unsigned char* entryTrampolineBuffer() { return static_cast<unsigned char*>(g_entryTrampolinePtr); }
size_t entryTrampolineCapacity() { return 16; }
unsigned long hookCallCount() { return g_obs.calls; }

extern "C" long dmFlipEntryWork() {
    if (!g_lifecycleEnabled || !g_obs.base || !g_obs.profile) return 0;
    auto* dev = *reinterpret_cast<IDirect3DDevice9**>(g_obs.base + g_obs.profile->deviceRva);
    HWND hwnd = *reinterpret_cast<HWND*>(g_obs.base + g_obs.profile->hwndRva);
    if (!dev) return 1;
    // Reset dispatches window messages and can reenter this function. Do not
    // consume hotkeys, call arbitrary D3D methods or open the native Flip then.
    if (rendererResetBusy()) {
        if (!rendererResetFrame(dev)) return 1;
    } else if (!rendererResetFrame(dev)) return 1;
    const unsigned long generation = rendererResetGeneration();
    if (generation != g_lastGeneration) {
        g_lastGeneration = generation;
        g_obs.haveDescs = false;
        g_obs.hasPrev = false;
    }
    completeWindowOperation(rendererResetLastOutcome());
    if (g_fsEnabled && g_fsHotkey) {
        const bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        const bool altEnter = g_altEnterOwned && (GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState(VK_RETURN) & 0x8000);
        if ((f11 && !g_fsKeyWasDown) || (altEnter && !g_altEnterWasDown)) InterlockedExchange(&g_fsPending, 1);
        g_fsKeyWasDown = f11; g_altEnterWasDown = altEnter;
    }
    if (g_fsEnabled && hwnd && InterlockedExchange(&g_fsPending, 0)) {
        toggleFullscreen(dev, hwnd);
        return 1; // even rollback succeeds: never use frame-local pre-reset state
    }
    if (!g_resizeEnabled || g_fsExclusive || g_fsBorderless || !hwnd || !rendererResetCanRequest()) return 0;
    RECT rc;
    if (!GetClientRect(hwnd, &rc)) return 0;
    const unsigned char* pp = g_obs.base + g_obs.profile->ppRva;
    ResizeInput input = {rc.right - rc.left, rc.bottom - rc.top, ppWidth(pp), ppHeight(pp),
        *reinterpret_cast<unsigned char*>(g_obs.base + g_obs.profile->borderlessRva)};
    long width = 0, height = 0;
    g_resizeBefore = g_resizeState;
    if (evaluateResize(&g_resizeState, input, &width, &height) != ResizeAction::Reset) return 0;
    unsigned char desired[kPp_Size], previous[kPp_Size];
    ppSave(previous, pp); ppSave(desired, pp);
    *reinterpret_cast<uint32_t*>(desired) = static_cast<uint32_t>(width);
    *reinterpret_cast<uint32_t*>(desired + 4) = static_cast<uint32_t>(height);
    ppApplyVSync(desired, g_fsVSync);
    g_operation = WindowOperation::Resize;
    completeWindowOperation(rendererResetRequest(desired, previous, "resize"));
    return 1;
}

extern "C" __declspec(naked) void dmFlipEntryShim() {
    __asm {
        pushfd
        pushad
        mov ebp, esp
        sub esp, 528
        and esp, -16
        cmp dword ptr [g_dmUseFxsave], 0
        je save_x87
        fxsave [esp]
        jmp fp_saved
    save_x87:
        fnsave [esp]
        fldcw word ptr [esp]
    fp_saved:
        cld
        call dmFlipEntryWork
        mov edx, eax
        cmp dword ptr [g_dmUseFxsave], 0
        je restore_x87
        fxrstor [esp]
        jmp fp_restored
    restore_x87:
        frstor [esp]
    fp_restored:
        mov esp, ebp
        test edx, edx
        jne skip_frame
        popad
        popfd
        jmp dword ptr [g_entryTrampolinePtr]
    skip_frame:
        popad
        popfd
        mov eax, 0
        ret 0xC
    }
}

extern "C" void __stdcall dmFinalStretchHook(IDirect3DDevice9* dev,
                                             IDirect3DSurface9* src,
                                             const RECT* srcRect,
                                             IDirect3DSurface9* dst,
                                             const RECT* dstRect,
                                             unsigned long filter) {
    if (rendererResetBusy()) return;
    ++g_obs.calls;

    // Flip-stall detector: modal move/size loops, focus loss, blocking waits.
    const DWORD nowTick = GetTickCount();
    if (g_lastFlipTick != 0 && nowTick - g_lastFlipTick > 500) {
        DM_LOG("stall: %lu ms without a flip (modal move/size loop, focus "
               "loss, or blocking wait)",
               static_cast<unsigned long>(nowTick - g_lastFlipTick));
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
        if (ok && g_lifecycleEnabled && !shadersActive()) {
            IDirect3DDevice9* dev0 = reinterpret_cast<IDirect3DDevice9*>(
                deviceGlobal);
            if (dev0 && !shadersCreateResources(dev0, int(g_obs.srcW),
                                                int(g_obs.srcH))) {
                // Creation failed; shadersConfigure is only ever called with
                // None afterwards, so nothing else to do here.
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

    if (shadersActive()) {
        if (shadersDrawUpscaled(dev, src, dst, &outRect)) return;
        // A failed final guard bind can let an EndScene overlay touch the
        // backbuffer. Redraw bars too before replacing the failed shader pass.
        if (outW < long(g_obs.bbW) || outH < long(g_obs.bbH)) {
            dev->ColorFill(dst, nullptr, static_cast<D3DCOLOR>(g_obs.adapt.backgroundColor));
        }
    }
    dev->StretchRect(src, srcRect, dst, &outRect,
                     static_cast<D3DTEXTUREFILTERTYPE>(effectiveFilter));
}

}  // namespace dm
