# B diagnostic console results

Build `2df1c301b26f`. Six photos contain all five distinct pages; one repeats the first longest-gap page. The optional exact Evolution 1 phase recorder is disabled for this Evolution 2 run, so there are no additional phase-record pages.

| Observation | Measured value |
| --- | ---: |
| Longest foreground reader delivery visit | 8.2176 ms |
| Longest controller-completion sampling gap | 23.6864 ms |
| Longest VMU submit C body | 0.07552 ms |
| Longest VMU completion C body | 0.10752 ms |
| IRQ-delivered SD blocks | 0 |
| Foreground-delivered SD blocks | 6506 |
| VMU storage calls / commits | 0 / 0 |

Clock samples were valid. The instruction cache remained enabled. Timing includes instrumentation overhead and uses the admitted nominal 781250-Hz TMU0 rate. The linked reader and worker agree on all eight sampled structure layouts, including the seven reader counters.

Each of the four longest controller gaps had a DMA read pending, the same token at both endpoints, and 40 foreground-delivered blocks. None had PIO, CRC or token-error activity. The full interval had one CRC error and 139 repaired receptions.

The measured VMU path does not show the reported long pause. The reader was serviced entirely by foreground GD calls during this capture. That does not establish why SCI interrupts delivered no data: tight foreground polling can consume completions before an interrupt delivers them. Further changes to just the IRQ burst cap would not target the path doing the measured work here.

The remaining measurement gap is total GD request lifetime and progress across calls. A game can wait for a read while controller interrupts and audio continue; short individual calls do not exclude that. The photographs are not synchronized to movement freezes and do not yet establish the cause. No scheduling change or new binary is claimed by these results.
