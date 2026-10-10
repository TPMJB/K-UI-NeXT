# Independent review request: G1 microSD and Wi-Fi bridge

**Historical proposal review request.** This request predates the selected FPGA circuit and subsequent owner measurements. It remains as the original review scope; the [10 October implementation status](../design/IMPLEMENTATION-STATUS-2026-10-10.md) describes the current hardware and outstanding work.

Review [the architecture](../README.md), [controller schematic](../controller/KUI-G1-Bridge-RevA.kicad_sch), [GPIO CSV](../controller/K-UI-G1-GPIO-RevA.csv), [CN503 mapping](CN503-reference.csv), [opening notes](../controller/START-HERE.txt) and [check limits](../controller/CHECKS.txt).

## Scope and fixed requirements

The target is the owner's VA1 motherboard 837-13778-02 with its original 3.3 V Samsung GD-ROM retained. RP2350B emulates ATA device 1 backed by raw sectors on a native four-bit microSD interface. A socketed XIAO ESP32-C5 provides Wi-Fi over SPI. K-UI owns exFAT/ext4; there is no independent C5 filesystem writer or FAT32 setup requirement. Optional dual BIOS must preserve stock boot independently of MCU firmware.

This is a proposal and first controller sheet. G1 isolation, SD/C5 interface circuits, console/reserve power, dual BIOS, ATA firmware and PCB layout are incomplete. Do not assume the old CF board's Gerbers implement this design. Do not infer that structural file checks prove electrical operation.

## Questions to resolve

1. **GPIO and implementation feasibility.** Check 28 ATA signals plus isolation controls, SD, C5 and power fail against the RP2350B pin mux and PIO pin-window rules. Propose a concrete PIO/DMA/state-machine allocation and supported timing, not just a pin count. Check the planned PIO2 SD and SPI1 high-pin assignments. GPIO47 is spare; LED and card detect are optional.
2. **Shared bus behavior.** Define device-select tracking, IDENTIFY, status/error/reset behavior, data turnaround, IORDY, IRQ and DMA ownership with the original drive retained. Identify every condition that needs hardware gating. Specify safe boot/reset/brownout defaults and isolation from an unpowered host.
3. **Storage throughput and stalls.** Bound PIO/DMA timing and buffering, including SD stalls and packet service. Do not assume advertised MCU clock, raw SD speed or another IDE emulator establishes Dreamcast compatibility. Determine whether an MCU-only implementation is sufficient or extra logic is justified.
4. **Power architecture.** Evaluate 5 V input plus local 3.3 V, or separate MCU/SD 3.3 V and C5 5 V feeds. Keep supply and signal compatibility distinct. Review USB isolation, radio peaks, power-failure threshold and load switching. Hold-up capacity must follow measured current and worst-case card behavior.
5. **Data integrity.** Review read-only collection access, explicit write sessions, verified temporary-file commits, ordering, ATA write-cache/flush semantics and actual ext4 journal/recovery configuration. Explain what reserve power and journaling can and cannot preserve. Propose power-cut tests that check hashes of previously completed games.
6. **Attachment and mechanics.** Validate both CN503 rows and actual solder-tail accessibility. A top-side carrier below the upper shield is preferred; a thin underside tap/flex is a fallback. Outline and socket heights await measurements. The numbered DragonCity picture depicts the GD-ROM PCB underside, uses IDE numbering and ties pin 1 to 3.3 V; it is not a mainboard solder map.
7. **Dual BIOS and networking scope.** Review independent ROM selection and chip-enable isolation without consuming assumed spare MCU pins. Treat networking inside existing games as a separate compatibility task; a K-UI driver alone is insufficient.

## Requested output

Return findings by severity, with exact file/net/pin references and primary-source links for technical claims. Separate confirmed errors from plausible alternatives and missing evidence. Recommend the smallest next schematic/firmware experiment that resolves each blocker. Identify what can proceed before physical measurements and what must wait for them. Review only; do not silently produce fabrication files or change unrelated software.
