# Smash Pack worker retirement

The initial normal-menu integration replaced the collection reader with a fresh
native child's plain reader. A later original return to the menu did not restore
the collection adapter. The reported second-game failure is consistent with
that code path; it does not establish that the shared card was unbound or lost.

The menu worker now has a separate 16-byte `KSRT` export. Its gateway preserves
the caller's stack, R8 and PR. A first, read-only preflight runs on the caller's
stack and refuses busy or invalid workers before touching the existing guarded
4 KiB worker stack. Only an idle preflight permits that stack switch and the
complete ownership proof in `kui_smash_native_return_retire()`. The gateway
changes no SR bank/mode and allocates no additional stack. The ordinary 80-byte
worker exports and 72 KiB lease are retained.

| Result | Meaning | Required caller behavior |
| --- | --- | --- |
| `1` | Quiescent worker retired once | Cold-restart immediately; never resume the old SDK |
| `0` | A capture, DMA or accepted VMU write is still pending | Keep the live worker and its state unchanged |
| `-1` | Invalid mode, ownership or card state | Refuse the transition |

Retirement requires IMASK15 and BL, valid lease/hooks/consumer/stack ownership,
an admitted healthy card, balanced completed transfers and no pending overlay,
controller capture, card handler, SD transaction or staged VMU write. MDST is
sampled before and after the ownership proof. Success removes worker entry
authority and overlay publication authority. It performs no SD operation,
backend reset, checkpoint or free.

The source-bound early reset hook runs before the original cleanup functions.
If a transfer is busy, the return gate parks its saved integer frame above the
old owner stack pointer and restores the owner's GBR, VBR and interrupt-eligible
SR while waiting for an IRQ completion. It then masks interrupts and retries.
The wait is bounded by 4,096 epochs of 1,024 NOPs, not a wall-clock promise. A
masked owner context or exhausted retry bound stops with a diagnostic. No busy
or refused return can resume the old CRT cleanup caller.

An acknowledged VMU SYNC has already completed journal/base synchronization,
readback and successful shared-reader loan release. An unfinished 128-byte write
phase remains pending until the original game's SYNC completes; retirement
cannot invent that command or publish a save acknowledgement. The next worker
must receive a fresh stage recovery/checkpoint and current data CRCs from the
same existing card. A previous worker's live backend is never copied or reused.

The automatic 576-completion startup diagnostic is omitted only from the normal
menu worker to retain the strict memory budget. Its failure page remains, and
the original direct RPG developer routes retain the automatic probe. Two size
flags apply only to this menu worker's C object; its code, BSS and guarded stack
still must satisfy the linked 72 KiB reservation.

The menu-only hook witness limit is 24: the reviewed MDE needs 23 witnesses for
SDK heads, pointer cells, exit sites and the coherence hook. Any larger plan is
refused. Other worker profiles retain their original limit.

```sh
python3 tests/test_smash_native_return.py
python3 tests/test_smash_menu_successive_card.py
python3 tests/test_smash_menu_successive_admission.py
python3 tests/test_smash_native_return_entry.py --elf build/smash-native-menu/native-worker.elf
```

The first test executes the production worker over authored RAM/MMIO, including
busy and ownership refusals, a pending write followed by durable SYNC, repeated
retirement and denial of an old worker's later submission. The second uses
three distinct durable workers over one synthetic SD card: Shining Force,
Phantasy Star II, then Shining Force again, with fresh recovery/checkpoints
between them. Representative save and settings blocks survive. The admission
test holds the same 4,096-byte encoded profile unchanged across three fresh
stages, while current data hashes change after saves; a changed journal
allocation is still refused before any write. The gateway model executes the
actual linked instructions with ABI-adversarial C callees and proves that busy
preflight does not change any word of the worker stack.

These checks establish worker/backend and gateway contracts. They do not run
the original menu, game save UI, return hook or real hardware. Successive
selection and reload through the restored collection adapter require the
complete return-route model and console test.
