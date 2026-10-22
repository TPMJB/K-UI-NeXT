# SCI game pacing: first console result — 2026-10-01

The owner tested the measured-batching build **8d930310f79d** and supplied
Storage Soak **run 13**, plus a separate DOA2 timing and gameplay observation.
The source date uses the owner's America/Chicago date, October 1.

## Storage integrity and comparison

The [original JSON](sci-game-pacing-result-2026-10-01.json) and
[CSV](sci-game-pacing-result-2026-10-01.csv) are preserved byte-for-byte.
Both identify run 13 and build 8d930310f79d; all 17 exported CSV fields agree
with JSON. The same SCI/exFAT recipe as run 10 was used: 128 KiB clusters,
64 KiB requests, music off, 2 Hz UI and a five-minute soak target.

Run 13 wrote, remounted and verified **160 MiB across ten complete cycles**
in **303.722145 seconds**, with zero read/write/sync/init/timeout/CRC/rejected/I/O
errors, zero DMA failures, zero FatFs error and successful cleanup.

| Measurement | Run 10: 6cc2abb460b5 | Run 13: 8d930310f79d |
| --- | ---: | ---: |
| Write KiB/s | 1198.318378 | 1197.400078 |
| Read KiB/s | 1064.866121 | 1064.983369 |
| RX setup us/block | 1.937759 | 1.951248 |
| RX transfer us/block | 330.774344 | 330.799301 |
| RX reversal/CRC us/block | 68.483878 | 68.431604 |
| TX setup us/block | 20.325806 | 20.319718 |
| TX transfer us/block | 330.412331 | 330.423138 |
| Read calls over 100 ms | 91 | 90 |
| Write calls over 100 ms | 2 | 3 |

Writes changed -0.076632%; reads +0.011011%. These values are effectively
unchanged, as expected: the new pacing policy is in the game resident, not
this runtime Storage-test path. Phase timing is also unchanged. Read maximum
latency was 116.132 ms (previously 113.621); write maximum was 147.970 ms
(previously 148.893). Histogram p95 upper bounds remain 131.071 ms and are
not exact percentiles. No exact repeatability claim follows from one run.

Run 13 reports 327680 DMA reads, 328205 DMA writes and 1021 polled blocks.
All DMA reads are profiled and exactly cover the verified payload. The extra
writes/polled calls are not classified by these whole-run counters.

## Separate DOA2 observation

The owner reports **about 25 seconds from selecting Kasumi to the match
starting**, versus the earlier **29-second** first-fight baseline: about four
seconds, or **13.8% shorter**. Both times are user observations, not automated
instrumentation or repeated controlled trials.

The owner describes the fight as noticeably smoother, with only slight
slowdown during approximately its first ten seconds. They clarify that the
previous very slow movement also lasted roughly ten seconds and then resolved:
the observed improvement is its severity, not the duration of that interval.
Audio remains pretty smooth. These observations are retained, but the later
counter photograph below shows that no enlarged read budget activated during
the captured resident session. The earlier suggestion that this improvement
came from delivering more sectors per call is therefore unsupported. Its cause
is not established by these results. General audio smoothness is reported;
an explicit FMV check, broader game compatibility, long play and VMU save/load
acceptance were not reported. Windows CE remains separately gated.

## DOA2 return-screen photograph

The owner subsequently supplied `image-1790901205556.jpg`, displaying
`K-UI V1.5 GAME LAUNCH`, build `8D930310F79D`, `NATIVE GD IMAGE` and
`GAME MENU RETURN`. These values are hexadecimal:

| Displayed field | Hexadecimal | Decimal, for counters |
| --- | --- | ---: |
| CALLER PR | `8C016BAE` | — |
| CALLER STACK | `8C2B6C68` | — |
| GUARD FAULT | `00000000` | 0 |
| GD COMMAND | `00000011` | 17 |
| BLOCKS READ | `0000D549` | 54,601 |
| GD CALLS | `00004592` | 17,810 |
| EXEC CALLS | `00001941` | 6,465 |
| READ STEPS | `00001752` | 5,970 |
| SECTORS READ | `00002E68` | 11,880 |
| FRAMES SEEN | `00001073` | 4,211 |
| PACED STEPS | `00000000` | 0 |
| SPIN STEPS | `00000000` | 0 |
| STEP CALLER SR | `60000160` | — |

The reader delivered **1.989950 game sectors per read step**
(`11,880 / 5,970`). It granted **zero enlarged budgets on successful read
steps** and performed **zero spin-assisted steps**. No guard fault is recorded.
`PACED STEPS` counts an enlarged budget even if a short request tail consumes
fewer sectors, so zero is not merely a consequence of short requests.

The owner clarifies that the first button combination returned DOA2 to its
title screen; several more attempts were needed to reach this display.
The counters persist through normal game/GD soft resets: `retail_gd.c`'s
`reset()` and command INIT reset request/drive state without clearing
`service.diag`, and neither resets `pacing.paced` or `pacing.spun`. Resident
BSS clearing starts a fresh session only when the high stage installs the
reader. Thus the photograph covers the captured session, including title-screen
restarts; the resets do not explain away zero paced steps. A complete return
to K-UI and a new game launch would start a new resident session.

The totals cannot isolate the 25-second character-to-fight interval or be
compared directly with the earlier photograph's longer session. `FRAMES SEEN`
counts observed counter wraps, not wall-clock duration or exact EXEC frequency.
A title-screen reset may change the video mode and the latest timing estimate,
while cumulative counters persist. The photo does not show actual game DMA
use or the timing/geometry inputs needed to identify which budget condition
rejected enlargement. The last caller SR describes one reading call only.

Keep **8d930310f79d** as the comparison build. The next
[period correction and diagnostic candidate](sci-pacing-period-fix-2026-10-01.md)
reads the scanline counter period from `SPG_LOAD`, rejects measurements across
invalid or changed geometry, and exposes timing inputs on the return screen.
This is a source-level correction with console performance still pending;
it does not establish DOA2's exact rejection reason from the old photo.
No additional Storage soak is needed to assess this game-reader change.

## Provenance

| Original upload | Bytes | SHA-256 |
| --- | ---: | --- |
| result(9).json | 1535 | b59b19f43de10db6aa969f15168583f945fa17009c12646043470ae110cd81d6 |
| result(9).csv | 243 | 86907143a37f80f17117593d140b688ef9f8b93f2a61add30c0a7c57ddd104c0 |
| image-1790901205556.jpg | 220690 | 434cbbd6132e32ff72ed651cf71dbed2f10f1e6ff69aa24cbb1f7ea6a3269564 |

The photograph was inspected from the supplied local copy
`/workspace/scratch/e58804339be4/upload/01-image-1790901205556.jpg`.
