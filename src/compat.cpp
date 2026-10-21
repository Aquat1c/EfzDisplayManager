#include "compat.h"
#include "overlay_reset_api.h"
#include "logging.h"

namespace dm {
namespace {
// efz_truecolor.dll (EFZStageStudio) holds D3DX sprite/line objects with no
// lost-device handling; listing it blocks voluntary resets until it adopts
// the contract.
const char* const kModules[] = {
    "efz_training_mode.dll", "efz_netplay_mod.dll", "ResetProbe.dll", "efz_truecolor.dll"
};
constexpr unsigned kModuleCount = sizeof(kModules) / sizeof(kModules[0]);
struct Participant {
    HMODULE module;
    EfzOverlayResetApiV1 api;
    bool prepared;
    bool resumed;
};
Participant g_participants[kModuleCount] = {};
bool g_enabled = true;
bool g_active = false;
bool g_inCallback = false;
IDirect3DDevice9* g_device = nullptr;
uint32_t g_epoch = 0;

bool query(HMODULE module, EfzOverlayResetApiV1* out) {
    auto fn = reinterpret_cast<EfzQueryOverlayResetApiV1>(
        GetProcAddress(module, "EFZ_QueryOverlayResetApiV1"));
    if (!fn) return false;
    const EfzOverlayResetApiV1* api = fn();
    if (!api || api->size < sizeof(*out) || api->version != 1 ||
        !api->beforeReset || !api->afterReset) return false;
    *out = *api;
    return true;
}
void clear() {
    for (auto& p : g_participants) {
        if (p.module) FreeLibrary(p.module);
        p = Participant{};
    }
    g_device = nullptr;
    g_active = false;
}
}
void compatConfigure(bool enabled) { g_enabled = enabled; }
bool compatPending() { return g_active; }
bool compatAnyLoaded() {
    for (const char* name : kModules)
        if (GetModuleHandleA(name)) return true;
    return false;
}

bool compatCanReset() {
    if (g_inCallback || g_active) return false;
    for (unsigned i = 0; i < kModuleCount; ++i) {
        HMODULE module = nullptr;
        // Hold a normal reference while reading the export; a mod may unload.
        if (!GetModuleHandleExA(0, kModules[i], &module)) continue;
        EfzOverlayResetApiV1 api = {};
        const bool ok = g_enabled && query(module, &api);
        FreeLibrary(module);
        if (!ok) {
            static unsigned warned = 0;
            if (!(warned & (1u << i))) {
                DM_LOG("coexist: %s lacks the enabled reset contract; voluntary resets blocked", kModules[i]);
                warned |= 1u << i;
            }
            return false;
        }
    }
    return true;
}

bool compatBegin(IDirect3DDevice9* device, uint32_t epoch, bool mandatory) {
    if (!device || !epoch || g_inCallback) return false;
    const bool wasActive = g_active;
    if (!g_active) {
        if (!mandatory && !compatCanReset()) return false;
        g_device = device;
        g_epoch = epoch;
        g_active = true;
    } else if (g_device != device) {
        return false;
    }
    // Refresh at EVERY actual attempt, including retries after a failed Reset.
    // A newly loaded legacy module cannot bypass the owner contract.
    for (unsigned i = 0; i < kModuleCount; ++i) {
        auto& p = g_participants[i];
        if (p.module) continue;
        HMODULE module = nullptr;
        if (!GetModuleHandleExA(0, kModules[i], &module)) continue;
        EfzOverlayResetApiV1 api = {};
        if (!g_enabled || !query(module, &api)) {
            FreeLibrary(module);
            if (mandatory) {
                static unsigned warned = 0;
                if (!(warned & (1u << i))) {
                    DM_LOG("coexist: %s lacks the enabled reset contract; resetting anyway (device must recover)", kModules[i]);
                    warned |= 1u << i;
                }
                continue;
            }
            if (!wasActive) clear();
            return false;
        }
        p.module = module;
        p.api = api;
    }
    // Pending participants keep their original epoch. If one resumed before
    // another failed, quiesce that participant again before another real Reset.
    bool ok = true;
    g_inCallback = true;
    for (auto& p : g_participants) {
        if (!p.module || (p.prepared && !p.resumed)) continue;
        const HRESULT hr = p.api.beforeReset(device, g_epoch);
        if (hr != S_OK) { ok = false; break; }
        p.prepared = true;
        p.resumed = false;
    }
    g_inCallback = false;
    if (!ok) {
        DM_LOG("coexist: participant refused Reset; keeping prepared owners quiesced");
        // Only the caller knows whether Reset already happened; cancellation
        // is explicit and legal only on the still-healthy device.
    }
    return ok;
}

bool compatEnd(bool ready) {
    if (!g_active) return true;
    if (g_inCallback) return false;
    g_inCallback = true;
    bool complete = ready;
    for (auto& p : g_participants) {
        if (!p.module || !p.prepared || p.resumed) continue;
        const HRESULT hr = p.api.afterReset(g_device, g_epoch, ready ? 1u : 0u);
        if (ready && hr == S_OK) p.resumed = true;
        else if (ready) complete = false;
    }
    g_inCallback = false;
    if (complete) clear();
    return complete;
}
}
