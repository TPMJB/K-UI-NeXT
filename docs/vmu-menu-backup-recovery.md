# VMU labels, save details and remembered matches test

Copy only `KUI/runtime.kui` from this package over the current SD installation.
Back up the previous runtime to the PC first; copy it back to revert this GUI.
Game modules, virtual cards, assignments and settings are not installation files
in this package. This build includes the accepted Games scan and Flow fixes.

The owner confirmed the previous `63dc87c39829` backup fix worked on the black
VMU. This follow-up keeps that read path and repairs the crowded VMU tools menu:
six single-line choices, right-aligned settings and help for the selected row.

## Identify and review imported saves

The unresolved-save list now favors informative game names and long
descriptions over generic short labels. Recognition includes additional
filename/header patterns observed in the supplied save image: Sonic Adventure,
Dead or Alive 2, Tony Hawk's Pro Skater 2, Worms Armageddon and Prince of Persia
3D. Matching still asks about ambiguous releases and keeps unrelated sequels
separate. Recognition does not add native virtual-VMU support to an ineligible
game or loader.

Identification uses K-UI's own save catalogue and works with no ripped games,
including an empty or missing `/Games` folder. A title recognized from its save
remains named when its release is absent from the installed game catalogue.
Browser bookmarks are retained as data instead of being forced onto a game.
The supplied image itself is not included in this test package or source.

Recognition and a playable card's release assignment are separate. This update
can identify and archive saves without SD games; its per-release assignment
picker still uses registered SD releases. A disc-only registration workflow can
read the real product/region from an inserted disc without ripping it, then link
an imported card. That registration workflow is not added by this GUI pass, and
save-family names are not substituted for real release IDs.

Press **Y → Save details** on an unresolved group, its game chooser, or the
advanced save-selection page. Read the original filename, short and long
descriptions, application ID, source VMU, type, block count and header offset.
Use the D-pad to inspect other files in the group. **B** returns to the same
choices without assigning or changing a save. Header text is informational;
it is not a verified declaration of ownership. No save icon preview is included
in this pass.

After a completed import, explicit game choices are remembered under
`/KUI/vmu/matches/`. Later imports can reuse them for the same save metadata
and a unique eligible game with the same title/product. Save payload changes
and allocation changes do not change this metadata key. Changed header text,
missing games, conflicting evidence and damaged match records require review.
Opening or cancelling a picker does not save a match. Physical cards and save
payloads are never edited to remember a choice.

Conflicting confirmations for indistinguishable source metadata are not
learned as a single automatic choice. A later explicit correction can replace a
remembered choice. Existing game/card assignments are independent of these
recognition records.

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

- Check **VMU tools** for the corrected menu spacing and readable settings.
- Import a source, inspect **Y Save details**, and return to the same selected
  group or save. Check that descriptions identify recognizable titles.
- Confirm a manual game match, finish the import, then begin another import of
  that source to check remembered matching. Inspect the final review before
  confirming. The other physical VMUs may reveal further catalogue gaps.
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
