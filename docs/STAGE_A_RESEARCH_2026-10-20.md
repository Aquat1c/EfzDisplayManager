# Stage A research: verified anchors, integration contracts, and port map

**Date:** 20 October 2026 (follows the initial repo scaffold, same day)
**Scope:** Binary verification of the feasibility report's Ddraw.dll anchors against
the on-disk renderer-split and original J packages; loader, netplay-frontend, and
upstream-source research needed before writing Stage A code. Static analysis only —
nothing was hooked, built into the game path, or run in-game.
**Method:** `tools/verify_anchors.py` (this repo; capstone + pefile) over the
hash-pinned binaries listed below, plus source review of EfzModManager (binary
analysis), InGameNetplay, and vendored SokuDisplayManager.
**Status:** all anchors PASS in both J packages. Several facts beyond the
feasibility doc were established and are marked **[NEW]**.

## 1. Verified binaries

| Binary | Location used | SHA-256 (prefix) |
|---|---|---|
| Renderer-split J `Ddraw.dll` | `D:\EFZ_FULLPACK\` (also `F:\EFZ_FULLPACK\`) | `0e072342…` |
| Renderer-split J `EfzRevival.dll` | `D:\EFZ_FULLPACK\` | `62d9ed74…` |
| Original J `Ddraw.dll` | `F:\EFZ_FULLPACK\_profile_backups\1.02j-original-20260620_pre_20260802\` | `e18abe00…` |

Evidence: [evidence/ddraw-split-j.txt](evidence/ddraw-split-j.txt) and
[evidence/ddraw-orig-j.txt](evidence/ddraw-orig-j.txt). The tool refuses to check
anchors when the hash does not match the profile, so anchor output is always tied
to the exact binary.

## 2. Anchor verification results

Every RVA from feasibility section 5 was disassembled and confirmed in **both**
binaries (preferred image base `0x64C00000` in both):

| Anchor | Split J | Orig J | Verified as |
|---|---:|---:|---|
| Flip entry | `0x27860` | `0x277A0` | DirectDraw surface Flip; contains the whole final-frame path |
| Native 640x480 surface global | `0xDF284` | `0xDF284` | referenced in Flip (`0x278C5` / `0x27805`) |
| D3D9 device pointer global | `0xDF2C4` | `0xDF2C4` | referenced at Flip prologue (`0x27874` / `0x277B4`) |
| Presentation-parameters global | `0xDF28C` | `0xDF28C` | referenced in borderless geometry math (`0x27AD5` / `0x27A15`) |
| Conversion blit | `0x27928` | — | `call [eax+0x88]` StretchRect into native target |
| Final StretchRect | `0x279C0` | `0x27900` | `call [eax+0x88]`, 6 bytes (`ff 90 88 00 00 00`) |
| Device Present | `0x279FC` | `0x2793C` | `call [eax+0x44]` |
| Reset helper | `0x363D0` | `0x36220` | TCL-gated reset path |
| Actual Device Reset | `0x36441` | `0x36291` | `call [edx+0x40]`, HRESULT not consumed |
| getEfzTimer export | `0xCF790` | `0xCF4E0` | export table matches; returns `&0x64CDF4A0` (timer object at RVA `0xDF4A0`) **[NEW]** |

D3D9 vtable offsets observed in the wrapper (indices): `+0x0C` TestCooperativeLevel(3),
`+0x20` GetDisplayMode(8), `+0x40` Reset(16), `+0x44` Present(17), `+0x48`
GetBackBuffer(18), `+0x88` StretchRect(34), `+0xA4` BeginScene(41), `+0xA8`
EndScene(42), `+0xAC` Clear(43). All consistent with the standard IDirect3DDevice9
layout.

## 3. The Flip path, instruction-verified (renderer-split J)

The full final-frame sequence inside Flip at `0x27860`:

1. Load device from `0xDF2C4`; `Clear(dev, 0, NULL, D3DCLEAR_TARGET, 0xFF000000, 1.0f, 0)` (`0x278B4`).
2. Load native surface from `0xDF284`; `BeginScene` (`0x278CE`).
3. Palette-conversion helper `0x27700` (reads DirectDraw surface description fields).
4. Conversion blit `0x27928`: `StretchRect(dev, src=[0xDF280+0], NULL, dst=[0xDF284], NULL, filter=[0xDF2EC])`.
5. `EndScene` (`0x27936`).
6. Overlay calls on the static renderer object at `0xDF280` (address passed as
   `this`/ecx): `0x27944 → 0x35630` and `0x2794E → 0x36510` — these are the doc's
   text/box overlay calls, now confirmed to draw after the conversion blit.
7. `GetBackBuffer(dev, 0, 0, &bb)` (`0x2797E`).
8. Mode query on the `0xDF280` object: `0x359E0`; `test al,al; jne 0x27A20`.
   - **Windowed (fall-through):** `dstRect = NULL`, `filter=[0xDF2EC]` pushed at
     `0x2799E`–`0x279A6`.
   - **Borderless (`0x27A20`):** `MonitorFromWindow(hwnd=[0xDF2CC])` +
     `GetMonitorInfoW` **[NEW: IAT 0x13C744/0x13C724 identified]**, `GetDisplayMode`
     (`vtable+0x20`), then x87 aspect math reading `0xDF28C` backbuffer dims and
     global `0xDF290`, with explicit rounding-control save/set/restore
     (`fnstcw`/`fldcw`, RC=round-to-nearest) **[NEW]**; writes a RECT to
     `[ebp-0x58..-0x4C]`, sets `dstRect=&rect`, `filter=[0xDF2EC]`, then
     **`jmp 0x279AA`**.
9. `0x279AA`–`0x279C0` (shared): remaining args — `src=[0xDF284]`, `srcRect=NULL`,
   `dst=backbuffer` — then the single shared **`StretchRect` at `0x279C0`**.
   Both geometry branches converge here exactly as the feasibility doc claimed;
   the precise convergence instruction is `0x279AA` **[NEW]**.
10. Backbuffer `Release` (`0x279D1`), then `Present(dev, 0,0,0,0)` at `0x279FC`.

**[NEW] Filter global `0xDF2EC`:** both StretchRect calls (conversion blit and
final blit) take their `D3DTEXTUREFILTERTYPE` argument from the same wrapper
global. This is the wrapper's own scaling-filter setting. A display module must
not assume the final blit's filter is a constant, and at hook time the effective
stack argument (`[esp+0x14]` at the call site) is the authoritative value.

**Other globals mapped [NEW]:** `0xDF280` = static C++ renderer object (its `+0`
field is the conversion/staging surface used as conversion-blit source; methods
at `0x35630`/`0x36510` render overlays, `0x359E0` queries the output mode);
`0xDF2CC` = HWND used for `MonitorFromWindow`; `0xDF290` = value used in the
aspect computation; `0xDF4A0` = timer object returned by `getEfzTimer`.

**Hook-site geometry [NEW]:** at `0x279C0` every input is on the stack:
`[esp]=device(this)`, `[esp+4]=native source surface`, `[esp+8]=srcRect(NULL)`,
`[esp+0xC]=destination backbuffer`, `[esp+0x10]=dstRect(NULL windowed / &letterbox
rect borderless)`, `[esp+0x14]=filter`. The instruction is 6 bytes, so a 5-byte
`E9 rel32` detour fits without touching the following instruction. A hook here
covers both output modes and can read the wrapper's own chosen destination
rectangle — that is the Stage A observation point and later the adaptation point.

## 4. Reset helper internals (renderer-split J) **[NEW detail]**

`0x363D0` (`ret 4`, takes one stack arg = `D3DPRESENT_PARAMETERS*`):

1. `TestCooperativeLevel` on the device stored at wrapper-object `+0x44`; if the
   result is neither `D3DERR_DEVICENOTRESET` (`0x88760869`) nor `D3D_OK`, return
   early (device lost — no reset attempted).
2. `Release()` the surfaces at wrapper-object `+0x00` and `+0x04`.
3. Call `vtable+0x40` (Reset) on the object at `+0x74` (secondary device-like
   object; argument setup not visible at that call site).
4. **The real device Reset at `0x36441`** using the incoming presentation
   parameters; the HRESULT is not tested — flow proceeds directly to further
   calls (`+0x74` object again, then font/target recreation). This confirms the
   feasibility warning: gating recovery on this helper's return value is wrong;
   a Stage A adapter must capture the HRESULT at `0x36441` itself (or verify
   surface recreation afterward).

## 5. Wrapper ABI direction (why the wrapper cannot be replaced)

`EfzRevival.dll` (renderer-split) **statically imports** from `Ddraw.dll`:
`EfzRender_addBox`, `EfzRender_addText`, `EfzRender_clearBoxes`,
`EfzRender_clearText`, `EfzRender_initTextRender`, `EfzRender_isActiveWindow`,
`EfzRender_setRenderText`, `EfzRender_toggleFullScreen`, `getEfzInput`,
`getEfzTimer` (IAT around RVA `0x1C1898`). Any wrapper replacement must provide
this exact export set or the Revival DLL fails to load. Verified with
`tools/verify_anchors.py imports`. The original-J package exports
`DirectDrawCreate`, `getEfzInput/getEfzTimer/getEfzRender` instead — mutually
incompatible with the split package, matching InGameNetplay's admission comments.

## 6. Loader contract: EfzModManager 1.1.0 (binary analysis)

The local copy is a binary distribution (`dinput.dll` + `EfzModManager.ini` +
`mods\`); findings from PE analysis of that binary:

- **Load route:** `efz.exe` statically imports `DINPUT.dll`; the proxy in the
  game folder wins. Host gate: process name must be `efz.exe`.
- **Mod layout:** `mods\<Name>\<Name>.dll` (folder name must equal DLL name);
  enabled via `[Mods]` `<Name>=1` in `EfzModManager.ini`; load order = INI file
  order; first match wins for its file-overlay redirection (CreateFileW/A
  Detours hooks — the only shared facility it provides).
- **Timing [important for us]:** mods are `LoadLibrary`'d synchronously inside
  the proxy's own `DllMain` on the main thread under loader lock, before
  `efz.exe`'s entry point. No entry-point protocol exists — no `CheckVersion` /
  `Initialize` exports are called; every mod self-bootstraps from its DllMain.
  Mods are never unloaded.
- **Consequences:** our `DllMain` must do almost nothing (the current stub is
  aligned with this); all initialization — including admission, hooking, and any
  D3D work — must happen on a worker thread after load, exactly as InGameNetplay
  does. Do not rely on CWD; the loader resolves `mods\...` relative to the
  process CWD, but our module path should come from `GetModuleFileName`.
- It provides **no** window-message dispatch, render hooks, config API, or
  inter-mod communication.

## 7. InGameNetplay integration (live tree: branch `0.5.5`, HEAD `7a87767`, clean)

Note: the feasibility report audited `af22f810`; the live tree has moved. All
line references below are to the live tree.

- **Package admission is fail-closed.** `ProfileMatchesDllFile`
  (`src/netplay/bridge/revival_launcher_probe.cpp:378-430`) hashes the loaded
  `EfzRevival.dll` and (for the split profile) the companion `Ddraw.dll` via
  `GetModuleHandleA`, plus the launcher EXE as parent process. The split-J
  profile (`include/netplay/bridge/revival_addresses.h:987-1072`) expects exactly
  the hashes we verified on disk. On mismatch the frontend goes **passive**
  (no hooks, native Revival keeps running). Our display module must not alter
  any of those files or their load behavior.
- **InGameNetplay does not import the Ddraw `EfzRender_*` exports itself
  [correction to feasibility section 4's framing].** It resolves only `init`
  from `EfzRevival.dll` (`ipc_shared.cpp:2901`) and calls the renderer-split
  text API through **thunks inside EfzRevival.dll** at RVAs `0x78B80`
  (`clearText`) and `0x788D0` (`setTextEnabled`) — those thunks forward into the
  paired `Ddraw.dll`. So the Ddraw↔Revival coupling is real but consumed
  indirectly.
- **D3D footprint:** one lazy EndScene MinHook installed via a temporary 2x2
  software-VP device created with **`D3DCREATE_FPU_PRESERVE`**
  (`src/netplay/hooks/battle_log_menu.cpp:2588-2602`; the comment documents the
  x87-single-precision hazard). No Present/Reset/device hooks; resets are
  detected by watching backbuffer size (`src/netplay/hooks/debug_overlay.cpp:1356-1377`).
  Implication: our StretchRect-site work does not collide with it, but any extra
  BeginScene/EndScene we add (Sharp/xBR stages) will re-trigger its EndScene
  hook — upstream Soku's scene-guard/stage-surface strategy (section 8) exists
  precisely for this and must be ported when shaders arrive.
- **WndProc removal semantics confirmed.** `InstallNetplayWindowHook` /
  `RemoveNetplayWindowHook` (`src/netplay/hooks/title_core.cpp:479-611`) only
  understand their own predecessor pointer and `DebugWndProc`'s
  (`debug_overlay.cpp:1227-1268` `ReplaceChainedWindowProc`). A foreign subclass
  above `NetplayWindowProc` makes removal fail (logged "ownership changed",
  hook deliberately left installed) and later installs refuse. Confirms the
  feasibility constraint: **no WndProc subclassing from this module** until a
  cooperative contract exists.
- **Startup ordering:** InGameNetplay's worker polls admission for up to 10 s
  before installing its EXE-side title hooks; it also captures a tournament-EXE
  preimage at process attach and notes native Revival installs Tournament EXE
  hooks immediately after its DllMain returns. Our module should therefore
  avoid touching `efz.exe` code bytes entirely (Stage A does not need any EXE
  patch) and should treat hook-install timing as unordered relative to the
  frontend.

## 8. Upstream port map (SokuDisplayManager 1.3.0)

From the vendored source (`references/SokuDisplayManager-master/src/DisplayManager.cpp`, "DM.cpp"):

**Liftable nearly verbatim (game-agnostic):**
- `computeOutput()` (DM.cpp:502-547): mode enum FitToScreen/IntegerScaling/CustomResolution;
  fit = `min(bbW/srcW, bbH/srcH)` scale with +0.5 rounding, clamped to backbuffer;
  integer = `src * clamp(n, 1, floor(min(bbW/640, bbH/480)))`; custom = raw clamped.
  Destination rect is always centered: `x=(bbW-outW)/2, y=(bbH-outH)/2`.
- Final-pass sequence in `mySCPresent` (DM.cpp:1080-1131): 1:1 grab StretchRect
  into an RT texture → `fillBorders` (4 ColorFill rects) → shader quad or
  filtered StretchRect upscale into the centered rect. This maps directly onto
  our hook at `0x279C0`: replace the wrapper's blit with this sequence.
- `drawShaderQuad` (DM.cpp:873-915): FVF `XYZRHW|TEX1` triangle strip with the
  **−0.5 half-pixel offset** on the destination rect (required for sharp-bilinear
  at integer scales), sampler states CLAMP + filter, Z/blend/cull/lighting off,
  full `D3DSBT_ALL` state block Capture/Apply around the pass (nothing
  hand-restored except render target).
- Sharp-bilinear (ps_2_0; `src/sharpbilinear.h`) and xBR (ps_2_b; `src/xbr.h`)
  bytecode and constant layouts port as-is — both take source size in c0 and
  work with fixed 640x480 input. Capability failures fall back to a plain
  filtered StretchRect — that fallback chain is the Stage-B safety net.
- **EndScene double-run protection** (DM.cpp:1700-1720, 879-911): stash the
  runtime's own BeginScene/EndScene pointers from the device vtable and call
  them directly when all three live in `d3d9.dll`; otherwise draw into an
  offscreen stage surface and blit back, and bind a 64x64 "scene guard" RT while
  scene brackets run so inline-Detour overlays land on the throwaway target.
  This is the mechanism that keeps InGameNetplay's EndScene ImGui pass from
  double-running or being clobbered — port it with the shaders, not before.
- Reset lifecycle pattern: `myReset` vtable hook (DM.cpp:1563-1580) +
  `beforeReset`/`afterReset` (release/recreate default-pool resources; DM.cpp:1462-1508)
  is the game-agnostic template. For EFZ, the real boundaries are the wrapper's
  own reset helper and surface recreation (section 4 above), so Stage A will
  gate on the `0x36441` Reset call and post-reset surface recreation instead of
  vtable-hooking Reset from outside.
- Config machinery (GetPrivateProfile* load at DM.cpp:2179-2360, write-back at
  2397-2441): keys `Mode` (FitToScreen|IntegerScaling|CustomResolution),
  `FullscreenScale`/`WindowScale` (`x2` style), `CustomWidth/Height`,
  `BackgroundColor` (RRGGBB), `Filter` (Point|Linear|Sharp|xBR|Auto),
  `Sharpness`, plus hotkeys/menu sections we do not need initially. A reduced
  `[Display]` schema (Mode, Scale, CustomWidth/Height, Filter, BackgroundColor,
  Enabled, Log) is the Stage A/B target.

**Must be rewritten for EFZ:** all th123 constants and addresses (DM.cpp:67-118),
swapchain-Present interception and `isGameSwapChain`, sprite/background detours
(1164-1432), game reset-wrapper hook and device-listener registration
(1510-1680), IME/Win-key options, DPI block (revisit later), and the Soku
bootstrap. Per feasibility §16.4, the sprite-filter installer is excluded
entirely from the EFZ build.

## 9. Stage A design implications (agreed constraints for implementation)

1. **Bootstrap:** mod DLL at `mods/DisplayManager/DisplayManager.dll`; DllMain
   logs only; a worker thread waits for `GetModuleHandleA("Ddraw.dll")` +
   `GetModuleHandleA("EfzRevival.dll")`, then admits by hashing the on-disk
   files (same method as InGameNetplay: CryptAPI SHA-256 of the module file
   path) against the two pinned profiles (split-J first; original-J later).
   Admission failure ⇒ log + stay inert (native path untouched).
2. **Observation hook:** 5-byte detour at Ddraw RVA `0x279C0` (split profile),
   installed only after byte-comparing the site against
   `ff 90 88 00 00 00`. Stage A step 1 is **forward-only**: reconstruct the
   call (`call [vtable+0x88]` with the original stack args) via trampoline and
   log the observed source/destination/filter geometry per mode. No behavior
   change until the forwarding output is proven identical.
3. **Adaptation:** replace the wrapper's destination rect (and later filter)
   with our own computed rect using upstream `computeOutput` math; the native
   source, backbuffer, and Present stay exactly where the wrapper put them.
4. **Reset:** never vtable-hook Reset in Stage A; instead hook (or poll) the
   real Reset at `0x36441` to capture its HRESULT and release/reacquire only
   our own resources around the wrapper's recreation sequence. Same-size resets
   must be handled as resets.
5. **FP discipline:** the wrapper's borderless path itself saves/sets/restores
   the x87 control word; our code must preserve the FP environment on the game
   thread (no device creation without FPU_PRESERVE; save/restore CW around our
   float work in the hook).
6. **No WndProc, no EXE patches, no second timer/limiter/input.**

## 10. Open questions (not blocking Stage A start)

- What writes `0xDF2EC` (the wrapper's filter global) and what values does it
  take at runtime — does the wrapper expose a filter setting through its own
  config/UI? (If a supported path exists, Point/Linear might be reachable
  without touching the blit — but aspect/integer scaling still needs the hook.)
- Identity/role of the wrapper object at `+0x74` in the reset helper (secondary
  device?) and of global `0xDF290`; harmless unknowns for Stage A, worth
  resolving before Reset-stage work.
- Whether the game ever presents through a path other than Flip (window
  resize/minimize) — runtime observation will answer this.
- Original-J profile: same anchors verified statically, but its borderless
  branch target (`0x27A20` equivalent at ~`0x279D4`?) has not been traced to the
  same convergence; verify before enabling that profile.
