# K-UI G1 microSD and Wi-Fi bridge proposal

**Architecture proposal and controller starter, updated 9 October 2026. Physical measurements are pending.**

Build a custom Dreamcast VA1 board that exposes a microSD card as an ATA slave through an RP2350B, retains the original 3.3 V GD-ROM, and carries a socketed Seeed XIAO ESP32-C5 for Wi-Fi. Optional dual BIOS belongs in the mechanical and electrical planning. This is the successor proposal to the separate [CF board](../cf-board/README.md); the existing CF design remains an independent, unvalidated option.

This folder makes the proposal available for independent review. It contains the original controller foundation and the decisions made after it was drafted. It does **not** contain completed G1 buffer, SD, C5, reserve-power or dual-BIOS circuits, an ATA-emulation firmware implementation, or a production PCB.

## Read and review

| File | Purpose |
| --- | --- |
| [REVIEW.md](docs/REVIEW.md) | Self-contained review request for Claude or a hardware engineer |
| [Review disposition](docs/review-disposition-2026-10-09.md) | Claude's review, accepted requirements, corrections and revised bring-up order |
| [GPIO allocation](controller/K-UI-G1-GPIO-RevA.csv) | All 48 RP2350B GPIOs; 47 assigned and one spare |
| [CN503 reference](docs/CN503-reference.csv) | Logical connector-to-ATA-to-MCU mapping, without a qualified physical tap drawing |
| [START-HERE.txt](controller/START-HERE.txt) | Opening guide and current mounting/power notes |
| [KiCad project](controller/KUI-G1-Bridge-RevA.kicad_pro) and [schematic](controller/KUI-G1-Bridge-RevA.kicad_sch) | Controller, boot flash, clock and bench programming foundation; requires KiCad 10 or newer |
| [CHECKS.txt](controller/CHECKS.txt) | Original structural checks and their limits |
| [Initial pin-plan PDF](docs/initial-pin-plan-2026-10-06.pdf) | Historical 6 October draft; current Markdown and START-HERE notes supersede its mounting assumptions |

## Architecture

| Part | Proposed responsibility |
| --- | --- |
| Dreamcast SH-4 / K-UI | ATA host; sole owner of the mounted exFAT or ext4 filesystem |
| Original GD-ROM | Remains ATA device 0, with normal disc operation retained |
| RP2350B | ATA device 1; raw-sector SD backend, buffering, command handling and bus isolation controls |
| microSD | Native four-bit interface to RP2350B; game sectors stored directly on the card |
| XIAO ESP32-C5 | Wi-Fi packet transport over a separate SPI link to RP2350B |
| Optional second BIOS | Independent stock/custom selection and chip-enable isolation; absent from the current schematic |

The bridge presents the actual SD card as a block device, rather than requiring a FAT-formatted card containing virtual-drive image files. exFAT remains a supported user choice and ext4 is the proposed option for journaled writes. FAT32 is not the target setup or validation workflow.

Network uploads must pass through K-UI's filesystem owner. The C5 must not independently mount and modify the same card. Retail-game networking is a separate compatibility project: a K-UI G1 packet driver does not make existing games recognize a modem or Broadband Adapter.

Storage is the first implementation milestone. Establish identification, verified PIO reads and coexistence with the original drive before DMA, writes and networking. Schedule packet work around game reads instead of promising a fixed division of bus bandwidth. No sustained throughput or game-compatibility claim has been measured on this hardware.

## GPIO plan and bus behavior

| RP2350B GPIO | Proposed use |
| --- | --- |
| 0-15 | ATA D0-D15 |
| 16-27 | ATA register address, chip selects, read/write, reset, IORDY, IRQ and DMA handshake |
| 28-30 | Data-buffer enable, direction and response gate |
| 31 | Early power-failure input |
| 32-37 | SD CLK, CMD and D0-D3, proposed PIO2 implementation |
| 38-39 | SD detect and optional status LED |
| 40-43 | C5 SPI1 RX, CS, SCK and TX |
| 44-46 | C5 IRQ, ready and switched-power enable |
| 47 | Spare |

Independent review supports the logical connector mapping, PIO pin windows and SPI1 mux. The concrete PIO programs, output ownership between PIO blocks, DMA resources and response timing remain unproven. See the review disposition before implementing its proposed state-machine split or output gate. The LED and card-detect assignment may be reclaimed if the electrical implementation needs more controls. The optional secondary flash circuit from the Raspberry Pi reference was removed to free GPIO0; the dedicated boot flash remains.

The bridge must track device selection and obey the ATA rules for shared data, IORDY, IRQ and DMA lines. It must not answer device-0 cycles or contend with the drive. Default isolation must be enforced in hardware during MCU boot, reset and power failure, including when MCU pins are unconfigured. DMA acknowledgment must be handled even when register chip selects are inactive. The output gate must preserve data hold and the final DMA word after DMARQ is negated. Exact gate topology, timing and supported transfer modes are unresolved.

## Power and interrupted writes

VA1 G1 signalling is a 3.3 V target. A 5 V supply input is a separate option, not approval for 5 V G1 signalling or VA0 compatibility. The proposed board can use console 5 V with local 3.3 V regulation, or console 3.3 V for MCU/SD plus a separate 5 V C5 feed. A 3.3 V-only complete-board configuration needs a qualified C5 supply solution. The XIAO's 3V3_OUT is not treated as an approved input. USB programming must not back-feed the console.

Plan upstream power-loss detection, isolation and reserve energy for RP2350B plus SD, with C5 switched off first. Reserve capacity awaits measured consumption and SD busy times; no capacitor size or power-loss guarantee is established.

Planned write protection includes read-only collection access during ordinary playback, explicit write sessions for dumping/uploads, temporary files followed by verification and final commit, correct block ordering, honest ATA cache/flush semantics through the entire SD path, and ext4 journal recovery before subsequent writes. Optional SD cache stays disabled until its flush semantics are implemented and qualified. Journal support must be demonstrated in the actual K-UI port. Reserve energy cannot save data still in Dreamcast RAM, and journaling cannot guarantee against failure inside a consumer SD controller.

## Mounting and attachment: still unresolved

The owner's photo shows VA1 mainboard **837-13778-02**, BIOS **IC501** and G1 connector **CN503**. Preferred placement is above the motherboard near IC501/CN503, below the normally seated upper metal shield. The shield is the local height constraint.

CN503 has two 25-pin rows carrying the required signals. The BIOS-facing tails are visible in the supplied photo; access to the board-edge row is not yet qualified. Logical pin mapping does not establish a practical soldering method. If direct top-side access proves unsuitable, investigate a thin underside tap and short flex to the upper board, with signal integrity and shield clearance checked. The controller/C5 assembly is not assumed to fit under the motherboard.

The DragonCity numbered-circle picture discussed in the design conversation is the underside of a **GD-ROM PCB**, not the motherboard. Its numbers refer to **IDE cable pins**, not CN503 contacts or RP2350B GPIOs. Data/control points map to corresponding G1 signals. Its IDE-pin-1-to-3.3-V tie is a reset workaround and is not copied into the MCU design; real G1 reset is A2.

Required physical evidence remains:

- Minimum gap from the top of IC501 to the underside of the installed upper shield.
- Minimum gap from motherboard PCB surface beside IC501 toward CN503 to that shield, accounting for existing components.
- Access to the outer CN503 row; finished carrier, C5 socket/module and SD-socket heights; card, USB and antenna access.

Optional dual BIOS requires additional connections and independent stock/custom selection. The RP2350B GPIO table is not a BIOS-bus pin allocation. A ROM piggyback alone does not supply every ATA control, DMA and interrupt signal. Fit, wiring and fallback behavior remain to be designed.

## Next deliverables

1. Close the review's output-ownership and timing questions, design isolation, and qualify a measurement fixture. The earlier CF board is unbuilt and unvalidated; a working CF rig is not a prerequisite assumed to exist.
2. Finish G1 buffers, SD/C5 interfaces, console/USB power and power-fail circuits; choose qualified production footprints and run ERC.
3. Obtain measurements and connector access evidence, then choose the attachment, outline and component heights.
4. Build a bench prototype: identify, read and hash sectors, test drive coexistence, then qualify DMA and writes.
5. Demonstrate filesystem recovery and repeated power cuts before adding packet transport and pursuing retail-game networking.

## Primary references and licensing

- [Raspberry Pi RP2350 documentation and reference design](https://pip.raspberrypi.com/categories/1214-rp2350), [datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf), [hardware-design guide](https://datasheets.raspberrypi.com/rp2350/hardware-design-with-rp2350.pdf).
- [Seeed XIAO ESP32-C5 documentation](https://wiki.seeedstudio.com/xiao_esp32c5_getting_started/) and [module schematic](https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/Seeed_Studio_XIAO_ESP32C5.pdf).
- [RDC VA1 scans and traced schematics](https://acidmods.com/forum/index.php?topic=44892.0): reverse-engineered references, not a substitute for continuity checks on an installation.
- [DragonCity IDE installation](https://dragoncity17.wordpress.com/2016/02/12/dreamcast-lire-les-jeux-depuis-un-disque-dur-sans-gdrom-avec-dreamshell/), [connector pinout in G1-ATA guide](https://dragoncity17.wordpress.com/2018/03/16/sega-dreamcast-g1-ata/), and [retained-drive GD-IDE installation](https://dragoncity17.wordpress.com/2018/03/17/sega-dreamcast-gd-ide/).
- [KallistiOS G1 ATA driver](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/hardware/g1ata.c).
- [lwext4 upstream](https://github.com/gkostka/lwext4) and [Linux ext4 journal documentation](https://www.kernel.org/doc/html/latest/filesystems/ext4/journal.html).

The controller schematic and bundled reference symbols derive from Raspberry Pi's RP2350B Minimal R4-S1 reference. Preserve [the included MIT license](controller/LICENSE-Raspberry-Pi-MIT.txt). No DragonCity PCB layout or firmware is included. References document prior work; they do not establish our board's hardware validation.
