// Stage A bootstrap: worker thread that waits for the Revival wrapper,
// admits the exact renderer-split J package, and installs the forward-only
// observation hook at the wrapper's shared final StretchRect call site.
//
// Everything here runs on a dedicated thread created from DllMain: the mod
// loader LoadLibrary's mods inside its own DllMain under loader lock, so our
// DllMain must do nothing but DisableThreadLibraryCalls + CreateThread.

#include <windows.h>

#include "config.h"
#include "detour.h"
#include "hook_flip.h"
#include "logging.h"
#include "profiles.h"
#include "hook_flip.h"
#include "sha256.h"
#include "version.h"

namespace dm {

namespace {

unsigned char* g_hookSite = nullptr;

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
    const DdrawProfile* candidates[] = {&splitJProfile(), &originalJProfile()};
    for (int c = 0; c < 2 && !profile; ++c) {
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

    // 4. Install the observation/adaptation hook.
    HookConfig hc;
    hc.logFirst = cfg.logFirst;
    hc.logEvery = cfg.logEvery;
    setHookContext(base, profile, hc);
    setAdaptConfig(cfg.mode, cfg.integerScale, cfg.customWidth, cfg.customHeight,
                   cfg.filter, cfg.backgroundColor);
    unsigned char* site = base + profile->finalStretchRva;
    if (!installDetour(site, profile->hookBytes, sizeof(profile->hookBytes),
                       reinterpret_cast<const void*>(&dmFinalStretchHook),
                       /*useCall=*/true)) {
        DM_LOG("bootstrap: hook installation failed; staying inert");
        return 0;
    }
    g_hookSite = site;

    // Window-resize support: a Flip-entry detour is the only between-frames
    // point where a voluntary reset is safe (no backbuffer reference held,
    // and every surface pointer is reloaded from globals afterward, so the
    // resizing frame renders normally at the new size).
    bool deviceResourceModLoaded = false;
    if (cfg.resizeBackBuffer) {
        // Mods with D3D9 default-pool resources (ImGui buffers) make
        // IDirect3DDevice9::Reset return D3DERR_INVALIDCALL by design; a
        // backbuffer resize is impossible until those mods release their
        // resources around resets. Gate off instead of attempting it.
        static const char* kDeviceResourceMods[] = {
            "efz_training_mode.dll", "efz_netplay_mod.dll",
            "ResetProbe.dll"  // in-tree coexistence test fixture
        };
        for (int m = 0; m < 3; ++m) {
            if (GetModuleHandleA(kDeviceResourceMods[m])) {
                deviceResourceModLoaded = true;
                DM_LOG("coexist: %s is loaded and holds D3D9 device "
                       "resources; backbuffer resize disabled for this "
                       "session (maximize falls back to the DWM stretch). "
                       "Reset-aware resource handling in that mod would "
                       "enable resizing alongside it.",
                       kDeviceResourceMods[m]);
            }
        }
    }
    setResizeConfig(cfg.resizeBackBuffer != 0 && !deviceResourceModLoaded);
    if (cfg.fullscreen && deviceResourceModLoaded) {
        DM_LOG("coexist: exclusive fullscreen also disabled (device-resource "
               "mod loaded)");
    }
    setFullscreenConfig(!deviceResourceModLoaded, cfg.fullscreenHotkey != 0,
                        cfg.vsync, cfg.fullscreen != 0);
    if (!deviceResourceModLoaded && cfg.fullscreen) {
        DM_LOG("fullscreen: exclusive requested at startup (F11 toggles)");
    }
    if (cfg.resizeBackBuffer) {
        prepareEntryTrampoline();
        unsigned char* entrySite = base + profile->flipEntryRva;
        if (!installEntryDetour(entrySite, profile->entryBytes,
                                sizeof(profile->entryBytes),
                                reinterpret_cast<const void*>(&dmFlipEntryShim),
                                entryTrampolineBuffer(),
                                entryTrampolineCapacity())) {
            DM_LOG("bootstrap: entry hook failed; resize disabled "
                   "(final-blit hook stays active)");
            setResizeConfig(false);
        }
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

// Called from DLL_PROCESS_DETACH with lpReserved == NULL (real unload).
void shutdownFromDetach() {
    if (g_hookSite) {
        removeDetour(g_hookSite);
        DM_LOG("shutdown: hook removed after %lu observed flips", hookCallCount());
        g_hookSite = nullptr;
    }
}

}  // namespace dm
