# Parts purchasing worksheet

[parts-to-buy.csv](../manufacturing/parts-to-buy.csv) lists the exact component order codes selected by the physical design contracts. Quantities describe **one complete assembly**. Select a single quantity column and multiply by the number of boards. Supplier reel sizes, MOQ and assembly attrition must be added when arranging the order.

| Quantity column | Population |
| --- | --- |
| `qty_base` | Factory base: 8 MiB PSRAM fitted; BIOS and reserve parts DNP. |
| `qty_bios_variant` | Base plus optional BIOS components and remote selector, with R410 removed. |
| `qty_reserve_variant` | Base plus R202, D20 and C202–C205. |
| `qty_bios_and_reserve_variant` | Both optional populations, with R410 removed. |
| `qty_base_psram_dnp_variant` | BASE_PSRAM_DNP: base with U203 PSRAM DNP. |
| `qty_bios_psram_dnp_variant` | BIOS population with U203 DNP and R410 removed. |
| `qty_reserve_psram_dnp_variant` | Reserve population with U203 DNP. |
| `qty_bios_and_reserve_psram_dnp_variant` | BIOS and reserve populations with U203 DNP and R410 removed. |

For the factory base, all **53 carrier SMT purchase rows** have exact JLC catalog identities. The C5 module is the separate manual purchase, making 54 base purchase rows and 278 fitted items in total. Fifteen supplier-code rows remain pending across the manual C5 purchase and optional BIOS/reserve parts.

These are full assembly totals, not quantities to add together. R409 remains fitted in every variant. Copper service pads, test points, JTAG pogo pads, the normally open BIOS write solder bridge and ERC power flags do not create component purchases. The source hash and excluded references appear in [parts-to-buy-audit.json](parts-to-buy-audit.json).

The **BASE_PSRAM_DNP** assembly omits only U203. It has 277 fitted items, including the separate manual C5 purchase, and 52 SMT purchase rows. R2007, the R2008 4.7 kΩ CS1 pullup and all existing bypass capacitors remain fitted. This keeps the GPIO47/CS1 line biased high without changing the boot-flash wiring or copper. `refs_psram_only` identifies U203; the audit records the exact purchased DNP references for all eight combinations. BASE remains PSRAM populated. Any PSRAM_DNP order needs the matching reviewed assembler population file, with U203 omitted, before release.

The [BASE_PSRAM_DNP candidate BOM](../manufacturing/rigid-psram-dnp-bom.csv), [CPL](../manufacturing/rigid-psram-dnp-cpl.csv) and [assembly report](../manufacturing/assembly-psram-dnp-draft-report.json) contain 276 SMT placements and 52 catalog rows. Compared with the BASE CPL, only U203 is absent; the other 276 placements match exactly. Both candidates remain subject to carrier route closure, placement preview and assembly release review. The purchasing generator also provides the other PSRAM_DNP combinations; their reviewed assembler files have not been generated.

PSRAM_DNP firmware must use internal SRAM/FIFOs, keep GPIO47/CS1 inactive high, disable CS autodetection and avoid all CS1 transactions or PSRAM memory mappings. Detecting RAM by reading floating data is insufficient. No qualified populated or PSRAM_DNP storage firmware is supplied yet.

A populated `lcsc_code` means the official LCSC/JLCPCB catalog matched the exact MPN, manufacturer and package on 2026-10-10. The separate `jlc_assembly_status` and `jlc_assembly_url` fields record whether that exact part is listed for SMT assembly. Neither field reserves stock or confirms acceptance of this board and assembly order. Empty codes and `sourcing_pending` require supplier selection. [procurement-evidence.json](procurement-evidence.json) holds the reviewed catalog URLs, manufacturer references and component-specific limits. This worksheet does not replace the assembler BOM/CPL or approve manufacturing release.

The C5 module is a manual purchase and modified installation. The sixteen Harwin contacts are real SMT parts; the clamp, insulation and mechanical fixture remain separate. JLC's exact catalog pages explicitly require an assembly support fixture for both **Harwin S7221-45R (C22445132)** and **Abracon ASE-50.000MHZ-LC-T (C596955)**. Agree that fixture process with the assembler before releasing an order; the C5 retention clamp does not by itself satisfy this assembly requirement. The optional remote BIOS switch is an offboard manual part. CN503 flex fabrication, optional BIOS FPC/selector harness, power leads and microSD media also remain separate items.

The selected converter inductors match the native RP235x reference part and TI's TPS6216x recommended 2.2 µH part. Converter outputs use two nominal 22 µF capacitors; their effective capacitance at 3.3 V still needs qualification. The optional reserve bank's 400 µF nominal value does not establish card flush time. Its 22 Ω charge resistor sees an ideal initial 1.14 W and approximately 5 mJ per charge at 5 V, so pulse-overload verification is required before populating that option.

The populated BIOS option needs isolated stock CE, R410 removal, separate installation/timing qualification and externally verified bank-0 preprogramming. Leave JP40 open. The factory hardware/HDL supports bank 0; runtime bank selection and an onboard flasher are not provided.

Regenerate after changing any source contract or procurement evidence:

```sh
python hardware/g1-sd-wifi/tools/generate_parts_list.py
python hardware/g1-sd-wifi/tools/generate_parts_list.py --check
```

The generator leaves unknown supplier IDs blank and stops on an unclassified DNP component, missing exact-MPN metadata or a supplier ID without matching official evidence.
