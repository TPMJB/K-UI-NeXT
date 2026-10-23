# Rev A fabrication status

**The hardware design is implemented; fabrication is not released yet.** The carrier now has a complete native circuit and a fully routed six-layer, 59 × 115 mm layout (native DRC 0, unconnected 0, parity 0; the normal-current and 1.35 A converter fault copper screens pass, fuse-clearing qualification remains open). Both independent CN503 flex arms are routed, but the flex sockets J50/J51 must move before either order: as placed, arm B cannot reach J51. The parts list and candidate assembly placement derive from the selected physical components. Remaining fabrication conditions include the socket-move layout revision, fuse-clearing qualification, mechanically supported fit, CN503 A/B proof, assembler process/fixture review and matching-hash engineering approval. FPGA device timing and completed firmware remain bring-up limitations.

| Item | Actual state |
| --- | --- |
| Integrated native circuit | KiCad10 ERC0; physical package-pin/footprint-pad audit passes |
| FlexA and flexB | Each ERC0, DRC0, unconnected0, parity0; independent native mapping check passes |
| FPGA logic | Original source and regressions; MachXO2 technology mapping fits; vendor place/route timing still open |
| Carrier PCB | Six-layer 0.8 mm, 59 × 115 mm, fully routed: DRC 0, unconnected 0, parity 0; normal and 1.35 A converter fault copper screens pass; fuse-clearing qualification and the J50/J51 socket move remain open; review report in `design/CARRIER-LAYOUT-REVIEW.md` |
| C5 mount | Sixteen discrete Harwin S7221 contacts,0.90 mm supported working gap; clamp/installed height unqualified |
| Procurement | All 55 BASE carrier SMT types have catalog identities, including D21 C22458622 and F21 C206990 (RC0402FR-07220RL C112291, added 17 October, and TPS62172DSGR C59873, 23 October, come from a search index; confirm both on lcsc.com with their JLC SMT entries); manual C5 and optional-variant sourcing remain separate |
| Assembly/release | BASE candidate BOM has 269 SMT parts; native CPL from the routed board exists; manufacturer rotation/fixture preview and reviewed release still pending |

The first release will be a bring-up prototype. It retains GD-ROM device0 and provides a device1 block backend, native four-bit SD,8 MiB bridge PSRAM, local USB-free removable C5 recovery/isolation, power-loss detection, a DNP reserve bank and a DNP BIOS option. K-UI owns the filesystem. Audio/+12 V, GDEMU/ATAPI emulation and retail-game modem/BBA compatibility are outside this hardware release.

See [supplier confirmations](SUPPLIER-CHECKLIST.md) and [owner next steps](../docs/OWNER-NEXT-STEPS.md). The review archive `review-package-2026-10-23-REVIEW-ONLY.zip` contains current native artwork and checks for inspection/quotation; it is not approved for ordering.

## Order files

There are **three separate fabrication orders**, rather than integrated rigid-flex: rigid carrier, flexA and flexB. The passive flex needs no assembly BOM/CPL. Rigid assembly uses the BASE population with BIOS_OPTION and reserve components DNP. The C5 is manually installed using a separately qualified support/clamp, which is still unfinished.

The alternative **BASE_PSRAM_DNP** uses the same fabricated PCB and omits only U203. Its separate candidates are [rigid-psram-dnp-bom.csv](rigid-psram-dnp-bom.csv), [rigid-psram-dnp-cpl.csv](rigid-psram-dnp-cpl.csv) and [assembly-psram-dnp-draft-report.json](assembly-psram-dnp-draft-report.json): 268 SMT parts in 54 part types (all with catalog identities). Boot flash, CS1 pullup and bypass capacitors remain fitted. Its firmware must disable PSRAM/cache mapping and must not probe or assert CS1/GPIO47. Generate this candidate with `/usr/bin/python3 hardware/g1-sd-wifi/tools/generate_assembly_draft.py --variant BASE_PSRAM_DNP`; the default invocation generates the populated 8 MiB BASE candidate. Submit one matching population's BOM and CPL together.

Draft settings are in [rigid-order-settings.json](rigid-order-settings.json) and [flex-order-settings.json](flex-order-settings.json). Rigid: FR-4, 6 layers, 0.8 mm, ENIG,1 oz outer/0.5 oz inner copper, top-side assembly. Its0.20 mm QFN thermal drills use0.50 mm pads, an absolute0.15 mm annular ring; signal routing vias are0.20/0.50 mm and power vias are0.30/0.60 mm. Open thermal-via/paste-window behavior needs assembly review. Flex: two layers,0.12 mm nominal base,0.5 oz copper, ENIG,0.20 mm top PI tail stiffeners, bottom-contact0.5 mm pitch/10.5 mm tails. Supplier must verify the exposed-finger finished0.30 ±0.05 mm tail and required width tolerance. Shared coverlay windows avoid subminimum webs between CN503 solder fingers. Board rules: 0.127 mm (5 mil) minimum track and space (the 0.15 mm rules stalled routing; published six-layer capability supports this; preserve these settings in the actual quote), 0.2/0.5 mm signal vias, 0.3/0.6 mm power vias, 0.3 mm copper to any edge, vias tented both sides. The BIOS opening and CN503 notch are milled: their inside corners come out with the router radius (about 0.5 mm), which the BIOS clearance tolerates. Vias inside pads (J20's ground pad, the U21/U22 (TPS62172) thermal pads, the RP2350 thermal pad) are open; if the fabricator offers epoxy-filled, capped vias for six-layer boards at no charge, choose it, otherwise leave them tented.

Power is a separate fused 5 V/GND pair at J20. The flex carries no console 5 V at all (contacts A3/B3 have no flex copper), and host 3.3 V enters only through contact B1 and fuse F21. The chosen FPC contact-current budget cannot be silently used for the full C5/SD/controller load.

[parts-to-buy.csv](parts-to-buy.csv) is a sourcing list, with exact manufacturer numbers and explicit population variants. Catalog identities do not promise inventory or assembly availability. [rigid-bom.csv](rigid-bom.csv) and [rigid-cpl.csv](rigid-cpl.csv) are BASE candidates, with source hashes and their unapproved placement status in [assembly-draft-report.json](assembly-draft-report.json). CPL coordinates must be compared with native PCB pads and JLC's actual assembly preview, including spring contacts and connector pin1. The exact catalog records for Harwin S7221-45R contacts (C22445132) and Abracon ASE-50.000MHZ-LC-T oscillator (C596955) require factory assembly fixtures; the installed C5 clamp is a separate mechanical part. Both notices and URLs are preserved in the purchasing worksheet. Do not order an earlier MCU-only starter or the independent CF board for this design.

## Checks and export

From the repository root:

```sh
/usr/bin/python3 hardware/g1-sd-wifi/tools/generate_design.py --reuse-localized-footprints
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
