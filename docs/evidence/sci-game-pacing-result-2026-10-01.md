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
Audio remains pretty smooth. This is positive evidence for the first tested
pacing change. Unchanged runtime card throughput and improved game behavior
are consistent with delivering more data per game-service call; they do not
establish the exact enlarged-budget frequency or prove every remaining delay
is caused by storage.

No new menu-return counter photo accompanies this report. The earlier
6cc2abb460b5 photo still belongs to the parent build and must not be reused as
this build's counters. General audio smoothness is reported; an explicit FMV
check, broader game compatibility, long play and VMU
save/load acceptance were not reported. Windows CE remains separately gated.

Keep 8d930310f79d as the current comparison build for the next DOA2 counter
capture. Read/sector/enlarged-step counters can determine how often the new
policy actually grants larger batches. No additional Storage soak is needed
to assess this pacing policy.

## Provenance

| Original upload | Bytes | SHA-256 |
| --- | ---: | --- |
| result(9).json | 1535 | b59b19f43de10db6aa969f15168583f945fa17009c12646043470ae110cd81d6 |
| result(9).csv | 243 | 86907143a37f80f17117593d140b688ef9f8b93f2a61add30c0a7c57ddd104c0 |
