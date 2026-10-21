#pragma once

#include <windows.h>
#include <cstdint>

// This control block is exported only by the test DLLs. The production bridge
// sees the ordinary overlay reset contract through the real Windows loader.
struct DmResetFixtureState {
    HRESULT beforeResult;
    HRESULT afterReadyResult;
    unsigned beforeCalls;
    unsigned afterReadyCalls;
    unsigned afterNotReadyCalls;
    unsigned quiesced;
    unsigned violations;
    void* lastDevice;
    uint32_t epoch;
    DWORD beforeThread;
    DWORD afterThread;
};

using DmResetFixtureControlFn = DmResetFixtureState* (__cdecl*)(void);
