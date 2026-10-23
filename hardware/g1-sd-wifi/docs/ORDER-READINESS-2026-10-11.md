# G1 bridge order readiness — 11 October 2026

**23 October 2026: not orderable yet, and not only for measurements.** The owner measurements in [OWNER-NEXT-STEPS.md](OWNER-NEXT-STEPS.md) items 1–5 now have pass numbers, except the flex route survey (3a), which reports where the strips arrive. Their results feed one more carrier layout revision that is needed anyway: J50/J51 must move, because flex arm B cannot reach J51 as placed (`design/CARRIER-LAYOUT-REVIEW.md`, Mechanical findings, item 1). The converter fault-current screen is closed: U21/U22 are TPS62172 and the 1.35 A screen passes. Two LCSC codes from a search index need confirming (item 7a: C112291 and C59873), and the release reviews must be redone on the final board.

**17 October 2026 update:** U10's JTAG port and PROGRAMn are now wired to the RP2350 (GPIO21/22/28/29/30 through R2044–R2048, 220 Ω), so assembled boards need no FPGA cable once the RP2350 configuration firmware exists; until then, first bring-up uses a MachXO2 cable on J10 with RUN held low.

Claude's `4b8c7d6` is the routed-board baseline. Online lookups are now
resolved (except the two search-index codes above), but **the manufacturing release remains unapproved**. Printing
and checking the refreshed template is useful now; fit alone does not
resolve the socket move or the unfinished mounting fixture.

| Item | Evidence / remaining work |
|---|---|
| Routed carrier | 59 × 115 mm. 11 October baseline: 351 footprints, 11,318 segments, 1,004 vias. Since 17 October: 356 footprints, 12,084 segments, 1,043 vias (the FPGA configuration wiring); native KiCad 10.0.7 DRC/opens/parity/ERC all zero. |
| Fabrication research | JLCPCB lists six-layer 0.8 mm, 0.09 mm track/space and suitable vias. Confirm the actual quote preserves the specified 0.8 mm, 1 oz outer / 0.5 oz inner stack. |
| Protection procurement | Exact D21: High Diode SMF5.0CA, C22458622, maximum height 1.4 mm. Exact F21: Littelfuse 0467.500NR, C206990. Manufacturer-recommended lands applied and checked against native CAD. |
| Power | Since 23 October U21/U22 are TPS62172 (0.5 A, 1.35 A maximum static limit); with three logic links widened, the 1.35 A local-branch screen passes with 0 failures (the TPS62162 at 2.5 A had 138). Normal-current screening passes. This peak-based screen does not establish continuous fault current or fuse coordination. The 5 V harness needs its tap fuse (Littelfuse 0251002.MXL), the only reverse-polarity protection once the flex is plugged in. |
| Native verification | Fresh native DRC/opens/parity/ERC are zero. Physical-pad audit and 8,240 static integration cases pass; power report retains its explicit limitations. |
| Print fit | Letter/A4 native-outline sheets identify the PCB hash and now show the socket bodies, free support bands and the microSD pull-out corridor. `OWNER-NEXT-STEPS.md` item 3 gives a pass number for each edge, the BIOS opening, parts under the laminate, the 6/8 mm boundary, the microSD corridor and the support anchors; item 3b measures the shield height directly. |
| Flex | Owner meter proof of CN503 A/B and contact numbering (unpowered; `OWNER-NEXT-STEPS.md` item 1) remains required before either arm order. Supply voltage cannot identify the rows. |
| Flex sockets | **Blocker:** as placed, arm B cannot reach J51 and arm A would fold into the gap beside CN503. J50/J51 move in the next layout revision, and both arms are redrawn; the flex route survey (item 3a) decides where. |
| Device 1 | Free K-UI log test, `OWNER-NEXT-STEPS.md` item 4. ATA=5 or 6 suggests the retained GD-ROM answers for device 1, which the design assumes it does not; settle that before ordering. ATA=3 does not prove the drive stays off the bus: add the raw DEV=1 echo diagnostic to K-UI first, or accept the risk for the prototype. |
| C5 mount | Earlier four support holes hit current copper. No fixture is qualified. Physical anchors, 0.90 mm spring gap and at least 6.24 N retention reaction remain to resolve. |
| Assembly | Supplier agreement remains needed for contact/oscillator fixture flags, exact catalog selections (including the two search-index codes) and placement preview. |
| Release | Refresh review hashes/exports and resolve recorded conditions before approval. REVIEW-ONLY output supports inspection/quotation, not an order. |

Height checks use the whole stack: the 6 mm zone leaves 0.4 mm after a
2.5 mm stand-off, 0.9 mm maximum PCB and 2.2 mm maximum FH12 connector,
before other tolerances/insulation. In the 8 mm zone, module plus clamp
above the 0.90 mm seating gap has 3.80 mm nominal allowance, or 3.70 mm at
maximum carrier thickness. Paper checks cannot verify these heights.

A first prototype can deliberately precede completed firmware, provided
its unresolved bring-up work is understood. Operation still needs RP2350
ATA/SD/link/cache firmware, K-UI device-1 support, C5 link changes, a
Diamond bitstream and hardware timing/coexistence tests. Copper
connectivity does not prove these functions.

[OWNER-NEXT-STEPS.md](OWNER-NEXT-STEPS.md) gives the continuity points and
owner/supplier checklist. Sources: [JLCPCB](https://jlcpcb.com/resources/6-layer-pcbs),
[TI](https://www.ti.com/lit/ds/symlink/tps62170.pdf),
[D21](https://jlcpcb.com/partdetail/HighDiode-SMF50CA/C22458622),
[F21](https://jlcpcb.com/partdetail/Littelfuse-0467500NR/C206990), and
`design/sources/connector-height-review.json`.
