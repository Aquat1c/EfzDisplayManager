# How EfzDisplayManager works

A tour of what the mod does inside the game, for curious users and modders.

## The short version

EFZ with Revival draws its picture into a 640x480 buffer, then Revival's
own `Ddraw.dll` stretches that buffer onto the real window using Direct3D 9.
EfzDisplayManager intercepts that final stretch step and the frame start,
and uses them to:

- resize the backbuffer when the window changes (so the picture follows),
- replace the stretched rectangle with a centered, aspect-correct one
  (letterboxing),
- optionally run a shader filter (Sharp or xBR) instead of plain scaling.

On Revival 2020 and original 1.02j, the mod can also change how the picture
is sent to the graphics card to reduce pauses. The game still builds its
640x480 picture and draws text and boxes in the same order. The Ddraw.dll
file on disk is never modified.

## Why a hash check runs at startup

The mod hooks specific machine-code locations inside Revival's Ddraw.dll.
Those locations are only valid for exact builds of that file, so the first
thing the mod does is hash the DLL and compare it against a pinned list:

| Wrapper build | Notes |
| --- | --- |
| 2020-generation (1.02e era) | the most common build |
| 1.02f / 1.02g / 1.02h | same layout, same anchors |
| 1.02i | same layout, code shifted by a fixed offset |
| 1.02j renderer-split | two different 1.02j builds exist; both supported |
| 1.02j original (MinGW) | as above |

If your Ddraw.dll is not on the list, the mod does nothing at all - no
hooks, no changes. The log records the hash it saw, which is the fastest
way to get a new build supported.

## The hooks

1. **The final blit.** Right before each frame goes on screen, the wrapper
   calls StretchRect to copy the 640x480 image onto the backbuffer. The mod
   replaces that one call with its own function, which computes the correct
   destination rectangle (aspect-fit, integer, or custom), fills the
   uncovered border with the configured color, and optionally draws the
   image through the shader pipeline instead.
2. **The frame start.** At the top of the wrapper's frame function, the mod
   checks whether the window size changed and whether a fullscreen hotkey
   was pressed. This is the only moment where a display reset is safe:
   nothing is half-drawn yet, and the wrapper reloads every pointer from
   its globals right after.
3. **Sending the picture.** With `DynamicUpload=1`, supported Revival builds
   use a different graphics buffer to reduce waiting while preparing the
   current picture. The rest of the drawing sequence stays in place.
4. **Only with `LowLatencyWindow=1`:** the wrapper's device request uses
   the newer windowed path. Exclusive fullscreen follows the `VSync`
   setting instead.

## Keeping the game at 64 fps

The game targets about 64 frames per second using its own timing.
DisplayManager leaves that timing in charge. Slow graphics operations
can still hold the game up.

1. **Exclusive with tearing can use 60 Hz.** With `VSync=0`, parts of the
   picture can update between screen refreshes, which causes tearing.
   `VSync=1` requires a mode of at least 64 Hz. Performance depends on
   the graphics driver; allowing 60 Hz does not guarantee full game speed.
2. **Window and borderless can use the shortest path.** With
   `LowLatencyWindow=1`, the game's device is created in the newer mode
   Windows 7 and later offer, and Windows can put a borderless game straight
   on the screen without the desktop compositor's extra frame of delay. In
   that mode frames are shown no faster than the screen refreshes, so it is
   only used on screens of 64 Hz or more; on slower screens the classic mode
   stays on. Some older mods create pictures in a way that mode does not
   accept, so the mod quietly creates those pictures in a compatible way.

## Resizing and fullscreen, step by step

When the window size changes (maximize, drag, borderless toggle):

1. The frame-start hook notices a stable new size.
2. Every cooperating overlay mod is told to release its GPU resources
   (see [COMPATIBILITY.md](COMPATIBILITY.md)). If any known overlay cannot
   or will not, the whole operation is cancelled and the game keeps its
   current display settings.
3. The wrapper's own font and surfaces are released, then the D3D9 device
   is reset with the new size. If the reset fails, the change is rolled
   back; nothing is left half-broken.
4. Surfaces and font are recreated, overlays rebuild theirs, and the next
   frame renders at the new size with correct letterboxing.

## The shader pipeline

When a shader filter (Sharp or xBR) is configured, the final blit grabs the
finished frame into a texture, draws one fullscreen quad with the filter
pixel shader, and lets the wrapper present as usual. The shaders are ports
from SokuDisplayManager (MIT, ported with permission). If the GPU does not
support the needed shader model, the mod silently falls back to plain
StretchRect scaling and notes it in the log.

## What can go wrong, and how it is handled

- **Unknown Ddraw build** - the mod stays completely passive (by design).
- **Overlay mod without reset cooperation** - window resizes are refused;
  everything else works. Fullscreen toggles and recovering from a lost
  display still happen, because the game resets the display for those
  anyway; DisplayManager just keeps them letterboxed.
- **A reset fails** - the previous display settings are restored and the
  operation retried on later frames, with backoff.
- **Exclusive mode is impossible or unsuitable on this monitor** - one
  graphics card driving several monitors only allows it on its main output,
  and synchronized presentation requires a mode of at least 64 Hz.
  The hotkey gives letterboxed borderless fullscreen there instead of doing
  nothing.
- **The newer device mode is unavailable** (`LowLatencyWindow=1` on
  Windows XP or Vista, or a device created before the mod loaded) - the
  game keeps the classic mode and the log says why.
- **Shader creation fails** - plain scaling is used instead.

Every one of these paths writes a line to `DisplayManager.log`. When in
doubt, that file tells you exactly what the mod did and why.
