# Compatibility guide for overlay mod authors

This document explains how a mod that draws on top of the EFZ game window
(ImGui overlays, HUD replacements, anything holding its own Direct3D 9
resources) coexists with EfzDisplayManager. It exists so that other
implementations do not run into the problems we hit with efz-training-mode
and InGameNetplay.

## The problem

EFZ with Revival renders through a Direct3D 9 device that everyone shares:
the game's Ddraw wrapper owns the device and the backbuffer, and each loaded
mod draws its overlay on the same device. When EfzDisplayManager resizes the
window or toggles fullscreen, the backbuffer must be recreated, which is
done with `IDirect3DDevice9::Reset`.

`Reset` destroys every resource in the D3DPOOL_DEFAULT memory pool - that
includes every overlay's render targets, fonts, textures, and sprite
objects. Two things can go wrong:

1. If a Reset happens while another mod is mid-frame or still holds stale
   device objects, that mod crashes or hangs the renderer. This is exactly
   the "rendering died" class of bug we shipped twice before this contract
   existed.
2. If a mod rebuilds its objects at the wrong moment (for example during
   the Reset itself), the rebuild lands on a half-initialized device and
   fails in ways that look random.

There is no way for EfzDisplayManager to clean up another mod's objects
safely - it cannot know what they are or who owns them. Only each mod can
invalidate and rebuild its own resources. So cooperation has to be explicit.

## The reset contract (API v1)

A compatible mod exports one function from its DLL:

```cpp
const EfzOverlayResetApiV1* EFZ_QueryOverlayResetApiV1(void);
```

It returns a pointer to a 16-byte, fixed-layout table. The layout is shared
byte-for-byte by all participants and must not change:

```cpp
struct EfzOverlayResetApiV1 {
    uint32_t size;      // sizeof(EfzOverlayResetApiV1) = 16
    uint32_t version;   // 1
    HRESULT (__cdecl* beforeReset)(void* device, uint32_t epoch);
    HRESULT (__cdecl* afterReset)(void* device, uint32_t epoch, uint32_t ready);
};
```

Both callbacks use the plain `__cdecl` calling convention on Win32 x86.
Export the query function under its undecorated name.

### beforeReset(device, epoch)

Called on the render thread before EfzDisplayManager releases anything or
calls Reset. Your mod must:

- Release or invalidate every DEFAULT-pool resource it owns (fonts,
  textures, render targets, sprite objects), and make sure no code path can
  touch them until afterReset.
- Stop drawing until afterReset is called with `ready = 1`.
- Return `S_OK` to proceed. Any other value aborts the reset before any
  damage is done - use this if you cannot quiesce right now.

### afterReset(device, epoch, ready)

Called after the reset attempt:

- `ready = 1`: the device was reset successfully. Rebuild your resources
  now and return `S_OK`. If your rebuild fails, return a failure code and
  EfzDisplayManager will call you again on later frames - do not draw with
  half-rebuilt objects.
- `ready = 0`: the attempt failed or was rolled back. Stay quiesced; you
  will get another beforeReset before the next real attempt.

### epoch

A counter that increases for every reset attempt. Pair your before/after
calls by epoch and ignore stale callbacks. A resumed participant that sees
a new epoch must expect to be quiesced again.

## What EfzDisplayManager does with this

- It looks for the export in a fixed list of known overlay DLLs
  (`efz_training_mode.dll`, `efz_netplay_mod.dll`, `ResetProbe.dll`).
- If a known DLL is loaded but does not answer with a valid v1 table,
  EfzDisplayManager refuses to resize or toggle fullscreen at all. The game
  keeps running with its native behavior; a note in DisplayManager.log says
  which mod blocked the transition. This is deliberate: it is better to
  lose letterboxing than to wedge the renderer.
- Late-loaded DLLs are re-checked before every attempt. A participant that
  resumed before another one failed gets quiesced again before the retry.
- Unknown DLLs (not on the list) are not queried and not certified. If you
  ship a new overlay mod and want resize/fullscreen support, add your DLL
  to the list via a pull request, or expose the same export and open an
  issue so it can be added.

## Two hard-won rules for any D3D9 hook code

These cost us a week of debugging. Please read them before writing any
device-level hook for this game.

1. **Never replace the D3D9 device's vtable pointer, not even with a
   byte-identical copy.** We tried exactly that to intercept stray Reset
   calls, and on the live game every frame entered `Clear` through the
   copied table and never returned: no error, no crash, just a black window
   forever (the wrapper's Flip calls Clear, then BeginScene - BeginScene
   never ran). Intercept Reset at its static call sites in the wrapper
   instead, and let legitimate callers use the real vtable.
2. **Only reset between frames, from the render thread.** The safe point is
   the very start of the wrapper's Flip, before any surface pointer is
   cached in a register. A Reset from another thread, or mid-frame, leaves
   the renderer dead even when the HRESULT is success.

## Reference implementations

- `efz-training-mode` 1.2.5 and later (`src/gui/imgui_impl.cpp`,
  `include/compat/overlay_reset_gate.h`): full participant, including a
  fallback-window device that stands itself down while the game holds
  exclusive fullscreen.
- `InGameNetplay` 0.5.5 and later (`src/netplay/hooks/debug_overlay.cpp`):
  full participant for its ImGui overlay.

The same header ships in all three projects (`include/compat/overlay_reset_api.h`)
and must stay identical. If you are adding support to another mod, copy the
header verbatim and keep the ABI check (`size == 16`, `version == 1`).
