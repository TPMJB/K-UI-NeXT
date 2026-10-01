# Original SCI DMA candidate — 2026-10-01

The initial SCI soak passed integrity but measured 521.75 KiB/s writes and
528.75 KiB/s reads. The owner also found DOA2 largely the same as SCIF and
requested a performance fix. These are the [accepted initial measurements](sci-soak-baseline-2026-10-01.md),
not evidence that the hardware's throughput potential has been reached.

The owner explicitly requires an original implementation: reference projects
may supply ideas, but SWAT's implementation must not be copied. The new DMA
code is written around the SH-4 register contract and K-UI's ownership model.
Existing source provenance and notices are retained.

## Transfer design

An optional payload-block operation removes the indirect callback and mode
setup from every data byte. SD commands, tokens, CRC bytes, busy waits, stop
commands and card-status checks retain their existing framing. Every received
sector is still CRC checked. A failed payload is never resumed by switching
methods halfway through the sector.

For eligible 512-byte reads, DMA channel 1 moves SCI receive data directly
into the caller's isolated, 32-byte-aligned RAM buffer. The CPU supplies exactly
512 dummy bytes to generate the clock; it no longer reads/clears the receive
register once per payload byte. The first validated build restored byte bit
order through the uncached alias, then calculated CRC in a separate pass.
The follow-up candidate combines reversal and CRC after DMA completion through
the caller's original alias, allowing isolated P1 buffers to use cache again
after the pre-DMA purge. No CPU access occurs while DMA owns the buffer.
The game reader's existing aligned sector cache avoids a second resident buffer.

Runtime writes use a separate aligned sector buffer, reverse the outgoing bits
before transmission and let channel 1 feed the transmit register. The follow-up
candidate computes CRC from the immutable original source while transmit DMA
is active. CRC is published only after successful completion; protocol CRC
bytes and response checking remain unchanged. This buffer and write
implementation are excluded from the read-only game resident.

DMA is borrowed only when the controller is enabled without a global error and
channel 1 has no active transfer, unacknowledged completion or enabled completion
interrupt. CPU interrupts are masked across the short claim/transfer/restore
operation. No DMA interrupt handler is installed. The SCI request is disabled
and channel state restored before restoring the CPU's interrupt mask. Global
DMA error flags are never cleared to force a transfer through.

Short, unaligned or otherwise ineligible transfers use a bounded block-polling
path. That path keeps at most one byte in flight, preserving tolerance for
interrupt latency. DMA failure stops the operation, latches a fault and returns
through existing SD cleanup; it does not silently turn a partial transfer into
a success. Both completion of memory transfer and the final serial byte must
be established before proceeding to CRC/response handling.

Runtime counters track completed DMA reads/writes, polled blocks and DMA
failures. Storage tests log deltas for the run and preserve them in successful
results' existing message field, including JSON and History. No on-card binary
result format change is required. These counters are excluded from the small
game resident. Polling counts include filesystem metadata and short transfers;
a nonzero count does not by itself mean the payload benchmark missed DMA.

The resident borrows the manifest's track records instead of keeping a second
16-track table. The SCI resident alone uses link-time optimization and SH call
relaxation. The protected memory reservation and stack limits are unchanged;
the SCI stack audit counts every final compiler-emitted frame, including
initialization, duplicate local names and optimized clones. Missing final
reports fail the build. SCIF and IDE keep their existing compilation settings.

## Validation and first console check

Host checks cover delayed transmit/receive flags, all byte values, exact sector
counts, DMA and polling paths, cache preparation, busy-channel fallback,
mid-transfer errors, bounded abort, state restoration and the next scalar SD
operation. Protocol tests exercise equivalent bulk/scalar framing, CRC
failures, stream limits and stop-command cleanup. Native builds must still
pass the instruction, resident memory and stack guards at their existing limits.

The first DMA build **cf8e7ea7866b** passed the owner's 15-minute soak with
416 MiB verified, zero errors/DMA faults and write/read 1,004.62/926.11 KiB/s.
DOA2 was substantially improved with a little lag remaining. See the
[hardware result](sci-dma-soak-2026-10-01.md). This does not validate the
follow-up processing changes or establish disc-equivalent game behavior.

Keep both original and first-DMA soaks as comparison points. Install matching `KUI/runtime.kui` and
`KUI/apps/games/retail-boot.kui`; the existing `6af5e11` boot CD and exFAT card
remain usable. First run Diagnostics → R → Storage tests → Quick once, and
inspect integrity, throughput and the DMA counts. A longer soak and another
DOA2 comparison should follow a successful short test. Do not claim a measured
speedup before receiving the new console result.

Hardware reference: [Renesas SH7750 hardware manual](https://www.renesas.com/en/document/mah/sh7750-sh7750s-sh7750r-group-users-manual-hardware),
sections 14 (DMAC) and 15 (SCI). The nominal 12.5 Mbit/s clock corresponds to
1.5625 MB/s before protocol, software and card overhead; sustained throughput
is a measurement, not a promise.
