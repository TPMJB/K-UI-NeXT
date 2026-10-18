<!-- SPDX-License-Identifier: GPL-3.0-only -->
# Shared ATA DMA implementation target

ATA DMA is not implemented or hardware-accepted yet. The current IDE/CF backend
is synchronous PIO. Its host tests do not prove physical G1 timing.

The target is one standards-based Dreamcast ATA DMA engine for conventional
soldered IDE drives, CF adapters and the K-UI G1 board. Device IDENTIFY data,
reported transfer modes and transfer limits determine admission. A device must
not be assumed to support the board's modes, nor the board a real disk's modes.
PIO remains available for unsupported DMA devices or an unqualified setup.

Keep board-specific activation and recovery outside that generic engine. The
planned RP2350/FPGA board presents ATA device 1 while the GD-ROM retains device
0. Its current design routes DMA handshake/interrupt signals, but still needs
working MCU firmware, a qualified bitstream and measured timing. Initial board
bring-up must advertise only implemented PIO capabilities; MWDMA is admitted
only after validation. UDMA and LBA48 must not be advertised merely because the
host supports them. A conventional ATA adapter must not depend on the board's
activation sequence, MCU byte link or storage firmware.

## Acceptance order

1. Prove device selection, IDENTIFY, native PIO reads and GD-ROM coexistence.
2. Implement a bounded read-only host DMA diagnostic: mode negotiation,
   aligned buffers/cache ownership, genuine completion IRQs, transfer limits,
   errors, timeout, cancellation and cleanup under shared G1 ownership.
3. Compare DMA bytes and buffer guards with PIO across LBAs and lengths;
   demonstrate CPU progress during transfers. Exercise reset/error recovery
   and read the GD-ROM before and after repeated ATA activity.
4. Validate independently on the K-UI board and a conventional IDE/CF mod.
   One device's success is evidence for the shared engine, not universal
   device or wiring compatibility.
5. Integrate ordinary native games, then background/Windows CE paths. These
   need correct asynchronous completion and interrupt ownership beyond a
   working synchronous DMA block-read function. Qualify DMA writes separately.

No untested DMA mode is enabled in normal startup or game selection by this
compressed-reader branch. Preserve the existing SCI/SCIF and PIO baselines.
