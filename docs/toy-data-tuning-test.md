# Toy Commander: AI, AJ, AK and AL DATA tests

AE and AF moved the second intro slightly farther in the reported runs, but
neither completed it. AF also had a brief audio glitch. The captured AF DATA
engine stopped at its finite poll-step limit in 1,437 of 2,300 service visits;
AE stopped at its timer allowance in 2,529 of 3,415 visits. This batch tests
whether AF's remaining step limit restricts useful DATA progress, then varies
the timer and physical block allowances separately. These candidates still
need hardware testing; host checks do not establish a playback improvement.

| Profile | DATA blocks | Timer allowance | Finite poll steps | Comparison |
|---|---:|---:|---:|---|
| AF reference | 24 | 12,000 ticks / 15.36 ms | 1,024 | Exact previously tested binary |
| AI | 24 | 12,000 ticks / 15.36 ms | 2,048 | Double AF's poll-step allowance |
| AJ | 24 | 9,000 ticks / 11.52 ms | 1,024 | Intermediate timer, original step allowance |
| AK | 32 | 12,000 ticks / 15.36 ms | 2,048 | Increase blocks from AI's 24 |
| AL | 32 | 18,000 ticks / 23.04 ms | 2,048 | Increase timer from AK's 12,000 |

Try **AI, then AJ, then AK, then AL**, cold booting each. AI directly tests
the limit that most often ended AF service visits. AJ checks a shorter timer
with AF's original step cap. Comparing AI with AK separates block allowance;
comparing AK with AL separates timer allowance. Capture a clearly worse result
and continue to the next profile.

One DATA block is a verified physical 512-byte card block. Toy Commander's raw
2352-byte image sectors supply 2048-byte cooked DATA payloads. A poll step is
one finite engine step; it can make no progress while waiting for the wire.
Doubling this count does not promise twice the throughput. The timer runs at
781,250 ticks per second and is checked between finite operations. An initial
command, final operation or cleanup can cross the allowance. Every visit still
has finite block, timer and step limits; these are not frame deadlines or
total request latency limits.

All four profiles retain AF's original audio refill allowance of **4 quanta
and 12,500 ticks / 16 ms**. The original audio transport, raw-sector reader,
CRC and cache policy are retained. Startup DATA retires after the first
accepted PLAY and returns ownership to the original reader. A stall before
that boundary is a DATA measurement; these candidates do not change the
audio policy after it.

## Install and capture

With power off, copy one file from `profiles/` to
`/KUI/apps/games/retail-boot.kui`. Keep the same original uncompressed Toy
Commander GDI, microSD card, display mode, K-UI 1.8.5 launcher and
**A / standard SCI reader**. Film the build line, both intros and sound.

At the **first actual stall**, keep the held picture visible for a few seconds,
then press **A+B+X+Y+Start** to freeze the report. Capture this before using
Start to skip. If the first intro completes, continue to the second and capture
its first stall. If both complete, record that and capture at a consistent
visible point afterward. Record whether playback resumed by itself or moved
after a deliberate skip. The last eight SCHED requests form a ring, so later
activity can replace requests near the original stall.

If the report combination fails, cold boot and capture just before the failure,
identifying that as a different stopping point. Frozen reports do not survive
power loss. In a separate ordinary launch, check whether the CD music starts
and continues; keep this separate from the first-stall capture.

## Report pages and interpretation

Film **SCHED 00–0B**, **DATA 00–0B**, **PILOT 00–07** and **TRACE 00–11**.
Keep headings, hexadecimal page numbers and all sixteen words readable. These
50 pages take about 100 seconds at 60 Hz or 120 seconds at 50 Hz. If TRACE 12
onward contains values, continue through TRACE 19. If the displayed later pages
are zero, filming may stop there; the decode will distinguish observed zero
pages from pages absent from the recording.

Reports freeze before display, so longer report filming adds no playback time
to their measurements. SCHED remains version 2 with 192 words. Budget profiles
**9, 10, 11 and 12** identify AI, AJ, AK and AL. The exact build ID and included
legend also identify the finite poll-step limit, which is not an extra SCHED
wire field. Exact AE, AF and AD reference binaries and the complete hardware
comparison are included. No extra reference rerun is required.

The budget counters partition service exits into block, timer, token, step
and finished/inactive reasons. SCHED records identify actual LBA, sector count,
destination, credited bytes, first-sector CRC, timings, IRQ progress and
retries. Compare matching LBA/count/destination requests rather than assuming
the same ring slot or request count. Original synchronous DATA counters can be
zero during asynchronous startup; SCHED and TRACE measure that engine's work.
PILOT measures the retained audio worker, and terminal PAUSE can result from
report capture. Successful transport alone does not establish smooth playback
or audio synchronization.

CRC32 covers the first credited sector only. To check an actual report value
against the original GDI without modifying it:

```bash
python3 tools/check_toy_sector.py "/path/to/Toy Commander.gdi" --lba-hex ACTUAL_LBA --bytes 2048 --crc-hex ACTUAL_CRC
```

Replace the placeholders with current report values. Restore
`restore/R-private-write-through.kui` to `/KUI/apps/games/retail-boot.kui` when
finished, or use the included exact AF reference for a direct comparison.
