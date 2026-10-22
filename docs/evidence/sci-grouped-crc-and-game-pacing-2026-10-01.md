# SCI grouped CRC and DOA2 pacing evidence — 2026-10-01

The owner's Quick **run 9** and five-minute Soak **run 10** both identify
runtime **6cc2abb460b5**. The return-screen photograph independently identifies
the same game-reader build. These observations follow
[run 8 on 499bcb53c2d4](sci-dma-word-soak-2026-10-01.md).

## Original evidence

The new exports are preserved byte-for-byte, including CSV line endings:

| Upload | Accepted copy | Bytes | SHA-256 |
| --- | --- | ---: | --- |
| `result(7).json` | [Soak JSON](sci-grouped-crc-and-game-pacing-2026-10-01-soak-result.json) | 1,543 | `038d2f6b3c90435d2a8c170eef8d191889f785e7f2e7fe3aa1da2c7bb2c11535` |
| `result(7).csv` | [Soak CSV](sci-grouped-crc-and-game-pacing-2026-10-01-soak-result.csv) | 251 | `3e2398683c4458f77fae8c4b7856e73a6702f3bec4e7691121f1faf7a1357029` |
| `result(8).json` | [Quick JSON](sci-grouped-crc-and-game-pacing-2026-10-01-quick-result.json) | 1,486 | `a1c7d5e765593f59f3fd8c2673796d0bb946ff96e921f0c4f85ce46de78beb11` |
| `result(8).csv` | [Quick CSV](sci-grouped-crc-and-game-pacing-2026-10-01-quick-result.csv) | 235 | `1dc5d952cda29d7b0a0338e92819ed5b2715a5c6ec56f2432272f4249acd3965` |

Original owner-supplied photograph: `61618.jpg`, **210,875 bytes**,
SHA-256 `676238aa1b09ccc8d4b8159930a07f44bf19a479e2931a370aa5efc1a46dcc50`.
The photograph is retained outside the source repository; its transcription
appears below.

## Runtime integrity and throughput

Both new runs use SCI, exFAT with 128 KiB clusters, 64 KiB requests, music off
and a 2 Hz UI. Volume geometry agrees with run 8. The soak's nickname is
`sci dma3`; Quick's nickname is blank. Relative to run 8, reported free space
is 640 KiB lower for Quick and 1,280 KiB lower for the new soak. Allocation,
card temperature and all other session conditions were not controlled.

Each new CSV agrees with its JSON on all 17 exported fields. Both runs report
zero read, write, sync, initialization, timeout, CRC, rejected-operation and
I/O errors; zero DMA failures; zero FatFs error; successful cleanup; and
successful verification after remount. Quick has `preset: 0`; its saved
`soak_minutes: 15` setting does not make it a 15-minute soak.

| Measurement | Prior soak: run 8 | New Quick: run 9 | New soak: run 10 |
| --- | ---: | ---: | ---: |
| Build | 499bcb53c2d4 | 6cc2abb460b5 | 6cc2abb460b5 |
| Written and verified MiB | 160 | 4 | 160 |
| Complete cycles | 10 | 1 | 10 |
| Elapsed seconds | 306.434992 | 8.573500 | 303.846006 |
| Write KiB/s | 1,196.641076 | 1,138.966732 | 1,198.318378 |
| Read KiB/s | 1,048.894394 | 1,068.193014 | 1,064.866121 |
| 64 KiB calls per direction | 2,560 | 64 | 2,560 |
| Mean write/read call, ms | 53.483 / 61.017 | 56.191 / 59.914 | 53.408 / 60.101 |
| Minimum write/read call, ms | 47.626 / 54.962 | 47.693 / 53.698 | 47.647 / 53.694 |
| Maximum write/read call, ms | 147.424 / 117.411 | 148.728 / 105.945 | 148.893 / 113.621 |
| Write/read calls above 100 ms | 2 / 73 | 1 / 3 | 2 / 91 |
| Flush time, ms | 101.296 | 7.659 | 93.114 |

The matched-size soak comparison is **+0.140168% writes** and
**+1.522720% reads**. Timed file reads fall from 156.202570 to 153.859717
seconds, saving 2.342853 seconds. One soak per build does not establish a
repeatable whole-file gain of that exact size. The count of read calls over
100 ms increased, although the maximum decreased. All latency histograms
retain a p95 upper bound of 131.071 ms; this is not an exact percentile.

## Measured phase change

These timings cover successful DMA payloads, not complete filesystem calls.

| Mean microseconds per successful DMA sector | Prior soak: run 8 | New Quick: run 9 | New soak: run 10 |
| --- | ---: | ---: | ---: |
| RX setup | 1.828085 | 1.950562 | 1.937759 |
| RX transfer | 330.944342 | 330.566284 | 330.774344 |
| RX reversal and CRC check | 78.181149 | 68.567627 | 68.483878 |
| TX setup, including reversal and purge | 20.212185 | 20.240693 | 20.325806 |
| TX transfer, including overlapped CRC | 330.461132 | 330.444863 | 330.412331 |

The grouped CRC change reduced the measured RX check phase by
**9.697272 microseconds per sector (12.403593%)** in the soak. Quick agrees
closely with the new soak's check timing. RX transfer and both TX phases are
essentially unchanged. This is measured evidence that the intended check
optimization worked, despite its small effect on whole-file throughput.

Across 327,680 received sectors, the check phase saves **3.177602 seconds**.
The summed RX phases decrease from 410.953577 to **401.195981 microseconds
per sector**. Timed file reads decrease from 476.692413 to **469.542593**,
leaving an unclassified residual of **68.346613**, versus 65.738837 previously.
The residual includes work outside these three scopes; it cannot be assigned
solely to the card or filesystem. See the
[profile scope](sci-dma-profile-2026-10-01.md).

The new soak reports 327,680 DMA reads, exactly its payload sector count,
328,355 DMA writes, 1,221 polled block calls and no DMA faults. Quick reports
8,192 DMA reads, also exactly its payload sector count, 8,488 DMA writes,
496 polled block calls and no DMA faults. All reported DMA blocks are
profiled. Extra writes and polled operations are not individually classified
by these whole-run counters. Runtime counters do not prove that every game
read uses DMA.

## DOA2 return-screen transcription

The photograph shows `K-UI V1.5 GAME LAUNCH`, build `6CC2ABB460B5`,
`NATIVE GD IMAGE`, and `GAME REQUESTED MENU RETURN`. Values on screen are
hexadecimal. They are cumulative since launch, including any internal game
restarts before reaching this return screen.

| Displayed field | Hexadecimal | Decimal, for counters |
| --- | --- | ---: |
| CALLER PR | `8C016BAE` | — |
| CALLER STACK | `8C2B6C68` | — |
| GUARD FAULT | `00000000` | 0 |
| GD COMMAND | `00000011` | 17 |
| BLOCKS READ | `00012E18` | 77,336 |
| GD CALLS | `000060DD` | 24,797 |
| EXEC CALLS | `0000264F` | 9,807 |
| READ STEPS | `00002112` | 8,466 |
| SECTORS READ | `000041B8` | 16,824 |
| FRAMES SEEN | `000018C0` | 6,336 |
| PACED STEPS | `00000000` | 0 |
| SPIN STEPS | `00000000` | 0 |
| STEP CALLER SR | `60000100` | — |

The reader delivered **1.987243 game sectors per read step**
(`16,824 / 8,466`), almost exactly the baseline two-sector budget. It granted
**no enlarged pacing budgets** and performed **no spin-assisted steps**.
This establishes that the existing larger-batch path was unused during this
session. It supports investigating the game reader's batching policy rather
than expecting another small storage-rate gain to eliminate the load delay.
The sector value corrects an initial chat transcription of `4188`; the
photograph shows `41B8`. This correction does not change the batching finding.

The counters do not isolate character selection through the first fight.
`FRAMES SEEN` counts observed scanline wraps, not a continuously sampled wall
clock, so it must not be converted into a session duration or combined with
the launch-wide sector total to claim a transfer rate. `EXEC CALLS` and
`FRAMES SEEN` do not prove one call per frame. Zero paced steps identifies an
unused path; it does not by itself prove exactly which pacing condition
prevented that path on every call. The last caller SR has interrupt mask bits
4–7 clear, but one saved value does not describe all prior callers.

The earlier owner-timed **29 seconds from selecting Kasumi to the first
fight** remains the prior-build baseline. No new equivalent elapsed time was
supplied with this photograph. Neither this photograph nor the storage tests
establishes a new game-load time or broad compatibility result.

## Next candidate and acceptance limits

The next candidate uses a time budget for game read batching, motivated by
the observed two-sector limit and unused larger-batch path. That candidate
is **not measured on the console by any evidence in this report**. No loading
speed, frame-pacing or compatibility gain is guaranteed, and this report does
not accept an untested candidate as the new gameplay baseline.

After the candidate passes its software and native-build checks, compare the
same Kasumi-selection-to-first-fight interval, then inspect the first seconds
of combat, speech and an FMV for regressions. Capture a new return-screen
photograph from that build. Keep CRC on every sector, bounded transport
operations, error cleanup, and the resident memory/stack limits. The accepted
runtime integrity results here remain separate from the next game's batching
experiment.
