# Native VMU continuation checkpoint — 22 October 2026

The owner confirmed the Power Stone 2 automatic allocation test from build
`9f684b46fd36`: phase 7, 192 KiB, payload `0c38a880`, entry heap `0c33a880`,
heap bytes `00080000`, return `0c02d312`. This does not prove sustained worker
lifetime, generic save/load, CDDA coexistence or SCIF storage backing.

Workspace maintenance then removed the checkout. Recovery used the full source
snapshot in `K-UI-Armada-CE-VMU-DMA-Park-Test.zip`, source checkpoint
`26a17083f516`, and the saved GCC 15.2.0 toolchain. Its 1,076 manifest entries
were checked with no regular-file hash mismatches. Native discovery, allocation
and relocation components were reconstructed from complete retained source.
Building the recovered allocator probe with the historical build ID produced
exactly the same 86,048 bytes as the saved tested module:

`cebbe36cd291b5b2ad70b814a3f25e89125d4f08b7e4d826f02b76a856edf1f5`

The current git history is a synthetic recovery history; it does not recreate
the original commit identities. Do not merge this root commit over main. Port
the new files and focused changes onto the real feature branch when available.
The test ZIP includes a complete, cloneable git bundle preserving the current
source and its exact synthetic recovery commits. Clone that bundle to continue
without reconstructing source or changing these new commit identities.

## New execution proof

`Makefile.vmu-native-worker` adds an actual relocatable worker, all P2 VMAs at a
temporary link origin `ac100000`, with a 64 KiB area and private last-4-KiB
stack. The stage discovers and obtains the previously tested 192 KiB lease,
purges stale P1 lines, applies exact linker-derived pointer relocations through
P2, zeroes private BSS/stack, and checks the export layout before calling it.
No game address table is added. It retains the allocator startup argument,
return, stack, arena and memory-overlap guards.

The worker checks its actual stack/SR and lease, initializes the existing
read-only VMU backend in its final location, performs DEVINFO, ALLINFO, media
geometry and all 256 block reads through the relocated callbacks, and rejects
SYNC before protocol entry. The reference blank image CRC is `3d826179`.
The stage checks its return, unchanged initialized-image SHA, all stack guard
words and continuing allocator exclusion, then stops at phase `0000000a`.
It never installs submission/completion hooks, submits Maple DMA, accesses SD
from the worker, or resumes the game. Hardware execution passed on Power Stone
2 on 22 October 2026, build `e08ad48e2c8a`: phase `0000000a`, allocated payload
`0c38a880`, relocated entry `ac38a914`, private SP `ac39a860`, 259 requests,
256 reads and blank image CRC `3d826179`. The photographed final message was
`WORKER VERIFIED - PHOTO THEN POWER OFF`.

## Verification

- Recovered allocator module reproduces the tested binary exactly.
- Heap suite: 9,972 recovered synthetic checks. The later expanded call-site
  test suite was not retained; its production call adapter is recovered and
  included in the byte-identical allocator build.
- Runtime boundary suite: 784 ASan/UBSan checks.
- Relocator: 113,772 ASan/UBSan checks and seven ELF extractor cases.
- Actual worker table passed the C relocator at three destination addresses;
  every byte and pointer matched independent offset arithmetic. A tampered
  worker failed before any destination write.
- Protocol proof passed on host twice, including all 256 blocks and exact CRC.
- Stack gateway auditor executes 1,288 adversarial ABI/SR/argument scenarios
  against the actual stage-linked code; all passed.
- Worker ELF/P2 layout, exports, exact relocation table, immutable stage
  embeddings, integer-only instructions and absence of state-changing worker
  instructions pass the linked audit; five corrupted artifacts were refused.
- Existing allocator/bootstrap/package audit passes its 19 mutation regressions.
- Installer passes eight backup/restore/integrity/update tests; existing route
  tests pass four cases. New receipts permit the next probe only after the
  verified original loader has been restored.

## Build and resume

```sh
export PATH="$PWD/.deps/sh-elf/bin:$PATH"
make -f Makefile.vmu-native-worker BUILD=build/native-worker-clean -j4 all
make -f Makefile.vmu-native-worker BUILD=build/native-worker-clean worker-host-tests
python3 tools/check_vmu_native_worker_gateway.py build/native-worker-clean
python3 tools/package_vmu_native_worker.py build/native-worker-clean
```

Use a fresh BUILD directory after committing so embedded build IDs match the
source. The source snapshot excludes the compiler; restore the separately saved
`K-UI-VMU-Toolchain-15.2.0-fcfa7d8.tar.gz` according to its instructions. This
freestanding probe needs the compiler/newlib but not a complete KOS source tree.

The previously written runtime validator, static cache codec and frontend
include have also been recovered. The static cache frontend wiring, its full
FatFs integration tests and cache-expanded discovery tests are not recreated in
this recovery snapshot. The include remains dormant; broad VMU routing is not
enabled. Reintegrate and retest those pieces when continuing the generic loader.

## One-pair passive continuation

`Makefile.vmu-native-passive` builds the next hardware test. Its console result
is pending. It copies the driver, allocator lease, immutable initializer layout,
display registers and runtime configuration into the relocated worker. All
retained addresses are game/lease/low-resident addresses. The high-stage
installer and stack pointers are cleared before publishing installed state.

The new layout adapter decodes the reviewed variable initializer and its actual
argument/index provenance, plus gun and TMU1/TMU2 callback cells. For the PS2
sample this yields TX `ac20ea60`, input pointer array `ac315180`, twelve inputs
and twenty-four nonzero slots across two banks. Buffer pointers originate in
BSS: the worker binds them at first submission, reproduces alignment/P2/bank
assignment, checks all 48 manager entries and rereads the array for stability.
Completion rebinds and compares that layout again. There is no production
title hash or per-game address table. This adapter currently admits only the
reviewed twelve-input variable initializer family.

The submission gateway captures original R14/R12, checks manager-cell identity
and R12==1, preserves integer/SR/PR/MAC/GBR state, and replays the six displaced
instructions including exactly one original MDST store. Before allowing it,
the worker checks the stack guards, lease/header, idle software-trigger DMA,
IRQ context and full bounded TX chain, with exact RX-slot membership and
lease exclusion. Completion checks exact original PR, phase-1 SDK state,
continuing ownership and every unchanged TX word before a terminal report.
Success is phase `0000000b` / `PASSIVE PAIR VERIFIED`. No virtual replies or
storage writes are enabled; the original consumer is not called.

The isolated resident route now uses ordinary GD destination mapping plus
dynamic exclusion of the complete 192 KiB lease and its 32-byte allocator
header. The heap initializer remains intercepted: repeat initialization stops
before reclamation. Dynamic terminal entry is bounded to the first 64 KiB.
The immutable patch-window scan rejects direct branches or aligned RAM
literals into the displaced instructions' interior. It cannot prove arbitrary
indirect flow. Power Stone 2 passes that conservative scan.

Validation: 602 ASan/UBSan passive protocol/boundary checks, 250 layout checks
including 21 actual-startup mutations and duplicate timer ambiguity, 206 patch
window checks, and 80,175 resident destination/terminal checks. The actual
linked passive gateways pass 5,760 register-state scenarios at three relocated
addresses plus four corrupted-gateway cases; the stage gateway retains its
1,288 scenarios. Exact ELF/flat image/export/relocation/stage/module binding
audits pass, including bootstrap and artifact mutations. Relocation extraction
now correctly scopes duplicate local C symbols by index while refusing global
name conflicts; its nine extractor regressions pass.

Build, test and audit (supply the unpacked PS2 startup only for real-image
mutation tests; copyrighted game binaries are not included in source):

```sh
export PATH="$PWD/.deps/sh-elf/bin:$PATH"
make -f Makefile.vmu-native-passive BUILD=build/native-passive-release -j4 all
make -f Makefile.vmu-native-passive BUILD=build/native-passive-release passive-host-tests STARTUP=/path/to/1ST_READ.BIN
python3 tools/check_vmu_native_passive.py build/native-passive-release --self-test
python3 tools/package_vmu_native_passive.py build/native-passive-release
```

After a successful console pair, add synthetic read-only VMU replies, followed
by persistence using the existing durable core. Before allowing sustained
gameplay, prove generic teardown/exit provenance and all lifetime paths, then
restore frontend/cache integration and test more initializer families. CDDA
coexistence and SCIF VMU backing remain outstanding. Keep the previously proven
Toy, Silver and Armada paths unchanged; do not remove their admission gates.
