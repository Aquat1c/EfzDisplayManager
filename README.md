# EFZ DisplayManager

A port of [SokuDisplayManager](https://github.com/Aneroze/SokuDisplayManager)'s
display features to Eternal Fighter Zero — Memorial 4.02 with Revival 1.02j —
as an independently loaded companion DLL. The DLL adapts the bundled Revival
`Ddraw.dll`'s **final scale operation** (native 640x480 composed frame →
output backbuffer) while leaving the wrapper's DirectDraw emulation, palette
conversion, timer, input, and Revival session behavior in place.

## Status

| Item | State |
|---|---|
| Feasibility assessment | Complete — see [EFZ_DisplayManager_Feasibility_2026-10-20.md](EFZ_DisplayManager_Feasibility_2026-10-20.md), especially section 16 (agent implementation handoff) |
| Stage A research (anchors verified, contracts mapped) | Complete — see [docs/STAGE_A_RESEARCH_2026-10-20.md](docs/STAGE_A_RESEARCH_2026-10-20.md) |
| v0.2.0 observation | **Runtime-verified**: hash-pinned admission (both 1.02j Ddraws), forward-only hook at the wrapper's final StretchRect, ~3600 flips observed in-game |
| v0.3.0 output adaptation (aspect/integer/custom geometry, Point filter, border fill) | **Implemented and runtime-verified**: defaults reproduce native output exactly; IntegerScale=1 + Point produced a centered 640x480 rect with borders, stable in-game |
| v0.4.0 window support (sizable+maximizable window, backbuffer follows maximize/restore/drag) | **Implemented and runtime-verified**: every reset S_OK, aspect-correct at all sizes, Revival's EfzRevival.ini [Global] backbuffer restored exactly at the original window size |
| In-game full qualification matrix | Not started |

Current code state: output adaptation module (stage `adapt-rect`). It loads,
admits the wrapper by SHA-256 + PE timestamp, hooks the shared final-blit call
site, and applies configured output geometry (Mode/IntegerScale/Custom,
Filter, BackgroundColor) with defaults that reproduce native output. Sharp
shaders, window interaction, and reset-owning resources are later gates.

## Build

MSVC **v141** toolset, Win32/x86 (EFZ is a 32-bit process), Windows XP
compatible: `_WIN32_WINNT=0x0501`, PE OS/subsystem version 5.01, and **no CRT
at all** (`/NODEFAULTLIB`, in-tree memcpy/memset + formatter) so the import
table contains only XP-era KERNEL32/USER32 functions — the stock v141 static
CRT would import Vista+ `FlsAlloc`/`InitializeCriticalSectionEx`.

```
cmake -S . -B build-xp -A Win32 -T v141
cmake --build build-xp --config Release
build-xp\Release\dm_sha256_test.exe   # self-tests: SHA vectors, file pins, formatter
```

Output: `build-xp/Release/DisplayManager.dll` (PE32, OS 5.01).

## DisplayManager.ini

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | 1 | 0 leaves the game completely untouched |
| `Mode` | FitToScreen | `FitToScreen` / `IntegerScaling` / `CustomResolution` |
| `IntegerScale` | 0 | integer multiple; 0 = largest that fits |
| `CustomWidth/Height` | 640/480 | CustomResolution output size |
| `Filter` | Linear | `Linear` (wrapper's) / `Point` |
| `BackgroundColor` | 000000 | letterbox border color (RRGGBB) |
| `Fullscreen` | 0 | start in exclusive fullscreen (desktop mode) |
| `FullscreenMode` | 0 | Alt+Enter/F11 target: 0 exclusive, 1 borderless |
| `FullscreenHotkey` | 1 | F11 (and Alt+Enter) toggle the configured mode |
| `VSync` | -1 | -1 keep wrapper interval in windowed, IMMEDIATE in exclusive / 0 immediate / 1 vsync |
| `WindowAdjustStyle` | 1 | enable maximize box on the fixed-size window |
| `WindowDragResize` | 1 | sizing border; drag pauses the game while held |
| `ResizeBackBuffer` | 1 | backbuffer follows maximize/restore/resize |

Notes: exclusive fullscreen and backbuffer resize disable themselves for the
session while `efz_training_mode.dll` / `efz_netplay_mod.dll` (or any mod
holding D3D9 default-pool resources) is loaded - `Reset` is impossible then;
make those mods reset-aware to enable both alongside them.

## Install

Via [EfzModManager](https://github.com/fishshapedfish/EfzModManager): place the
DLL at `mods/DisplayManager/DisplayManager.dll` and enable the mod. The Revival
launcher's bundled `Ddraw.dll` must remain untouched (see constraints below).

## Verified Revival packages on disk

Hash-verified 2026-10-20 against the feasibility report (section 4):

| Package | Location | Notes |
|---|---|---|
| Renderer-split J (Stage A target) | `D:\EFZ_FULLPACK\`, `F:\EFZ_FULLPACK\` | `Ddraw.dll` `0e072342…9f603`, `EfzRevival.dll` `62d9ed74…68f32`, `EfzRevival.exe` `7c770732…b1c42` — exact match to the feasibility audit |
| Original J | `F:\EFZ_FULLPACK\_profile_backups\1.02j-original-20260620_pre_20260802\` | `Ddraw.dll` `e18abe00…366f9d`, `EfzRevival.dll` `cedd6068…468a169` verified; the `EfzRevival.exe` there (`194710a1…c027`) has no counterpart hash in the feasibility report |

Other Revival installs present on disk, **not yet hashed or profiled** (later
stages add each as a separately verified profile; do not assume RVAs transfer
between them): `D:\EFZ + Reviva 1.02f`, `D:\EFZ+Revival 1.02g`,
`D:\EFZ + Revival 1.02h` (plus a variant with a 580,608-byte `DDRAW.dll`),
`F:\EFZ + Revival 1.02i`, `F:\EFZ_trial`, `F:\EfzOldVersions`, clean EFZ copies
on both drives, and the workspace research copies under
`D:\dev\EFZ_modding\gamecopy\`.

## Hard constraints (from the feasibility handoff, section 16.2)

These contracts gate every implementation step:

- **Wrapper identity/ABI:** the bundled on-disk `Ddraw.dll` stays byte-exact;
  InGameNetplay's package admission must keep passing.
- **Integration boundary:** adapt the final `StretchRect` (renderer-split J
  Ddraw RVA `0x279C0`, inside Flip at `0x27860`); native surface pointer global
  `0xDF284`, device pointer global `0xDF2C4`, device Present at `0x279FC`,
  actual Device Reset at `0x36441`. Verify every RVA at runtime against the
  exact loaded binary before use.
- **Native composition:** the 640x480 native target, palette/text/box
  composition, and overlay coordinate system stay unchanged; output changes
  happen after composition.
- **Pacing ownership:** no second timer, frame limiter, input poll, or
  simulation callback; timer/input/rollback/checkpoints stay with their owners.
- **FP environment:** preserve x87/MXCSR state; FPU-preserving device creation.
- **Reset lifecycle:** release owned default-pool resources before the real
  Reset, gate recovery on its actual HRESULT (the wrapper helper at `0x363D0`
  discards it), reacquire only after the wrapper recreates its surfaces.
- **Window ownership:** no WndProc subclassing until a cooperative
  registration/removal contract with InGameNetplay's online handoff exists.
- **Fallback:** unsupported builds, hook conflicts, or capability failures must
  leave the native display path running with a clear diagnostic.

Stage gates: Sharp/xBR shaders, window interaction/hotkeys, exclusive
fullscreen, per-sprite filtering, and wider Revival profiles are all **later
gates** — not part of the first prototype.

## Repository layout

```
EFZ_DisplayManager_Feasibility_2026-10-20.md   Founding feasibility assessment + handoff
docs/                                          Stage A research notes + binary-anchor evidence
references/SokuDisplayManager-master/          Vendored upstream 1.3.0 source (MIT) — port origin, do not modify
src/                                           EFZ DisplayManager implementation
tools/verify_anchors.py                        Ddraw/EfzRevival anchor + import verifier (hash-pinned profiles)
CMakeLists.txt                                 Win32/x86 DLL build
```

Upstream SokuDisplayManager and its xBR shader are MIT-licensed; Dear ImGui is
vendored under `references/SokuDisplayManager-master/third_party/imgui/` with
its own notices. Preserve attribution notices when reusing code or shaders.
