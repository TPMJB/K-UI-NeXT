# Carrier layout review — 10 October 2026

Status: **current native checkpoint reviewed; fabrication release remains blocked by 600 open connections, secondary local bypass work and installation mechanics**. The reviewed PCB is `controller/KUI-G1-Bridge-RevA.kicad_pcb`, SHA-256 **`fa3e88489094eb537a993f7c3f23d4d93129e2101b7172367934f1d7267eb7b8`**. The spring pattern, all forty flex mating positions and corrected ground-plane contours were independently checked again on this exact replacement snapshot. This report does not qualify electrical operation or mechanical fit and does not authorize ordering the current carrier.

| Current native snapshot | Result |
|---|---:|
| Physical footprints | 364 |
| Fitted SMT components | 277, all on top |
| Bare underside raw-G1 probe pads | 39, no solder paste |
| Copper segments / through vias | 437 / 318 |
| F.Cu / In2.Cu track segments | 432 / 5 |
| Signal tracks on reference layer In1.Cu | 0 |
| All-severity native DRC geometry violations | 0 |
| Native schematic parity issues | 0 |
| Unconnected items, uncapped native count | **600** |
| Unconnected GND / power / other signal | **3 / 209 / 388** |

The independent DRC used all track errors, schematic parity, all severities and zone refill without saving the PCB. Its ordinary DRC output displays a capped 499 unconnected items; a fresh native connectivity calculation proves the full count is 600. Fresh category-specific calculations independently reproduce the 3 + 209 + 388 breakdown. Geometry DRC success does not close these connections. Exact source hashes, native pad coordinates, every flex mating position, plane contours and the checks' negative controls are recorded in `carrier-layout-review-evidence.json`.

## Checks passed on the current snapshot

- **All sixteen C5 nominal spring tips match the manufacturer's actual native underside lands.** K201–K216 were checked individually against the fourteen numbered SMD lands and TP2/BOOT plus TP8/EN in Seeed's official native C5 PCB, SHA-256 `de3838f8908e40d6f022e43ae3a3261bbdc79d436a39925a1a898824917d7607`. The check applies each actual carrier footprint's orientation and its body-origin-to-tip offset; the maximum discrepancy is below 0.001 mm. A deliberately displaced contact fails the check. This is XY registration evidence; compression, wipe, fixture and height remain open.
- **All forty A/B flex mating positions match the selected bottom-contact socket and current carrier pad nets.** With each tail tip north, F.Cu up and B.Cu gold down, both tails have physical pin1 left, pin20 right, 0.5 mm pitch and B.Cu-only fingers. J50/J51 rotations were applied in the native socket frame. J51 pin1 is B21/GND and pin20 is B1/CONSOLE_3V3. The intentional CN503 `CONSOLE_5V` to carrier `FLEX_5V` naming is preserved. Reversing B's physical order fails the check.
- **The corrected In1 reference is continuous around the BIOS hole and CN503 notch.** Its native geometry has one eight-vertex outer outline, one four-vertex BIOS hole and one filled polygon. The In1 no-track rule uses the same proper outer/hole geometry. The current native SVG and its render were visually inspected; there is no diagonal void. No signal tracks occupy In1.Cu. Future routing still needs return-path review.
- All thirty-nine raw-G1 probes are bare B.Cu pads with no B.Paste; they do not introduce underside SMT assembly. The native routing audit records added probe stubs at most 2.997 mm. They are accessible with the carrier removed and need installed underside insulation.
- Seeed's official C5 PCB has no fitted underside IC, resistor, capacitor or connector. Its bottom footprint inventory consists of test lands, battery lands and a logo. The carrier reserves the C5 body rectangle. This supports a clear nominal carrier-facing module surface, but does not prove warpage or installed height.
- The BIOS cutout uses the measured 26.68 mm west–east body axis; the CN503 notch uses the measured 38.67 mm north–south housing axis. Both rotated FPC sockets lie west of the retained housing. This provisional datum does not register the entire carrier to the installed motherboard.

## Current local power, clock and bypass copper

The current board contains explicit local routes for the three switch nodes; fifteen RP input/local bypass feeds; RP output, filtered AVDD and quiet feedback; both TI buck input/output/VOS circuits; the RP crystal loop; and twenty-eight FPGA, major IO, NOR and PSRAM positive bypass feeds with local ground vias. These routes are preserved in `design/carrier-critical-routes.json`. The current source also contains remaining ground fanout and the raw-G1 probe taps. Global power and signal routing remain incomplete.

| Native critical-net copper | Current length | Evidence and remaining review |
|---|---:|---|
| RP VREG_LX switch node | 4.494 mm | F.Cu, no vias; 0.15 mm escape then 0.40 mm main copper. Earlier 2.988 mm metric is superseded. |
| Logic buck SW | 2.551 mm | F.Cu, no vias; 0.15 mm escape then 0.40 mm main copper |
| Storage buck SW | 2.056 mm | F.Cu, no vias; 0.15 mm escape then 0.40 mm main copper |
| RP MCU_XIN | 6.784 mm total | F.Cu, no signal vias; total includes its load-capacitor branch |
| RP MCU_XOUT | 1.300 mm | F.Cu path to damping resistor, no signal vias |
| RP MCU_XTAL_OUT | 6.384 mm total | F.Cu, no signal vias; total includes its load-capacitor branch |

The RP feedback and filtered AVDD use In2.Cu behind the corrected In1 reference. Their local copper avoids crossing the F.Cu input/switch loop. The TI VOS pickups run from output-capacitor nodes; their quiet branches must be preserved when the power nets are completed. Same-net connectivity cannot prove a Kelvin pickup: final review must confirm that load current has not been routed through these branches and that input/output ground loops remain compact. The QMI source resistor is close to the RP, but the NOR/PSRAM clock and data network is not fully routed or timing-qualified.

Twenty-seven of the twenty-eight major-device positive bypass feeds are at most 1.995 mm. U35 C227 uses a 2.390 mm dogleg to clear adjacent TSSOP signal pads and the RP bypass ground land; it is the documented exception to the 2 mm target. Each of these twenty-eight capacitor ground pads has a 0.60 mm, 0.30 mm wide local stub and a 0.50/0.20 mm through via. The RP core bypasses were moved close to their actual power pins, with separate input/analog/core paths. C2018's ground via is now at local (37.3, 26.15), avoiding the earlier drill-separation violation. Short bottom-edge RP positive feeds use 0.15 mm copper to clear the crystal escapes. Native geometry passes all-severity DRC; supply impedance, current capacity, oscillator behavior and high-speed timing remain unqualified.

**Whole-board decoupling remains open.** A separate scratch proposal for 26 small-device owner clusters has not been merged or routed. The current frozen PCB retains the following unresolved owner/capacitor groups:

| Owner or group | Bypasses still needing local placement and route closure | Current frozen PCB evidence |
|---|---|---|
| FPGA oscillator X10 | C119 | Positive-pad separation 26.741 mm; local oscillator supply bypass is not established |
| BIOS NOR U40 | C400, C401 | 22.994 / 21.925 mm positive-pad separation |
| Small supervisors/logic U23–U34 | C212–C223 | Owner-specific short supply feeds and nearby returns remain unqualified |
| Small logic U37–U39, U70, U72, U73 | C229–C234 | Same; shared power-net membership does not prove local bypass |
| BIOS support U41–U45 | C402–C406 | Owner-specific supply bypass and ground returns remain unqualified |
| C5 regulator U71 | C235, C236, C2036 | Input/output/C5 bulk separations 5.701 / 15.086 / 54.172 mm |
| microSD socket J201 | C2025, C2026 | F.Cu socket housing prevents a ≤2 mm external positive-cap location; power distribution and actual return need review |

These are layout blockers for a fabrication release even when their capacitors are present in the netlist. Optional BIOS parts can be DNP in a purchasing variant, but populated variants require their corresponding local bypass geometry. Exact measured owner/pad pairs are retained in the evidence JSON; the separate scratch reclustering proposal has no qualified status.

## Mechanical findings that block release

1. **Motherboard obstacles and north/south registration are not qualified.** The 59 × 105 mm outline is provisional. A 2.5 mm carrier-underside stand-off clears only motherboard parts below that height, with an additional insulating/tolerance allowance. The named installed obstacles needing fit confirmation are **CE502 directly below IC501/BIOS, CE501 southeast/below CN503, and CE504 near CN601 at the front/south edge**. The current 59 × 105 mm carrier projection and its southern power circuitry have not been registered to those capacitors. Their exact outline and height are not recoverable from the angled photos, and no calibrated motherboard CAD overlay exists. The actual-size paper carrier must show each capacitor outside its projection or inside an appropriate relief/cutout; the nominal 2.5 mm underside gap is not evidence that it passes over them. Sockets, plastic posts and shield features also need installed contour clearance. A BIOS/CN503 pin template alone cannot establish the full carrier fit. The two no-clearance thermal chip areas remain hard keepouts; the proposed outline must be registered against them.
2. **The C5 compression fixture has no finished mechanical design.** The electrical spring pattern is usable, but the module cannot simply rest on the contacts. An insulating rigid support and removable clamp must set the nominal copper-to-module underside gap to 0.90 mm, preserve XY alignment, and resist at least 6.24 N of aggregate spring reaction plus tolerances without bowing either PCB. Hard stops must prevent over-compression. There are currently no qualified attachment points, fasteners or clamp CAD files. Do not substitute adhesive pressure or shield pressure for the clamp.
3. **C5 height is not measured without USB.** With the present 2.5 mm stand-off, 0.8 mm carrier and 0.90 mm working spring gap, the module plus any clamp protrusion has at most **3.80 mm nominal** within an 8 mm region. The original 4.48 mm USB-included measurement cannot prove the USB-free stack fits. The module location must also be confirmed to lie entirely in that 8 mm region. Tolerance and electrical insulation reduce the nominal budget.
4. **The underside stand-off and upper insulation are not established by DRC.** The 6 mm BIOS/CN503 region leaves 2.70 mm for top-mounted parts above the 3.30 mm carrier top datum. The 2.00 mm FPC connectors nominally reach 5.30 mm, leaving 0.70 mm before tolerances and insulation. Shield contact with pads, springs, solder joints or connector shells is unacceptable. Many component heights remain unset in the contract; verify maximum heights from the exact sourced MPNs before accepting the stack.
5. **Card, flex and harness service envelopes need physical fit verification.** The microSD footprint includes its inserted-card outline, but shell access and finger/extraction clearance are not qualified. The flex tails need the correct exposed-face orientation, full insertion, retention and bend clearance. The optional BIOS tap and recovery-switch harness have carrier connectors but no released motherboard-tap or switch-board fabrication files; only the base three-board set is presently implemented in native CAD.

## Corrections and superseded evidence

Three specific defects were corrected before this checkpoint. The original B flex tail handedness reversed all twenty conductors, including the power/ground mating positions. B tail pad numbers and matching schematic/interface/J51 nets were reversed together while preserving physical flex copper. The original plane-hole append used the outer contour and produced a large diagonal void that native DRC did not detect; explicit hole indexing repaired both the GND plane and In1 rule area.

The intermediate carrier SHA-256 `4d412b896b0b465de445983f6a5a105853c37ae43ba12f7ced33f9fe67e0fca2` was withdrawn before publication after the independent all-sixteen check found K210–K216 displaced. A scratch placement filter had protected only K201–K209. All sixteen positions and orientations were restored from the official coordinate source, the affected K213 ground stubs were rebuilt, and the current replacement passed a fresh direct manufacturer-native check. A numeric sixteen-contact generator guard now rejects a deliberately displaced contact. Neither that withdrawn snapshot nor the earlier 325-footprint local-copper candidate is accepted as a mechanical design.

Historical measurements and failed checks remain in the evidence JSON for traceability. The current opening hash and current checks above supersede them; historical DRC and contact checks are not inherited as acceptance of the replacement board.

## Manufacturing and route closure

Finish all 600 native open connections, the remaining local bypass groups and the actual power distribution, then rerun native DRC/parity after plane fill with zero unconnected items. Preserve the quiet feedback branches, crystal copper and reference plane, and review the complete QMI, SD and G1 timing/return paths. DRC and route-count metrics do not establish electrical function.

Review the RP QFN thermal-via drill/annulus, mask and paste against the selected fabrication and assembly process. Its stock footprint contains nine 0.20 mm drilled, 0.50 mm diameter thermal lands beneath the exposed pad. Bottom tenting does not establish acceptable reflow yield; the assembler must confirm the process or specify a supported filled/capped-via and stencil option. Verify exact MPN heights and CPL centroids/rotations in the assembler preview, including Harwin body-origin contacts, inductors, pin1 ICs and FPC sockets. The C5 uses its external U.FL antenna connection; the intended external antenna placement and cable route still need installed RF/fit qualification.

Fabrication release remains open until routing, whole-board bypass geometry, the installed carrier fit around the named obstacles, the C5 fixture/USB-free height, assembly settings and required electrical verification are closed. This checkpoint is a concrete native design for review, not an order-ready carrier.

## Primary source records

The geometry checks use Seeed's [official C5 V1.1 schematic/PCB archive](https://files.seeedstudio.com/wiki/XIAO_ESP32C5/res/Seeed_Studio_XIAO_ESP32C5.zip), identified by native PCB SHA-256 in `sources/xiao-c5-carrier-contact-coordinates.json`, and Harwin's [S7221-45R technical drawing](https://content.harwin.com/asset/1801555a-aeb3-408d-b163-bf8c8afc2263/DRG-02334-Technical-Drawing-Datasheet-S7221-45R-pdf.pdf). The selected RP and TI circuits cite their official design guides in `controller-implementation.md` and `power-and-isolation.md`.
