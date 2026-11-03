# Physical VMU save-backup recovery test

Copy only `KUI/runtime.kui` from this package over the current SD installation.
Back up the previous runtime to the PC first; copy it back to revert this GUI.
Game modules, virtual cards, assignments and settings are not installation files
in this package. This build includes the accepted Games scan and Flow fixes.

## Back up the black VMU

1. Open **VMU tools → Physical VMUs** and choose the black VMU with Left/Right.
2. Press **L** for actions, then select **Back up saves to one file** and press A.
3. Wait for **Verified saves VMD**. B can stop an operation. Recovery may take a
   while when a save needs additional complete reads.
4. Copy the newly created folder from `/KUI/backups/vmu/` to the PC. The image
   is `saves.vmd` (128 KiB); keep `saves.vmd.crc` and `complete.txt` with it.
   Folder names identify the source slot and a new backup number, for example
   `A2-0001`. Existing backups are not overwritten.
5. This image can be opened under **Backups & restore** to view or extract its
   saves. The image also contains the filenames and headers needed to investigate
   unresolved game matches; sending the image and its CRC file is sufficient.

The new image includes all live save files together in a rebuilt, standard
200-block card. It preserves their payloads, filenames, timestamps and file
metadata, relocating allocation pointers. It does not copy deleted/free flash
or the physical card's system layout. Empty cards have no saves to back up;
cards exceeding the standard image capacity or with ambiguous allocation refuse.

The read path is shared with the hardware-tested black-VMU import wizard.
Healthy blocks need two matching replies. Later save disagreements may acquire
the complete save again, requiring three consecutive matching whole-save copies
within six fresh attempts. It does not combine individual bytes or blocks from
different whole-save candidates. This establishes repeated-read consistency,
not which historical bit value was originally written. Transport failures,
changed devices/metadata, cancellation and retained-memory mismatches still stop
the operation. The physical VMU is read only.

**Back up selected save** and the existing per-file **Back up all** action now
share those whole-save checks and recovery as well. Their existing `.vms`,
original `.dir` and `.crc` format is unchanged.

**Back up exact raw image** remains a separate final action. It creates
`card.vmd` by checking all 256 physical blocks, including unused flash, across
two full scans. Unstable unused flash can still make exact raw capture refuse.
No raw capture is silently replaced with a rebuilt save image. Whole-card
restoration is not implemented; individual saves can be extracted for the
existing restore workflow.

## Console acceptance

- Run the new save-image backup on the black VMU, then browse it and check the
  expected save count and Time Stalkers entry.
- Try a selected-save or per-file backup if that was the failing operation.
- If it refuses, save Diagnostics with Y and keep the failed operation's exact
  message. That identifies whether refusal was transport, metadata or save
  consistency; no additional retry-count guess is necessary.

Host tests use simulated VMU replies and real FAT32/exFAT filesystems. They
check recovery, unused-flash isolation, byte-exact logical contents and metadata,
image browsing, refusal paths, interrupted SD publication and existing-backup
preservation. The SH-4 runtime build and menu dispatch are checked separately.
These checks do not establish console acceptance of this new menu path.
