# Owner next steps — G1 bridge Rev A

11 October 2026. Claude's `4b8c7d6` is the routed-board baseline. This
follow-up resolves the JLCPCB/TI lookups, corrects protection-part lands
and power screening, and refreshes the printable fit sheets. Final native
checks are still required; **the release remains unapproved**. See
[current order readiness](ORDER-READINESS-2026-10-11.md).

17 October 2026: U10's JTAG port and PROGRAMn are now wired to the RP2350
(see `design/CARRIER-LAYOUT-REVIEW.md`). This adds one lookup, item 7a, and
the owner reviews must be repeated for the new board.

Original item numbers are retained to match earlier messages. Items 8 and
9 no longer need research from you. Fit alone will not resolve the power
qualification or unfinished mounting fixture.

## One console-open session

**1. Prove CN503 row A/B by continuity before either flex order.** Power
off; use ohms. Supply/ground contacts are identical on both rows: A1/B1
3.3 V, A3/B3 5 V, grounds 8/13/18/21, A25/B25 12 V. Voltage cannot
distinguish the rows. The proposed mapping came from a mating riser
symbol and trace references, not a test on your motherboard. Swapped rows
require new artwork for both arms.

| IC501 pin | Proposed CN503 contact | Signal |
|---|---|---|
| 14 | A15 | DIOR- |
| 15 | B11 | DD0 |
| 17 | A11 | DD1 |
| 4 | A19 | DA0 |

Record all four readings and identify the physical row you call A in a
photo or sketch. Expect low resistance including any series resistors.

**2. Confirm the supply levels if not already measured.**

| Point | Expected |
|---|---|
| CN503 A1 or B1, proposed south/CN601 end | 3.3 V |
| CN503 A3 or B3 | 5 V |
| IC501 pin 23, BIOS supply | 3.3 V on VA1 |

These check supply assumptions, not row identity or numbering by
themselves. A25/B25 at the proposed north end carry 12 V. Use an insulated
probe exposing only its tip.

**3. Check the refreshed 1:1 template.** Choose the Letter or A4 PDF in
`mechanical/`, print at **Actual size / 100%**, and measure both 50 mm bars
before cutting it out.

- Align the BIOS opening and CN503 notch, including the connector's wider
  6.56 mm ends. Check body clearance, leads and solder access separately.
- Mark CE501, CE502 and CE504; record height and overlap wherever a
  capacitor would sit under solid carrier material.
- Check the north extension near the AV connector, SD access, flex bends,
  case posts and shield edges. The routed outline is 59 × 115 mm, extending
  10 mm further north than the earlier 59 × 105 mm draft.
- Mark both thermal-contact chips as zero-clearance keepouts. Confirm the
  6 mm BIOS/CN503 region and the 8 mm region containing the C5.
- Mark usable support anchors and photograph the template in place. The
  earlier four-hole C5 support model intersects current copper; those are
  not approved holes. Carrier support and C5 retention still need a new
  mechanical design.

Paper checks XY fit. Separately check the complete vertical stack against
its shield: 2.5 mm stand-off + 0.9 mm maximum carrier + 2.2 mm maximum
FH12 connector = **5.6 mm**, leaving only **0.4 mm** in the 6 mm zone before
other tolerances and insulation. Connector evidence is in
`design/sources/connector-height-review.json`.

**5. Measure the USB-free C5 and retained stack.** With a 2.5 mm stand-off,
0.8 mm carrier and 0.90 mm copper-to-module-underside gap, the 8 mm zone
allows **3.80 mm nominal for module plus clamp above the seating plane**.
A 0.9 mm carrier reduces this to 3.70 mm before other tolerances and
insulation. Measure the tallest feature after USB removal; the earlier
4.48 mm reading included USB.

Sixteen spring contacts require at least 6.24 N combined reaction at the
working gap. Hard supports and retention must maintain alignment and
prevent bow or over-compression. Paper fit does not qualify this fixture.

## No teardown needed

**4. Check retained-GD-ROM device-1 response (B4).** Boot K-UI with no card
in the SCI adapter and nothing on the rear port. Record the
`IDE/CF initialization failed: ATA=N` line. The existing diagnostic treats
5 as likely device-1 response, 3 as favourable and 4 as inconclusive.
This preliminary check does not qualify shared-bus coexistence.

## Supplier and release checks

**6. Select the verified rigid-board options in the actual JLCPCB quote.**
The official [six-layer capabilities](https://jlcpcb.com/resources/6-layer-pcbs)
list 0.8 mm boards, 0.09 mm track/space and sufficiently small vias.
Research is complete; confirm the uploaded-board quote retains:

| Option | Required setting |
|---|---|
| Layers | Six: signal / ground / signal / signal / +3V3_LOGIC / signal |
| Finished thickness | 0.8 mm; published tolerance ±0.1 mm |
| Track/space | Support 0.127 mm / 0.127 mm minimum |
| Signal vias | 0.2 mm drill / 0.5 mm copper diameter |
| Supply vias | 0.3 mm drill / 0.6 mm copper diameter |
| Copper | 1 oz outer / 0.5 oz inner |

Height and copper checks depend on these settings. A substituted stack
needs a revised check before ordering.

**7. Obtain assembly agreement.** Harwin S7221-45R, C22445132, and Abracon
ASE, C596955, have catalog assembly-fixture flags. Have JLCPCB confirm the
selected service can assemble them and explain any fixture charge or
handling requirement. Inspect its placement/orientation preview. Factory
assembly fixtures are separate from in-console C5 retention.

**7a. Confirm the FPGA configuration resistor.** R2044-R2048 use Yageo
RC0402FR-07220RL (220 ohm, 0402). Its LCSC code C112291 was taken from a
search engine's index because lcsc.com and jlcpcb.com could not be reached
on 17 October. Check on lcsc.com that C112291 is that exact part, find its
JLC SMT parts-library entry and note whether it is a Basic or Extended part
(Extended parts add a fee to the quote).

**8. Protection-part lookup completed.**

| Ref | Exact selected part | JLCPCB code |
|---|---|---|
| D21 | High Diode SMF5.0CA, bidirectional 5 V, SOD-123FL, 1.4 mm maximum height | [C22458622](https://jlcpcb.com/partdetail/HighDiode-SMF50CA/C22458622) |
| F21 | Littelfuse 0467.500NR, 0.5 A fast, 0603, exact NR suffix | [C206990](https://jlcpcb.com/partdetail/Littelfuse-0467500NR/C206990) |

Manufacturer-recommended lands have been applied to D21 and the F20/F21
467-family fuses. Fresh native DRC/opens/parity/ERC are zero; BASE/DNP assembly exports use the final board. Catalog
identity does not reserve stock; substitutions need electrical and
package review.

**9. TI current-limit lookup completed; corrected power review remains open.**
The [TPS62160-family datasheet](https://www.ti.com/lit/ds/symlink/tps62160.pdf)
gives a TPS62162 high-side limit of 1.45 A minimum, 1.95 A typical and
**2.45 A maximum at VIN = 12 V, TJ = 25°C**. Delay can temporarily increase
the peak. The corrected local-branch copper screen uses 2.5 A. It is not
a guaranteed continuous fault-current bound or fuse-clearing qualification;
the previous 1.5 A assumption was insufficient. Failures under that
conservative screen require correction or justified qualification before
release, not an automatic conclusion that continuous overheating occurs.

**10. Review current evidence before release approval.** Read
`design/CARRIER-LAYOUT-REVIEW.md` and its refreshed reports. The exporter
requires an approved manifest and accepted reviews bound to current
input hashes. Do not change approval flags merely to obtain an archive.
REVIEW-ONLY files support inspection/quotation; they are not order files.

Signal routes on In4.Cu cut slots in the +3V3_LOGIC plane. Connected planes
and DC screening do not prove return-path integrity, bus timing or
retained-drive coexistence. Final DRC, opens, parity, physical-pad and
power reports must match the final board; unresolved checks remain
release conditions.

**11. Diamond for functional bring-up.** FPGA simulation/mapping exists;
vendor placement/routing, a bitstream and timing verification still need
Lattice Diamond and an account/licence. This is a bring-up dependency,
separate from consciously ordering an accepted first hardware prototype.
Since 17 October 2026 the RP2350 drives U10's JTAG port and PROGRAMn
(GPIO21/22/28-30 through R2044-R2048), so assembled boards need no Lattice
cable once the RP2350 configuration firmware (a JTAG player) exists. That
firmware is new work and not written yet: for first bring-up, have a
target-powered 3.3 V MachXO2 JTAG cable for the J10 pads and hold RUN
(J203.5) low while it is attached.

## After fabrication

Follow `manufacturing/BRINGUP.md`: current-limited bench power, FPGA, SD,
C5, then console integration. Do not seat the spring-contact C5 without
qualified supports/retention. Measure J20 during GD-ROM spin-up and
establish upstream harness protection: F20 cannot protect wire ahead of
it, and fuse nameplate current alone does not bound fault current.

Compare measured steady 5 V current with exact F20 time-current and
temperature derating data. The summed 1.35 A budget is 90% of its 1.5 A rating;
check nuisance opening in the enclosure. Fuse changes need matching
copper and protection coordination.

Firmware still needed before operation: RP2350 ATA/SD/link/PSRAM cache;
K-UI NOP unlock, IDENTIFY and PIO reads; and C5-to-RP2350 link changes.
These and real timing/coexistence tests remain deliberate prototype
bring-up work, not completed functionality.
