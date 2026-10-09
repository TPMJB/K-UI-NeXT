# G1 bridge: controller, RAM and BIOS disposition

9 October 2026. Reconciles [Claude's controller/RAM/BIOS proposal](https://github.com/TPMJB/K-UI-NeXT/blob/0ac289d4bc2739618ec0a67c8bea56a88b96c139/docs/g1-bridge-controller-ram-bios-2026-10-09.md) and [its response to the first disposition](https://github.com/TPMJB/K-UI-NeXT/blob/0ac289d4bc2739618ec0a67c8bea56a88b96c139/docs/g1-bridge-disposition-response-2026-10-09.md). Those documents remain on Claude's branch. This pass updates the proposal and references, not its native circuit, firmware or PCB.

## Architecture to evaluate

| Function | Proposed owner | Status |
| --- | --- | --- |
| ATA register replies, strobes, DMA burst ownership | Small FPGA/CPLD front end | Preferred candidate to evaluate; no part, HDL, timing closure or link selected |
| SD, buffering, C5 packets, USB | RP2350B | Retain MCU foundation; revised allocation needed for a front end |
| Immediate bus payload | Front-end FIFO plus RP2350 SRAM | Capacity and refill behavior need a concrete design |
| Larger read-ahead cache | Optional 8 MiB QSPI PSRAM | Candidate footprint/CS provision; no measured bandwidth |
| exFAT/ext4, file and track extents | Dreamcast K-UI | Unchanged sole filesystem owner |
| Stock/custom BIOS recovery | Physical selection and independent stock path | Must work without MCU firmware or FPGA configuration |

The original GPIO CSV and KiCad starter remain the MCU-only baseline. They must not be mistaken for the proposed FPGA implementation. The next circuit pass should show the chosen front end, explicit MCU link, buffer ownership, power/reset domains and any optional PSRAM/BIOS circuits.

## Controller corrections

Programmable logic is a strong candidate because register responses, final-word ownership and interlocks can be expressed directly in hardware. It does not remove shared-bus selection, turnaround, isolation or contention requirements. Switching MCU families is not justified yet, but the claim that every MCU alternative must use interrupt-driven responses is too broad: [NXP FlexIO](https://www.nxp.com/docs/en/application-note/AN12686.pdf) supplies hardware shifters/timers and DMA. That source does not establish an ATA implementation. Package claims also need exact parts; STM32H743VI includes an [LQFP100 option](https://www.st.com/en/microcontrollers-microprocessors/stm32h743vi.html).

Holly's shared read-wait register makes a common register/data timing plausible. It does not prove the actual waveform or a 115 ns register deadline. Keep the standard register/data distinction in the first disposition and measure both on the console. RP2350's estimated 100-120 ns response and FPGA's suggested 10-20 ns are design estimates until complete paths, loading, routing and competing traffic are bounded.

MachXO2-1200HC/-2000HC TQFP100 are plausible candidates; [the family datasheet](https://www.latticesemi.com/view_document?document_id=38834) lists 79 I/O and 64/74 kbit EBR. Count actual usable pins and FIFO capacity after configuration, clock, reset, debug and BIOS allocation. iCE40HX4K is another candidate with a larger package and separate core supply. UP5K's smaller I/O budget excludes the proposed wide-link allocation, not every narrower-link architecture. No package is selected before footprint and assembly review.

**Instant-on does not mean valid at power application.** The [MachXO2 configuration guide, §§2 and 5.1-5.4](https://www.latticesemi.com/view_document?document_id=39085) describes ramp, POR and configuration before user logic operates. Keep stock-ROM access and bridge isolation independent of configured FPGA outputs. Demonstrate safe behavior for absent/corrupt configuration, brownout and reconfiguration; configuration-good/reset sequencing alone must not replace physical stock recovery.

An eight-bit 40 MHz link has a 40 MB/s raw ceiling only if it moves a byte every beat. Define handshake/full signaling, direction, turnaround, PIO instruction/DMA cost, FIFO depth and error recovery, then measure payload while SD, QMI and C5 work concurrently. Specify underrun/overflow and backpressure before the FIFO empties, including legal DMA burst termination with the final word preserved. A small FIFO alone cannot hide long SD stalls. FPGA timing reports and tests must cover the final bus buffers too.

## PSRAM and buffering

Accept optional read-ahead RAM as a useful direction. It caches raw 512-byte LBAs; the MCU must not mount the card or infer file ownership. Ordinary ATA does not communicate track extents, so any K-UI extent hints need an explicit protocol. Define write invalidation, media-change/reset handling and cached/uncached access policy. Cold sequential traffic should be able to use SRAM directly rather than requiring every byte to traverse PSRAM.

QMI CS1 permits GPIO 0, 8, 19 or 47. GPIO 47 is available in the original allocation. It is one **additional Bank 0 GPIO**, plus the shared QSPI clock/four data wires, power and decoupling. An FPGA frees allocation choices but does not remove the CS constraint. Configure CS explicitly and disable general CS autodetection, which can toggle ATA-assigned pins. Flash fetches also share QMI. [RP2350 §§4.4/12.14](https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008373-DS-2-rp2350-datasheet.pdf?disposition=inline); [Pico SDK PSRAM setup](https://github.com/raspberrypi/pico-sdk/blob/master/src/rp2_common/hardware_psram/psram.c).

For APS6404L-3SQR, package choice matters: **USON ZR is 0.6 mm maximum; SOP SN is 1.45 mm maximum**. Qualify power-up, burst/page and refresh timings for the exact part. [AP Memory datasheet, package drawings and interface requirements](https://www.apmemory.com/en/downloadFiles/032411212009597427). These are component heights; carrier PCB, solder, insulation and sockets still count toward clearance.

The following are arithmetic budgets, not measured results. MB/s is decimal; MiB/KiB are binary.

| Budget | Calculation / implication |
| --- | --- |
| 50 MHz, four-bit SD SDR | 25 MB/s wire ceiling; actual driver/card payload may be lower |
| 75 MHz quad PSRAM | 37.5 MB/s gross; protocol, refresh, arbitration and flash fetches consume margin |
| 16.7 MB/s through PSRAM twice | At least 33.4 MB/s for incoming writes plus outgoing reads; refill superiority is unproven |
| 8 MiB buffer at 16.7 MB/s | About 0.50 s before reservations |
| 256 KiB SRAM at 16.7 MB/s | About 15.7 ms |
| 8 MiB of 44.1 kHz stereo 16-bit PCM | About 47.6 s; FMV coverage depends on its actual bitrate |

The SD ceiling agrees with [SD Association High Speed](https://www.sdcard.org/developers/sd-standard-overview/bus-speed-default-speed-high-speed-uhs-sd-express/). RAM helps stalls, not a proved peak-speed increase. Serve ATA deadlines from FIFO/SRAM, never directly from nondeterministic PSRAM. Keep write-through/flush durability and the reserve-power investigation from the first disposition.

## BIOS-footprint tap candidate

The [RDC VA1 drawing](https://consolemods.org/wiki/images/2/27/Dreamcast_VA1_FULL.pdf), linked by its [author](https://acidmods.com/forum/index.php?topic=44892.0), operates IC501 as an **8-bit ROM**. It nevertheless exposes 20 ATA nets through data, multiplexed address and read pins. This is a traced reference, not verified continuity on the owner's board.

| ATA signals | IC501 pins, in corresponding order |
| --- | --- |
| DD0-DD7 | 15, 17, 19, 21, 24, 26, 28, 30 |
| DD8-DD15 | 31, 11, 10, 9, 8, 7, 6, 5 |
| DA0, DA1, DA2 | 4, 42, 41 |
| DIORn | 14 |

DD8 is ROM Q15/A-1; DD9-DD15 are ROM A0-A6. DA0-DA2 are ROM A7-A9, not A0-A2. Q8-Q14 are unconnected. [IC501-candidate-reference.csv](IC501-candidate-reference.csv) joins these points to the logical CN503 map. Eight controls still need separate taps: RESETn, CS0n, CS1n, DIOWn, IORDY, INTRQ, DMARQ and DMACKn. Four are on CN503's B row. This tap is before the existing connector series resistors, so routing/loading needs signal-integrity review. The drawing labels VCC pin 23 as 3.3 V; measure supply and bus levels separately.

No IC501 removal, adapter manufacture or soldering plan is approved by these references. Confirm physical package/orientation and continuity first, then compare this approach with the connector/underside options and the installed shield measurements.

## BIOS recovery and programming

Keep the original immutable mask ROM as the preferred stock recovery device. A physical selector must route a stock chip-enable path and disable custom/bridge outputs without depending on FPGA logic being configured. Mutually exclusive chip enables and unpowered behavior require a circuit, not a software promise.

For an x8/x16 64 Mbit NOR such as MX29LV640E in byte mode, Q15/A-1 supplies byte bit 0; A0-A19 supply bits 1-20, so physical A20/A21 can select four 2 MiB images. This banking claim is correct for that organization. Match BYTE mode, address mapping, pinout and voltage in an explicit adapter schematic; a TSOP48 NOR is not a drop-in assertion about IC501's footprint.

**Blocking WE only while bank 0 is mapped is insufficient protection.** A whole-chip erase commanded from another bank can erase it. Device-enforced sector protection/command filtering would need qualification; WP alone does not protect an arbitrary full image. A writable stock copy is weaker than the original mask ROM. [Macronix MX29LV640E datasheet, pinout, protection and chip-erase sections](https://www.macronix.com/Lists/Datasheet/Attachments/8514/MX29LV640E%20T-B%2C%203V%2C%2064Mb%2C%20v1.7.pdf).

Session bank selection is a candidate, not a working warm reboot. Stop/drain G1 activity and prevent ROM calls, including fonts and flash services, before switching/programming. Define a complete restart rather than assuming a jump to the reset vector resets all peripherals. Specify separately how ATA SRST, G1 hardware reset, MCU reset, FPGA reconfiguration, console reset and power loss affect the session latch and write gate. Selection must stay stable while games use ROM in place. [KOS font-lock contract](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/include/dc/syscalls.h) confirms exclusive G1 ownership is required for font access. Validate every installed image's boot/unlock/optical behavior; distribute tools and patches rather than BIOS images.

## Evidence and next steps

Claude correctly withdrew the nonexistent-CF-rig assumption and its original PIO-output/DMARQ gate errors. Software logs and a controlled raw-register probe can precede a hardware responder; passive waveform capture does not require a working CF card. However, an IDE failure code does not alone prove which physical device drove the bus. Floating levels, stale state and probe cleanup must be considered. The existing ATA path sends IDENTIFY directly; a silent bridge would need the explicit activation contract discussed previously.

1. Obtain the two installed-shield clearances and IC501 package/orientation. Check the candidate continuity map with power disconnected; measure supply/signal levels in a properly qualified setup.
2. Review existing IDE-probe evidence and capture baseline register/data strobes before freezing timing architecture. Any software probe must own/restore the bus and timing; do not sweep values blindly under game/optical activity.
3. Develop the chosen front-end register file, ownership/reset/interlock model, MCU link and independent stock path. Demonstrate resource/timing closure and isolate the responder before console attachment.
4. Prove coexistence and hashed PIO reads, then DMA. Qualify writes/cache coherence/flush and power cuts before networking or BIOS programming.

Validation here covers references, arithmetic, document links, 28 logical ATA joins and 20 unique candidate IC501 pins. No electrical, console, RF, thermal, FPGA timing or fabrication validation is claimed. Native design files and the original GPIO allocation remain unchanged.
