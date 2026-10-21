# How EfzDisplayManager works

A tour of what the mod does inside the game, for curious users and modders.

## The short version

EFZ with Revival draws its picture into a 640x480 buffer, then Revival's
own `Ddraw.dll` stretches that buffer onto the real window using Direct3D 9.
EfzDisplayManager intercepts that final stretch step and the frame start,
and uses those two points to:

- resize the backbuffer when the window changes (so the picture follows),
- replace the stretched rectangle with a centered, aspect-correct one
  (letterboxing),
- optionally run a shader filter (Sharp or xBR) instead of plain scaling.

Nothing else about rendering is touched: the game still composes its native
640x480 frame, palettes, text, and boxes behave exactly as before, and the
Ddraw.dll file on disk is never modified.

## Why a hash check runs at startup

The mod hooks specific machine-code locations inside Revival's Ddraw.dll.
Those locations are only valid for exact builds of that file, so the first
thing the mod does is hash the DLL and compare it against a pinned list:

| Wrapper build | Notes |
|---|---|
| 2020-generation (1.02e era) | the most common build |
| 1.02f / 1.02g / 1.02h | same layout, same anchors |
| 1.02i | same layout, code shifted by a fixed offset |
| 1.02j renderer-split | two different 1.02j builds exist; both supported |
| 1.02j original (MinGW) | as above |

If your Ddraw.dll is not on the list, the mod does nothing at all - no
hooks, no changes. The log records the hash it saw, which is the fastest
way to get a new build supported.

## The two hooks

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
- **Overlay mod without reset cooperation** - resizes and fullscreen are
  refused; everything else works.
- **A reset fails** - the previous display settings are restored and the
  operation retried on later frames, with backoff.
- **Shader creation fails** - plain scaling is used instead.

Every one of these paths writes a line to `DisplayManager.log`. When in
doubt, that file tells you exactly what the mod did and why.
