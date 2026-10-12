# Toy Commander startup DATA budget tests: AA, AB, AC and AD

These four experimental readers test why the bounded startup DATA path made Z
worse. They vary only its work allowance and use the same new measurements.
The original CDDA algorithm, raw-sector reader, cache policy and launcher are
preserved. The startup engine is retired after the first accepted PLAY and
hands ownership back to the original reader. Build checks establish the
candidate's limits and retained code; console playback remains unverified.

| Profile | Verified 512-byte card blocks per visit | Timer allowance | Purpose |
|---|---:|---:|---|
| AA | 4 | 1,500 ticks / 1.92 ms | Control with the new measurements |
| AB | 12 | 1,500 ticks / 1.92 ms | Increase the block allowance |
| AC | 4 | 6,000 ticks / 7.68 ms | Increase the timer allowance |
| AD | 12 | 6,000 ticks / 7.68 ms | Increase both allowances |

The timer runs at 781,250 ticks per second. Allowances are checked between
finite steps; one step, initial command work or final cleanup can exceed the
listed timer allowance. They are service limits, not a promise about frame
time or total request latency.

These block counts describe physical card work. The original raw image uses
2352-byte sectors, from which cooked DATA requests receive 2048 payload bytes.

## Install and test

Start with **AD**. If it gives a clear improvement, preserve that capture and
describe what changed. If it does not, continue **AB, then AC, then AA**. Cold
boot every candidate. With power off, copy its file from `profiles/` to
`/KUI/apps/games/retail-boot.kui`. Use the same original Toy Commander GDI,
microSD card, display mode, K-UI 1.8.5 launcher and **A / standard SCI reader**.
Record the build line with each capture.

Film the first FMV with sound. At its **first repeatable freeze**, let the hold
be visible for a few seconds and press **A+B+X+Y+Start** to capture the report.
Do this before using Start to skip and before continuing to the second FMV.
The report keeps only eight recent DATA requests; later menu or metadata
reads can replace the requests that caused the freeze. Use the same first-FMV
point for every profile. If it freezes earlier, capture there and label it as
an earlier failure. If the report combination fails, cold boot and capture
just before the failing point; the frozen measurements do not survive power
loss.

In your notes, distinguish playback that resumes by itself from playback that
advances only after **Start**. Keep a deliberate skip or a later CD music
check in a separate launch so it does not replace the first-freeze evidence.
Stop a candidate on a new read fault, a failed reset combination, or clearly
worsened playback.

## Film the report

The report begins at **SCHED 00**. Keep all headings, page numbers and sixteen
values readable; all page numbers and values are hexadecimal.

- Film SCHED **00–0B**, DATA **00–0B**, PILOT **00–07**, and TRACE **00–11**.
  This is 50 pages, about **100 seconds at 60 Hz** or **120 seconds at 50 Hz**.
- If TRACE **12** and following pages are still empty, you can stop there.
  If they contain values, continue through TRACE **19**. A full cycle back to
  SCHED 00 has 58 pages: about **116 seconds at 60 Hz** or **139 seconds at 50 Hz**.

The gameplay measurements are frozen before the report cycles. Filming more
pages does not add more playback time to the counters.

The ZIP also includes the exact previous **B** control and **R** restoration.
A new B capture is useful only if its playback and stopping point are
comparable to the first-freeze capture; it is not required for this batch.
After testing, restore `restore/R-private-write-through.kui` to
`/KUI/apps/games/retail-boot.kui`. The previous B uses SCHED version 1; the
new four profiles use SCHED version 2. Their separate legends are included.

## What the new data distinguishes

SCHED version 2 has 192 words across 12 pages. Its eight records retain the
request sequence, LBA, sector count, destination, delivered bytes, status,
error, first-sector CRC32, first-credit time, completion time and longest GD
visit. Per-request interrupt blocks and retry counts help distinguish physical
work completed by interrupts from bytes observed in a GD call. For an active
request, those physical counters are refreshed at report capture before
cleanup; logical bytes, status and timing retain the last GD observation.
Consumed requests remain unchanged. Sort records by sequence; page positions
are ring slots, not chronological order.

The budget page records the selected profile, configured allowances, service
calls, the reason each service call returned, verified card blocks, zero-credit
calls and credited bytes. Its block, timer, token, step and finished/inactive
exit counters partition counted service calls. A saturated counter or an
invalid timer limits the conclusions that can be drawn from it.

The old synchronous DATA read and payload counters may be zero for these
profiles because the startup engine bypasses those callbacks. Use SCHED's
engine and budget counters, its per-request latency, and TRACE's credited
progress instead. TRACE now accounts for sectors delivered between GD calls
by interrupt work. A successful transport or a matching checksum alone does
not establish smooth video playback.

The CRC32 covers only the first credited sector. Compare matching LBA/sector
sizes between captures, or use the read-only checker against the original GDI:

```bash
python3 tools/check_toy_sector.py "/path/to/Toy Commander.gdi" --lba-hex ACTUAL_LBA --bytes 2048 --crc-hex ACTUAL_CRC
```

Replace the example fields with actual report values. The checker reads one
sector and changes no image or card files. CRC observation cost is reported
separately and is common to AA, AB, AC and AD.
