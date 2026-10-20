# Reset ownership repair — implementation ledger

Base: `b84d1d6a428d068461f6a878279936906999b372` (v0.8.2).
Scope authorized: implement, verify and push a repair branch with coordinated
training/netplay changes. This is a preview until native Windows acceptance.

## Contract and implementation

1. Delete foreign-resource tracking and forced Release. Each loaded known overlay
   must expose the versioned `EFZ_QueryOverlayResetApiV1` callbacks. Refuse voluntary
   resets before mutating window state when an old overlay lacks the contract.
2. Full-function-match Revival's native reset helper, then replace its entry with
   a calling-convention-correct adapter. All known wrapper Reset paths enter a
   single transaction; the native unconditional failed-Reset continuation is gone.
3. Capture surface descriptions before release. Preserve immutable requested and
   rollback parameters. Each actual Reset gets a fresh mutable copy. Do not call
   font OnResetDevice, recreate required surfaces or reopen overlays after failure.
4. Retry on subsequent Flip calls with a closed renderer gate. No sleeping loop,
   dummy pixel buffer, foreign COM destruction or unrelated DirectDraw teardown.
5. Verify the production transaction on the host, compile/link an actual Win32
   x86 DLL, and independently review integration before pushing draft PRs.

## Native acceptance still required

The reported game crash is in an unchecked DirectDraw **Lock** result, not
GetPalette. It uses per-game-surface backing distinct from renderer staging and
native surfaces. This repair must not claim that crash independently reproduced
and fixed in this cloud environment. Record Lock HRESULT/pBits and resource pool
if it recurs. Run no-mod, training, netplay, both-mod, equal-size Reset, resize,
exclusive enter/exit, focus loss and shader-enabled cases on the target machine.

Training public main does not contain local cooperative commit `4e3af4b`; the
companion branch must be reconciled with that local change before deployment.
