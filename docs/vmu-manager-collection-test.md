# VMU Manager virtual-card transfer test

This update adds a Virtual VMU copy destination and a managed-card browser.
It creates a fresh standard 200-block card for each selected-save or copy-all
operation. Copy-all preserves every admitted save and companion file together;
selected copy keeps exactly one file. Existing physical and virtual cards stay
unchanged. These stored cards require a later game-activation step; this update
does not add automatic game selection or support more game interception hooks.

## Console test

1. With the console off, back up your current `KUI/runtime.kui` outside KUI.
   Install only this package's `KUI/runtime.kui`. Keep the ordinary Games loader
   restored from before the temporary p0005 Toy test. Keep the working Silver
   module and all pNNNN folders.
2. Connect the physical card that failed to a controller. Open VMU Manager, select
   its slot with Left/Right, and press A to refresh. Note the total saves.
3. Press L for Save / card actions. Select **Copy all saves**, then use
   Left/Right to choose **Virtual VMU (new card)**. Press A for preview,
   check its save count, and press A to confirm. Keep the VMU and SD connected
   until the verified Stored result appears.
4. Select **Browse virtual cards**. Open the new v0001-or-later card with A.
   Check the total and filenames; Up/Down scroll across pages and START still
   advances pages. The scrollbar and visible range show that more saves exist.
   Embedded save descriptions appear when available; the selected save's exact
   filename remains visible. Opening/browsing does
   not need the physical VMU. X/Y in this image browser extract selected/all
   saves into ordinary verified SD backup bundles.
5. Fully power off, remove the physical VMU, and restart. Open VMU Manager,
   press L, choose Browse virtual cards, and check the copied card and saves
   are still listed. Report the physical and virtual save counts and any error.
6. Optionally repeat with the other two physical cards. Each transfer creates
   its own new card. There is no shared 200-block limit across the collection.

The same destination works with Copy selected. That deliberately copies only
the selected file; use Copy all when companion-file relationships are unknown.
Two physical cards containing the same filename can both be imported: each
transfer creates its own virtual card, preserving both original names and save
sets. They cannot coexist under the same name on one standard VMU filesystem.
Game activation will need a card selector for separate playthrough/save sets.
No step deletes the original save. Stop is deferred once final card publication
starts, through the short final verification. A storage error can leave a
partial folder or a completed card; refresh the collection before retrying.

To undo the UI update, restore your backed-up runtime. Keep newly copied card
folders if you want to retain their saves.

## Storage and admission

New cards live under `/KUI/vmu/cards/v0001` through `v9999`. Publication creates
`card.vmd`, `card.journal` (1024 zero bytes), `identity.bin` (16 bytes),
`card.vmd.crc`, and a final 512-byte `ready.bin`. Each file uses CREATE_NEW and
is synced, closed, reopened, and checked. The readiness record is written last.
Failed folders are never overwritten or automatically deleted. The browser
omits incomplete or changed cards and reports storage I/O failures.

The physical source is read-only. The importer follows its root-specified FAT,
directory and user area, checking live entries, bounded exact chains, duplicate
ownership and orphan allocations. It rereads and compares all copied payloads
and metadata, and SHA256 pins the resulting image to the confirmed source/device.
It builds a separate standard card from the logical files. New directory
locations and first-block pointers reflect new allocation; remaining live entry
bytes, protection and complete allocated payloads are preserved. No game CRC is
repaired. Physical root decoration, unused system bytes and inactive directory
slots are not transplanted, and free/unrelated reserved flash is never read.
Raw-card backup and strict virtual-image admission are unchanged.

The first8e5fc15 console test reported a generic standard200-block validation
failure before SD publication. That message did not identify which condition
failed; the exact source-specific cause is unproven without its raw metadata.
The revised import avoids requiring a physical card to be a canonical virtual
image, and preserves concrete read/layout/entry/capacity errors when it refuses.

Save descriptions are display-only VMS-header metadata. The reader follows the
file's logical header-block offset through its allocation chain. Implausible or
unreadable headers fall back to the filename and do not block listing/copying.
Unsupported text becomes '?' with the current font; no save text is rewritten.

Version1 catalog cards are immutable management snapshots, not active game
pairs. The browser checks complete image SHA/CRC, standard filesystem, exact
zero journal, identity, metadata lengths and the marker before displaying them.
Game activation must derive a live allocation/ownership profile and update the
catalog contract to follow legitimate game changes. Existing p0003/p0005 stay
under their tested resident loaders and are not silently enrolled or rebound.

## Verification

The standalone test `make -f Makefile.vmu-collection-test test-vmu-collection`
uses real FatFs with an independent write-back sector device. It checks strict
paths, no overwrites, publication ordering, corruption/lengths, races,
cancellation and every write/sync failure boundary. The regular shell and VMU
adapter suites check UI/stale results, allocated-only physical capture, selected
page mapping, all-save copies, physical write absence and legacy operations.
Native SH cross compilation checks the complete runtime, including its worker
and renderer. Console behavior remains the next acceptance check.

Initial2026-10-20 host results: all270 VMU adapter modes passed on each of FAT32
and exFAT (540 total), including24 collection modes per filesystem. Full shell
controls/rendering pass, with exact canonical collection-path tests. The
standalone backend passes316 write/sync interruption boundaries, semantic and
byte corruption, incomplete folders, races, cancellation and read failures.
Canonical image/harness-save/import/extract/restore compatibility also passes
on both filesystems with clean fsck checks.

After the source-copy revision,285 adapter modes pass on each filesystem
(570 total), including33 collection cases and6 description cases per filesystem.
Cases cover noncanonical source-only metadata, alternate physical layout,
sort flag,240-user-block source, stale inactive padding, source capacity and
active-entry refusals, plus unchanged legacy operations. Pure pack tests cover
exact payload preservation, mini-game allocation, capacity and alias rejection.
Description tests cover bounded text, malformed headers and fragmented header
locations. Scrollbar/UI console acceptance remains pending.

Original optical-disc use needs a separate resident launch path. See
`docs/vmu-optical-launch-assessment.md`; the current GD Play reboot does not
retain the virtual-card service.
