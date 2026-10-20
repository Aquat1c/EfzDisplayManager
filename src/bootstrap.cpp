// Stage A bootstrap: worker thread that waits for the Revival wrapper,
// admits the exact renderer-split J package, and installs the forward-only
// observation hook at the wrapper's shared final StretchRect call site.
//
// Everything here runs on a dedicated thread created from DllMain: the mod
// loader LoadLibrary's mods inside its own DllMain under loader lock, so our
// DllMain must do nothing but DisableThreadLibraryCalls + CreateThread.

#include <windows.h>

#include "compat.h"
#include "config.h"
#include "detour.h"
#include "hook_flip.h"
#include "logging.h"
#include "profiles.h"
#include "renderer_reset.h"
#include "sha256.h"
#include "shaders.h"
#include "version.h"

namespace dm {

namespace {

void logSha(const char* label, const unsigned char* hash) {
    char text[65];
    static const char hex[] = "0123456789abcdef";
    for (int i = 0; i < 32; ++i) {
        text[2 * i] = hex[hash[i] >> 4];
        text[2 * i + 1] = hex[hash[i] & 0xF];
    }
    text[64] = '\0';
    DM_LOG("%s: %s", label, text);
}

// Reads the PE timestamp of a loaded module straight from its headers.
bool loadedPeTimestamp(HMODULE mod, unsigned long* timestampOut) {
    unsigned char* base = reinterpret_cast<unsigned char*>(mod);
    if (base[0] != 'M' || base[1] != 'Z') return false;
    unsigned long eLfanew = *reinterpret_cast<unsigned long*>(base + 0x3C);
    if (eLfanew == 0 || eLfanew > 0x400) return false;
    unsigned long* sig = reinterpret_cast<unsigned long*>(base + eLfanew);
    if (*sig != 0x00004550) return false;  // "PE\0\0"
    *timestampOut = *reinterpret_cast<unsigned long*>(base + eLfanew + 8);
    return true;
}

DWORD WINAPI bootstrapThread(void* param) {
    Config cfg = loadConfig(reinterpret_cast<HMODULE>(param));
    if (!cfg.enabled) {
        DM_LOG("bootstrap: Enabled=0 in DisplayManager.ini; staying inert");
        return 0;
    }

    // 1. Wait for the Revival wrapper module (efz.exe imports DDRAW).
    HMODULE ddraw = nullptr;
    for (int i = 0; i < 480; ++i) {  // up to 120 s
        ddraw = GetModuleHandleA("DDRAW.dll");
        if (ddraw) break;
        Sleep(250);
    }
    if (!ddraw) {
        DM_LOG("admission: DDRAW.dll never loaded; staying inert");
        return 0;
    }
    char ddrawPath[MAX_PATH] = "?";
    GetModuleFileNameA(ddraw, ddrawPath, MAX_PATH);
    DM_LOG("admission: DDRAW.dll loaded at %p (%s)", ddraw, ddrawPath);

    // 2. Hash the on-disk file and admit against the pinned profiles.
    unsigned char hash[32];
    if (!sha256File(ddrawPath, hash)) {
        DM_LOG("admission: cannot read DDRAW.dll file; staying inert");
        return 0;
    }
    logSha("admission: file SHA-256", hash);

    unsigned long ts = 0;
    if (!loadedPeTimestamp(ddraw, &ts)) {
        DM_LOG("admission: cannot read loaded PE header; staying inert");
        return 0;
    }

    const DdrawProfile* profile = nullptr;
    const DdrawProfile* candidates[] = {
        &splitJProfile(),  &originalJProfile(), &rev2020Profile(),
        &rev102fProfile(), &rev102gProfile(),   &rev102hProfile(),
        &rev102iProfile(),
    };
    for (int c = 0; c < 7 && !profile; ++c) {
        bool hashOk = true;
        for (int i = 0; i < 32; ++i) {
            if (hash[i] != candidates[c]->ddrawSha256[i]) {
                hashOk = false;
                break;
            }
        }
        if (hashOk && ts == candidates[c]->peTimestamp) profile = candidates[c];
    }
    if (!profile) {
        DM_LOG("admission: unsupported wrapper (PE timestamp %08lx); staying "
               "inert - native display path remains in charge",
               ts);
        return 0;
    }
    DM_LOG("admission: profile accepted: %s (PE timestamp %08lx)", profile->name,
           ts);

    // 3. Wait until the wrapper has created its device (global +0x44).
    unsigned char* base = reinterpret_cast<unsigned char*>(ddraw);
    void* device = nullptr;
    for (int i = 0; i < 480; ++i) {
        device = *reinterpret_cast<void**>(base + profile->deviceRva);
        if (device) break;
        Sleep(250);
    }
    if (!device) {
        DM_LOG("bootstrap: wrapper device global stayed null; staying inert");
        return 0;
    }
    DM_LOG("bootstrap: wrapper device %p, native surface %p",
           device, *reinterpret_cast<void**>(base + profile->nativeSurfaceRva));

    compatConfigure(cfg.compatLayer != 0);

    // 3b. Make the window sizable and maximizable. The wrapper creates it as
    // WS_VISIBLE|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX (fixed-size, grayed
    // maximize button); add WS_THICKFRAME|WS_MAXIMIZEBOX while preserving the
    // client area, before hooks install so the resize baseline captures the
    // adjusted size.
    if (cfg.windowAdjustStyle) {
        HWND hwnd = *reinterpret_cast<HWND*>(base + profile->hwndRva);
        if (hwnd) {
            LONG_PTR style = GetWindowLongPtrA(hwnd, GWL_STYLE);
            LONG_PTR want = style | WS_MAXIMIZEBOX;
            if (cfg.windowDragResize) want |= WS_THICKFRAME;
            if (want != style) {
                RECT rc;
                GetClientRect(hwnd, &rc);
                SetWindowLongPtrA(hwnd, GWL_STYLE, want);
                AdjustWindowRect(&rc, static_cast<DWORD>(want), FALSE);
                SetWindowPos(hwnd, nullptr, 0, 0, rc.right - rc.left,
                             rc.bottom - rc.top,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOOWNERZORDER |
                                 SWP_FRAMECHANGED | SWP_NOACTIVATE);
                DM_LOG("window: style %08lx -> %08lx (maximize enabled%s, "
                       "client preserved)",
                       static_cast<unsigned long>(style),
                       static_cast<unsigned long>(want),
                       cfg.windowDragResize
                           ? " + drag-resize (pauses game during drag)"
                           : "");
            }
        }
    }

    HookConfig hc = {cfg.logFirst, cfg.logEvery};
    setHookContext(base, profile, hc);
    setAdaptConfig(cfg.mode, cfg.integerScale, cfg.customWidth, cfg.customHeight,
                   cfg.filter, cfg.backgroundColor);
    setResizeConfig(false);
    setFullscreenConfig(false, cfg.fullscreenHotkey != 0, cfg.vsync, false,
                        cfg.borderlessFullscreen != 0);

    // Inline hooks and device shadow vtables outlive individual callbacks.
    // Pin the DLL before publishing any code pointer. Hot unload is deliberately
    // unsupported; no attempt to suspend/rewrite render threads under DllMain.
    HMODULE pinned = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCSTR>(&bootstrapThread), &pinned)) {
        DM_LOG("bootstrap: could not pin hook module; staying inert");
        return 0;
    }
    const unsigned char* helperBytes = nullptr;
    size_t helperLength = 0;
    bool lifecycle = false;
    if (rendererResetSignature(&helperBytes, &helperLength) && prepareEntryTrampoline()) {
        if (installEntryDetour(base + profile->flipEntryRva, profile->entryBytes,
                                profile->entryByteCount,
                                reinterpret_cast<const void*>(&dmFlipEntryShim),
                                entryTrampolineBuffer(), entryTrampolineCapacity())) {
            lifecycle = installDetour(base + profile->resetHelperRva, helperBytes, 6,
                                       reinterpret_cast<const void*>(&dmNativeResetHook), false, helperLength);
        }
    }
    setResetLifecycle(lifecycle);
    if (!lifecycle) {
        DM_LOG("reset: complete helper/entry admission failed; resize/fullscreen/shaders disabled");
    } else {
        DM_LOG("reset: lifecycle installed (full helper match: %u bytes)", static_cast<unsigned>(helperLength));
    }

    ShaderConfig sc;
    if (lifecycle && cfg.filter == 100) sc.filter = ShaderFilter::Sharp;
    if (lifecycle && cfg.filter == 101) sc.filter = ShaderFilter::Xbr;
    sc.sharpness = float(cfg.sharpness) / 10.0f;
    shadersConfigure(sc);
    unsigned char* site = base + profile->finalStretchRva;
    if (!installDetour(site, profile->hookBytes, sizeof(profile->hookBytes),
                       reinterpret_cast<const void*>(&dmFinalStretchHook), true)) {
        DM_LOG("bootstrap: final blit hook unavailable; admitted reset lifecycle retained");
        return 0;
    }
    setResizeConfig(lifecycle && cfg.resizeBackBuffer != 0);
    setFullscreenConfig(lifecycle, cfg.fullscreenHotkey != 0, cfg.vsync, false,
                        cfg.borderlessFullscreen != 0);

    // Only replace a working native shortcut if its replacement is installed
    // and enabled. If admission fails, F11 remains independent and we do not
    // also poll Alt+Enter (two owners would race).
    if (lifecycle && cfg.fullscreenHotkey && compatCanReset()) {
        unsigned char* keySite = base + profile->altEnterCmpRva;
        if (memcmp(keySite, profile->altEnterBytes, 3) == 0) {
            DWORD oldProtect = 0;
            if (VirtualProtect(keySite, 3, PAGE_EXECUTE_READWRITE, &oldProtect)) {
                keySite[2] = 0xE6;
                FlushInstructionCache(GetCurrentProcess(), keySite, 3);
                DWORD ignored = 0;
                VirtualProtect(keySite, 3, oldProtect, &ignored);
                setAltEnterOwned(true);
                DM_LOG("window: Alt+Enter replacement installed");
            }
        }
    }
    if (lifecycle && cfg.fullscreen) {
        if (cfg.fullscreenDelayMs > 0) Sleep(cfg.fullscreenDelayMs);
        setFullscreenConfig(true, cfg.fullscreenHotkey != 0, cfg.vsync, true,
                            cfg.borderlessFullscreen != 0);
    }
    DM_LOG("bootstrap: hook active (observe + adapt: mode=%d scale=%d "
           "custom=%dx%d filter=%lu bg=%06lx)",
           int(cfg.mode), cfg.integerScale, cfg.customWidth, cfg.customHeight,
           cfg.filter, cfg.backgroundColor & 0xFFFFFFUL);
    return 0;
}

}  // namespace

void startBootstrap(HMODULE module) {
    DWORD tid = 0;
    HANDLE t = CreateThread(nullptr, 0, &bootstrapThread,
                            reinterpret_cast<void*>(module), 0, &tid);
    if (t) CloseHandle(t);
}

// Armed hooks pin the module for process lifetime. An inert module has no
// published code pointers and needs no hook removal under the loader lock.
void shutdownFromDetach() {}

}  // namespace dm
