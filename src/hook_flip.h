#pragma once

#include <windows.h>

#include <cstdint>

#include "geometry.h"
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

// Enables output-geometry adaptation: every forwarded blit uses a computed
// centered destination rectangle (see geometry.h), an optional filter
// override (0 keeps the wrapper's filter), and border fill when the image
// does not cover the output. Call before hook installation.
void setAdaptConfig(ScaleMode mode, int integerScale, int customWidth,
                    int customHeight, unsigned long filterOverride,
                    unsigned long backgroundColor);

// Window-resize support: arms the Flip-entry resize evaluation.
void setResizeConfig(bool enabled);

// Trampoline storage for the Flip-entry detour (filled by
// installEntryDetour before the entry site is patched).
unsigned char* entryTrampolineBuffer();
size_t entryTrampolineCapacity();
void prepareEntryTrampoline();

// Observation state of the final-blit hook, for reporting at shutdown.
unsigned long hookCallCount();

// The final-blit hook. Installed over the wrapper's shared final StretchRect
// call site (a 6-byte `call [eax+0x88]`) with an E8 call encoding;
// __stdcall with the same 6 stack arguments reproduces the original stack
// effect exactly. Without adaptation config it forwards every call
// unchanged; with adaptation it replaces the destination rectangle/filter
// and fills borders, leaving source, backbuffer, and presentation to the
// wrapper.
extern "C" void __stdcall dmFinalStretchHook(IDirect3DDevice9* dev,
                                             IDirect3DSurface9* src,
                                             const RECT* srcRect,
                                             IDirect3DSurface9* dst,
                                             const RECT* dstRect,
                                             unsigned long filter);

}  // namespace dm

// Flip-entry hook (global scope: the naked shim cannot live in a namespace).
// dmFlipEntryWork evaluates window-size changes and performs a font-safe
// device reset through the wrapper's own globals when needed; dmFlipEntryShim
// tail-jumps into the trampoline so Flip's prologue continues normally.
extern "C" void dmFlipEntryWork();
extern "C" void dmFlipEntryShim();  // defined __declspec(naked) in hook_flip.cpp
