# SCIF 15-minute soak baseline — 2026-10-01

The owner supplied the completed report and designated this run as the SCIF
baseline for the upcoming SCI comparison. Runtime build **3a368ddcfaff**, run
**1**, nickname **scif**. The original uploads are retained byte-for-byte:
[result.json](scif-soak-baseline-2026-10-01-result.json) and
[result.csv](scif-soak-baseline-2026-10-01-result.csv).

Upload SHA-256 fingerprints:

- JSON: `773b4ce914cfa302815339a71a032799d4d941b2e1623ed31dc947ff0f58f39e`
- CSV: `4bac6a3af14d7759f19f9598c0077cf3d7a6a693380f7c510b21c0142f234c44`

## Accepted result

**Passed.** All 336 MiB written across 21 cycles verified after filesystem
remount. All reported transport and FatFs errors are zero; cleanup succeeded.

| Measurement | SCIF baseline |
| --- | ---: |
| Elapsed time | 907.070527 s (15 min 7.071 s) |
| Completed cycles | 21 |
| File per cycle | 16 MiB |
| Written / verified | 336 MiB / 336 MiB |
| Request size | 64 KiB |
| Write / read calls | 5,376 / 5,376 |
| Write throughput | 1,079.937 KiB/s |
| Read throughput | 612.316 KiB/s |
| Mean write / read call | 59.263 / 104.521 ms |
| Minimum write / read call | 52.228 / 93.247 ms |
| Maximum write / read call | 141.026 / 173.527 ms |
| Approximate p95 upper bound, write / read | 131.071 / 262.143 ms |
| Calls over 100 ms, write / read | 173 / 1,485 |
| Total measured sync time | 198.940 ms |
| Reported errors | 0 |

Rates are `sample.bytes / (sample.direction_us / 1e6) / 1024`, covering all
completed soak cycles. They time filesystem transfer calls; pattern work,
verification comparisons and flush time are excluded. The run exceeded the
15-minute target while finishing its last complete verification cycle.

The over-100-ms read count is not itself a retry/stall count: a normal 64 KiB
read at the measured rate takes approximately 105 ms. The p95 values are log2
histogram bucket ceilings, so the read ceiling can exceed the exact observed
maximum. This aggregate report does not show a time series or establish whether
throughput changed within the run.

## Conditions to retain for SCI

- Same runtime build: `3a368ddcfaff`.
- Same card where practical. Actual card model was not supplied; `scif` is the
  entered nickname, not a detected model.
- exFAT, 131,072-byte (128 KiB) allocation units.
- Volume starts at sector 2,048 and spans 249,997,312 sectors.
- Reported free space: 82,732,908,544 bytes.
- Soak preset, 15 minutes, 64 KiB requests; music off during measurement and
  UI limited to 2 Hz. The stored repeats setting is inactive for Soak.

Run Quick first after connecting SCI, then repeat this Soak recipe. Preserve
`/KUI/tests/` to retain on-device reports and any baseline selected with Y.
The owner's designation records the project baseline; the upload alone does
not confirm that the console's Y baseline action was performed.

This accepts runtime SCIF filesystem integrity and throughput for the supplied
run. It does not accept SCI/IDE, exercise retail Games readers, or validate the
entire card's capacity. No repeat of this accepted SCIF soak is required.
