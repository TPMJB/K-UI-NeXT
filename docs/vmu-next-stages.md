# Virtual VMU work after the performance gate

Planning assessment: 2026-10-23. This document separates future work from the
current native performance build. It records source inspection and proposed
acceptance criteria; it does not claim that these features are implemented.

The optimized build `920c86efc512` has now passed the owner's SA2 gameplay
comparison: any difference from VMU-off was imperceptible. Skies performance
has not yet been retested. The earlier latency capture recorded no VMU storage
calls; the correction removed repeated resident CPU work. Current work has
moved to wider native compatibility while preserving that runtime.

The owner wants the SCI and SCIF work completed, and the resulting lessons
captured, before reopening the console for the G1 board. Some Kapton is already
lifting, and handling the console could disturb the existing SCI solder joints. No
stage below requires a hardware change to be started in software; later G1 work is
a separate hardware milestone.

## Evidence boundary

The current generic native adapter exposes one virtual **A1** card. Power
Stone 2, Sonic Adventure 2 and Skies of Arcadia Disc 1 have owner-reported
save/reboot/load success. SA2 performance is accepted; Skies still needs its
optimized-build performance retest.
Those results establish useful native SDK families, not every standard
Dreamcast game, every revision, or every peripheral combination.

The desired arrangement is a virtual **A2** card with physical A1 available
for the real VMU and its screen. A2 is a real integration task, not a menu
label change. The native filter, parent advertisement, receive-slot handling,
response source and physical conflict checks currently contain A1 assumptions.
The common protocol engine can represent another subunit; the native adapter
must preserve that address consistently.

Armada's WinCE save/load result uses a separate CE hook/lifetime adapter.
Earlier exact-title native modules are also separate evidence. CDDA, WinCE,
SCIF and physical-disc combinations must each retain their own acceptance
record instead of inheriting support from a different route.

## Recommended order

| Stage | Deliverable | Reuse | Acceptance gate |
| --- | --- | --- | --- |
| 0. Performance | Ordinary native gameplay with a persistent card and bounded startup | Current allocator, prepared runtime geometry, latency capture and persistent worker | SA2, Skies and Power Stone 2 compared with the same loader and VMU off; no material added input lag, smooth FMV, save/reboot/load still correct |
| 1. Wider native families | Add proven family variants and peripheral schedules | Automatic discovery, executable identity cache, family corpus, host SH allocator execution | Each newly admitted family passes static/live refusal tests, repeated boots, gameplay and durable save/reload; unsupported cases remain identifiable |
| 2. A2 and on-console cards | Import, create, select and bind a card in Games without PC personalization | VMU Manager, guarded physical capture, collection publisher, persistent preparation | Physical A1 continues working; selected virtual A2 saves and reloads; changing cards cannot silently overwrite another save set |
| 3. SCIF | One shared, durable SD service used by game reads and virtual saves | Existing SCIF image reader plus transport-neutral persistence | Read-only first, then save/power-off/recovery; streaming and saves coexist with measured worst-case input/frame latency |
| 4. Original GD-ROM | Retained optical boot with VMU-only SCI/SCIF backing | Native family adapters, protocol, journal, card management and the new shared SD service | Original optical services remain in use; passive hooks, imported-save load and durable new saves verified in that order |

Stages 1 and 2 can share source work once performance is accepted. Restoring
the complete UI build environment can happen independently. Transport and
optical experiments should wait for a stable card-management and storage
ownership interface so they do not create additional one-off adapters.

## Broaden support by family and by behavior

The automatic detector should continue resolving reviewed code families and
checking exact startup bytes and live state. A cache stores immutable
resolution results, not permission to reuse an old live heap or SD ownership
map. The current cache path is
`/KUI/VMU/native-cache/v1/<executable-SHA>.nvr`; its full startup hash and
launch-local checks should remain separate from save-card selection.

The recovered corpus already identifies useful next targets:

| Corpus case | Current gap | Useful next work |
| --- | --- | --- |
| Resident Evil Code: Veronica | Candidate RX/TX intervals overlap the startup image under the current proof | Determine the actual live ownership interval and lifetime; retain the overlap refusal until that proof is narrower and valid |
| Grandia II | Owner reports the generic build was just fine | A unique proved startup call now distinguishes its real initializer literal from an unused export-table copy; original allocator, layout and persistence models pass |
| Evolution 1 | VMU launch reaches MPEG Sofdec then black; same `2bf215f0fb72` build with `--control` plays the opening FMV | Collect failure-only allocation diagnostic; keep the lease and transfer acceptance unchanged until its cause is observed; silent GD-destination refusal remains another hypothesis |
| Crazy Taxi 2 | Allocator found; initializer pointer/root-call provenance incomplete | Extend dataflow and call-root evidence, then execute the discovered allocator contract in the host SH model |
| Phantasy Star Online | Layout root now resolves; allocator-call and fourth terminal-consumer proofs remain | Review the additional consumer and dispatch lifetime together |
| Jet Grind Radio | Allocator arguments now resolve; Maple root transport does not | Prove the actual root transport before using its otherwise matching initializer |
| Evolution 2 / Soul Reaver | Different callback, timer or terminal-entry layouts | Model the layout explicitly; close terminal entries cannot accept overlapping veneers |

There is a second compatibility axis after signature discovery. The present
native DMA adapter expects a real port-A parent request and at least one
physical descriptor in its admitted batch. All-virtual and parent-free
batches remain separate scheduler work. It preserves a bounded subset of
physical queries; physical storage reads, physical writes, rumble SETCOND and
other state-changing peripheral commands are not broad passthrough yet.
Virtual RESET is implemented in a limited form; KILL needs coordinated
silence and parent-advertisement lifetime handling.

Each new schedule needs a descriptor/response model and rejection tests as
well as a new executable fixture. Deleting a gate to make a title launch is
not an implementation of the missing contract. An unsupported game should
offer an ordinary launch without the selected virtual card before installing
hooks, with a concise reason visible in its compatibility details.

## Build the wizard from existing services

The source already contains substantially more than a blank-card creator.

| Existing component | Reusable behavior | Remaining integration |
| --- | --- | --- |
| `src/apps/vmu_collection.inc` | Reads a physical card without writing it; pins device/metadata; reads allocated payload twice; previews and packs a selected save or all saves into a canonical image | Present this as a normal Games/VMU workflow and keep companion saves together |
| `kui_vmu_collection_publish` | Creates a new verified card under `/KUI/vmu/cards/vNNNN`; writes the readiness marker last; never overwrites a previous or partial entry | A catalog card is a management snapshot, not an active resident backing pair |
| `src/experimental/vmu_import_prep/main.c` | Creates a separate persistent `pNNNN` pair, checks card identity/allocation ownership, exports and verifies the profile | Factor the service out of the experimental app; remove its dependency on an existing active p0001 anchor from the final user flow |
| `src/apps/vmu.c`, `src/apps/vmu_import.c` | Existing VMU Manager, backup/restore operations and independently authored VMS/VMI/DCI parsing with preview/confirm pinning | Reuse input formats and error reporting without asking the owner to run an installer or personalization script |
| `include/kui/shell.h`, `src/dreamcast/main.c` | Paginated collection state, worker actions, preview/commit generations and progress routing | Compose asynchronous wizard steps; avoid a blocking full scan in the render/input thread |
| Games launch and native cache integration | Final module routing, mounted-filesystem preparation and cache publication | Pass a dynamically selected, freshly admitted persistent profile into the generic native launch route |

The catalog and persistent pair serve different purposes. Publishing a card
snapshot does not arm runtime writes. Activation must create or reopen the
card's writable base/journal/identity pair, check the actual SD device and
current filesystem allocation, perform recovery when allowed, and pass a
validated immutable launch handoff. Old exported extents cannot stand in for
a fresh ownership scan.

The first finished flow should be:

1. Open a game's **Virtual VMU** action. Show its selected card, virtual slot
   and whether the current launch route supports it.
2. Choose **Existing card**, **New empty card**, or **Import from real VMU**.
   For import, choose the physical source and preview its saves. Offer the
   complete save set as the uncomplicated route; an individual save must not
   imply that companion files have been identified automatically.
3. Create a separate card and verify its content before making it selectable.
   Keep the physical source and every existing card untouched. Show required
   blocks and remaining standard-card space.
4. Select the card and virtual slot. Prefer A2 once its native adapter is
   implemented; detect a real card at the selected endpoint before launch.
5. Save the game-to-card assignment. Before each launch, revalidate the
   backing identity and allocation and then show the selected card in Games.

Use a **save-set card** model. A standard card remains 128 KiB with 200 user
blocks, while the library can contain many cards and independent playthroughs.
Duplicate names from different physical VMUs remain separate save sets.
Do not rename or merge PSO-like duplicate filenames simply to fit a global
directory. Preserve the newest on-device card data when updating software.

Game identity and code-cache identity must be different keys. Hook resolution
belongs to the exact executable hash and reviewed family epoch. Save binding
should deliberately survive original/2048/container variants of the same
game and supported multi-disc sets. Region/revision sharing needs known save
compatibility or an explicit user assignment; a similar title string alone
must not merge cards. Card selection should be stored by stable card identity,
with paths treated as locations rather than ownership proofs.

Cancellation needs defined boundaries: cancellable reads/preview and cleanup
of incomplete creation, followed by a bounded final publication step. Repeated
button presses or stale worker generations must not publish twice. An import
preview becomes invalid if the real VMU changes before confirmation.

### UI build dependency

This recovered tree can build freestanding SH modules, but its complete UI
toolchain is not present: the KOS headers/environment/Makefile rules and
FatFs source/header dependencies are missing. Restore the pinned compatible
dependencies and reproduce a baseline UI build before claiming a wizard
binary. The source components above make the integration concrete; they do
not eliminate that build prerequisite.

## SCIF: separate throughput from latency and ownership

The game-image storage abstraction already has SCI, SCIF and IDE backends.
That does not yet make persistent virtual VMUs transport-independent in the
resident. The current native VMU lease is SCI-only and borrows the initialized
card under the image reader's cached P1 lock. It refuses an active image
stream, returns BUSY only before doing I/O, runs on a private stack and
restores ownership before a virtual acknowledgement.

The common persistence engine already accepts device identity, read, write,
sync and health operations. Keep its durability contract: a successful block
commit means the data is durable after power loss and reopen. A RAM-only
acknowledgement would make the interface appear faster while losing saves.
The prepared profile also currently identifies SCI; transport expansion needs
a checked format/version contract rather than editing that constant alone.

There is no reason to infer impossibility solely from SCIF's lower bandwidth.
Card data is small and save writes are bursts; normal controller polling
should perform no SD I/O. Conversely, a low average write rate says little
about a game's allowable interruption during a save.

The existing host oracle for a final save SYNC reports at least one 512-byte
readback and four 512-byte writes: **2.5 KiB of sector payload**, plus status,
identity, command and turnaround costs. The table gives arithmetic payload
floors at illustrative effective rates, not measured SCIF performance:

| Effective sector payload rate | 2.5 KiB commit payload | One 128 KiB card pass |
| --- | ---: | ---: |
| 100 KiB/s | 25 ms | 1.28 s |
| 800 KiB/s | 3.125 ms | 0.16 s |

A multi-block save can require multiple commits. Startup admission can make
several passes plus allocation/identity checks. These figures exclude every
gap, CPU operation and timeout; they must not be advertised as a maximum
save time or a hardware rate. The currently captured gameplay lag occurred
with zero storage calls, so its cause is independent of this payload budget.

The SCIF service should have one explicit initialized device owner shared by
the image reader and VMU backend. Add scoped SCIF register/DMA/IRQ admission
and restoration, fixed device/transport binding, health and timeout handling,
and fair arbitration at legal stream boundaries. VMU storage must not steal
an active transfer. The current native worker enters with IMASK 15; long SD
transactions there can hurt latency even when their bandwidth is sufficient.
Review a deferred worker/state machine if measured commits exceed the
available window, including Maple completion timing and the game's retry
behavior. The CE deferred adapter is a useful architecture reference, not a
drop-in native execution context.

Validate SCIF in this order:

1. Device and bus-ownership tests: failures before acquisition, faults after
   acquisition, restoration on every path, no cross-device continuation.
2. Read-only virtual discovery and imported-save loading.
3. Durable writes and controlled interruption/recovery at each journal phase.
4. Gameplay, repeated save bursts and sustained game-image reads together;
   measure worst-case hook time, input latency and streaming stalls.
5. Separate CDDA/native and WinCE/CDDA combinations after their scheduling and
   audio ownership are represented. SCI support does not pre-admit them.

## Physical GD-ROM needs a retained launch path

The existing GD Play action sets `ARCH_EXIT_REBOOT` and calls `arch_exit()`.
The UI parks its workers and exits to the stock BIOS. That handoff contains
no retained VMU installer or SD-owner contract. A menu setting cannot keep the
current image-loader resident alive across it.

A physical-disc route should preserve original GD services and provide a
separate retained VMU-only bootstrap:

1. Identify the inserted disc's real TOC/session, IP and executable; resolve
   its code with the same exact-byte and family rules. An optical launch
   manifest must describe optical facts, not fictional SD game extents.
2. Establish controlled optical boot and an entry relay without installing
   image GD replacement or synthetic drive/disc responses. Verify gameplay
   without a virtual card first.
3. Prove ownership of bootstrap code, control data, private stack and the
   eventual native heap allocation. Image-loader low-memory reservations do
   not prove that optical firmware never uses the same scratch area.
4. Retain the same transport service used for SCI/SCIF card access, without
   depending on the absent image reader's lock. Fresh SD filesystem/card
   admission still applies even though game data comes from the disc.
5. Prove real GD DMA/interrupt coexistence with the worker and card buffers.
   Interrupt masking does not stop a GD DMA already in progress; real optical
   firmware bypasses the image reader's worker-destination exclusions.
6. Begin with one admitted executable and passive Maple/heap observation,
   then imported-save loading with writes disabled, then durable saving and
   full power-off/reload from the real disc.

Keep ordinary BIOS GD Play available while this route is experimental. Reset,
reinitialization, disc changes and return-to-menu need explicit quiesce,
checkpoint and device-release behavior. Sharing a card between an image and
its original disc is useful when assigned deliberately; it must not be an
automatic title-string merge.

## Completion criteria and product opportunities

The practical product improvements are predictable gameplay latency, a short
repeat launch, visible card/slot selection, whole-save-set imports and clear
unsupported reasons. These can be compared against other implementations by
the same observable measures; source differences alone do not establish a
better product.

The acceptance matrix should keep these axes independent:

- **Games:** Power Stone 2 as a stable baseline, SA2 for sustained input/frame
  sensitivity, Skies for the reported control effect, then one title from
  each newly admitted family and revision.
- **Startup:** empty/stale/corrupt cache, repeat cache hit, changed executable,
  changed SD allocation and missing/removed card. Measure phases instead of
  reporting one unqualified boot-time number.
- **Cards:** empty, imported, nearly full, companion saves, duplicate filenames
  on separate cards, different playthroughs, multi-disc assignment and a
  physical A1 alongside virtual A2.
- **Persistence:** create/save/load, full power-off/reboot/load, fresh ownership
  scan after filesystem changes and fault recovery without touching foreign
  card data. Block durability does not imply atomicity of an entire game save.
- **Runtime:** capture disabled for normal measurement, VMU-off comparison,
  cold/warm launch, busy storage and new save bursts. Diagnostic sampling
  overhead must not be mistaken for the release worker's cost.
- **Transport/source:** SCI image, SCIF image, real disc plus SCI, real disc
  plus SCIF; native, WinCE and CDDA combinations recorded separately.

Useful follow-on features include an on-console compatibility report with
the exact refusal phase, card labels and free-space display, a controlled
export/duplicate-card operation, and a compact timing history for cache miss,
cache hit and save commits. These should reuse the same card service rather
than creating new writable paths with weaker checks. Multiple simultaneous
virtual cards, every native SDK, every WinCE title and every peripheral remain
separate extensions beyond this first complete single-card workflow.

## Source anchors

- [`vmu-save-collection-design.md`](vmu-save-collection-design.md) and
  [`vmu-normal-launch-import-test.md`](vmu-normal-launch-import-test.md): card
  collections, save sets and earlier normal-launch/import work.
- [`../include/kui/vmu_collection.h`](../include/kui/vmu_collection.h),
  [`../src/apps/vmu_collection.inc`](../src/apps/vmu_collection.inc) and
  [`../src/experimental/vmu_import_prep/main.c`](../src/experimental/vmu_import_prep/main.c):
  immutable publication, physical capture and persistent preparation.
- [`../src/apps/games_native_cache.inc`](../src/apps/games_native_cache.inc),
  [`../src/apps/games_retail.c`](../src/apps/games_retail.c) and
  [`evidence/native-sdk-family-corpus-2026-10-22.json`](evidence/native-sdk-family-corpus-2026-10-22.json):
  launch/cache boundaries and unresolved native families.
- [`vmu-native-runtime-cost.md`](vmu-native-runtime-cost.md),
  [`../src/loader/vmu_volatile_chain.c`](../src/loader/vmu_volatile_chain.c) and
  [`../src/loader/vmu_readonly_overlay.c`](../src/loader/vmu_readonly_overlay.c):
  current polling, peripheral and A1 boundaries. Its measurements describe
  the preceding worker; current performance changes require a fresh report.
- [`../include/kui/vmu_emulation.h`](../include/kui/vmu_emulation.h),
  [`../include/kui/vmu_persist_session.h`](../include/kui/vmu_persist_session.h),
  [`../include/kui/vmu_persist_profile.h`](../include/kui/vmu_persist_profile.h),
  [`../src/loader/vmu_sd_lease.inc`](../src/loader/vmu_sd_lease.inc) and
  [`../src/loader/retail_storage.h`](../src/loader/retail_storage.h): protocol,
  durable session, prepared profile and storage-ownership contracts.
- [`vmu-optical-launch-assessment.md`](vmu-optical-launch-assessment.md) and
  [`../src/apps/gd_play.c`](../src/apps/gd_play.c): original-disc requirements
  and the current BIOS reboot handoff. The older assessment's exact-title
  examples are not a claim of a current optical implementation.
