// Bootstrap worker started from DllMain: admit the Revival wrapper package
// by SHA-256 + PE timestamp, then install the display hooks. Runs on this
// dedicated thread because the mod loader LoadLibrary's mods inside its own
// DllMain under loader lock.

#include <windows.h>

#include "compat.h"
#include "config.h"
#include "detour.h"
#include "flip_model.h"
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
    wchar_t ddrawPath[MAX_PATH];
    const DWORD pathLength = GetModuleFileNameW(ddraw, ddrawPath, MAX_PATH);
    char logPath[MAX_PATH * 3] = "?";
    if (pathLength && pathLength < MAX_PATH)
        WideCharToMultiByte(CP_UTF8, 0, ddrawPath, -1, logPath, sizeof(logPath), nullptr, nullptr);
    DM_LOG("admission: DDRAW.dll loaded at %p (%s)", ddraw, logPath);

    // 2. Hash the on-disk file and admit against the pinned profiles.
    unsigned char hash[32];
    if (pathLength == 0 || pathLength >= MAX_PATH || !sha256File(ddrawPath, hash)) {
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
    for (int c = 0; !profile && profileAt(c); ++c) {
        const DdrawProfile* candidate = profileAt(c);
        if (ts == candidate->peTimestamp && memcmp(hash, candidate->ddrawSha256, 32) == 0) profile = candidate;
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
    rendererResetUseFlipModel(flipModelActive());
    if (cfg.lowLatencyWindow && !flipModelActive())
        DM_LOG("flip model: LowLatencyWindow=1 but the device is classic (see the lines above)");

    // 3b. Make the window maximizable (and sizable), keeping the client area.
    // Runs before hooks install so the resize baseline is the new size.
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
                        cfg.borderlessFullscreen != 0, cfg.fullscreenRefresh, cfg.hideCursor != 0);

    // Inline hooks outlive individual callbacks. Pin the DLL before publishing
    // any code pointer; hot unload is deliberately unsupported.
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
                        cfg.borderlessFullscreen != 0, cfg.fullscreenRefresh, cfg.hideCursor != 0);

    // Disable Revival's Alt+Enter only when our toggle is installed and enabled
    // (two owners would race). Its toggle stretches 4:3; ours letterboxes.
    if (lifecycle && cfg.fullscreenHotkey) {
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
    if (lifecycle && flipModelActive()) armPresentHook();
    if (lifecycle && cfg.fullscreen) {
        if (cfg.fullscreenDelayMs > 0) Sleep(cfg.fullscreenDelayMs);
        setFullscreenConfig(true, cfg.fullscreenHotkey != 0, cfg.vsync, true,
                            cfg.borderlessFullscreen != 0, cfg.fullscreenRefresh, cfg.hideCursor != 0);
    }
    DM_LOG("bootstrap: hook active (observe + adapt: mode=%d scale=%d "
           "custom=%dx%d filter=%lu sharpness=%d bg=%06lx refresh=%lu cursor=%d)",
           int(cfg.mode), cfg.integerScale, cfg.customWidth, cfg.customHeight,
           cfg.filter, cfg.sharpness, cfg.backgroundColor & 0xFFFFFFUL,
           cfg.fullscreenRefresh, cfg.hideCursor);
    return 0;
}

}  // namespace

void startBootstrap(HMODULE module) {
    // The flip model must be in place before the wrapper creates its device
    // (after mods load). Under the loader lock: header reads and one import-slot
    // write, no waiting.
    const Config cfg = loadConfig(module);
    if (cfg.enabled && cfg.lowLatencyWindow) {
        HMODULE ddraw = GetModuleHandleA("DDRAW.dll");
        unsigned long ts = 0;
        const DdrawProfile* profile = nullptr;
        for (int c = 0; ddraw && loadedPeTimestamp(ddraw, &ts) && !profile && profileAt(c); ++c)
            if (profileAt(c)->peTimestamp == ts) profile = profileAt(c);
        unsigned char* base = reinterpret_cast<unsigned char*>(ddraw);
        if (!profile) {
            DM_LOG("flip model: Revival's Ddraw.dll not loaded yet or not supported; off");
        } else if (*reinterpret_cast<void**>(base + profile->deviceRva)) {
            DM_LOG("flip model: the game's display already exists (mod loaded too late); off");
        } else if (memcmp(base + profile->presentRva, profile->presentBytes, 6) != 0) {
            DM_LOG("flip model: unexpected Present call in Ddraw.dll; off");
        } else if (!flipModelRedirect(ddraw)) {
            DM_LOG("flip model: Ddraw.dll does not import Direct3DCreate9; off");
        }
    }
    DWORD tid = 0;
    HANDLE t = CreateThread(nullptr, 0, &bootstrapThread,
                            reinterpret_cast<void*>(module), 0, &tid);
    if (t) CloseHandle(t);
}

}  // namespace dm
