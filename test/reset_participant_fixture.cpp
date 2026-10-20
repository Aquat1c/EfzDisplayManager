#include "reset_participant_fixture.h"
#include "../src/overlay_reset_api.h"

namespace {
DmResetFixtureState g_state = {};

#if !defined(DM_RESET_FIXTURE_LEGACY)
HRESULT __cdecl beforeReset(void* device, uint32_t epoch) {
    ++g_state.beforeCalls;
    g_state.beforeThread = GetCurrentThreadId();
    if (!device || !epoch) {
        ++g_state.violations;
        return E_INVALIDARG;
    }
    if (g_state.beforeResult != S_OK) return g_state.beforeResult;
    g_state.lastDevice = device;
    g_state.epoch = epoch;
    g_state.quiesced = 1;
    return S_OK;
}

HRESULT __cdecl afterReset(void* device, uint32_t epoch, uint32_t ready) {
    g_state.afterThread = GetCurrentThreadId();
    if (ready) ++g_state.afterReadyCalls;
    else ++g_state.afterNotReadyCalls;
    if (!g_state.quiesced || g_state.lastDevice != device ||
        g_state.epoch != epoch || !epoch) {
        ++g_state.violations;
        return E_UNEXPECTED;
    }
    if (!ready) return S_OK;
    if (g_state.afterReadyResult != S_OK) return g_state.afterReadyResult;
    g_state.quiesced = 0;
    return S_OK;
}

const EfzOverlayResetApiV1 g_api = {
    sizeof(EfzOverlayResetApiV1), 1, beforeReset, afterReset
};
#endif
}

#if defined(_MSC_VER) && defined(_M_IX86)
#pragma comment(linker, "/EXPORT:DM_ResetFixtureControl=_DM_ResetFixtureControl")
#if !defined(DM_RESET_FIXTURE_LEGACY)
#pragma comment(linker, "/EXPORT:EFZ_QueryOverlayResetApiV1=_EFZ_QueryOverlayResetApiV1")
#endif
#endif

extern "C" __declspec(dllexport) DmResetFixtureState* __cdecl DM_ResetFixtureControl() {
    return &g_state;
}

#if !defined(DM_RESET_FIXTURE_LEGACY)
extern "C" __declspec(dllexport) const EfzOverlayResetApiV1* __cdecl
EFZ_QueryOverlayResetApiV1() {
    return &g_api;
}
#endif
