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

Later photographs bearing the same `87140926efd1` build reached the observer's
planned stop: 1,200 pre-submit schedules and 4,800 descriptors, with a maximum
of four descriptors per schedule. Each port A–D had 1,200 main-unit requests;
no subunit requests were recorded. The shown malformed/unsupported, missing-end,
receive-range and stack-guard error counters were zero. Captures cover pages
0, 2 and 3; page 2 was photographed twice and page 1 is missing, so the complete
command histogram has not been captured.

These counts include the final schedule, which the observer deliberately does
not start. They demonstrate a live pre-submit hook reaching its planned stop,
not DMA-completion interception or virtual VMU discovery, saving or loading.
The user also reported further progress with CD music On, but the exact loader
route for that attempt was not established. It is not evidence for the
observer's CDDA compatibility.

Review also found that the experimental low P2 bootstrap control shared a
cache line with mutable P1 pacing state. The observer now reserves complete
cache lines for that control, with linked-ELF checks to reject shared layouts.
This fixes a definite coherency hazard; its connection to the photographed
I/O failure remains unproven. Build `e17a57de4252` contains this isolation and
compact per-layer failure details, but the photographs above still show the
older `87140926efd1` build. Console validation of the newer build is pending;
the released 2.0.0 loader is unchanged.

Subsequent photographs explicitly bearing `e17a57de4252` show all four report
pages reaching the planned 1,200-schedule stop, with 4,800 descriptors and no
shown parse, unsupported-pattern, missing-end, receive-range or stack-guard
errors. Commands were 3,601 DEVINFO and 1,199 GETCOND requests; all addressed
main units and none addressed a VMU subunit. Trigger/enable/busy samples were
0/1/0, with no sampled deviations counted; the SDK manager base was `A05F6C00`.
Entry SR `60000161` and mask bitmap `40` agree on interrupt mask 6. This
establishes the pre-submit observer running on the console, without attributing
the earlier image-I/O failure to the cache-line fix or proving completion or
virtual saving. The next capture waits longer and retains bounded subunit
metadata so a physical VMU's actual memory-card requests can be examined. The
next experimental observer also samples response headers before the exact
normal SDK consumer, with caller, DMA, SDK-ready/lock and private-memory guards.
It changes no responses and does not mount a virtual card.

All four photographs of build `83bf9649d565` show its first successful passive
completion match: one submitted schedule, four RX headers sampled, no orphan,
overwrite, busy, mask or context rejection, and caller PR `8C0B5362`. The
retained main-A DEVINFO header `1C210005` advertises a subunit. The observer then
stopped before submitting schedule 2: five packet requests had been counted
(four main DEVINFO and one main GETCOND), with one unsupported control and one
missing-end count, but no malformed or RX-range error. No subunit packet had
yet been recorded. This demonstrates the normal completion hook and controller
subunit advertisement, not a physical-card read or virtual save.

The stopped build did not retain the rejected cursor/control, so that value
cannot be recovered from these photographs. Independent inspection of the exact
owner shows its SDK inserting one-word `00000700` padding between same-port
packets; the old observer rejected that legitimate form. The next passive
revision admits only that exact padding value, counts it separately, preserves
the packet/source/RX bounds, and displays failing cursor/control details on an
unsafe stop. Its padding-aware parser still requires console validation.

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
