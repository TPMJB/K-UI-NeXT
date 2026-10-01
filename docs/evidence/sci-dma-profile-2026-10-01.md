# SCI DMA phase measurements — 2026-10-01

The owner supplied two passing runtime reports from **65fcaafadb98**. Upload
`result(5)` is **run 6, Quick**; `result(4)` is **run 7, five-minute Soak**.
The original JSON/CSV pairs are preserved byte-for-byte, including CSV CRLF
line endings:

- Run 6: [Quick JSON](sci-dma-profile-2026-10-01-quick-result.json) and
  [Quick CSV](sci-dma-profile-2026-10-01-quick-result.csv).
- Run 7: [Soak JSON](sci-dma-profile-2026-10-01-soak-result.json) and
  [Soak CSV](sci-dma-profile-2026-10-01-soak-result.csv).

| Original upload | Bytes | SHA-256 |
| --- | ---: | --- |
| `result(5).json` | 1,494 | `762bb13b6eeb257c07df9f0ea01a56a4034479136ab03be72372f021a0b0a43d` |
| `result(5).csv` | 243 | `762a7253870f207ba809e30e3e60c12bddd7a240c38db9ae29350625cd4d2780` |
| `result(4).json` | 1,544 | `944b58af9b27dc7ea7b98f06e6036fa46a21044d0b42cf5a8f7d85042a726da5` |
| `result(4).csv` | 250 | `53c2189a6ab77ac67aa127cf9f0f18b84f1fc141af3c3b2475010467eb4c329f` |

## Accepted results and comparison

Both runs wrote, remounted and verified every reported byte, with zero
transport/FatFs errors, zero DMA faults and successful cleanup. Each JSON
matches its CSV. Quick is preset 0; its inactive `soak_minutes:15` value does
not change the preset. Soak is preset 2 and completed the current cycle beyond
its five-minute target.

| Measurement | Run 6 Quick | Run 7 Soak |
| --- | ---: | ---: |
| Complete cycles | 1 | 10 |
| Written and verified MiB | 4 | 160 |
| Elapsed seconds | 8.820040 | 315.049953 |
| Write KiB/s | 1,097.417799 | 1,132.602179 |
| Read KiB/s | 1,037.031336 | 1,044.269606 |
| 64 KiB calls per direction | 64 | 2,560 |
| Mean write/read call, ms | 58.319 / 61.715 | 56.507 / 61.287 |
| Maximum write/read call, ms | 123.153 / 104.995 | 151.133 / 115.350 |
| Write/read calls above 100 ms | 4 / 3 | 40 / 92 |

The latest Quick is **0.63% slower writing and 1.49% slower reading** than
the [a6cb218 Quick](sci-dma-cached-quick-2026-10-01.md). This small difference
between single runs does not establish a regression: the new build also adds
timing instrumentation, and uncontrolled run conditions may differ. It does
not demonstrate a speed gain from the latest feed change either.

The latest Soak is **12.74% faster writing and 12.76% faster reading** than
the [cf8e7ea DMA soak](sci-dma-soak-2026-10-01.md). That comparison spans both
the cached processing and feed/profile changes, with **five versus fifteen
minutes** and 160 versus 416 MiB. It is observed run-to-run evidence, not a
controlled comparison that isolates either change.

All these runs use 64 KiB requests, SCI, exFAT with 128 KiB clusters, music
off and a 2 Hz UI. Volume start 2,048 and size 249,997,312 sectors match.
Free space and the user-entered nickname differ. The latency p95 upper bound
is 131.071 ms for both directions in both new reports; this histogram bound
is not an exact percentile or a count of retries.

## Measured DMA phases

All successful DMA blocks in both new runs were profiled. RX counts exactly
match the verified payload: 8,192 sectors in Quick and 327,680 in Soak. TX
counts are 8,497 and 328,475, respectively 305 and 795 sectors above written
payload. Polling counts are 475 and 1,343. These whole-run counters also cover
operations outside the timed file calls; they do not identify each extra
operation or establish a byte fraction for variable-length polling transfers.

| Mean microseconds per successful DMA sector | Quick | Soak |
| --- | ---: | ---: |
| RX setup | 1.728516 | 1.727475 |
| RX transfer | 331.048096 | 331.018472 |
| RX reversal and CRC check | 76.971802 | 77.179285 |
| TX setup, including reversal and purge | 35.825586 | 35.958566 |
| TX transfer, including overlapped CRC | 329.792162 | 330.303594 |

The phase boundaries in the tested
[SCI driver](../../src/loader/sci_sd_bus.c) exclude channel eligibility and
restoration, commands, token/busy waits, polling fallback and filesystem work.
Transfer includes CPU feeding, completion checks and shutdown; it is not a
direct measurement of time with the clock active. Timing calls add overhead.
The driver's nominal 12.5 Mbit/s clock gives an ideal 512-byte payload duration
of 327.68 microseconds, so the measured 331-microsecond RX phase is already
close to that nominal floor.

For Soak, file reads average **478.803555 microseconds per sector**. Measured
RX phases sum to **409.925232**, leaving **68.878323 microseconds per sector**
(22.570049 seconds total) outside those phases. That residual cannot be
assigned solely to card delays, CRC or CPU feeding. Quick gives a similar
409.748413-microsecond phase sum and a 72.397095-microsecond residual.

## Implications for the next experiment

The measured RX reversal/CRC pass accounts for **16.12%** of Soak's timed
reads and is a supported optimization target. The combined measurement does
not separate reversal from CRC. The follow-up candidate reverses four bytes at a time in the shared runtime/
game reader and aligned runtime write staging, with byte staging for unaligned
sources. It retains the per-byte algebraic CRC and checks after DMA stops.
No lookup table or extra buffer is added. Console results for this candidate
are pending; processing completed cache lines during DMA remains a proposal. Active-DMA processing would require a separate review
of completed-line ownership, cache behavior, bounded clock feeding and error
cleanup; its overhead could lengthen the currently near-ideal transfer phase.

The suggested **1,150 KiB/s** target requires reducing the current
478.803555-microsecond file-read average to 434.782609: **44.020947 microseconds
saved per sector**, about **57.04%** of the measured check phase. Completely
hiding that phase while holding all other costs fixed would yield about
1,244.94 KiB/s. These are conditional arithmetic estimates, not promised
throughput or evidence that the check can be removed or hidden at no cost.

These reports accept runtime integrity and the measured phase totals for
these runs. They do not benchmark the separate retail reader or provide new
DOA2 load-time/gameplay evidence. Preserve matching runtime and
`KUI/apps/games/retail-boot.kui` build identity when comparing Games behavior.

## Candidate validation and overlap review

The independent word-reversal implementation uses three byte-preserving
shift/mask swaps. Aligned word accesses use a local GCC may-alias type; the
receive buffer already requires 32-byte alignment. CRC still visits every
logical byte in memory order before publication. The first bounded TDRE wait
now shares the feed loop, preserving one TDR seed and exactly 512 clocks.

Focused ASan/UBSan tests pass, including aligned and all three unaligned TX
source offsets, every byte value, exact wire counts, CRC and error-before-seed
cleanup. Native normal and benchmark preflights pass instruction, layout,
embedded-payload, symbol and conservative-stack audits. SCI payload is 11,152
bytes, memory end 0x8c00bae8 (24 bytes free), and conservative stack is
1,180/1,232 bytes (52 bytes free). Existing limits are unchanged. Pinned CI
remains the package gate; host/native checks cannot establish console speed.

Renesas SH7750 hardware manual Rev. 7.02 section 14.2.3 describes DMATCR as
remaining transfers; section 14.3.1 Figure 14.2 orders one transfer before the
count decrement. Sections 14.3.4 and 14.3.6 describe the destination-write
phase and completion of accepted transfers on stopping. This supports a
completed-prefix interpretation of 512 minus DMATCR, but is architectural
reasoning, not a measured register-to-RAM timing result. Section 4.3.2 cache
line fills mean any future overlap must access only fully completed, aligned
32-byte lines after a compiler memory barrier, with no prefetch into future
lines. It must preserve bounded feeding, final DMA/SCI completion and CRC
acceptance before returning data.

A current whole-line check takes roughly 4.82 microseconds, exceeding the
roughly 1.28 microseconds of two serial bytes. Future overlap therefore needs
fine-grained interleaving; processing an entire line between feed operations
would risk stretching the nearly ideal transfer phase. No active-DMA CPU
buffer access is introduced by this candidate.

Primary reference: [Renesas SH7750/SH7750S/SH7750R hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware).
