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

For APS6404L-3SQR, package choice matters: **USON ZR is 3 × 2 mm and 0.6 mm maximum height; SOP SN is 1.45 mm maximum height**. Qualify power-up, burst/page and refresh timings for the exact part. [AP Memory datasheet, package drawings and interface requirements](https://www.apmemory.com/en/downloadFiles/032411212009597427). These are component heights; carrier PCB, solder, insulation and sockets still count toward clearance.

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

## Connector-only storage and boot behavior

The baseline SD/Wi-Fi bridge takes its complete ATA interface from CN503 and **does not require soldering to IC501**. The BIOS opening is a mechanical clearance feature. The alternate IC501 tap map above is retained for reference, not a requirement for the preferred connector attachment.

Sharing G1 does not expose the complete BIOS interface at CN503. The traced VA1 drawing shows ten additional ROM address lines, A10-A19, absent from the ATA connector, and a separate ROM chip-enable net at IC501 pin 12 rather than ATA CS0/CS1. Optional ROM replacement needs those signals plus mutually exclusive stock/custom enables and the independent recovery path below. Driving CN503 alone cannot reliably replace or disable the existing ROM.

[GDEMU](https://gdemu.wordpress.com/about/) replaces the optical drive and requires no custom BIOS. Its [installation](https://gdemu.wordpress.com/installation/gdemu-installation/) removes the GD-ROM and plugs the replacement into its motherboard socket; its [operation](https://gdemu.wordpress.com/operation/gdemu-operation/) boots image 01 at power-on. The inference is that it supplies the drive and disc behavior expected by the original BIOS, not that it replaces BIOS execution.

Our proposed device-1 ATA sector bridge retains the original GD-ROM as device 0. It is not a GD-ROM emulator and does not by itself create a stock-BIOS boot path. Use a disc bootstrap on the retained drive to start K-UI/DreamShell for initial validation, then access the bridge through compatible software. Optional custom-BIOS boot and a possible future optical-emulation architecture are separate designs; neither is implemented or promised by this proposal.

## BIOS recovery and programming

Keep the original immutable mask ROM as the preferred stock recovery device. A physical selector must route a stock chip-enable path and disable custom/bridge outputs without depending on FPGA logic being configured. Mutually exclusive chip enables and unpowered behavior require a circuit, not a software promise.

For an x8/x16 64 Mbit NOR such as MX29LV640E in byte mode, Q15/A-1 supplies byte bit 0; A0-A19 supply bits 1-20, so physical A20/A21 can select four 2 MiB images. This banking claim is correct for that organization. Match BYTE mode, address mapping, pinout and voltage in an explicit adapter schematic; a TSOP48 NOR is not a drop-in assertion about IC501's footprint.

**Blocking WE only while bank 0 is mapped is insufficient protection.** A whole-chip erase commanded from another bank can erase it. Device-enforced sector protection/command filtering would need qualification; WP alone does not protect an arbitrary full image. A writable stock copy is weaker than the original mask ROM. [Macronix MX29LV640E datasheet, pinout, protection and chip-erase sections](https://www.macronix.com/Lists/Datasheet/Attachments/8514/MX29LV640E%20T-B%2C%203V%2C%2064Mb%2C%20v1.7.pdf).

Session bank selection is a candidate, not a working warm reboot. Stop/drain G1 activity and prevent ROM calls, including fonts and flash services, before switching/programming. Define a complete restart rather than assuming a jump to the reset vector resets all peripherals. Specify separately how ATA SRST, G1 hardware reset, MCU reset, FPGA reconfiguration, console reset and power loss affect the session latch and write gate. Selection must stay stable while games use ROM in place. [KOS font-lock contract](https://github.com/KallistiOS/KallistiOS/blob/master/kernel/arch/dreamcast/include/dc/syscalls.h) confirms exclusive G1 ownership is required for font access. Validate every installed image's boot/unlock/optical behavior; distribute tools and patches rather than BIOS images.

## Provisional shield-clearance report

On 9 October 2026 the owner reported approximately 8.62 mm from motherboard to shield, 0.62 mm shield-sheet thickness and 1.4 mm of additional metal near IC501/CN503. After briefly questioning the readings and checking the motherboard's approximately 1.62 mm thickness with digital calipers, the owner reaffirmed the measurements. The initial arithmetic gives an 8.00 mm general gap and 6.60 mm local gap. The owner later specified **6.00 mm effective working height in the lower-metal region and 8.00 mm outside it**. Retain 6.60 mm as the earlier calculated/report history. The owner now confirms the qualitative height map: **6.00 mm around IC501/CN503, 8.00 mm outside the lower-metal area, and zero usable carrier clearance over the two thermal-contact chip footprints**. Complete assembly fit remains pending.

| Reported item | Value | Measurement reference |
| --- | --- | --- |
| Motherboard-to-shield measurement | About 8.62 mm | Calculation assumes PCB top surface to main shield outer face |
| Main shield sheet | About 0.62 mm | Subtracted from the outer-face measurement |
| Extra metal near BIOS/G1 | About 1.4 mm | Calculation assumes downward projection into that same gap |
| Motherboard PCB thickness | About 1.62 mm | Separate board-thickness reading; not subtracted from a PCB-top clearance |
| Clearance below main shield | About 8.00 mm | 8.62 - 0.62 |
| Earlier calculated gap beneath additional metal | About 6.60 mm | 8.00 - 1.40; retained as initial report |
| Latest effective local working height | About 6.00 mm | Owner-directed planning ceiling in the lower-metal region |

The reference assumptions stay explicit: an inner-face measurement already excludes the shield sheet thickness, and metal thickness must correspond to actual projection into the candidate footprint. A plausible PCB-thickness reading supports the scale of the measurements; it does not independently verify the clearance endpoints. No photo-derived dimension has been verified for this report.

The remaining mechanical work is to map the smallest PCB-to-metal gap across the proposed footprint and locate the extra metal's overlap. The owner's preferred cutout carrier below avoids placing the board over IC501, so BIOS-top clearance is needed only if a later layout overhangs or stacks anything above that package. With the console unplugged and shield seated normally, a loose stack of thin nonconductive card can serve as a gauge where caliper access is difficult: increase until it just touches without lifting or forcing the shield, then measure the stack outside the console.

Budget the complete stack within those local gaps: mounting/supports and insulation, carrier PCB, lower-side components where present, and socket/module or tallest top-side part. A PCB-to-metal gap is not the spare height above the existing BIOS. The socketed C5 needs its actual mounted stack height checked before choosing the PCB outline or releasing fabrication files. Retain the shield and its normal thermal contacts in the fit check.

## BIOS cutout and C5 stack

The owner proposes a carrier with a cutout around the existing IC501 package, with optional short signal taps soldered to its legs. Keep the original BIOS in place and put the carrier in the adjacent free area rather than over the package. The later confirmation of access to all CN503 contacts makes the connector the preferred full-G1 tap; the BIOS cutout remains part of the mechanical outline. Use the owner-reported package/lead dimensions below for the initial keepout, then add edge/alignment allowance and map surrounding parts and available board area. Support and retain the carrier independently so its weight, socket insertion and cable forces do not load the BIOS legs. Leg access, continuity, solder-pad geometry and routing remain to be qualified.

The alternative IC501 tap map remains useful: it exposes 20 candidate ATA nets, while eight controls need other taps as listed above. Where BIOS taps are used, keep added routing short and evaluate loading/stubs on the Holly side of the existing connector series resistors. Do not duplicate G1 taps at both IC501 and CN503 without a concrete reason. Optional custom BIOS components can occupy adjacent carrier space; stock recovery still needs the independent physical path already specified.

The owner measured the C5 module's total height, including its USB connector, as **4.48 mm** on 9 October 2026. It is a module-envelope measurement, not the complete mounted carrier/socket stack.

| Clearance region | PCB-to-metal gap | Space remaining after 4.48 mm C5 envelope |
| --- | --- | --- |
| Below main shield | 8.00 mm | 3.52 mm |
| Owner-confirmed working ceiling around IC501/CN503 | 6.00 mm | 1.52 mm |

The remaining space must cover carrier elevation above the motherboard, carrier thickness, the actual gap from carrier top to the module's lowest point, insulation and a fit/tolerance allowance. The owner reports other chips are below approximately 2 mm; wherever the carrier passes over them, their heights determine part of its required elevation and consume this budget. Include lower-side parts or solder where they set either reference surface. A connector's quoted height may use a different datum; use the mounted geometry rather than adding dimensions that overlap.

Illustrative carrier-thickness arithmetic, before every other addition:

| Assumed carrier PCB thickness | Remaining in 8.00 mm region | Remaining in 6.00 mm region |
| --- | --- | --- |
| 0.80 mm | 2.72 mm | 0.72 mm |
| 1.00 mm | 2.52 mm | 0.52 mm |
| 1.60 mm | 1.92 mm | -0.08 mm (already exceeds ceiling) |

These are planning examples, not a selected PCB stackup or proof that a socket fits. The 6.00 mm column applies around IC501/CN503; it never applies over the two zero-clearance thermal-contact footprints. A 1.60 mm carrier plus the reported C5 envelope already exceeds the latest 6.00 mm local ceiling before supports or a socket are added. Place the C5 in the 8.00 mm region where the measured footprint permits it, then qualify a low-height removable connection and independent carrier supports. Keep the USB connector in the baseline. The owner is willing to remove it if necessary, but removal helps only if it determines the tallest point; remeasure the remaining module envelope before claiming a reduced height. It does not remove carrier or socket height.

The owner is willing to consider physical metal modification as a last resort. Keep the **intact installed shield and stock thermal contacts** as the proposal baseline. First evaluate a thin carrier and low-height removable interface, with the C5 located in a usable 8.00 mm area on a carrier extension or separate small mount with a short interconnect. A concrete placement, mounted stack and routing check must precede deciding that USB removal or metal modification is needed; neither is a prerequisite established by these measurements.

## Owner-reported package and contact dimensions

Additional measurements supplied on 9 October 2026:

| Item | Owner report | Use in proposal |
| --- | --- | --- |
| IC501 black body length | 26.68 mm | Initial body keepout |
| IC501 black body width | 12.65 mm | Initial body keepout; owner's “black PCB” description interpreted as package body |
| IC501 outer pin-edge span | 16.66 mm across the width | Full lead envelope for a clearance-only cutout |
| IC501 height | Roughly 2 mm above motherboard | Excluded from carrier stack where the cutout clears the package |
| Other chip heights | Significantly below the BIOS height | Component positions/heights still matter beneath solid carrier areas |
| CN503 length × maximum body width | 38.67 × 6.56 mm | Owner-corrected full body envelope; wider end sections |
| CN503 narrower-section width | 5.03 mm | Initial width at the pin-run section; not maximum body width |
| CN503 pin-run length | 24.39 mm, centered within the 38.67 mm length | Initial contact-run location; measurement endpoints not specified |
| CN503 tail height | Basically flush with motherboard | Near PCB-surface contact plane; qualitative, not a precision 0.00 mm value |
| General usable area | Outside the CPU/GPU thermal-contact area | Candidate placement region, subject to components, metal geometry and independent supports |

The initial IC501 body rectangle is 26.68 × 12.65 mm. A clearance-only opening must also clear the reported 16.66 mm outer lead span and an assembly/alignment allowance; a narrower opening intended to overlap/tap the leads needs an explicit landing geometry. Do not derive lead pitch from the body dimensions or the difference between body and lead span. These reported sizes establish a concept keepout, not a final routed opening or qualified IC footprint.

## Lower-metal region and connector placement

The reference chip is now identified in the inline `62256.jpg` photo and confirmed by the owner: the large square chip toward the upper left marked **SEGA 315-6267**. The owner also identifies the large chip directly below it with thermal compound. **Both chip/thermal-contact footprints have zero usable carrier clearance** and are excluded from placement; preserve their installed metal contact.

The area description uses the upper chip's north and west edges through the south/east board edges for the lower-metal region. The owner confirms approximately **6.00 mm usable working height around IC501/CN503**, **8.00 mm outside that metal area**, and **zero usable carrier clearance at the two thermal-contact chip footprints**. The height categories are resolved; transfer the described boundaries and keepouts into the measured outline.

| Region | Usable carrier height | Status |
| --- | --- | --- |
| Upper SEGA 315-6267 and lower thermal-compound chip contact footprints | 0 mm | Owner-confirmed placement keepouts |
| Usable space outside the owner-identified metal area | About 8.00 mm | Owner-reported clearance |
| Extra-metal space around IC501/CN503 | About 6.00 mm | Owner-confirmed working limit |

The replacement photo is visible inline and supplies placement context. It does not establish measured X/Y offsets or a calibrated shield-boundary outline.

CN503 is reported as 38.67 mm long, 5.03 mm wide in the narrower pin-run section and **6.56 mm at its wider ends**. Use **38.67 × 6.56 mm** as the initial full body keepout, before clearance/alignment allowance. A shaped opening can narrow between the ends only after the actual width-transition positions are measured; do not assume those transitions coincide with the contact-run endpoints. The pin run remains centered and 24.39 mm long. Centering gives provisional end margins of (38.67 - 24.39) / 2 = **7.14 mm**. This is a plan-location calculation only. The span may be measured over outside edges rather than first/last contact centers; do not divide it by 24 and treat the result as a qualified pin pitch.

The remaining concept-placement input is the X/Y offset between clearly identified CN503 and IC501 reference corners. The chip reference and qualitative zero/6.00/8.00 mm height map are resolved. Exact tail-row spacing, contact width/pitch, body-versus-tail envelope and joining tolerances remain footprint/fabrication checks.

## CN503 cutout and remaining measurements

The owner reports that **all G1/CN503 pins are accessible**, including both rows. Use a second carrier cutout/notch around CN503 as the preferred outline candidate, with individually aligned solder contacts at the exposed tails. Accessibility is owner-confirmed; the precise footprint and joining geometry are not yet qualified. This makes the full connector bus the preferred source for the bridge. IC501 leg taps are optional for BIOS functions or an alternative attachment, rather than a necessary workaround for an inaccessible connector row.

The owner has now answered the coarse height question: the CN503 tails are essentially at the motherboard surface. Ordinary top-side pads on a carrier laid above that surface would be raised by the carrier thickness and any support/insulation gap. The proposed flush attachment therefore needs a defined solder cross-section, rather than assuming flat top pads will be coplanar with the tails.

Evaluate an accessible edge/underside landing with a visible solder fillet, potentially using an open notch and plated edge contacts. Castellated edges are a documented way to join a daughterboard to mainboard pads or component pins, but their hole, spacing and edge rules depend on the fabricator. [JLCPCB castellation guide](https://jlcpcb.com/blog/castellated-pcbs-introduction-and-design-requirements); [PCBWay half-hole guide](https://www.pcbway.com/pcb_prototype/What_are_Plated_Half_Holes_Castellated_Holes_.html). These generic references do not qualify closed internal-cutout castellations or this connector's unmeasured pitch. Bottom-side overlap contacts need actual installation/solder/inspection access. A thin local landing/flex or very short formed links to the carrier remain alternatives if a direct rigid edge joint is unsuitable.

Define lead/pad overlap, solder access, insulation, edge clearance and the precise contact-height tolerance before selecting the joining geometry. Support the carrier independently of the solder joints and connector/BIOS legs.

The architecture proposal can proceed with the reported dimensions and latest height budgets. The following remaining inputs locate and qualify the carrier:

| Measurement bundle | Minimum useful information | Purpose |
| --- | --- | --- |
| Relative position | X/Y offset between identified CN503 and IC501 corners; use the reported 38.67 × 6.56 mm maximum body envelope and centered 24.39 mm contact run | Locate both cutouts in one coordinate system |
| Placement/metal map | Transfer the confirmed zero-clearance footprints, 6.00 mm IC501/CN503 area and 8.00 mm outer area to the measured outline; locate supports | Apply height regions to usable board/C5 placement |
| Components beneath carrier | Positions and heights wherever solid carrier would pass over existing parts | Choose local cutouts or elevation within the height budget |
| C5 mounted stack | Select socket/contact from a manufacturer drawing; check assembled module height above carrier with carrier thickness/elevation counted separately | Verify the 4.48 mm module within the complete stack |

A dimensioned sketch is enough for the first outline; a square-on photo with a metric ruler in the PCB plane can supplement it. If the removable C5 connector is not chosen, select it from a manufacturer drawing first rather than requiring the owner to buy one just to measure. No additional BIOS-top or GD-ROM height measurement is needed for a carrier that clears both bodies and stays below the normally installed shield.

Before fabrication, qualify CN503 tail pitch, row spacing, tail width/exposed length, contact-height tolerance and pin orientation, plus the corresponding IC501 dimensions wherever leg taps are retained. For a uniform 25-contact row, measuring between matching edges of the first and last tail and dividing by 24 is a useful pitch check; it is not a substitute for the complete footprint and alignment tolerance. The existing CSV is a logical net reference, not that mechanical footprint.

## Remote C5, RAM provision and factory assembly

The owner proposes moving the C5 north/west under the PSU, removing its USB connector, or using a ribbon to mount it near G2. Evaluate a **remote removable C5** while keeping the G1 front end close to CN503. The remote link carries C5 power/ground, SPI and handshakes; placement near G2 does not imply connecting to the G2 bus. The ribbon's route, return paths, signal integrity, supply drop, unpowered isolation and tested SPI rate require a concrete design. Extending the ATA bus or relocating the native SD socket would be a different routing decision.

| C5 candidate | Information needed before choosing it |
| --- | --- |
| North/west toward PSU | Local footprint, clearance to the installed PSU/metalwork, safe support location and cable/antenna route; the motherboard's 8 mm area does not establish clearance beneath the PSU |
| USB connector removed | Remeasure tallest remaining point and retain accessible programming/service connections; removal does not eliminate carrier/socket height |
| Separate mount near G2 | Local length/width/height, support position and practical ribbon route length/bends; check fit with retained modem/expansion hardware |

Provision **8 MiB of bridge PSRAM** in the Rev A requirements, with a factory-populated option and an unpopulated variant. The candidate is **APS6404L-3SQR-ZR**, a 64 Mbit device (8 MiB), in a 3 × 2 mm USON-8 package with 0.6 mm maximum height. It is a bare SMT chip on our carrier, not a separate RAM module. Its function is raw-sector buffering/read-ahead; it does not expand Dreamcast system RAM. QMI wiring, chip-select allocation, sharing with firmware flash, cache coherence and bandwidth still require the design and tests described above. [AP Memory product family](https://www.apmemory.com/en/product/iotram/SPIQSPI); [manufacturer package drawing, mirrored by Mouser](https://www.mouser.com/datasheet/3/4815/1/APS6404L_3SQR.pdf). Reconcile the latest ordering suffix, footprint and pinout before final BOM release.

Custom boards can be ordered with SMT components factory-soldered. The assembly release needs the finished PCB manufacturing files, exact BOM and component placement/rotation data, plus clear polarity/assembly information. [PCBWay assembly requirements](https://www.pcbway.com/helpcenter/pcb_assembly_ordering/What_files_are_requested_for_assembly_production_.html); [JLCPCB part-sourcing/consignment workflow](https://jlcpcb.com/help/article/how-to-use-my-own-parts-for-pcb-assembly-order). Confirm sourcing and package support for the actual BOM; these references are not a stock or price quote. No assembler or order is selected here.

## Remaining inputs before PCB layout

The known body dimensions and zero/6/8 mm height regions are sufficient for architecture and schematic work. Before fixing the outline and console attachment, collect:

1. **Relative placement:** CN503's X/Y offset from IC501. In the existing photo orientation, measure the horizontal gap from the BIOS black body's right edge to the connector housing's left edge, and the signed vertical offset between their top edges. Mark which connector housing edge/end was used.
2. **Actual solder geometry:** CN503 tail pitch, spacing between the two tail rows and the exposed contact length/width. Use a counted multi-pin span between matching edges divided by the number of intervals, or a verified connector drawing. The 24.39 mm overall pin run is not enough to set pitch.
3. **Outline and supports:** mark usable board limits, metal steps, existing component keepouts and available independent supports on one dimensioned overhead sketch. Account for component heights beneath solid carrier areas. Set microSD insertion/removal and programming access; internal service access is the baseline.
4. **Chosen remote C5 site, if used:** local usable length/width/height, supports and the practical ribbon route. Select the removable connector from its drawing, then check the complete mounted stack. No need to buy an unspecified socket just to measure it.

Complete the electrical design, selected front-end/link, production footprints and ERC/DRC before generating an assembly order. The current KiCad foundation and GPIO CSV are not completed versions of the RAM or remote-C5 circuits.

## Evidence and next steps

Claude correctly withdrew the nonexistent-CF-rig assumption and its original PIO-output/DMARQ gate errors. Software logs and a controlled raw-register probe can precede a hardware responder; passive waveform capture does not require a working CF card. However, an IDE failure code does not alone prove which physical device drove the bus. Floating levels, stale state and probe cleanup must be considered. The existing ATA path sends IDENTIFY directly; a silent bridge would need the explicit activation contract discussed previously.

1. Complete the CN503-to-IC501 relative position and transfer the resolved height map to the outline; choose and check the C5 mounting stack, then qualify component clearances, contact geometry and physical pin orientation. Keep both confirmed thermal-contact footprints clear. Check the candidate continuity map with power disconnected; measure supply/signal levels in a properly qualified setup.
2. Review existing IDE-probe evidence and capture baseline register/data strobes before freezing timing architecture. Any software probe must own/restore the bus and timing; do not sweep values blindly under game/optical activity.
3. Develop the chosen front-end register file, ownership/reset/interlock model, MCU link and independent stock path. Demonstrate resource/timing closure and isolate the responder before console attachment.
4. Prove coexistence and hashed PIO reads, then DMA. Qualify writes/cache coherence/flush and power cuts before networking or BIOS programming.

Validation here covers references, arithmetic, document links, 28 logical ATA joins and 20 unique candidate IC501 pins. No electrical, console, RF, thermal, FPGA timing or fabrication validation is claimed. Native design files and the original GPIO allocation remain unchanged.
