# SPDX-License-Identifier: GPL-3.0-only
"""Writes cf-board.kicad_sym (the CompactFlash socket's symbol) and
cf-board.kicad_sch from design.py: every pin gets a short wire ending in a
net label, a power symbol, or a no-connect flag. Needs kiutils
(pip install kiutils) and KiCad's symbol libraries in /usr/share/kicad."""
import math
import sys
import uuid
from pathlib import Path

from kiutils.items.common import Effects, Font, Justify, Position, Property, Stroke
from kiutils.items.schitems import (Connection, Junction, LocalLabel, NoConnect, SchematicSymbol,
                                    SymbolInstance, Text)
from kiutils.schematic import Schematic
from kiutils.symbol import SymbolLib

sys.path.insert(0, str(Path(__file__).parent))
import design  # noqa: E402

HERE = Path(__file__).resolve().parent.parent
GRID = 1.27
STUB = 2.54


def uid():
    return str(uuid.uuid4())


# ---- The CompactFlash socket's symbol ----
LEFT = ["D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9", "D10", "D11", "D12", "D13", "D14", "D15",
        None, "A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10"]
RIGHT = ["A0", "A1", "A2", "~{CS0}", "~{CS1}", "~{IORD}", "~{IOWR}", "IORDY", "INTRQ", "DMARQ", "~{DMACK}",
         "~{RESET}", "~{CSEL}", "~{DASP}", "~{PDIAG}", "~{ATA_SEL}", "~{WE}", None, "~{IOCS16}", "~{CD1}",
         "~{CD2}", "~{VS1}", "~{VS2}"]


def write_cf_symbol(path):
    by_name = {}
    for pin, name, kind, _ in design.CF_PINS:
        by_name.setdefault(name, []).append((pin, kind))
    lines = []

    def pin(kind, x, y, angle, name, number, hide=False):
        lines.append(f'      (pin {kind} line (at {x:g} {y:g} {angle}) (length 5.08){" hide" if hide else ""}\n'
                     f'        (name "{name}" (effects (font (size 1.27 1.27))))\n'
                     f'        (number "{number}" (effects (font (size 1.27 1.27))))\n      )')
    top = 30.48
    for i, name in enumerate(LEFT):
        if name:
            (number, kind), = by_name[name]
            pin(kind, -17.78, top - i * 2.54, 0, name, number)
    for i, name in enumerate(RIGHT):
        if name:
            (number, kind), = by_name[name]
            pin(kind, 17.78, top - i * 2.54, 180, name, number)
    for i, (number, kind) in enumerate(by_name["VCC"]):
        pin(kind, -2.54 + 5.08 * i, 38.1, 270, "VCC", number)
    for i, (number, kind) in enumerate(by_name["GND"]):
        pin(kind, -2.54 + 5.08 * i, -38.1, 90, "GND", number)
    body = "\n".join(lines)
    text = f'''(kicad_symbol_lib (version 20220914) (generator gen_schematic)
  (symbol "CF_Card_TrueIDE" (pin_names (offset 1.016)) (in_bom yes) (on_board yes)
    (property "Reference" "J" (at -12.7 35.56 0)
      (effects (font (size 1.27 1.27)) (justify left))
    )
    (property "Value" "CF_Card_TrueIDE" (at 2.54 35.56 0)
      (effects (font (size 1.27 1.27)) (justify left))
    )
    (property "Footprint" "cf-board:CF-Card_3M_N7E50-E516xx-30_SmallRing" (at 0 -43.18 0)
      (effects (font (size 1.27 1.27)) hide)
    )
    (property "Datasheet" "https://www.compactflash.org" (at 0 -45.72 0)
      (effects (font (size 1.27 1.27)) hide)
    )
    (property "ki_description" "CompactFlash card socket, pins named for True IDE mode" (at 0 0 0)
      (effects (font (size 1.27 1.27)) hide)
    )
    (symbol "CF_Card_TrueIDE_0_1"
      (rectangle (start -12.7 33.02) (end 12.7 -33.02)
        (stroke (width 0.254) (type default))
        (fill (type background))
      )
    )
    (symbol "CF_Card_TrueIDE_1_1"
{body}
    )
  )
)
'''
    path.write_text(text)


# ---- Placing symbols ----
LIBS = {}


def lib_symbol(lib_id):
    lib, name = lib_id.split(":")
    if lib not in LIBS:
        path = HERE / "cf-board.kicad_sym" if lib == "cf-board" else Path(design.KICAD) / "symbols" / f"{lib}.kicad_sym"
        LIBS[lib] = SymbolLib.from_file(str(path))
    for sym in LIBS[lib].symbols:
        if sym.entryName == name:
            assert not sym.extends, lib_id
            return sym
    raise KeyError(lib_id)


def pins_of(sym):
    out = []
    for unit in sym.units:
        out.extend(unit.pins)
    return out


def transform(x, y, sx, sy, rot):
    """A library point to schematic coordinates (library y points up)."""
    dx, dy = x, -y
    a = math.radians(rot)
    return (round(sx + dx * math.cos(a) + dy * math.sin(a), 4),
            round(sy - dx * math.sin(a) + dy * math.cos(a), 4))


def outward(angle, rot):
    """Unit vector from a pin's end away from the symbol body."""
    a = math.radians(angle)
    dx, dy = -math.cos(a), math.sin(a)  # library direction toward the body, flipped and y inverted
    r = math.radians(rot)
    return (round(dx * math.cos(r) + dy * math.sin(r)), round(-dx * math.sin(r) + dy * math.cos(r)))


class Sheet:
    def __init__(self):
        self.sch = Schematic.create_new()
        self.sch.version = 20211123
        self.sch.uuid = uid()
        self.sch.paper.paperSize = "A3"
        self.embedded = set()
        self.power_count = 0

    def embed(self, lib_id):
        if lib_id in self.embedded:
            return
        sym = lib_symbol(lib_id)
        sym.libraryNickname = lib_id.split(":")[0]
        self.sch.libSymbols.append(sym)
        self.embedded.add(lib_id)

    def place(self, lib_id, ref, value, footprint, x, y, rot=0, fields=None, hidden_ref=False, hidden_value=False):
        self.embed(lib_id)
        sym = lib_symbol(lib_id)
        s = SchematicSymbol(libraryNickname=lib_id.split(":")[0], entryName=lib_id.split(":")[1],
                            position=Position(x, y, rot), unit=1, inBom=not ref.startswith("#"),
                            onBoard=not ref.startswith("#"), uuid=uid())
        eff = Effects(font=Font(width=1.27, height=1.27))
        hid = Effects(font=Font(width=1.27, height=1.27), hide=True)
        ref_xy = fields.get("ref_at", (x, y - 5)) if fields else (x, y - 5)
        val_xy = fields.get("val_at", (x, y + 5)) if fields else (x, y + 5)
        s.properties = [
            Property(key="Reference", value=ref, id=0, position=Position(*ref_xy, 0), effects=hid if hidden_ref else eff),
            Property(key="Value", value=value, id=1, position=Position(*val_xy, 0), effects=hid if hidden_value else eff),
            Property(key="Footprint", value=footprint, id=2, position=Position(x, y, 0), effects=hid),
            Property(key="Datasheet", value="", id=3, position=Position(x, y, 0), effects=hid),
        ]
        s.pins = {p.number: uid() for p in pins_of(sym)}
        self.sch.schematicSymbols.append(s)
        self.sch.symbolInstances.append(SymbolInstance(path=f"/{s.uuid}", reference=ref, unit=1, value=value,
                                                       footprint=footprint))
        return sym

    def wire(self, a, b):
        self.sch.graphicalItems.append(Connection(type="wire", points=[Position(*a), Position(*b)],
                                                  stroke=Stroke(width=0, type="default"), uuid=uid()))

    def label(self, text, at, direction):
        angle = {(-1, 0): 180, (1, 0): 0, (0, 1): 270, (0, -1): 90}[direction]
        just = Justify(horizontally="right") if angle in (180, 270) else Justify(horizontally="left")
        self.sch.labels.append(LocalLabel(text=text, position=Position(at[0], at[1], angle),
                                          effects=Effects(font=Font(width=1.27, height=1.27), justify=just),
                                          uuid=uid()))

    def power(self, net, at, direction):
        """A power symbol at the end of a stub, pointing away from the pin,
        its name written level just beyond it."""
        lib_id = {"GND": "power:GND", "+3V3": "power:+3V3", "+5V": "power:+5V"}[net]
        self.power_count += 1
        if net == "GND":
            rot = {(0, 1): 0, (0, -1): 180, (1, 0): 90, (-1, 0): 270}[direction]
        else:
            rot = {(0, -1): 0, (0, 1): 180, (1, 0): 270, (-1, 0): 90}[direction]
        if direction[1] == 0:
            val_at = (round(at[0] + direction[0] * 3.81, 4), at[1])
        else:
            val_at = (at[0], round(at[1] + direction[1] * 4.45, 4))
        sym = self.place(lib_id, f"#PWR{self.power_count:02d}", net, "", at[0], at[1], rot, hidden_ref=True,
                         fields={"ref_at": (at[0], at[1] + 3), "val_at": val_at})
        if direction[1] == 0:
            # KiCad turns fields with their symbol: a quarter turn back keeps the name level.
            value = self.sch.schematicSymbols[-1].properties[1]
            value.position.angle = 90
            # A symbol turned 90 degrees shows the field upside down, which KiCad mirrors back.
            outward_right = direction[0] > 0
            if rot == 90:
                outward_right = not outward_right
            value.effects.justify = Justify(horizontally="left" if outward_right else "right")
        return sym

    def no_connect(self, at):
        self.sch.noConnects.append(NoConnect(position=Position(*at), uuid=uid()))

    def junction(self, at):
        self.sch.junctions.append(Junction(position=Position(*at), diameter=0, uuid=uid()))

    def tie(self, sym, x, y, rot, pins, net, stub=STUB):
        """Pins on one side, all on `net`: stubs to a common wire, one symbol."""
        ends = []
        for p in pins_of(sym):
            if p.number in pins:
                end = transform(p.position.X, p.position.Y, x, y, rot)
                d = outward(p.position.angle, rot)
                tip = (round(end[0] + d[0] * stub, 4), round(end[1] + d[1] * stub, 4))
                self.wire(end, tip)
                ends.append(tip)
        ends.sort(key=lambda t: (t[1], t[0]))
        for a, b in zip(ends, ends[1:]):
            self.wire(a, b)
        for t in ends[1:-1]:
            self.junction(t)
        last = ends[-1]
        tail = (last[0], round(last[1] + stub, 4))
        self.wire(last, tail)
        self.junction(last) if len(ends) > 1 else None
        self.power(net, tail, (0, 1))

    def connect_pins(self, sym, x, y, rot, pin_nets, stub=STUB, skip=()):
        for p in pins_of(sym):
            if p.number in skip:
                continue
            end = transform(p.position.X, p.position.Y, x, y, rot)
            d = outward(p.position.angle, rot)
            net = pin_nets.get(p.number)
            if net is None:
                self.no_connect(end)
                continue
            tip = (round(end[0] + d[0] * stub, 4), round(end[1] + d[1] * stub, 4))
            self.wire(end, tip)
            if net in ("GND", "+3V3", "+5V"):
                self.power(net, tip, d)
            else:
                self.label(net, tip, d)

    def text(self, body, x, y, size=1.27):
        # KiCad wants line breaks in strings escaped.
        self.sch.texts.append(Text(text=body.replace("\n", "\\n"), position=Position(x, y, 0),
                                   effects=Effects(font=Font(width=size, height=size),
                                                   justify=Justify(horizontally="left")), uuid=uid()))


def main():
    write_cf_symbol(HERE / "cf-board.kicad_sym")
    sheet = Sheet()
    tb = sheet.sch.titleBlock
    from kiutils.items.common import TitleBlock
    sheet.sch.titleBlock = TitleBlock(title="K-UI CF board: CompactFlash on the Dreamcast's G1 ATA bus",
                                      date="2026-09-29", revision="1", company="TPMJB / K-UI",
                                      comments={1: "The CF card is the slave beside the GD-ROM drive (master).",
                                                2: "J2's pads take wires from the same-named legs of CN503 on the VA1 mainboard.",
                                                3: "Generated by tools/gen_schematic.py from tools/design.py."})
    del tb
    layout = {
        "J1": (120.65, 128.27, 0), "J2": (260.35, 128.27, 0),
        "JP1": (60.96, 215.9, 0), "JP2": (106.68, 215.9, 0),
        "R1": (144.78, 208.28, 0), "D1": (160.02, 222.25, 0), "R2": (182.88, 208.28, 0),
        "C1": (205.74, 215.9, 0), "C2": (226.06, 215.9, 0), "C3": (246.38, 215.9, 0),
        "H1": (320.04, 205.74, 0), "H2": (320.04, 220.98, 0),
    }
    for ref, (lib_id, value, footprint, pin_nets, _) in design.PARTS.items():
        x, y, rot = layout[ref]
        sym_fields = {"ref_at": (x - 10.16, y - 35.56), "val_at": (x + 7.62, y - 35.56)} if ref == "J1" else None
        if ref == "J2":
            sym_fields = {"ref_at": (x + 1.27, y - 29.21), "val_at": (x + 1.27, y + 29.21)}
        sym = sheet.place(lib_id, ref, value, footprint, x, y, rot, fields=sym_fields)
        full = {p.number: pin_nets.get(p.number) for p in pins_of(sym)}
        tied = ()
        if ref == "J1":
            # A3-A10, grounded for True IDE mode, share one ground symbol.
            tied = tuple(str(p) for p, name, _, _ in design.CF_PINS if name in ("A3", "A4", "A5", "A6", "A7", "A8", "A9", "A10"))
            sheet.tie(sym, x, y, rot, tied, "GND")
        sheet.connect_pins(sym, x, y, rot, full, skip=tied)
    # Power flags: the supplies arrive through wire pads.
    for i, net in enumerate(("+3V3", "+5V", "GND", "VCARD")):
        x, y = 50.8 + 17.78 * i, 248.92
        sheet.place("power:PWR_FLAG", f"#FLG{i + 1:02d}", "PWR_FLAG", "", x, y, 0, hidden_ref=True,
                    fields={"ref_at": (x, y - 6), "val_at": (x, y - 4)})
        tip = (x, y + STUB)
        sheet.wire((x, y), tip)
        if net == "VCARD":
            sheet.label(net, tip, (0, 1))
        else:
            sheet.power(net, tip, (0, 1))
    sheet.text("J2, the wire pads: each takes a wire from the CN503 leg of the same name\n"
               "(odd pins = A1-A21, even pins = B1-B21). A17 (CD audio emphasis) and B2 stay unused;\n"
               "CN503's A22-A25 and B22-B25 (CD audio, +12 V) never come to this board.", 213.36, 176.53)
    sheet.text("JP1: the card runs from 3.3 V as made (pads 1-2 bridged).\n"
               "For 5 V, cut the trace between 1 and 2 and bridge 2-3.\n"
               "JP2 open: the card is the slave (needed beside the GD-ROM drive).", 45.72, 185.42)
    sheet.text("True IDE mode: ATA SEL (pin 9) and A3-A10 grounded, WE (pin 36) held high.\n"
               "ACT lights while the card asserts DASP (busy, or slave present at start-up).", 45.72, 232.41)
    sheet.sch.filePath = str(HERE / "cf-board.kicad_sch")
    sheet.sch.to_file()
    print("wrote", sheet.sch.filePath)


if __name__ == "__main__":
    main()
