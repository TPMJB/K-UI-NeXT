# SCI cached processing Quick result — 2026-10-01

The owner supplied run **5**, runtime **a6cb21895c37**, and reported that it
was a little faster. Original uploads are preserved byte-for-byte:
[JSON](sci-dma-cached-quick-2026-10-01-result.json) and
[CSV](sci-dma-cached-quick-2026-10-01-result.csv).

- JSON SHA-256: `1cb2dc8335d1b53d403cad2a86189262f5086d6f5cf27ac616a6b256fe91e2e4`
- CSV SHA-256: `12ef3cde469df541b7a8a50770e24d6f87d44be925b85a0b544f5b0de5bfbee5`

## Accepted observation

**Quick passed:** 4 MiB written, remounted and verified in 8.739782 seconds,
with zero transport/FatFs errors, zero DMA faults and successful cleanup.
JSON and CSV agree. This is preset 0 (Quick); the stored inactive
`soak_minutes:15` setting does not make it a soak.

| Measurement | Previous DMA Soak | Cached processing Quick |
| --- | ---: | ---: |
| Runtime | cf8e7ea7866b | a6cb21895c37 |
| Write KiB/s | 1,004.619 | 1,104.348 |
| Read KiB/s | 926.115 | 1,052.725 |
| Written and verified MiB | 416 | 4 |
| 64 KiB calls per direction | 6,656 | 64 |
| Complete cycles | 26 | 1 |

The observed run-to-run differences are **+9.93% writes and +13.67% reads**.
They are encouraging preliminary evidence, not a controlled sustained gain:
Quick and Soak have different durations and total data sizes. Both use
64 KiB calls, exFAT/128 KiB clusters, music off and a 2 Hz UI; volume geometry
matches. Do not replace the accepted long-run baseline with this short check.

DMA counts are 8,192 reads, 8,483 writes, 482 polled block calls and zero
faults. Reads exactly equal the verified payload sector count. The 291 extra
write sectors and polling include operations outside the timed payload calls;
the counters do not identify their individual purposes. Mean write/read call
times are 57.953/60.795 ms, maxima 152.703/104.128 ms. Each direction has two
calls above 100 ms. No separate game timing or new title acceptance accompanies
this report.

## Review of the supplied optimization notes

The notes reference **cf8e7ea**, before the changes in the tested build:
cached receive processing, fused reversal/CRC and transmit CRC during DMA are
already implemented in **a6cb218**. Payload CRC already uses byte arithmetic.
The normal runtime inherits `-O2` from the pinned KOS environment; the cited
Makefile `-Os` rule is specific to lwext4. The small game resident uses `-Os`.

Subtracting the ideal 327.68 microseconds for 512 bytes from filesystem-call
averages leaves aggregate overhead, not measured time with the clock stopped.
It cannot distinguish payload feeding gaps, card/token waits, protocol work,
filesystem costs and CPU processing. The suggested 1,300–1,450 KiB/s range is
a target, not an established prediction. The first next measurement should
separate DMA setup, transfer and receive processing at their boundaries.

The reflected CRC identity is valid for polynomial 0x8408, zero seed and no
final XOR, operating on raw SCI bytes. Existing scalar reads already reverse
trailer bytes; any reflected implementation must preserve the logical CRC
returned by the block callback. A table also costs 512 bytes, which the game
resident's existing reservation cannot casually absorb.

The pre-DMA purge already invalidates all complete isolated buffer lines, and
the CPU does not access them while DMA owns them. Additional invalidation
after corrected bytes become dirty would discard data. Cross-sector pipelining
requires a new buffer/ownership and error-cleanup design. Keep CRC verification
before successful return, real runtime deadlines, bounded loops and the
existing channel/IRQ and resident memory limits.

The SH7750 hardware permits internal SCI DMA requests on channels 0–3
(manual tables 14.5 and 14.7); KOS channel reservations are a separate ownership
constraint. This does not authorize borrowing a second channel during games.
Reference: [Renesas hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware).
All new implementation remains original; no SWAT/DreamShell driver code is used.
