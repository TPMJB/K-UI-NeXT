# G1 bridge order readiness — 11 October 2026

**17 October 2026 update:** U10's JTAG port and PROGRAMn are now wired to the RP2350 (GPIO21/22/28/29/30 through R2044–R2048, 220 Ω), so assembled boards need no FPGA cable once the RP2350 configuration firmware exists; until then, first bring-up uses a MachXO2 cable on J10 with RUN held low. See `design/CARRIER-LAYOUT-REVIEW.md`. The board now has 356 footprints, 12,084 segments and 1,043 vias; DRC, opens, parity and ERC remain zero, and the owner reviews must be repeated for the new board. One new lookup is open: confirm on lcsc.com that C112291 is Yageo RC0402FR-07220RL, and its JLC SMT entry and Basic/Extended status (`OWNER-NEXT-STEPS.md` item 7a).

Claude's `4b8c7d6` is the routed-board baseline. Online lookups are now
resolved (except the 17 October C112291 check above), but **the manufacturing release remains unapproved**. Printing
and checking the refreshed template is useful now; fit alone does not
resolve the corrected power review or unfinished mounting fixture.

| Item | Evidence / remaining work |
|---|---|
| Routed carrier | 59 × 115 mm. 11 October baseline: 351 footprints, 11,318 segments, 1,004 vias. Since 17 October: 356 footprints, 12,084 segments, 1,043 vias (the FPGA configuration wiring); native KiCad10.0.7 DRC/opens/parity/ERC all zero. |
| Fabrication research | JLCPCB lists six-layer 0.8 mm, 0.09 mm track/space and suitable vias. Confirm the actual quote preserves the specified 0.8 mm, 1 oz outer / 0.5 oz inner stack. |
| Protection procurement | Exact D21: High Diode SMF5.0CA, C22458622, maximum height 1.4 mm. Exact F21: Littelfuse 0467.500NR, C206990. Manufacturer-recommended lands applied and checked against native CAD. |
| Power | TI's 2.45 A maximum high-side threshold replaces the assumed 1.5 A. Local-branch screening at 2.5 A exposed failures; bounded widening reduced the screen from240 to138 failed track/via entries; normal-current screening passes, fault qualification remains open. This peak-based screen does not establish continuous fault current or fuse coordination. |
| Native verification | Fresh native DRC/opens/parity/ERC are zero. Physical-pad audit and8240static integration cases pass; power report retains its explicit limitations. |
| Print fit | Letter/A4 native-outline sheets identify the PCB hash. Verify both 50 mm bars, then check BIOS/CN503, north extension, capacitors, posts and zero-clearance thermal chips. |
| Flex | Owner meter proof of CN503 A/B remains required before either arm order. Supply voltage cannot identify the rows. |
| C5 mount | Earlier four support holes hit current copper. No fixture is qualified. Physical anchors, 0.90 mm spring gap and at least 6.24 N retention reaction remain to resolve. |
| Assembly | Supplier agreement remains needed for contact/oscillator fixture flags, exact catalog selections and placement preview. |
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
[TI](https://www.ti.com/lit/ds/symlink/tps62160.pdf),
[D21](https://jlcpcb.com/partdetail/HighDiode-SMF50CA/C22458622),
[F21](https://jlcpcb.com/partdetail/Littelfuse-0467500NR/C206990), and
`design/sources/connector-height-review.json`.
