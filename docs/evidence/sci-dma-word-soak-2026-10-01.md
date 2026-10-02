# SCI word-reversal build: soak and DOA2 observation — 2026-10-01

The owner's **run 8**, runtime **499bcb53c2d4**, passed the same five-minute
Soak recipe as [run 7](sci-dma-profile-2026-10-01.md). This build includes
four-byte reversal and the subsequent module-wake delay correction. The
original `result(6)` uploads are preserved byte-for-byte, including CSV CRLF:
[JSON](sci-dma-word-soak-2026-10-01-result.json) and
[CSV](sci-dma-word-soak-2026-10-01-result.csv).

| Original upload | Bytes | SHA-256 |
| --- | ---: | --- |
| `result(6).json` | 1,535 | `96dd985c98206bbb4183ecb314a2683f2701b7ec6e313a945028d2856c1f182a` |
| `result(6).csv` | 243 | `dbe034e6598942a5b97ddf39b2f15372f1b77313d9d5c5339945fadfa60705d7` |

## Accepted runtime result

Run 8 wrote, remounted and verified **160 MiB across 10 complete cycles** in
**306.434992 seconds**, with zero transport/FatFs errors, zero DMA faults and
successful cleanup. JSON and CSV agree on every exported field. Both runs use
SCI, exFAT with 128 KiB clusters, 64 KiB requests, music off and a 2 Hz UI.
The five-minute target, repeat setting and volume geometry match. Run 8's
nickname is blank and reported free space is 640 KiB lower; this comparison
does not control card temperature, allocation or all other session conditions.

| Measurement | Run 7: 65fcaafadb98 | Run 8: 499bcb53c2d4 |
| --- | ---: | ---: |
| Written and verified MiB | 160 | 160 |
| Complete cycles | 10 | 10 |
| Elapsed seconds | 315.049953 | 306.434992 |
| Write KiB/s | 1,132.602179 | 1,196.641076 |
| Read KiB/s | 1,044.269606 | 1,048.894394 |
| 64 KiB calls per direction | 2,560 | 2,560 |
| Mean write/read call, ms | 56.507 / 61.287 | 53.483 / 61.017 |
| Minimum write/read call, ms | 49.707 / 55.073 | 47.626 / 54.962 |
| Maximum write/read call, ms | 151.133 / 115.350 | 147.424 / 117.411 |
| Write/read calls above 100 ms | 40 / 92 | 2 / 73 |
| Flush time, ms | 81.947 | 101.296 |

Writes are **5.65% faster** and reads **0.44% faster** in this pair of runs.
Timed writes save 7.741453 seconds and timed reads 0.691779 seconds over the
same payload. Both directions retain a histogram p95 upper bound of 131.071 ms;
that bound is not an exact percentile, and long calls are not necessarily
retries. This accepts the new runtime's integrity and observed write gain;
the small read-rate difference does not establish a repeatable read gain.

## Phase measurements

| Mean microseconds per successful DMA sector | Run 7 | Run 8 |
| --- | ---: | ---: |
| RX setup | 1.727475 | 1.828085 |
| RX transfer | 331.018472 | 330.944342 |
| RX reversal and CRC check | 77.179285 | 78.181149 |
| TX setup, including reversal and purge | 35.958566 | 20.212185 |
| TX transfer, including overlapped CRC | 330.303594 | 330.461132 |

The clearest phase change is **TX setup falling 43.79%**, saving
15.746381 microseconds per profiled write sector. This is consistent with
faster write preparation. The TX transfer phase is effectively unchanged.
The profile does not attribute the entire filesystem write gain to setup.

**RX processing did not improve:** the reversal/CRC phase increased by
1.001865 microseconds per sector, or 1.30%, while transfer stayed near
331 microseconds. The earlier 1,150 KiB/s read target has not been reached.
Do not describe four-byte reversal as a measured receive-side speedup.

Run 8 has 327,680 DMA reads, exactly the sector count for 160 MiB; all are
profiled. Its 328,266 profiled DMA writes exceed payload by 586 sectors, with
1,101 polled block calls and zero DMA failures. Extra operations are not
individually classified by these whole-run counters.

Timed file reads average **476.692413 microseconds per sector**. Measured
RX phases sum to **410.953577**, leaving **65.738837 microseconds** outside
those phases, compared with 68.878323 in run 7. The residual includes channel
restoration, framing, waits, polling and filesystem/runtime work; it cannot
be assigned solely to the card. See the [profile scope](sci-dma-profile-2026-10-01.md)
before interpreting the phase totals as whole-operation costs.

## Separate DOA2 observation and counter capture

For the current game test, the owner reports **29 seconds from selecting
Kasumi to the first fight**. This exact boundary is recorded as a user-timed
observation. The earlier roughly 15-second SWAT result was unstable and lacks
matched conditions and a confirmed identical timing boundary; it is not a
controlled before/after measurement of this K-UI build.

The owner reports that A+B+X+Y+Start usually restarts the game. They eventually
triggered the return path but could not capture its roughly two-second counter
screen. No counter values or photograph are available, so actual game DMA
eligibility, batch sizes and module-standby state remain unmeasured. A retry
was offered and deferred until the counter display is easier to capture.

The runtime soak does not exercise the resident's repeated acquire/release
pattern and does not prove the module-wake correction improved game loading.
See the [game-loading review](sci-game-loading-review-2026-10-01.md). No new
game speed change, wake-state diagnosis or broad compatibility acceptance is
established by these reports.

## Follow-up candidate: grouped CRC and readable return counters

The next reader processes 128 words. Each word is reversed and stored once;
its four logical bytes then feed CRC directly from the local word, in memory
order for the target endianness. This removes the 512 per-byte word-boundary
tests and 512 post-store byte reloads retained in build 499bcb53c2d4.

Native runtime -O2 assembly confirms inline reversal/CRC without spills or
unaligned access. The counted RX-loop instructions fall from 17,920 to 15,872
per sector; instruction counts are not measured execution time. Successful
CRC publication, the post-DMA memory barrier and bounded cleanup are unchanged.
No active-DMA CPU access, lookup table or extra buffer is introduced.

Independent ASan/UBSan runs at -O1 and -O2 pass an oracle-based CRC/data test
covering every byte value in each word position and 36 mixed patterns, plus
the existing ownership, timeout, profiling and TX-offset tests. Native normal
and benchmark instruction/layout/stack audits pass unchanged limits: SCI
payload 11,168 bytes, end 0x8c00bae8 (24 bytes free), stack 1,180/1,232 bytes.
SCIF/IDE retain their stack limits. Console gain remains unverified.

The BIOS menu-return counter screen now waits 900 video frames: approximately
15 seconds at 60 Hz or 18 seconds at 50 Hz. The finite polling allowance resets
per observed frame, allowing the longer display while still bounding a stopped
scan generator. K-UI then reboots as before. This does not override a game's
internal restart handling of A+B+X+Y+Start.

First run Quick once and inspect RX check time before spending another full
soak. For game pacing, record the same Kasumi-to-first-fight time and film the
return counter screen. Do not change read pacing from a timing anecdote alone.
