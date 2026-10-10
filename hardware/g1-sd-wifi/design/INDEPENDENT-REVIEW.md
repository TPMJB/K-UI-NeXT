# Independent circuit and protocol review — 10 October 2026

Scope: the implemented physical-pin blocks, integrated connector nets, FPGA RTL and transaction tests. This review examined retained-device bus ownership, power-domain isolation, write completion and the optional NOR recovery circuit. ERC/DRC success was not used as evidence of protocol correctness.

Two blocking protocol defects were found and corrected during review.

| Finding | Consequence | Correction and independent evidence |
| --- | --- | --- |
| High: Features, Sector Count and LBA writes were accepted only while device1 was selected. | A host could write parameters while device0 was selected, then select device1 and issue a command using stale parameters. | The FPGA now receives shared parameter writes regardless of selection, subject to the ATA busy/DRQ/DMACK rules. Command execution remains device1-only. The independent test writes all five parameter registers before selecting device1 and checks the mailbox values. |
| High: final host write word cleared DRQ without asserting BSY. | Host status polling could report command completion when data had only reached the FPGA RX FIFO, before storage commit. Draining that FIFO also does not establish durability. | Final PIO/DMA RX acceptance now clears DRQ and asserts BSY. The MCU must explicitly complete the command after the storage completion condition. The independent test checks status before and after RX drain and in both transfer modes. |

`fpga/tb_review.v` passes **20 assertions** against the corrected RTL. As a negative control, temporary mutations reinstating selected-only parameter reception and removing final-write BSY cause **8 of those assertions to fail**. The mutations are not part of the design. The test therefore distinguishes the faulty behaviors from the corrections rather than reproducing an implementation formula.

The register rule is supported by [ATA-3](https://www.scs.stanford.edu/23wi-cs212/pintos/specs/ata-3-std.pdf), sections 5.1, 5.2.8 and 5.2.13, and [ATA/ATAPI-6](https://flint.cs.yale.edu/cs422/readings/hardware/ATA-d1410r3a.pdf), section 7.1 Tables 14–17. The latter explicitly defines unselected-device register reception, taskfile access with DMACK deasserted, and selected-device DRQ restrictions. The write completion correction follows the requirement to retain BSY or DRQ until command completion; receiving bytes is only a transfer phase.

## Circuit findings

The current net contract is consistent in the following reviewed cases:

* U35 has separate logic/host supplies, host data on B, logic data on A, and qualified OE. IRQ and DMARQ each have protocol ownership and hardware-qualified output enables. Host-side OE pullups prevent an absent logic rail from leaving those drivers enabled. R268 biases the host-powered IORDY buffer to release when its logic-domain source is absent.
* The primary carrier input at J20 is separate from `FLEX_5V`. C5 power is switched from the unretained primary rail, while its SPI/UART isolation ICs use the module's own `+3V3_C5_IO` output as their B supply. C5 EN/BOOT use default-off open-drain sinks. No carrier 3.3-V source feeds the module's output pad. External recovery equipment must obey the documented VTREF-only/powered-module contract.
* SD isolation uses separate direction controls for DAT and CMD, plus a storage-supervisor-qualified enable. The unused reversible lane is grounded at both ends; the unused fixed-forward output is NC. This is electrically defined in both directions.
* In the populated BIOS option, the DPDT switch connects the motherboard ROM CE pad to the original chip's isolated CE leg directly in stock recovery. That path contains no MCU or FPGA gate. The optional NOR and its write/bank gates use the unretained console rail. BASE_ONLY R410 must be absent in the switch variant. The factory bank outputs remain zero across ATA/MCU resets and early power-fail warning. This preserves the intended independent recovery path.
* TPS2116 MODE=PR1=GND is the documented highest-input selection state in its [truth table](https://www.ti.com/lit/ds/symlink/tps2116.pdf), section 7.3.1. It is not an accidentally disabled or VIN2-only configuration.

No additional blocking net-level defect was identified in these checks. This statement is limited to the reviewed circuit contract and transactions.

## Qualification conditions carried forward

The first PIO0 firmware should preload a complete read sector and reserve sufficient RX space before DRQ. A PIO0 host is not required to honor IORDY; a starved streaming test establishes HDL behavior, not Dreamcast support for that extension. Qualification must establish actual host behavior before relying on it.

Power-domain bench tests must verify the supervisor response versus rail fall rate, C5 turn-off with the console alive, and each unpowered-domain case. The AXC devices' automatic zero-rail isolation is not a substitute for measuring the interval before a collapsing rail reaches their isolation threshold. Stock recovery must also be demonstrated with the controller rail absent and the FPGA erased. No rail-energy calculation establishes card-write survival by itself.

The corrected final-write BSY behavior supplies a safe protocol boundary. A future storage implementation must use it: flush/commit failures must produce an ATA error, and completion must not be posted merely because bytes left the RX FIFO or entered volatile PSRAM.
