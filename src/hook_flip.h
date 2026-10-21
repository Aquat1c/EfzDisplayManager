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

// Fullscreen support: arms the F11 / Alt+Enter toggle, the exclusive VSync
// choice (1 = wait for the screen, else immediate; windowed keeps Revival's
// interval), the exclusive refresh rate (0 = desktop), idle cursor hiding,
// and optionally requests entering fullscreen at startup.
void setFullscreenConfig(bool enabled, bool hotkey, long vsync,
                         bool requestEnter, bool useBorderless,
                         unsigned long refresh, bool hideCursor);

// Flip model: asks the render thread to rewrite the wrapper's device Present
// call at its next Flip entry (see presentFrame). Call after every other
// patch is in.
void armPresentHook();

// Trampoline storage for the Flip-entry detour (filled by
// installEntryDetour before the entry site is patched).
unsigned char* entryTrampolineBuffer();
size_t entryTrampolineCapacity();
bool prepareEntryTrampoline();

// Final-blit hook, an E8 call over the wrapper's shared StretchRect site;
// __stdcall with the same 6 stack arguments keeps the original stack effect.
// With adaptation config it replaces destination rect/filter and fills borders.
extern "C" void __stdcall dmFinalStretchHook(IDirect3DDevice9* dev,
                                             IDirect3DSurface9* src,
                                             const RECT* srcRect,
                                             IDirect3DSurface9* dst,
                                             const RECT* dstRect,
                                             unsigned long filter);

// Replacements for the wrapper's device Present call (see installPresentHook).
extern "C" long __stdcall dmPresentPushHook(IDirect3DDevice9* dev, const RECT* dst, HWND window,
                                            const RGNDATA* dirty);
extern "C" long __cdecl dmPresentMovHook(IDirect3DDevice9* dev, const RECT* src, const RECT* dst,
                                         HWND window, const RGNDATA* dirty);

}  // namespace dm

// Flip-entry hook. dmFlipEntryWork handles fullscreen toggles and resizes and
// returns nonzero to skip the frame; dmFlipEntryShim otherwise tail-jumps into
// the trampoline so Flip's prologue continues normally.
extern "C" long dmFlipEntryWork();
extern "C" void dmFlipEntryShim();  // defined __declspec(naked) in hook_flip.cpp
