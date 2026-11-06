# Shared startup policy and Smash Pack integration

The performance requirement is shared behavior: the next supported game must
inherit batched reads, complete block validation and fast cold verification
without adding a title-specific switch or recipe.

The shared image reader batches consecutive physical sectors. GUI executable
checks use 32 KiB logical batches, including exact final-byte CRC/SHA checks.
Cold stages use the byte CRC implementation and run their verification code
with the instruction cache enabled; the operand cache stays off until the
original owner's cache state is restored. These defaults do not depend on VMU,
Background, SDK family or title. Native/disc resident and VMU-worker CRC code
retains its own existing contract. Every transferred SD block retains CRC16.
CRC reconstruction after a lost DMA byte does not establish that the rest of
the received block is correct. Quarantine such blocks before cursor/payload
delivery and retain the shared bounded same-LBA retry/PIO fallback; do not
reintroduce reconstructed-byte acceptance in another title or loader.

Measure complete user-visible phases: menu selection to bootstrap, bootstrap
to the collection menu, and collection selection to Start/Resume. Separate
VMU-on/off results and cold launch from launch after a return. Command and
model instruction counts explain work; they do not prove elapsed-time gains.
A shared-source change becomes an installed global fix only when the affected
routed modules have been rebuilt and their actual packaged bytes qualified.

`tests/test_global_launch_performance_policy.py` checks effective make flags and
preprocessed policy across ordinary native/CE, compressed, foreground,
standalone, Standard VMU, compact Background, Smash and disc families, including
native and CE CDDA configurations. Explicitly disabling the old optional CRC
flag must not disable the shared fast implementation. Linked instruction,
CRC, stack and filesystem tests provide separate implementation evidence.

## Installing the policy

Each installed loader embeds its own cold stage. Rebuilding only the GUI or
Standard VMU cannot update an installed ordinary `retail-physical.kui`, CE,
compressed or CDDA loader. The earlier eight-file performance ZIP omitted
those routes. `tools/global_loader_rollout.py` explicitly collects the affected
normal roles and checks their linked policy and source receipts. Native CDDA
admission includes generated exact identities for rebuilt compatibility
modules while retaining existing exact accepted identities and presence rules.

The separate Standard 72 KiB service and compact Background 72 KiB worker /
284 KiB reservation remain distinct. This work does not establish a new
prefetch-only allocation or change the accepted playing-time CDDA policy.
The GUI retains the current updater source and its full-release packaging
block. An unrelated updater-only package must continue inheriting readers.

Keep optional services independent in routing and memory ownership. VMU-only,
reader/prefetch-only, combined and neither are separate requirements; a disabled
toggle is not proof that its reservation disappeared. Preserve accepted
component geometry and the exact immutable helper prefix. If a prepared helper
gains a typed transport tail, identify that deliberate variant and its new
complete hash; do not describe the old zero-padded file as unchanged.

## Smash Pack

The original menu selects independently built native, Virtua Cop 2 and Sega
Swirl cold backends. One shared card selection and history preparation supplies
all three. The native engine retains original arguments and the existing
Shining Force / Phantasy Star II return path. Both CE children retain their
separate kernel/helper identity, virtual slot and original SAMPLE folder.
Background and prefetch remain off for this collection route.

All six generated capsule allocations are checked before truncation, including
cluster slack, disc extents and card backing files. New allocations are checked
against all peers before writing, then synced, reopened, remapped and read back.
Unchanged CE helper code can be reused; the selected cold stage always replaces
and verifies its launch-local configuration tail. On-disk outer return seeds
remain blank, avoiding a self-referential checksum. The launch-only return plan
is published after the complete outer capsule is verified.

The collection allocation epoch avoids repeating a full host-filesystem
ownership scan at every internal selection. Initial admission remains fresh;
each legal transfer still checks current card identity, journal recovery,
current VMU image/block checksums and its checkpoint/handoff. An on-disk receipt
alone never grants ownership. A launch-local runtime transported through a
generated capsule requires a fresh RAM seal after bounded overwrite, sync and
exact readback, in addition to static code admission. Reuse compares every
static byte and ignores only the declared runtime window; a runtime-only
difference must not trigger a full capsule rewrite or filesystem rescan.

Return is part of the loader lifecycle. Reaching E0 does not prove VMU, reader,
DMA or Maple retirement. The CE return candidate must drain through the
original schedulable owner, establish complete idle, and publish a terminal
replacement only after source-verified retirement. Busy work retains its
original stack, SR, MMU and continuation; a masked exit hook must not call the
CE worker directly. Preserve exact ROOT context and original video provenance.
The owner accepted the complete collection flow on2026-11-06 usingcc81e63ca4d2,
including repeated VC2 launches after all other games in one session. Preserve
that accepted return/worker/read-recovery baseline in `accepted-progress.md`.

Before delivery, bind clean source/compile receipts and hashes to every actual
GUI route, physical module and embedded copy. Compare embedded and separately
installed module bytes, preserve deliberate absence and current Main updater
parity, and test the current producer/consumer when producing an update.
Helper hashes alone do not establish loader equivalence: raw hydration readers,
entry stubs, stage options and return state can change independently.

Unrelated UI navigation must not synchronously read SD settings or probe a
device. Open hubs immediately and defer work to the action that needs it;
lazy settings reads must complete before a toggle can write defaults.

Host and linked proofs do not measure console elapsed time. Future performance
work, when requested, should compare complete launch phases under the same
settings. Smash Pack is accepted complete; VC2's remaining load latency is an
optional follow-up and does not require another build or console test now.
Current Smash completion is scoped to Shining Force, Phantasy Star II,
Virtua Cop 2 and Sega Swirl. The owner reports that Google says the other
eight Genesis games have no save support; this is an unverified owner report,
not a source audit. Do not keep their persistence as a blocking task or start
more title-specific tuning/research without contrary evidence or a new request.
Quake III has only its previously reported all-services-off boot result.

Shared batching, validation, cache scopes and the collection epoch are
implemented mechanisms with their own evidence. Their existence does not
complete universal rollout, qualify every SDK, or establish all-game launch,
save and return behavior. Keep unfinished route coverage and hardware results
explicit in the existing acceptance record instead of adding per-title
performance exceptions.
