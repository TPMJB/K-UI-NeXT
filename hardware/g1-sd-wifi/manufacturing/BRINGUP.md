# Rev A programming and first-article bring-up

Status: **unvalidated prototype design, 23 October 2026**. The native integrated schematic has passed ERC with zero findings, and the physical-pad audit reports no mismatches. The carrier is routed (DRC, opens, parity and ERC zero); the flex sockets J50/J51 still have to move before an order, and fabrication review and the release re-review are pending. These checks establish CAD consistency; no board has completed the electrical, installed-fit or console tests below. This document accompanies the actual circuit, not an invented firmware release.

## Implemented hardware and software status

| Provision | Actual hardware | Software / qualification still required |
| --- | --- | --- |
| Main controller | U201 RP2350B; X201 12 MHz crystal; U202 W25Q128JVSIQ 16 MiB boot flash; USB J202 and SWD J203 service copper | RP2350 application, ATA command engine, native four-bit SD driver and recovery tooling are not ready |
| ATA front end | U10 LCMXO2-2000HC-4TG100C; X10 50 MHz oscillator; two 512-word FIFOs in implemented HDL; JTAG and PROGRAMn wired to U201 through R2044–R2048 (220 Ω); J10 JTAG copper kept for an external cable | Vendor device fit, complete timing constraints, placed/routed bitstream and measured G1 timing remain open; no programming image supplied |
| microSD | J201 Molex1040310811; U207/U208 rail-isolating four-bit/CMD/CLK buffers; U209 enable gate | PIO driver must manage CMD/DAT direction and hardware enable; start with a disposable card and raw-sector tests |
| Cache provision | U203 APS6404L-3SQR-ZR, 8 MiB volatile QSPI PSRAM, QMI CS1 on GPIO47 | Initialization, burst/deselect timing, cache policy and validation are unimplemented; this is bridge cache, not added SH-4 RAM |
| Local removable Wi-Fi | USB-free, header-free U206 XIAO ESP32-C5; K201–K216 Harwin S7221-45R contacts; U204/U205 SPI/UART isolation; U71 switched input; J204 recovery copper | C5 SPI firmware, protocol, RP UART proxy, installed SPI updater and network integration are unimplemented |
| Power-loss handling | J20 dedicated 5 V input; U20 source mux; U21/U22 separate logic/storage bucks; U23–U26 supervisors; independent U27–U34 bus/radio gates | Reserve R202/D20/C202–C205 is DNP; no bounded flush, hold-up duration or durable-write guarantee exists |
| Optional custom BIOS | U40–U45, J40/J41, SW40 and JP40 are explicitly included as the BIOS_OPTION circuit | Default BASE_BIOS_DNP leaves optional hardware unpopulated. Optional fitting, NOR first-fetch timing and programming workflow require separate qualification |

The old MCU-direct-ATA allocation is obsolete. The current [GPIO CSV](../controller/K-UI-G1-GPIO-RevA.csv) lists all 48 GPIOs with actual U201 package pins, nets, firmware direction and external reset/default contracts. The MCU talks to the FPGA over `LINK_*`; U35–U39/U70 and the FPGA handle console bus timing and ownership. The stock GD-ROM remains device0 and the bridge is device1. Initial console operation is **PIO mode0 only**; firmware must not advertise DMA or faster PIO until timing qualification passes. UDMA and ATAPI emulation are not implemented.

## Before ordering the prototype

A first order is an unvalidated bring-up prototype. It does not require tests on hardware that has not been manufactured, but it does need one more owner console-open session before either the carrier or a flex arm is ordered: the unpowered CN503 row and numbering proof, the 1:1 fit check, the flex route survey and the shield-height check in [OWNER-NEXT-STEPS.md](../docs/OWNER-NEXT-STEPS.md) items 1, 3, 3a and 3b. Their results feed the layout revision that moves J50/J51 (`design/CARRIER-LAYOUT-REVIEW.md`, Mechanical findings, item 1). Contact landing, installed clearances and electrical behavior are first-article tests.

Before an order, finish and review the routed rigid/flex CAD, actual ERC/DRC and schematic parity, power-converter/core-regulator layout, exact footprint and pin orientation, BOM/CPL population and rotations, purchase availability, and plotted Gerber/drill/coverlay/stiffener layers. Resolve any manufacturing-process exception with the fabricator rather than treating a CAD check as process approval. Record an explicit base/optional-BIOS and fitted/DNP population variant. Confirm the planned flex contact face, mating thickness, CN503 joining window, carrier supports, removable C5 clamp and antenna keepout against the captured dimensions. Keep the manufacturing release gate blocked while those reviews are incomplete.

For the C5, hard supports must establish a nominal 0.90 mm carrier-copper-to-module-underside gap. Sixteen S7221-45R contacts have at least 6.24 N combined specified reaction at that working height. Retention must resist that load without PCB bow or compressed-stop contact. The installed height includes motherboard stand-off, carrier thickness, spring gap, measured USB-free module height and clamp protrusion; the earlier 4.48 mm measurement includes USB. Use the measured 8 mm region for this assembly. The 6 mm BIOS/CN503 region and zero-clearance CPU/GPU thermal contacts remain separate keepouts. Do not assume an under-PSU clearance from the general 8 mm measurement.

### Fire and short-circuit safety

These are the board's protection layers; keep every one of them in place.

- **J20 5-V input:** F20 (1.5 A fast fuse), then D21 (bidirectional 5-V TVS: intended to divert a mistaken 12-V feed; TVS/fuse clearing coordination unqualified) and Q20 (reverse-polarity block on the bench only: once the flex is connected, carrier ground is console ground through the flex, so a reversed pair bypasses Q20 and F20 and only the harness tap fuse protects against it). F20 protects the board, not the wire to it: put the 2 A Littelfuse 0251002.MXL in the harness within 20 mm of the console tap (`design/PARTS-TO-BUY.md`), keep the pair short, and route it clear of shield edges and screw bosses. Strain-relieve the wires at J20 so they cannot tear the pads.
- **Host 3.3 V from the flex:** one contact (B1) through F21 (0.5 A fast fuse). The flex carries no console 5 V at all, but arm B's B1 trace is console 3.3 V with no fuse before F21: never power the console with arm B unplugged unless its tail fingers are taped over.
- **Copper:** 5-V and storage supply tracks stay on the 1 oz outer layers. `tools/check_power_copper.py` screens local paths using IPC-2221; `design/power-copper-check.json` records remaining failures. Fuse nameplate current is not a fault-current bound, and converter peak limits are not a continuous-current guarantee. Resolve the fault-current/clearing review before console-powered use.
- **Underside:** the 39 raw-G1 probe pads and J10's ten plated holes (`+3V3_LOGIC`, GND and JTAG) are bare copper on the bottom face, 2.5 mm above the motherboard. Before the first console power, cover the whole underside with one layer of 25 µm polyimide tape (≤ 0.07 mm with adhesive), cut back only where supports bear, and use insulating supports, so no pad or via can touch a motherboard part. Put one layer of the same tape on the shield's inner face over the carrier. Both layers count in the stack height; after fitting, the clay check at the sockets must still leave at least 0.2 mm. Installed probing uses the top-side points and temporary leads described under console introduction. Open and close the FH12 latches only with the shield off: an open actuator stands about 8 mm above the motherboard.
- **ESD:** the microSD socket, service pads and flex tails have no ESD protection parts. Handle the carrier and arms on a grounded mat with a wrist strap, and keep the flex tails' fingers covered until they are plugged in.
- **C5 module:** never fit a battery to the XIAO ESP32-C5. Its underside battery lands are not contacted by the carrier, and a lithium cell under the heat shield is a fire risk.
- **First power:** always the current-limited bench start in step 2 below, before the board ever sees the console's supply.

Bench application binaries and the fitted FPGA image must be built, hashed and recorded before their corresponding powered test. No `.uf2`, `.bin`, JTAG programming file or ready-to-use command sequence is supplied here. Synthesis and simulation results in [fpga/README.md](../fpga/README.md) do not substitute for the Lattice programmer/fit/timing flow.

## Service pads and programming

All service interfaces are unpopulated copper contacts for a temporary jig. Orient by numbered CAD pads, not cable color or an assumed header view. Programmers use 3.3 V logic; their voltage-reference pins are sense inputs. Supply the target through J20. Keep console power disconnected during standalone programming and do not let a programmer inject power into a console rail.

### RP2350 USB: J202

| Pad | Net | Jig connection |
| --- | --- | --- |
| 1 | GND | USB ground |
| 2 | USB_SERVICE_DM | USB D− |
| 3 | USB_SERVICE_DP | USB D+ |
| 4 | USB_SERVICE_VBUS | USB 5 V, **sense only** |

VBUS feeds only R2040/R2041's 100 kΩ / 100 kΩ divider to U201 GPIO23, physical pin23. It does not feed the board or console 5 V. D−/D+ reach U201 pins66/67 through R2038/R2039, 27 Ω. Target power is still needed at J20. Normal application USB attachment must wait for valid VBUS sense.

To enter the RP ROM USB loader, with target power available and G1 disconnected, ground J203.6, pull J203.5 low, release reset while keeping BOOTSEL requested, then release J203.6 once ROM USB mode is entered. J203.6 reaches QSPI CS through R2006, 1 kΩ; use a ground closure, not a driven high voltage. Connect the USB jig at J202 and use the Raspberry Pi ROM-loader tools appropriate to the built image. Keep `BRIDGE_ARM=0` throughout recovery. Check ROM enumeration/readback before attempting an application.

### RP2350 SWD/reset: J203

| Pad | Net | Connection / role |
| --- | --- | --- |
| 1 | +3V3_LOGIC | Target VTREF; sense only |
| 2 | GND | Debug ground |
| 3 | MCU_SWCLK | U201.33 SWCLK |
| 4 | MCU_SWDIO | U201.34 SWDIO |
| 5 | LOGIC_RESETn | U201.35 RUN; active-low reset, also U23 supervisor output |
| 6 | MCU_BOOTSEL_PAD | Ground to request ROM USB boot |

Use SWD to identify the part, inspect reset/clock state, load a minimal RAM diagnostic and recover boot flash without relying on application firmware. A reset jig must pull RUN low and release it; do not drive it high against the open-drain supervisor. `PWR_FAILn` is a separate interrupt on GPIO31/U201.39 and is not RUN/reset.

### FPGA JTAG: J10

| Pad | Net | Role |
| --- | --- | --- |
| 1 | +3V3_LOGIC | Target VREF; sense only |
| 2 | GND | Ground |
| 3 | FPGA_TCK | JTAG clock |
| 4 | GND | Ground |
| 5 | FPGA_TMS | JTAG mode |
| 6 | FPGA_TDI | Programmer data output |
| 7 | FPGA_TDO | Programmer data input |
| 8 | FPGA_PROGRAMn | Configuration control observation/service |
| 9 | FPGA_INITn | Configuration status observation |
| 10 | FPGA_DONE | Configuration status observation |

U201 is U10's normal programmer: TMS GPIO21, TCK GPIO22, TDI GPIO29, TDO GPIO30, and PROGRAMn GPIO28 (open-drain), each through a 220 Ω resistor (R2044–R2048) whose FPGA side is the J10 net above. INITn and DONE are not wired to the controller; it reads configuration status over JTAG. An assembled board therefore needs no FPGA cable once the controller's boot flash holds the configuration firmware and image. J10 remains for bring-up before that firmware exists, or as recovery: hold RUN low through J203.5 (or have firmware release all five pins as inputs) before attaching a cable, otherwise the programmer fights the RP2350 through 220 Ω (at most 15 mA per pin). With a cable, use a MachXO2-capable, target-powered 3.3 V JTAG programmer. Identify U10 before programming and use the exact LCMXO2-2000HC-4TG100C build. Record image hash, programmer settings and readback/verify result. PROGRAMn is a configuration control, not the user-mode `FPGA_RESETn`. Neither DONE nor successful JTAG verify proves the user logic, external clock or G1 timing. Scope X10/R110 at U10.3 (PCLKT5_0; R110 sits between X10.3 and U10.3): the intended clock is 50 MHz. Its phase relative to Dreamcast strobes is unknown and must be measured; the incomplete LPF is not timing sign-off. Installed recovery through J10: shield off, RUN held low (`LOGIC_RESETn` then keeps `BUS_SAFE` low), no G1 session, and power either from the console for the minimum time or, with the J20 harness unsoldered (or its tap fuse removed) and the console off, from a current-limited bench 5 V at J20, never through the harness, which would feed the whole console 5 V rail; release RUN only after the cable is removed or idle.

### USB-free C5 UART recovery: J204

| Pad | Net | External 3.3 V UART jig |
| --- | --- | --- |
| 1 | GND | Ground |
| 2 | +3V3_C5_IO | Target VTREF; sense only |
| 3 | C5_UART_TX | Adapter RX; module D6/GPIO11/U0TXD |
| 4 | C5_UART_RX | Adapter TX; module D7/GPIO12/U0RXD |
| 5 | C5_ENn | Open-drain/ground reset closure |
| 6 | C5_BOOTn | Open-drain/ground BOOT closure, module TP2/GPIO28 |

J204 is on the module side of U204/U205. Disable those buffers with `C5_IO_OEn=1` before attaching an external UART adapter; do not allow two UART transmitters to drive C5 RX. Power U206 through the board's switched `+5V_C5` and confirm its local `+3V3_C5_IO`. Pad12 on U206 is a **3.3 V output**, not the carrier supply input. It must remain separate from `+3V3_LOGIC`. Never use a 5 V UART adapter or power the module through J204 VTREF.

To enter C5 ROM UART recovery: hold BOOT low, pull EN low for at least 50 µs, release EN while retaining BOOT low for at least 3 ms, then release BOOT. The selected path uses the module's UART0 and joint-download strap state; leave the module's GPIO27 strap undriven. Read chip identity and flash details, program a separately built C5 image and verify it. This works with the module physically installed if service-jig access and retention are qualified. Dreamcast-initiated flashing, an RP UART proxy, SPI application updates and OTA still require software; physical recovery wiring does not implement them.

| Carrier contact | U206 contact | Net |
| --- | --- | --- |
| K201 | 1 / D0 / GPIO1 | C5_IRQ |
| K202 | 2 / D1 / GPIO0 | C5_READY |
| K203 | 3 / D2 / GPIO25 | Unconnected strap pad |
| K204 | 4 / D3 / GPIO7 | Unconnected strap pad |
| K205 | 5 / D4 / GPIO23 | C5_CSn |
| K206 | 6 / D5 / GPIO24 | Unconnected |
| K207 | 7 / D6 / GPIO11 | C5_UART_TX |
| K208 | 8 / D7 / GPIO12 | C5_UART_RX |
| K209 | 9 / D8 / GPIO8 | C5_SCLK |
| K210 | 10 / D9 / GPIO9 | C5_MISO |
| K211 | 11 / D10 / GPIO10 | C5_MOSI |
| K212 | 12 / 3V3_OUT | +3V3_C5_IO |
| K213 | 13 / GND | GND |
| K214 | 14 / VBUS | +5V_C5 |
| K215 | TP2 / BOOT / GPIO28 | C5_BOOTn |
| K216 | TP8 / EN | C5_ENn |

Only these sixteen contacts are fitted. The module's other underside JTAG/test/battery pads are unconnected. TP2/TP8 are actual underside contacts derived from the official PCB, not substitutes for exposed edge pins. Inspect USB removal for damage/shorts; install/remove U206 only with all target power disconnected. Retention and compressed tip alignment still need a first-article contact/wipe test.

## First bench article: no G1, Wi-Fi off

Use the base population, G1 flex disconnected, no card, no C5 initially, and no optional BIOS tap/harness. Keep R410 fitted only for BASE_BIOS_DNP; JP40 remains open. Record board revision, assembly population, instrument settings and software/image hashes with every result.

1. **Unpowered inspection.** Compare component orientation and actual assembly to BOM/CPL. Confirm F20, F21 and D21 are fitted (D21 has no polarity); measure F20 and F21 as near-zero ohms. Inspect U201 thermal pad/core buck, U20/U21/U22 exposed pads, fine-pitch U10, U203 and FPC connectors. Check J20 polarity and resistance from every supply to GND; allow capacitors to settle and investigate low resistance before applying power. Check reserve parts are DNP. Check the orientation marks of L201 (the RP2350 core inductor, winding mark per the RP reference) and L20/L21 (dot to SW): two-pad parts are easily placed rotated. Check flex pad-to-net mapping/neighbor isolation separately on the loose flex before console attachment.
2. **Current-limited start.** Feed regulated 5.0 V to J20.1 and GND to J20.2 from a bench supply, initially limited to 0.10 A for fault screening. If the board stays in current limit, turn it off and inspect; this screening limit is not a valid operating-current allowance. Increase only within the reviewed rail budget and measured inrush after shorts/polarity are excluded. Do not feed J20 from a 12 V Dreamcast rail. Service USB does not supply target power.
3. **Measure rails and clocks.** Check `PRIMARY_5V`, `+5V_HOLD` at TP24, U21 `+3V3_LOGIC`, U22 `+3V3_STORAGE`, U201 `+1V1_MCU`, X201 12 MHz and X10 50 MHz. `+5V_C5` must remain off. Record ramp, ripple, input current and hot spots. Check U23 releases `LOGIC_RESETn` only after valid logic rail; U24 releases `STORAGE_PGOOD` after valid storage rail. CT-open release is nominally 20 ms, not an assertion-delay guarantee.
4. **Verify safe defaults before running code.** With host disconnected, TP22 `BUS_SAFE` must stay low, TP23 `DATA_OEn` high, and INTRQ/DMARQ outputs released. GPIO15 `BRIDGE_ARM`, GPIO14 `FPGA_RESETn`, GPIO46 `C5_PWR_EN` and GPIO26 `SD_IO_EN` have default-low external pulls; GPIO20 `C5_IO_OEn` has a pull-up. The FPGA configuration pins idle at the board pulls through their 220 Ω resistors: TMS, TDI and PROGRAMn high, TCK low; GPIO28 (PROGRAMn) is only ever pulled low, never driven high. Erased/unconfigured FPGA, MCU held reset and missing host rail/reset must each leave the console-facing outputs disabled. Do not bypass these gates to make initial code run.
5. **Recover the MCU and memory first.** Use SWD/ROM USB and a minimal diagnostic, keeping ARM low and C5 off. Verify U202 identity/readback, clocks, reset and GPIO defaults. For populated BASE, initialize U203 after its required power-stable wait, validate reset/read/write patterns, wrap boundaries and repeated accesses while observing CS timing. Respect AP Memory's maximum 8 µs CE-low and required deselect interval. Exercise reset/USB recovery before enabling the FPGA link. Force a watchdog reset with ARM raised and scope GPIO15 `BRIDGE_ARM` and TP22 `BUS_SAFE`: record how long ARM stays high before boot code drives it low (RP2350 pad isolation may hold the last output). For BASE_PSRAM_DNP, leave CS1/GPIO47 inactive, disable all PSRAM probing/mapping and validate the SRAM-only path; U202 remains fitted.
6. **FPGA link without the console.** Build the qualified vendor image and program/verify it, from the RP2350 over GPIO21/22/29/30 once its configuration firmware exists (until then through J10 with RUN held low), with ARM and FPGA_RESETn low and, in the RP firmware, SAMPLE/PRELOAD (1Ch) with all ones for the full boundary-scan register length of the LCMXO2-2000HC TQFP100 BSDL before ISC_ENABLE (C6h) or ISC_ENABLE_X (74h) (confirm in the BSDL that a control cell's disable value is 1), or a Lattice-generated SVF replayed unchanged; confirm IDCODE, then DONE in the JTAG status register (and at J10.10 with a meter); scope READY and reset. Establish WRn/RDn high and the data/address directions before releasing GPIO14; use the byte-link timings below. While `BUS_SAFE`, ARM, `FPGA_READY`, host RESET or `BIOS_RECOVERYn` is low, the FPGA clears both FIFOs, status, transfer count and byte phases every clock (`fpga/bridge.v`), and on the bench host RESET (R263 pull-down) and `BIOS_RECOVERYn` (R409 pull-down; R410 lifts it to about 3.0 V only when `CONSOLE_3V3` is present) are low, so test in three stages:
   - **6a. No host, ARM low:** `LINK_READY` and `FPGA_RESETn` reset; index write/readback; reset values (index 03h = 01h, 07h = A0h, 09h = 40h, 0Ah = 01h); flags = 03h; TX writes and a written transfer count are discarded (the safety gate works); a 48h status write with a zero count sets the fault flag and `LINK_IRQ`, and writing 01h to index 14h clears both. TP22 stays low even with ARM raised.
   - **6b. Host-power jig:** with J20 powered first, feed a current-limited 3.30 V (50 mA) into J51 pin 20 (`CONSOLE_3V3_FLEX`, through F21) and pull J50 pin 2 (`ATA_RESETn`) to it through 4.7 kΩ; connect grounds at J50 pins 8/13/17/20 and J51 pins 1/4/9/14 and leave all other contacts open (R258–R262 hold the strobes idle). J50/J51 are bottom-contact sockets, so use 0.5 mm FFC breakouts. Before applying the jig supply, check with the meter that each breakout pin reaches the same-numbered J50/J51 pad (J51 pin 20 to F21 pin 1, J51 pin 1 to ground): the FFC cable type can reverse the numbering. Raise `FPGA_RESETn` and ARM: TP22 rises only when every permit term is valid and falls at once when ARM, `FPGA_RESETn`, the jig 3.3 V or `ATA_RESETn` is removed. Test TX fill to 512 words, TX-full, the fault on the 513th word, flush, 24-bit count write/readback while DRQ is clear and its lock while DRQ is set, rejection of DRQ with a zero count, and that INTRQ and DMARQ requests never reach J50/J51 while the activation latch is locked (TP23 high). Remove the jig supply before J20.
   - **6c.** RX push/pop and byte order, TX pop, exact transfer-count completion, IORDY, activation, IDENTIFY and sector transfers need real DIOR/DIOW/CS/DA/DD cycles and the keyed unlock: use a host-bus emulator on J50/J51 (for example an RP2350B board running K-UI's ATA code) with the 6b supply and reset, or record that they are first exercised in the console step.

   Successful tests here do not authorize G1 drivers or validate Dreamcast timing.
7. **Card tests with C5 still off.** Insert only a disposable scratch microSD. Confirm J201 detect reads low when inserted, U24 storage-good, and U209 enable polarity. Begin at the SD initialization clock rate, then validate native four-bit direction changes and CRC/data handling at conservative clocks. The firmware must deliberately set GPIO24/25 and set GPIO26 only when the rail is valid; verify `SD_IO_OEn = NOT(SD_IO_EN AND STORAGE_PGOOD)` at the actual buffers. Never drive CMD/DAT against the card during turnaround.

For scratch-media testing, write/read back bounded raw-LBA ranges on a card containing no valuable data. Record card model, capacity, negotiated bus rate and test-range bounds; compare deterministic patterns/checksums, cross sector/multi-sector boundaries, timeout/CRC faults, removal and reset. Check writes only report completion after the card's required busy/completion contract. This stage does not mount exFAT/ext4 or prove filesystem integrity. Do not issue an erase or raw write to a card whose ownership is uncertain.

### Bench rail-loss and isolation tests before the console

Use the step 6b host-power jig to emulate the host 3.3 V/reset inputs while G1 remains disconnected. Monitor TP20 `PWR_FAILn`, TP21 `PWR_GOOD`, TP22 `BUS_SAFE`, TP23 `DATA_OEn`, storage-good and actual data/IRQ/DMARQ enables. Independently test logic/storage/host-rail loss, slow ramps, abrupt primary loss, MCU reset, FPGA unconfigured/held reset and ARM low. With one supply domain at zero, measure unintended supply current/backfeed through buffers and service jigs. Release each condition only after the intended reset/recovery sequence.

U26 senses primary loss before U20 at approximately 4.180 V nominal; U25 senses host 3.3 V; U23/U24's nominal threshold is 3.07 V. Include tolerances and actual finite supervisor response in acceptance bands. In particular, prove storage-invalid disables U207/U208 before the card supply falls below its valid operating range: the measured droop between the supervisor threshold and 2.7 V must cover the worst assertion/logic/buffer delays. A finite supervisor response and a static Boolean test do not establish this margin.

Firmware must stop issuing new writes on `PWR_FAILn`, disarm the bridge and cut the radio request; hardware independently removes bus permits and U34 disables the radio switch. The reserve bank is DNP, and normal bulk capacitors do not guarantee that a card busy interval can finish. Record what actually completed; never acknowledge a durable flush based on elapsed time or PSRAM contents.

## C5 first power-up and removable contact test

After standalone logic/SD/isolation tests pass, power off and install the inspected USB-free U206 with its hard supports and clamp. Check each K201–K216 connection and neighbor isolation at working compression, including the underside BOOT/EN points. Validate alignment/wipe, module bow, extraction procedure and repeat insertion before operating under the shield.

Keep GPIO20 high (buffers disabled), SPI CS high, clock/MOSI defined, and EN/BOOT assertion outputs low. With no external UART driving RX, request GPIO46 high and confirm U71's switched 5 V ramp, module 3.3 V output and current. Test J204 ROM UART recovery first. Only enable U204/U205 after the powered module is stable and both firmware ends have agreed their directions/protocol. Configure the C5 GPIO matrix to the exact table above; CS is D4/GPIO23, IRQ D0/GPIO1 and READY D1/GPIO0.

For shutdown, disable buffers first, stop transactions, then deassert C5 power. Test primary-loss hardware cutoff and absent/unpowered/removal states while carrier logic remains alive. Exercise radio traffic later while measuring input inrush, peak current, ripple, thermal load and effects on SD/G1; measure antenna performance in the actual enclosure. No RF, throughput or total-height guarantee follows from the circuit.

## FPGA link and console introduction

For initial MCU transactions wait for `LINK_READY=1`; use at least 80 ns address/write-data setup, 120 ns asserted WRn/RDn, read sampling no earlier than 100 ns into RDn-low, 20 ns write-data/address hold after WRn rises and 160 ns recovery after a strobe rises. Never assert both strobes. Keep address stable until READY returns high. These are the initial asynchronous-link contract, subject to vendor/bench validation. READY is not FIFO space.

TX words are low byte then high byte, with high-byte commit; RX words are low byte then high byte, with high-byte pop. Firmware must program the exact 24-bit remaining word count at indexed 0x16/0x17/0x18 while DRQ is clear, then establish FIFO/phase/status before setting DRQ. Temporary FIFO empty is not transfer completion. Read 0x15 completion and acknowledge its IRQ flag; use FIFO counts/flags to refill/drain. For a host write, transfer-done means the last host word was received, **not** that the card committed it. The FPGA clears DRQ, sets BSY and removes stale INTRQ at final reception; firmware must drain RX, complete/error-resolve the card operation, then explicitly clear BSY and request INTRQ. Shared Features/Count/LBA taskfile writes must be captured for both device selections. See the full [register map](../fpga/README.md). IDENTIFY data, LBA/count advancement, errors, timeouts and command completion remain MCU work. The bridge keeps one byte per Features/Count/LBA register, so it is **LBA28-only**: IDENTIFY must report at most 0FFFFFFFh sectors with the 48-bit feature bits clear, and every 48-bit command is aborted (`design/blocks/controller.json`). Cards are usable up to 128 GiB less 512 bytes. Between PIO data-in blocks the current logic leaves BSY and DRQ clear until firmware posts the next block; K-UI polls for DRQ and tolerates that, but a strict host would read it as command end (`fpga/README.md`).

Only after the standalone rail/gating/link/storage tests pass, install as follows. Keep the console's power cord unplugged for every soldering and ohms step.

- **I1. Loose flex.** Check each arm against [flex-interface.json](../design/flex-interface.json): every landing pad reads under 1 Ω to its tail finger and OL to its neighbours. Arm A: contacts A2 and A4–A16 go to fingers 2 and 4–16, A18–A21 to fingers 17–20, and fingers 1 and 3 connect to nothing. Arm B: contact Bn (n = 4–21) goes to finger 22 − n and B1 to finger 20; finger 19 connects to nothing. With the gold fingers facing you and the tip pointing away, finger 1 is on the right.
- **I2. Baseline before soldering.** Record the ohms between these CN503 leg pairs: A1–A2, A2–A3, A3–A4, A16–A17, A17–A18, B1–B2, B3–B4, A21–A22 and B21–B22 (A17 has no flex pad and sits between two pads). Cover the A22–A25 and B22–B25 tails (audio and +12 V) with Kapton tape; flex copper passes about 1 mm from them, under coverlay only.
- **I3. After soldering each arm.** Repeat the pair readings. Each must match its baseline; a new reading under 2 Ω is a solder bridge. A1 (3.3 V) and A3/B3 (5 V) have no flex pad, so a bridge from the A2, A4 or B4 pad onto them puts a supply on a G1 signal. Then check end to end: J50 finger 15 to IC501 pin 14 and J51 finger 11 to IC501 pin 15 read under 100 Ω (about 33 Ω); J51 finger 1 (B21) reads under 2 Ω to ground.
- **I4. Tail voltages, tails not plugged in.** Tape over both tails' fingers except the one you are measuring: J51 finger 20 carries console 3.3 V with no fuse (F21 is on the carrier), so a loose tail touching the shield shorts the console's 3.3 V supply. Power the console for under a minute (the shield is off). J51 finger 20 to ground must read 3.20–3.47 V; J50 finger 2 (A2, `ATA_RESETn`) must read about 3.3 V and never 5 V. Power off. On any 5 V or 12 V reading, unplug, find the bridge and repeat I3 and I4.
- **I5. J20 harness.** Before soldering it to J20, power the console for under a minute with the carrier end free and insulated. The wire for J20.1 must read +4.75 to +5.25 V to console ground and the other wire 0 V, and the tap fuse must be in the +5 V wire. Once the flex is plugged in, carrier ground is console ground, so Q20 cannot block a reversed feed.
- **I6. Staged boot, bridge unpowered.** With console power disconnected and the shield off, plug both tails into J50/J51, close the latches and support the carrier independently. Solder thin insulated temporary leads to TP24, TP26, J203.1 and a ground, route them where the shield cannot pinch them, then refit the shield. Leave J20 disconnected and boot the stock console at least ten times, loading several GD-ROM titles. Stock boot and disc reading must be unchanged; TP24 (`+5V_HOLD`) and J203.1 (`+3V3_LOGIC`) must stay below about 0.3 V; TP26 (`CONSOLE_3V3`) must read 3.20–3.47 V. A failure here is added loading or stub ringing (47 kΩ biases, unfitted-U40 stubs, the flex), not bridge logic.

Scope signal levels and host IORDY pull-up/rise time before ARM. Keep the optional BIOS absent for this first integration. Check actual shield clearances without loading CPU/GPU thermal-contact regions or stressing CN503 solder tails. The G2 connector is a routing obstacle, not an electrical bridge interface.

Next power the console with J20 connected and `BRIDGE_ARM=0`, and confirm stock boot and GD-ROM operation. Record the lowest TP26 (`CONSOLE_3V3`) reading while a GD-ROM game loads: at least 3.20 V keeps margin above U25's 3.07 V nominal threshold; if lower, review U25 before arming. Measure 5 V at the tap during spin-up with the bridge at full load later in this step: at least 4.75 V, with U26 never asserting `PWR_FAILn`. TP26 and TP20 (`PWR_FAILn`) are under the shield, so solder thin insulated temporary leads to them and to ground, routed where the shield cannot pinch them, before refitting it. Then use a deliberately limited diagnostic image to release FPGA reset, validate link/READY, and ARM only after all nine hardware-permit terms are valid. After hardware ARM, keep the FPGA activation latch locked and confirm all host output requests remain released. Send the documented two fresh keyed NOP (00h) frames on DEV=1, verify activation through the MCU indexed link, then start device1 IDENTIFY and single-sector PIO mode0 reads. Scope data direction/OE, DIOR/DIOW, device/head changes, IRQ ownership and IORDY during stock device0 use and device1 use. Verify nIEN, explicit relock, SRST, ATA reset, unselection and every rail/recovery kill independently. Reset/safety loss must invalidate activation and stale command/FIFO state; nIEN alone must not unlock it. The host driver must repeat unlock after reset. Exercise shared 0x90 receipt with DEV=0 and DEV=1 and use a real MCU diagnostic result; this hardware has no qualified DASP/PDIAG discovery handshake. The raw TP100–TP138 probes face the motherboard once installed. With the shield off (each power-on under a minute, as in `docs/OWNER-NEXT-STEPS.md` item 2), probe DD0–DD15, DA0–DA2 and DIOR- at IC501's legs inside the BIOS opening (pins per `docs/IC501-candidate-reference.csv`; this is the host side of RA507–RA515), and `BUS_SAFE`/`DATA_OEn` at the top-side TP22/TP23. Before installation, solder temporary insulated leads of at most 30 mm to TP119 (CS0-), TP120 (CS1-), TP122 (DIOW-), TP126 (IORDY), TP127 (INTRQ), TP128 (`DATA_DIR`), TP129 (`INTRQ_OEn`), TP131 (`IORDY_RELEASE`) and the grounds TP137/TP138, lay them flat under the tape and bring them up through the BIOS opening; record the lead length (it adds loading, [raw-g1-debug.md](../design/raw-g1-debug.md)) and remove the leads afterwards. Use the direction/enable/ground mapping in raw-g1-debug.md with a recorded low-capacitance probe setup. Hardware gates must release all bridge outputs without MCU cooperation. Keep the original drive connected throughout coexistence qualification.

Introduce writes only on disposable media after read/coexistence tests pass. K-UI on the Dreamcast is the sole exFAT/ext4 filesystem owner; the MCU is a raw-sector backend and does not independently mount it. PC access requires clean shutdown/unmount and transfer of ownership. Network delivery must pass through K-UI's storage operations rather than a second writer. Define and implement write ordering and ATA flush behavior before exposing normal storage. Repeated/randomized power cuts during data and metadata updates must test acknowledged-write survival and recoverability; reserve-energy sizing and a durability claim depend on those results. DMA and faster PIO remain disabled until vendor timing and measured final-word/ownership/turnaround constraints are satisfied.

## Optional BIOS qualification and programming

BASE_BIOS_DNP leaves the original motherboard mask ROM untouched, optional U40–U45/J40/J41 unpopulated and R409/R410 fitted. This is the first bench/console population. BIOS_OPTION is a separate populated/install variant: remove **R410 before connecting J41/SW40**, because that 1 kΩ pull-up from console 3.3 V would otherwise fight the selector and draw about 3.3 mA in STOCK_RECOVERY (it was a 0 Ω strap before 10 October). Keep JP40 open unless intentionally programming.

Preprogram U40 MX29LV640ETTI-70G before fitting, using a 3.3 V-compatible external programmer that supports this exact x8/byte configuration and verified bank0 image addressing. Read back and compare the complete intended image. No BIOS image or onboard flasher is provided. Original stock recovery resides in the immutable motherboard mask ROM; it is not a purported protected bank of writable U40. Chip erase can destroy all four custom banks.

J40 needs ROM_A10–ROM_A19 and separate motherboard ROM-CE pad/isolated-stock-leg connections in addition to the raw ATA-derived address/data lines. Never join those two CE nodes or connect the original mask ROM to WE. SW40's passive stock path must be continuity-tested while unpowered: STOCK_RECOVERY closes terminals2–1 and5–4; CUSTOM closes2–3 and5–6. Change position only with console power disconnected. The stock position must boot without MCU/FPGA configuration and must hold the bridge hardware interlock low. A missing selector is not a bootable stock bypass.

The current factory FPGA build permits bank0 only. No runtime bank switch or nonzero-bank build is qualified: bank-address changes can corrupt an active ROM fetch, and neither MCU reset nor a cold-start assumption proves a whole-console held-reset interval. Scope U45 NOR reset release, bank stability, CE/OE/data and the console's first fetch together before CUSTOM cold boot. U45 is not a console CPU reset. Optional placement/CE-harness length/first-fetch timing remain separate acceptance tests.

JP40 supplies physical write arm only when U45 has released reset; CUSTOM selection and an explicit FPGA `BIOS_WR_EN` request are also required. Keep JP40 open and request low in normal use. In-system erase/programming requires a future RAM-only flasher, bus serialization, timeout/error recovery and qualified restart/reset workflow. Do not arm it merely to test stock boot. After any intentional programming session, reopen JP40 and verify readback/recovery before normal use. See [BIOS-variant.md](../design/BIOS-variant.md) for the pin/address map and population rules.

## Evidence to retain

For each first article retain assembly photos, fitted/DNP list, measured installed/contact clearances, continuity results, rail/current/thermal traces, reset/OE/power-loss captures, memory/card checksums, source/image hashes, FPGA fit/timing reports and stock-drive/device1 coexistence results. Mark incomplete stages explicitly. A passing CAD/ERC/pad audit or simulation is not installed-fit, boot-compatibility, RF or write-durability evidence.

Primary service references: [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf), [Raspberry Pi hardware design guide](https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008280-DS-1-hardware-design-with-rp2350.pdf), [Seeed official C5 design archive](https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/Seeed_Studio_XIAO_ESP32C5.zip), [Espressif C5 hardware checklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c5/schematic-checklist.html), [Harwin contact drawing](https://content.harwin.com/asset/1801555a-aeb3-408d-b163-bf8c8afc2263/DRG-02334-Technical-Drawing-Datasheet-S7221-45R-pdf.pdf), [AP Memory APS6404L](https://www.apmemory.com/en/downloadFiles/032411212009597427), [Lattice MachXO2 hardware checklist](https://www.latticesemi.com/view_document?document_id=39095), and the selected TI parts linked in [power-and-isolation.md](../design/power-and-isolation.md).
