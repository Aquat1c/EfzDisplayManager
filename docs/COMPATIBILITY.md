# Compatibility guide for overlay mod authors

This document explains how a mod that draws on top of the EFZ game window
(ImGui overlays, HUD replacements, anything holding its own Direct3D 9
resources) coexists with EfzDisplayManager.

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
   device objects, that mod crashes or hangs the renderer.
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
- `ready = 0`: the device is not usable yet: EfzDisplayManager has released
  its own resources and is about to reset, or the attempt failed or was
  rolled back. Stay quiesced. More afterReset calls for the same epoch can
  follow, and the Reset itself can happen without another beforeReset;
  `ready = 1` for that epoch is your signal to rebuild. You only get a new
  beforeReset if you already resumed, or with a new epoch.

### epoch

A counter that increases for every reset attempt. Pair your before/after
calls by epoch and ignore stale callbacks. A resumed participant that sees
a new epoch must expect to be quiesced again.

## What EfzDisplayManager does with this

- It looks for the export in a fixed list of known overlay DLLs
  (`efz_training_mode.dll`, `efz_netplay_mod.dll`, `ResetProbe.dll`,
  `efz_truecolor.dll`).
- If a known DLL is loaded but does not answer with a valid v1 table,
  EfzDisplayManager refuses to resize the backbuffer with the window. The
  game keeps its native windowed behavior; a note in DisplayManager.log says
  which mod blocked the resize. This is deliberate: those resets would not
  happen without EfzDisplayManager, so it does not add them.
- Resets the game would perform anyway are never refused: recovering a lost
  device (alt-tab out of exclusive mode, the lock screen, a driver reset),
  Revival's own resets, and the fullscreen toggle (EfzDisplayManager owns
  Alt+Enter, which resets the device just like Revival's own toggle, but
  letterboxed). Participants are still called; a module without the
  contract is skipped, and if its resources make the Reset fail, the change
  is rolled back or retried about once a second, exactly as the game would
  fail without EfzDisplayManager.
- The device is only ever replaced (exclusive mode on another graphics
  adapter) when none of the listed DLLs is loaded, because v1 has no way to
  hand you a new device. With any of them loaded, that request lands in
  borderless fullscreen instead.
- Late-loaded DLLs are re-checked before every attempt. A participant that
  resumed before another one failed gets quiesced again before the retry.
- Unknown DLLs (not on the list) are not queried and not certified. If you
  ship a new overlay mod and want resize/fullscreen support, add your DLL
  to the list via a pull request, or expose the same export and open an
  issue so it can be added.

## Two rules for any D3D9 hook code

Read these before writing any device-level hook for this game.

1. **Never replace the D3D9 device's vtable pointer, not even with a
   byte-identical copy.** The wrapper's Flip then enters `Clear` through the
   copied table and never returns: no error, no crash, just a black window.
   Intercept Reset at its static call sites in the wrapper instead, and let
   legitimate callers use the real vtable.
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

efz-training-mode and InGameNetplay ship the same header,
`include/compat/overlay_reset_api.h`, including the export declaration.
EfzDisplayManager's consumer-side copy is `src/overlay_reset_api.h`. The
files differ in comments and declarations, but the 16-byte layout must never
change. If you are adding support to another mod, copy the participant header
from one of those two projects and keep the ABI check (`size == 16`,
`version == 1`).
