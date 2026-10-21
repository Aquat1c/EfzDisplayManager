# EfzDisplayManager

A display mod for **Eternal Fighter Zero -Memorial- with Revival**.

- Resize or maximize the game window freely: the picture keeps its shape,
  with black bars instead of stretching.
- Go fullscreen with `F11` or `Alt+Enter`.
- Sharper scaling filters for big screens.
- The game always keeps its full speed.

It never changes your game files. Everything happens while the game runs, and
deleting the mod folder undoes it all.

Based on [SokuDisplayManager](https://github.com/Aneroze/SokuDisplayManager)
(MIT, ported with the creator's permission).

## Install

You need EFZ with Revival and
[EfzModManager](https://github.com/fishshapedfish/EfzModManager).

1. Copy the `DisplayManager` folder into the game's `mods` folder.
2. Open `EfzModManager.ini` and add `DisplayManager=1` under `[Mods]`.
3. Start the game.

**Update:** close the game, then replace the files in `mods\DisplayManager`.
**Uninstall:** set `DisplayManager=0` in `EfzModManager.ini`, or delete the folder.

## Controls

| Key / action | What it does |
| --- | --- |
| `F11` or `Alt+Enter` | Fullscreen on / off (only while the game window is active) |
| Maximize button, window snapping | The picture fills the window, with black bars where needed |
| Dragging the window edge | Resizes the window. The game pauses while you hold the edge (Windows does that) |

In fullscreen the mouse pointer hides itself after 2 seconds without movement.

## Settings

All settings are in `mods\DisplayManager\DisplayManager.ini`; each one is
explained right there. Close the game before changing them. They are read
when the game starts.

The ones most people touch:

| Setting | Default | Choices |
| --- | --- | --- |
| `Mode` | `FitToScreen` | `FitToScreen` (as big as fits), `IntegerScaling` (whole multiples, pixel-perfect), `CustomResolution` |
| `Filter` | `Linear` | `Linear` (smooth), `Point` (crisp pixels), `Sharp` (crisp but even), `xBR` (smoothed pixel art) |
| `FullscreenMode` | `Borderless` | `Borderless` or `Exclusive` |
| `Fullscreen` | `0` | `1` = start the game in fullscreen |
| `LowLatencyWindow` | `0` | `1` = faster picture in window and borderless mode |
| `BackgroundColor` | `000000` | Color of the bars, as RRGGBB |

## FAQ

**Borderless or exclusive fullscreen?**
Borderless is the safe choice: switching to other programs is instant and
other mods' menus always show. Exclusive gives the game the screen to itself.
On most PCs the difference in delay is small. Try `LowLatencyWindow=1` first.

**Why did I get borderless when I set `FullscreenMode=Exclusive`?**
Exclusive fullscreen is only used on screens that run at 64 Hz or faster.
EFZ draws 64 frames a second, and on a slower screen exclusive mode would
slow the whole game down, which also breaks netplay. Most laptop screens run
at 60 Hz, so they get borderless. With several monitors on one graphics card,
exclusive usually only works on the main monitor.

**Does the game run at full speed on a 60 Hz screen?**
Yes. A 60 Hz screen can only show 60 pictures a second, so about 4 of the
game's 64 frames are never shown, but the game itself keeps its speed. Every
game and mod does the same on such a screen. A 120 Hz or faster screen shows
the game more evenly, and G-SYNC/FreeSync shows it perfectly evenly.

**What does `LowLatencyWindow=1` do?**
The picture reaches the screen sooner in window and borderless mode (about
16 ms sooner on a 60 Hz laptop). It needs Windows 7 or newer and works best on
Windows 10 and 11. If the game will not start, or pictures from other mods go
missing, set it back to `0`.

**Which filter should I pick?**
`Sharp` is a good default for big screens: it looks crisp without uneven
pixels and costs nothing. `xBR` smooths the pixel art and can be heavy for
laptop graphics at high resolutions. `Point` gives hard pixels.
`IntegerScaling` with `Point` is pixel-perfect but leaves wider bars.

**Does it work with training mode and netplay?**
Yes. efz-training-mode 1.2.5+ and InGameNetplay 0.5.5+ work with everything.
Older versions of those mods still run, but the picture stops following
window resizing while they are loaded. Fullscreen still works. Update them to
get resizing back. Other mods (translations, stages, replay tools) are not
affected.

**The picture tears in exclusive fullscreen.**
Set `VSync=1`. The game keeps its full speed either way.

**The mod does nothing at all.**
Your Revival `Ddraw.dll` is probably a version the mod does not know, and the
mod then stays out of the way on purpose. Supported: Revival 1.02e (2020),
1.02f, 1.02g, 1.02h, 1.02i and both 1.02j builds. The log (see below) shows
what it found.

**Does it work on Windows XP?**
Yes. `LowLatencyWindow` needs Windows 7 or newer and is skipped on older
systems.

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

Win32 (x86) only, with MSVC or an LLVM-MinGW x86 toolchain.

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
