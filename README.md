# EFZ DisplayManager

A port of [SokuDisplayManager](https://github.com/Aneroze/SokuDisplayManager)'s
output scaling features to Eternal Fighter Zero Memorial 4.02 with Revival.
The companion DLL adapts Revival's final native-frame-to-backbuffer blit.
Revival's bundled `Ddraw.dll` remains unchanged on disk.

## Current branch: 0.9.0 reset preview

This branch repairs concrete Reset ownership and recovery defects in v0.8.2.
It is a **preview requiring native game qualification**, not a claim that the
reported intermittent `D3DERR_INVALIDCALL` has been reproduced and eliminated.
See [the repair ledger](docs/RESET_REPAIR_2026-10-20.md) for implementation,
verification, limitations, and the acceptance matrix. Earlier runtime reports
are retained in [Stage A research](docs/STAGE_A_RESEARCH_2026-10-20.md).

The repair removes all foreign-resource tracking and forced COM destruction.
Compatible training/netplay builds expose `EFZ_QueryOverlayResetApiV1`; each
owner invalidates and rebuilds its own ImGui backend. A known loaded overlay
without that enabled contract blocks voluntary Reset before resource teardown.
Unknown third-party renderers are not certified by those two providers.

A persistent transaction replaces the admitted Revival reset helper. It passes
fresh parameters to every Reset, preserves requested and rollback settings,
rebuilds only after success, and gates native Flip until required surfaces and
font resources are ready. Failed allocations can recover on later frames.
There is no sleeping Reset retry loop. The lifecycle remains installed when
`ResizeBackBuffer=0`, because shaders and natural device loss still need it.

Full wrapper hash/timestamp admission remains mandatory. Helper takeover also
requires an exact full-function signature match (165 bytes for the 2020 family;
305 for J). Three actual wrapper binaries were available for the repair audit;
f/g/h/i require their existing profile plus a canonical helper match at runtime.
A failed lifecycle admission disables manager resets and persistent shader
resources while retaining basic geometry/filter adaptation when its hook admits.

The render-thread device guard refuses uncoordinated Reset calls. All Reset
sites identified in the three audited wrappers use the replaced helper. New
third-party Reset callers need an explicit integration review. Hooks pin the
manager DLL for process lifetime; change settings or remove the DLL after exit.

## Build and verification

Win32/x86 is required. The production DLL is CRT-free, uses XP-era API target
defines, and requests PE OS/subsystem 5.01. These are static compatibility gates;
an actual Windows XP runtime pass is still required. Modern MSVC and a real
LLVM-MinGW x86 compiler are supported. A `v141` toolset can be selected if present.

```bat
cmake -S . -B build-win32 -A Win32 -DDM_BUILD_PROFILE_FIXTURE_TEST=ON
cmake --build build-win32 --config Release
ctest --test-dir build-win32 -C Release --output-on-failure
python tools/verify_pe.py build-win32/Release/DisplayManager.dll
```

`tools/build-xp.bat` discovers Visual Studio or uses an existing x86 developer
prompt, compiles each source, runs the portable tests, checks the DLL imports,
and packages a preview ZIP. `tools/package.py` includes the preview version.
Portable SHA, geometry, formatter, presentation-parameter and transaction tests
also run on Linux with the same production core sources:

```sh
cmake -S . -B build-host -DCMAKE_BUILD_TYPE=Release
cmake --build build-host --parallel
ctest --test-dir build-host --output-on-failure
```

Profile fixtures are explicit inputs to `dm_profile_fixture_test`; default unit
tests do not depend on a developer's `D:`/`F:` game directories. `ResetProbe.dll`
is a manual legacy-owner negative fixture and is never shipped.

## Installation and coordinated mods

Use [EfzModManager](https://github.com/fishshapedfish/EfzModManager) to load
`mods/DisplayManager/DisplayManager.dll`. Keep the game's original Revival
`Ddraw.dll`. Replace DLLs only with the game closed.

For resize/fullscreen with overlays, use the coordinated reset-contract builds
of `Aquat1c/efz-training-mode` and `Aquat1c/InGameNetplay`. Their branches are
`fix/displaymanager-reset-contract-20261020`. The training patch starts from
published main; the user's local fallback-device change `4e3af4b` is not in that
baseline and must be reconciled before replacing a deployed training build.

## DisplayManager.ini

| Key | Default | Meaning |
|---|---|---|
| `Enabled` | 1 | 0 installs no hooks |
| `Mode` | FitToScreen | FitToScreen / IntegerScaling / CustomResolution |
| `IntegerScale` | 0 | Integer multiple; 0 selects the largest fitting value |
| `CustomWidth/Height` | 640/480 | Custom output dimensions |
| `Filter` | Linear | Linear / Point / Sharp / xBR; shader failure uses Linear |
| `Sharpness` | 20 | Sharp filter strength 10–40 |
| `BackgroundColor` | 000000 | Letterbox border RRGGBB |
| `Fullscreen` | 0 | Request fullscreen at startup |
| `FullscreenMode` | 1 | 0 exclusive, 1 borderless |
| `FullscreenHotkey` | 1 | F11 and admitted Alt+Enter toggle the configured mode |
| `VSync` | -1 | Keep windowed interval/use immediate exclusive; 0 immediate; 1 vsync |
| `CompatLayer` | 1 | Enable known overlay owner callbacks; never force-release resources |
| `WindowAdjustStyle` | 1 | Enable maximize box |
| `WindowDragResize` | 1 | Add sizing border; native modal drag can pause the game |
| `ResizeBackBuffer` | 1 | Follow stable client size changes |

An overlay that cannot recreate its own backend remains quiesced and retries
while the device is healthy. The wrapper can resume once its required resources
are ready. Further voluntary resets remain blocked until participants finish.

## Integration constraints

- Preserve native 640×480 composition, palette/text/box behavior and overlay coordinates.
- Keep simulation, input, pacing, rollback and session ownership with Revival and their mods.
- Match wrapper identity and complete helper ABI before patching.
- Release only the manager's resources and the wrapper references taken over by the admitted helper.
- Preserve registers, flags and floating-point state around the injected Flip worker.
- Do not use window dimensions as a Reset lifecycle signal; equal-size resets matter.
- Do not replace unrelated game-facing DirectDraw SYSTEMMEM/CPU surfaces to hide Lock failures.
- Never claim native game acceptance from mocked COM or portable unit tests alone.

Upstream SokuDisplayManager and its xBR shader are MIT-licensed. Preserve
`LICENSE.ported` and vendored attribution notices. The founding feasibility
report and the historical evidence remain in this repository.
