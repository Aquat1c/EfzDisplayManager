#pragma once
#include <windows.h>
#include <cstdint>

// Identical ABI in DisplayManager, efz-training-mode and InGameNetplay.
// Export the query under its undecorated name on Win32 x86.
struct EfzOverlayResetApiV1 {
    uint32_t size;
    uint32_t version;
    HRESULT (__cdecl* beforeReset)(void* device, uint32_t epoch);
    HRESULT (__cdecl* afterReset)(void* device, uint32_t epoch, uint32_t ready);
};
using EfzQueryOverlayResetApiV1 = const EfzOverlayResetApiV1* (__cdecl*)(void);
static_assert(sizeof(EfzOverlayResetApiV1) == 16, "The reset ABI requires Win32 x86");
