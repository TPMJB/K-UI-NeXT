# G1 FPGA front end

This directory now contains an implemented front-end circuit pin allocation and original synthesizable logic. It is not a complete storage product: the RP2350 firmware must still execute ATA commands, produce IDENTIFY data, transfer raw microSD sectors, manage cache/PSRAM and preserve write/flush ordering. Lattice Diamond device fit, full timing constraints, placed-and-routed bitstream and physical bus measurements remain release requirements.

The hardware target is **LCMXO2-2000HC-4TG100C**, 100-pin TQFP, with 3.3 V core and all six VCCIO banks at `+3V3_LOGIC`. The exact physical pin table is in `../design/front-end-pinplan.csv`, derived from Lattice's MachXO2-2000 CSV, document 42568, and checked for all 100 package pins. `../design/blocks/frontend.json` is the source circuit block consumed by the KiCad generator. The 50 MHz Abracon ASE oscillator uses U10 pin20, the PCLKT3_0 primary clock input. External JTAG pulls and local/bulk decoupling follow the Lattice hardware checklist, document39095. A target-powered 3.3 V JTAG programmer uses the unpopulated J10 pogo pads; it must not power the board through VREF. No programmer/header is installed beneath the heat shield.

## Implemented logic

* Device0 remains the original GD-ROM. The FPGA tracks shared DEVICE/HEAD writes and only drives activated device1 reads. A separate vendor activation latch starts locked even when hardware `BRIDGE_ARM` is high. Data-buffer input direction observes taskfile writes for both devices without driving the console bus.
* Separate 512-word TX and RX FIFOs use synchronous memory ports, first-word bypass, guarded full/empty counts and explicit flush. PSRAM remains an RP2350 cache behind this link, not extra SH-4 RAM.
* Taskfile registers, command mailbox, BSY/DRQ/status/error update, nIEN, SRST, interrupt request and explicit DMA-read/DMA-write selection are implemented. Firmware supplies an exact 24-bit transfer word count before asserting DRQ. FIFO drain retains DRQ while words remain; only the accepted final host word ends the data phase and raises the MCU completion flag. For a WRITE the host stays BSY until media commit. Firmware controls transfer phase and command completion. Features/Count/LBA writes reach both devices before DEVICE/HEAD selection; ordinary command execution requires activated device1; `EXECUTE DEVICE DIAGNOSTIC` (`0x90`) is the shared exception. Taskfile reception requires DMACK deasserted and BSY clear; selected DRQ blocks command-block writes. PIO register/data ownership is disabled during DMACK so DMA uses its separate decode. On the final RX write word the host remains BSY until the MCU drains RX and durably commits media; receiving or draining data does not complete a WRITE command.
* Data direction and ownership are separated. PIO register and data reads drive the bus combinationally from DIOR- assertion and release on DIOR- negation: board propagation (input buffer, FPGA, OE gate, transceiver disable) supplies ATA t6 (5 ns minimum hold) and must stay inside t6z (30 ns maximum release); there is no clocked PIO hold. Only a DMA final word keeps a latched two-clock hold after DIOR deasserts, even when DMACK or DMA request changes; its release against DMACK negation (tZ) remains a DMA qualification item. Reset, `BUS_SAFE`, power warning and physical stock recovery immediately remove console-facing ownership.
* INTRQ and DMARQ each have separate active-low ownership requests as well as value outputs. INTRQ OE requires activation, selected device1 and nIEN clear; DMARQ OE requires activation, selected device1 and an explicit DMA state. Both requests pass through the independent power hardware gate.
* `FPGA_READY` depends on clock startup and MCU/power-warning reset, **not** `BUS_SAFE`. This avoids a READY/BUS_SAFE startup loop.
* A starved PIO read asserts the IORDY-low request, latches newly supplied data, then waits two more FPGA clocks before releasing IORDY. A full RX FIFO stretches a write. A host ending a starved read instead of honoring IORDY records a fault.

**Conservative bring-up:** use PIO mode0 only. A PIO0 host is not required to honor IORDY. Until Dreamcast IORDY behavior is measured, firmware must preload the complete512-byte read sector (256 words) and reserve256 RX slots before asserting DRQ. Do not depend on starvation stretching for initial operation. Sampled strobes and the 50 MHz clock cannot establish compatibility with fast PIO or MWDMA modes by simulation alone. The DMA ownership logic is implemented and tested, but firmware must not advertise or enable DMA until exact Dreamcast waveforms, transceiver delays and Diamond setup/hold/recovery constraints are satisfied. No UDMA logic is implemented. ATAPI emulation is not implemented.

## Host activation and relock contract

This is an original K-UI vendor protocol implemented in RTL. The K-UI host driver and RP2350 initialization firmware still need to implement it; there is no delivered driver or programmed bitstream. The protocol prevents ordinary boot-time device1 probes from activating the bridge. It is an accidental-activation guard, not authentication.

Firmware first establishes healthy storage, clears stale state, and raises hardware `BRIDGE_ARM`. The external hardware gate remains mandatory throughout. The host then writes `DEVICE/HEAD=0xB0`, supplies each row's five bytes to Features, Sector Count and LBA low/mid/high, and finally writes `COMMAND=0x00` (ATA NOP). NOP is used because every ATA/ATAPI device must abort it without side effects: if the retained GD-ROM turns out to ignore the DEV bit, it sees only a harmless abort, never a vendor-specific opcode. Every key byte must be freshly written while `DEVICE/HEAD=0xB0`; shared writes made with DEV=0 do not qualify. No read, IRQ or BSY acknowledgement is supplied for the first frame, because the bridge is still locked.

| Frame | Features | Count | LBA low | LBA mid | LBA high | Command |
| --- | --- | --- | --- | --- | --- | --- |
| Unlock stage1 | 0x4B | 0x55 | 0x49 | 0x01 | 0xA5 | 0x00 |
| Unlock stage2 | 0xB4 | 0xAA | 0xB6 | 0xFE | 0x5A | 0x00 |
| Relock | 0x4C | 0x4F | 0x43 | 0x4B | 0x00 | 0x00 |

Both unlock frames must be consecutive command writes and stage2 must arrive within **1ms** of accepting stage1 at the specified50MHz clock. Use a deadline below990µs to leave sampling margin. Wrong, partial, stale, out-of-order, ordinary-command or DEV=0 command frames cannot unlock. Selecting DEV=0 cancels a partial sequence. A repeated correct stage1 starts a new deadline; it does not activate. After stage2 the FPGA clears old mailbox/FIFO/transfer state, exposes DRDY status for device1, and allows normal commands. MCU indexed0x19 exposes activation and partial-frame flags but cannot set activation.

The keyed relock frame is accepted only when the taskfile accepts idle command writes. K-UI must complete and durably flush media writes before relocking. MCU indexed0x19 bit0 can independently abort and relock; writing zero cannot activate. Relock clears mailbox, transfers, FIFO byte phases and interrupt state. An unkeyed NOP on an activated device1 returns ABRT, as ATA requires (`ERROR=0x04`, `STATUS=0x41`); a locked bridge supplies no reply.

Hardware RESET, SRST, MCU/FPGA reset, power warning, ARM drop, BUS_SAFE loss or physical BIOS recovery relock and invalidate partial keys. External safety loss removes outputs immediately; the activation latch also clears asynchronously so restoring a brief safety pulse cannot restore prior activation. A sticky loss flag also clears stale BSY, mailbox, transfer and FIFO state on the next FPGA clock, including when the pulse was shorter than one clock. While locked, **data outputs, IORDY-low, INTRQ and DMARQ values and their ownership requests are released**. Taskfile input-direction buffer enable is permitted for snooping; it drives only toward the FPGA and never drives console DD pins. MCU status/IRQ writes cannot bypass activation. The host must unlock again after any reset or safety event.

## Shared diagnostic contract

An idle, hardware-armed bridge receives `EXECUTE DEVICE DIAGNOSTIC` (`0x90`) with either DEV bit, including while protocol-locked. This command never unlocks. It posts0x90 to the MCU mailbox, sets internal BSY, clears Device/head to0x00 and releases all console-facing data/IRQ/DMA/IORDY ownership. The supplied Features/Count/LBA inputs are not diagnostic parameters. Firmware must run the actual self-tests; the front end does not invent a passing result. Ordinary MCU status/IRQ writes cannot finish this diagnostic. MCU indexed0x1A supplies a result code and completes it, posting the ATA signature Count=1, LBA low=1, LBA mid/high=0, Device/head=0, STATUS=0x40 and ERROR=result bits6:0. Bit7 is forced clear for device1. Code0x01 means pass; other codes report failure. Completion generates no INTRQ and requires host reselection of device1. Protocol activation is preserved if already set and otherwise remains locked.

This implements **shared diagnostic receipt and the MCU result contract**, not a complete parallel-ATA discovery protocol. CN503's current circuit/FPGA allocation does not implement DASP/PDIAG. Those interdevice signals, real device0 coexistence, actual diagnostics and their standard timing still need hardware qualification before claiming a standards-compliant device1 diagnostic/discovery implementation. The intended K-UI driver explicitly unlocks/probes the bridge; generic BIOS discovery is not claimed. ATA-3 §7.5 describes DEV-independent diagnostic execution, the returned signature and device1 PDIAG handshake; §8.7.2 notes no device1 completion interrupt.

## RP2350 byte-link contract

`LINK_D0..7` map to RP GPIO0..7; GPIO8/9 are WRn/RDn, GPIO10/11 are A0/A1, GPIO12/13 are IRQ/READY, GPIO14 is FPGA_RESETn and GPIO15 is BRIDGE_ARM. The byte link is asynchronous relative to the 50 MHz clock and deliberately slow for initial bring-up.

1. Wait for `LINK_READY=1`, then establish address and write data or release the MCU data outputs for a read.
2. Allow at least **80 ns** setup before asserting WRn or RDn. Never assert both simultaneously.
3. Hold the selected strobe low for at least **120 ns**. Sample read data after at least **100 ns** of RDn-low. Hold write data/address for at least **20 ns** after WRn rises.
4. Keep address stable until `LINK_READY` returns high. Allow at least **160 ns** recovery after strobe deassertion. READY describes transaction synchronization, not FIFO capacity.
5. Check FIFO flags before transferring a complete word. A TX word is low-byte then high-byte, and the high byte commits the word. An RX word is low-byte then high-byte, and the high-byte read pops the word. Flush resets both byte phases.

| A1:A0 | Write | Read |
| --- | --- | --- |
| 0 | TX FIFO byte | RX FIFO byte |
| 1 | Reserved | Bit4 fault; bit3 RX full; bit2 TX full; bit1 RX empty; bit0 TX empty |
| 2 | Indexed register address | Current indexed address |
| 3 | Indexed register value | Indexed register value |

| Index | Read | Write |
| --- | --- | --- |
| 0x00 | Selected, pending, INTRQ, fault, transfer mode, DRQ, physical BIOS selector | Reserved |
| 0x01 | ATA command | Reserved |
| 0x02 | Features | Reserved |
| 0x03 | Sector count | Reserved |
| 0x04..0x06 | LBA low/mid/high | Reserved |
| 0x07 | Device/head | Reserved |
| 0x08 | Device control | Reserved |
| 0x09 | ATA status | Reserved |
| 0x0A | ATA error | Reserved |
| 0x10 | Reserved | ATA status; firmware must establish valid FIFO/phase before DRQ |
| 0x11 | Reserved | ATA error |
| 0x12 | Reserved | 0=PIO, 1=DMA read, 2=DMA write; 3 coerced to0 |
| 0x13 | Reserved | Bit0 sets/clears INTRQ; bit1 acknowledges command mailbox; bit2 clears transfer-done flag |
| 0x14 | Reserved | Bit0 flushes FIFOs/byte phases, clears fault/completion and aborts transfer |
| 0x15 | Bit0 host-word transfer-done; WRITE still requires media commit | Reserved |
| 0x16..0x18 | Remaining host words low/mid/high | Program exact 24-bit word count only while DRQ clear |
| 0x19 | Bit0 activated, bit1 unlock stage1 pending, bit2 diagnostic pending | Bit0 aborts/relocks; zero cannot activate |
| 0x1A | Reserved | Actual device1 diagnostic result bits6:0; completes pending0x90 without IRQ |
| 0x20/0x21 | RX word count low/high | Reserved |
| 0x22/0x23 | TX word count low/high | Reserved |
| 0x30 | Bit2 custom-flash write request; bit1/0 BIOS bank outputs | Bit2 write request; bank bits ignored |

`LINK_IRQ` is high for a pending command, transfer completion, fault or RX FIFO data. There is no independent command queue: firmware must acknowledge and complete/abort the current command before the next one. A second command while pending sets fault. Sector count/LBA progression and media command error handling remain firmware responsibilities. A status write attempting to assert DRQ while the programmed transfer count is zero is rejected and sets fault. Refill TX or drain RX while a transfer remains active; do not overwrite its count mid-DRQ.

## Optional BIOS behavior

The physical `BIOS_RECOVERYn` net is high for CUSTOM and low for STOCK_RECOVERY. Stock recovery mechanically selects the original immutable mask ROM and independently disables the bridge in the external hardware gate. The FPGA also qualifies ownership with the same physical input.

The active factory FPGA build uses **bank0 only**. `BIOS_INITIAL_BANK` is a configuration-time constant, not a runtime selector; Any nonzero value is unqualified: external bank pulldowns initially select bank0 before configuration, so even a nonzero configuration-time value can change a live ROM address. Nonzero-bank builds require verified whole-console held reset before configuration and release; a cold restart by itself does not prove safety. This is deliberate because the optional circuit has no whole-console held-reset input. Pre-ARM alone is insufficient to change ROM address banks safely once the CPU is fetching. Runtime bank-select writes are rejected. MCU reset, ATA reset, SRST and early `PWR_FAILn` warning preserve the bank outputs, while the write request is cleared. A future RAM-only flasher/restart protocol and hardware reset qualification are required before exposing runtime bank changes or in-system erase/programming to K-UI. The optional BIOS assembly remains DNP and requires its separate installation/timing qualification.

## Verification

Run `./run_checks.sh` with Icarus Verilog and Yosys installed. Verified locally on 2026-10-10 with Icarus12.0 and Yosys0.33:

* **66 bridge assertions:** startup isolation, device0 coexistence, device/head selection, command/LBA mailbox, status, byte-order PIO transfers, RX consumption, nIEN ownership, DMACK/BSY/DRQ taskfile gating, exact word-count completion, streaming through FIFO drain, final DMA word hold/OE release, PIO read release on DIOR negation, IORDY starvation recovery, SRST release, independent BUS_SAFE kill, ATA/MCU reset and physical BIOS recovery.
* **24 independent media/ATA review assertions:** shared taskfile reception and write-data receipt/drain versus actual media completion for PIO and DMA.
* **703 independent activation/diagnostic assertions:** locked wire ownership, input-only shared writes, fresh keyed activation, malformed/stale/wrong-device keys, real deadline expiry, host/MCU relock, short safety pulses, sticky recovery from busy/diagnostic state, DEV-independent0x90 reception, explicit actual diagnostic results/signatures, no diagnostic IRQ and BSY/DRQ command rejection.
* **Five compiled behavioral negative controls** deliberately bypass activation, drop DEV=0 diagnostics, remove asynchronous relock, remove sticky busy-state cleanup or restore a clocked PIO read hold. The wire-level suites reject each mutant; compilation failures do not count.
* **521 FIFO scoreboard cycles:** order/count/flags, full overflow prevention, empty underflow prevention, wrap-around, simultaneous push/pop and single-word replacement.
* Generic Yosys `check -assert` passes after memory collection/cleanup, with **two synchronous memory cells** preserved. MachXO2 technology mapping (`synth_lattice -family xo2`) reports **956 LUT4, 82 CCU2D carry cells, 410 flip-flops and 2 DP8KC EBRs**. The gross resource budget passes against 2112 LUTs and eight EBRs; this is not physical device fitting or timing closure. The only front-end warnings are Yosys' limited generic tri-state support. The actual I/O tri-state cells and EBR mapping must be verified by the vendor flow.

`bridge.lpf` provides physical pin locations and the primary clock frequency. It deliberately does not hide all asynchronous paths behind a global false-path declaration. External setup/hold, read latency, data-hold, transceiver propagation, synchronizer and recovery constraints are still unfinished; **this LPF is not timing sign-off**.

ATA behavior references: [ATA-3](https://www.scs.stanford.edu/23wi-cs212/pintos/specs/ata-3-std.pdf) §§5.1,5.2.8,5.2.13 and [ATA/ATAPI-6](https://flint.cs.yale.edu/cs422/readings/hardware/ATA-d1410r3a.pdf) §7.1 Tables14–17.

Primary sources: [Lattice pinout](https://www.latticesemi.com/view_document?document_id=42568), [Lattice data sheet](https://www.latticesemi.com/view_document?document_id=38834), [hardware checklist](https://www.latticesemi.com/view_document?document_id=39095), [Abracon ASE](https://abracon.com/Oscillators/ASEseries.pdf), [JLC exact FPGA catalog entry](https://jlcpcb.com/partdetail/LCMXO2-2000HC-4TG100C/C1521632). Component catalog identity is verified; availability and price require confirmation at ordering time.
