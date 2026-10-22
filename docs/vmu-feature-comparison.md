<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Virtual VMU feature and performance comparison

Comparison updated on 22 October 2026 after the native SDK/cache iteration.
K-UI performance baseline:
`2e5314bd582f8dd4f124301feedbd15aafe50f1b` on
`recovery/native-worker-20261022`. SWAT/DreamShell baseline:
[`f3cb6cd8f11cece3461244e2b6c92f1c539983ab`](https://github.com/DC-SWAT/DreamShell/commit/f3cb6cd8f11cece3461244e2b6c92f1c539983ab),
confirmed as upstream master during this review. No upstream implementation
was imported or translated. The changes and remaining priorities below describe independently authored
behavior and tests, not an instruction to transplant code.

## Evidence boundaries

The owner confirmed Power Stone 2 saving and loading after a complete reboot
with K-UI `d503f1aa423d` and prepared card p0005. That build had unacceptable
startup, FMV and gameplay speed. The owner subsequently confirmed that
`2e5314bd` runs Power Stone 2 FMV and gameplay smoothly, with roughly five
seconds to boot followed by a 30-second native scan. The persistence report
and the later performance report are distinct evidence; neither certifies the
new SDK/cache iteration on hardware.

This iteration provides console-test candidates for Power Stone 2, Skies of
Arcadia and Sonic Adventure 2. The latter two pass complete immutable SDK
resolution plus an independent SH argument-transport model and live layout
binding. Resident Evil Code: Veronica passes immutable resolution, but its
modeled live binding refuses RX/TX storage overlapping the loaded executable;
that guard remains enabled and it is not a supported persistent candidate.
The nine-input findings are recorded in
[evidence/native-sdk-family-corpus-2026-10-22.json](evidence/native-sdk-family-corpus-2026-10-22.json).

Toy Commander and Silver retain their scoped owner-reported save/reboot/load
results. The owner reported Armada WinCE save/reboot/load success in this
conversation; its older console guide still says pending. None establishes
universal native or WinCE support.

SWAT findings below establish source behavior and reachable build/settings
paths. This review did not run DreamShell on hardware or measure its frame
time. A combined feature build is evidence that a combination is implemented,
not that every title, transport and peripheral works. K-UI's ordinary game
reader supporting a transport does not establish writable VMU support on it.

## Why the first generic K-UI worker was slow

The problematic K-UI worker ran all instructions through uncached P2 and
repeated complete ownership/layout proofs within the same uninterrupted hook.
Production-C instrumentation measured 1,268 guest/MMIO reads for a controller
poll pair: ten heap walks, five layout binds, eight runtime checks, seven TX
parses and five installed-hook checks. A valid 64-node free list raised that
to 2,528 reads. These costs occur without a VMU storage request. They explain
why gameplay can slow down even when serial-storage bandwidth is sufficient;
they do not by themselves measure the console's lost frame time.

SWAT's normal Maple path hooks the CPU interrupt vector, handles ASIC Maple
DMA completion, reads the hardware DMA source and walks at most 24 descriptors.
It dispatches port-A/A1 traffic without discovering each game's Maple submit
and completion functions. The inspected path has no equivalent per-poll heap
walk, RX-layout rebinding or original/private-chain integrity replay. Normal
loader code uses P1; DMA memory is explicitly accessed through an uncached
alias. Its optional in-game allocator search remembers a matching signature
and uses fixed function offsets, rather than repeatedly proving ownership.
See [S1], [S2], [S3] and [S4].

The accepted `2e5314bd` correction reduced the original controller fixture
from 1,268 reads to 450 and block reads from 2,094 to 880. Immutable worker
instructions/constants use P1 while mutable state, DMA storage and stack
remain P2. The current iteration additionally prepares immutable geometry
once, samples the required live words without rebuilding it for every poll,
and combines lease/free-list validation into one walk per check. Updated
production-C fixtures measure 344/366 reads versus their 450/476 comparison
runs; these are guest/MMIO read counts, not console frame-time measurements.
Full live state, installed-hook, lease/lifetime and external-storage boundary
checks remain active. New console timing and save/reboot/load tests remain
necessary for the new iteration.

Startup now has an operational complete-resolution NVR fast path. A preseeded
record binds driver, layout, allocator, initializer call, terminal exits and
patch-interior proof to exact executable bytes/load/SHA256 and detector epoch.
The personalized module independently pins the record digest; a matching
launch rehashes the loaded executable and replays that immutable proof without
repeating the discovery scans. A miss falls back to complete discovery.
Live manager buffers, the actual allocator lease, hook/lifetime state, card
identity and filesystem ownership are never restored from this cache.
See [native-worker-checkpoint.md](native-worker-checkpoint.md) and
[vmu-native-startup-performance.md](vmu-native-startup-performance.md).

Startup has another cost: a clean persistent-stage fixture performs about
1,096 sector reads, mostly two full image verification passes around recovery
and checkpointing. Fresh filesystem ownership, card identity and recovery
must not be replaced by stale saved authority merely to reduce startup time.
Measure scan CPU, ownership I/O, recovery, checkpoint and final verification
separately before choosing the next optimization.

## Feature matrix

| Area | SWAT/DreamShell source | K-UI at this checkpoint | Product implication |
| --- | --- | --- | --- |
| Automatic interception | VBR/ASIC completion interception reads the hardware Maple chain; optional UBC path exists. [S1][S2] | Dynamic SDK/allocator/layout/exit discovery now admits reviewed pointer-array and contiguous initializers, two IRQ register-allocation shapes and four bounded terminal-exit families. Power Stone 2/Skies/SA2 are console-test candidates; earlier Toy/Silver and Armada adapters remain separate. | Two additional native startup contracts are implemented and host-tested. Other corpus failures remain explicit. An independently designed interrupt observer could later reduce SDK dependence, with its own vector/lifetime contract. |
| Runtime work and cache | One bounded chain walk at completion; ordinary code P1, DMA buffers uncached. [S1][S4] | Owner accepted the cached baseline on Power Stone 2. Prepared live-geometry checks and a single lease/free-list walk further reduce host read counts; this iteration still needs console acceptance. | Benchmark controller-only, card-read and save workloads separately. Keep full validation at actual external mutation boundaries, with cheap checks between private operations. |
| Virtual slots | One virtual A1, selected by a numbered VMD. Other port traffic is skipped by the VMU dispatcher. [S1][S5] | Native integration is one A1. Armada is one A2 with physical A1 retained. The protocol core accepts a configurable single subunit, but this is not multi-card integration. | Neither inspected VMU implementation supplies several simultaneous virtual cards. Selectable slots and explicit physical-conflict policy are worthwhile independent additions. |
| Unrelated physical peripherals | Original DMA remains intact; the VMU handler limits edits to port A. Physical requests on other slots/ports are not rebuilt. Source contains an unfinished physical-VMU filtering item. [S1] | Native filtered chain now retains one-hot physical subunit INFO/ALLINFO and bounded one-word GETCOND for controller, storage, LCD, clock or rumble functions. Main-controller queries retain their controller-only contract. Physical writes, RESET/KILL, SETCOND and storage MEDIA/READ remain refused. | Device discovery and bounded condition queries are implemented. Physical rumble actuation, physical card I/O and unrestricted accessory passthrough remain separate compatibility work. |
| Physical A1 collision | No prevention of sending commands to a real A1 in the inspected Maple code. [S1] | A1 requests are removed before physical DMA; parent A1-presence conflicts refuse before virtual storage handling. | Retain the physical-command exclusion. Offer an empty alternate slot or explicit refusal; never silently shadow a user's writable real card. |
| Unusual schedules | Walks the current completed chain; no requirement for an A-parent request in every batch. [S1] | Overlay requires exactly one port-A main-controller request per batch; filtered chain requires at least one physical descriptor. All-virtual batches and parent-free batches are not yet supported. | Independent support for these schedules can admit games even when SDK discovery already succeeds. Virtual-only completion needs a deliberate DMA/consumer design. |
| Storage protocol | INFO/ALLINFO/media/read/write/sync handlers. Katana can advertise one write phase; other OS paths retain four. [S1][S6] | Standard 512-byte blocks, four 128-byte write phases, one read phase, final durable commit. Native overlay admits only its explicit command/phase subset. | Support additional legal schedules/phases only with exact snapshots and repeat/idempotence tests. One-phase writes are an optional throughput feature, not required to fix controller overhead. |
| RESET/KILL | No dedicated virtual RESET/KILL cases in the inspected command dispatcher. [S1] | Protocol core implements both. The native filtered overlay and durable worker now admit a zero-payload virtual RESET, discard staged RAM and return ACK without an SD loan. Native KILL remains refused. | RESET integration is implemented and host-tested; verify it on console. KILL still needs endpoint timeout and parent-advertisement integration. This is not an established SWAT advantage. |
| LCD, clock, beep and VMU CPU | Device information advertises LCD/clock alongside storage; non-storage block writes receive an acknowledgement without storage work. No virtual LCD renderer, clock service or standalone VMU CPU was found in this Maple implementation. [S1] | Honest storage-only advertisement. No virtual LCD, clock, beep or VMU CPU. Physical startup branding is separate. | Do not count advertised bits or ignored LCD writes as complete LCD/clock emulation. A physical LCD function router or launcher LCD preview can be separate optional features. |
| Rumble | No virtual rumble in this VMU implementation; unrelated real-device DMA remains physical. [S1] | No virtual rumble; native accessory passthrough is currently restricted. | Preserve real accessories first. Virtual rumble is not a useful substitute for the player's physical hardware. |
| Card capacity | UI has private 128 KiB and 1 MiB templates; runtime derives capacity from the opened VMD and system-block metadata. [S1][S5] | Standard 128 KiB, 256 physical/200 user blocks; custom/241-block/larger cards are outside the validated endpoint contract. | Automatic separate standard cards scale the collection without nonstandard geometry. Larger cards are optional and require title compatibility and filesystem validation. |
| Card assignment | Numbered shared card or a private image in the game directory; settings persist the VMU number. [S5][S7] | Existing pNNNN image/journal/identity/ownership pair remains bound by the personalizer. Exact packaged game selection can attach a checked NVR seed. Automatic whole-card/save-set assignment remains unimplemented. | Launcher-managed private/shared cards and stable bindings across original/2048/container/multi-disc variants are high-value usability work. Keep overlapping save names on separate save sets. |
| Durable acknowledgement | Seeks and reads/writes the backing file, requests filesystem sync and returns a VMU response. No VMU redo-journal/readback contract appears in this handler; SD CTRL_SYNC itself returns success in the inspected disk layer. [S1][S8] | Identity/extents/ownership admission, ordered redo-journal writes/syncs, complete base-block readback comparison and healthy shared-bus restoration precede final write ACK. Scoped hardware save/reboot/load evidence remains distinct from the new iteration. | Keep this stronger explicit per-block contract while reducing redundant CPU work. It does not make an entire multi-block game save atomic. No general claim about all SD cards' power-loss behavior follows. |
| CDDA plus VMU | `feat`/`full` builds include both; module selection chooses the combined variant. Busy image I/O causes VMU AGAIN responses. [S1][S6] | New native and Armada writable tests still gate CDDA off. Existing separate native/CE CDDA support does not prove combined arbitration. | Implement and test a common bounded storage owner/scheduler: FMV, audio refill and save operations need deterministic priority and retry behavior. |
| SCI/SCIF/IDE | SD path mounts SCIF then SCI; SD and IDE writable VMU feature variants exist. CD-loader builds also include Maple code, but this does not establish writable optical VMU backing. Network/DC-LOAD standard target lists are commented. [S6][S8][S9] | VMU backing is SCI/Standard only. Ordinary game reading supports more modes/transports; IDE remains experimental and SCIF VMU backing is separate work. | Reuse a transport-neutral durable service with separately validated loans. SCIF bandwidth may suffice for saves while its CPU cost still competes with playback. Do not open unsupported combinations by deleting gates. |
| WinCE/KOS breadth | Shared interrupt VMU path has explicit WinCE vector handling; main initializes VMU for OS types and uses single-phase mode only for Katana. [S2][S6] | Armada CE has owner-reported save/reboot/load; generic CE adapters and KOS-native game coverage remain unproven. | “WinCE works” should mean the admitted driver, transport and combination. A generic vector path is a compatibility direction, not proof every CE kernel/driver works. |
| Return, reinitialization, multi-disc | Menu exit waits for reads, stops CDDA and loads DreamShell/BIOS. Reader has disc-change paths and presets retain launch choices. [S7][S10] | Reviewed native exit/reinit/free paths stop terminally before reclamation. No demonstrated seamless launcher return/checkpoint/re-entry. CE shutdown with an outstanding loan remains an unverified lifecycle. | Add a clean quiesce/checkpoint/transport-release/handoff lifecycle, then test save after reset and disc changes with stable card assignment. |
| Modern physical banked cards | Prelaunch game-ID bank selection detects VM2, USB RP2040 emulation and VMUPro extended identities. This is physical-device integration, separate from virtual VMD storage. [S11] | No equivalent game-ID bank-switch integration found in the current launcher. | Useful optional convenience after ordinary virtual-card selection; request a documented device capability and preserve a manual override. |
| Screenshots and presets | Controller screenshot hotkey is available in full builds; user/bundled device-specific presets select VMU/CDDA/IRQ/heap settings. External syscall use disables some combinations. [S1][S6][S7] | Per-game reader/music/encoding settings exist; no equivalent in-game screenshot hotkey is integrated. Preseeded complete NVR replay is operational. The new Games cache writer is source-integrated and host-tested, but the full UI native build is blocked. | Prefer clear compatibility status and automatic proven settings over exposing many low-level tuning knobs. Screenshot writes must use the same storage arbitration. |

K-UI evidence for the integration restrictions above is in
`src/loader/vmu_volatile_chain.c` (`volatile_chain_classify`, `build`, `verify`),
`src/loader/vmu_readonly_overlay.c` (`target_response_bytes`, `prepare`,
`complete`), and `src/core/vmu_emulation.c`. In particular, protocol-core
support is not evidence that the native overlay admits that command.

The old `games_native_vmu.inc`/`.nvm` mapping preflight is not the new cache.
The operational path uses 1,024-byte `.nvr` complete-resolution records, a
versioned detector epoch and a module seed carrying an independently pinned
record digest. The cache proves immutable resolution only, including all
uniqueness and patch-interior checks performed when it was produced.

The new Games-route writer reads/builds these records, syncs and reads back a
temporary file before publishing it, then fills the copied module seed. Its
source integration has host tests, but the full UI native build remains
blocked. The current delivered launcher binary therefore cannot be described
as automatically discovering and writing NVR records. Packaged/preseeded
records work without that launcher update.

The durable contract is also precise: a block commit invalidates and syncs the
old marker, writes and syncs the redo payload, writes and syncs its CRC-protected
commit marker, then writes and syncs the base block. The durable worker reads
that complete base block back and compares all 512 bytes before final ACK,
after the storage loan returns with a healthy restored bus. Journal marker and
payload CRCs are verified during recovery; the current code does not claim an
independent readback of each individual journal write. A storage error blocks
success, and a later open handles retained committed redo data. This is a
per-block protocol, not atomicity for an entire game save or a guarantee about
all SD controllers under power loss.

## Completed work and remaining priorities

1. **Console-test the new iteration.** Power Stone 2 baseline performance is
   owner-accepted. New Power Stone 2, Skies and SA2 candidates now need matching
   startup, controller/FMV/audio, card read/write and cold-reboot load results.
   Preserve existing card pairs. Compare a preseeded NVR hit with the full-scan
   fallback separately from card recovery/checkpoint I/O.
2. **Finish the launcher cache build and card selection.** Complete-resolution
   replay and packaged seeds are implemented; Games-route cache file handling
   is source-integrated and host-tested. Finish the blocked full UI build and
   console validation before promising automatic cache writes. Automatic
   standard-card/save-set assignment and dynamic checked handoff remain
   separate work; retain stable image/journal/identity pairs and explicit
   known aliases across original/container/multi-disc variants.
3. **Extend schedules and physical traffic deliberately.** Virtual RESET and
   bounded physical discovery/condition queries are implemented. Native KILL,
   parent-free and all-virtual batches, rumble SETCOND and physical storage I/O
   remain refused. Each extension needs bounded synthetic schedules and a
   focused controller/card/accessory console case. Keep every virtual A1
   command excluded from physical DMA.
4. **Use corpus refusals to guide the next SDK adapter.** Pointer-array and
   contiguous initialization and newer terminal shapes are implemented without
   per-title patch addresses. RECV remains blocked by executable/buffer
   overlap; Grandia II by allocator ambiguity; other inputs have unresolved
   initializer/IRQ/terminal contracts. Design and prove the missing ownership
   or interception contract before admitting a title. A static cache hit never
   bypasses a live refusal.
5. **Measure remaining startup I/O.** The NVR path removes repeated immutable
   scans, while card recovery/checkpointing still performs fresh ownership,
   identity and image verification. Measure those phases before reducing work;
   do not cache live allocation or filesystem ownership as authority.
6. **Prove CDDA arbitration, then SCIF backing.** Keep final ACK/base readback
   and bounded retries. Coordinate audio refill/image reads and save commits
   through one storage owner, distinguishing bandwidth from CPU stalls. An
   optional compressed-audio sidecar can reduce payload bandwidth later; it
   cannot repair excessive controller-poll work.
7. **Add deliberate slot and lifecycle policies.** Empty-slot preference,
   physical conflict explanation, explicit shared-card choice, reliable return
   to launcher, reset and multi-disc handoff still need integration and
   save/reboot tests. Current reviewed exit/reinit/free paths stop safely rather
   than demonstrating seamless return and re-entry.
8. **Keep optional features separate.** Physical banked-card selection,
   screenshots, LCD preview/function routing and larger custom cards can follow
   stable ordinary saves. Standalone VMU execution, virtual rumble and arbitrary
   loader placement are separate projects.

## Pinned primary sources

- [S1: Maple VMU implementation](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/maple.c), especially command handlers, `maple_dma_proc`, `maple_dma_handler`, `maple_init_vmu`.
- [S2: Interrupt-vector installation](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/exception.c#L93-L164) and [ASIC dispatch](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/asic.c#L40-L82).
- [S3: Allocator detection/placement](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/malloc.c#L25-L178).
- [S4: Cache startup](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/startup.s#L32-L75) and [P1 target/build configuration](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/Makefile.cfg).
- [S5: Shared/private 128 KiB/1 MiB card selection](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/applications/iso_loader/modules/module.c#L1600-L1639) and [one-A1 parameter](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/include/isoldr.h#L200-L215).
- [S6: Actual feature build targets](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/Makefile), [module selection](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/modules/isoldr/module.c#L504-L530), [main OS/VMU setup](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/main.c#L191-L214).
- [S7: Preset fields and selection](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/modules/isoldr/preset.c), [preset packaging](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/applications/iso_loader/README_PRESETS.md).
- [S8: Device writes/sync](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/fs/fat/src/diskio.c#L306-L434).
- [S9: SCIF/SCI selection](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/fs/fat/fs.c#L97-L107), [SD backends](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/dev/sd/sd.c#L66-L118).
- [S10: Menu return](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/syscalls.c#L1462-L1490), [reader/disc-change paths](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/firmware/isoldr/loader/reader.c).
- [S11: Physical VM2/USB4Maple/VMUPro bank selection](https://github.com/DC-SWAT/DreamShell/blob/f3cb6cd8f11cece3461244e2b6c92f1c539983ab/modules/isoldr/vm2.c#L136-L197).

Relevant K-UI contracts: [protocol](vmu-protocol.md),
[store](vmu-store.md), [backing](vmu-backing.md),
[card collection](vmu-save-collection-design.md),
[native checkpoint](native-worker-checkpoint.md),
[Armada test scope](armada-ce-vmu-write-console-test.md),
[optical-launch assessment](vmu-optical-launch-assessment.md).
