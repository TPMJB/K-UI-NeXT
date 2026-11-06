# Smash collection card lifetime

## Current status: 9f video-provenance refusal

Build `9f470e548ad2` reached Start Game / Resume in about 18 seconds according to
the owner, then failed native return with `RET B4C2CD76`, detail `00000000`.
That reason matches `SMASH TIMING ORIGINAL VIDEO REFUSED`: returned outer tried
the KOS-only VID1 capture against game PVR state. Original KOS framebuffer zero
passes the reproduced guard; nonzero/blanked/320×240 game states fail it. See
[the evidence record](evidence/smash-native-return-9f470e548ad2.json). The older
5ca `105` photograph remains a broad progress observation, not this diagnosis.

The current follow-up retains the 9f return/outer markers and v2 epoch, carrying
the original strict 80-byte VID1 through the return runtime instead of capturing
game PVR as a KOS seed. The diagnostic runtime is 596 bytes rather than516,
with the whole record CRC-covered. A blank outer VID1 slot follows the368-byte
return seed; validated bytes are published only after the outer capsule CRC
passes. Initial GUI capture and video-mode validation remain unchanged. No
worker/reader-code change, synthesized timing or relaxed video mode is involved.
The follow-up has no console return or speed acceptance yet.

The [marker map](smash-card-session-build.md#return-progress-interpretation)
retains105→10A and return-only outer110→116; initial GUI launch has no extra
outer markers. Outer errors draw `CRC32(message) | 0x80000000` and the original
detail on the current framebuffer before the old renderer, then park.

## Version 2 allocation epoch retained

The c375 hardware capture bypassed the full semantic filesystem walk, but its
replacement metadata replay/identity interval still took 4.685 seconds. The
owner observed about 22 seconds to Shining Force's next usable screen and a
failed native menu return. Those are historical c375 results, not timings or
acceptance for this candidate. See [accepted progress](accepted-progress.md).

The collection already selected one backing card. The ordinary worker already
retains admitted image/journal extent maps and current block CRCs during its
own gameplay lifetime. The v2 change retains the **authority established by
complete allocation admission across collection owners**. Each new owner can
build a fresh backend without another filesystem walk or metadata-witness
replay. It does not copy a started backend, callbacks or an old save snapshot.

Initial outer entry performs complete filesystem admission after GUI filesystem
writes finish. Only successful admission and fresh card identity checks can
mint the 96-byte live receipt. It binds the collection build/generation,
canonical ownership profile, card identity, admission attestation, current owner
and sequence. The attestation records successful live admission; it is not a
fresh replay hash of all metadata on each attach.

The receipt travels only through checked raw handoffs. Legal transitions are
outer to a child, and native child back to outer after worker retirement.
Initial outer sequence 1 becomes native 2, returned outer 3, then next native 4.
Blank, damaged, stale-version, wrong-owner or mismatched child receipts refuse.
A new K-UI launch starts blank and requires new admission; persisted bytes
alone cannot create a live allocation epoch.

This contract depends on known owners that do not allocate or modify host
filesystem metadata during the epoch. A filesystem writer, different owner or
unqualified return cannot reuse it. Every child's game/audio extents are still
checked against complete backing-file allocations. SCI is acquired/released by
each owner; there is no continuously acquired SCI lease. The old raw gate or
worker certifies quiescence and prior health before transfer; the new owner
checks current card identity with its own local storage adapter.

## Fresh save state on each owner

`kui_vmu_persist_stage_rebind` consumes a one-use allocation-transfer descriptor
bound to the live receipt, card geometry/CID, image and ownership profile.
Mutation, reuse or failure poisons/refuses the transfer. Guard callbacks and
that descriptor remain temporary high-stage state.

The child still recovers the real journal, validates/hashes the current VMU
image, checkpoints, verifies the zero marker and final CID/health, and seals a
fresh 1,272-byte durable handoff. Ordinary SD block CRC, sync and readback remain.
Preparation hashes and old worker block CRCs do not authorize later saved data.
The worker reservation and backend ABI are unchanged.

The GUI currently still prepares the 295,424-byte `smash-card-session.bin`
allocation and binds its maps for configuration/cohort compatibility. The v2
runtime does not capture, load, store or replay its witness contents. The older
witness/attach/file-I/O facilities remain separately tested regression paths.

## Native GIN continuation and return

`SMASH_NATIVE_GIN_ADOPT=1` preserves the original GIN-loaded MDE instead of
loading it again and rerunning BOOT2. Its prefix verifier authenticates the
complete original 1,848,288-byte source with SHA256/CRC, checks the original GIN
frame/context and normalizes only source-owned argument fields for the existing
SDK/allocator proof. It restores that context before the original continuation.
Accepted allocator hooks, cache-domain checks and 72 KiB worker geometry remain.

The native A+B+X+Y+Start edge opens the original emulator options menu. After
the RPG's own save, use its **Save Game** option to commit cartridge SRAM to
the VMU, and wait for success/SD idle. Explicit **Return to Menu** then reaches
the checked outer-return gate, retaining the original dirty-save confirmation.
The former direct-combo shortcut bypassed required save UI and is superseded.
Return cannot synthesize an unfinished save or VMU SYNC. The gate
must drain/quiesce and retire the worker before verified outer reload. Busy or
refused transitions cannot resume the old cleanup caller. The 5ca hardware
return still failed; the current visibility changes seek the stopping boundary
and have no new console acceptance.

All ten original Genesis selections retain their source-derived context checks.
This does not create SRAM/high-score support where the original engine declares
none. Phantasy Star II and Shining Force declare 8,192 SRAM bytes each; other
Genesis high-score persistence remains unproved.

## Diagnostics and acceptance

All four roles must match: outer, native, Virtua Cop 2 and Swirl. The GUI rejects
mixed capabilities before mutation and checks allocation disjointness before
sealing capsules. Legacy all-absent modules retain their old route. See
[the build procedure](smash-card-session-build.md).

The 4,096-byte TXT labels counters THIS_OWNER. Native rebind should report
full_admissions 0, attaches 1, allocation_epoch 1, witness sectors/runs 0,
native_gin_route 1 and boot2_calls 0. Generation/sequence identify the live
handoff; these are not aggregate history counters. Zero witness traffic does
not mean zero card traffic: CID and current-save checks still run. Uncollected
SD/CPU split timing stays NA. cpu_other is elapsed time outside SD callbacks,
not CPU utilization.

Prefix verification is phase 5→10, SD initialization 10→11, and allocation-epoch
validation 12→13. Total starts at the first MDE payload callback and ends at
worker installation. Initial pre-callback wait, later SGA/BUP/options work and
report I/O remain outside it. No new console timing is claimed.

Host/linked-SH checks cover rebind guards, current save integrity, receipt
roles/transport, original-source SHA/CRC, all ten contexts, direct-combo
publication and unchanged stack guards. They do not establish real button
handling, saves, native menu return or console speed. The next test is save →
native return → second RPG save → native return → first RPG resume.

Canonical CE helpers remain unchanged. First CE entry uses the allocation
epoch, but CE-to-outer transfer is not admitted: Sega Swirl and Virtua Cop 2 menu
return remain unresolved. Current Main engine 3 updater sources are unchanged.
