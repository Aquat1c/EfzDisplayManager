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
| Stage A prototype (transparent final-scale adapter) | **Not started** |
| In-game runtime qualification | **Not started** |

Current code state: transparent bootstrap stub only. The DLL loads, writes
`DisplayManager.log` next to itself, and installs no hooks. The native display
path remains fully in charge. This exists to validate the loader route and
toolchain before the admission/hook work in Stage A.

## Build

MSVC, Win32/x86 (EFZ is a 32-bit process). Static CRT, no extra dependencies.

```
cmake -S . -B build -A Win32
cmake --build build --config Release
```

Output: `build/Release/DisplayManager.dll`.

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
references/SokuDisplayManager-master/          Vendored upstream 1.3.0 source (MIT) — port origin, do not modify
src/                                           EFZ DisplayManager implementation
CMakeLists.txt                                 Win32/x86 DLL build
```

Upstream SokuDisplayManager and its xBR shader are MIT-licensed; Dear ImGui is
vendored under `references/SokuDisplayManager-master/third_party/imgui/` with
its own notices. Preserve attribution notices when reusing code or shaders.
