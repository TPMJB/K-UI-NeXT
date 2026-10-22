# VMU reset checkpoint — 2026-10-20

## Confirmed on hardware

- Silver p0003 imported saves and new progress survive reboot.
- p0004 contains the 18-save backup (initially196 used/4 free), and the owner confirms Toy Commander sees the imported AAA save, creates new saves, and loads those new saves after reboot.
- Dedicated Toy p0005: on2026-10-20 the owner reports the test worked, created a new profile, fully rebooted, and confirmed that profile persisted. The updated image was not uploaded for independent inspection.
- Current p0004 and p0003 contain progress absent from earlier uploaded images. Preserve both complete image/journal/identity pairs. Do not overwrite either from a ZIP or derive split cards from stale uploads.
- Current p0005 also contains newly saved progress. Preserve its image/journal/identity together; do not overwrite it with the original Toy split output.

## Dedicated Toy card: restart persistence confirmed

The owner ran `tools/prepare-toy-card.py` against the current p0004. Its report
lists19 files/198 used/2 free in that source and selects all three Toy files:
`TOY_COMM.___` (8 blocks), `TOY_COMM.AAA` (2), and `TOY_COMM.ZZZ` (2). The output
has12 used/188 free, preserves source directory/payload bytes, and has SHA256
`f538ffe476a2561df4c85036ccb2d0e066c3a49e5deea722f771bc4e064126af`.
The owner imported this as `/KUI/vmu-test/p0005` and supplied its canonical P2
profile, SHA256 `45dabd313fcf60ddf8ee5f06719e89b5f7e456bf3e1e1cee2dcb9f1bfe86d872`.
The profile's base image digest matches the report; matching p0005 image bytes
were not uploaded, so no independent full-image/block-CRC verification is claimed.

The previously audited bf91 Toy loader was personalized for p0005 by replacing
only its read-only4096-byte P2 record and two runtime-envelope CRC words. The
unmodified aefd native package auditor passes; worker, resident and executable
sections are unchanged. Install module SHA256:
`0be2b1ed0c256b509f8eb4f94467f1b00b7a69aba4707e4df476728d8cebac32`.
`K-UI-Toy-p0005-Dedicated-Card-Test.zip` temporarily installs that module as
`KUI/apps/games/retail-boot.kui`. The owner confirms creating a new profile and
loading it after a full reboot. Individual AAA/ZZZ load results were not
separately reported. Restore the ordinary loader after the test. Its inherited
five-minute card/ten-minute overall diagnostic bounds remain; this result does
not establish unrestricted play or compatibility with other titles. Preserve
p0005 and all earlier pairs.

## Accepted goal

One K-UI save collection with an automatically selected standard200-block card for each game/save set; keep companion progress/settings/system/guild files together. Permit separate playthrough cards. No global200-block limit, and no account/login requirement. See docs/vmu-save-collection-design.md.

## Manager transfer/browser integration — console test pending

The owner requested copying saves from three physical cards into a manager-visible
virtual collection, and asked about using it with the original-disc GD launcher.
The manager now has a ninth copy destination, Virtual VMU (new card), for selected
or all-save preview/confirm, plus Browse virtual cards. A new immutable v1
catalog stores verified standard200-block save snapshots at `/KUI/vmu/cards/vNNNN`.
Existing pNNNN pairs and physical cards are never written by these new routes.
All-save capture keeps the complete source save group, selected keeps exactly
one file; free flash is not read. See docs/vmu-manager-collection-test.md.

The complete SH runtime now builds: pinned KOS headers/scripts, retained matching
libs/compiler and FatFs were restored. This also compiles the previously
source-only optional exact-Toy Games routing, but no new capability-bearing Toy
resident module is installed by the manager-only test. The old bf91 Toy module
must still not be renamed to vmu-toy.kui. Existing game modules remain unchanged.

Catalog v1 is deliberately an immutable management snapshot contract. It does
not yet activate cards for game use, enroll/recover current pNNNN pairs, merge
several selected copies into an existing card, infer per-game groups, or supply
automatic game bindings. Next: prove manager copying/browsing on console, then
connect activation/live profile handoff and mutable catalog admission to the
existing exact-title resident modules. Broad interception remains separate.

Original-disc GD Play currently performs a BIOS reboot and retains no service.
The source assessment in docs/vmu-optical-launch-assessment.md describes the
separate controlled optical handoff, retained SD owner, DMA/memory ownership and
passive/read-only/persistent test sequence needed. No optical support is claimed.

The first manager test8e5fc15 failed at physical source snapshot validation
before SD creation. Exact source-specific cause is unknown because the message
collapsed image, change and read failures. The revised manager imports checked
logical files into a canonical new card using `vmu_image_pack.c`; it does not
transplant physical root decoration, system FAT or inactive entries. Raw source
geometry, file chains, unique ownership, no-orphan checks and repeated payload/
metadata comparisons remain. Read/layout/entry/capacity refusals are concrete.
The strict active-image validator and raw backup paths are unchanged.

The owner also requested a scrollbar and recognizable save descriptions. The
revised UI scrolls across pages with Up/Down and retains START paging, displays
a range/scrollbar, reads optional VMS descriptions through logical header-block
offsets, and keeps exact filenames visible. Descriptions are display-only;
missing/malformed headers fall back to filenames. A new owner requirement is
duplicate-name save sets (likely PSO V1 on two cards): retain separate virtual
cards, unchanged original names and complete companion groups; provide a card
selector during future game activation rather than merging/renaming either.

## Work completed in this checkpoint

- Extended launch capability/map admission from exact Silver to exact Toy plus Silver, preserving the Silver API and capability constants.
- Added optional normal Games route for KUI/apps/games/vmu-toy.kui beside vmu-silver.kui. Existing unrelated games retain ordinary routing; present malformed or mismatched modules refuse.
- Toy candidate is the established original GDI CRC; its product ID was not known and was not invented. Full track layout, options, IP/EXEC fingerprints and matching linked capability still gate handoff.
- Expanded independent launch tests for Toy success, every capability-byte mutation, swapped valid modules, missing module, settings and altered manifests. make build/test-vmu-launch-package and its ASAN/UBSAN executable pass.

The frontend and complete runtime now compile with the routing change, and shell
host tests pass. A capability-bearing Toy resident module still needs a fresh
native build/audit and p0005 profile personalization. The older bf91 test loader
has no normal-launch capability and must not simply be renamed to vmu-toy.kui.

## Next implementation milestones

1. Finish integration/native checks and package optional Toy normal-launch routing, leaving the working Silver module bound to p0003.
2. Add checked runtime card-profile selection/handoff, automatic per-game/save-set creation, catalog and VMU Manager presentation. Prepare each card's identity/journal/ownership profile before any game use; storage placement cannot be inferred from a name.
3. Migrate complete verified groups from current card pairs into fresh per-game cards while preserving source pairs. Define stable bindings for original/2048/compressed variants and multi-disc games; do not guess region compatibility.
4. Generalize interception beyond the two exact games. Current stage/worker/assembly contain title-specific submission/completion, heap lease, manager/lock, RX/TX and instruction witnesses. Removing title checks does not make these addresses generic. Existing tools/vmu_retail_probe.py inventories bounded MMIO candidates; new adapters need control-flow/ownership analysis and passive hardware checks before save writes.

The durable protocol/journal/storage backend is reusable and proven for two titles. Automatic card management is bounded implementation work; broad hooks/compatibility remain the largest uncertainty and require multiple development/console-test cycles.

## Recovery/provenance

This checkout was restored from retained source snapshots and patch chains through stable Silver3c80 and normal routing50e. Its commit history is synthetic recovery history, not the original commit IDs; RECOVERY-PROVENANCE.json records the two omitted historical documentation-only hunks. The snapshot in this package contains the complete current tracked source, so the next session need not repeat that recovery. Private P2 profiles remain outside Git.

