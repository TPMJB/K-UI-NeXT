# SCI DMA soak and DOA2 improvement — 2026-10-01

The owner supplied run **4**, runtime **cf8e7ea7866b**, after installing the
matching SD update. The original uploads are preserved byte-for-byte:
[JSON](sci-dma-soak-2026-10-01-result.json) and
[CSV](sci-dma-soak-2026-10-01-result.csv).

Upload SHA-256 fingerprints:

- JSON: `84ca02acab2afb7b5f675a9007d50c0e256220de8255969efbe72c7a29e2735d`
- CSV: `31df021e0ff28eb0c6d5e437015f30a7c5b49cabc6266aede9fe0f2be910450c`

## Accepted measurements

**Passed:** 26 complete cycles, 416 MiB written and verified after remount in
917.692062 seconds (15 min 17.692 s). All transport and FatFs errors are zero;
cleanup succeeded. JSON and CSV agree, and sample times match latency totals.

| Measurement | SCIF baseline | Original SCI | SCI DMA |
| --- | ---: | ---: | ---: |
| Runtime build | 3a368ddcfaff | 3a368ddcfaff | cf8e7ea7866b |
| Write KiB/s | 1,079.937 | 521.747 | 1,004.619 |
| Read KiB/s | 612.316 | 528.753 | 926.115 |
| Completed cycles | 21 | 15 | 26 |
| Written and verified MiB | 336 | 240 | 416 |
| Elapsed seconds | 907.071 | 955.936 | 917.692 |

Relative to the original SCI implementation, writes improved **92.55%** and
reads **75.15%**. Relative to SCIF, reads improved **51.25%**, while writes
remain **6.97% lower**. Rates time filesystem transfer calls, excluding pattern
generation, verification comparisons and sync. See the original
[SCIF](scif-soak-baseline-2026-10-01.md) and
[SCI](sci-soak-baseline-2026-10-01.md) reports for their complete conditions.

The recipe remains 15-minute Soak with 64 KiB calls and 16 MiB per cycle,
exFAT/128 KiB clusters, music off and 2 Hz UI. Volume geometry matches both
baselines. Build differs intentionally. Physical card identity is not encoded
in the reports; the entered nickname is `sci`.

## DMA evidence and latency

The saved message records **851,968 DMA reads, 853,377 DMA writes, 3,063 polled
block calls and zero DMA faults**. The DMA read count exactly equals verified
payload bytes divided by 512. DMA writes exceed payload sectors by 1,409;
filesystem and test-marker operations occur within the counted interval.
The counters establish actual DMA use, but do not identify each operation's
purpose. Polling includes metadata/short buffers and is not a transfer-error
count. Final report saving occurs after the counter snapshot.

| 64 KiB filesystem call | Write | Read |
| --- | ---: | ---: |
| Calls | 6,656 | 6,656 |
| Mean | 63.706 ms | 69.106 ms |
| Minimum | 56.188 ms | 62.435 ms |
| Maximum | 157.238 ms | 130.972 ms |
| Calls over 100 ms | 295 (4.43%) | 548 (8.23%) |
| Approximate p95 upper bound | 131.071 ms | 131.071 ms |

The histogram bound is not an exact percentile. Long complete calls are not
proof of retries or stalls, and this aggregate report cannot locate a delay
within the driver, card or filesystem.

The configured 12.5 MHz clock has a raw ceiling of 1.5625 MB/s, or
1,525.879 KiB/s. Measured write/read rates are 65.84%/60.69% of that ceiling.
Remaining bandwidth is not automatically recoverable: card responses, framing
and software work still take time.

## Gameplay and next optimization

The owner reports that Dead or Alive 2 is **much better** with this update,
with a tiny amount of lag remaining in some areas. No load-time measurement,
separate FMV/VMU acceptance or retail build report accompanied this observation.
It supports an improvement for this title, not disc-equivalent behavior or
compatibility across other games.

The next candidate targets proven extra CPU work: combine receive bit reversal
and CRC calculation using the caller's cache after DMA completion, and overlap
write CRC calculation with active transmit DMA. Both existing CRC algorithms
already use fast byte arithmetic; no per-bit payload-CRC defect is present.
Keep integrity checks, bounded failures, channel ownership and protected
resident limits. Keep game pacing unchanged for this comparison so any new
speed improvement can be attributed to the transfer path. Its effect on the
remaining DOA2 lag still needs a console check.
