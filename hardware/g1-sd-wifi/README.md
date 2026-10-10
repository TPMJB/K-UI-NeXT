# K-UI G1 microSD and Wi-Fi bridge — Rev A

The design now has a complete native KiCad circuit, two routed CN503 flex arms, selected components and original FPGA logic. All 53 BASE SMT part types have exact catalog matches, and candidate BOM/CPL files exist. **Prototype fabrication is not released yet:** carrier routing and electrical layout review, mechanical support/fit and assembler placement/fixture review remain open. Device timing and firmware qualification are also outstanding. This directory supersedes the historical MCU-only starter.

The Dreamcast VA1 keeps its original 3.3 V GD-ROM as device0. The bridge supplies ATA device1, raw microSD sectors and Wi-Fi transport. K-UI is the sole filesystem owner; the C5 never independently mounts the card. exFAT is the baseline user choice; ext4 remains a software option. No FAT32 setup is prescribed.

| Implemented hardware | Selected component or circuit |
| --- | --- |
| ATA front end | U10 LCMXO2-2000HC-4TG100C; 50 MHz clock, JTAG, TX/RX FIFOs and hardware isolation |
| Controller | U201 RP2350B with boot NOR, native four-bit SD, USB/SWD recovery |
| Bridge cache | U203 APS6404L-3SQR-ZR, 8 MiB PSRAM on QMI CS1/GPIO47; does not add SH-4 system RAM |
| microSD | Molex1040310811 socket, card detect, pullups and rail-qualified bidirectional isolation |
| Wi-Fi | Manual USB-free XIAO ESP32-C5, sixteen Harwin spring contacts, switched5 V, isolated SPI/UART and EN/BOOT recovery |
| Power | Dedicated fused/reverse-protected5 V pair, separate logic/storage bucks, power warning, supervisors and DNP reserve bank |
| G1 attachment | Independent passive20-conductor A/B arms and Hirose FH12 bottom-contact sockets; audio/+12 V excluded |
| Optional BIOS | DNP x8 NOR, extra ROM taps, physical independent stock recovery and hardware-qualified write arm; active factory bank0 |

## Open the actual design

Open [the controller project](controller/KUI-G1-Bridge-RevA.kicad_pro) with KiCad10. The [root schematic](controller/KUI-G1-Bridge-RevA.kicad_sch) links eleven component sheets. The [carrier PCB](controller/KUI-G1-Bridge-RevA.kicad_pcb) is a draft with a BIOS clearance opening and an open CN503 notch. Both flex projects are under [flex/](flex/); A and B have different contact maps and are not interchangeable.

[Physical component contracts](design/blocks/) drive the deterministic schematic generator. Local symbol and footprint libraries are included. The [physical pad audit](design/physical-pad-audit.json) checks every electrical terminal against KiCad's loaded footprint, including exposed pads and connector anchors. [Controller notes](design/controller-implementation.md), [power/isolation notes](design/power-and-isolation.md) and [BIOS population notes](design/BIOS-variant.md) describe actual pins and circuits. [Current GPIO allocation](controller/K-UI-G1-GPIO-RevA.csv) is controller-to-FPGA/SD/C5, rather than the original direct-ATA candidate.

The reusable [integration checker](tools/check_integration.py) verifies the source contracts against the integrated native components and exported XML netlist, checks 100 FPGA package functions and 56 pin constraints, and evaluates 8,240 hardware safety combinations using the exported connections. From the repository root, run `python hardware/g1-sd-wifi/tools/check_integration.py --check`. After reviewed source changes and a fresh native netlist export, omit `--check` to refresh [integration-audit.json](design/integration-audit.json).

The [FPGA directory](fpga/) contains synthesizable logic, exact physical-pin LPF, simulation scoreboards and the MCU-link contract. Simulation and MachXO2 resource mapping pass. A Lattice device-fit/timing result and completed RP2350 ATA/media firmware are still required; there is no validated programming image or game-compatibility claim. Initial firmware must advertise PIO0 only. DMA ownership is provisioned and tested in simulation, but performance and physical timing are unqualified.

## Measured mechanical limits

The owner may reassemble the console; no further teardown is requested for this draft. Motherboard-top clearance is6 mm in the BIOS/CN503/lower-shield region,8 mm elsewhere, and zero over the two thermal-contact chips. Nominal carrier underside is2.5 mm above the motherboard;0.8 mm PCB puts its top at3.3 mm. A2 mm FPC connector reaches5.3 mm before tolerances. The C5 assembly is reserved for the8 mm region and needs a supported0.90 mm contact gap and removable clamp.

BIOS body26.68 ×12.65 mm, outer lead span16.66 mm, height approximately2 mm. Its long axis is west-east; CN503's long axis is north-south. CN503 is38.67 mm long,6.56 mm maximum housing width; accepted solder-contact pitch is1.00 mm with approximately0.44 mm metal width. The25-contact measured run is24.39 mm. BIOS-to-connector housing distance is approximately12 mm; the west BIOS strip and east connector strip are approximately7 mm each. Those strips contain existing parts and solder joints. The housing-derived5.03 mm row spacing does not constrain the independent flex arms.

The59 ×105 mm carrier envelope and cutout/notch north-south registration are provisional. The angled photographs do not establish a production outline or support location. Existing tall capacitors, case features, thermal-pad pressure, C5 height/retention, SD access and shield insulation require mechanical review. No shield cutting is assumed. [Actual-size carrier fit sheets](mechanical/README.md) use native PCB edges and component pads, with Letter/A4 paper choices and independent horizontal/vertical calibration.

## Manufacture and review

[Manufacturing status](manufacturing/README.md) identifies the current order blockers, native checks, exact draft settings and procurement work. Final release uses three separate archives: rigid carrier, flexA and flexB. The exporter refuses to publish an order package while required checks, assembly data and reviewed source hashes are incomplete.

JLC's exact catalog pages require assembly support fixtures for both the **Harwin S7221-45R contacts (C22445132)** and **Abracon ASE-50.000MHZ-LC-T oscillator (C596955)**. Obtain assembler agreement on those fixtures before releasing an assembly order. The C5 retention clamp is a separate mechanical installation requirement. [The purchasing worksheet](manufacturing/parts-to-buy.csv) records the exact SMT identities and fixture notices.

The historical dispositions remain in [docs/](docs/); Claude's original reviews are pinned to [commit b207dd6](https://github.com/TPMJB/K-UI-NeXT/tree/b207dd659565826ffd757ccf7abe1998f63c9202/docs). The [implementation status](design/IMPLEMENTATION-STATUS-2026-10-10.md) maps each accepted addition to its circuit or source artifact and remaining condition. Software caching/OTA/BIOS programming, runtime bank switching and measured SD power-cut retention are subsequent implementation/qualification work; a DNP reserve capacitor footprint cannot establish safe write completion. The original controller starter is preserved under [controller/reference/](controller/reference/).
