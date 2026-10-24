# EfzDisplayManager

A display mod for **Eternal Fighter Zero -Memorial- with Revival**.

- Resize or maximize the game window freely: the picture keeps its shape,
  with black bars instead of stretching.
- Go fullscreen with `F11` or `Alt+Enter`.
- Sharper scaling filters for big screens.
- Keeps the game's own timing, with tearing allowed by default in exclusive fullscreen.

It leaves the game and Revival files unchanged. Its own settings and log
live in the mod folder. Delete that folder to remove the mod.

Based on [SokuDisplayManager](https://github.com/Aneroze/SokuDisplayManager)
(MIT, ported with the creator's permission).

## Install

You need EFZ with Revival and
[EfzModManager](https://github.com/fishshapedfish/EfzModManager).

1. Copy the `DisplayManager` folder into the game's `mods` folder.
2. Open `EfzModManager.ini` and add `DisplayManager=1` under `[Mods]`.
3. Start the game.

**Update:** close the game and replace `DisplayManager.dll`. Keep your existing
`DisplayManager.ini` to keep your choices; the mod adds missing settings at startup.
**Uninstall:** set `DisplayManager=0` in `EfzModManager.ini`, or delete the folder.

## Controls

| Key / action | What it does |
| --- | --- |
| `F11` or `Alt+Enter` | Fullscreen on / off (only while the game window is active) |
| Maximize button, window snapping | The picture fills the window, with black bars where needed |
| Dragging the window edge | Resizes the window. The game pauses while you hold the edge (Windows does that) |

In fullscreen the mouse pointer hides itself after 2 seconds without movement.

## Settings

Keep your window position, window size and starting backbuffer size in
`EfzRevival.ini`. DisplayManager uses the window and buffers Revival creates
from those settings. When you resize the window, DisplayManager adjusts the
picture; returning to the starting size restores Revival's original buffer size.

DisplayManager's extra options are in `mods\DisplayManager\DisplayManager.ini`.
Close the game before editing them. If the file is missing, the mod creates it.
Missing or invalid entries are repaired without replacing valid choices.

New settings start the game in a window. `F11` or `Alt+Enter` selects exclusive
fullscreen. `VSync=Auto` allows tearing, and `DynamicUpload=Auto` enables the
upload improvement where supported. These are the recommended starting settings
for a 60 Hz screen; existing explicit choices take precedence.

The ones most people touch:

| Setting | Default | Choices |
| --- | --- | --- |
| `Mode` | `FitToScreen` | `FitToScreen` (as big as fits), `IntegerScaling` (whole multiples, pixel-perfect), `CustomResolution` |
| `Filter` | `Linear` | `Linear` (smooth), `Point` (crisp pixels), `Sharp` (crisp but even), `xBR` (smoothed pixel art) |
| `FullscreenMode` | `Exclusive` | `Borderless` or `Exclusive` |
| `Fullscreen` | `0` | `1` = start the game in fullscreen |
| `VSync` | `Auto` | Allow tearing; `1` waits for refreshes and requires at least 64 Hz |
| `DynamicUpload` | `Auto` | Use the upload improvement when supported; `0` disables it |
| `BackgroundColor` | `000000` | Color of the bars, as RRGGBB |

## FAQ

**Borderless or exclusive fullscreen?**
Exclusive gives the game control of the screen and is the default fullscreen
choice. Borderless can make switching between programs easier. Choose it with
`FullscreenMode=Borderless` if you prefer.

**Why did I get borderless when I set `FullscreenMode=Exclusive`?**
With `VSync=Auto` or `0`, exclusive fullscreen allows 60 Hz and requests immediate
presentation with tearing. With `VSync=1`, a mode of at least 64 Hz is still
required. With several monitors on one graphics card, exclusive usually
only works on the main output. Unsupported modes retain the existing
borderless fallback; check the log to confirm which mode actually started.

**Does the game run at full speed on a 60 Hz screen?**
The game keeps its own timing. With `VSync=Auto` or `0`, exclusive mode allows tearing
instead of waiting for each screen refresh. Performance still depends on
the graphics driver, so full speed is not guaranteed on every machine.

**What does `DynamicUpload=Auto` do?**
It changes how the picture is sent to the graphics card to reduce pauses
on supported graphics cards. Other builds keep their existing behavior.
This is a preview feature. If it causes picture problems, set it to `0`.

**What does `LowLatencyWindow=1` do?**
It enables a newer windowed display mode that may reduce delay on Windows 7
or newer. It is used only on screens of at least 64 Hz and stays off by default.
If it causes startup or overlay problems, set it back to `0`.

**Which filter should I pick?**
`Sharp` keeps edges crisp when scaling to a big screen. `xBR` smooths the pixel
art and can be heavy for laptop graphics at high resolutions. `Point` gives hard pixels.
`IntegerScaling` with `Point` is pixel-perfect but leaves wider bars.

**Does it work with training mode and netplay?**
Display resizing supports efz-training-mode 1.2.5+ and InGameNetplay 0.5.5+.
Older versions may prevent resizing the picture with the window. Update them
if that happens. The upload improvement is still a preview; compatibility
during live matches needs more testing.

**The picture tears in exclusive fullscreen.**
Set `VSync=1` to synchronize presentation. This requires a mode of at least
64 Hz and can add waiting; use `VSync=0` when tearing is acceptable.

**The mod does nothing at all.**
Your Revival `Ddraw.dll` is probably a version the mod does not know, and the
mod then stays out of the way on purpose. Supported: Revival 1.02e (2020),
1.02f, 1.02g, 1.02h, 1.02i and both 1.02j builds. The log (see below) shows
what it found.

**Does it work on Windows XP?**
The DLL is built for Windows XP compatibility. `LowLatencyWindow` needs
Windows 7 or newer. This preview has not been tested on an XP machine.

## Troubleshooting

The mod writes `mods\DisplayManager\DisplayManager.log` every time the game
starts. It records what the mod found and every display change it made. When
reporting a problem, attach that file and say which Revival version you use.

- **Black bars do not appear after maximizing:** an old training or netplay
  mod is loaded (see the FAQ).
- **The game will not start after changing settings:** set
  `LowLatencyWindow=0`, or delete `DisplayManager.ini` to get the defaults back.
- **Something else is wrong:** set `Enabled=0` to check whether the mod is the
  cause.

---

## For modders and developers

- [How it works](docs/HOW_IT_WORKS.md): what the mod changes inside the game
  and why.
- [Compatibility guide](docs/COMPATIBILITY.md): how an overlay mod cooperates
  when the display is reset (`EFZ_QueryOverlayResetApiV1`).

### Building

Win32 (x86) only, with MSVC or an LLVM-MinGW x86 toolchain, plus Python 3.

```bat
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
ctest --test-dir build-win32 -C Release --output-on-failure
```

`tools/build-xp.bat` is the release build: it compiles without the C
runtime, runs every test, checks the DLL's imports and packages the ZIP.
The portable tests also build on Linux:

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

### Notes for maintainers

- A `Ddraw.dll` is only hooked on an exact SHA-256 and PE timestamp match,
  and every patched spot is byte-checked first. Unknown builds are never
  touched.
- Keep `LICENSE.ported` (SokuDisplayManager and the xBR shader are MIT). The
  upstream source is a git submodule under `references/`.
