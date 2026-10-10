# Rev A implementation status - 10 October 2026

The design branch now contains the actual KiCad circuit, physical footprints, carrier layout, two routed flex boards and original FPGA logic. The circuit and RTL implement the accepted hardware provisions and record their remaining software, timing and mechanical conditions. This is an engineering implementation with recorded open findings; it is not a released fabrication package or a tested Dreamcast peripheral.

| Addition or decision | Implemented artifact | Remaining condition |
| --- | --- | --- |
| Retain GD-ROM device 0 and add ATA device 1 | MachXO2 taskfile/selection logic and separate hardware-qualified output enables | Native carrier route closure, physical bus timing and RP media firmware |
| Explicit bridge activation | Default-off FPGA latch, two fresh keyed vendor frames on device 1, host/MCU relock and asynchronous safety-loss invalidation | Host and MCU must implement the documented protocol; physical timing remains open |
| Shared device diagnostic | ATA 0x90 captured with either DEV selection, MCU result mailbox and no invented pass/IRQ | Actual firmware diagnostic and DASP/PDIAG discovery behavior remain unimplemented or unqualified |
| Correct shared taskfile reception and final-write completion | Independent regression catches both original defects; final received write word asserts BSY until storage completion | MCU must actually implement media commit/error handling |
| Replace MCU-only timing path | LCMXO2-2000HC TQFP100, exact LPF, 50 MHz clock, 512-word TX/RX FIFOs and 8-bit controller link | Vendor device place-and-route/timing and real bitstream |
| RP2350 controller | RP2350B, exact QFN, reference buck, boot NOR, crystal, USB and SWD service pads | Routed power/clock review and firmware |
| Additional RAM | APS6404L, 8 MiB QMI PSRAM, populated BASE and BASE_PSRAM_DNP purchasing/assembly candidates | Bridge cache only; does not expand SH-4 system RAM; DNP firmware must leave CS1 inactive |
| Raw G1 debugging | All 28 raw G1 lines, eight direction/enable/request controls and three local grounds have native bottom-copper probe pads and short tap routes; each added raw/control stub is at most 3 mm | Probe loading, access with the carrier removed and installed underside insulation remain unqualified |
| Native microSD | Four-bit SD, detect/pullups, rail-qualified reversible DAT/CMD isolation and separate storage supply | Driver and card qualification; first PIO0 operation preloads a whole sector |
| Local USB-free C5 | Seeed underside coordinates, 16 real Harwin spring contacts, switched power and isolated SPI/UART | Finished retention/support fixture and USB-free installed height |
| C5 RF access | Local module and intact shield remain the mechanical baseline | External antenna route outside the metal shield and installed RF behavior remain unqualified |
| C5 recovery without module USB | EN/BOOT sinks, controller UART and separate six-pad powered-module UART service access | RP/C5 update software; external UART recovery remains documented |
| Power sequencing/backfeed | Separate logic/storage bucks, supervisors, supply-aware bus gates, C5 I/O powered from module output | Collapse-rate/off-state bench tests |
| Separate power feed | Fused/reverse-protected 5 V/GND harness pads; CN503 flex 5 V does not feed the carrier | Installed harness and supply measurements |
| Power-loss provision | Upstream loss monitor, radio cutoff, DNP reserve mux/capacitors | Reserve energy and arbitrary-card durability are unqualified; no write-survival promise |
| Optional BIOS | DNP x8 NOR, additional ROM-tap connector, hardware write qualification and physical independent stock CE recovery | Optional tap/switch harness, preprogrammed bank 0 and installation qualification; R410 must be removed in BIOS variant |
| BIOS bank behavior | Factory bank 0; reset/power-warning do not silently switch a running CPU's mapped ROM | Runtime bank switching intentionally unavailable without whole-console reset control |
| Modchip-style CN503 attachment | Two independent 20-conductor passive flex arms, retained connector housing, 0.5 mm Hirose sockets | Physical continuity/orientation and assembled bend/landing fit |
| Measured heat-shield space | Provisional BIOS opening/CN503 notch and 6/8 mm height assumptions in native carrier | Full existing-part obstacle map, registration, support and installed clearance |
| Parts and assembly | BASE: 53 exact JLC SMT catalog types, 277 fitted SMT parts, actual native candidate CPL; separate manual C5 | Stock allocation, factory fixture agreement and manufacturer rotation preview |
| Factory assembly conditions | Harwin contacts and Abracon clock oscillator explicitly identified as requiring assembly fixtures | Assembler confirmation; these are separate from the installed C5 clamp |
| Filesystem ownership | K-UI sole filesystem owner; C5 transport does not mount media; exFAT baseline | Storage/filesystem software and power-cut testing |

Native schematic ERC and both flex ERC/DRC/parity checks pass. FPGA checks pass 60 bridge checks, 703 independent activation/diagnostic checks, 20 independent media checks and 521 FIFO scoreboarding cycles; four compiled behavioral mutations are rejected. Technology mapping uses 966 LUT4, 82 CCU2D, 409 flip-flops and two DP8KC blocks. These are measured software/CAD results, not hardware timing or game-compatibility evidence.

Open the native controller project and the two flex projects in KiCad 10. [Independent protocol review](INDEPENDENT-REVIEW.md), [carrier layout review](CARRIER-LAYOUT-REVIEW.md), [purchasing worksheet](PARTS-TO-BUY.md), [actual-size fit sheets](../mechanical/README.md) and [bring-up procedures](../manufacturing/BRINGUP.md) accompany the implementation. The manufacturing exporter checks recorded source hashes and native KiCad checks; unapproved settings do not produce order archives.
