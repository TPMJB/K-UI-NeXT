# Virtual card in A1

Some games only ever save to A1. Sword of the Berserk's save code passes drive
0 (A1) to the backup library as an immediate, so it never polls A2, where the
combined module's virtual card lives. `Makefile.vmu-native-background-compact-a1`
builds the compact Background module with the virtual card answering as A1
instead. A2 then stays physical, exactly as A1 does in the default build.

## What changes

One compile-time choice, `KUI_VMU_VIRTUAL_A1`, sets
`KUI_VMU_NATIVE_VIRTUAL_ADDRESS` (2, or 1) and `KUI_VMU_NATIVE_PHYSICAL_ADDRESS`
(the other slot) in `vmu_native_passive.h`. The resident uses them wherever it
used the A2 address before:

- the durable card, overlay and read-only card are created at that address;
- the chain parser captures WRITE payloads, and the completion skips physical
  replies, for that address;
- the physical-reply checks for a real VMU apply to the other slot;
- the controller-only fast path refuses a physical device in the virtual slot
  and sets that slot's presence bit;
- the issued hardware chain filters that address.

`vmu_volatile_chain.c` keeps the real VMU in the other slot in the same way. Its
pass-through rules and output bounds follow the build's virtual slot, and the
no-pass-through mode (the historical A1 experiment) uses the other address. An
A1 build is therefore the default build with controller A's two expansion
slots exchanged.

## Verification

- The default build is byte-identical to the audited compatibility test 2
  build (d3f9322e): stage, worker, both residents, read-ahead and module.
- In the A1 build only the stage strings and seven functions differ:
  `chain`, `kui_vmu_native_passive_arm`, `_submit` and `_complete`, and the chain
  builder's classify, geometry and verify. The other 82 changed worker bodies
  differ only by relocated addresses.
- `make -f Makefile.vmu-native-background-compact-a1 virtual-a1-host-tests` runs
  the split-controller/stream-guard test (1,607,463 checks), the persistent
  bridge test (2,085,252 checks) and the chain test with the slots exchanged.
  The default builds of the same tests still pass.

## Limits

- A1 must be physically empty, just as A2 must be for the default build. A real
  device in the virtual slot stops the game on the resident's failure screen,
  before any storage access.
- A2 can hold a real VMU; its memory, LCD and clock traffic pass through.
- Vibration commands are not passed through yet, in any slot or port, in the
  default build either. With a jump pack plugged in, the game's first
  vibration is refused and stops the game on the resident's failure screen.
  Polling a jump pack is fine.
- The slot is chosen at build time, so the A1 build ships beside the default
  one as `retail-native-dual-a1.kui`. Each game's Advanced page has a
  "Virtual card slot" row (A2 by default); a game set to A1 launches the A1
  module when Virtual VMU is On. The A2 module stays byte-identical.
