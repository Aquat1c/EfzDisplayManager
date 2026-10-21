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

// Output-geometry adaptation: computed centered destination rectangle,
// optional filter override (0 keeps the wrapper's filter), and border fill
// when the image does not cover the output. Call before hook installation.
void setAdaptConfig(ScaleMode mode, int integerScale, int customWidth,
                    int customHeight, unsigned long filterOverride,
                    unsigned long backgroundColor);

// Window-resize support: arms the Flip-entry resize evaluation.
void setResizeConfig(bool enabled);
// Lifecycle is independent of resize/fullscreen configuration.
void setResetLifecycle(bool enabled);
void setAltEnterOwned(bool owned);

// Exclusive fullscreen support: arms the F11 toggle, the VSync choice
// (-1 keep, 0 immediate, 1 vsync) applied at every reset we perform, and
// optionally requests entering exclusive at startup.
void setFullscreenConfig(bool enabled, bool hotkey, long vsync,
                         bool requestEnter, bool useBorderless);

// Trampoline storage for the Flip-entry detour (filled by
// installEntryDetour before the entry site is patched).
unsigned char* entryTrampolineBuffer();
size_t entryTrampolineCapacity();
bool prepareEntryTrampoline();

// Observation state of the final-blit hook, for reporting at shutdown.
unsigned long hookCallCount();

// The final-blit hook, installed over the wrapper's shared StretchRect call
// site with an E8 encoding; __stdcall with the same 6 stack arguments
// reproduces the original stack effect. Forwards unchanged without
// adaptation config; with it, replaces destination rectangle/filter and
// fills borders, leaving source, backbuffer, and presentation to the wrapper.
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
extern "C" long dmFlipEntryWork();
extern "C" void dmFlipEntryShim();  // defined __declspec(naked) in hook_flip.cpp
