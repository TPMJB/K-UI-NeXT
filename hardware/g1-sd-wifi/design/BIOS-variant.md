# Optional BIOS circuit and assembly variants

This is an implemented preliminary circuit in [blocks/bios.json](blocks/bios.json), retained on the native schematic. It does not contain BIOS images. The default carrier assembly is **BASE_BIOS_DNP**: the original BIOS stays electrically intact, the additional BIOS tap and all optional BIOS ICs/connectors are unpopulated, and R409/R410 establish the bridge recovery-interlock input. The feature has not been removed from the design.

The **BIOS_OPTION** population is a separate assembly and installation variant. It needs additional motherboard address connections and isolation of the original mask ROM's chip-enable leg. It is not a solder-free BIOS bypass through CN503. Its populated placement, ribbon routing and timing must be checked before fabrication or installation. The base carrier uses CN503 for its ATA storage/Wi-Fi connection; it needs no BIOS-leg soldering.

## Actual circuit

U40 is **MX29LV640ETTI-70G**, a 3 V x8/x16 64 Mbit NOR in normal TSOP48. BYTE# is tied to ground; the chip operates x8. U41 buffers the two FPGA bank outputs into the console-powered NOR domain. U42, U43 and U44 implement the write gate. U45 monitors the unbuffered console 3.3 V supply and resets the NOR during undervoltage. The mechanically selected stock path contains no MCU or FPGA gate.

All optional ICs, their pullups and the NOR use **CONSOLE_3V3**, not the held-up `+3V3_LOGIC` rail. The selected TI logic supports Ioff, so held-up bank/write-control inputs cannot supply the NOR through an unpowered logic output. This depends on the front end also releasing its raw ATA connections when console power disappears. Supply compatibility is VA1-only; a 5 V BIOS/G1 installation is not covered.

### Mechanical stock recovery

J41 carries six connections to remote SW40, a gold-contact **C&K 7201SYZBE** DPDT ON-NONE-ON switch. The switch is off the carrier board. Its first pole routes the lifted motherboard ROM-CE pad directly to exactly one ROM's CE input; the unselected CE input has a 10 kΩ pullup. Its second pole selects the hardware recovery interlock.

| Switch position | Closed terminals | ROM selected | BIOS_RECOVERYn | Bridge response drivers |
| --- | --- | --- | --- | --- |
| STOCK_RECOVERY | 2–1 and 5–4 | Original immutable mask ROM | 0 | Hardware disabled |
| CUSTOM | 2–3 and 5–6 | U40, selected custom bank | 1 | Subject to all other BUS_SAFE checks |

Move this selector only with console power disconnected. The contact diagram is authoritative; do not infer mode from toggle-lever direction. A disconnected selector cable leaves `BIOS_RECOVERYn` low through R409 and both ROM enables pulled high. It disables the bridge, but does not supply a bootable ROM until the stock path is restored.

In STOCK_RECOVERY, original boot-ROM reads remain possible without MCU firmware, FPGA configuration or controller-rail power. The front-end hardware gate must consume `BIOS_RECOVERYn` directly, so a stuck FPGA response request cannot override this interlock. The original GD-ROM is retained. This is a recovery position, not the base variant's ordinary stock-BIOS-plus-bridge operating mode.

**R410 is BASE_ONLY. Remove R410 before connecting the populated selector harness.** R410 is a 1 kΩ pull-up rather than a 0 Ω tie: if it is forgotten, the STOCK_RECOVERY pole still pulls the interlock low, at a cost of about 3.3 mA from the console 3.3 V supply, instead of shorting that supply to ground. With R409's 10 kΩ pull-down, BASE reads about 3.0 V. Base assembly populates R409/R410 but leaves the original motherboard ROM CE connection untouched. Optional assembly keeps R409, omits R410, populates the optional circuit and installs the separate CE/address tap.

### Write protection and supply reset

JP40 is a normally open, low-profile solder-bridge footprint. U45's released reset output reaches `BIOS_FLASH_ARMED` only when JP40 is deliberately shorted. R411 is 100 kΩ, preventing the arm input from floating without creating a 1:1 divider with the reset output's 10 kΩ pullup. Reopen JP40 after programming.

The discrete gates implement:

```
BIOS_WRITE_ALLOWED = BIOS_RECOVERYn AND BIOS_FLASH_ARMED AND BIOS_WR_EN
BIOS_WRITE_BLOCKED = NOT BIOS_WRITE_ALLOWED
BIOS_NOR_WEn = ATA_DIOWn OR BIOS_NOR_CEn OR BIOS_WRITE_BLOCKED
```

Only an authorized write while the custom ROM is selected can pull its WE# low. FPGA `BIOS_WR_EN` has a pulldown; request changes must occur with the G1 bus idle. The service protocol must clear write authorization on reset/error/timeout, serialize ROM programming against G1 DMA and reject unrequested flash operations. That protocol is not claimed complete by the discrete gate.

The original mask ROM receives **no WE connection** and no erase/program voltage. U40 contains only user-installed custom banks. All four custom banks may be erased when programming is armed and authorized; **chip erase can destroy every custom bank**. A bank-select-dependent WE rule does not make a bank immutable. WP#/ACC is pulled to normal 3.3 V and is not claimed to protect an entire BIOS image. Never apply the datasheet's high-voltage acceleration/unprotect modes to this assembly. Immutable recovery resides in the separate original mask ROM.

U45 is **TLV803EB29DBZR**, the pin-1-ground SOT23 version: pin 1 GND, pin 2 open-drain RESET#, pin 3 VDD. The `B` delay is 40 µs nominal, 80 µs maximum; threshold is 2.93 V. Its reset clears the NOR's command state and inhibits the physical program-arm signal on undervoltage. NOR power-up/reset timing and the console's first ROM fetch must be captured together before claiming CUSTOM cold boot. This supervisor does not delay or reset the console CPU, and it does not affect the passive stock path.

## Address wiring and additional tap

The original ROM is x8. Shared G1 wiring is not a generic 16-bit ROM footprint. The custom NOR's mappings are:

| NOR pins/functions | Motherboard/G1 source |
| --- | --- |
| DQ0–DQ7 | ATA_DD0–ATA_DD7 |
| DQ15/A-1 | ATA_DD8 |
| A0–A6 | ATA_DD9–ATA_DD15 |
| A7–A9 | ATA_DA0–ATA_DA2 |
| A10–A19 | Separate ROM_A10–ROM_A19 tap |
| A20/A21 | Buffered BIOS_BANK0/BIOS_BANK1 |
| OE# | ATA_DIORn |
| CE# | Mechanical CUSTOM throw |

DQ8–DQ14 are no-connects in byte mode. The shared bus signals are raw host-side ATA nets, not MCU/FPGA-side buffered copies. The NOR drives the eight low data lines only under its mechanical CE selection and host OE. Higher shared ATA data lines are NOR address inputs in ROM cycles. Include existing motherboard series resistors, branch stubs, the retained GD-ROM and the switch harness in the loading/timing analysis.

J40 is a **FH12-14S-0.5SH(55)** 14-way bottom-contact FPC receptacle. Pins 1–10 carry ROM_A10–ROM_A19; pin 11 is the motherboard ROM-CE pad; pin 12 returns to the isolated original CE leg; pins 13–14 are grounds. The added address tap targets are:

| Address | Original IC501 pin | Address | Original IC501 pin |
| --- | --- | --- | --- |
| A10 | 40 | A15 | 35 |
| A11 | 39 | A16 | 34 |
| A12 | 38 | A17 | 3 |
| A13 | 37 | A18 | 2 |
| A14 | 36 | A19 | 43 |

ROM CE is IC501 pin 12. The motherboard pad and lifted chip leg are different nodes after installation: `ROM_CEn` and `STOCK_CEn`. Joining them defeats selection and can create bus contention. Verify actual board continuity and pin-1 orientation before installing this tap. The traced VA1 reference remains a candidate map, not a measurement of the owner's board.

Physical A20 and A21 select four **2 MiB** custom images in x8 mode because A-1 is byte bit 0, A0–A19 are byte bits 1–20, and A20/A21 are byte bits 21/22. Default pull states select bank 0. A firmware reset must not silently change the selected bank while the CPU or a game reads ROM/font data. The front-end session latch and reset domain must enforce this independently of MCU restarts. An early power-fail warning must not reset the bank while the console can still fetch ROM data.

The implemented front-end build fixes the bank bits at FPGA configuration and provides no runtime bank-write register. The factory build selects bank 0, matching the external pull states. Initial bring-up must retain bank 0 unless the whole console is held in a verified reset state: a nonzero configuration value would change the initially pulled bank after FPGA configuration, potentially during a ROM fetch. Selection of other banks and K-UI bank switching remain deferred until a complete CPU-reset or RAM-only, drained-bus restart protocol exists. ATA RESETn/SRST alone is not that protocol.

## Height and footprint checks

U40's normal TSOP48 has 18.4 × 12.0 mm nominal body, 20.2 × 12.1 mm maximum lead/body envelope, 0.5 mm lead pitch and 1.2 mm maximum seated height. The footprint is `Package_SO:TSOP-I-48_18.4x12mm_P0.5mm`. J40/J41 are horizontal FH12 connectors, 2.0 mm high, 0.5 mm pitch, mating 0.3 mm FPC. They replace elevated pin headers; JP40 is a solder bridge. Their nominal height can fit a 6 mm region only after carrier PCB, mounting, solder, tolerances, actuator clearance and cable bends are included. Do not claim the populated optional variant fits solely from these component heights.

J41 is **FH12-6S-0.5SH(55)**. Its FPC pins map one-for-one to SW40 terminals 1–6. Exact cable contact-facing orientation and the remote switch adapter must be verified; arbitrary FFC end orientation may reverse the mapping. SW40's large panel-toggle body is outside the carrier/shield envelope. Its external mounting is an installation choice, not an on-carrier component placement.

## Primary references

- [RDC traced VA1 schematic](https://consolemods.org/wiki/images/2/27/Dreamcast_VA1_FULL.pdf) and [author's caveats](https://acidmods.com/forum/index.php?topic=44892.0).
- [Macronix MX29LV640E datasheet](https://www.macronix.com/Lists/Datasheet/Attachments/8514/MX29LV640E%20T-B%2C%203V%2C%2064Mb%2C%20v1.7.pdf), normal TSOP48 pinout, bank organization, protection and package drawing.
- TI [SN74LVC2G125](https://www.ti.com/lit/ds/symlink/sn74lvc2g125.pdf), [SN74LVC1G11](https://www.ti.com/lit/ds/symlink/sn74lvc1g11.pdf), [SN74LVC1G04](https://www.ti.com/lit/ds/symlink/sn74lvc1g04.pdf), [SN74LVC1G332](https://www.ti.com/lit/ds/symlink/sn74lvc1g332.pdf), and [TLV803E](https://www.ti.com/lit/ds/symlink/tlv803e.pdf).
- Hirose [14-way FH12](https://www.hirose.com/en/product/p/CL0586-0533-0-55) and [6-way FH12](https://www.hirose.com/en/product/p/CL0586-0582-5-55).
- [C&K 7000 toggle datasheet](https://www.ckswitches.com/media/1394/7000toggle.pdf), DPDT connected terminals and gold-contact ordering option.

This circuit is reviewable hardware, with an explicit population rule and physical recovery path. Electrical validation, warm boot, populated placement, installation geometry and release-to-manufacture checks remain outstanding.
