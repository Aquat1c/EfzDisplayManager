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
**Second pass (same day):** the four open questions of the first pass are now
resolved — see section 10. The globals interpretation in section 3 was refined:
the state block at `0xDF280` is a thiscall renderer object with the
`D3DPRESENT_PARAMETERS` embedded at `+0x0C`.

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
     (`vtable+0x20`), then x87 aspect math reading the presentation parameters'
     first two dwords (`0xDF28C` = BackBufferWidth, `0xDF290` =
     BackBufferHeight), with explicit rounding-control save/set/restore
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

**Renderer object layout [NEW, refined in pass 2]:** the whole globals cluster
is one static thiscall object at `0xDF280` — every wrapper method takes
`ecx = 0x64CDF280`:

| Offset | Global | Meaning |
|---|---|---|
| `+0x00` | `0xDF280` | conversion/staging surface (`IDirect3DSurface9*`) |
| `+0x04` | `0xDF284` | native 640x480 target surface |
| `+0x0C` | `0xDF28C` | `D3DPRESENT_PARAMETERS` (0x38 bytes, spans to `0xDF2C3`) |
| `+0x44` | `0xDF2C4` | `IDirect3DDevice9*` |
| `+0x4C` | `0xDF2CC` | HWND (monitor/window queries) |
| `+0x58` | `0xDF2D8` | device type used at `CreateDevice` (1 = HAL) |
| `+0x6C` | `0xDF2EC` | `D3DTEXTUREFILTERTYPE` for both blits (see section 10, Q1) |
| `+0x70` | `0xDF2F0` | init-failure byte |
| `+0x71` | `0xDF2F1` | borderless flag byte (inverted by the toggle) |
| `+0x74` | `0xDF2F4` | D3DX font object (`D3DXCreateFontW`, d3dx9_43.dll) |
| `+0x78` | `0xDF2F8` | text-rendering enabled byte |

Known methods (thiscall, `this = 0x64CDF280`): `0x35630` render text,
`0x36510` render boxes/lines, `0x359E0` output-mode query, `0x361C0`
fullscreen toggle, `0x363D0` reset helper, `0x354F0` destructor. The text
method calls `SetRenderTarget(0, native)` / `GetRenderTarget` around its draw
(vtable `+0x94`/`+0x98`). `CreateDevice` is called with
`BehaviorFlags = 0x22` = `D3DCREATE_FPU_PRESERVE |
D3DCREATE_SOFTWARE_VERTEXPROCESSING` — the wrapper already preserves the FPU.
The wrapper depends on **d3dx9_43.dll** (`D3DXCreateFontW`, `D3DXCreateLine`).
`getEfzTimer` returns the timer object at RVA `0xDF4A0`.

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
2. `Release()` the surfaces at wrapper-object `+0x00` and `+0x04` (staging and
   native).
3. Call `vtable+0x40` on the object at `+0x74` — resolved in pass 2 as the
   **D3DX font's `OnLostDevice`** (the wrapper imports `D3DXCreateFontW` /
   `D3DXCreateLine` from d3dx9_43.dll; `+0x74` is the font object guarding all
   text drawing, with the enable byte at `+0x78`). The post-Reset call at
   `+0x44` is the matching **`OnResetDevice`** — so the helper follows the
   canonical D3DX loss/reset sequence and is more correct than the feasibility
   report implied; only the Reset HRESULT is still discarded.
4. **The real device Reset at `0x36441`** using the incoming presentation
   parameters; the HRESULT is not tested — flow proceeds directly to the font's
   `OnResetDevice` and further recreation. This confirms the feasibility
   warning: gating recovery on this helper's return value is wrong; a Stage A
   adapter must capture the HRESULT at `0x36441` itself (or verify surface
   recreation afterward).
5. The helper has exactly one caller: the reset block at `0x27BA0` inside
   Flip's borderless path (`this = 0x64CDF280`, arg = `&PP`), which then
   returns `DD_OK` from Flip **without presenting that frame**; the next Flip
   presents in the new mode. Reaching this block is how the fullscreen toggle
   applies a mode change (see section 10, Q3).

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

## 10. Open questions — resolved (second pass, 2026-10-20)

All four first-pass questions were closed by further static analysis
(`tools/scan_refs.py` cross-references, byte-pattern scans, and targeted
disassembly of the same hash-pinned binaries).

### Q1 — Filter global `0xDF2EC` (struct `+0x6C`): internal, no config path

- Exactly two writers, both in the renderer-init method at `0x26C50`
  (thiscall, reached from the init function at `0x26DC0` during surface/desc
  processing): `0x26C91: mov [0xDF2EC], 0` (`D3DTEXF_NONE`) written first, and
  `0x26D90: mov [0xDF2EC], 2` (`D3DTEXF_LINEAR`) reached when
  `(flags & 0x02000200) == 0x02000200`, where `flags` is the method's
  third argument (a caps/flags dword; `0x200` matches
  `DDSCAPS_PRIMARYSURFACE`). Initial `.data` value is 0.
- **No external control exists**: the wrapper's only file I/O string is its
  `logDdraw.txt` log (plus an embedded edition check string, "Only use
  DDRAW.dll with EFZ -Memorial- edition, version 4.02"). No INI, no export,
  no UI sets the filter. Point/Linear behavior cannot be changed through any
  supported wrapper path — the blit-site hook remains the only lever, and it
  must read the effective filter from the stack argument at runtime rather
  than assume NONE or LINEAR.

### Q2 — Reset helper `+0x74` object and `0xDF290`: D3DX font; PP dword

- `+0x74` (`0xDF2F4`) is the wrapper's **D3DX font object**, created via
  `D3DXCreateFontW` (imports also include `D3DXCreateLine`; d3dx9_43.dll).
  It guards all text drawing (`+0x78` enable byte); the text method
  (`0x35630`) switches the render target to the native surface around the
  draw. The reset helper's two `+0x74` vtable calls are the font's
  `OnLostDevice` (before Reset) and `OnResetDevice` (after) — the canonical
  D3DX sequence, folded into section 4 above.
- `0xDF290` is the **second dword of the `D3DPRESENT_PARAMETERS` embedded at
  `0xDF28C`** (standard layout: BackBufferWidth at `0xDF28C`, BackBufferHeight
  at `0xDF290`; the struct spans `0x38` bytes to `0xDF2C3`, with the device
  pointer immediately after at `0xDF2C4`). The borderless aspect math reads
  exactly these two dwords. The full object-field map is in section 3.
- Bonus: `CreateDevice` uses `BehaviorFlags = 0x22`
  (`FPU_PRESERVE | SOFTWARE_VERTEXPROCESSING`), and the init function's
  error path sets byte `0xDF2F0` and logs.

### Q3 — Present paths: Flip only; Blt is a stub; toggle defers to next Flip

- The emulated `IDirectDrawSurface7` vtable lives at `.rdata 0xF9128` with
  **Flip at slot 11** (`+0x2C`) exactly per the ddraw.h layout. **Blt (slot 5,
  `0x27850`) and BltFast (slot 7) are unimplemented stubs**
  (`xor eax,eax; ret`) — there is provably no present path other than
  Flip → shared final `StretchRect` → `Present`, plus the reset block
  (below) which skips that frame's present.
- **Fullscreen toggle** (`EfzRender_toggleFullScreen` export at `0x23400`
  → method `0x361C0`): inverts the borderless byte at `+0x71` (`0xDF2F1`),
  rewrites the window style (`GetWindowLongA(GWL_STYLE)` → strip
  `WS_CAPTION|WS_THICKFRAME`-family bits / OR the borderless style →
  `SetWindowLongA`, then a `SWP_FRAMECHANGED`-style `SetWindowPos`). It does
  **not** reset or present directly — the reset happens on the next Flip via
  the `0x27BA0` block calling the reset helper, and that Flip returns `DD_OK`
  without presenting.
- **The wrapper owns the render window**: class `"ETERNAL FIGHTER ZERO"`
  (string at RVA `0xDF190`), created at `0x28957` (`CreateWindowExA`),
  registered at `0x28890` with WndProc **`0x24910`**. The WndProc:
  - `WM_KEYDOWN/WM_SYSKEYDOWN` + `wParam == VK_RETURN` + `lParam & 0x20000000`
    (Alt context) → calls the same toggle method `0x361C0` — **Alt+Enter is
    implemented by the wrapper**, not the game.
  - `WM_SIZE` → logs (`logDdraw.txt`) and forwards; no resize/reset logic,
    confirming the feasibility claim.
  - One private message `0x2000` is swallowed (returns 0).
  - Everything else **tail-jumps to absolute VA `0x401670` inside efz.exe** —
    a hardcoded game-address dependency (assumes the exe at base `0x400000`).
    Co-loaded mods (including our module) must not break this forwarding, and
    InGameNetplay's window hooks operate on this wrapper-owned window.
- Residual runtime-only checks (non-blocking): whether the game actually calls
  Blt/BltFast (harmless no-ops if it does), and the meaning of private
  message `0x2000`. The Stage A observation hook will log Flip activity and
  can confirm no other present traffic exists.

### Q4 — Original-J convergence: identical, at shifted RVAs

Traced end-to-end in the original-J binary (evidence:
`docs/evidence/ddraw-orig-j.txt`):

| Role | Original J | Renderer-split J |
|---|---:|---:|
| Conversion helper | `0x27640` | `0x27700` |
| Conversion blit | `0x27868` | `0x27928` |
| Mode-query method | `0x357A0` | `0x359E0` |
| Borderless branch | `0x27960` | `0x27A20` |
| Convergence `jmp` | `0x27ADB → 0x278EA` | `0x27B9B → 0x279AA` |
| Final StretchRect | `0x27900` | `0x279C0` |
| Present | `0x2793C` | `0x279FC` |
| Reset block | `0x27AE0` (helper `0x36220`) | `0x27BA0` (helper `0x363D0`) |

Same structure in both: the borderless branch computes the letterbox rect
(reading PP dims at `0xDF28C`/`0xDF290`), sets `dstRect`/`filter` from the
same globals, jumps into the shared argument setup, and hits the single final
`StretchRect`. Both use filter global `0xDF2EC`. **The port design holds
unchanged for both profiles**; only the RVA table differs. The profiles in
`tools/verify_anchors.py` now encode all of these anchors and PASS on both
binaries.

## 11. Residual runtime unknowns — resolved by the Stage A observation run

The v0.2.0 `stage-a-observe` module ran inside the game on 2026-10-20
(evidence: [evidence/runtime-observe-1.txt](evidence/runtime-observe-1.txt);
~3600 forwarded flips over ~57 s, windowed, then clean termination):

- **Native source at the final blit:** 640x480, `D3DPOOL_DEFAULT`,
  `D3DFMT_X8R8G8B8` (fmt 22) — exactly the surface class the feasibility
  report described.
- **Output backbuffer:** 1280x960 (4:3 windowed), same format; PP globals
  `0xDF28C`/`0xDF290` confirmed as BackBufferWidth/Height (1280/960).
- **Windowed `dstRect = NULL`** (full-output stretch) — static analysis
  confirmed at runtime.
- **Effective filter = 2 (`D3DTEXF_LINEAR`)** — the game's surface caps satisfy
  the wrapper's LINEAR condition, so stock upscale is bilinear. The
  Point/Sharp stages will be the first visible change from adaptation.
- **Cadence:** ~62.5 flips/s sustained — consistent with the wrapper's
  63.9 Hz timer domain; no resets or surface recreation occurred during the
  windowed run (native-surface pointer never changed).
- Still unobserved at runtime (harmless): whether the game ever calls the
  stubbed Blt/BltFast, message `0x2000`'s meaning, and the borderless-path
  runtime geometry (dstRect letterbox values). The observation hook logs all
  of these automatically when they occur.

## 12. Stage A implementation record (v0.2.0 → v0.5.2, 2026-10-20)

**v0.5.2 all-copies support:** DisplayManager now admits every Revival
wrapper generation found across the user's game copies. A disk-wide
inventory (hash + PE timestamp) found SEVEN distinct Revival DDRAW.dll
binaries; all seven are now pinned:

| Wrapper | SHA-256 prefix | Notes |
|---|---|---|
| Renderer-split 1.02j | `0e072342…` | since v0.2.0 |
| Original 1.02j | `e18abe00…` | profile since v0.4.0 (static) |
| 2020-generation | `c486903e…` | EFZ_clean, EFZ_RELAY, F:\EFZ, F:\EFZ - Copy (1.02e), F:\EFZ - Copy (2), capture_diag |
| 1.02f | `712a45d3…` | D:\EFZ + Reviva 1.02f |
| 1.02g | `295b3f3f…` | D:\EFZ+Revival 1.02g |
| 1.02h | `4d83369b…` | D:\EFZ + Revival 1.02h, F:\…Concerto pack |
| 1.02i | `606601cc…` | F:\EFZ + Revival 1.02i (code shifted +0xB0) |

The 2020 generation (all five new builds) shares the J-generation
renderer-object layout exactly - verified via its exported MSVC-mangled
symbols (`?isFullscreen@EfzRender` = `mov al,[ecx+0x71]` confirming
borderless +0x71, `?initTextRender` confirming font +0x74), the same
final-blit bytes (`ff 90 88 00 00 00`), and `Flip ret 0xC`; only code RVAs
differ (1.02f/g/h identical to the 2020 original; 1.02i shifted). The
non-Revival wrappers under `gamecopy/` (memorial, 2v2, netmatch) and the
odd 580,608-byte variant are intentionally NOT admitted - different
projects' binaries.

A transcription lesson worth recording: the first hand-typed hash literals
contained late-byte typos that only surfaced at runtime (admission
correctly refused); literals are now generated programmatically from the
actual files and locked by a new test that hashes each real wrapper
against its profile (five machine-verified PASS cases; 47 tests total).
Deployed to all eight manager-equipped copies and runtime-verified live:
profile accepted + hooks active + adaptation logging in every one,
including the user's original 1.02e test copy (`F:\EFZ - Copy`). Evidence:
[evidence/runtime-2020gen-relay.txt](evidence/runtime-2020gen-relay.txt).
The original-J backup folder has no mod manager; its profile is
static-verified and will be runtime-confirmed when that copy gets a loader.

The sections below describe earlier versions as implemented at the time.

**v0.5.1 reference alignment** (against `D:\dev\qoh_99\ddraw_wrapper`, the
user's QOH'99 wrapper, reviewed as the exclusive-fullscreen reference with
low input latency as the goal):

- `PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE` is now the
  exclusive default (the reference's deliberate low-latency choice); the
  wrapper's interval is no longer inherited into exclusive mode, where at
  60 Hz it would both add latency and fight the 63.9 Hz game timer.
  `VSync=1` opts into vsync explicitly.
- Exclusive backbuffer format now comes from
  `IDirect3D9::GetAdapterDisplayMode` (the reference's D3D-side truth)
  instead of inheriting the wrapper's windowed format.
- Exclusive is refused unless the DEVICE'S adapter owns the window's monitor
  (`GetAdapterMonitor` == `MonitorFromWindow`), like the reference's
  multi-adapter guard; logged as a refusal.
- Window prep per the reference: style/exstyle/rect saved once, then
  `WS_POPUP|WS_VISIBLE` + `WS_EX_APPWINDOW` covering the monitor during
  exclusive (no caption flash), restored exactly on exit - including on a
  failed enter.
- Not ported (documented): the reference's D3D9Ex FlipEx windowed path with
  `SetMaximumFrameLatency(1)` - it requires recreating the device, which is
  the wrapper's; and their Fullscreen-Optimizations caveat applies to us
  too.
- Live-verified: startup enter at 2560x1440@240Hz, fmt=22 (desktop format),
  interval=immediate, surfaces 1/1, letterbox intact, adapter validated;
  focus-loss lost/recovered cycle clean. Evidence:
  [evidence/runtime-exclusive-fs-v2.txt](evidence/runtime-exclusive-fs-v2.txt).
  F11-exit under SendKeys automation remains focus-starved; the exit path is
  the same verified reset machinery + unit-tested parameter/window restore -
  confirm with one manual keypress.

The sections below describe earlier versions as implemented at the time.

**v0.5.0 `exclusive-fullscreen` (ported with the creator's permission):**
exclusive fullscreen at the window monitor's desktop mode (no display-mode
change), driven entirely from the Flip-entry hook - F11 edge-detected via
`GetAsyncKeyState` (no WndProc; the InGameNetplay window-hook contract stays
intact), `Fullscreen=1` INI starts exclusive, `VSync` (-1/0/1) overrides the
presentation interval at every reset we perform. Presentation-parameter
manipulation is pure and unit-tested (`fullscreen.cpp`: enter-exclusive
fields, windowed snapshot/restore round-trip, vsync choices; 41 tests total).
Live-verified: F11 enter at 2560x1440@240Hz with surface recreation 1/1 and
the correct 4:3 letterbox; a real focus-loss DEVICELOST storm (~2.6 s, 597
lost frames) ridden out with one clean DEVICENOTRESET recovery back into
exclusive; `Fullscreen=1` startup enter. Evidence:
[evidence/runtime-exclusive-fs.txt](evidence/runtime-exclusive-fs.txt).
The exit path is the same reset machinery in reverse (snapshot-restored
windowed parameters, rollback on failure); SendKeys automation could not
reliably deliver the second F11 (focus fighting), so exit is verified by
construction + startup-enter symmetry - confirm F11 exit by hand.
Device-resource mods (training/netplay) gate exclusive off for the session,
same as resize. The DEVICELOST/NOTRESET announcements are one-shot.

The sections below describe earlier versions as implemented at the time.

**v0.4.x coexistence hardening (same day):** live testing with a
resource-holding probe mod (a D3D9 DEFAULT-pool texture, standing in for the
ImGui buffers of efz-training-mode / efz_netplay_mod) established the hard
ecosystem boundary: `IDirect3DDevice9::Reset` returns `D3DERR_INVALIDCALL`
while any co-loaded mod holds default-pool resources, so backbuffer resize
is *impossible* alongside those mods until they release resources around
resets (the cooperation contract: hook Reset, invalidate on pre, rebuild on
post - their overlays already rebuild after observed resets). Consequences
implemented and live-verified:

- Resize gates itself off when `efz_training_mode.dll` / `efz_netplay_mod.dll`
  (or the in-tree `ResetProbe.dll` fixture) is loaded, with an explanatory
  log; the window still maximizes via the DWM stretch. Verified: probe
  loaded → graceful disable, game survives maximize/restore.
- `WindowDragResize` defaults ON (corner-drag requested): live drag runs
  Win32's modal size loop and pauses the game until mouse-up - inherent,
  not fixable without WndProc-level pumping (forbidden by the InGameNetplay
  window-hook contract). A flip-stall detector (>500 ms) now logs any such
  pause as evidence.
- Clean run re-verified: style `14ca0000 → 14cf0000` (maximize + drag),
  maximize resets to 2560x1377 with surface recreation 1/1 and the proper
  aspect letterbox `adapt: -> (362,0,2198,1377) 1836x1377`; restore returns
  to Revival's configured 1280x960. Evidence:
  [evidence/runtime-window-resize-v2.txt](evidence/runtime-window-resize-v2.txt).
- Toolchain note: the v141 MSBuild CL environment intermittently emitted
  phantom `C2065: 'IDirect3Surface9' undeclared` on valid code (an exact
  replica of its command line compiles clean; single-file invocations are
  deterministic). `tools/build-xp.bat` (direct cl, one TU per invocation) is
  the authoritative build; the code also avoids late-function
  `IDirect3Surface9*` declarations in dmFlipEntryWork as belt-and-braces.

The sections below describe v0.2.0/v0.3.0 as implemented at those versions.

**v0.4.0 `window-resize`:** the window is now sizable and maximizable, and the
backbuffer follows user-driven size changes while Revival's
`EfzRevival.ini [Global]` configuration stays authoritative at rest.

- Static findings behind the design: the wrapper creates its window with
  style `0x10CA0000` (`WS_VISIBLE|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX` —
  fixed-size, grayed maximize button, at the `CreateWindowExA` call at RVA
  `0x28957`); the wrapper's only own-reset trigger is a FAILED `Present`
  (branch at `0x27A04` → reset block `0x27BA0`), and its reset helper
  (`0x363D0`) dereferences the D3DX font at `+0x74` WITHOUT a null check —
  a latent wrapper bug (crash at `DDRAW+0x36426`) that fires in direct
  efz.exe launches where Revival's text render never initializes. The
  wrapper also caches surface pointers in registers across Flip, so a
  mid-Flip reset would leave it blitting from freed surfaces - the only
  safe voluntary-reset point is Flip ENTRY (all pointers reloaded from
  globals afterward, no backbuffer reference outstanding).
- Implementation: one-time style patch (`WS_THICKFRAME|WS_MAXIMIZEBOX`,
  client-preserving via `AdjustWindowRect`, before hooks install so the
  resize baseline captures the adjusted size); a Flip-entry naked-shim
  detour with a 7-byte trampoline; a font-safe OWN reset sequence
  (GetDesc → font OnLostDevice → release/null staging+native → update PP →
  `IDirect3DDevice9::Reset` → recreate surfaces with captured descs → font
  OnResetDevice); and a pure, unit-tested resize decision (stability window
  8 flips, cooldown 30, minimum client 320x240, borderless never triggers,
  restore-to-original-client ⇒ restore Revival's original backbuffer).
  Runtime evidence incl. live user drag-resizing:
  [evidence/runtime-window-resize.txt](evidence/runtime-window-resize.txt)
  — every reset `hr=00000000`, aspect-correct at every size, Revival's
  1280x960 backbuffer restored exactly when the window returns to 640x480.

The sections below describe v0.2.0/v0.3.0 as implemented at those versions.

**v0.3.0 `adapt-rect` (same day):** output-geometry adaptation is live and
runtime-verified. The hook now computes a centered destination rectangle from
the actual surface dimensions (aspect-fit / integer / custom, pure integer
arithmetic — no floating point in the hook path at all), optionally overrides
the filter (Point), and fills letterbox borders with a configurable color
before the blit. Defaults (`FitToScreen` + `Linear`) reproduce native output
exactly on a 4:3 output (verified: rect `(0,0,1280,960)`, filter passthrough).
A full-adaptation run (`IntegerScaling=1` + `Point`) produced the centered
640x480 rect `(320,240,960,720)` with filter override and borders, stable
in-game. Evidence:
[runtime-adapt-default.txt](evidence/runtime-adapt-default.txt),
[runtime-adapt-integer1.txt](evidence/runtime-adapt-integer1.txt).
Geometry is unit-tested (`dm_sha256_test`, 31 cases total); one code/doc
mismatch (IntegerScale=0 semantics) and two wrong test expectations were
caught by the tests before deployment.

- Module: `mods/DisplayManager/DisplayManager.dll`, loaded by EfzModManager;
  DllMain only logs + starts a worker (loader-lock discipline).
- Admission: DDRAW.dll file SHA-256 + loaded PE timestamp against two pinned
  profiles (renderer-split J and original J — two different 1.02j Ddraws
  exist; each carries its own anchor table). Mismatch ⇒ inert, native path
  untouched. The fail-closed gate caught a real transcription error in the
  first deployed pin (`…6ada9f60` vs correct `…6ad9f603`) before any hooking —
  the design worked as intended.
- Hook: 6-byte site `0x279C0` (split) / `0x27900` (orig) byte-verified, then
  patched with `E8 call rel32 + NOP` under all-threads-suspended; a
  `__stdcall` 6-arg handler reproduces the original stdcall stack effect
  exactly and forwards every call to `IDirect3DDevice9::StretchRect`
  unchanged.
- Toolchain (XP requirement): MSVC v141 (14.16), `_WIN32_WINNT=0x0501`,
  `/SUBSYSTEM:WINDOWS,5.01`, static-linked **without any CRT**
  (`/NODEFAULTLIB /ENTRY:DllMain`, in-tree memcpy/memset and a custom
  formatter) — the stock v141 CRT imports Vista+ `FlsAlloc`/
  `InitializeCriticalSectionEx` and would not load on XP x86. Final imports:
  28 KERNEL32 + 2 USER32 functions, all XP-era; PE OS/subsystem version 5.01.
- Build: `cmake -S . -B build-xp -A Win32 -T v141 && cmake --build build-xp
  --config Release`. Self-tests: `dm_sha256_test` (FIPS vectors, real-file
  hash pins, formatter cases incl. the overload-ambiguity regression).
- Two implementation bugs were found and fixed during bring-up, both caught by
  the fail-closed design and local tests: (1) the profile hash transcription
  error above; (2) an MSVC x86 overload-ambiguity where
  `formatString(out, cap, fmt, charPtr)` resolved to the `va_list` overload
  (va_list **is** char* on x86) and dereferenced the string as an argument
  cursor — fixed by splitting into `formatString` (variadic) and
  `formatStringV` (va_list), with a regression test.
