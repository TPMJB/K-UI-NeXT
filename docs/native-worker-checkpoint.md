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
from the worker, or resumes the game. Hardware execution is still pending.

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

The next runtime step is a relocated passive submission/completion window,
using Power Stone 2's variable initializer layout rather than Silver's uniform
one. Admit exact RX pointer arrays, gun/timer cells, original R14 manager-cell
identity and R12 == 1, then check lease/header and DMA/IRQ state at both
boundaries. Keep reinitialization and teardown intercepted before memory can be
reclaimed. Copy every retained configuration object into the lease; no pointer
to the temporary stage may survive the final installer. Only then add synthetic
read-only VMU replies, followed by persistence using the existing durable core.
