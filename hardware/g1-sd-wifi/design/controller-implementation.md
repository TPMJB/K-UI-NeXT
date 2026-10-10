# Controller circuit implementation — 10 October 2026

`blocks/controller.json` is the physical-pin circuit contract for the controller, memory, SD socket, C5 module, rail-isolation devices and service contacts. It replaces the old MCU-only ATA allocation. The FPGA owns console ATA timing; the RP2350B never connects directly to the Dreamcast G1 data/control lines.

This circuit is implemented locally for schematic integration. It is not a fabricated or tested board. The CN503 geometry, board envelope, heat-shield clearance, removable C5 retention/contact stack and SD access measurements remain necessary before board layout can be released.

## GPIO and link contract

| RP GPIO | Function | Direction during normal use |
|---|---|---|
| 0–7 | LINK_D0–7 | Bidirectional asynchronous FPGA register/FIFO bus |
| 8, 9 | LINK_WRn, LINK_RDn | MCU outputs |
| 10, 11 | LINK_A0, LINK_A1 | MCU outputs |
| 12, 13 | LINK_IRQ, LINK_READY | FPGA outputs |
| 14, 15 | FPGA_RESETn, BRIDGE_ARM | MCU outputs; frontend owns safe default pulls |
| 16, 17 | C5_UART_TX_MCU, C5_UART_RX_MCU | UART0 TX/RX for recovery proxy |
| 18, 19 | C5_EN_ASSERT, C5_BOOT_ASSERT | Active-high transistor sink controls |
| 20 | C5_IO_OEn | High disables both C5 isolation ICs |
| 23 | USB_VBUS_SENSE | Input, 100k/100k divider from external USB VBUS |
| 24, 25, 26 | SD_DAT_DIR, SD_CMD_DIR, SD_IO_EN | PIO direction controls and active-high enable request |
| 31 | PWR_FAILn | Input from retained power block |
| 32, 33 | SD_CLK_MCU, SD_CMD_MCU | Native SD clock and bidirectional command |
| 34–37 | SD_DAT0_MCU–SD_DAT3_MCU | Native SD data, PIO |
| 38 | SD_DETECTn | Low on card insertion |
| 40–43 | C5_MISO_MCU, C5_CSn_MCU, C5_SCLK_MCU, C5_MOSI_MCU | SPI1 RX, CSn, SCLK, TX |
| 44, 45, 46 | C5_IRQ_MCU, C5_READY_MCU, C5_PWR_EN | Inputs, input, switched-5V enable output |
| 47 | PSRAM_CSn_MCU | QMI/XIP CS1 |

GPIO24–26 are additions required by the SD hardware isolation. A PIO SD driver must manage the two direction signals at CMD/DAT turnaround and set the MCU pin directions consistently. DIR=1 transfers MCU to card; DIR=0 transfers card to MCU. Keep enable low during reset and initialization. Native four-bit SD has not yet been benchmarked; this design does not promise a transfer rate.

## MCU power, clock and memory

U201 includes all 80 perimeter pins and ground pad81. RUN pin35 and the service reset contact join the power supervisor LOGIC_RESETn directly; PWR_FAILn is a separate interrupt. The native 1.1V buck uses the exact marked Abracon AOTA-B201610S3R3-101-T inductor, 4.7µF input/output capacitors, 33Ω VREG_AVDD filter and local bypass copied from Raspberry Pi's R4-S1 reference. Layout and winding-mark orientation must follow that reference; merely connecting the same nets is insufficient. Each IOVDD/DVDD/ADC/USB/QSPI supply terminal has its own local 100nF bypass in this block. ADC_AVDD is supplied even though analog conversion is unused.

X201 is the recommended ABM8-272-T3 12MHz crystal with 1k damping and two15pF C0G load capacitors. Its two case terminals are grounded. USB recovery requires the external crystal.

U202 is the reference W25Q128JVSIQ 128Mbit/16MiB boot flash in the 208mil SOIC package; the selected stock footprint is the5.3×5.3mm body,1.27mm pitch variant. U203 is APS6404L-3SQR-ZR,64Mbit/8MiB, on the same four QSPI data wires and clock with separate GPIO47 chip select. The **ZR package is3×2mm USON-8,0.5mm pitch and0.50mm maximum height**, as verified in the current AP Memory Rev2.7 drawing. It has eight numbered electrical pins; no invented ground/thermal center pin is connected. The center metal/tiebar region is left free of carrier copper.

The PSRAM CS pull-up is4.7k so the RP default GPIO pull-down does not select it during boot; memory CS pulls and BOOTSEL circuitry sit beside the memories. AP Memory recommends a low-ESR1µF VDD bypass; an additional100nF is also fitted. Firmware must observe its150µs initialization wait, reset sequence, maximum8µs CE-low interval and required deselect time. The device supports84MHz linear bursts; usable cache bandwidth will be lower and must be measured. Volatile cache is not a persistence guarantee.

## Native SD rail isolation

J201 is Molex1040310811, a1.42mm push-pull socket. Its eight card contacts, two detection terminals and four shell lands are represented. All CMD/DAT pull-ups reference `+3V3_STORAGE`. Clock/CMD/DAT have33Ω tuning resistors between isolation and socket; values require edge validation after routing.

U207/U208 are SN74AXC4T245PWR. U207 reverses all four DAT channels together. U208 has fixed-forward CLK and a separately reversible CMD channel. The unused CMD-pair lane has both ends grounded so either selected direction has a defined input; the unused fixed-forward output is NC. Both ICs have `+3V3_LOGIC` on A and `+3V3_STORAGE` on B, specified Ioff and automatic isolation when either rail is below100mV.

U209 computes `SD_IO_OEn = NOT(SD_IO_EN AND STORAGE_PGOOD)`. This adds hardware brownout gating rather than requiring firmware to react before every power collapse. The power block supplies STORAGE_PGOOD from TPS3808G33, powered from retained logic and sensing storage with nominal3.07V trip and20ms release delay. A finite supervisor propagation/input-pulse time remains; effective output capacitance and measured rail fall rate must provide adequate3.07→2.7V margin. Rail isolation does not guarantee that an SD write finishes after console power disappears. The hold-up reservoir is separately sized and verified by the power block.

## C5 interface, removal and recovery

U206 is the **complete Seeed Studio XIAO ESP32-C5**, SKU100010048, with its USB connector removed and no tall headers or battery. It is powered through edge pad14 `VBUS` from switched `+5V_C5`. Pad12 is **3V3_OUT**, a module output; it creates `+3V3_C5_IO` for the module side of U204/U205 and must not be tied to the bridge's always-on3.3V rail.

| C5 physical terminal | Actual chip function | Bridge use |
|---|---|---|
| 1 / D0 | GPIO1 | IRQ output |
| 2 / D1 | GPIO0 | READY output |
| 3 / D2 | GPIO25, strap | NC |
| 4 / D3 | GPIO7, strap | NC |
| 5 / D4 | GPIO23 | SPI CS input |
| 6 / D5 | GPIO24 | NC |
| 7 / D6 | GPIO11/U0TXD | Recovery TX |
| 8 / D7 | GPIO12/U0RXD | Recovery RX |
| 9 / D8 | GPIO8 | SPI clock input |
| 10 / D9 | GPIO9 | SPI MISO output |
| 11 / D10 | GPIO10 | SPI MOSI input |
| 12 | 3V3_OUT | Module-side isolation supply |
| 13 | GND | Ground |
| 14 | VBUS | Switched5V input |
| TP2, TP8 | GPIO28/BOOT, EN | Underside recovery contacts |

SPI uses the C5 GPIO matrix; no ROM SPI-slave download support is assumed. C5 application firmware must configure this peripheral pinout and define IRQ/READY behavior. U204/U205 isolate all four forward and four return signals: SPI, IRQ/READY and UART. They are dual-rail AXC devices with Ioff and automatic rail isolation, not an ordinary SN74LVC125A. OE defaults high until MCU firmware deliberately enables it after power is stable; disable before switching C5 off or removing it.

Q201/Q202 pull EN and BOOT low through open-drain MOSFETs and default off. The official PCB has actual underside testpads **TP8=EN and TP2=BOOT/GPIO28**, so no edge pin number is fabricated. Existing module10k EN/BOOT pull-ups and reset capacitor remain. The module's other underside testpads and battery pads are explicitly NC on this carrier; the three unused numbered edge contacts also have NC base lands. The custom footprint coordinates come from the official V1.1 PCB archive, origin at its U9 footprint, with no XY mirror: module components face up and underside pads face carrier contacts. The fingerprint/source coordinate table is stored separately. U206 is a virtual/off-board manual module assembly, with no carrier PCB footprint placed. K201–K216 are sixteen separate Harwin S7221-45R SMT compression contacts: fourteen numbered edges plus TP2/TP8. Each has the actual two solder lands from the Harwin drawing, both terminal1, and F.Paste for assembly. The old full-module copper artwork is a coordinate reference only and is not overlaid on the spring components.

J204 provides direct module-side3.3V UART/BOOT/EN service contacts. Disable MCU buffers when using an external UART jig, power C5 through the board, and use VTREF as a voltage reference rather than a power input. For ROM recovery, assert BOOT low, pulse EN low, release EN, retain the strap at least3ms, then release BOOT. GPIO27 must remain high at reset; it has the chip's default weak pull-up and the module user-LED path, and the bridge does not drive it. UART download is supported on current C5; ROM SPI download is revision-dependent and is not this recovery path.

The hardware supports in-place C5 recovery with the console/bridge powered. A Dreamcast menu update, RP-to-C5 ROM UART programming proxy, signed/CRC image transport and application SPI OTA are **firmware work**, not capabilities implied solely by these wires.

## C5 compression contacts and retention

The selected **S7221-45R** has a2.60×1.00mm body,1.23mm free height,0.90mm recommended working height,0.63mm recommended minimum working height and0.55mm positive stop. The official land pattern is0.55×1.00mm and1.25×1.00mm with0.45mm gap; both lands are one electrical terminal. Contact rating is1A, maximum contact resistance50mΩ, and durability10,000 cycles under the manufacturer's test conditions. The gold finish is suitable for the module's plated pads; actual board contact behavior still requires bench testing.

The footprint origin is the part body centroid, not its moving tip. Nominal tip local coordinate is(-0.70,0)mm, derived from the drawing's0.60mm tip location and2.60mm body length. `sources/xiao-c5-spring-contact-placement.json` and each K component contain the source pad center, rigid group body-centroid offset and KiCad rotation. Edge contacts point inward, preserving2.54mm pitch. Recovery contacts point toward the clear underside region; their tip centers match TP2/TP8 exactly. Compression causes a contact wipe; the nominal-tip drawing, board pad size, alignment and travel must be checked together.

A rigid, removable insulating end clamp is required. Nonconductive hard supports must set the carrier copper-to-module underside gap to0.90mm nominal rather than allowing clamp force to crush springs to their stop. At0.90mm, Harwin specifies minimum0.39N per contact, so sixteen contacts produce at least6.24N total reaction; the bracket must resist that force and assembly tolerances without bowing either PCB. Target XY alignment is±0.10mm. Clamp hooks/supports must avoid the numbered edge contact rows and antenna connection. Exact clamp shape, fasteners, module warpage and support tolerances depend on the owner measurements; no board mounting holes are assumed.

Place this assembly only within the measured8mm region. The installed envelope equals motherboard-to-carrier stand-off + carrier PCB thickness +0.90mm contact gap + actual USB-free module total height + clamp protrusion. Removal/free-contact clearance uses1.23mm. The prior4.48mm measurement includes USB and is not an exact measurement of the USB-free module. Neither that arithmetic nor an uncompressed spring dimension proves fit beneath the shield.

## RP programming contacts

J202 is four copper USB service contacts. D+/D− have27Ω series resistors close to RP pins67/66. External USB VBUS only feeds a100k/100k sense divider; it does not power the board or join console5V. The board must receive console or bench power during USB recovery, and firmware attaches only when VBUS is present. J203 provides SWD, GND, VTREF, RUN and a1k-isolated BOOTSEL request. Ground BOOTSEL, pulse RUN and use the RP ROM USB loader, or recover through SWD independently of the application image. These contacts have no fitted connectors and are excluded from the fitted-component BOM.

## Primary sources and licensing

- [RP2350 datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf), pinout, GPIO mux and QMI chip-select options.
- [Raspberry Pi hardware design guide R4-S1](https://pip-assets.raspberrypi.com/categories/1214-rp2350/documents/RP-008280-DS-1-hardware-design-with-rp2350.pdf), native buck, crystal, flash and BOOTSEL circuit.
- [Abracon marked inductor drawing](https://abracon.com/datasheets/AOTA-B201610S3R3-101-T.pdf).
- [AP Memory APS6404L Rev2.7](https://www.apmemory.com/en/downloadFiles/032411212009597427), package, pinout, initialization, timing and decoupling; copied under `sources/aps6404l.pdf` for reproducible review.
- [Molex1040310811 sales drawing](https://www.molex.com/content/dam/molex/molex-dot-com/products/automated/en-us/salesdrawingpdf/104/104031/1040310811_sd.pdf).
- [TI SN74AXC4T245 RevB](https://www.ti.com/lit/ds/symlink/sn74axc4t245.pdf) and [SN74LVC1G00 RevAC](https://www.ti.com/lit/ds/symlink/sn74lvc1g00.pdf), physical pin maps, directional control and partial-power-down behavior.
- [Nexperia2N7002](https://assets.nexperia.com/documents/data-sheet/2N7002.pdf), SOT23 gate1/source2/drain3.
- [Seeed XIAO C5 official design archive](https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/Seeed_Studio_XIAO_ESP32C5.zip) and [pin map](https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/). Coordinate-derived carrier is adapted from Seeed Studio V1.1 design (CC BY-SA4.0 as marked on the source schematic); attribution and source retained here.
- [Harwin S7221-45R product](https://www.harwin.com/products/S7221-45R) and [official land/height drawing](https://content.harwin.com/asset/1801555a-aeb3-408d-b163-bf8c8afc2263/DRG-02334-Technical-Drawing-Datasheet-S7221-45R-pdf.pdf), retained as `sources/harwin-s7221.pdf`.
- [Espressif C5 hardware checklist](https://docs.espressif.com/projects/esp-hardware-design-guidelines/en/latest/esp32c5/schematic-checklist.html), UART0 and bootstrapping.

Validation performed: JSON parse/schema checks, reference uniqueness, explicit net/NC classification and81-terminal MCU coverage. All placed controller footprint electrical pad sets were also checked through KiCad10 pcbnew; repeated shell, thermal and spring-base lands are handled by their shared electrical numbers. U206 is deliberately off-board and excluded from PCB pad checks. Final integration must run native schematic ERC and netlist checks. Package/footprint correctness does not establish clearance, removable contact force, heat-shield safety, SD signal integrity or firmware timing.
