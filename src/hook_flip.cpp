#include "hook_flip.h"
#include <windows.h>
#include <d3d9.h>
#include <intrin.h>
#include "format.h"
#include "compat.h"
#include "detour.h"
#include "fullscreen.h"
#include "flip_model.h"
#include "upload_trace.h"
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
unsigned long g_fsRefresh = 0;
bool g_fsExclusive = false;
bool g_fsBorderless = false;
bool g_fsUseBorderless = false;
bool g_altEnterWasDown = false;
volatile LONG g_fsPending = 0;
bool g_fsKeyWasDown = false;
bool g_inTransition = false;
bool g_hideCursor = true;
volatile LONG g_presentArmed = 0;
unsigned char g_fsSavedPp[kPp_Size];
struct WindowFrame {
    LONG_PTR style, exStyle;
    WINDOWPLACEMENT placement;  // keeps the maximized state and restore size
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
    f.placement.length = sizeof(f.placement);
    f.valid = GetWindowPlacement(hwnd, &f.placement) != FALSE;
    return f;
}
// D3D9 moves the window into the TOPMOST band on any exclusive attempt (even
// a failed Reset) and leaves it there, and WS_EX_TOPMOST only changes through
// SetWindowPos; otherwise an unfocused window floats above everything.
void restoreFrame(HWND hwnd, const WindowFrame& f) {
    if (!f.valid) return;
    SetWindowLongPtrA(hwnd, GWL_STYLE, f.style);
    SetWindowLongPtrA(hwnd, GWL_EXSTYLE, f.exStyle & ~WS_EX_TOPMOST);
    SetWindowPlacement(hwnd, &f.placement);
    SetWindowPos(hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
}

void completeWindowOperation(ResetOutcome result) {
    if (g_operation == WindowOperation::None || result == ResetOutcome::Pending) return;
    const bool accepted = result == ResetOutcome::RequestedReady;
    if (g_operation == WindowOperation::Resize) {
        if (!accepted) {
            g_resizeState = g_resizeBefore;
            g_resizeState.candidateCount = 0;
            // A refusal (overlay without the reset contract) retries rarely.
            g_resizeState.cooldown = result == ResetOutcome::Refused ? 600 : 60;
        }
    } else if (accepted) {
        if (g_operation == WindowOperation::Exit) {
            // Restore the window frame only once device and wrapper resources are ready.
            restoreFrame(g_operationWindow, g_windowedFrame);
            g_windowedFrame.valid = false;
            g_fsExclusive = g_fsBorderless = false;
            // Frame and backbuffer return unchanged, so keep the resize baseline.
            g_resizeState.candidateCount = 0;
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

// Fullscreen hides the pointer after 2 s without movement; moving the mouse
// shows it again (overlay menus stay usable). ShowCursor's counter is per
// thread, so this runs on the game thread and only ever undoes its own step.
void updateCursor() {
    static POINT last = {};
    static DWORD lastMove = 0;
    static bool hidden = false;
    POINT p = {};
    const DWORD now = GetTickCount();
    if (GetCursorPos(&p) && (p.x != last.x || p.y != last.y)) {
        last = p;
        lastMove = now;
    }
    const bool hide = g_hideCursor && (g_fsExclusive || g_fsBorderless) && now - lastMove > 2000;
    if (hide != hidden) {
        ShowCursor(hide ? FALSE : TRUE);
        hidden = hide;
    }
}

// Mandatory request, like Revival's own Alt+Enter: a contract-less overlay does
// not block it; contract owners are still quiesced and may refuse.
void toggleFullscreen(HWND hwnd) {
    if (!rendererResetCanRequest()) return;
    unsigned char desired[kPp_Size], previous[kPp_Size];
    ppSave(previous, g_obs.base + g_obs.profile->ppRva);
    ppSave(desired, previous);
    g_beforeFrame = captureFrame(hwnd);
    if (!g_beforeFrame.valid) return;
    if (g_fsExclusive || g_fsBorderless) {
        if (!g_windowedFrame.valid) return;
        ppRestoreWindowed(desired, g_fsSavedPp);
        g_operation = WindowOperation::Exit;
    } else {
        const HMONITOR mon = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = {};
        mi.cbSize = sizeof(mi);
        if (!mon || !GetMonitorInfoA(mon, &mi)) return;
        // Exclusive needs the device on the window's adapter (re-created there if
        // needed); anything else falls back to borderless.
        bool exclusiveNow = false;
        DisplayMode mode = {};
        unsigned long ordinal = 0;
        if (!g_fsUseBorderless) {
            if (!rendererResetExclusiveTarget(hwnd, g_fsRefresh, &ordinal, &mode, g_fsVSync != 1)) {
                DM_LOG("fullscreen: exclusive not possible on the window's monitor; entering borderless");
            } else if (ordinal == rendererResetDeviceAdapter()) {
                exclusiveNow = true;
            } else if (compatAnyLoaded()) {
                // The v1 overlay contract cannot hand its owners a replacement device.
                DM_LOG("fullscreen: moving the device to adapter %lu needs overlay mods unloaded; entering borderless",
                       ordinal);
            } else {
                rendererResetSwapAdapter(ordinal);
                exclusiveNow = true;
                DM_LOG("fullscreen: reacquiring device on adapter %lu for exclusive mode", ordinal);
            }
            if (exclusiveNow)
                DM_LOG("fullscreen: exclusive %ldx%ld@%ld", mode.w, mode.h, mode.refresh);
        }
        if (exclusiveNow) {
            ppEnterExclusive(desired, mode, reinterpret_cast<uintptr_t>(hwnd));
            if (g_fsVSync == 1) ppApplyVSync(desired, 1);
            g_operation = WindowOperation::EnterExclusive;
        } else {
            *reinterpret_cast<uint32_t*>(desired) = mi.rcMonitor.right - mi.rcMonitor.left;
            *reinterpret_cast<uint32_t*>(desired + 4) = mi.rcMonitor.bottom - mi.rcMonitor.top;
            g_operation = WindowOperation::EnterBorderless;
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
    const bool wantedExclusive = g_operation == WindowOperation::EnterExclusive;
    ResetOutcome outcome = rendererResetRequest(desired, previous, "window transition", true);
    completeWindowOperation(outcome);
    if (wantedExclusive && outcome == ResetOutcome::RestoredReady) {
        // Exclusive was rolled back (some drivers refuse D3D9 exclusive on an
        // output); land in borderless instead of a dead hotkey.
        DM_LOG("fullscreen: exclusive refused by the device; entering borderless instead");
        HMONITOR mon2 = MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi2 = {};
        mi2.cbSize = sizeof(mi2);
        WindowFrame before2 = captureFrame(hwnd);
        if (mon2 && GetMonitorInfoA(mon2, &mi2) && before2.valid) {
            ppSave(desired, previous);
            *reinterpret_cast<uint32_t*>(desired) = mi2.rcMonitor.right - mi2.rcMonitor.left;
            *reinterpret_cast<uint32_t*>(desired + 4) = mi2.rcMonitor.bottom - mi2.rcMonitor.top;
            ppSave(g_fsSavedPp, previous);
            g_beforeFrame = before2;
            g_windowedFrame = before2;
            g_operation = WindowOperation::EnterBorderless;
            SetWindowLongPtrA(hwnd, GWL_STYLE, WS_POPUP | WS_VISIBLE);
            SetWindowLongPtrA(hwnd, GWL_EXSTYLE, WS_EX_APPWINDOW);
            SetWindowPos(hwnd, nullptr, mi2.rcMonitor.left, mi2.rcMonitor.top,
                mi2.rcMonitor.right - mi2.rcMonitor.left, mi2.rcMonitor.bottom - mi2.rcMonitor.top,
                SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
            completeWindowOperation(rendererResetRequest(desired, previous, "window transition", true));
        }
    }
}

// Flip model only (LowLatencyWindow=1): windowed presents use
// D3DPRESENT_DONOTWAIT so a frame with no refresh left is dropped instead of
// stalling the game. D3D9Ex success codes (occluded, mode changed) are returned
// as D3D_OK because the wrapper Resets on any other value.
HRESULT presentFrame(IDirect3DDevice9* dev, const RECT* src, const RECT* dst, HWND window,
                     const RGNDATA* dirty) {
    HRESULT hr;
    IDirect3DSwapChain9* chain = nullptr;
    uploadTraceMark(PresentBegin);
    const bool windowedFlip = flipModelActive() && readU32(g_obs.base, g_obs.profile->ppRva + kPp_Windowed);
    if (windowedFlip && SUCCEEDED(dev->GetSwapChain(0, &chain))) {
        hr = chain->Present(src, dst, window, dirty, D3DPRESENT_DONOTWAIT);
        chain->Release();

    } else {
        hr = dev->Present(src, dst, window, dirty);
    }
    uploadTracePresent(hr);
    if (windowedFlip && hr == D3DERR_WASSTILLDRAWING) hr = D3D_OK;
    return SUCCEEDED(hr) ? D3D_OK : hr;
}

// Runs on the render thread at Flip entry, so no thread can be inside the
// rewritten call (a MinGW site's return address lies within the patch).
void installPresentHook() {
    unsigned char* site = g_obs.base + g_obs.profile->presentRva;
    unsigned char patch[6];
    if (g_obs.profile->presentPushStyle) {
        // `push 0; push edi; call [eax+44h]` -> `push edi; call hook`: the
        // NULL source rect is dropped and the stdcall hook pops 4 arguments.
        patch[0] = 0x57;
        encodeRel32(patch + 1, 0xE8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site + 1)),
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&dmPresentPushHook)));
    } else {
        // `call [eax+44h]; sub esp,14h` -> `call hook; nop`: the cdecl hook
        // leaves the 5 stored arguments, just as the sub re-reserved them.
        encodeRel32(patch, 0xE8, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(site)),
                    static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&dmPresentMovHook)));
        patch[5] = 0x90;
    }
    if (!installPatch(site, g_obs.profile->presentBytes, sizeof(patch), patch))
        DM_LOG("present: hook unavailable; presentation tracing/flip-model handling disabled");
}
}

extern "C" HRESULT __stdcall dmPresentPushHook(IDirect3DDevice9* dev, const RECT* dst, HWND window,
                                               const RGNDATA* dirty) {
    return presentFrame(dev, nullptr, dst, window, dirty);
}
extern "C" HRESULT __cdecl dmPresentMovHook(IDirect3DDevice9* dev, const RECT* src, const RECT* dst,
                                            HWND window, const RGNDATA* dirty) {
    return presentFrame(dev, src, dst, window, dirty);
}
void armPresentHook() { InterlockedExchange(&g_presentArmed, 1); }

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
void setFullscreenConfig(bool enabled, bool hotkey, long vsync, bool enter, bool borderless,
                         unsigned long refresh, bool hideCursor) {
    g_fsEnabled = enabled; g_fsHotkey = hotkey; g_fsVSync = vsync; g_fsUseBorderless = borderless;
    g_fsRefresh = refresh; g_hideCursor = hideCursor;
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

extern "C" long dmFlipEntryWork() {
    if (!g_lifecycleEnabled || !g_obs.base || !g_obs.profile) return 0;
    // Window-style calls and Reset send messages into the game's window
    // procedure, which can re-enter Flip. Skip that nested frame entirely;
    // rendererResetFrame also refuses while a Reset executes.
    if (g_inTransition) return 1;
    if (InterlockedExchange(&g_presentArmed, 0)) installPresentHook();
    uploadTraceFrameStart();
    auto* dev = *reinterpret_cast<IDirect3DDevice9**>(g_obs.base + g_obs.profile->deviceRva);
    HWND hwnd = *reinterpret_cast<HWND*>(g_obs.base + g_obs.profile->hwndRva);
    if (!dev || !rendererResetFrame(dev)) return 1;
    const unsigned long generation = rendererResetGeneration();
    if (generation != g_lastGeneration) {
        g_lastGeneration = generation;
        g_obs.haveDescs = false;
        g_obs.hasPrev = false;
    }
    completeWindowOperation(rendererResetLastOutcome());
    updateCursor();
    if (g_fsEnabled && g_fsHotkey) {
        // GetAsyncKeyState is system-wide: F11 typed into another app must not
        // toggle the game. Edges are still tracked while unfocused so a key
        // held across a focus change does not fire.
        const bool focused = hwnd && GetForegroundWindow() == hwnd;
        const bool f11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
        const bool altEnter = g_altEnterOwned && (GetAsyncKeyState(VK_MENU) & 0x8000) && (GetAsyncKeyState(VK_RETURN) & 0x8000);
        if (focused && ((f11 && !g_fsKeyWasDown) || (altEnter && !g_altEnterWasDown)))
            InterlockedExchange(&g_fsPending, 1);
        g_fsKeyWasDown = f11; g_altEnterWasDown = altEnter;
    }
    if (g_fsEnabled && hwnd && InterlockedExchange(&g_fsPending, 0)) {
        g_inTransition = true;
        toggleFullscreen(hwnd);
        g_inTransition = false;
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
    uploadTraceMark(FinalCopyBegin);
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
                // Failure leaves shaders off; nothing else to do.
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
        uploadTraceFinalCopy(dev->StretchRect(src, srcRect, dst, dstRect,
                         static_cast<D3DTEXTUREFILTERTYPE>(filter)));
        return;
    }

    long outW = 0, outH = 0;
    if (!computeOutputSize(int(g_obs.srcW), int(g_obs.srcH), int(g_obs.bbW),
                           int(g_obs.bbH), g_obs.adapt.mode,
                           g_obs.adapt.integerScale, g_obs.adapt.customWidth,
                           g_obs.adapt.customHeight, &outW, &outH)) {
        uploadTraceFinalCopy(dev->StretchRect(src, srcRect, dst, dstRect,
                         static_cast<D3DTEXTUREFILTERTYPE>(filter)));
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

    // DISCARD leaves the backbuffer undefined after Present, so the bars are
    // filled every frame, but only the bars.
    Box bars[4];
    const int barCount = letterboxBars(long(g_obs.bbW), long(g_obs.bbH),
        {outRect.left, outRect.top, outRect.right, outRect.bottom}, bars);
    auto fillBars = [&] {
        for (int i = 0; i < barCount; ++i) {
            const RECT bar = {bars[i].left, bars[i].top, bars[i].right, bars[i].bottom};
            dev->ColorFill(dst, &bar, static_cast<D3DCOLOR>(g_obs.adapt.backgroundColor));
        }
    };
    fillBars();

    if (shadersActive()) {
        if (shadersDrawUpscaled(dev, src, dst, &outRect)) {
            uploadTraceFinalCopy(S_OK);
            return;
        }
        // A failed pass may have let an EndScene overlay touch the bars; redraw them.
        fillBars();
    }
    uploadTraceFinalCopy(dev->StretchRect(src, srcRect, dst, &outRect,
                     static_cast<D3DTEXTUREFILTERTYPE>(effectiveFilter)));
}

}  // namespace dm
