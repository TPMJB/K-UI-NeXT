# Experimental VMU implementation status

This branch develops an original virtual memory card for K-UI. The public
2.0.0 release remains the stable runtime. No retail game currently receives
virtual VMU responses from this prototype.

## Implemented

- A standard memory-card-only Maple device: discovery, media geometry,
  512-byte reads, four 128-byte write phases, durable block sync, reset,
  retries and protocol error handling. LCD, clock and standalone VMU CPU
  execution are outside the initial device.
- A bounded software Maple schedule adapter that removes virtual requests,
  preserves physical packets and advertises the chosen subunit in a successful
  parent-controller response. Its caller must own both DMA submission and
  completion; the adapter itself does not install a hardware hook.
- Plain 128 KiB images backed by a separate two-sector redo journal. Success
  follows ordered durability barriers; recovery handles an interrupted image
  update. Each block is atomic under the documented storage fault model.
- A freestanding SCI backend with copied extent whitelists, partition bounds,
  game-data exclusions, pinned card identity and serialized bus ownership.
  The existing original SD command reader/writer serves actual I/O. Writes
  require a separate experimental compile flag and runtime permission.
- A resident service that composes the protocol, journal and SCI backend.
  It reserves storage before a transaction, reports initial contention as a
  retry, and requires healthy release before the final save acknowledgement.
  Its raw SD path is host tested and SH4 compiled; it is not installed in games.
- A blank-only formatter and bounded validator for canonical 200-block cards.
  Existing images are never reformatted automatically.
- A standalone KOS program that exercises the shared software layers through
  real microSD storage. Its synthetic FatFs sector map is not a retail-game
  extent handoff. See [the console test](vmu-harness-test.md).
- An independently selected Toy Commander observation loader. It validates the
  complete executable and exact hook instructions, then records real Maple
  schedules in a guarded title-owned allocation. It injects no VMU commands or
  responses. See [the observer test](vmu-maple-observer-test.md).
- A read-only SH4 executable analyzer and an exact match for the owner's
  Toy Commander executable. Static hook candidates do not authorize generic
  patches to other executables.

## Evidence and limits

`make test-vmu-prototype` runs strict-warning ASan/UBSan tests for the protocol,
Maple adapter, journal, default read-only and experimental SD backend, actual
SD command framing and cross-layer durable acknowledgements. Tests exercise
all 256 card blocks and thousands of torn writes, ordering failures, CRC
errors, card changes and malformed schedules.

`make -f Makefile.vmu host-test` and
`python3 tests/test_vmu_harness_images.py` execute the actual console sequence
through real FatFs on disposable FAT32/exFAT images. Fresh processes verify
persistence; refused invalid pairs leave the storage image unchanged. fsck
checks the outer filesystem. The harness creates a real allocated, CRC-valid
two-block VMS save through the protocol and verifies reset and remount behavior.
`python3 tests/test_vmu_image_compat.py` feeds that same image through the
production VMU Manager, extraction, VMS import and verified restore.
The pinned SH4/KOS workflow compiles and packages
an independently selectable `KUI/tools.kui` and retains source and native build
records.

Host fault injection does not substitute for a physical power-cycle test.
Successful SD busy/status completion relies on the card honoring its device
protocol; arbitrary defective or dishonest flash behavior is not covered.
The journal protects a block, not an entire game's multi-block save.

### Console evidence, 17 October 2026

The standalone harness from source `87140926efd1` passed on SCI/exFAT:
allocated `KUIPROOF` bytes and package CRC16, two used/198 free blocks,
virtual-device reset, fresh reopen after remount/reconnect, and a synced clean
checkpoint. A photograph of the post-power-cycle **X** verification is still
needed; the successful checkpoint screen alone does not prove that step.

The same source's Toy observer stopped during an accepted GD DMAREAD with
CD music off, before completing its four report pages. The terminal screen
reported function `2`, command `0x11`, rejection `0`, and caller PR
`0x8c08d55a`. This is an image-I/O failure, not a rejected request; its zero
parameter/stack-offset fields are expected for that terminal path. The screen
does not distinguish storage acquisition, physical reads, raw-sector parsing,
or stream-stop failure, and does not prove the Maple hook had run.

Review also found that the experimental low P2 bootstrap control shared a
cache line with mutable P1 pacing state. The observer now reserves complete
cache lines for that control, with linked-ELF checks to reject shared layouts.
This fixes a definite coherency hazard; its connection to the photographed
I/O failure remains unproven. The next observer build adds per-layer failure
details. Hardware validation is pending, and the released 2.0.0 loader is
unchanged.

## Work required before in-game saves

1. Prove exact game-side Maple ownership before submission and before the
   original completion consumer sees responses. Controller discovery, mixed
   physical/virtual schedules, no-hardware schedules and IRQ bookkeeping all
   need correct behavior. A write watchpoint that fires after MDST starts is
   too late to provide this contract.
2. Reserve memory that the game cannot overwrite. Toy Commander's admitted
   allocator leases 192 KiB, but existing live-use evidence protects only the
   first 64 KiB; direct SDK scratch may use the remainder. The new service
   streams blocks and does not assume a safe 128 KiB RAM mirror.
3. Prepare a per-game image and journal while the shell owns the filesystem,
   establish immutable physical extents and exclusive allocation, pin the SD
   card identity, sync, unmount, and hand off only resident-owned state.
   Imported images require validation; related discs need deliberate shared
   card identities.
4. Reserve storage transaction ownership before a commit. Contention after a
   journal transaction has started cannot be reported as a harmless retry.
   Any uncertain I/O must stop writes until controlled recovery.
5. Validate discovery, formatting, saving, loading and restart persistence in
   the admitted game on the owner's console. Start with synchronous native
   SCI loading; async/background CDDA and WinCE require separate ownership and
   scheduling work.

The chosen virtual address shadows a complete Maple subunit. Keeping a
physical VMU's LCD at that same address would require an additional function
router. Other physical subunits must continue through hardware unchanged.

No DreamShell loader implementation was consulted or incorporated. Protocol
and SH4 behavior were checked against primary documentation and the project's
pinned KallistiOS sources; the new project modules are independently authored.
See [protocol](vmu-protocol.md), [journal](vmu-store.md),
[Maple ownership](vmu-maple-interception.md), and
[runtime design](vmu-runtime-design.md) for the precise contracts. The
[service contract](vmu-service.md) describes the composed resident API.
