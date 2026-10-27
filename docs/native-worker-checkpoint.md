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

`Makefile.vmu-native-passive` builds the one-pair hardware test. It passed on
Power Stone 2 with build `abd30e1d97cc` (details below). It copies the driver, allocator lease, immutable initializer layout,
display registers and runtime configuration into the relocated worker. All
retained addresses are game/lease/low-resident addresses. The high-stage
installer and stack pointers are cleared before publishing installed state.

The new layout adapter decodes the reviewed variable initializer and its actual
argument/index provenance, plus gun and TMU1/TMU2 callback cells. For the PS2
sample the corrected mapping is TX `ac315180`, raw RX pointer array `0c20ea4c`,
aligned `fc8` value `ac20ea60`, twelve inputs and twenty-four nonzero slots
across two banks. The RX pointer array is preloaded executable data; its buffer
pointers address BSS. The worker compares the live array to its exact captured
immutable values at first submission, reproduces alignment/P2/bank assignment,
checks all 48 manager entries and rereads the array for stability.
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

Build `ac097cf48ff0` was tested on hardware and refused at phase `0000000c`,
detail `0000000e`, with zero submitted/completed transfers, inherited SR
`60000161` and lease `0c38a880`. The binding refusal caught a reversed argument
mapping in the adapter: root R6 is the RX array and R7 is TX storage. The upper
wrapper reloads R5 from saved R6, and R6 from saved R7, immediately before the
variable-initializer wrapper. Tests that merely repeated the mistaken mapping
did not catch this; the correction adds an independent actual-instruction
argument-transport regression. No physical DMA was started by the refused
submission gateway. The owner reported a one-to-two-minute pause during layout
discovery. Combined fresh driver/layout discovery removes the duplicate SDK
scan and SHA, and cheap opcode prefilters avoid most witness-helper calls.
Console timing of that optimization has not yet been measured.

The isolated resident route now uses ordinary GD destination mapping plus
dynamic exclusion of the complete 192 KiB lease and its 32-byte allocator
header. The heap initializer remains intercepted: repeat initialization stops
before reclamation. Dynamic terminal entry is bounded to the first 64 KiB.
The immutable patch-window scan rejects direct branches or aligned RAM
literals into the displaced instructions' interior. It cannot prove arbitrary
indirect flow. Power Stone 2 passes that conservative scan.

Validation of the corrected mapping: 605 ASan/UBSan passive protocol/boundary
checks and 1,509 layout checks
including 21 actual-startup mutations and duplicate timer ambiguity, 206 patch
window checks, and 80,175 resident destination/terminal checks. The actual
linked passive gateways pass 5,760 register-state scenarios at three relocated
addresses plus four corrupted-gateway cases; the stage gateway retains its
1,288 scenarios. Exact ELF/flat image/export/relocation/stage/module binding
audits pass, including bootstrap and artifact mutations. Relocation extraction
now correctly scopes duplicate local C symbols by index while refusing global
name conflicts; its nine extractor regressions pass. Scan instrumentation on
the actual startup image counted 6,194,862 full witness-decoder calls before
opcode prefilters and 1,927 afterward, with identical discovery constraints.
The combined API additionally avoids one full driver scan/hash replay. No
cache-mode changes were made.

The failure screen now retains exact live-bind code/address/value/expected.
`CHECK` values `00010001`..`00010014` encode layout codes 1..20, documented in
`vmu_native_layout.h`. Other nonzero CHECK values are runtime checker results;
their AT/VALUE fields show the last sampled word, and EXPECT `ffffffff` means
that the runtime validator did not supply an expected value.

Build, test and audit (supply the unpacked PS2 startup only for real-image
mutation tests; copyrighted game binaries are not included in source):

```sh
export PATH="$PWD/.deps/sh-elf/bin:$PATH"
make -f Makefile.vmu-native-passive BUILD=build/native-passive-release -j4 all
make -f Makefile.vmu-native-passive BUILD=build/native-passive-release passive-host-tests STARTUP=/path/to/1ST_READ.BIN
python3 tools/check_vmu_native_passive.py build/native-passive-release --self-test
python3 tools/package_vmu_native_passive.py build/native-passive-release
```

After the read-only console test below, add persistence using the existing
durable core. Before allowing sustained gameplay, prove generic teardown/exit provenance and all lifetime paths, then
restore frontend/cache integration and test more initializer families. CDDA
coexistence and SCIF VMU backing remain outstanding. Keep the previously proven
Toy, Silver and Armada paths unchanged; do not remove their admission gates.

## Hardware passive result and read-only continuation

The owner supplied the successful `abd30e1d97cc` hardware screen: phase
`0000000b`, detail zero, one submitted/completed pair, 24 RX slots, four
descriptors, no NOP descriptors. Source was `0c315180`, 48 bytes, fingerprint
`88230d29`; inherited submit/complete SR were `60000161`/`600001f1`.
First RX `0c30f000` contained `1c200005`; the last reply was `ffffffff`.
The runtime check returned zero and the lease remained `0c38a880`.
This proves one physical pair, not completion return or virtual responses.

`Makefile.vmu-native-readonly` now builds a separate bounded blank-A1 test.
The worker retains the original physical descriptor chain and admits only
the existing read-only overlay protocol. Each completion rechecks ownership,
layout, bank, TX snapshot, original PR/context and the dynamic consumer cell.
The overlay stages every reply, requires a physical main-A controller with
A1 absent and target timeout, preserves unrelated replies, publishes payload
before header and advertises A1 last. The returning gateway preserves the
original consumer's register/SR/PR/MAC/GBR context, including inherited SP.
Success stops at phase `0000000d` after INFO or ALLINFO, media and one block
read. Writes, sync and unsupported A1 requests stop before physical DMA;
there are no persistent storage operations in the relocated worker.
The window is limited to 3,600 matched pairs. This build has not yet run on
hardware and does not enable ordinary native-game saving.

The initializer pointer-slot guard remains installed. After the first real
initializer and our allocation return, its entry also becomes a terminal
guard. A guarded release entry validates the live lease, candidate chunk and
free-list exclusion before native free; unrelated valid frees remain allowed.
The release gateway keeps IMASK15 continuously through validation and the
original mask-setting prologue, while retaining the inherited mask in R7 for
the original epilogue. Its exact ten-instruction replay resumes at release+20;
the signed word literal is `ffffff0f`. Admission checks both original entry
windows again before patching and scans submit, initializer and the full
20-byte release replay window in one immutable image pass.

This is still bounded lifetime evidence. The release predicate does not prove
that every arbitrary aligned pointer was actually allocated. Power Stone 2's
observed TX/RX buffers are static and below its managed arena. Broader games
with heap-allocated SDK buffers need their release exclusion too. Generic
BIOS exit can use both `8c008300` and the firmware vector, with cleanup before
the latter: the ordinary top-level return guard does not cover every such
path. Generic teardown must be established before long gameplay/persistence.

Host validation includes 1,966 read-only worker/protocol/overlay checks,
18,648 release-admission checks, 1,143 patch-window checks with the actual
startup, and the existing passive/layout/resident/relocation regressions.
The actual read-only gateways are modeled at three relocation destinations
and all interrupt masks: 7,680 cases, including 1,920 release cases compared
with the independent original twenty-byte prologue. Mutation cases include a
transient interrupt gap that would preserve final registers. The artifact
audit binds the ELF, flat worker, exact relocation table, stage embeddings and
final module, and rejects storage/persistence links in the worker.

```sh
export PATH="$PWD/.deps/sh-elf/bin:$PATH"
make -f Makefile.vmu-native-readonly BUILD=build/native-readonly-release -j4 all
make -f Makefile.vmu-native-readonly BUILD=build/native-readonly-release readonly-host-tests
make -f Makefile.vmu-native-readonly BUILD=build/native-readonly-release passive-host-tests STARTUP=/path/to/1ST_READ.BIN
python3 tools/check_vmu_native_readonly.py build/native-readonly-release --self-test
python3 tools/package_vmu_native_readonly.py build/native-readonly-release
```

Commit first and build into a fresh directory for the packaged source/build
identity check. Install by copying the supplied KUI folder; no installer or
backup/restore receipt is involved. Use the included README for hardware steps.

## Read-only hardware pass and native durable save test

The owner supplied the successful `65699ce32aa2` Power Stone 2 screen on
22 October 2026. Phase was `0000000d`, detail zero, submitted/completed both
`00000065` (101), 24 RX slots, 101 advertisements and three virtual replies:
one INFO, one media reply and one block read. Refused, overlay, flags and
runtime CHECK were zero. Last request `0200010b`, reply `82010008`, RX
`0c30f820`; lease remained `0c38a880`. This establishes returning completions
and real game-driven card reads. It does not establish native persistence.

The new separate `Makefile.vmu-native-persistent` uses the existing durable
backend and live ownership/checkpoint pipeline. Its worker uses 128KiB of
the unchanged192KiB allocator lease, with an80-byte typed export header and
private last4KiB stack. Passive and read-only modes retain64KiB. Exact linker
relocation extraction and the C relocator now support at most128KiB; the
actual table still enumerates every internal pointer and fails before writes
on malformed input. No permanent high-stage pointers survive installation.

The stage accepts a4096-byte prepared P2 profile, scans its live filesystem
ownership, recovers/checkpoints the image and journal, verifies card identity
and release health, and copies only the1272-byte sealed runtime handoff.
The worker filters every A1 packet out of physical Maple DMA using the
established physical-controller chain builder. It snapshots full128-byte
write payloads, validates the filtered source at idle completion, restores
the original MDSTAR and rechecks the original SDK chain before handling the
virtual request. Final write ACK requires the existing journal/data readback
and successful restoration of the exact scoped SCI loan shared with GD I/O.

The persistent-only low reader pins the exact relocated transaction callback,
validates the installed dynamic worker/lease/control shape, and protects the
complete lease plus header from GD destinations. The SCI fault/restoration
checks include this new mode; the ordinary low game reader contains no
storage writer. Native release excludes all admitted SDK RX/TX/control spans
and the preloaded executable, as well as the worker. Every boundary checks
the exact submit/release/initializer/exit patches and original callback cells.

Strict exit discovery now finds three complete reviewed SDK families:
normal exit, cleanup exit and executable-loader entry. The PS2 sample resolves
`8c2b3a98`, `8c2b3ac8`, `8c2aeaa0`; its exec thunk transports`8c008300`.
Additional encoded BIOS-vector/bootstrap consumers, ambiguous families and
interior targets are refused. All three entries stop before cleanup or image
replacement. This is not a proof of arbitrary synthesized indirect exits.

The save test continues after reads and commits, with a180,000-pair diagnostic
limit. Console save, full power-off and reboot/load remain the acceptance
test; host commits alone never count as verified hardware save/load. CDDA,
SCIF backing, other initializer/exit families and general frontend routing
remain outside this test. Previously proven Toy/Silver/Armada paths retain
their existing admission gates.

The ZIP's single `personalize.py MOUNT_ROOT` command selects an existing
prepared pair, validates its exported profile and identity, and installs only
the personalized loader. It changes only the module's typed profile span
and envelope CRCs; it never copies or edits the pair. Historical snapshot
hashes may differ after legitimate saves; live console recovery produces the
new handoff. Ambiguous pairs require explicit `--profile`. This setup is
card binding, with no backup/restore installer flow.

Host validation of this continuation: 1,187,929 integrated worker/protocol/
store/SPI checks, including four RAM-only write phases, durable commit,
readback, management checkpoint/reseal and reopened-card loading; torn base
writes and failed release never produce a final ACK. Lifetime ranges pass
18,695 checks; exit discovery passes309 with the real startup. The extended
relocator passes769,073 checks, and the actual worker table is compared at
three destinations. Native SCI restoration, 20,000 dynamic callback-interval
cases, original fixed-loan regressions and12 personalizer integration tests
pass. Linked code/stack/gateway audits are required before packaging.

The completed linked audit passes: worker30,016 initialized bytes,461 exact
relocations, unchanged192KiB game allocation. The low resident ends exactly
at`8c007800`; its scoped SCI gateway is312 bytes with a76-byte C frame.
Source-qualified emitted call graphs, bounded indirect callback sets and an
assembly margin bound the worker/SCI path to1,736 of4,096 bytes; the ordinary
GD path is1,228 of1,232 guarded bytes. Unknown callbacks, missing frames and
cycles fail the audit. Both actual PIO adapters prove callback-NULL and
one-sector writer entry. Gateway/bootstrap/artifact mutation regressions pass.

```sh
export PATH="$PWD/.deps/sh-elf/bin:$PATH"
make -f Makefile.vmu-native-persistent BUILD=build/native-persistent-release -j4 all
make -f Makefile.vmu-native-persistent BUILD=build/native-persistent-release persistent-host-tests STARTUP=/path/to/1ST_READ.BIN
python3 tools/check_vmu_native_persistent.py build/native-persistent-release --self-test
python3 tools/package_vmu_native_persistent.py build/native-persistent-release
```

Use a fresh build directory after the final commit. The generic package has
an intentionally non-owner CI profile; run its personalizer before console
testing. Console success is a save that survives full power-off and loads on
the next launch. Record the owner result and build ID before broadening gates.

## Native persistence hardware pass and performance correction

The owner tested `d503f1aa423d` with prepared pair `p0005`: Power Stone 2
saved and loaded correctly, including a full power-off and reboot. Startup
took roughly five minutes, the FMV crawled, and actual gameplay was very slow.
Persistence is hardware-proven for that test; acceptable performance is not.

Production-C instrumentation found that a controller-only Maple pair performed
1,268 guest/MMIO reads, ten complete heap walks, five layout binds, eight
runtime checks, seven TX parses and five hook checks. Sixty pairs per second
would repeat 600 heap walks even with no SD request. A valid 64-node free list
raised the pair to 2,528 reads. Every instruction also ran through uncached P2.

The persistent worker now makes one complete submission proof and one complete
completion proof. Reviewed private RAM operations within each uninterrupted
IMASK15 call use compact live context/MMIO checks. Each external SCI loan keeps
a complete proof immediately before acquisition and after restoration; failed
proofs cannot publish the virtual reply or final save ACK. Heap reset/free,
exit, exact hook, original TX and filtered DMA lifetime protections remain.
Controller-only reads fall to 450, block reads from 2,094 to 880, and the
64-node controller fixture to 702. These are host callback counts, not console
speed claims. The integration suite passes 1,192,894 sanitizer checks,
including pre-acquisition and post-restoration heap/layout/TX/hook mutations.

`Makefile.vmu-native-performance` is a separate persistent build. Its mixed
linker layout retains the immutable 80-byte P2 export header, puts instructions
and constants in P1, and keeps every mutable object, DMA buffer and stack in
P2. The immutable/mutable boundary is cache-line aligned. Exact physical
PT_LOAD coverage, flat-image bytes, pointer aliases and all relocation records
are audited. The stage purges the owned lease before P2 relocation and publishes
instructions before the first P1 call; mutable aliases are never changed.
Low terminal/SCI admission accepts only this build's canonical P1 code targets
while retaining the P2 worker end and protecting the whole 192KiB allocation.

Startup also removes a duplicate full executable patch-target pass while
retaining all six guarded windows. The performance-only temporary relay enables
instruction caching after the existing full-RAM publication and keeps data
caching disabled. Its P2 return restores the owner's saved CCR mode bits.
The separate startup profiling tools document scan CPU and preparation I/O;
fresh filesystem ownership, recovery, checkpoint and post-checkpoint image
verification are unchanged. See `vmu-native-startup-performance.md`.

An upstream SWAT/DreamShell comparison was read-only. At pinned commit
`f3cb6cd8f11cece3461244e2b6c92f1c539983ab`, its Maple implementation uses the
VBR/ASIC DMA-completion interrupt path and hardware DMA source, with a bounded
descriptor walk. Optional allocator discovery keeps a signature result. That
path avoids K-UI's repeated per-poll ownership/layout proofs and runs normal
loader code through P1. No upstream implementation was imported by this work.

Build and audit after committing, using a fresh directory:

```sh
export PATH="$PWD/.deps/sh-elf/bin:$PATH"
make -f Makefile.vmu-native-performance BUILD=build/native-performance-release -j4 all
make -f Makefile.vmu-native-performance BUILD=build/native-performance-release persistent-host-tests passive-host-tests STARTUP=/path/to/1ST_READ.BIN
python3 tools/check_vmu_native_persistent.py build/native-performance-release --self-test
python3 tools/package_vmu_native_persistent.py build/native-performance-release
```

Install the performance ZIP with its `personalize.py` and the same explicit
p0005 profile. The next console acceptance test compares startup, FMV and
gameplay with baseline, then reconfirms the existing save and reboot/load.
CDDA, SCIF backing and broad frontend routing have not been opened.

Final linked performance audit: 29,920 initialized worker bytes, 452 exact
relocations and three disjoint physical loads. Models pass 7,680 worker gateway,
1,288 stage-call and 5,120 relay/cache cases; 17 gateway, eight relay and four
artifact mutations are rejected. A 448-node linked installer flow proof checks
that relocation, zero-fill and callback-cell initialization precede instruction
publication, which precedes the first worker entry. Stack bounds are 1,568 of
4,096 worker bytes and 1,228 of 1,232 guarded ordinary GD bytes. The legacy
persistent audit also passes with its original cache policy. Read-only and
passive regressions pass 1,966 and 605 checks respectively; the new exit guard
passes 595 checks with the real executable and the patch guard passes 1,143.

## Cached mappings, lower runtime cost and broader native families

The owner tested performance build `2e5314bd582f`: approximately five seconds
to the bootloader and another thirty seconds through startup; Power Stone 2
FMV and gameplay were smooth. The owner correctly requested heavier titles
before treating that result as general performance acceptance. Persistence
had previously passed on `d503f1aa423d` using p0005. Neither report establishes
all native titles or all transport/music combinations.

`Makefile.vmu-native-compat` now combines independently reviewed changes:

- Complete 1024-byte versioned resolution records, bound to executable SHA,
  size, load address and detector epoch. Replay skips immutable discovery
  after fresh image hashing and an independently pinned record digest.
  The immutable 1120-byte seed has exact typed ELF/module provenance. Invalid
  or other-image seeds take the original full discovery path. Live allocation
  safety, fresh patch windows, initializer/release replay and all lifetime,
  layout and storage proofs remain mandatory. Actual linked-SH models check
  6,144 replay/guard/ABI cases and 2,688 surrounding relay routes; ten cache
  gateway mutations are rejected.
- Generic contiguous initializer/root/wrapper and bounded allocator-expression
  discovery, plus reviewed PVR cleanup/compact/pair-only exit families. A
  shared twelve-byte veneer supports halfword-aligned cleanup entries while
  submit, initializer and release retain their original four-byte alignment.
  Independent instruction-transport and live-binding models pass Power Stone
  2, Skies of Arcadia Disc 1 and Sonic Adventure 2. RECV static discovery passes
  but its loaded-image/buffer overlap remains refused. Five other uploaded
  startup families remain unadmitted with distinct evidence reasons.
- Once-per-launch immutable geometry preparation, retaining all live witness
  sampling; full original proofs still surround every external SCI loan.
  Combined release/lease validation walks the same free list once. Actual
  production-C instrumentation reduces steady two-controller reads 450→344,
  four controllers 476→366 and a 256-node unrelated free guard 1056→543.
  These are work counts, not measured console frame rates or timings.
- Zero-payload virtual RESET discards staged RAM without SD access. Bounded
  physical non-target INFO/ALLINFO/GETCOND queries preserve typed real replies.
  SETCOND/physical storage/virtual KILL and parent-free/all-virtual schedules
  remain deferred. The existing A1 conflict and physical-DMA exclusions stay.

The actual Games frontend now prepares and persists `.nvr` records while FatFs
is still mounted, after final module choice and before map encoding/unmount.
It only opts in for a unique cache-bearing native SCI/Standard module, handles
corrupt/stale files, cancellation, low memory and optional write failures, and
never treats a cache as live ownership. Its real include passes 111,646 focused
ASan/UBSan checks with the actual PS2 startup. The recovered checkout lacks
complete KOS/FatFs headers/build dependencies; there is no newly built UI
runtime. The console ZIP instead supplies three exact-image precomputed
metadata records selected by `personalize.py --game ps2|skies|sa2`, keeping the
owner's working UI binary. Installing without `--game` provides a cold-scan
control. See `vmu-native-launch-cache.md` and `vmu-native-compat-test.md`.

Verification includes 67,277 resolution-cache, 63,936 C seed-codec, 1,196,760
persistent-worker, 5,401 prepared-live, 10,247 peripheral/RESET and 33,276
workload checks under ASan/UBSan. The Python personalizer preserves all twelve
legacy tests and passes thirty-two total; the independent seed-artifact audit
passes thirteen mutation tests. Real metadata fixtures contain no executable,
track, IP or save bytes. All three records were rebuilt and fully verified
against the external owner startup inputs before versioning.

After committing, build into a new directory, run `compat-host-tests` with
`STARTUP` set to PS2, `HEAP_NEGATIVE` to CT2 and `CACHE_STARTUPS` to the Skies,
SA2 and RECV executable paths. Then run
`tools/check_vmu_native_persistent.py BUILD --self-test` and
`tools/package_vmu_native_persistent.py BUILD`. The package requires a clean
source commit and matching fresh build ID and includes its full git bundle.
The next console work is the cached/cold, streaming, gameplay, save/poweroff/
reload matrix on Skies and SA2, followed by a brief PS2 regression.

The read-only SWAT comparison is in `vmu-feature-comparison.md`. Important
remaining work is broader Maple schedules, automatic card selection, CDDA
storage arbitration, SCIF/IDE loans and orderly reset/return/multi-disc
lifecycle. The ordered journal/base-block readback/healthy-bus ACK contract is
an explicit K-UI advantage; it is not a whole-game-save atomicity guarantee.

## SA2 phase-2 refusal and actual first-allocation placement

The owner reported Power Stone 2 and Skies launching successfully on every
boot, then supplied an SA2 refusal from `3cd64df45cfe`: phase 2, detail and
initializer slot `8c0101bc`, initializer `8c110144`, allocator `8c1101f6`,
submit `8c1587bc`. This is before the heap initializer runs. The report does
not separately establish a Skies virtual-card save/power-off/load cycle.

Both SA2's fixed four-MiB arena and the Skies BSS-tail arena end at
`8d000000`. Their first 192-KiB allocation returns raw P1 `8cfd0000`. The
128-KiB worker would occupy physical `0cfd0000..0cff0000`, overlapping the
post-initializer callback's old stack at `8cff0000-32`. The blanket high-tail
placement refusal prevented that overwrite. Earlier full static-resolution
tests did not exercise this gate with the actual first allocation; the linked
wrapper's Boolean placement oracle and synthetic runtime lease did not expose
this gap. Do not infer full-path readiness from static discovery alone.

The native stage now owns an aligned, typed 64-KiB `native_installer_stack`
in its BSS. The low heap hook switches to this stack after the original game
initializer returns; the earlier relay stack and UI package header remain
unchanged. The pure production placement guard protects the entire linked
stage/BSS span, including the new stack, while preserving executable,
arena-start, sentinel, dispatch and full predicted allocation exclusions.
Game arena arguments and capacity are unchanged. The high stage is discarded
before ordinary game allocation resumes.

The same review found a second refusal: runtime worker admission and several
DMA/RX protection calculations assumed raw allocator payloads were physical.
Raw owner begin/header/payload and post-allocation/free alias checks now remain
exact; self-location, runtime geometry, DMA exclusions and low-control bounds
use canonical physical addresses. P2-to-P1 relocation first masks the source
alias. No detector metadata, record schema or epoch change is needed: these
live checks run afresh after every cache hit or miss.

`test_vmu_native_stage_placement.c` passes 790,683 sanitizer checks using the
three real metadata records and boundary/alias mutations. The new relocated
persistent fixture passes 32,697 SA2/Skies and 16,349 actual-PS2-payload checks,
including save/read/reopen, raw-alias refusal and RX/DMA exclusions. The old
persistent regression remains 1,196,760 checks with unchanged polling counts.
`test_vmu_native_heap_execution.py` independently executes the original
initializer, dispatch, bootstrap release and first allocation from each exact
external game image. It reproduces the former Skies/SA2 stack overwrite and
checks real worker copy/zero spans against stage/BSS/new-stack canaries. No
retail instructions are distributed. Native phase-2 reports omit zero-valued
post-allocation rows so the stop message does not wrap over earlier fields.

Run `native-placement-host-tests` with `STARTUP`, `SKIES_STARTUP` and
`SA2_STARTUP`, and `native-lease-alias-host-tests`, in addition to persistent,
passive/read-only and linked-artifact audits. See the detailed observed failure
and contract in `evidence/native-sa2-installer-stack-2026-10-22.json`.
The next console test is SA2 using `--game sa2`, followed by a save/full
power-off/reload and a short PS2 regression. Skies needs an explicit virtual
card result because the old exact-image geometry should also hit this guard.
The linked stack audit executes 384 allocation-wrapper cases, 384 complete
relay cache-hit/miss routes and 576 low-hook cases, rejecting six destructive
mutations. It destroys the retired relay stack during installation and checks
complete caller restoration. The legacy performance artifact still passes its
original stack policy. Publication proofs retain exact compiler variants and
reject three coherent but mismatched register/initialization mutations.

## SA2 persistence acceptance and bounded ordinary polling

The owner tested heap-fix build `c6b7bccdd7f2`: SA2 saves and loads successfully,
but gameplay is choppy with substantial input lag. Skies reloaded successfully.
A later report describes slightly gummy ship controls without establishing
whether those controls behaved the same with virtual VMU disabled. Persistence
acceptance does not establish acceptable gameplay latency or universal native
support.

Both submit and completion still walked the complete allocator free list under
IMASK15. Ordinary polling now checks the eight arena/dispatch/allocated-header
identity words after renewing installed initializer/release/terminal hooks.
The full arm proof plus normal allocation's remove/split behavior and fully
admitted intercepted frees preserve the reserved lease inductively. Full walks
remain at arm, every release and both sides of every external SCI loan. The
identity API explicitly cannot detect arbitrary unrelated free-list corruption;
integration tests show that corruption still prevents storage acquisition and
virtual RX publication. See `native-heap-poll-identity.md` for the exact scope.

ASan/UBSan integration passes 1,197,394 persistent checks, 60,828 lifetime checks,
1,013 full-release differential checks and 37,609 stress checks through 1,023
free nodes and 64 free calls per batch. The two-controller measurement is
387 reads for first preparation, 336 steady, 342 INFO and 758 block READ.
A 64-node poll drops from 596 to 336 reads; four-controller polls remain 358
reads independent of free-list length. Full release costs remain unchanged
and may still contribute latency in games with high free frequency. Reader
counts do not establish console timing or frame rate.

The original SH allocator execution model now also runs 227 allocation/free
operations per exact PS2, Skies and SA2 startup, exercising 32 free nodes,
split/exact-fit/failure/coalescing paths while retaining worker contents and
all eight identity witnesses. No new SH model opcode was needed. Review found
no concrete steady cache-policy change; SA2's entry preserves the owner ICE bit,
so changing cache policy speculatively is not warranted.

Next hardware acceptance is the same SA2 gameplay scene and save/full-power-off/
reload, then the reported ship-control scene and brief Skies/PS2 regressions.
If lag persists, compare the same scene/settings with virtual VMU disabled and
measure the remaining release-hook interrupt scope before claiming parity.

## Matched control and gameplay latency capture

The owner found `26b6d5d10fa6` only marginally better: substantial SA2 lag and
slightly gummy Skies controls remain, with no established no-VMU baseline.
Further instruction-count reductions alone cannot attribute the console
symptom. Review found no extra-frame queue or concrete stale-bank/SR/PR/stack
restoration error. Exact SA2 startup explicitly enables CCR `0x105` before the
heap initializer; its bootstrap's earlier CCR=0 does not establish disabled
caching during gameplay. See `evidence/native-sa2-ccr-2026-10-22.json`.

The compatibility module now contains a canonical typed 32-byte runtime mode:
ordinary 0, control 1 and profile 2. The no-VMU control uses the same low GD reader
and base relay but skips VMU preparation before any profile/recovery/write
callback, then returns before native discovery, heap allocation or hook
installation. It preserves the original return frame and zero low-control
state. It cannot load a virtual save; compare SA2's initial stage/new game.
It isolates the VMU path as a whole, including its 192 KiB reservation (about
4.7% of SA2's four-MiB arena), rather than attributing a difference solely to
hook execution.

`personalize.py --control` and `--profile-runtime` modify only the audited mode
span in addition to the existing profile/seed/envelope spans. They are mutually
exclusive; omitting both restores normal mode. Legacy packages retain default
behavior and reject unsupported explicit modes. C mode tests pass 871 checks,
Python codec 3 tests, personalizer 56 tests and mode artifact 12 mutation tests.
Actual linked proofs execute 768 helper cases, 1728 storage routes, 8064 cache/
control routes, 1152 complete relay routes and 576 low-hook cases. They reject
selector swaps and malformed modes before storage, and preserve legacy audits.
The new strict compiler publication witness retains complete argument/zero/
publication blocks and CFG domination rather than accepting broad opcodes.

Profile mode retains the normal virtual card and samples the existing TMU0
read-only at C submit/completion/free/storage boundaries. Exact existing clock
admission permits 781250 Hz; unsupported/changed profiles, wrap/interval limits
and counter saturation remain explicit. It also samples CCR. Both triggers+B
for 30 consecutive controller replies resets startup statistics and starts a
fresh gameplay window; the report stops after 20s or 1200 completed poll pairs,
after pending clears and storage is idle. Completion includes storage timing;
assembly gateway entry/exit and final accounting are outside each C span.
The user guide describes the sampling overhead and comparison limits.

Profiler core ASan/UBSan passes 1003 checks. Production worker integration
passes 1,260,032 checks, including no normal-mode timer sampling, gesture
debounce/window reset, both capture endings, invalid clocks and idle report
state. Passive/read-only and actual placement/alias/save-reopen regressions
also pass. Ordinary guest-read counts remain first 387, steady 336, INFO 342, READ 758.
Run the focused instructions in `vmu-native-latency-test.md`; these artifacts
provide attribution evidence, not an unmeasured claim that the lag is fixed.

## 23 October: consolidated Evolution diagnostics

Continuation recovered the exact `99cfec5c62fd` bundle and the saved GCC
15.2.0 toolchain; all 1,051 extracted compiler file hashes matched the
checkpoint manifest. The owner reported that build's hooks-only comparison
still froze. It retained copied/filtered Maple DMA, so it did not isolate the
original DMA command list. No root cause or console fix has been established.

The next package is documented in `vmu-native-evolution-trace-test.md`, with
machine-readable evidence in `evidence/native-evolution-trace-2026-10-23.json`.
It adds exact-Evolution frame/timer/external-wait checkpoints, a bounded live
VRAM marker, 16-event ring, excessive-wait watchdog and three cycling terminal
pages. Runtime fault/malloc and existing worker failures share those pages.
It has no arbitrary fault-PC or general interrupt-masked-hang capture.
`--raw-chain` retains active hooks and guards while keeping the original
MDSTAR/command-list bytes and disabling the virtual card/in-game VMU storage.
All previous comparison selections remain in the same binary.

The trace is explicitly title-specific diagnostics, gated by exact immutable
startup SHA/size/load and original windows. It does not introduce a normal
compatibility exception or relax a guard. The source bundle contains no game
bytes. Reproduce its exact-instruction/installer validation using the owner's
original executable:

```sh
export PATH="$PWD/.deps/sh-elf/bin:$PATH"
make -f Makefile.vmu-native-compat BUILD=build/evolution-trace-clean -j4 all
make -f Makefile.vmu-native-compat BUILD=build/evolution-trace-clean native-evolution-trace-host-tests EVOLUTION1_STARTUP=/path/to/Evolution/1ST_READ.BIN
python3 tools/package_vmu_native_persistent.py build/evolution-trace-clean --guide evolution-trace --trace-startup /path/to/Evolution/1ST_READ.BIN
```

Build only after committing for clean embedded provenance. The new full ZIP
contains the complete cloneable source bundle; use its revision rather than
trying to reconstruct an earlier scratch checkout. Further console results
must establish which original-DMA/hook/wait boundary fails before changing
normal VMU behavior again.

### Evolution video result and surface-loss capture

Owner video `63254.mp4` from `62b24dc35d60`, reported raw-chain mode, still
fails before FMV. Independent reads agree on last hook/game fields `1F44`;
sequence digits keep changing until approximately48.63s and disappear about
48.67s. Exact final sequence is unreadable. No root cause or fault PC is
established. Raw-chain failing excludes DMA redirection/virtual replies as
necessary conditions, not every active-hook/IRQ path.

The sampler had a real observability gap: it silently returned on unsupported
or disabled video. After the first accepted surface it now deliberately
reports `E003` on those conditions, retaining current and prior accepted video
registers in a fourth page. A legal transition can produce this diagnostic
stop; it is not a game compatibility fix or general crash watchdog. If no
hook reaches it, a black screen can still remain. Exact game windows and
register gateways are unchanged. See the updated trace guide and
`evidence/native-evolution-video-2026-10-23.json`.
