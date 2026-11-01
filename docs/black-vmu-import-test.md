# Black physical VMU import test — @BUILD@

This GUI update adds bounded whole-save recovery to the import wizard. The
black Sega VMU refused the earlier builds at `TIME_STK.001`, physical block
128. Build `4ca1b2142a6a` showed one bit changing at file offset 123 (`08` to
`09`), while the first captured block stayed unchanged in RAM. This update
tries to acquire a consistent complete save; it does not establish which bit
value is historically correct or prove a hardware defect.

The owner subsequently tested build `08045e88736e` and reported that the
black VMU import took a while but worked. The targeted logical-import fix is
therefore hardware validated for that reported case. Exact raw-VMD backup,
the original value of the changing bit and subsequent game reloads were not
separately established by that report. The instructions below retain the
original test procedure for future regressions.

The reader previously placed the request in the same memory used for replies.
When a VMU answered AGAIN, KOS could retry with the reply header overwriting
the original request. Separate persistent request words now remain owned until
the frame completes, including a late completion after a timeout.

A healthy block still needs two identical full reads. After a mismatch, the
reader permits at most eight total samples and requires three consecutive
identical 512-byte samples. It never votes on individual bytes or invents
allocated save data. The later independent save/metadata comparisons remain.
Timeouts, malformed or oversized responses, and device changes refuse.

## Whole-save recovery in Import saves

If a later payload comparison differs and the first captured RAM still matches
its diagnostic pins, the wizard discards the partial reread as recovery evidence.
It then reads that entire save afresh, at most six times. Three consecutive
complete, byte-identical acquisitions are required. A disagreeing candidate is
replaced only by a different complete acquisition; blocks or bits from different
copies are never combined.

Every complete acquisition is bracketed by checks of the same original card
identity and exact root/FAT/directory. A change there, a transport failure,
cancellation or a changed retained-RAM pin stops the import. The accepted
whole file replaces only the wizard's private captured copy, followed by final
metadata and retained-payload checks before packing the virtual card.

This deliberately permits a later consistent save after earlier read variation.
It establishes repeated-read consistency, not recovery of a known original bit.
Strict capture, copy preview/confirmation and exact raw VMD backup retain their
existing refusal behavior. No game-specific checksum, padding or bit repair is
inferred from the Time Stalkers header.

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
   It records recovery attempts and consecutive whole-save matches, the save
   name, physical block, position within that save,
   CRCs, differing byte/bit counts and the first differing offsets and values.
   No camera photograph is required.

The wizard builds a separate standard virtual card from verified logical
saves. It skips unused physical flash, which previously returned changing
bytes on this card. **Backups & restore → whole-card VMD backup** is a separate
exact-copy operation: it still reads all 256 blocks, including unused flash,
and refuses if its full scans differ. No unstable block is silently skipped.

This test reads the physical VMU only. Recovery of persistently unreadable
allocated data is not established; a refused capture remains a useful result.
