#pragma once

#include <windows.h>

#include <cstdint>

#include "profiles.h"

struct IDirect3DDevice9;
struct IDirect3DSurface9;

namespace dm {

struct HookConfig {
    int logFirst;   // log the first N hook invocations
    int logEvery;   // then log every Nth invocation
};

// Gives the hook the verified Ddraw base and profile before installation.
void setHookContext(unsigned char* ddrawBase, const DdrawProfile* profile,
                    const HookConfig& config);

// Observation state of the final-blit hook, for reporting at shutdown.
unsigned long hookCallCount();

// The Stage A observation hook. Installed over the wrapper's shared final
// StretchRect call site (a 6-byte `call [eax+0x88]`) with an E8 call
// encoding; __stdcall with the same 6 stack arguments reproduces the
// original stack effect exactly. Forwards every call to
// IDirect3DDevice9::StretchRect unchanged; observation is throttled and
// read-only.
extern "C" void __stdcall dmFinalStretchHook(IDirect3DDevice9* dev,
                                             IDirect3DSurface9* src,
                                             const RECT* srcRect,
                                             IDirect3DSurface9* dst,
                                             const RECT* dstRect,
                                             unsigned long filter);

}  // namespace dm
