# EfzDisplayManager

Display upgrades for **Eternal Fighter Zero -Memorial- 4.02 with Revival**:
resize the game window freely, get proper letterboxing instead of stretching,
jump in and out of fullscreen with a single key, and sharpen the picture with
modern scaling filters. It runs as a companion mod and never modifies your
game files - everything it does happens in memory, and deleting the mod
folder undoes it all.

Based on the display features of
[SokuDisplayManager](https://github.com/Aneroze/SokuDisplayManager) (MIT,
ported with the creator's permission; the upstream source is included as a
git submodule under `references/`).

## Features

- **Free window resizing** - drag the window edge or maximize it; the game
  picture follows and stays in the correct aspect ratio with black bars
  (letterboxing) instead of stretching.
- **Fullscreen with one key** - `F11` or `Alt+Enter` toggles fullscreen.
  Choose borderless (default, plays nice with overlays and other mods) or
  exclusive fullscreen (lowest input lag) in the ini.
- **Scaling modes** - fit to screen, integer scaling (pixel-perfect), or a
  custom output size.
- **Upscaling filters** - Linear (default), Point, Sharp, or xBR, applied to
  the final image with GPU shaders.
- **VSync choice** - keep the game's setting, or force it off/on.
- **Mod friendly** - designed to run alongside efz-training-mode and
  InGameNetplay (see [Compatibility](#compatibility)).

## Requirements

- EFZ Memorial 4.02 with Revival, using Revival's own `Ddraw.dll`
  (supported: 1.02e-era/2020, 1.02f, 1.02g, 1.02h, 1.02i, and both 1.02j
  builds). The wrapper is verified by hash before anything is hooked; an
  unknown `Ddraw.dll` is left completely untouched.
- Windows, 32-bit game process. The DLL is built clean for XP-era targets
  and works on modern Windows.
- [EfzModManager](https://github.com/fishshapedfish/EfzModManager) to load
  the mod (or your usual mod loader).

## Install

1. Copy the `DisplayManager` folder into the game's `mods` folder.
2. In `EfzModManager.ini`, add `DisplayManager=1` to `[Mods]`.
3. Start the game. A `DisplayManager.log` file appears next to the DLL when
   it loads.

To update: close the game first, then replace `DisplayManager.dll`.
To uninstall: set `DisplayManager=0` (or `Enabled=0` in
`DisplayManager.ini`), or delete the folder.

## Usage

- Resize or maximize the window whenever you like. During an edge-drag the
  game pauses briefly (that is Windows' own modal resize loop); it resumes
  when you let go.
- `F11` or `Alt+Enter` toggles fullscreen. In borderless mode the picture is
  letterboxed to your monitor; exclusive mode switches the display mode for
  the lowest latency.
- Everything is configured in `mods/DisplayManager/DisplayManager.ini`.
  Settings are read at startup - restart the game after changing them.

## DisplayManager.ini

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | 1 | Master switch; 0 installs no hooks |
| `Mode` | FitToScreen | `FitToScreen`, `IntegerScaling`, or `CustomResolution` |
| `IntegerScale` | 0 | Integer multiple; 0 = largest that fits |
| `CustomWidth` / `CustomHeight` | 640 / 480 | Output size for CustomResolution |
| `Filter` | Linear | `Linear`, `Point`, `Sharp`, or `xBR` (shader-based; falls back to Linear if the GPU path is unavailable) |
| `Sharpness` | 20 | Sharp filter strength, 10-40 (means 1.0-4.0) |
| `BackgroundColor` | 000000 | Letterbox bar color, RRGGBB hex |
| `Fullscreen` | 0 | 1 = start in fullscreen |
| `FullscreenMode` | 1 | 0 = exclusive, 1 = borderless |
| `FullscreenHotkey` | 1 | Enable the F11 / Alt+Enter toggle |
| `VSync` | -1 | -1 keep the game's setting, 0 off, 1 on |
| `WindowAdjustStyle` | 1 | Make the window maximizable |
| `WindowDragResize` | 1 | Add a resizing border |
| `ResizeBackBuffer` | 1 | Follow window size changes (set 0 to keep a fixed backbuffer) |
| `CompatLayer` | 1 | Cooperate with known overlay mods on device resets |

## Compatibility

DisplayManager, training mode, and the netplay mod all draw on top of the
same 3D device. When the backbuffer is resized, everyone's overlays must
rebuild their resources first - that coordination uses a small versioned
contract (`EFZ_QueryOverlayResetApiV1`).

- **efz-training-mode 1.2.5+** and **InGameNetplay 0.5.5+** include the
  contract. With them, resizing, fullscreen, and letterboxing all work
  together.
- **Older builds of those mods** run fine, but DisplayManager deliberately
  refuses to resize the display while they are loaded (you will see a note
  about the reset contract in the log, and windowed play keeps the game's
  native behavior). Update the mods to get resizing back.
- Other mods that do not draw 3D overlays (translations, stages, replay
  tools, etc.) are unaffected.

## Troubleshooting

- **Something looks wrong** - check `mods/DisplayManager/DisplayManager.log`.
  It records what was admitted, every hook installed, and each display
  change with full parameters.
- **No letterboxing after maximizing** - see Compatibility above: an old
  training/netplay build is the usual cause.
- **The window style or hotkeys do nothing** - your `Ddraw.dll` was not one
  of the supported builds, so the mod stays passive by design; the log says
  which hash it saw.
- **Want to report a problem** - include the log file and which game copy /
  Revival version you use.

## Building from source

Win32/x86 is required. Modern MSVC or a real LLVM-MinGW x86 toolchain work.

```bat
cmake -S . -B build-win32 -A Win32
cmake --build build-win32 --config Release
ctest --test-dir build-win32 -C Release --output-on-failure
python tools/verify_pe.py build-win32/Release/DisplayManager.dll
```

`tools/build-xp.bat` is the full release pipeline: it finds a toolchain,
compiles everything CRT-free, runs the tests, checks the DLL imports, and
packages the ZIP. The portable tests (SHA-256, geometry, formatting,
presentation parameters, reset transactions) also build and run on Linux:

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

## Documentation

Guides live in [docs/](docs/):

- [How it works](docs/HOW_IT_WORKS.md) - what the mod hooks, why the hash
  check exists, and what happens on resize/fullscreen.
- [Compatibility guide](docs/COMPATIBILITY.md) - for mod authors: how to
  make an overlay mod cooperate on device resets (the
  `EFZ_QueryOverlayResetApiV1` contract used by efz-training-mode and
  InGameNetplay), and the two device-hook rules that cost us a week to
  learn.

## For maintainers

- Full design notes: see the [Documentation](#documentation) section above.
- Every supported `Ddraw.dll` is admitted only on an exact SHA-256 + PE
  timestamp match, and the reset helper is byte-matched before takeover.
  Hooking anything unknown is intentionally impossible.
- Upstream SokuDisplayManager and its xBR shader are MIT-licensed; keep
  `LICENSE.ported`. The upstream tree itself is referenced as a git
  submodule (`references/SokuDisplayManager`), not copied in.
