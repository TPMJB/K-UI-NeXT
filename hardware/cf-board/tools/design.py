# SPDX-License-Identifier: GPL-3.0-only
"""The CF board's design in one place: the GD-ROM connector's pins on the
Dreamcast mainboard (CN503), the CompactFlash socket's True IDE pins, the
parts, and every connection. gen_schematic.py and gen_board.py build the
KiCad files from it, and check_netlist.py compares KiCad's netlist with it.

The CF card is the slave device on the G1 ATA bus, beside the GD-ROM drive
(the master), as KallistiOS's g1ata driver expects."""

# CN503, the GD-ROM connector on the VA1 mainboard: 2 x 25 legs at 1.0 mm,
# A1-A25 along the side facing the middle of the board, B1-B25 along the
# board edge. Signal names follow the iceGDROM riser board, which plugs into
# the same connector (github.com/zeldin/iceGDROM, pcb/riser, GPL-3.0); only
# facts about the connector are taken from it. None: not used here.
CN503 = {
    "A1": "+3V3", "A2": "~{RESET}", "A3": "+5V", "A4": "D7", "A5": "D8", "A6": "D5", "A7": "D10",
    "A8": "GND", "A9": "D3", "A10": "D12", "A11": "D1", "A12": "D14", "A13": "GND", "A14": "DMARQ",
    "A15": "~{DIOR}", "A16": "~{DMACK}", "A17": None, "A18": "GND", "A19": "DA0", "A20": "~{CS0}",
    "A21": "GND",
    "B1": "+3V3", "B2": None, "B3": "+5V", "B4": "D6", "B5": "D9", "B6": "D4", "B7": "D11",
    "B8": "GND", "B9": "D2", "B10": "D13", "B11": "D0", "B12": "D15", "B13": "GND", "B14": "~{DIOW}",
    "B15": "IORDY", "B16": "INTRQ", "B17": "DA1", "B18": "GND", "B19": "DA2", "B20": "~{CS1}",
    "B21": "GND",
}
# What the unused legs carry, for the wiring guide. A22-A25 and B22-B25
# (CD audio, ground, +12 V) are not brought to the board at all.
CN503_UNUSED = {"A17": "EMPH (CD audio emphasis)", "B2": "not connected on the iceGDROM riser"}

# The wire pads (J2): a 2 x 21 field at 2.0 mm, odd pins A1-A21 and even
# pins B1-B21, so each pad takes the wire from the CN503 leg of the same name.
def j2_pin(leg):
    row, n = leg[0], int(leg[1:])
    return 2 * n - 1 if row == "A" else 2 * n

# CompactFlash socket (J1), True IDE mode: pin, name, KiCad electrical type,
# and the net (None: left open).
CF_PINS = [
    (1, "GND", "power_in", "GND"),
    (2, "D3", "bidirectional", "D3"),
    (3, "D4", "bidirectional", "D4"),
    (4, "D5", "bidirectional", "D5"),
    (5, "D6", "bidirectional", "D6"),
    (6, "D7", "bidirectional", "D7"),
    (7, "~{CS0}", "input", "~{CS0}"),
    (8, "A10", "input", "GND"),
    (9, "~{ATA_SEL}", "input", "GND"),       # grounded: True IDE mode
    (10, "A9", "input", "GND"),
    (11, "A8", "input", "GND"),
    (12, "A7", "input", "GND"),
    (13, "VCC", "power_in", "VCARD"),
    (14, "A6", "input", "GND"),
    (15, "A5", "input", "GND"),
    (16, "A4", "input", "GND"),
    (17, "A3", "input", "GND"),
    (18, "A2", "input", "DA2"),
    (19, "A1", "input", "DA1"),
    (20, "A0", "input", "DA0"),
    (21, "D0", "bidirectional", "D0"),
    (22, "D1", "bidirectional", "D1"),
    (23, "D2", "bidirectional", "D2"),
    (24, "~{IOCS16}", "open_collector", None),
    (25, "~{CD2}", "passive", None),
    (26, "~{CD1}", "passive", None),
    (27, "D11", "bidirectional", "D11"),
    (28, "D12", "bidirectional", "D12"),
    (29, "D13", "bidirectional", "D13"),
    (30, "D14", "bidirectional", "D14"),
    (31, "D15", "bidirectional", "D15"),
    (32, "~{CS1}", "input", "~{CS1}"),
    (33, "~{VS1}", "passive", None),
    (34, "~{IORD}", "input", "~{DIOR}"),
    (35, "~{IOWR}", "input", "~{DIOW}"),
    (36, "~{WE}", "input", "VCARD"),        # tied high in True IDE mode
    (37, "INTRQ", "tri_state", "INTRQ"),
    (38, "VCC", "power_in", "VCARD"),
    (39, "~{CSEL}", "input", "~{CSEL}"),    # open: slave (JP2 grounds it)
    (40, "~{VS2}", "passive", None),
    (41, "~{RESET}", "input", "~{RESET}"),
    (42, "IORDY", "tri_state", "IORDY"),
    (43, "DMARQ", "tri_state", "DMARQ"),
    (44, "~{DMACK}", "input", "~{DMACK}"),
    (45, "~{DASP}", "bidirectional", "~{DASP}"),
    (46, "~{PDIAG}", "bidirectional", "~{PDIAG}"),
    (47, "D8", "bidirectional", "D8"),
    (48, "D9", "bidirectional", "D9"),
    (49, "D10", "bidirectional", "D10"),
    (50, "GND", "power_in", "GND"),
]

KICAD = "/usr/share/kicad"
# ref: (library symbol, value, footprint, {pin: net or None}, description for the BOM)
PARTS = {
    "J1": ("cf-board:CF_Card_TrueIDE", "CF socket",
           "cf-board:CF-Card_3M_N7E50-E516xx-30_SmallRing",
           {str(p): net for p, _, _, net in CF_PINS},
           "CompactFlash header, Type I, SMT, 3M N7E50-E516PG-30"),
    "J2": ("Connector_Generic:Conn_02x21_Odd_Even", "To CN503",
           "Connector_PinHeader_2.00mm:PinHeader_2x21_P2.00mm_Vertical",
           {str(j2_pin(leg)): net for leg, net in CN503.items()},
           "Wire pads, 2.0 mm grid (fit nothing, or a 2 x 21 2.0 mm header)"),
    "JP1": ("Jumper:SolderJumper_3_Bridged12", "3V3/5V",
            "Jumper:SolderJumper-3_P1.3mm_Bridged12_RoundedPad1.0x1.5mm",
            {"1": "+3V3", "2": "VCARD", "3": "+5V"},
            "Card supply: 3.3 V as made (cut the trace and bridge 2-3 for 5 V)"),
    "JP2": ("Jumper:SolderJumper_2_Open", "CSEL",
            "Jumper:SolderJumper-2_P1.3mm_Open_RoundedPad1.0x1.5mm",
            {"1": "~{CSEL}", "2": "GND"},
            "Open: slave (as needed in the Dreamcast); bridged: master"),
    "R1": ("Device:R", "470", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
           {"1": "VCARD", "2": "LED_A"}, "Activity LED resistor, 0805"),
    "D1": ("Device:LED", "ACT", "LED_SMD:LED_0805_2012Metric_Pad1.15x1.40mm_HandSolder",
           {"1": "~{DASP}", "2": "LED_A"}, "Activity LED, green, 0805"),
    "R2": ("Device:R", "10k", "Resistor_SMD:R_0805_2012Metric_Pad1.20x1.40mm_HandSolder",
           {"1": "VCARD", "2": "~{PDIAG}"}, "PDIAG pull-up, 0805"),
    "C1": ("Device:C", "10u", "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder",
           {"1": "VCARD", "2": "GND"}, "10 uF 16 V X5R, 0805"),
    "C2": ("Device:C", "100n", "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder",
           {"1": "VCARD", "2": "GND"}, "100 nF X7R, 0805"),
    "C3": ("Device:C", "100n", "Capacitor_SMD:C_0805_2012Metric_Pad1.18x1.45mm_HandSolder",
           {"1": "VCARD", "2": "GND"}, "100 nF X7R, 0805"),
    "H1": ("Mechanical:MountingHole", "M2", "MountingHole:MountingHole_2.2mm_M2", {}, "Mounting hole"),
    "H2": ("Mechanical:MountingHole", "M2", "MountingHole:MountingHole_2.2mm_M2", {}, "Mounting hole"),
}


def nets():
    """{net: sorted [(ref, pin)]} from PARTS."""
    out = {}
    for ref, (_, _, _, pins, _) in PARTS.items():
        for pin, net in pins.items():
            if net:
                out.setdefault(net, []).append((ref, pin))
    return {n: sorted(v) for n, v in out.items()}
