# C5 support reconciliation — 2026-10-11

The previous four-hole C5 support cannot be attached to the current 59 × 115 mm carrier without cutting populated pads and routed copper. Keep the current PCB unchanged and develop the independent support/retention fixture from verified console anchor and obstacle measurements. No fixture models were copied, no holes were added, and no new fixture CAD was generated.

The reviewed native PCB has SHA-256 `7d7c3d2693fd01a43f67d1618dabdd9e908bb48c382820c1bf2be7a41367bfc9`. Coordinates below are millimetres from its north-west corner, whose native coordinate is (70, 45). The old support requires 1.8 mm NPTH holes, 0.30 mm copper clearance from each drill edge, and a 3.6 mm boss envelope.

| Old mount | Carrier-local centre | Populated pad centres inside proposed drill | Routed tracks cut | Via copper cut |
|---|---|---|---:|---:|
| NW | (2.1, 2.8) | R2028.1, R2028.2 | 3 | 0 |
| NE | (26.3, 2.8) | C2036.1 (+5V_C5) | 3 | 0 |
| SW | (2.1, 17.2) | R2030.1, R2030.2 | 6 | 2 |
| SE | (26.3, 17.2) | U204.6, U204.7, U204.8 | 11 | 1 |

These are direct drill intersections, not merely courtyard or clearance warnings. The audit reads the serialized native pad centres, trace endpoints/widths and via diameters; its calculation does not mutate the PCB. Filled zones are unnecessary to establish the destructive intersections and were not used to certify safe mounting locations. A read-only KiCad 10.0.7 `pcbnew` cross-check confirmed all eight pad centres, all trace/via counts and all 16 contact centroids/rotations; the PCB hash remained unchanged. [Mechanical evidence](mechanical-support-review.json) records the geometry, source hashes and limits.

The useful reusable reference is the module-side geometry: the old model and current contact-coordinate contract use the same official C5 source hash and module origin (14, 10) carrier-local. All 16 current spring body centroids and rotations match their contact contract exactly. The earlier pocket and corner bearing patches can therefore guide a future fixture, but its through-board bases, caps and backers are incompatible with this carrier. This result does not establish that every possible relocated hole is impossible; choosing new holes would reopen placement, routing and arm geometry and require complete native checks.

The concrete next step is an independent rigid support carrying both the carrier and removable C5 end clamp, with attachment outside the present populated/routed carrier. Select its anchors from the physical fit check; the paper perimeter does not provide mounting coordinates. Keep the PCB and all spring contacts fixed while establishing the support geometry.

Before producing the new fixture CAD:

1. Print the carrier fit sheet at **Actual size / 100%**, confirm both 50 mm calibration bars, and check the perimeter, north/south registration and candidate independent support points in the assembled console.
2. Measure the modified USB-free C5 bare PCB thickness, total height, antenna connector/cable and removal envelope. Source thickness metadata cannot set the final clamp seat.
3. Check actual underside obstacles and shield height at every support and C5 corner. The 2.5 mm underside stand-off and 8 mm upper region remain nominal assumptions.
4. Establish a precision insulating 0.90 mm nominal underside gap. Confirm the loaded gap at all corners within 0.87–0.93 mm and contact alignment within ±0.10 mm. Raw printed dimensions do not qualify these stops.
5. Bench-test retention above the 6.24 N aggregate minimum spring reaction, including maximum spring force/lot variation, PCB deflection, fastener retention and temperature/creep. The minimum reaction is not a proven maximum load.

The existing manufacturer-derived S7221-45R contract calls for a recommended 0.90 mm working gap, a minimum 0.63 mm working gap, free height 1.10–1.23 mm, and at least 0.39 N per contact across 16 contacts. This audit is attachment evidence, not a fabrication or installed-fit release. Do not drill the carrier or fabricate the old attachment models from it.

The follow-up board `8c891b8b5a62dc336a8c3707fa846e28394412a2acb061c2c4e02a207ac74ed8` preserves all footprint placements and route coordinates (`power-native-preservation-check.json`); the old holes remain destructive. Track-intersection counts above refer to the original routed baseline; widened traces may add intersections.
