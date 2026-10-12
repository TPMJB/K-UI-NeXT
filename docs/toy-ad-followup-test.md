# Toy Commander: AE, AF, AG and AH follow-up tests

AD completed the first intro in the reported hardware run, with some choppiness;
the second intro still stalled. AA, AB and AC did not provide that improvement.
These four candidates explore the larger DATA allowance and the original audio
worker's refill scheduling. The ZIP includes the exact tested AD as a reference
and R for restoration. New candidates still require hardware testing.

| Profile | Startup DATA blocks | DATA timer allowance | Audio refill allowance | Purpose |
|---|---:|---:|---|---|
| AE | 24 | 6,000 ticks / 7.68 ms | Original: 4 quanta / 16 ms | Increase DATA blocks from AD's 12 |
| AF | 24 | 12,000 ticks / 15.36 ms | Original: 4 quanta / 16 ms | Increase both DATA allowances |
| AG | AD's 12 | AD's 6,000 ticks / 7.68 ms | 2 quanta / 16 ms | Reduce audio refill work per visit |
| AH | AD's 12 | AD's 6,000 ticks / 7.68 ms | 4 quanta / 8 ms | Reduce audio refill time admission |

One DATA block is a verified physical 512-byte card block. Toy Commander's raw
2352-byte image sectors supply 2048-byte cooked DATA payloads. One audio quantum
is the original refill operation: it can fetch at most one verified raw sector
and can serve two fragments already in its sector buffer. It is not a fixed
number of milliseconds or an unconditional sector read.

The clock runs at 781,250 ticks per second. Both engines check time admission
between finite operations; a final operation, initial command or cleanup may
cross the stated allowance. These limits do not promise a frame deadline or
total request latency. All profiles retain the original raw-sector transport,
CRC, cache policy and audio protocol. AG and AH change refill scheduling only.
Startup DATA retires after the first accepted PLAY and returns ownership to the
original reader. An audio refill policy can affect a stall only when that
worker is active; a stall before accepted PLAY remains a DATA experiment.

## Test order and capture

Try **AG, then AE, then AH, then AF**, cold booting each. AG directly tests a
smaller audio refill visit while retaining AD's startup settings. AE then tests
whether more DATA blocks fit inside the successful timer allowance. AH and AF
separately examine shorter audio admission and longer DATA admission. If a
candidate is clearly worse, capture that failure and continue to the next.

With power off, copy one file from `profiles/` to
`/KUI/apps/games/retail-boot.kui`. Keep the same original uncompressed Toy
Commander GDI, microSD card, display mode, K-UI 1.8.5 launcher and
**A / standard SCI reader**. Film the build line and both intros with sound.

At the **first actual stall**, let the held picture remain visible for a few
seconds, then press **A+B+X+Y+Start** for the report. Capture this before using
Start to skip. If the first intro completes, continue into the second and
capture its first stall. If both complete, record that and capture at a
consistent visible point afterward. Distinguish playback resuming by itself
from advancing after a deliberate skip. Recent request records form an
eight-slot ring; later activity can replace the requests around the stall.

If the report combination fails, cold boot and capture just before the failing
point, identifying that as a different stopping point. Frozen measurements do
not survive power loss. In a separate ordinary launch, check whether the CD
music starts and continues; keep this separate from the first-stall capture.

## Report pages

The report begins at **SCHED 00**. Keep headings, page numbers and all sixteen
values readable. Page numbers and values are hexadecimal.

- Film SCHED **00–0B**, DATA **00–0B**, PILOT **00–07**, and TRACE **00–11**.
  These 50 pages take about **100 seconds at 60 Hz** or **120 seconds at 50 Hz**.
- If TRACE **12** onward are still zero, stop there. If they contain values,
  continue through TRACE **19**. A full return to SCHED 00 takes about
  **116 seconds at 60 Hz** or **139 seconds at 50 Hz**.

Reports freeze before displaying their pages. Filming more pages does not add
playback time to the measurements. The exact AD reference uses SCHED v2 too;
its budget profile is 4. New budget profiles **5, 6, 7 and 8** identify AE, AF,
AG and AH. The build ID also identifies their audio policy. Exact legends and
the full AA/AB/AC/AD hardware comparison are included.

The SCHED budget counters show which DATA limit ended each service visit.
Its records include LBA, size, destination, credited bytes, first-sector CRC,
timing, physical IRQ progress and retries. Sort them by request sequence.
Original synchronous DATA and fixed-step counters may be zero because the
startup engine bypasses those callbacks; use SCHED and TRACE for actual
progress. PILOT describes the original audio worker, and its terminal PAUSE
can result from report capture. Successful transport counters alone do not
establish smooth playback or audio synchronization.

CRC32 covers only the first credited sector. To compare an actual observation
with the original GDI without modifying it:

```bash
python3 tools/check_toy_sector.py "/path/to/Toy Commander.gdi" --lba-hex ACTUAL_LBA --bytes 2048 --crc-hex ACTUAL_CRC
```

Replace the placeholders with report values. No extra AD rerun is required;
its exact binary is available if a direct comparison becomes useful. Restore
`restore/R-private-write-through.kui` to `/KUI/apps/games/retail-boot.kui` when
finished, or use the included AD reference for an ordinary comparison run.
