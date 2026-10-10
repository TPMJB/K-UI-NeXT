# G1 bridge JLCPCB release

**Not ready to order — 10 October 2026.** This directory contains a fabrication preflight, not a finished bridge, BOM or Gerber package. The design branch still contains the RP2350B controller starter. It has no rigid PCB, flex CAD, FPGA circuit/HDL or production assembly data. Nothing here should be uploaded as an order.

The owner's requested measurements and photos are sufficient for the first mechanical draft, and the Dreamcast can be reassembled. The remaining work is circuit and layout engineering. The paper contact guide is a fit aid; the older CF board is a different design.

## Requested hardware and Claude's additions

The source review is Claude's three G1 documents at [b207dd659565826ffd757ccf7abe1998f63c9202](https://github.com/TPMJB/K-UI-NeXT/tree/b207dd659565826ffd757ccf7abe1998f63c9202/docs), including the latest VA1-only, expected 3.3 V BIOS correction. The existing [review disposition](../docs/review-disposition-2026-10-09.md) and [controller/RAM/BIOS disposition](../docs/controller-ram-bios-disposition-2026-10-09.md) record the corrections. The items below remain in scope; recording them does not implement their circuits.

| Item | Native implementation now | Work before first fabrication |
| --- | --- | --- |
| RP2350B, native four-bit microSD | MCU/boot-flash/clock starter only | Complete SD circuit and revised allocation |
| Small FPGA/CPLD ATA front end | None; MachXO2 candidates unselected | Exact part, usable I/O/FIFO budget, clocks, configuration/JTAG, MCU link, pin map and synthesized timing/ownership logic |
| G1 data/control buffers | None | Qualified off-state isolation, default-disable, turnaround and reset/brownout behavior |
| Retained GD-ROM device 0; bridge device 1 | Proposal only | Device selection, optical IRQ/DMA coexistence, activation/relock, register semantics and shared-line ownership |
| FIFO/SRAM path and DMA | Proposal only | Handshake/backpressure; final DMA word and hold-time ownership; conservative mode/timing contract |
| 8 MiB PSRAM provision | Absent | Exact part/footprint, shared QSPI plus legal CS1, decoupling, populated and unpopulated variants; FIFO/SRAM remains the immediate path |
| Console and USB power | Bench foundation only | Regulator/source isolation, current/thermal budget, no USB backfeed |
| Local removable XIAO ESP32-C5, USB removed | Absent | Low-height removable contacts, switched supply, SPI/IRQ/READY off-state isolation, antenna and service access |
| USB-free C5 programming | Absent | UART TX/RX/GND and BOOT/EN recovery contacts with safe strap/power behavior; installed SPI updater remains software work |
| Power-loss handling | Absent | Early detection, radio cutoff, isolated controller/SD reserve domain and sizing/qualification plan |
| Optional custom/dual BIOS | Absent; still an open hardware variant | Additional ROM address/control taps, exact NOR/adapter, immutable original-ROM path, independent physical selection, exclusive enables and protected programming |
| CN503 flex and supported carrier | Contact-guide artwork only | Routed CAD, joining geometry, FPC contact face/thickness, ground returns, coverlay/stiffeners, independently supported carrier |
| Debug and bring-up access | Bench headers in starter | Accessible 28 G1 signals and buffer enables; production bench-header population explicitly excluded |
| Write integrity and filesystem ownership | Requirements only | K-UI remains sole exFAT/ext4 owner; define write-through/flush and recovery behavior; no durability claim before power-cut testing |

The optional BIOS circuit is not provided by CN503 alone. It must be explicitly accounted for in the chosen fabrication variant; it has not been silently removed. No BIOS bypass, validated dual-bank recovery, extra Dreamcast system RAM or peak-transfer claim exists. The PSRAM is bridge cache memory.

CN503's remaining audio/+12 V contacts are deliberately unused in the baseline. Hardware CDDA and replacing retail-game modem/BBA interfaces are later projects, not implicit features of this order.

## What constitutes the first order

The first hardware release will be an **unvalidated bring-up prototype**, not a working product. Before manufacturing it, select the actual components, implement complete fail-safe power/reset/bus ownership and service circuits, finish rigid and flex layout, resolve footprints, review the BOM/rotations, run real ERC/DRC and inspect the plotted fabrication layers. A programming/recovery and initial bench bring-up procedure must accompany it.

Higher DMA modes, PSRAM caching, C5 OTA, final SD power-cut qualification and optional BIOS programming can be staged after prototype manufacture only when the required hardware is present and the release explicitly states what is unimplemented. This is a staging option, not authorization to omit the owner's requested provisions. A working-product claim needs the subsequent console tests.

A finished order package must contain:

| Deliverable | Use |
| --- | --- |
| Rigid-board Gerber/drill ZIP | FR-4 fabrication order |
| Rigid-board production BOM and CPL | JLC SMT assembly, exact population variant |
| Separate flex Gerber/drill/coverlay/stiffener ZIP | Passive custom FPC order |
| Exact order settings for each | Layer count, material, thickness, finish, dimensions and assembly selection matching the CAD |
| Separate-purchase parts list | Removable C5, card, supports, programming/service items and any parts not supplied by assembly |
| Programming and bring-up instructions | Exact firmware/FPGA images, bench power sequence, recovery and first tests |
| Review/check reports and input hashes | Tie fabrication exports to the reviewed sources |

There is no populated purchase list yet. An empty BOM or guessed connector would make this package misleading.

## JLC-specific design constraints

Use **separate rigid and flex orders**. JLC's current capabilities do not support integrated rigid-flex. The passive flex requires no assembly BOM/CPL. Rigid SMT assembly requires a production BOM and placement file from the final design.

The accepted 1.00 mm working CN503 pitch and approximately 0.44 mm lead width do not define a finished copper pad. At a 0.44 mm pad width with 0.10 mm coverlay expansion per side, the remaining web is `1.00 - (0.44 + 0.20) = 0.36 mm`, below JLC's 0.50 mm minimum. Choose an explicit shared solder window or qualify narrower landing pads; do not assume coverlay between every joint.

Choose the FPC connector before its finished mating-tail thickness and stiffener. State stiffener material, side, thickness and filled outline in dedicated fabrication layers. Check copper-to-outline/slot clearance, gold-finger setback and the installed bend radius. JLC's plotted production-file review remains a separate step after upload.

Mechanical engineering uses the owner's 6 mm BIOS/CN503 region, 8 mm general region and zero-clearance thermal-contact footprints. The roughly 7 mm strips contain existing parts/solder/case features; neither is a blank rectangular mounting area. The housing-derived 5.03 mm trial row separation and solder-tail landing geometry still require prototype qualification. No new console teardown is being requested.

## Candidate sourcing only

These catalog references are research, **not an approved BOM or shopping list**. The FPGA is not selected; package capacity, circuit compatibility, timing and current stock still need qualification.

| Candidate | JLC catalog reference |
| --- | --- |
| LCMXO2-1200HC-4TG100C, TQFP100 | [C453479](https://jlcpcb.com/partdetail/Lattice-LCMXO2_1200HC4TG100C/C453479) |
| LCMXO2-2000HC-4TG100C, TQFP100 | [C1521632](https://jlcpcb.com/partdetail/LCMXO2-2000HC-4TG100C/C1521632) |
| APS6404L-3SQR-ZR, USON8 3 × 2 mm | [C3040877](https://jlcpcb.com/partdetail/APMemory-APS6404L_3SQRZR/C3040877) |

## Preflight

`release-manifest.json` lists the intended native inputs and engineering review topics. Its initial status is blocked. The script's audit mode needs Python only and must report that state; it does not fabricate blank order files. Export mode additionally needs KiCad 10 and completed, reviewed inputs.

Run from a repository checkout:

```sh
python3 hardware/g1-sd-wifi/manufacturing/prepare_jlcpcb_release.py --audit --json
python3 -m unittest discover -s hardware/g1-sd-wifi/manufacturing/tests -p 'test_*.py' -v
```

Audit exits 2 while blocked and emits the exact missing inputs. After design completion, the manifest status must be `approved_for_prototype_fabrication`, each order-settings file must have `released: true`, and `review-evidence.json` must record accepted reviews of all eight design-closure topics for the SHA-256 inventory produced by the audit. The reviewer, timestamp and substantive notes are required for each topic. Changing a native project, footprint, manufacturing input, manifest, script or review invalidates the corresponding snapshot; reacquire actual reviews rather than regenerating flags automatically.

The script standardizes BOM CSV headers to exactly `Comment,Designator,Footprint,LCSC Part #` and CPL headers to exactly `Designator,Mid X,Mid Y,Layer,Rotation`. CPL coordinates are millimeters, with top/bottom side and reviewed rotation. The purchase list must retain exact manufacturer part numbers as well as the assembly catalog codes. A matching ref set and valid numbers do not prove PCB coordinates or part rotation; `assembly_bom_cpl` review must compare the final placement and JLC preview.

With every input and review complete, export to a **new** directory:

```sh
python3 hardware/g1-sd-wifi/manufacturing/prepare_jlcpcb_release.py --export --output /new/path/jlc-revA
```

This runs actual KiCad 10 ERC/DRC (including schematic parity) and exports separate fabrication archives. Failed checks must leave no order package. The [branch tooling workflow](../../../.github/workflows/g1-manufacturing.yml) tests failure handling with synthetic fixtures and saves the current blocked preflight; it does not run KiCad or publish order files.

Checks and review records are engineering controls, not proof of game compatibility, durability or installed fit. A green tooling test does not mean the bridge is ready. The manifest must not be changed to ready merely to suppress missing-file errors.

## Primary manufacturing references

Checked 10 October 2026:

- [JLC KiCad BOM/CPL preparation](https://jlcpcb.com/help/article/how-to-generate-the-bom-and-centroid-file-from-kicad)
- [JLC Gerber preparation](https://jlcpcb.com/help/article/gerber-files-preparation)
- [JLC flex capabilities](https://jlcpcb.com/capabilities/flex-pcb-capabilities)
- [JLC FPC stiffeners and layer naming](https://jlcpcb.com/help/article/fpc-stiffener-emi-guide)
- [KiCad 10 command-line reference](https://docs.kicad.org/10.0/en/cli/cli.html)
