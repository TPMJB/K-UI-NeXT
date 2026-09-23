# VMU backup and restore

VMU Manager now browses its SD backups and restores a selected save into free
space on a selected VMU. Restore always previews the source name, block count,
and destination slot before a separate confirmation. An existing filename is
refused. Restore does not overwrite, format, or automatically repair a VMU. The separate
managed delete/copy actions below require their own preview and confirmation.

A new backup consists of three files:

- `.vms`: the complete block-padded save payload.
- `.dir`: its original 32-byte VMU directory entry, including name, timestamp,
  copy-protection flag, type, and header offset. This is not a VMI file.
- `.crc`: a 32-byte record binding the payload length, payload CRC32, and
  directory-entry CRC32; the record has its own CRC32.

Each file is synchronized, closed, reopened, and checked before publication.
The `.crc` record is published last. A completed file in an interrupted
multi-save backup can therefore be restored without relying on the folder's
`complete.txt` marker. The browser shows candidates with a CRC record; preview
validates the complete source before offering confirmation. Older backups
without `.crc` remain untouched, but cannot be restored through this workflow;
make a new backup to obtain a recorded source checksum.

Before writing, restore checks the destination root, directory, allocation
chains, unowned blocks, duplicate names, free directory entries and free blocks.
The exact source CRC record and destination device/metadata must still match
its preview. Original names use VMU/KOS's 12-byte, NUL-aware comparison; aliases
cannot bypass the no-overwrite check.

The destination's root/FAT/directory are saved and reread on SD as
`/KUI/backups/vmu/A1-NNNN/before-restore.bin` (with the selected slot in place of
A1) before any VMU write. This is a diagnostic metadata backup, not a whole-card
image or an automatic rollback mechanism. The source backup remains intact.

Only free data blocks are written. Each block boundary checks that the selected
VMU is still present and checks Stop. Original save data/metadata are retained;
data files allocate downward and VMU mini-games upward, following upstream KOS
conventions. All new payload bytes are reread before publishing allocation and
directory changes through upstream KOS's filesystem APIs. The final allocation,
directory, root and every payload byte are reread before success is reported.
Stop is deferred during the short metadata commit and its final check.

## Hardware limits

**Keep the VMU connected and leave power on during a restore.** VMU flash
metadata updates are not atomic. An interrupted or failed FAT/directory write
can leave leaked blocks or a damaged directory, including entries in the same
physical block. Such an outcome is reported as unverified; the app does not
blindly retry or roll back. The metadata backup is kept for diagnosis. Per-block
device checks cannot authenticate a replacement VMU during one physical block
transaction, and the Maple API cannot distinguish every identical replacement.

Multi-bank third-party cards must expose a consistent normal VMU filesystem;
never switch banks during an operation. Overwriting and formatting are not
implemented. The managed copy/delete actions do not silently replace saves or
repair a damaged card.

## Managed delete and VMU-to-VMU copy

VMU Manager's Actions page offers **Back up + delete** and **Copy to VMU** for
its selected save. Opening an action first reads a preview; a separate
confirmation starts it. Preview itself performs no SD or VMU writes. The exact
source slot, listed row, device, root/FAT/directory fingerprint and complete
source payload CRC32 are pinned. Copy also pins the destination device and
metadata. Any change requires a fresh preview. Both operations refuse duplicate
names or unowned allocated blocks in the source; copy applies the existing
restore destination checks and never overwrites an existing name.

Delete requires an SD card. Before changing the VMU, it creates and rereads a
new `.vms`, `.dir`, and `.crc` backup plus `before-delete.bin`, an exact snapshot
of the source root/FAT/directory. The backup remains selectable in Restore.
The directory entry is removed and the entire directory reread before freeing
only that save's allocation chain. Final root/FAT/directory readback must match
exactly. This sequence prefers leaked blocks after an interrupted deletion over
a live save pointing into newly free blocks, but it cannot make physical flash
metadata updates power-loss atomic. A failed or uncertain write stops without
retry or automatic rollback. The source's old payload blocks are not securely
erased; the operation removes the save from the filesystem.

Copy creates the same verified SD save backup, with `before-copy-source.bin`
and `before-copy-destination.bin`, then uses the normal restore transaction on
the destination's free blocks. It verifies every payload byte before and after
metadata publication. The source is read-only and its SD backup is retained.
A source/destination change, missing card, source checksum change or failed SD
backup prevents VMU writes. Stop is honored during reading, backup and free-block
writing; it is deferred through the short metadata commit and final readback.

The UI does not expose a bulk erase or format operation. To test restore with
one VMU, choose a disposable save, confirm Back up + delete, then restore the
new verified backup into the now-free name. A second VMU supports copy testing
without removing the source.

## Acceptance check

Use the existing boot disc and updated SD runtime. A spare VMU with a disposable
save is preferred; no extra reader is required. A second VMU makes it easy to
restore to an empty destination without deleting any source save.

1. List a source VMU, back up a disposable save, and confirm the SD backup is
   reported verified. Keep the `.vms`, `.dir`, and `.crc` together.
2. Open Restore backups, select the new backup and destination slot. Confirm
   preview shows the expected name, blocks and destination. Cancel once and
   confirm the VMU is unchanged.
3. Preview again, confirm, and keep the VMU connected until every-byte
   verification succeeds. Check the save from the BIOS/game and back it up
   again; the new `.vms` bytes must equal the source backup.
4. Try restoring the same source to the same destination again. It must refuse
   the existing name without any VMU write.
5. With a disposable save selected, open Actions and preview Back up + delete.
   Cancel once; the save must remain. Preview again, confirm, and check that it
   disappears only after the verified backup is reported. Restore from that
   backup and compare its bytes through another backup or the BIOS/game.
6. With two VMUs, preview Copy to VMU and confirm the source/destination slots.
   Check the copied save and unchanged source. Repeat to the same destination:
   the existing name must be refused without writing.
7. Save the diagnostic report with source/destination slot and save name.

Host tests cover FAT32/exFAT backup publication, source damage/truncation,
preview invalidation, existing-name aliases, damaged destination allocation,
insufficient capacity, cancellation/removal, failed payload/metadata writes,
failed readback and failed SD metadata backup. Managed-action tests additionally
exercise delete/copy preview pinning, changed source bytes and cards, orphaned
blocks, duplicate names, SD backup failures, metadata tearing/readback failures,
Stop during commit, missing destination and a complete delete/restore round trip.
These are transport simulations; **physical VMU restore/copy/delete acceptance
remains pending**. No hardware unplug test is
needed for this first acceptance pass.

## Source/API provenance

The adapter is K-UI code. Filesystem layout, allocation order, mutex ownership
and metadata commit calls were checked against pinned upstream KallistiOS
`fcfa7d869471591ca1c777543261a7bfea7cb726`:

- `kernel/arch/dreamcast/include/dc/vmufs.h`
- `kernel/arch/dreamcast/fs/vmufs.c`

No DreamShell filesystem implementation is imported. The high-level KOS
whole-file writer is deliberately not used because it lacks the per-block
device/cancellation checks required by this UI.
