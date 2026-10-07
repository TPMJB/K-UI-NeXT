# CDDA calibration and command console results

Recorded 2026-10-07 from the owner's photographs `61960.jpg` (profile 04)
and `61961.jpg` (profile 05). Both screens identify build `2cb5d25bb97f`,
show `CDDA TEST COMPLETE`, and report zero failures / stopped audio.
The photographed values were independently read from both supplied images.
The photographs and owned game audio are not included in this source record.

The published test source is
[`2cb5d25bb97f44db7f5a8ef6eeaabbcf47687f07`](https://github.com/TPMJB/K-UI-NeXT/tree/2cb5d25bb97f44db7f5a8ef6eeaabbcf47687f07),
with source tree `8f3113a5240f5b5b3f1fe0286c03b153cf1c47c0`.
The [short-test instructions](../cdda-calibration-commands-test.md) describe
the exact profiles; [earlier controls, soak and stress evidence](cdda-controls-soak-stress-hardware-2026-10-07.md)
remains a separate checkpoint.

## Exact photographed counters

| Counter | Profile 04: calibration | Profile 05: commands |
| --- | ---: | ---: |
| Completed playback stages | 1 | 7 |
| Worst half refill, us | 80,169 | 80,174 |
| Maximum service gap, us | 7,298 | 6,256 |
| Minimum refill margin, us | 105,396 | 105,396 |
| Checked card blocks | 20,904 | 5,784 |
| Observed private stack use, bytes | 5,252 | 5,192 |
| Failures / stopped audio | 0 | 0 |

Profile 04 also reports:

| Counter | Value |
| --- | ---: |
| Paired played frames | 2,652,584 |
| Elapsed ticks, lower / upper | 750,000,076 / 750,000,242 |
| Read ticks, start / end | 84 / 82 |
| TMU1 TCR1, start / end | 0 / 0 |
| FRQCR, start / end | 3,594 / 3,594 |
| Nominal TMU Hz / configured AICA pitch | 12,500,000 / 0 |

Profile 05 also reports:

| Counter | Value |
| --- | ---: |
| Accepted model commands | 48 |
| Completed command actions | 27 |
| Checked STATUS snapshots | 12 |
| Expected invalid/state refusals | 2 |
| Expected stale token refusals | 4 |
| Saved PAUSE played frame | 181,255 |
| Maximum played loop passes | 4 |

## Calibration interpretation

The calibration met its implemented numerical conditions: one completed
stage, valid matched endpoint bounds, stable clock-control snapshots, and
zero unexpected failures. Its lower elapsed bound exceeds the required
750,000,000 ticks; its upper bound is below 752,521,995 ticks. Both endpoint
read windows are below the 100,000-tick limit. Their sum, 166 ticks, equals
the difference between the upper and lower elapsed bounds. The raw FRQCR
snapshots are decimal 3,594 (`0x0e0a`); no clock-source cause is inferred here
from that register value.

Using the **assumed nominal** TMU frequency of 12,500,000 Hz converts the
elapsed bounds to 60.00000608 through 60.00001936 nominal seconds. The
endpoint read windows become 6.72 us and 6.56 us on that same assumption.
The measured frame count is hardware left-channel progress over these
matched endpoints, rather than source prefetch progress or rounded loop
counts.

Let `N = 2652584`, `L = 750000076`, `U = 750000242`, and
`H = 12500000`. Including the conservative displayed ±1-frame endpoint
quantization, the nominal-TMU-referenced playback-rate interval is:

```text
lower rate = (N - 1) * H / U
upper rate = (N + 1) * H / L
           = 44209.702401669 .. 44209.745520079 frames/nominal second
```

Relative to 44,100 frames per nominal second, this is a ratio of approximately
1.002487582804 through 1.002488560546, or **+2,487.58 to +2,488.56 ppm**
(about **+0.2488 percent**). The midpoint-tick calculation without the
quantization allowance is approximately 44,209.723960872 frames per nominal
second. These are derived values from the photographed integers, not extra
measurements.

This establishes a relative AICA/TMU timing difference under the nominal
conversion. It does not establish an absolute 44,209.7 Hz audio sample rate,
which oscillator accounts for the difference, audible pitch error, or a
game synchronization error. The unchanged clock snapshots supply no
independent frequency reference. Configured pitch 0 is the programmed
contract, not a hardware pitch-register readback. No automatic clock or
pitch retuning ran in this profile; attribution requires separately verified
clock-source information or an independent reference.

### Comparison with the pinned clock reference

The already pinned official KallistiOS
[`timer.c`](https://github.com/KallistiOS/KallistiOS/blob/fcfa7d869471591ca1c777543261a7bfea7cb726/kernel/arch/dreamcast/kernel/timer.c)
documents a measured main clock of 199,499,520 Hz and a peripheral clock one
quarter of that. With the harness's peripheral-clock/4 timer selection, this
reference gives **12,468,720 Hz** rather than the convenient nominal
12,500,000 Hz used in the test. This is a primary implementation reference
already covered by the pinned KOS provenance; no DreamShell source was used.

Conditionally substituting that documented reference frequency in the same
endpoint calculation gives **44,099.072042379 through 44,099.115052890
frames per second**, approximately -21.04 through -20.07 ppm relative to
44,100. The nominal timer approximation therefore accounts for almost all
of the observed +0.2488 percent ratio difference. This agreement is an
inference from the raw measurement and upstream reference, not an independent
measurement of either oscillator on this particular console.

The next experimental build should use an explicit documented timer contract
consistently for duration conversion, deadline checks and admission budgets,
with focused arithmetic and timing tests. Preserve build `2cb5d25bb97f` and
its raw counters as the tested checkpoint. Do not tune AICA pitch or derive a
new absolute sample rate from this one relative comparison.

## Commands, listening and remaining limits

Profile 05 completed all seven automatic stages. Its 48 accepted commands,
27 completed actions and 12 checked STATUS snapshots match the implemented
sequence. The two invalid/state refusals and four stale token refusals are
expected checks, separate from unexpected failures. The saved PAUSE cursor
and loop maximum are exact photographed values; the completed sequence also
indicates that its implemented played-cursor, pause/resume, cancellation,
EOF and repeated STOP checks succeeded on this run.

Both runs retained more than 105 ms of displayed refill margin. Their stack
watermarks remain within the guarded 65,472 usable private-stack bytes;
neither screen reports a guard failure. These observations cover the exercised
paths and card workload, not arbitrary-card or error-path worst cases.
The [memory and host audit](cdda-calibration-commands-memory-2026-10-07.md)
remains the separate static/model evidence.

The owner reported: **“Here's 4 and 5. Sounded successful.”** Record this as a
successful casual-listening impression. Combined with the earlier report
that TV speakers did not clearly separate left/right, it does not verify
channel distinction, absolute pitch, every planned silent interval, or
the absence of a brief glitch. A later short check through clearly separated
stereo output can close those listening questions.

Both profiles ran the detached SCI homebrew harness with owned main RAM,
AICA resources and timer. No retail game or BIOS CDDA command hooks were
running. These results close the requested short-profile numerical tests;
they do not establish Toy Commander sound-driver sharing, retail memory
ownership, BIOS command delivery or periodic background service inside a
game. The released 1.8.5 readers remain preserved while those separate
[retail admission gates](../cdda-roadmap.md) are addressed.
