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
