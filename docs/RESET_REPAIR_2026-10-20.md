# Reset ownership repair — implementation and acceptance ledger

**Status: code pushed for review; native EFZ acceptance pending.** This is
`0.9.0-reset-preview`, based on DisplayManager v0.8.2 at
`b84d1d6a428d068461f6a878279936906999b372`. It repairs concrete ownership and
recovery defects. It does **not** establish that the intermittent no-mod
`D3DERR_INVALIDCALL` has been reproduced or eliminated.

## Coordinated branches

| Repository | Repair branch | Review |
| --- | --- | --- |
| DisplayManager | `fix/reset-ownership-20261020` | [PR #1](https://github.com/Aquat1c/DisplayManager/pull/1) |
| efz-training-mode | `fix/displaymanager-reset-contract-20261020` | [PR #36](https://github.com/Aquat1c/efz-training-mode/pull/36) |
| InGameNetplay | `fix/displaymanager-reset-contract-20261020` | [PR #7](https://github.com/Aquat1c/InGameNetplay/pull/7) |

Training starts from published main `b6b14902c16d1dcc6e87336776516c946e53c2d5`;
netplay starts from `af22f8104206dcc1f12d5da5f84789ba14bfed80`. The reported
local training fallback-device change **`4e3af4b` is absent from that published
baseline**. Recover and reconcile it before replacing the deployed training DLL.
These branches have not been deployed into a game installation.

## What changed and why

### Resource owners participate explicitly

`src/compat.cpp` no longer hooks resource creation, counts foreign references or
releases another mod's COM objects. The versioned 16-byte x86 table in
`src/overlay_reset_api.h` exposes `beforeReset(device, epoch)` and
`afterReset(device, epoch, ready)`. Known loaded overlays must acknowledge their
own invalidation before a voluntary Reset can begin. An older known overlay
without the contract blocks that transition before wrapper resources or window
state are changed.

The coordinator holds module references across pending callbacks, detects
late-loaded known modules before each real Reset, retains failed participants,
and re-quiesces a participant that resumed before another loss. It never treats
a callback refusal as permission to destroy the owner's resources.

The companion patches invalidate and reconstruct their own ImGui DX9 objects
in their actual ImGui context. Their nonblocking gates cover rendering, input,
late initialization and netplay battle-log device work. Device, epoch and
observed owner-thread checks reject stale or unrelated callbacks. Context
selection restores the caller's previous context, including null. Menu state,
the Win32 backend, the WndProc chain and online/practice admission remain owned
by the existing mods. Backbuffer dimensions are no longer a Reset detector.

### One persistent transaction controls supported Reset paths

`src/reset_transaction.cpp` is the portable production state machine;
`src/renderer_reset.cpp` binds it to the admitted Revival renderer. The native
helper entry is replaced only after the existing wrapper identity gate and an
exact complete-function match: 165 bytes for the 2020 family or 305 bytes for J.
The complete helper signature is rechecked while other threads are suspended;
only the entry prefix is overwritten. The replacement preserves the helper ABI.

The adapter captures both required wrapper surface descriptions before release,
notifies the font and overlays, then releases only the wrapper references it
takes over and DisplayManager's shader resources. Requested and rollback
presentation parameters remain immutable; each actual Reset receives a fresh
mutable copy. A failed Reset does not trigger surface allocation, font
`OnResetDevice`, or overlay reopening. If both the requested Reset and immediate
rollback fail, the transaction remains pending for later frames.

After an actual successful Reset, failed required-surface allocation is retried
without repeatedly resetting the healthy device. Partial font reconstruction
is cleaned before retry or another actual Reset. Native Flip remains gated until required
wrapper surfaces and font state are ready. An overlay that cannot reconstruct
its own objects remains quiesced and retries while the wrapper can resume;
further voluntary transitions wait for participant completion.

The native-helper replacement and Flip entry route supported requests through
the transaction; a per-device Reset guard refuses uncoordinated Reset calls.
The guard installs on the observed render thread,
handles a replacement device reusing the same address with a different vtable,
and invalidates generation-based caches. Lifecycle coverage stays active with
`ResizeBackBuffer=0`. Recovery uses later frames with bounded retry frequency,
not a sleeping loop inside Reset.

### Supporting integration repairs

- Correct the immediate presentation interval and COPY swap-effect assumptions.
- Resolve the actual device adapter before choosing an exclusive display mode.
- Commit fullscreen mode flags after the transaction's outcome is known;
  restore saved window placement when a transition fails.
- Preserve flags, registers and floating-point state in the injected Flip shim;
  place its trampoline in dedicated executable memory.
- Pin the manager while installed hooks can target it. There is no hot-unload
  teardown from `DllMain`.
- Require the shader path's guard target, end-of-scene, target restoration and
  state restoration to succeed; otherwise use the native Linear fallback and
  restore letterbox bars.
- Log presentation parameters, device/font state, window/foreground identity,
  required surface descriptions, Reset attempts and HRESULT changes at a bounded
  rate. These diagnostics support the remaining native failure investigation.

## Verification completed here

The environment was Linux. A real LLVM-MinGW x86 compiler and Windows headers
were used; no Windows executable or native game was run.

| Check | Observed result | Scope |
| --- | --- | --- |
| Release host CMake/CTest with GCC 13.3 | **2/2 targets passed**: `portable_core`, `reset_transaction` | Production portable sources; not D3D runtime execution |
| Complete Win32 CMake build with LLVM-MinGW / Clang 23.1.3 | **Passed** | All 17 production translation units, CRT-free DLL, test executables and fixture DLLs compiled/linked |
| PE gate | **Passed** | PE32/I386 DLL, GUI subsystem, OS/subsystem 5.1, no delay imports, only kernel32/user32 import DLLs |
| Native renderer adapter suite | **Compiled/linked; execution pending** | 15 isolated cases linking the actual adapter, transaction and participant bridge |
| Companion reset gates | **46 checks passed in each mod** | Actual portable production gate |
| Companion context scopes | **4 checks passed in each mod** | The repositories' real portable ImGui implementation |
| Existing netplay Python contracts | **7 checks passed** | Existing source-level contracts |
| Changed companion production code | **All four translation units compiled as Win32 x86** | Full companion DLL linkage remains pending |
| Callback ABI | **Verified** | 16-byte x86 layout and exact undecorated query export directive in both provider objects |
| Whitespace patch check | **Passed** | `git diff --check` |

The PE gate does not prove that every imported function is present on Windows XP
or that the DLL runs on XP. Cross-compilation does not prove MSVC compatibility
or native D3D9 behavior. Local workflow command blocks passed where executable
on Linux; hosted CI did not run those steps.

The [build record](evidence/reset-preview-build-status.json) contains compiler,
command and artifact details. Its [51-input manifest](evidence/reset-preview-build-inputs.sha256)
matches the final source, test and build files. The local DLL is 57,856 bytes,
SHA-256 `810552c842bd11db944c1fe2acb0ccd8320ea2fda0556bdd02aab1da22894bc7`.
The [host CTest log](evidence/reset-preview-host-ctest.txt) and
[PE gate log](evidence/reset-preview-pe-gate.txt) are retained alongside it.

The 15 native adapter cases cover success, same-address device replacement,
failed rollback/retry, repeated failure, partial recreation, native-helper
entry, late cooperative and legacy modules, refusal after failure, healthy
preparation refusal, failed font cancellation, partial font retries/cleanup,
re-quiescing a resumed participant after new loss, and complete helper signature
admission. These use controlled fake COM objects and real Windows module loading.
They are deterministic regression fixtures, not a reproduction of the driver's
intermittent INVALIDCALL or the original game crash.

Native test DLLs live under `renderer-reset-tests/` and intentionally use the
known mod filenames. **Never copy these fixture DLLs into a game installation.**
The separate manual `ResetProbe.dll` is a legacy-owner negative fixture and is
not a distributable mod.

### Hosted CI status

The first pushed revisions failed before executing build/test steps:

| Repository | Run | Evidence |
| --- | --- | --- |
| DisplayManager | [39494038885](https://github.com/Aquat1c/DisplayManager/actions/runs/39494038885) | `startup_failure`; no jobs returned and no commit check runs |
| Training | [39494208248](https://github.com/Aquat1c/efz-training-mode/actions/runs/39494208248) | Failed job; no assigned runner and empty steps |
| Netplay | [39494275807](https://github.com/Aquat1c/InGameNetplay/actions/runs/39494275807) | Failed job; no assigned runner and empty steps |

The accessible outputs did not provide the server-side reason. Companion checks
reported one annotation each, but the connector could not retrieve that endpoint;
the attempted job-log retrieval returned no blob. The pushed workflow files were
read back, their YAML structure checked and referenced action files resolved.
Those checks do not establish why GitHub rejected or failed to start the runs.
No account, billing, policy or permission cause is inferred. The evidence is
preserved in [reset-preview-ci-status.json](evidence/reset-preview-ci-status.json).
Check subsequent runs on the repair branch before treating CI as green.

## Native acceptance required before release

First build the exact three reviewed revisions with the supported Windows x86
toolchains, reconcile training `4e3af4b`, and run the registered native tests:

```bat
cmake -S . -B build-win32 -A Win32 -DDM_BUILD_PROFILE_FIXTURE_TEST=ON
cmake --build build-win32 --config Release --parallel
ctest --test-dir build-win32 -C Release --output-on-failure
python tools/verify_pe.py build-win32/Release/DisplayManager.dll
```

CTest should register `sha256_file_abc`, `portable_core`, `reset_transaction`
and `renderer_reset`. The latter launches each of its 15 cases in a fresh
process. Build and run each companion's reset-gate/context targets as documented
in its `docs/displaymanager-reset-contract.md`, then link the complete mods.

Use a separate game copy, keep its original Revival `Ddraw.dll`, and record
exact DLL hashes. Test no mods, training only, netplay only and both mods against
the audited 2020, original-J and split-J wrappers before expanding to other
profile generations. A scripted or instrumented transition harness should
report the requested mode and actual completion; blind SendKeys timing is not
evidence that a transition occurred.

| Scenario | Required observation |
| --- | --- |
| Resize and equal-size Reset | Both successful and forced-failure transitions have one coherent epoch; owner objects are invalidated before the real call and reopen only when ready |
| Exclusive enter/exit and borderless transitions | Correct adapter/mode/interval; coherent window placement on requested success, rollback success and pending recovery |
| Focus loss, Alt+Tab and device loss | No drawing or prohibited reconstruction while lost; recovery occurs on the actual device creation thread |
| Font or required-surface allocation failure | Failed partial objects are cleaned appropriately; later retry succeeds without stale ownership or endless Reset on a healthy device |
| Partial overlay reconstruction | The failed overlay remains closed; healthy wrapper frames resume; a new loss re-quiesces already resumed participants |
| Legacy or late-loaded overlay | Voluntary work refuses before destructive changes, or remains safely pending if loss already occurred |
| Point, Linear, Sharp and xBR | Geometry, palette/text/boxes and overlay coordinates remain correct; failed shader setup has a valid fallback |
| Netplay/training UI and input | Online/practice admission, simulation, input, pacing and rollback behavior remain unchanged |
| Windows XP target | Actual startup, imports, rendering and transitions pass on the supported target hardware/runtime |

For the intermittent INVALIDCALL, capture a D3D9 debug-runtime trace when the
target environment supports it, together with the manager's parameters, thread,
font, surface and focus diagnostics. Do not promote the preview merely because
the mock adapter tests or a few successful exclusive cycles pass.

## Remaining limits and corrected historical interpretation

1. **The original INVALIDCALL cause is still unproven.** These patches repair
   known contract violations and make failed recovery persistent; they do not
   identify a driver/debug-layer explanation unavailable in this environment.
2. **The reported game crash follows unchecked DirectDraw Lock, not GetPalette.**
   At game `0x40BCF0` the call uses vtable slot `+0x64`; its HRESULT is saved but
   not checked before the pixel access at `0x40BCF9`. The J wrapper's per-surface
   backing at `self+0x0C` is distinct from the two renderer surfaces managed here.
   Flip gating alone therefore does not prove this crash fixed. If it recurs,
   record Lock HRESULT, returned `pBits`, backing pool and lifetime. This patch
   does not introduce a dummy buffer or replace unrelated game-facing backing.
3. **Observed thread identity is not proof of device creation-thread identity.**
   Verify the latter in the native trace; there is no speculative thread
   marshalling in this patch.
4. **Initial helper takeover has a runtime qualification boundary.** Suspended
   instruction-pointer checks and full-body matching avoid overwriting an active
   instruction or a changed helper. They do not prove an earlier helper call
   suspended inside an external COM method has already returned. Qualify loader
   timing and initial activation before release.
5. **Coverage is bounded.** Three wrapper binaries were available for direct
   binary audit. Other admitted profiles still require the complete canonical
   helper match at runtime. Unknown third-party Reset callers and training's
   external registered renderers are not certified by the two ImGui providers.
6. **Device guard retention is bounded.** The process-lifetime table has 16
   entries. Exhaustion or an unsupported device/vtable transition refuses
   takeover; this is not a general hot-reload/device-recycling framework.

Keep the existing historical research as evidence of what was previously
observed. Its claims of a GetPalette crash or complete live compatibility must
not be silently carried forward as verification of this preview.
