# CN503 split-arm passive flex, Rev A

There are now two routed native flex designs. **These are engineering prototypes; installed fit, motherboard continuity, signal integrity and JLC production-file approval remain unqualified.** They are not a released working bridge or a verified installation kit.

- `KUI-CN503-A-Flex-RevA.kicad_pcb` connects the BIOS-facing/west row to carrier J50.
- `KUI-CN503-B-Flex-RevA.kicad_pcb` connects the motherboard-edge/east row to carrier J51.
- Each arm has 20 routed conductors and its own schematic and local contact/finger footprint library.
- The two independently positioned arms eliminate the need to manufacture a rigid 5.03 mm row-center separation. A and B have different mappings and mirror-shaped landings; they are not interchangeable.
- `../design/flex-interface.json` is the carrier pin contract. The parent `docs/CN503-reference.csv` remains a logical reference, rather than continuity evidence.

Each arm routes 14 ATA signals, one 3.3 V reference, one optional 5 V conductor, and four separate ground returns. All 28 ATA signals, four supply contacts and eight returns appear in the pair. There are no pads or copper connections at the ten excluded contacts, including both +12 V pins and every audio contact. The four return traces stay separate on the flex and join ground on the carrier and motherboard.

The carrier uses **Hirose FH12-20S-0.5SH(55)** connectors: 20 positions, 0.5 mm pitch, bottom contacts, horizontal insertion, 2 mm installed height. Gold fingers are on B.Cu. Viewed from the F.Cu/top side with the mating tip north and gold fingers down, **both tails have pin 1 at the left and pin 20 at the right**, matching the unrotated bottom-contact socket. J50 pin 1 maps A1; **J51 pin 1 maps B21/GND and pin 20 maps B1/CONSOLE_3V3**. B's tail numbers therefore decrease as its CN503 contact numbers increase. Both CN503 rows are numbered from the south/CN601 end toward the north/AV end. Rotate a tail and its socket together; do not flip an arm to change the contact face.

**Baseline load power comes from a separate fused power harness.** The carrier's optional flex 5 V links are unpopulated; the flex is not assumed to power the C5, FPGA, RP2350, SD card or hold-up circuit. A connector contact is rated 0.5 A, with 70% derating when all contacts carry current, but that is not a qualification of this flexible trace stackup or the console rail. Any later powered-flex assembly option requires a full rail budget, voltage-drop and thermal review first.

The approximately 60 mm flat pattern has a narrow landing/neck alongside CN503 and widens beyond the connector's north end before reaching the 10.5 mm mating tail. It leaves about 40 mm from the last used solder contact to the tip. The first routed pattern is not evidence that a carrier can occupy any particular location or that the north route clears the AV connector and metal. The initial prototype must verify the installed route; there are no vias or pads in the specified Y24..51.5 mm bend region. Maintain at least 1.2 mm static bend radius and do not crease the flex. The shape around CN503's end follows provisional housing measurements and may need a later outline correction.

Run from the repository root:

```sh
/usr/bin/python3 hardware/g1-sd-wifi/flex/tools/generate_flex.py
/usr/bin/python3 hardware/g1-sd-wifi/flex/tools/check_flex.py
```

`generate_flex.py` is a reproducible native CAD generator. `check_flex.py` independently reads the original CN503 CSV, runs KiCad ERC/DRC with schematic parity, exports netlists and checks that each expected contact reaches its exact carrier pin without extra signals. It also loads the native PCB with `pcbnew` to check physical tail handedness, bottom copper and 0.5 mm finger pitch. Use the system Python containing KiCad's `pcbnew` module. A successful run checks CAD consistency, rather than installed hardware behavior.

See [fabrication notes](../manufacturing/flex-fabrication-notes.md) for the exact prototype stackup, coverlay apertures, stiffener and remaining release conditions.
