# Porting SokuDisplayManager to EFZ

## Feasibility and Revival compatibility assessment

**Prepared for:** Aquat1c / Mykhailo Hudyma  
**Investigation date:** 20 October 2026  
**Scope:** Source review, recovery of prior EFZ documentation, and targeted PE/disassembly verification. This is a feasibility assessment. No display-manager port was implemented or tested in a running game.

**Markdown edition for implementing agents:** This file contains the full assessment from the PDF, followed by an actionable [agent implementation handoff](#16-agent-implementation-handoff). Start with section 16 for the next task, then read the referenced evidence and relevant assessment sections. Repository revisions and binary identities describe the completed 20 October 2026 audit; compare later source changes before integration. The accompanying evidence ZIP preserves the original assessment snapshot. This standalone Markdown edition adds the handoff.

## 1. Recommendation

**The useful display features are feasible, but the work is an EFZ-specific integration of SokuDisplayManager's display code. Retaining the exact bundled Revival Ddraw.dll is the right foundation.** Scaling math, output layout, point/linear/sharp-bilinear/xBR filtering, configuration, and parts of the settings interface can be reused. Device discovery, final-frame interception, reset ownership, window handling, loader startup, and sprite-specific hooks need EFZ implementations.

The strongest result of this investigation is a concrete boundary inside the bundled wrapper: it composes a native 640x480 frame, including its text and rectangle overlays, then scales that surface into the output backbuffer and calls IDirect3DDevice9::Present. A port can adapt that final scaling operation while leaving the wrapper's DirectDraw emulation, palette conversion, timer, input interface, and Revival session behavior in place. A shader pass will need a sampleable texture and carefully managed D3D resources; the identified source is a render-target surface, not automatically a texture. [B01]

My assessment is **medium difficulty for a focused, single-build display enhancement; medium-high for a dependable Revival/InGameNetplay-compatible release; high for the complete Soku feature set across Revival generations.** The filters are not the main uncertainty. Lifecycle and rendering ownership are.

Recommended first scope: one exact renderer-split Revival 1.02j package, existing borderless/windowed behavior, fixed native 640x480 content, aspect-correct and integer scaling, Point/Linear/Sharp filters, and a configuration that remains effective online. Introduce xBR after the basic pipeline is qualified. Treat exclusive fullscreen, live reset-producing changes during online play, and per-sprite filtering as later stages with separate acceptance criteria.

## 2. Documentation recovered and reconciled

The investigation located the previous EFZ master reference and followed its project leads through the connected GitHub repositories. It also recovered the September timing and 1.02j interoperability packages, the older DirectDraw binary/decompilation, the original J corpus, and the newer Revival archive. The new archive's executable/DLL/wrapper hashes match the renderer-split package declared by current InGameNetplay. [D01-D04, I02-I03, B01]

| Material | Role in this assessment | Evidence limit |
|---|---|---|
| EFZ_Master_Reference.md | Map of EFZ projects, shared contracts, older research and archive leads | Historical inventory; its previously recorded branch tips are not current-version proof |
| Aquat1c/EFZ_modding | Shared graphics, hotkey, lifecycle and address documentation; EFZ and Revival decompilation inventory | The README describes a historical local workspace; current root does not contain mod_projects |
| Revival_Timing_Assessment.md and evidence | Timer producer/consumer, batching, render suppression and checkpoint behavior | September report was static; relevant binaries and outer EFZ wait were checked again here |
| Revival_102j_Findings.md and profile map | Distinct J builds; training/render-patch conflicts; FP-state concerns | Older integration baselines; current source was consulted for current behavior |
| EFZ_Palette_Conflict_Report.md | Recent concrete example of an unsafe trampoline over another mod's jump | Specific character-select crash mechanism, not proof of a display-manager crash |
| Current InGameNetplay source | Exact package admission, window-hook removal, online handoff and UI teardown | Source behavior; no live handoff was rerun in this investigation |
| Current training-mode source | Live D3D9 discovery, native target geometry and shared overlay API | Offline overlay interface is not an always-on display API |
| fishshapedfish/EfzModManager | Actual loading mechanism for a companion DLL | Supplies module loading and file overrides, not a render/reset broker |
| Aquat1c/EfzDdrawWrapper | Separate legacy GDI wrapper | Not Revival's required companion wrapper |

The gathered reading packet preserves the four most relevant existing references unchanged: the master reference, timing assessment, J interoperability findings, and palette-conflict report. Repository documents remain linked at fixed commits in the source index. New audit notes and focused disassembly accompany this report.

### Audited repository revisions

| Repository | Revision |
|---|---|
| Aneroze/SokuDisplayManager | a084c66bddd11ddd6134d1e2a47be2853567abe9 |
| Aquat1c/EFZ_modding | 9b1d81a28695035de99ed14a8de6614214785687 |
| Aquat1c/InGameNetplay | af22f8104206dcc1f12d5da5f84789ba14bfed80 |
| Aquat1c/efz-training-mode | b6b14902c16d1dcc6e87336776516c946e53c2d5 |
| fishshapedfish/EfzModManager | 82448bcda1fafd2abb1f528e1bc5945143cf6d45 |
| Aquat1c/EfzDdrawWrapper | 1a9d731c518ab8b0fcfceabd7e1001156ce8048e |

SokuDisplayManager's source version is 1.3.0. Its pinned HEAD differs from the 1.3.0 tag by a CMake static-runtime configuration fix; the rendering source is the same. Training's pinned merge is labeled 1.2.4 while its version header still says 1.2.3. Commit identifiers are therefore the reliable identity for this assessment. [S04, I06]

## 3. What SokuDisplayManager actually provides

The upstream DLL is self-contained Win32/D3D9 code with embedded Dear ImGui and compiled shaders. It does not depend on SokuLib and is not a DirectDraw proxy. Its central implementation is approximately 3,178 lines, plus a 720-line menu header and supporting shader/API/configuration code. The source is closely tied to th123 1.10a despite the absence of a SokuLib dependency. [S01-S04]

| Feature | EFZ reuse potential | Work required |
|---|---|---|
| Fit-to-screen, centered 4:3 output, borders | High | Feed verified EFZ native/output dimensions and one shared geometry calculation |
| Integer scale and custom output rectangle | High | Preserve native 640x480 coordinates; distinguish content size from monitor mode |
| Window size, position and always-on-top | High in principle | Cooperate with EFZ/Revival window ownership, activation and cursor mapping |
| Point and linear filters | High | Apply to the native source at the wrapper's final scale boundary |
| Sharp-bilinear filtering | High shader reuse | Add a source texture, state isolation, reset lifecycle and capability fallback |
| xBR-lv2 filtering | High shader reuse | Pixel Shader 2_b support and performance qualification; retain a cheaper fallback |
| Borderless fullscreen | High conceptual reuse | Coordinate with Revival's existing borderless behavior instead of adding a second owner |
| Exclusive fullscreen and display refresh selection | Partial | Implement around Revival's device/reset/focus rules; the reviewed wrapper uses a windowed device |
| Resizable window with filters | Partial | Resize output backbuffer through the proper owner/thread; avoid stale or double-scaled output |
| Settings menu and persistent INI | Partial | Replace Soku bootstrap/input assumptions and integrate EFZ menu lifecycle |
| Output-rectangle and overlay API | Useful design precedent | Create an EFZ API with explicit ordering, reset and callback-lifetime contracts |
| Per-character/per-stage sprite filtering | Low direct reuse | Rewrite layer/camera/sprite hooks for EFZ's pipeline; high interaction risk |
| MSAA | Limited for the intended EFZ image | Does not reconstruct detail in an already composed pixel-art surface; separate research |
| Win-key and CJK input-mode options | Little value to the core port | Soku-specific input patch and IME behavior should not be carried into the first release |

CustomResolution in upstream settings controls the scaled content rectangle. FullscreenWidth, FullscreenHeight and FullscreenRefresh control the actual display/backbuffer mode. Neither is a true higher-resolution re-render of game content. Scaling an EFZ frame to 1440p does not create new native sprite detail. [S01-S02]

### The upstream code is not a frame-pacing replacement

No separate FPS limiter or input-delay algorithm was found in the display manager. Its VSync option changes the presentation interval for its exclusive-fullscreen path; it otherwise preserves the intercepted Present parameters/flags/results. There is no basis for importing a Soku simulation rate into EFZ. The README's 10-20 ms display-latency statement is not an EFZ measurement and is not used as an estimate in this report. [S01-S02]

### Default settings do not mean all sprite hooks are absent

Current initialization calls the sprite-detour installer. The default filter-cycle hotkey can reach Auto, and the settings menu can enable sprite filtering, so the installer can run even while Point is selected and optional sprite-strength settings are commented out. A safe first EFZ build should omit or explicitly disable that machinery at the installation level, rather than relying on a zero strength setting. [S01, lines 2346-2352 and 3152-3160]

## 4. Why Revival's Ddraw.dll must remain in place

The bundled wrapper is part of the runtime contract, not merely an interchangeable scaler. The original J binary exports DirectDrawCreate, getEfzInput, getEfzTimer, getEfzRender and C++ EfzRender methods. The renderer-split binary keeps the input/timer exports but removes getEfzRender and exposes procedural functions such as EfzRender_addText, EfzRender_addBox, EfzRender_clearText and EfzRender_toggleFullScreen. The newer Revival DLL directly imports the procedural functions. [B01]

Current InGameNetplay checks the exact companion Ddraw.dll when admitting the renderer-split launcher-first package. An arbitrary replacement or modified on-disk wrapper would not satisfy that unchanged admission contract. Exporting only ordinary DirectDraw functions would also be insufficient for the newer DLL's imports. [I02-I03]

### Binary identities checked in this investigation

| Component | Bytes | SHA-256 |
|---|---:|---|
| EFZ executable recovered for outer-loop verification | 3,743,744 | 3a1616d6c76d2eea08a95bb198d853e96cb6b9b3266e277ca1d167426dd66481 |
| Original J Ddraw.dll | 1,314,816 | e18abe007fbb7c48b63ab56027754e0aee196dc753af189949ba2dc46f366f9d |
| Original J EfzRevival.dll | 1,914,880 | cedd606869ee6cfa227b385af27dc92abc13fe9c2d432935bcbe3b789468a169 |
| Renderer-split J Ddraw.dll | 1,314,816 | 0e072342d87d240e18969bc3663032e0e370eaba24d1fbb8e6c1f6216ad9f603 |
| Renderer-split J EfzRevival.dll | 1,911,296 | 62d9ed74b7931e2bc7ea208109425bc25207c82faca91a96a55c778f8d568f32 |
| Renderer-split J EfzRevival.exe | 2,673,664 | 7c7707322b7cda0b1479dfe97a7260d9e971ba162dfce755c894b9626feb1c42 |

The two Ddraw files have identical file sizes but different hashes, PE timestamps, exports and code locations. The two Revival DLLs both identify as 1.02j. Version strings and file sizes alone cannot choose the adapter. [B01, I02]

The separately maintained Aquat1c/EfzDdrawWrapper is an 8-bit software/GDI implementation that presents with StretchDIBits. Its export definition lacks getEfzTimer, getEfzInput and the Revival rendering interfaces. It is useful for older EFZ releases, but is not a shortcut to this Revival-compatible port. The older 814,080-byte DDRAW binary recovered from the user's archive is a third, distinct binary and must not be confused with that GDI source repository. [L02, B01]

## 5. The concrete rendering boundary

The original J raw Ddraw decompilation contains an error instead of a body for its central Flip function. Relying solely on the decompilation would miss the operation that matters most. This investigation recovered the path from the original binary, then independently mapped the equivalent path in the newer companion binary. [B01]

The reviewed wrapper performs the following work:

1. Convert or upload indexed game-surface pixels using the active palette.
2. Copy the result into a native 640x480 D3D9 render-target surface.
3. Render its queued text and rectangle overlays onto that native surface.
4. Get the output backbuffer and choose the full-output or aspect-preserving destination rectangle.
5. Stretch the native surface into that destination.
6. Call IDirect3DDevice9::Present; on failure, enter wrapper-owned reset/recovery behavior.

This is a **device Present call**, using vtable byte offset 0x44. SokuDisplayManager intercepts the implicit swapchain's Present method. A swapchain hook cannot be assumed to observe the reviewed EFZ call path just because both use D3D9. [B01, S01]

### Build-specific anchors

All values in the following table are RVAs relative to the actual loaded Ddraw.dll base. The preferred image base in both examined J wrappers is 0x64C00000. These are research anchors, not a general signature or an instruction to patch unverified memory.

| Role | Original J wrapper | Renderer-split J wrapper |
|---|---:|---:|
| DirectDraw surface Flip entry | 0x277A0 | 0x27860 |
| Native 640x480 surface pointer global | 0xDF284 | 0xDF284 |
| D3D9 device pointer global | 0xDF2C4 | 0xDF2C4 |
| Presentation-parameters storage | 0xDF28C | 0xDF28C |
| Final StretchRect, shared after geometry branches | 0x27900 | 0x279C0 |
| Device Present instruction | 0x2793C | 0x279FC |
| Wrapper reset helper | 0x36220 | 0x363D0 |
| Actual Device Reset instruction | 0x36291 | 0x36441 |
| getEfzTimer export | 0xCF4E0 | 0xCF790 |

The windowed and borderless geometry branches converge at the same final StretchRect call. An adapter must preserve or replace the appropriate destination rectangle for each mode. In the newer binary, text and box calls are at RVAs 0x27944 and 0x2794E; the source is fully composed after them. [B01]

### Why capturing only at Present is insufficient

At Present the wrapper may already have stretched the native image using linear filtering. Upscaling that finished backbuffer again can compound blur, misidentify the source rectangle and clear or move already-composed overlays. The reviewed configuration separates a 640x480 window from a larger output backbuffer, so the backbuffer's dimensions are not evidence of native game resolution. [B01]

The preferred adaptation is to replace or intercept the **final scale operation**, using the complete native source. Point/Linear can remain simple blits. Sharp/xBR need a sampleable texture, likely populated by a 1:1 GPU copy from the native render target, followed by a shader draw into the output backbuffer. The copy needs a supported surface/format/capability combination and a clean fallback. That is a design recommendation grounded in the recovered surface path, not implemented code. [W06]

Selecting a D3D9 render target resets the viewport to that target's full size. A new output shader pass must set its viewport after selecting the target, then restore affected device state. The wrapper's native-target geometry and the new output rectangle are distinct responsibilities. [B01, W05]

The original native target should remain 640x480. Both training and InGameNetplay use that coordinate system. Training expressly skips its overlay dispatch if the active target has another size. Output-size changes belong after that target. [I06]

## 6. Timing, rollback and input must retain their owners

EFZ's screen update dispatch at 0x00401642 is followed by an infinite wait on the engine event at 0x00401652. Those instructions and the WaitForSingleObject import were checked in the recovered efz.exe. Prior analysis traces Revival's batch execution before the outer wait and the bundled Ddraw timer that supplies it. [B02, D02]

The reviewed J batches suppress intermediate rendering and enable the final iteration's render. They also normally restore a retained checkpoint after speculative work. The newer batch and its render gate were independently verified at Revival RVAs 0x4F560 and 0x78620. Consequently, a Present count, a state-load count and a gameplay-frame count represent different things. A display module must not advance input, trigger simulation, or regenerate intermediate rollback frames to satisfy an output refresh target. [D02, B01]

The bundled timer's static initializer uses 63.9 Hz in the examined J builds. The native/mod ecosystem also describes 64 Hz domains, including input and training behavior. This report does not resolve that difference by choosing a new game speed. The active runtime rate can be changed after initialization and must be observed. Preserve the current timer, negotiated input delay, prediction limit, checkpoints, sound reconciliation and update ordering. [D02, B01]

A display-only module should add no separate FPS limiter, global Sleep interception, DwmFlush wait or simulation clock. VSync and extra GPU work can still affect the time before the next update even without changing the network protocol. At a 60 Hz display, enabling a blocking presentation path can constrain a loop whose timer target is approximately 64 Hz. Whether and how that occurs on a specific system requires measurement.

For the first release, retain the wrapper's presentation interval. Treat a VSync or exclusive-mode latency experiment as an independently selectable feature with paired-peer pacing measurements. Do not promise upstream's reported latency saving on EFZ. [S01-S02, W03]

Floating-point state is another compatibility boundary. Current EFZ integrations explicitly use D3DCREATE_FPU_PRESERVE for temporary devices. New calculations and device operations must preserve the relevant x87/MXCSR environment on the executing thread. Microsoft documents the precision behavior of D3D9 device creation. A visual feature is not automatically simulation-neutral if it changes the creating or calling thread's machine state. [I06-I07, W01]

## 7. Mod-loader and window integration

### Load as an ordinary companion DLL

EfzModManager uses a dinput.dll proxy and loads enabled modules as mods/<name>/<name>.dll. This provides a path to install a display module while keeping Revival's Ddraw.dll intact. The actual loader does not invoke Soku's CheckVersion or Initialize exports. The EFZ port needs its own deferred bootstrap, exact-build admission and readiness checks. [L01]

Existing mods initialize substantial work on workers. INI order controls LoadLibrary order, but does not establish deterministic hook installation order. The loader also provides no common window-message or render/reset dispatcher. Those are integration responsibilities, not facilities to assume already exist. [L01, I08]

### A new WndProc can block online handoff

Current InGameNetplay removes its NetplayWindowProc and UI/render hooks before committing Online/Spectator handoff. Its removal routine understands its own procedure and a known predecessor link in DebugWndProc. It cannot splice its procedure out of an arbitrary foreign module's private predecessor pointer. If removal fails, handoff fails. [I04-I05]

Therefore a display module that subclasses above NetplayWindowProc can prevent a session from starting even if it forwards every message correctly through CallWindowProc. This is a concrete inferred failure from current code, not a newly reproduced user crash. Merely specifying a preferred mod order does not address asynchronous bootstrap and later hook reinstallation.

Use a cooperative message owner or an explicit hook-registration/removal contract with InGameNetplay. A minimal proof can initially read configuration at startup without adding display hotkeys. Before enabling interactive controls, verify handoff/removal in both load orders and preserve WM_CLOSE, activation, quit broadcast, return-to-menu and re-host behavior.

### Publish one geometry mapping

The new display module should expose the actual game rectangle in client pixels. Consumers need a consistent mapping between native 640x480 coordinates, output backbuffer pixels and OS client coordinates. Integer letterboxing, a non-4:3 window, mixed DPI monitors and cropped/custom output cannot be represented safely by assuming that the whole window is the game image.

Existing mouse mapping will need to consume that geometry or demonstrate equivalent behavior. Otherwise an overlay can look correct while its controls click at the wrong positions. This is a targeted UI interoperability change; it should not alter EFZ's DirectInput gameplay mapping. [I06, S01]

## 8. Overlay and reset ownership

Training exposes EFZ_RegisterOverlayRenderer / EFZ_UnregisterOverlayRenderer, ABI 1. It is a useful example of sharing one EndScene owner. It is not sufficient for this port: the callback runs only on the native target, is suppressed online, depends on training being active, and has no device-reset or presentation notifications. The header's after-ImGui description differs from current code, which dispatches before the ImGui pass. [I06]

InGameNetplay's shared-state ABI 7 is also not an always-current match or rendering callback. Its worker is deliberately suspended before active rollback; its final snapshot may still describe the preceding menu. stateSeq and lastUpdateTick must be interpreted according to the producer contract. Background HOST_IDLE can coexist with offline training, so a generic hosting flag is not a correct reason to disable all settings. [I05, I08]

The port needs an explicit display lifecycle: device ready, before native reset, reset succeeded, reset failed/device lost, output resized, and module/session teardown. Callback registration must also define in-flight lifetime so a callback cannot execute after its DLL or resources are released.

D3D9 requires default-pool resources, explicit render targets, additional swapchains and state blocks to be released before Reset. Reset must execute on the appropriate creation thread and can send window messages during the call. A worker firing Reset directly, or a size-only heuristic that misses a same-size reset, is insufficient. Preserve the native owner's failure/retry path and do not recreate a second device as a casual workaround. [W02]

There is a specific binary-level complication: the wrapper's reset helper does not preserve the actual Device::Reset result. It continues into font reset and target recreation. Observe the actual Reset result to gate the adapter's resource recovery, then wait for and verify the wrapper's later surface recreation before reacquiring the native source. The helper's final return is not a reliable Reset-success signal. The verified call sites are included above and in the evidence packet. [B01]

The reviewed wrapper's WM_SIZE path logs/forwards rather than implementing Soku's resize/reset system. Its fullscreen toggle changes HWND style/placement while using a windowed D3D device. Exclusive fullscreen and arbitrary live backbuffer resize therefore need deliberate new coordination; they are not accomplished by copying Soku's toggle routine or editing window styles alone. [B01]

A shader pass can issue an extra BeginScene/EndScene pair. Existing mod hooks may run again during that pair and duplicate UI or auxiliary work. The implementation must identify its presentation-only pass and coordinate with existing hooks while saving/restoring the touched render state. Blindly bypassing unknown hook chains is not a sound substitute. Upstream itself contains substantial compatibility handling and historical failures around this problem. [S01, S03]

## 9. Per-sprite filtering is a separate project

Soku's experimental character/stage filtering intercepts its own sprite and object render paths and reads Soku-specific camera state. EFZ's palette/indexed-surface operations and rendering suppression are different. Reusing the shader does not recover the layer identity, atlas coordinates, transparent-pixel behavior or correct blend order for EFZ. [S01, E01]

Our existing J investigation identifies intersections between training's HUD detours and Revival's graphics-suppression sites, including EFZ addresses 0x409A90 and 0x40A0B0. An online flag inside a hook does not arbitrate another component rewriting that entry. The recent palette-conflict report gives another precise example: copying an existing relative jump into a new trampoline without relocation led to the logged wrong branch destinations. [D03-D04]

This history argues for staying after native frame composition in the initial display port. Later sprite filtering would require separate EFZ layer/camera reverse engineering, shared patch ownership, palette/blend verification, capture/replay tests and online regression work. The visual difference between a whole-frame filter and a true per-sprite filter should remain explicit in the UI and release notes.

MSAA is similarly not an automatic improvement for a composed 2D frame. Enabling it on an output surface does not restore lost sprite detail. It may also conflict with the wrapper's lockable, non-multisampled native target. It should not delay a useful first display-manager release. [B01]

## 10. Recommended implementation approach

Three broad approaches were assessed.

| Approach | Assessment |
|---|---|
| Companion EFZ DLL with a versioned adapter to the bundled wrapper | Recommended. Reuses display algorithms and preserves required DLL identity/ABI. Needs precise final-scale, reset and window integration |
| Extend the wrapper itself through a maintained upstream/source change | Architecturally attractive if the correct wrapper source and maintainer contract become available. A changed binary requires coordinated package identity/admission updates; it is not a transparent drop-in under current checks |
| Replace Revival's wrapper with a generic Ddraw implementation or stack an unverified second renderer | Poor fit. Recreates private ABI, palette/render/input/timer and lifecycle obligations; conflicts with exact companion checks and can add another scaling/pacing owner |

For the recommended path, use five small responsibilities:

1. **Runtime adapter:** admit the exact executable/Revival/wrapper family, resolve live objects, validate current hook bytes/ownership, and retire safely on unsupported state.
2. **Output geometry:** compute native-to-output and output-to-client rectangles once; share them with menus and overlays.
3. **Final scaler:** use the composed native source, perform only the requested GPU scale/filter, restore state, and preserve native Present/recovery behavior.
4. **Device lifecycle:** release/rebuild only owned resources through native reset boundaries; retain resources and hook ownership until in-flight calls finish.
5. **Configuration and interaction:** separate lightweight settings UI/hotkeys from the scaler's always-applied output policy; coordinate online handoff and defer reset-producing changes when necessary.

Unknown builds or incompatible existing hooks should leave the original display path running with a clear diagnostic. Do not select an old getEfzRender layout after that export is absent, patch only by version text, or weaken InGameNetplay's fingerprint check to make a replacement load.

The first admission profile should match the recovered renderer-split J package. Add original J and the user's older h configuration as separately verified profiles. Existing InGameNetplay support for e/f/f-framestepping/g/h/i/j does not make a new display module automatically compatible with all of them. [I02]

## 11. Effort estimate

These are planning estimates for one experienced developer familiar with the existing EFZ work, with the exact binaries and access to two running peers. They are not measured implementation times, commitments or claims that these stages have been completed. Runtime regressions, driver-specific behavior and recovering additional wrapper builds can expand the ranges.

| Stage | Work and acceptance goal | Estimated focused effort |
|---|---|---|
| A. Narrow prototype | One exact split-J build; observe/adapt final scale; native-size and integer output; no new gameplay/window hooks; clean fallback | 2-4 working days |
| B. Usable display core | Aspect/window geometry, Point/Linear/Sharp, basic persistent configuration, native reset/resource lifecycle, standalone operation | 5-10 additional days |
| C. Qualified Revival/InGameNetplay release | Cooperative window teardown, UI/cursor mapping, mixed modded/unmodded peers, spectator/replay/round/menu cycles, repeated reset/focus testing | 5-10 additional days |
| D. Wider display/platform coverage | Exclusive fullscreen and refresh/VSync experiments, xBR fallback/performance, more Revival profiles, selected Wine/legacy systems | 1-3 additional weeks, depending on selected targets |
| E. Sprite/background filtering | EFZ-specific render/camera/palette layer research and new compatibility validation | Separate multi-week effort; too uncertain for a tight quote |

For a useful first version with compatibility as the priority, budget roughly **three to five focused weeks** through stages A-C, rather than a same-day recompile. A visually convincing prototype can arrive much earlier; it is not the same milestone as reliable online/session/device behavior.

The scope can be smaller if the immediate goal is fixed startup geometry and basic final filtering. It grows if all Soku features, legacy OS compatibility, all Revival builds and live exclusive-mode transitions are required together.

## 12. Verification needed before calling it compatible

Static analysis identifies the correct surfaces and conflicts. Only execution can establish that a future hook maintains all relevant contracts. The proposed validation should compare like-for-like builds and input domains, rather than treating a smooth screenshot as sufficient.

| Area | Required comparison or exercise |
|---|---|
| Package admission | Exact recovered split-J triple; original J and h only after distinct profile checks; unsupported build remains functional without enhancements |
| Mixed peers | Same compatible native Revival baseline, display mod on only one peer; test it once as host and once as joiner, then on both |
| Determinism | Same inputs/replays at the same confirmed frame/checkpoint; compare appropriate game-state witnesses and FP environment while excluding presentation-only fields |
| Native image | Compare pre-filter 640x480 pixels with baseline; preserve palette fades, text, boxes, menus, superflashes and screen transitions |
| Rollback cadence | Distinguish outer update, speculative update, final render and Present; no additional simulation/input work or render of suppressed steps |
| Session lifecycle | Launch through efz.exe and admitted launcher-first paths; select, battle, round/result, disconnect, return, rematch/re-host; repeat cycles |
| Spectator/replay | Standard spectating and catch-up, replay pause/fast-forward and end-of-stream; do not infer missing renders are missing simulation frames |
| Existing mods | InGameNetplay/training present and absent; both hook-install orders; multiple menu opens; known palette/HUD conflicts recorded separately |
| Async hosting | Host idle while playing offline/practice, incoming peer acceptance, cancel and retry; display settings must not destroy session ownership |
| Window/input | Alt+Enter, Alt+Tab, minimize/restore, close/quit, drag resize, non-4:3 windows, multi-monitor and mixed-DPI geometry; no duplicate gameplay inputs |
| Reset failures | Same-size reset, real device loss, failed reset followed by recovery, resource-allocation failure; no retained extra backbuffer references or reentrant reset |
| Performance | Compare Present and outer-loop time distributions, CPU/GPU cost, long-run pacing and resource growth; test weak hardware and negotiated low rollback delay |

For the first release, prefer applying mode/reset changes from menus or idle states. Keeping the chosen filter active during a match does not require allowing disruptive fullscreen/resolution transitions in the middle of rollback. Any later live-transition feature must pass the same mixed-peer/lifecycle matrix.

FPS alone is a weak metric. An extra GPU copy or VSync wait may increase latency or reduce rollback headroom without a dramatic average-FPS change. Record the active native timer rate and separate CPU simulation cost from presentation waiting.

## 13. Platform and release considerations

The source builds a 32-bit Windows DLL. Soku's CMake/scripts target Windows 7 API levels, and its direct SRW-lock/DPI API use prevents treating the unmodified DLL as XP-compatible. If XP is a selected EFZ requirement, replace or dynamically gate those APIs and qualify the toolchain/import table separately. Wine behavior must also be tested rather than inferred from successful native D3D9 compilation. [S04, W04]

Sharp and sprite shaders use Pixel Shader 2_0; xBR uses 2_b. Upstream has fallback behavior when shader creation/drawing fails. Keep capability detection and a predictable Point/Linear fallback in EFZ, and expose the effective filter in diagnostics so a selected-but-unavailable filter is not silently misreported. [S01, S04]

The upstream project and xBR attribution use MIT notices; Dear ImGui is vendored separately. Preserve the corresponding notices when reusing code/shaders. This observation concerns the retrieved files and does not imply ownership of EFZ/Revival binaries or game assets. [S04]

Upstream KNOWN-BUGS still records an unresolved external Alt+Enter/frozen-rendering report and a focus-loss Reset failure lead. It also documents many fixes and successful targeted tests. Neither the unresolved report nor those upstream tests establish how an EFZ adapter will behave. Reuse the lessons about reset/resource/hook ownership and run the EFZ-specific matrix. [S03]

## 14. What is established and what remains open

**Established in this investigation:** the relevant documentation and current source revisions were recovered; original and newer J packages were distinguished by bytes; the newer companion matches InGameNetplay's exact profile; renderer exports/imports were compared; both wrappers' native-source/final-scale/device-Present paths were disassembled; the original decompiler hole was identified; EFZ's outer dispatch/wait was checked; current loader, WndProc and overlay restrictions were traced in source.

**Design conclusion:** a versioned companion module can reuse much of the display/filter code while retaining Revival's wrapper and native simulation behavior. The complete 640x480 surface before final scaling is the best identified integration boundary. This conclusion is stronger than a generic claim that two games both use DirectX, but remains a feasibility conclusion.

**Still requiring execution:** safe installation/removal amid the actual mod set; precise callback/reset ordering under drivers; preservation of every overlay and input mapping; mixed-peer compatibility and session longevity; acceptable GPU/presentation cost; actual latency benefit; additional versions and OS/display modes. No live compatibility pass, game benchmark, port build or deployment is claimed.

The practical next milestone is a small, exact-build final-scale prototype that preserves the wrapper, timer and online/session hooks. Its acceptance is clean native output and lifecycle behavior with one modified peer, followed by gradual addition of filters and interaction.

## 15. Source index

### Upstream display manager

- **S01 - Rendering, device/reset/window/sprite hooks:** [DisplayManager.cpp at audited commit](https://github.com/Aneroze/SokuDisplayManager/blob/a084c66bddd11ddd6134d1e2a47be2853567abe9/src/DisplayManager.cpp). Key areas: game constants 67-118; presentation parameters 649-713; swapchain Present 1065-1149; sprite install 2346-2352; initialization 3148-3178.
- **S02 - Feature and configuration contract:** [README](https://github.com/Aneroze/SokuDisplayManager/blob/a084c66bddd11ddd6134d1e2a47be2853567abe9/README.md), [DisplayManager.ini](https://github.com/Aneroze/SokuDisplayManager/blob/a084c66bddd11ddd6134d1e2a47be2853567abe9/DisplayManager.ini).
- **S03 - Integration failures and remaining reports:** [KNOWN-BUGS.md](https://github.com/Aneroze/SokuDisplayManager/blob/a084c66bddd11ddd6134d1e2a47be2853567abe9/docs/KNOWN-BUGS.md), particularly 198-288 and 292-338.
- **S04 - Build, version and shader/license material:** [CMakeLists.txt](https://github.com/Aneroze/SokuDisplayManager/blob/a084c66bddd11ddd6134d1e2a47be2853567abe9/CMakeLists.txt), [src/version.h](https://github.com/Aneroze/SokuDisplayManager/blob/a084c66bddd11ddd6134d1e2a47be2853567abe9/src/version.h), [source directory](https://github.com/Aneroze/SokuDisplayManager/tree/a084c66bddd11ddd6134d1e2a47be2853567abe9/src), [license](https://github.com/Aneroze/SokuDisplayManager/blob/a084c66bddd11ddd6134d1e2a47be2853567abe9/LICENSE). Detailed anchors are in audits/soku_audit.md.

### EFZ source and integration

- **E01 - EFZ research repository and graphics reference:** [Repository at audited commit](https://github.com/Aquat1c/EFZ_modding/tree/9b1d81a28695035de99ed14a8de6614214785687), [graphics notes](https://github.com/Aquat1c/EFZ_modding/blob/9b1d81a28695035de99ed14a8de6614214785687/shared_documentation/palletes_framedata/EFZ_Runtime_Graphics_Notes.md), [shared documentation](https://github.com/Aquat1c/EFZ_modding/tree/9b1d81a28695035de99ed14a8de6614214785687/shared_documentation). This repository is private to authorized collaborators.
- **I01 - Current online/rendering policy:** [InGameNetplay README](https://github.com/Aquat1c/InGameNetplay/blob/af22f8104206dcc1f12d5da5f84789ba14bfed80/README.md), 91-110 and 302-320.
- **I02 - Exact Revival profiles:** [revival_addresses.h](https://github.com/Aquat1c/InGameNetplay/blob/af22f8104206dcc1f12d5da5f84789ba14bfed80/include/netplay/bridge/revival_addresses.h), 983-1086.
- **I03 - Companion wrapper admission:** [revival_launcher_probe.cpp](https://github.com/Aquat1c/InGameNetplay/blob/af22f8104206dcc1f12d5da5f84789ba14bfed80/src/netplay/bridge/revival_launcher_probe.cpp), 403-429.
- **I04 - Window-hook ownership:** [title_core.cpp](https://github.com/Aquat1c/InGameNetplay/blob/af22f8104206dcc1f12d5da5f84789ba14bfed80/src/netplay/hooks/title_core.cpp), 533-610; [debug_overlay.cpp](https://github.com/Aquat1c/InGameNetplay/blob/af22f8104206dcc1f12d5da5f84789ba14bfed80/src/netplay/hooks/debug_overlay.cpp), 765-780, 1227-1248 and 1356-1376.
- **I05 - Online/Spectator handoff:** [title_flow.cpp](https://github.com/Aquat1c/InGameNetplay/blob/af22f8104206dcc1f12d5da5f84789ba14bfed80/src/netplay/hooks/title_flow.cpp), 4774-4825.
- **I06 - Training render/geometry/FP behavior:** [overlay.cpp](https://github.com/Aquat1c/efz-training-mode/blob/b6b14902c16d1dcc6e87336776516c946e53c2d5/src/gui/overlay.cpp), 507-653, 803-814, 879-898, 2045-2049; [mouse mapping in imgui_impl.cpp](https://github.com/Aquat1c/efz-training-mode/blob/b6b14902c16d1dcc6e87336776516c946e53c2d5/src/gui/imgui_impl.cpp#L1343-L1423); [overlay API](https://github.com/Aquat1c/efz-training-mode/blob/b6b14902c16d1dcc6e87336776516c946e53c2d5/include/gui/overlay_api.h); [version header](https://github.com/Aquat1c/efz-training-mode/blob/b6b14902c16d1dcc6e87336776516c946e53c2d5/include/core/version.h).
- **I07 - Netplay FPU-preserving temporary device:** [battle_log_menu.cpp](https://github.com/Aquat1c/InGameNetplay/blob/af22f8104206dcc1f12d5da5f84789ba14bfed80/src/netplay/hooks/battle_log_menu.cpp), 2588-2624.
- **I08 - State freshness and startup:** [efz_netplay_state.h](https://github.com/Aquat1c/InGameNetplay/blob/af22f8104206dcc1f12d5da5f84789ba14bfed80/include/efz_netplay_state.h), 7-27; [netplay bootstrap](https://github.com/Aquat1c/InGameNetplay/blob/af22f8104206dcc1f12d5da5f84789ba14bfed80/src/dllmain.cpp); [training online detection](https://github.com/Aquat1c/efz-training-mode/blob/b6b14902c16d1dcc6e87336776516c946e53c2d5/src/utils/network.cpp), 1012-1056.
- **L01 - Actual EFZ Mod Manager:** [README](https://github.com/fishshapedfish/EfzModManager/blob/82448bcda1fafd2abb1f528e1bc5945143cf6d45/README.md), [module loading](https://github.com/fishshapedfish/EfzModManager/blob/82448bcda1fafd2abb1f528e1bc5945143cf6d45/src/mods.cpp), [bootstrap](https://github.com/fishshapedfish/EfzModManager/blob/82448bcda1fafd2abb1f528e1bc5945143cf6d45/src/dllmain.cpp).
- **L02 - Separate legacy GDI wrapper:** [README](https://github.com/Aquat1c/EfzDdrawWrapper/blob/1a9d731c518ab8b0fcfceabd7e1001156ce8048e/README.md), [export table](https://github.com/Aquat1c/EfzDdrawWrapper/blob/1a9d731c518ab8b0fcfceabd7e1001156ce8048e/ddraw.def), [implementation](https://github.com/Aquat1c/EfzDdrawWrapper/blob/1a9d731c518ab8b0fcfceabd7e1001156ce8048e/ddraw.c).

### Recovered documents and binary evidence in the packet

- **D01:** references/EFZ_Master_Reference.md. Existing project inventory, preserved unchanged.
- **D02:** references/Revival_Timing_Assessment.md. 14 September 2026 static assessment; companion prior timing evidence is under evidence/prior_timing/.
- **D03:** references/Revival_102j_Findings.md. 11 September 2026 interoperability findings; current-source differences are described in this report.
- **D04:** references/EFZ_Palette_Conflict_Report.md. 17 October 2026 final evidence update; specific collision/trampoline evidence.
- **B01:** audits/revival_renderer_audit.md and evidence/revival_renderer/. New PE fingerprints, import/export comparison, creation/native-target/final-blit/reset/timer excerpts. Raw game/DLL binaries are not needed to read this packet.
- **B02:** evidence/efz_outer_loop_current.disasm.txt. Fresh EFZ dispatch/event-wait disassembly; executable hash is in section 4 and the machine-readable source index.

### Primary platform references

- **W01:** [Microsoft D3DCREATE](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dcreate), particularly FPU_PRESERVE. Consulted 20 October 2026.
- **W02:** [Microsoft IDirect3DDevice9::Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-reset), resource/thread/failure rules. Consulted 20 October 2026.
- **W03:** [Microsoft IDirect3DSwapChain9::Present](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3dswapchain9-present), flags and nonblocking-return behavior. Consulted 20 October 2026.
- **W04:** [Microsoft AcquireSRWLockExclusive](https://learn.microsoft.com/en-us/windows/win32/api/synchapi/nf-synchapi-acquiresrwlockexclusive), minimum client version. Consulted 20 October 2026.
- **W05:** [Microsoft IDirect3DDevice9::SetRenderTarget](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-setrendertarget), viewport changes when selecting a target. Consulted 20 October 2026.
- **W06:** [Microsoft IDirect3DDevice9::StretchRect](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-stretchrect), supported source/destination surfaces and driver capabilities. Consulted 20 October 2026.

## 16. Agent implementation handoff

### Start here

**Next task:** Implement the narrow Stage A prototype described in section 11: an independently loadable, 32-bit EFZ display module for the exact renderer-split Revival 1.02j package. Establish a transparent final-scale adapter, then add basic aspect/integer scaling with Point/Linear output. Keep Revival's bundled wrapper and native simulation behavior intact. Sharp, xBR, interactive window controls, broader version support, and exclusive fullscreen follow the appropriate later gates.

The static investigation is complete. Use it as the baseline and verify the code and binaries relevant to the implementation. Repeating the whole feasibility investigation is unnecessary unless new evidence contradicts a finding. Preserve existing work and newer commits; the audited revisions are evidence anchors, not an instruction to reset an active checkout.

| Work item | Status at handoff |
|---|---|
| Recover and reconcile EFZ/Revival documentation | Complete |
| Inspect current SokuDisplayManager and EFZ integration sources | Complete at the revisions in section 2 |
| Identify both J packages, imports/exports, native target, final scale, Present and Reset | Complete by targeted PE/disassembly inspection |
| Verify EFZ outer event wait and recent Revival batch/render gate | Complete by static inspection |
| Implement or build an EFZ display-manager prototype | Not started |
| Run the prototype inside EFZ, compare native pixels, or qualify netplay | Not started |
| Establish performance or latency improvement | Not measured |

Keep these statuses distinct in subsequent reports. A successful build does not establish in-game rendering or network compatibility.

### 16.1 Recover the existing evidence and exact binaries

Use the following stable source identifiers in an environment with access to the user's files. These are recovery locators, not public download URLs. The technical report is readable on its own; the packet supplies the disassembly and original references needed for implementation. For an agent without connected-file access, provide the evidence ZIP and the indicated original package through its normal file input.

| Material | Exact file name | Stable source identifier | What to use |
|---|---|---|---|
| Completed evidence packet | EFZ_DisplayManager_Evidence_2026-10-20.zip | `libfile_add34dd2fa8481918abfc08ac880141a` | Three audits, source index, fingerprints, disassembly, prior references |
| Renderer-split J package | Archive (1).zip | `libfile_cebe127c33e481919d7e8ecdf4e20d3f` | `Ddraw.dll`, `EfzRevival.dll`, `EfzRevival.exe`; initial implementation target |
| Original game executable | efz.exe | `libfile_cc893f59c9b4819183f6de949c0eb462` | The inspected EFZ binary; its SHA-256 is in section 4 |
| Original J corpus | revival_dll.zip | `libfile_e39211e1ae6481918ae5f90954683a13` | `EfzRevival_1.02j_refactor/Raw/dll/Binaries/`; later original-J profile work |
| Original PDF assessment | EFZ_DisplayManager_Feasibility_2026-10-20.pdf | `libfile_658056056a3c81918cbeaee0b0095fcd` | Presentation copy of sections 1-15; no need to re-extract it when this Markdown is available |

After extracting the evidence ZIP, read `audits/revival_renderer_audit.md`, `audits/efz_integration_audit.md`, `audits/soku_audit.md`, and `source_index.json`. Paths such as `evidence/revival_renderer/` in this document are relative to that extracted packet's root. Its checksum manifest covers the original packet snapshot; keep subsequent implementation records separately identified.

Full SHA-256 values for the initial EFZ/Revival/wrapper files are in section 4 and `source_index.json`. Compare the actual files before using any RVA. Repository URLs and exact source revisions are in sections 2 and 15. Reuse those sources and review relevant newer changes without overwriting ongoing work.

### 16.2 Contracts the prototype must preserve

| Contract | Required behavior | Evidence |
|---|---|---|
| Wrapper identity and ABI | Retain the bundled on-disk Ddraw.dll and its required imports/exports. Keep existing InGameNetplay package admission intact. | Sections 4 and 10; I02-I03, B01 |
| Native composition | Keep the wrapper's 640x480 target and its palette/text/box composition. Apply output changes after that composition. | Section 5; B01 |
| Presentation | Use the existing device Present and preserve its native call/recovery flow. Soku's swapchain hook is not the EFZ integration point. | Section 5; S01, B01 |
| Simulation, input and pacing | Preserve timer ownership, input handling, rollback batching, checkpoints and render suppression. Keep adapter counters/resources outside rollback state. | Section 6; D02, B01-B02 |
| Floating-point environment | Preserve the relevant x87/MXCSR state; retain FPU-preserving device creation behavior. | Section 6; I06-I07, W01 |
| Reset and resource readiness | Gate recovery on the actual Reset result and the wrapper's later surface recreation. | Section 8; B01, W02 |
| Window ownership | Coordinate hook registration/removal before adding interactive window controls. Test asynchronous initialization and handoff. | Section 7; I04-I05, I08 |
| Optional overlays | Keep display output independent of training's offline 640x480 callback and of stale frontend state. | Section 8; I05-I06, I08 |

### 16.3 Stage A implementation sequence

- [ ] **Establish the baseline.** Recover the evidence and initial package, compare full hashes and PE identities, inspect the chosen workspace, and record the source revisions actually used. Resolve the display module's project location from the active workspace; the investigation did not create an implementation repository.
- [ ] **Add the companion-module bootstrap.** Use the existing mod-loader route where available, defer device-dependent work until the actual renderer exists, and support initialization when the device was created before the module became ready. Replace Soku-specific globals and initialization assumptions with the verified EFZ adapter.
- [ ] **Validate hook installation.** Check the loaded module profile, relevant instruction bytes, object identities, and any existing hook ownership before installing an adapter. Handle unsupported profiles or incompatible hooks by leaving native display behavior available. Use proper instruction relocation for any trampoline.
- [ ] **Observe and forward the final blit.** For renderer-split J, the shared final StretchRect is at Ddraw RVA `0x279C0`, within Flip at `0x27860`. The native-surface pointer global is `0xDF284`; the device-pointer global is `0xDF2C4`. Verify those objects and the actual source/destination at runtime. The earlier blit at `0x27928` is the conversion-to-native stage and is not the completed-frame boundary. [B01]
- [ ] **Cover both output geometries.** Windowed and borderless branches converge at the same final call; honor the full-output or calculated destination rectangle. Do not introduce separate unverified call sites based on the mode flag. Preserve the wrapper's device Present at `0x279FC` and existing error handling. [B01]
- [ ] **Add the smallest display feature.** After transparent forwarding is correct, introduce fixed startup configuration for aspect/integer output and Point/Linear filtering. Keep the native composition target unchanged. Record native, backbuffer and client dimensions separately, and retain a working fallback to the original blit.
- [ ] **Integrate Reset before retaining resources across frames.** Release owned default-pool objects and extra source/backbuffer references before the actual Reset at `0x36441`. Capture its HRESULT. The helper at `0x363D0` loses that result and continues into other work. Reacquire the native source only after the wrapper has successfully recreated it. Handle failed Reset, failed recreation, same-size Reset, and replacement devices as distinct states. [B01, W02]
- [ ] **Preserve thread and state ownership.** Perform device work through its existing owner, restore touched render state and floating-point state, and make teardown wait for in-flight adapter work before freeing owned resources or callbacks. Avoid introducing a second timer, frame limiter, input poll, or simulation callback.
- [ ] **Build and record the actual result.** Produce the Windows x86 DLL with a reproducible build command and dependency versions. Record the resulting binary hash and any build limitations. Package configuration and instructions with a clear way to disable the module and restore the native output path.
- [ ] **Run the relevant acceptance cases below.** Capture direct runtime evidence and mark each case passed, failed, or not run. If game execution is unavailable, finish the source/build work that is possible and leave runtime qualification explicitly pending.

### 16.4 Later feature gates

**Sharp/xBR:** The source is a standalone, lockable, non-multisampled X8R8G8B8 render-target surface. Add a supported sampleable texture/copy path and fallback before reusing a shader. Set the viewport after selecting the output target and restore affected state. Account for additional BeginScene/EndScene calls so other mods do not execute their overlays or auxiliary work unintentionally during the adapter's pass. [B01, S01, W05-W06]

**Window interaction:** Establish a cooperative WndProc contract before adding hotkeys, live window controls, or a settings menu. Forwarding messages alone cannot guarantee InGameNetplay can remove its hook during handoff. Loader order alone cannot guarantee hook order. Publish the actual game rectangle for existing mouse-coordinate consumers. [I04-I06, L01]

**Wider support:** Add original J or other Revival versions as separately verified profiles. Keep exclusive fullscreen, presentation-interval changes and per-sprite filtering outside the initial prototype. Soku's default configuration can still install sprite detours, so exclude that installer rather than merely setting visible filter strength to zero. [S01, B01]

### 16.5 Runtime acceptance and progress record

Use section 12 as the complete qualification matrix. The following checks are the minimum handoff record; they do not replace the wider matrix when expanding feature or platform scope.

- [ ] Native output through a forwarding-only adapter matches the baseline, including palettes, Revival text/boxes and screen transitions.
- [ ] Aspect/integer output is correct in the supported windowed and borderless configurations, with the native image still 640x480.
- [ ] Missing capabilities, unsupported profiles, allocation failure and incompatible hook ownership leave a usable native display path.
- [ ] Same-size Reset, failed Reset, subsequent recovery and wrapper surface recreation do not leave stale pointers or retained references.
- [ ] The timer, input, rollback and render-suppression paths remain owned by their original components; record pacing and relevant state witnesses at matching frames.
- [ ] With matching native Revival packages, a session works with the display module on only the host, only the joiner, and both peers. A peer without the display module still uses the compatible native Revival baseline.
- [ ] Repeated handoff, battle/results, return/re-host, disconnect, spectator and replay cases pass for the claimed scope.
- [ ] Existing training/netplay overlays, hook-install orders and client-coordinate mapping behave correctly for the supported configuration.

For every implementation handoff, append the implementation repository/branch and commit, build command/result, DLL hash, exact tested package, OS/graphics environment, enabled mods, passed/failed/not-run cases, and paths to logs or captures. Describe the concrete failure and next action for each blocked case. Keep new evidence linked from this document and preserve the original static findings as dated evidence.

**First completion target:** a reproducibly built, exact-build final-scale prototype with verified native output, basic scaling, fallback and reset behavior. Claim Revival/InGameNetplay compatibility only for package/mode combinations that pass the corresponding runtime cases. Full filter parity and latency improvements remain separate milestones.
