# SCI inline CRC console result — 2026-10-01

Run **18**, build **93794e47df59**, passed a five-minute SCI soak. The owner
initially described better writes and similar reads, then reported:
**"Gameplay was largely unchanged."** Exact exports show both storage rates
within 0.32% of the previous retained soak. No gameplay or storage benefit
from the resident CRC inlining is established by this result.

## Original reports

The uploads are preserved byte-for-byte, including CSV line endings:

| Upload | Accepted copy | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `result(10).json` | [JSON](sci-inline-crc-result-2026-10-01.json) | 1,534 | `06ee5c71c42775f53f981431df7f4fdb4047560da847491d14de60ff59d91943` |
| `result(10).csv` | [CSV](sci-inline-crc-result-2026-10-01.csv) | 244 | `a732fe5e61a59d420ab8f4d0bd2c189ca7b4abab3649a87c6f0cb389ec67713d` |

All 17 CSV fields agree with the JSON. The successful soak wrote, remounted
and verified **167,772,160 bytes (160 MiB), ten cycles**, in **302.997325
seconds**. All error categories, CRC failures, timeouts and DMA failures are
zero; FatFs error is zero; cleanup succeeded; failure phase is empty.

The comparison is [run 13 on 8d930310f79d](sci-game-pacing-result-2026-10-01.md):
the same preset 2, five-minute target, one repeat, SCI transport, exFAT with
128 KiB clusters, 64 KiB calls, music off and 2 Hz UI. Both labels are blank.
Volume start is 2,048 sectors and size is 249,997,312 sectors in both runs;
free space is now 82,721,505,280 bytes, 3.125 MiB lower. Physical card identity,
allocation, temperature and other session conditions are not independently
controlled by these exports. This is the immediately preceding retained
storage soak, not a new isolated timing of the later `ce7006087f20` game build.

## Storage throughput and latency

| Measurement | Run 13: 8d930310f79d | Run 18: 93794e47df59 |
| --- | ---: | ---: |
| Written and verified MiB | 160 | 160 |
| Complete cycles | 10 | 10 |
| Elapsed seconds | 303.722145 | 302.997325 |
| Write KiB/s | 1,197.400078 | 1,201.155382 |
| Read KiB/s | 1,064.983369 | 1,068.096839 |
| Timed writes, seconds | 136.829789 | 136.402003 |
| Timed reads, seconds | 153.842778 | 153.394331 |
| 64 KiB calls per direction | 2,560 | 2,560 |
| Mean write/read call, ms | 53.449 / 60.095 | 53.282 / 59.920 |
| Minimum write/read call, ms | 47.606 / 53.697 | 47.623 / 53.700 |
| Maximum write/read call, ms | 147.970 / 116.132 | 148.384 / 116.084 |
| Write/read calls above 100 ms | 3 / 90 | 3 / 87 |
| Flush time, ms | 93.720 | 105.447 |

Writes differ by **+0.313621%**, reads by **+0.292349%**. Timed writes save
0.427786 seconds and timed reads save 0.448447 seconds over the same payload.
Both directions are effectively unchanged; this pair does not demonstrate a
distinct write gain or a repeatable gain of these exact percentages. Every
call exceeds 20 ms. Both directions retain a histogram p95 upper bound of
131.071 ms, which is not an exact percentile.

## DMA phases and scope

| Phase | Run 13 total, us | Run 18 total, us | Run 13 us/block | Run 18 us/block |
| --- | ---: | ---: | ---: | ---: |
| RX setup | 639,385 | 630,373 | 1.951248 | 1.923746 |
| RX transfer | 108,396,315 | 108,392,790 | 330.799301 | 330.788544 |
| RX reversal and CRC check | 22,423,668 | 22,420,521 | 68.431604 | 68.422000 |
| TX setup | 6,669,033 | 6,679,583 | 20.319718 | 20.354963 |
| TX transfer | 108,446,526 | 108,410,209 | 330.423138 | 330.362813 |

All phase averages remain effectively unchanged. They cover successful DMA
payloads, not entire filesystem calls. Run 18 reports **327,680 RX DMA
blocks**, exactly its payload sector count, **328,155 TX DMA blocks** and
**959 polled block calls**, versus 327,680 / 328,205 / 1,021 previously.
Every reported DMA block is profiled. Extra writes and polled calls are not
individually classified; these runtime totals do not measure the game's DMA
eligibility or blocked CPU time.

Run 18's RX phases sum to **401.134290 us/block**, versus 401.182153 before.
Timed file reads average **468.122348 us/block**, versus 469.490900, leaving
**66.988058 us/block** outside these phases, versus 68.308746. That residual
includes multiple transport, filesystem and runtime costs; it cannot be
assigned solely to the card or to framing. See the
[profile scope](sci-dma-profile-2026-10-01.md).

## Correction: runtime code was already inlined

The [candidate note](sci-inline-crc-2026-10-01.md) previously implied that
forcing the helper inline would change runtime transport performance too.
That expectation was incorrect: the runtime already emitted inline CRC code.
The new annotation changes the detached resident's generated assembly, where
it removes per-byte calls and spills; it does not change the runtime's
executable code in these delivered packages.

The independently inspected files are
`output/kui-ce7006087f20/KUI/runtime.kui` and
`output/kui-93794e47df59/KUI/runtime.kui` under
`/workspace/scratch/e58804339be4/`. Both are **1,595,396 bytes**. Their full
SHA-256 values are, respectively,
`d5827f5abb4b88a2cc9cab9996f344ab1d3b40c8af14a3b254e6338f0ad6ff56` and
`e7b0559d98f9b2f036fc2a5ad117d009252676317428770328320f057ae3bbe6`.

Only **69 bytes differ**, all in the 64-byte package header, two embedded
build-ID strings, and the SDK timestamp/build-host banner. For reproducible
normalization, discard the first 64 bytes and zero these half-open decimal
offset ranges in each remaining payload:

| Payload range | Metadata |
| --- | --- |
| `[702064, 702076)` | Build ID |
| `[705893, 705905)` | Build ID in runtime banner |
| `[1587609, 1587637)` | SDK timestamp |
| `[1587640, 1587760)` | SDK runner host/path token |

The resulting **1,595,332-byte payloads are byte-identical**, SHA-256
`3d874b3b5112e5cc19518702ddd04b48e359d0aa9d95332992bfc9bf10d2ba43`.
No other byte differs. Therefore this storage soak confirms integrity and
stable runtime performance; it cannot measure the detached game resident's
CRC improvement. The earlier recommendation to look for a new runtime
check-phase gain should not be repeated.

## Return-screen photograph

The supplied `61640.jpg` shows build `93794E47DF59`, `NATIVE GD IMAGE` and
`GAME MENU RETURN`. The inspected copy is
`/workspace/scratch/e58804339be4/upload/01-61640.jpg`, **249,714 bytes**,
SHA-256 `098eab4010d6c658f8fcb9971ac6ab7dd0885e2eb72ce8a5e7ea2f94d1f654e1`.
The original image is retained outside the source repository. All displayed
numeric fields are hexadecimal:

| Displayed field | Hexadecimal | Decimal |
| --- | --- | ---: |
| GUARD FAULT | `00000000` | 0 |
| READ STEPS | `00000E1B` | 3,611 |
| SECTORS READ | `000035F8` | 13,816 |
| PACED STEPS | `00000CD7` | 3,287 |
| SPIN STEPS | `00000000` | 0 |
| PACE PERIOD | `0000020D` | 525 |
| PACE VBI | `00000104` | 260 |
| PACE COST16 | `00000450` | 1,104 |
| PACE STILL | `00000001` | 1 |

The session delivered **3.826087 game sectors per reading step**. Enlarged
budgets were granted on **91.027416%** of its reading steps, with 324 steps
not enlarged. The preceding `ce7006087f20` photograph showed 3.234546 sectors
per step and 79.192547% enlarged, but the sessions cover different totals and
are not matched first-fight traces. Larger cumulative averages do not prove
a new speed gain. Short request tails can deliver fewer sectors than their
budget. Zero guard fault and spin steps are reported; this display does not
count every error or physical DMA use.

Latest period/vblank remains **525/260**. Cost is `1104 / 16 = 69` counter
ticks per game sector. With the captured stillness of 1, the moving-buffer
allowance is `floor(525/2) * 16 = 4192` scaled ticks: three sectors fit
(`3 * 1104 = 3312`), four do not (`4 * 1104 = 4416`). A retained cost at or
below 1048, 5.072464% lower, would admit four under that particular allowance.
This is a policy calculation, not a speed prediction or an observation of
the first seconds of combat. The latest cost increased from 1084 in the
prior photograph, but it is the latest sample rather than a controlled phase
comparison; it does not show that the inlined loop got slower.

## Game observation and next experiment

The owner reports **"Gameplay was largely unchanged."** No new exact
Kasumi-selection-to-first-fight time is supplied. The current game comparison
remains the earlier **18 seconds on ce7006087f20**, with working FMVs and
remaining slowdown in approximately the first seven seconds of combat; do not
replace it with a fabricated new timing or the older 25/29-second baselines.

The resident's removed calls/spills remain a verified assembly improvement,
but these observations establish no measured gameplay benefit. The next
separate diagnostic, now under implementation, tests autonomous SCI reception
and useful CPU work during DMA, bounded blocked time, and timer/IRQ
responsiveness. It is not yet console-validated or enabled as asynchronous
game I/O. Keep all CRC checks, finite cleanup and resident memory/stack limits.
See the [asynchronous-read design and unresolved CE requirements](sci-async-and-ce-irq-2026-10-02.md).
