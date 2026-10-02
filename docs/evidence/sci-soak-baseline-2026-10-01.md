# SCI 15-minute soak baseline — 2026-10-01

The owner supplied the first completed SCI microSD soak report. Runtime build
**3a368ddcfaff**, run **2**, nickname **sci**. The original uploads are retained
byte-for-byte: [result.json](sci-soak-baseline-2026-10-01-result.json) and
[result.csv](sci-soak-baseline-2026-10-01-result.csv).

Upload SHA-256 fingerprints:

- JSON: `bb2a5b74de3a08e770132fd879d7a5556f8fc1e5fa953d4486c5b7599c698540`
- CSV: `df0bfb2b3e3502ee8ea61601a6883096791b90d6c0edc015036f2f07f09bdfc7`

## Accepted result

**Passed.** All 240 MiB written across 15 cycles verified after filesystem
remount. All reported transport and FatFs errors are zero; cleanup succeeded.
This accepts runtime SCI filesystem integrity for this run. Its measured
throughput is lower than the accepted
[SCIF baseline](scif-soak-baseline-2026-10-01.md); it is not a speedup result.

| Measurement | SCIF baseline | SCI baseline |
| --- | ---: | ---: |
| Elapsed time | 907.070527 s (15 min 7.071 s) | 955.936465 s (15 min 55.936 s) |
| Completed cycles | 21 | 15 |
| File per cycle | 16 MiB | 16 MiB |
| Written / verified | 336 MiB / 336 MiB | 240 MiB / 240 MiB |
| Request size | 64 KiB | 64 KiB |
| Write / read calls | 5,376 / 5,376 | 3,840 / 3,840 |
| Write throughput | 1,079.937 KiB/s | 521.747 KiB/s |
| Read throughput | 612.316 KiB/s | 528.753 KiB/s |
| Mean write / read call | 59.263 / 104.521 ms | 122.665 / 121.040 ms |
| Minimum write / read call | 52.228 / 93.247 ms | 108.524 / 108.029 ms |
| Maximum write / read call | 141.026 / 173.527 ms | 293.726 / 181.576 ms |
| Approximate p95 upper bound, write / read | 131.071 / 262.143 ms | 262.143 / 262.143 ms |
| Calls over 100 ms, write / read | 173 / 1,485 | 3,840 / 3,840 |
| Total measured sync time | 198.940 ms | 119.620 ms |
| Reported errors | 0 | 0 |

SCI write throughput is **51.69% lower** and read throughput **13.65% lower**.
Rates are `sample.bytes / (sample.direction_us / 1e6) / 1024`, covering all
complete verified cycles. They time filesystem transfer calls, excluding
pattern work, verification comparisons and flush time. Both runs finish their
last verification cycle after the 15-minute target.

Every SCI 64 KiB call exceeded 100 ms, but this is not a count of abnormal stalls
or retries: ordinary requests took about 121–123 ms at these measured rates.
The p95 values are coarse log2 histogram bucket ceilings, not exact percentiles.
The report contains aggregate statistics, not a time series; it does not locate
the source of the performance difference or establish whether speed changed
within either run.

## Comparable conditions

Both reports use build `3a368ddcfaff`, the 15-minute Soak preset, 64 KiB requests,
exFAT with 131,072-byte (128 KiB) allocation units, music off during measurement,
and a 2 Hz UI. The volume starts at sector 2,048 and spans 249,997,312 sectors
in both reports. SCI free space was 82,732,122,112 bytes versus
82,732,908,544 bytes on SCIF, a difference of 786,432 bytes. The stored repeats
setting is inactive for Soak.

The transport IDs are 0 (SCIF) and 1 (SCI). Nicknames are user-entered, not
detected card models. Matching volume geometry is consistent with the same
card but does not itself identify the physical card. No on-device baseline
selection is inferred from the uploads.

## Initial performance investigation

Source inspection found that SCI payloads use the generic full-duplex byte
callback in `src/loader/sci_sd_bus.c`. Each byte performs software bit reversal,
control/status register accesses and polling, and waits for receive completion
before returning to the caller. Writes also receive a byte that is discarded.
There is no bulk SCI payload operation or DMA in this implementation. The
runtime SCIF path instead uses KOS's specialized bulk read/write routines.

The SCI fast setting is BRR=0 with a nominal 50 MHz peripheral clock:
12.5 Mbit/s, or 1.5625 MB/s before overhead. The measured read rate is about
0.541 MB/s. Per-byte software overhead is a leading explanation, not a profiled
root cause; the soak does not separately measure bus idle time, card waits or
CPU work. Runtime CRC already uses the fast algebraic update. Do not attribute
the result to a slow per-bit CRC loop or to a proven hardware/wiring limit.
See the [SH7750 hardware manual, section 15](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware)
for SCI timing and transfer constraints; continuous operation at BRR=0 must
not be assumed without checking those constraints.

## Scope and next check

The runtime SCI soak passes its integrity check and supplies an initial
performance baseline. Retail Games readers are separate programs; this result
does not accept the SCI game reader, prove a particular bootstrap path or
validate the entire card's capacity. IDE/CF remains hardware pending.

The next game compatibility check is the same known-good **Dead or Alive 2** image with a matching,
SCI-capable `/KUI/apps/games/retail-boot.kui`; updating only `runtime.kui` does
not replace that game loader. For the first run, record character selection to
the first fight, the first ten seconds of gameplay, one FMV and return to K-UI
with A+B+X+Y+Start. Compare against the accepted
SCIF behavior using the same image. Keep the current SCI throughput figures
as measured; do not predict faster gameplay from this soak. Evolution 2 can
follow if DOA2 is stable, along with VMU save/load and further transitions.
Performance investigation remains open. No repeat of the accepted soaks is required before
this first game compatibility check.
