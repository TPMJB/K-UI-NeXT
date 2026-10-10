# Rev A fabrication status

**The hardware design is implemented; fabrication is not released yet.** The carrier now has a complete native circuit and a four-layer layout draft. Both independent CN503 flex arms are routed. The parts list and candidate assembly placement derive from the selected physical components. Remaining fabrication blockers are carrier routing/layout review, mechanically supported fit and assembler rotation/fixture review. FPGA device timing and completed firmware remain bring-up limitations.

| Item | Actual state |
| --- | --- |
| Integrated native circuit | KiCad10 ERC0; physical package-pin/footprint-pad audit passes |
| FlexA and flexB | Each ERC0, DRC0, unconnected0, parity0; independent native mapping check passes |
| FPGA logic | Original source and regressions; MachXO2 technology mapping fits; vendor place/route timing still open |
| Carrier PCB | Four-layer0.8 mm native placement/routing draft; actual checks and placement reports accompany CAD |
| C5 mount | Sixteen discrete Harwin S7221 contacts,0.90 mm supported working gap; clamp/installed height unqualified |
| Procurement | All53 BASE carrier SMT part types have exact catalog identities; manual C5 and optional-variant sourcing remain separate |
| Assembly/release | BASE candidate BOM has277 SMT parts; actual native CPL exists; manufacturer rotation/fixture preview and reviewed release still pending |

The first release will be a bring-up prototype. It retains GD-ROM device0 and provides a device1 block backend, native four-bit SD,8 MiB bridge PSRAM, local USB-free removable C5 recovery/isolation, power-loss detection, a DNP reserve bank and a DNP BIOS option. K-UI owns the filesystem. Audio/+12 V, GDEMU/ATAPI emulation and retail-game modem/BBA compatibility are outside this hardware release.

## Order files

There are **three separate fabrication orders**, rather than integrated rigid-flex: rigid carrier, flexA and flexB. The passive flex needs no assembly BOM/CPL. Rigid assembly uses the BASE population with BIOS_OPTION and reserve components DNP. The C5 is manually installed in the retained spring-contact fixture.

The alternative **BASE_PSRAM_DNP** uses the same fabricated PCB and omits only U203. Its separate candidates are [rigid-psram-dnp-bom.csv](rigid-psram-dnp-bom.csv), [rigid-psram-dnp-cpl.csv](rigid-psram-dnp-cpl.csv) and [assembly-psram-dnp-draft-report.json](assembly-psram-dnp-draft-report.json): 276 SMT parts in 52 exact catalog types. Boot flash, CS1 pullup and bypass capacitors remain fitted. Its firmware must disable PSRAM/cache mapping and must not probe or assert CS1/GPIO47. Generate this candidate with `/usr/bin/python3 hardware/g1-sd-wifi/tools/generate_assembly_draft.py --variant BASE_PSRAM_DNP`; the default invocation generates the populated 8 MiB BASE candidate. Submit one matching population's BOM and CPL together.

Draft settings are in [rigid-order-settings.json](rigid-order-settings.json) and [flex-order-settings.json](flex-order-settings.json). Rigid: FR-4,4 layers,0.8 mm, ENIG,1 oz outer/0.5 oz inner copper, top-side assembly. Its0.20 mm QFN thermal drills use0.50 mm pads, an absolute0.15 mm annular ring; signal routing vias are0.20/0.50 mm and power vias are0.30/0.60 mm. Open thermal-via/paste-window behavior needs assembly review. Flex: two layers,0.12 mm nominal base,0.5 oz copper, ENIG,0.20 mm top PI tail stiffeners, bottom-contact0.5 mm pitch/10.5 mm tails. Supplier must verify the exposed-finger finished0.30 ±0.05 mm tail and required width tolerance. Shared coverlay windows avoid subminimum webs between CN503 solder fingers.

Power is a separate fused5 V/GND pair at J20. The flex's5 V contacts do not supply the assembled bridge. The chosen FPC contact-current budget cannot be silently used for the full C5/SD/controller load.

[parts-to-buy.csv](parts-to-buy.csv) is a sourcing list, with exact manufacturer numbers and explicit population variants. Catalog identities do not promise inventory or assembly availability. [rigid-bom.csv](rigid-bom.csv) and [rigid-cpl.csv](rigid-cpl.csv) are BASE candidates, with source hashes and their unapproved placement status in [assembly-draft-report.json](assembly-draft-report.json). CPL coordinates must be compared with native PCB pads and JLC's actual assembly preview, including spring contacts and connector pin1. The exact catalog records for Harwin S7221-45R contacts (C22445132) and Abracon ASE-50.000MHZ-LC-T oscillator (C596955) require factory assembly fixtures; the installed C5 clamp is a separate mechanical part. Both notices and URLs are preserved in the purchasing worksheet. Do not order an earlier MCU-only starter or the independent CF board for this design.

## Checks and export

From the repository root:

```sh
/usr/bin/python3 hardware/g1-sd-wifi/tools/generate_design.py
hardware/g1-sd-wifi/fpga/run_checks.sh
/usr/bin/python3 hardware/g1-sd-wifi/flex/tools/check_flex.py
python3 -m unittest discover -s hardware/g1-sd-wifi/manufacturing/tests -p 'test_*.py' -v
python3 hardware/g1-sd-wifi/manufacturing/prepare_jlcpcb_release.py --audit --json
```

The audit remains blocked until the actual design review is accepted. `release-manifest.json` records exact native inputs. `review-evidence.json` must describe substantive reviews for the SHA-256 inventory. The exporter additionally executes actual KiCad ERC/DRC and schematic parity; a failed check produces no order package. Manufacturing-tool tests use synthetic fixtures and do not qualify this hardware.

After completion, set only reviewed matching settings to released:true and the manifest to approved_for_prototype_fabrication. Export into a new directory:

```sh
python3 hardware/g1-sd-wifi/manufacturing/prepare_jlcpcb_release.py --export --output /new/path/jlc-revA
```

A complete release contains three Gerber/drill archives, native coverlay/stiffener files, reviewed BASE BOM/CPL, separate manual-purchase list, programming/bring-up instructions, source hashes and the actual checks. Bring-up instructions must state which firmware/images are available and which tests remain; no dummy bitstream or firmware is supplied.

## Primary fabrication references

- [JLC rigid capabilities](https://jlcpcb.com/capabilities/pcb-capabilities/)
- [JLC flex capabilities](https://jlcpcb.com/capabilities/flex-pcb-capabilities)
- [FPC stiffener layer naming](https://jlcpcb.com/help/article/fpc-stiffener-emi-guide)
- [KiCad BOM/CPL preparation](https://jlcpcb.com/help/article/how-to-generate-the-bom-and-centroid-file-from-kicad)
- [KiCad10 command-line reference](https://docs.kicad.org/10.0/en/cli/cli.html)
