# Toy Commander DATA scheduling tests: B, Y and Z

These are separate exact-title experimental readers. B is the original
two-sector DATA behavior with the new measurements. Y limits startup visits
to one logical sector. Z uses the existing bounded stateful SCI DATA engine
only before the first accepted PLAY, then closes DMA/card/SCI ownership and
returns to the original synchronous reader. All use the original audio
algorithm, original raw image, original cache policy and original launcher.
No console playback improvement is established by the build checks.

## Test order

Cold boot each profile: **B, then Y, then Z**. With power off, copy one profile
to `/KUI/apps/games/retail-boot.kui`. Use the same Toy GDI, card, display mode
and **A / standard SCI reader**. Record the build line with each capture.

For the first run, film the intro with sound. At the repeated problem point,
let the hold be visible for a few seconds, then use **A+B+X+Y+Start**. Use the
same stopping point for the comparisons. If playback cannot reach that point,
request the report earlier and retain that as a distinct run. A hard crash
that disables the combination requires a cold boot and one capture just
before that point; the report cannot survive power loss.

The report begins at **SCHED page 00**. Record one entire SCHED/DATA/PILOT/TRACE
cycle, until SCHED 00 returns: about **114 seconds at 60 Hz**, or **137 seconds
at 50 Hz**. All headings, page numbers and sixteen values must be readable.
There are 11 SCHED, 12 DATA, 8 PILOT and 26 TRACE pages. Values are hexadecimal.
Filming the terminal does not extend the frozen gameplay measurements.

If the profile can reach music, use a separate launch to check that the previously
working CD music starts and continues. If the repeatable crash prevents reaching
PLAY, record that limitation. Keep that run's evidence separate.
Stop a candidate on a new read fault, reset failure or worsened playback.
Restore `restore/R-private-write-through.kui` when finished. No new R/T/V/W/X
capture is required just to obtain this batch's data.

## What the reports mean

SCHED preserves the last eight accepted DATA requests in chronological ring
slots. Its header identifies the next slot and active slot; sort records by
sequence, not by page position. Each record retains the request LBA, count,
destination, sector size, delivered bytes, status/error and first-sector CRC32.
The checksum is sampled once through a read-only guest mapping only after at
least one complete sector is credited. A successful CRC flag does not verify
every later sector of the request. The sampling cost is recorded separately;
B/Y/Z all use this observer, so B controls for its effect.

The checksum can be compared among matching B/Y/Z requests or with the original
GDI using the included read-only `tools/check_toy_sector.py`:

```bash
python3 tools/check_toy_sector.py "/path/to/Toy Commander.gdi" --lba-hex 0007829C --bytes 2048 --crc-hex XXXXXXXX
```

Use actual LBA, byte count and CRC from the report; the values above are an
example shape. This reads only one sector and changes no game/card files.

For Z, DATA's old synchronous callback counters may be zero: the startup DATA
engine bypasses those callbacks. Use SCHED's engine page, TRACE's verified
progress and whole-request latency instead. Successful DMA alone does not
prove the game got execution time. The service allowance is checked between
finite steps and can be exceeded by one step. Initial commands and final
stream cleanup can also take longer than the steady-state service allowance.

Y may cost more command overhead or provide less data per frame. A one-sector
visit does not guarantee the largest stall halves, and returning to a caller
with interrupts masked does not enable interrupts. The source locations,
checksum and visible motion are needed alongside timing to identify the
repeatable fault. Working audio remains on the preserved path after PLAY.
