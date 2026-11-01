# Black physical VMU import test — @BUILD@

This GUI update retains the Maple BREAD retry correction and bounded block
recovery, and adds diagnostics for a later capture-pass disagreement. The
black Sega VMU still refused build `bc455d0ca6df`: save 3 changed at block
128. Its complete log showed no transport refusal or local recovery before
that comparison. This build improves diagnosis; the original changing-data
problem remains unresolved.

The reader previously placed the request in the same memory used for replies.
When a VMU answered AGAIN, KOS could retry with the reply header overwriting
the original request. Separate persistent request words now remain owned until
the frame completes, including a late completion after a timeout.

A healthy block still needs two identical full reads. After a mismatch, the
reader permits at most eight total samples and requires three consecutive
identical 512-byte samples. It never votes on individual bytes or invents
allocated save data. The later independent save/metadata comparisons remain.
Timeouts, malformed or oversized responses, and device changes refuse.

Each accepted first-pass block now gets a diagnostic CRC recorded immediately
after capture. A later mismatch logs that original CRC, the retained block's
current CRC and the reread CRC. These observations help distinguish retained
RAM changing from later accepted samples differing. They do not replace the
exact comparison or permit an inconsistent import. The report also includes
bounded raw VMS-header fields labelled unvalidated, for interpreting the
changing byte's position within the file.

## Copy the GUI

With the console powered off, back up your current `KUI/runtime.kui` to the PC.
Merge this ZIP's **KUI** folder onto the SD root, replacing that one file.
The menu should show **@BUILD@**. No installer script is needed. The remaining
source, artifacts and evidence folders stay on the PC.

This installs no game/disc modules and changes no existing cards, save images,
journals, assignments or settings. Restore the backed-up runtime to revert.

## Import all saves from the black VMU

1. Insert the black VMU and open **VMU Manager → Import saves**.
2. Start a new import and select its physical slot. Capture the whole source
   in one operation, then review all the saves offered by the wizard.
3. If capture succeeds, use the normal review/import flow. Check that saves
   from every expected game are present, including Time Stalkers if it is
   still on this card. Do not delete or restore anything on the physical card.
4. If capture refuses, open Diagnostics, save the log with **Y**, and send it.
   It records the save name, physical block, position within that save,
   CRCs, differing byte/bit counts and the first differing offsets and values.
   No camera photograph is required.

The wizard builds a separate standard virtual card from verified logical
saves. It skips unused physical flash, which previously returned changing
bytes on this card. **Backups & restore → whole-card VMD backup** is a separate
exact-copy operation: it still reads all 256 blocks, including unused flash,
and refuses if its full scans differ. No unstable block is silently skipped.

This test reads the physical VMU only. Recovery of persistently unreadable
allocated data is not established; a refused capture remains a useful result.
