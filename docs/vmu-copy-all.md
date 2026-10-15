# Copy all VMU saves

VMU Manager's Actions page offers **Copy all to VMU**. Select a different
source and destination slot, read the batch preview, then confirm separately.
The source VMU is read-only. Existing destination names are never replaced or
skipped: any conflict refuses the entire batch before writing anything.

Preview reads both cards' complete filesystem metadata and every source save.
It requires healthy, supported VMU filesystems: readable root/FAT/directory,
valid save types and header offsets, in-range allocation chains, no cycles or
shared blocks, no orphaned allocations, unique names, and restorable metadata.
It checks enough free data blocks and directory entries for the entire batch.
An empty source, identical slots, an unavailable card, a name conflict or
insufficient capacity produces an explicit refusal. Preview itself does not
connect or write to SD, and does not write either VMU.

Confirmation is tied to the exact two devices, both metadata snapshots and all
source payload checksums from preview. A change requires a new preview. The
confirmation is consumed once; repeating a failed or stopped request does not
automatically retry it.

Before the first destination write, K-UI creates a new SD backup directory such
as `/KUI/backups/vmu/A1-0002`, using the source slot's prefix. Every source save
is staged as the existing verified `.vms`, `.dir` and `.crc` bundle. Files are
created with unique output paths, synchronized, closed, reopened and compared
byte-for-byte, with CRC checks. Their records also pass the normal restore
metadata parser. All source payloads and the source/destination metadata are
rechecked before destination writes start. SD failure or changed source data
during this stage prevents all VMU writes. Existing SD backups remain intact.

The folder also retains `before-copy-all-source.bin`, containing the original
root/FAT/directory, and a separate `before-copy-NNN-destination.bin` before each
save's destination transaction. These metadata snapshots are diagnostic files,
not whole-card images or an automatic rollback mechanism.

Each destination save uses the existing guarded restore transaction: write only
free blocks, read the payload back, publish FAT/directory changes through KOS,
then reread metadata and every payload byte. The exact original filename,
timestamp, save or mini-game type, copy flag and header offset are preserved.
The source and the expected destination metadata are checked between saves.
Original source payloads are reread before their copies and all source payloads
are checked again before final completion. Memory is bounded by one normal
VMU's source data, one largest-save buffer and fixed filesystem metadata.

Stop is honored during reads, SD staging and free-block writes. Once one save's
metadata commit begins, Stop is deferred through that save's final verification.
The batch then stops before another save. Already verified copied saves and all
verified SD bundles are retained. A later failure does not remove completed
copies. A wholly successful batch publishes `complete.txt`; individual staged
bundles are usable through the existing separately confirmed restore workflow
whether or not that final marker exists.

The result reports three values:

- **Verified:** destination saves whose complete transaction and readback passed.
- **Remaining:** total planned saves minus verified saves. This includes an
  uncertain current item if one exists.
- **Unverified:** zero or one current save whose metadata commit began but could
  not be verified. The operation stops immediately after such a failure, without
  retrying or rolling back. Retain the SD backup and inspect the destination
  before further writes.

The byte progress counter includes only fully verified copied saves. A failure
after writing free-block data but before metadata publication does not count a
completed save. A failed physical FAT/directory write can damage filesystem
metadata, including entries sharing a physical block; neither per-save copy nor
bulk copy is power-loss atomic. Keep both VMUs connected and leave power on
through each commit. Full-card restore, formatting, overwrite and bank switching
are not part of this feature.

## Acceptance

1. Use two VMUs and a disposable source save. Preview Copy all and cancel once;
   neither card should change.
2. Confirm with sufficient free space and different names. Check every copied
   save in the BIOS or its game; verify the source and prior destination saves
   remain present.
3. Repeat preview to that destination. It should report conflicts and refuse
   the whole batch without writing either card.
4. With several disposable source saves and a fresh destination, stop after one
   verified copy. Check the reported verified/remaining counts and the retained
   copies and SD bundles. Do not unplug hardware to test failure handling.
5. Keep the diagnostic report and SD backup directory if any operation reports
   an unverified save.

Host checks use real FAT32 and exFAT filesystems with simulated VMU transport,
including complete staging before the first destination write, metadata and
payload pinning, conflicts/capacity/directory admission, changed or removed
cards, cancellation, SD failures, payload errors, metadata tearing, final
readback failure and exact partial-result counts. Console acceptance remains
pending. All implementation code is K-UI code using the existing independently
authored adapter and pinned upstream KallistiOS APIs; no DreamShell source is
read or reused.
