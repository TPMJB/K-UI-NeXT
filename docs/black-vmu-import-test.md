# Black physical VMU import test — @BUILD@

This GUI update corrects a Maple BREAD retry defect and adds bounded recovery
for differing block samples. It needs a new test on the black Sega VMU; host
tests do not establish that its previously observed changing bytes are fixed.

The reader previously placed the request in the same memory used for replies.
When a VMU answered AGAIN, KOS could retry with the reply header overwriting
the original request. Separate persistent request words now remain owned until
the frame completes, including a late completion after a timeout.

A healthy block still needs two identical full reads. After a mismatch, the
reader permits at most eight total samples and requires three consecutive
identical 512-byte samples. It never votes on individual bytes or invents
allocated save data. The later independent save/metadata comparisons remain.
Timeouts, malformed or oversized responses, and device changes refuse.

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
   It records the exact block, repeated-read CRCs, first differing byte and
   allocated save context when available. No camera photograph is required.

The wizard builds a separate standard virtual card from verified logical
saves. It skips unused physical flash, which previously returned changing
bytes on this card. **Backups & restore → whole-card VMD backup** is a separate
exact-copy operation: it still reads all 256 blocks, including unused flash,
and refuses if its full scans differ. No unstable block is silently skipped.

This test reads the physical VMU only. Recovery of persistently unreadable
allocated data is not established; a refused capture remains a useful result.
