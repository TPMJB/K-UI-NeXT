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
