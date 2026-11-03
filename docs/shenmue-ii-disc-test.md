# Shenmue II PAL physical-disc support

## Confirmed hardware result

On 3 November 2026 the owner confirmed that build `afb1b3a3d7cf` both boots
PAL Shenmue II through GD Play on an NTSC Dreamcast and completes the rip to
SD. Both were reported to work perfectly. This is the accepted hardware
baseline for these two paths; it does not establish compatibility with every
PAL title or a catalogue/readback match for the dump.

The integration into main preserves its existing menus and optional native
session controls. Physical boot readiness, track bounds, acquisition and
handoff code match the tested source. Main's existing capture naming is
retained; the GD leadout acquisition policy is the tested policy.

The original investigation and test procedure follow. Historical host-test
receipts retain the hardware-pending status recorded when they were created.

This candidate addresses two independent failures from
`diagnostics(20261103-051650).txt` (runtime `e91d20e6dc49`).

The follow-up report from build `5b1d3f193f5a` was "Physical disc boot data
could not be read". No new log identifies its FAD or underlying firmware
result. The acquisition changes address concrete weaknesses and preserve
more evidence on a future failure. The successful hardware result above
confirms the combined fix, without isolating the exact original optical cause.

## Install and test

Merge the supplied `KUI` folder onto the SD card. Replace `KUI/runtime.kui`
and `KUI/apps/disc/native-disc.kui` when prompted. No installer command is
needed. Keep the existing game folders, settings and incomplete dump.

1. Reboot the Dreamcast before testing: the previous failed command left
   the drive requiring reset and disabled DMA for that runtime session.
2. Open GD Play Options. Set **Region-free On** and **Virtual VMU Off**.
3. Insert Shenmue II PAL Disc 1 and use **A: K-UI boot**. The stock retail
   boot option still follows the console BIOS's own region rules.
4. Report the furthest screen reached, approximate delay, and build number.
   Failed GD Play checks/preparation now save a diagnostic automatically.
   If it still fails, retain the latest `/KUI/probes/p*/diagnostics.txt` and
   the displayed stage/FAD/reason. A new B press can cancel report saving.
   Rejected ISO extents include their LBA/count in the log.
5. Reboot before the ripper test, then choose **Resume** for the existing
   `SHENMUE2 EU (Disc 1)` job. Resume retains the existing durable-byte
   verification. It should finish the remaining tail using single-sector
   PIO reads rather than repeat the failing bulk DMA command.
6. If capture completes, use **Verify** and retain its report. A completed
   verification report is still needed before claiming saved-byte verification
   or a catalogue match; successful capture alone does not imply either.

## GD Play change

The old physical-disc source exposed only the first high-density DATA track
to ISO9660 validation. The observed disc has DATA track 3, AUDIO tracks 4/5,
then DATA track 6. Boot-file and directory extents in that later DATA track
were rejected before region handling or game execution.

The candidate supplies all validated high-density data ranges. Reads remain
inside one data range; audio tracks, declared type-transition gaps, invalid
maps, overflow and cross-boundary extents remain refused. The staged IP and
executable still pass the existing byte-fingerprint checks.

With Region-free enabled, K-UI's direct path bypasses the menu locale gate
and enters the original Bootstrap 2 at `0xac00e000`. It does not change
console flash or disc contents. Region-specific behavior inside the running
game remains a separate, untested possibility.

## Physical boot read recovery

The previous app stopped immediately on a recoverable optical result. Its
raw repeat policy also inherited the ripper's global timing phase, so the
same launch could use paired or single reads depending on prior operations.

Boot acquisition now explicitly uses two guarded raw reads with different
initial fills and requires exact lengths/equality. Each command allows up to
30 seconds for seeking/firmware recovery and 5 seconds to confirm an abort.
Capture and salvage retain their existing policies.

A recoverable run is reduced to single-sector requests for the rest of the
preparation. Each bad sector has a finite raw retry budget. The final 150
sectors of a final DATA track use single-sector requests preemptively; a
request crossing into that region is acquired as singles too. Sector counts
and file extents are preserved.

If raw acquisition still cannot verify a sector, the app may switch to the
firmware's normal Mode1 2048-byte data path. This requires two guarded reads,
exact lengths and matching payloads. It relies on firmware correction/status
and repeat equality; a cooked sector has no externally verified raw header
or EDC. That distinction is logged. A previously observed wrong raw sector
address must recover through correctly addressed raw reads or fail, including
when another sector in the same run already selected cooked acquisition.

Every requested run is copied to its caller only after all its sectors pass.
The original metadata rules, media bounds, first-IP comparison, staged
CRC/SHA checks, final expected-IP handoff and physical firmware are retained.
The final two IP-sector checks use the same longer command/abort windows.
A one-use receipt from a successful explicit boot read permits the observed
post-read PLAYING state at handoff entry. A new command, format operation or
boot read retires that receipt; an unexplained PLAYING state remains refused.
Both exact IP reads and the final drive/media checks still apply.
No failed-abort, guard corruption or media-change latch is cleared to retry.
Clean cancellation is distinct from a real failure followed by a late B.

The command and retry budgets are finite, but a difficult sector can still
take several minutes. B remains available to stop. No total launch-time
guarantee or physical-drive recovery is claimed by the host fixtures.

## Ripper change and limits

The log committed through FAD 549158, leaving 142 sectors before the declared
leadout at 549300. The bulk DMA read timed out and the following raw read
could not be aborted cleanly. That does not identify whether the underlying
cause is media, drive firmware or transfer behavior.

The candidate clips requests before the final 150-sector window of the last
GD-ROM track, then reads that window one sector at a time through PIO. It
preserves every requested sector and the existing integrity/checkpoint rules.
It does not shorten the track, synthesize bytes or label failed reads good.
The policy applies to capture and resume, including prefetched reads.

## Technical references

- [Dreamcast IP.BIN/bootstrap structure](https://mc.pp.se/dc/ip.bin.html)
- [httpd-ack dumper and transfer options](https://github.com/sega-dreamcast/httpd-ack)
- [Original Shenmue II EU dump report](https://dcemulation.org/dumpcast/viewtopic.php?t=28)

Host regression results accompany the package. They verify the request and
admission policies, not the behavior of the user's physical drive or full
Shenmue II gameplay.
