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

Nothing else about rendering is touched: the game still composes its native
640x480 frame, palettes, text, and boxes behave exactly as before, and the
Ddraw.dll file on disk is never modified.

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
3. **Only with `LowLatencyWindow=1`:** the wrapper's request for a Direct3D
   device is answered with the newer kind (see below), and its call that
   puts a frame on screen is replaced so it never waits.

## Keeping the game at 64 fps

EFZ draws 64 frames per second and runs three game-logic steps for each of
them (192 a second). The whole game is timed by those drawn frames: if one
is held up, the game itself slows down. Most screens refresh 60 times a
second, so they cannot show all 64 (in window and borderless mode the
desktop's own compositor leaves about 4 a second out). Making the game wait
for the screen instead would slow it down by 6%.

1. **Exclusive fullscreen only runs at 64 Hz or more.** In exclusive mode
   every frame waits for the screen, so a slower mode would slow the game
   down (and break netplay). On a slower screen the hotkey always gives
   borderless instead; no setting overrides this.
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
  and a monitor with no mode of at least 64 Hz would slow the game down.
  The hotkey gives letterboxed borderless fullscreen there instead of doing
  nothing.
- **The newer device mode is unavailable** (`LowLatencyWindow=1` on
  Windows XP or Vista, or a device created before the mod loaded) - the
  game keeps the classic mode and the log says why.
- **Shader creation fails** - plain scaling is used instead.

Every one of these paths writes a line to `DisplayManager.log`. When in
doubt, that file tells you exactly what the mod did and why.
