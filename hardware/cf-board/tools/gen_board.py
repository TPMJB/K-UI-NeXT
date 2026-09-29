# SPDX-License-Identifier: GPL-3.0-only
"""Builds cf-board.kicad_pcb from the schematic's netlist, with KiCad 7's
Python module (run with the system python3 that KiCad installs):

  gen_board.py place NETLIST DSN      placement, outline, labels; writes the
                                      board and a Specctra file for Freerouting
  gen_board.py route NETLIST SES      the placed board again, plus Freerouting's
                                      tracks and vias, ground pours, and DRC

The board: the CompactFlash socket on top with the card lying over the board
(it slides in from the right-hand edge), the wire pads in two columns along
the left edge, and the small parts on the underside, under the card."""
import re
import sys
from pathlib import Path

import pcbnew
from pcbnew import FromMM as mm

HERE = Path(__file__).resolve().parent.parent
BOARD = HERE / "cf-board.kicad_pcb"
FP = "/usr/share/kicad/footprints"
OX, OY = 50.0, 50.0          # where the board sits on the page
W, H = 66.5, 54.0            # board size, mm
X0, YC = 25.1, 27.0          # the CF socket's origin
J2X, J2Y = 6.0, 7.0          # the wire pads' pin 1
TRACK, CLEAR, VIA, DRILL = 0.25, 0.2, 0.6, 0.3
KEEPOUT = 2.4                # radius kept clear of tracks and vias at the mounting holes

# ref: (x, y, rotation, side)
PLACE = {
    "J1": (X0, YC, 0, "F"),
    "J2": (J2X, J2Y, 0, "F"),
    "H1": (2.8, 2.8, 0, "F"),
    "H2": (2.8, H - 2.8, 0, "F"),
    "JP1": (12.0, 3.4, 0, "B"),
    "R1": (29.0, 12.0, 0, "B"),
    "D1": (29.0, 16.0, 180, "B"),
    "R2": (29.0, 20.0, 0, "B"),
    "JP2": (29.0, 24.0, 0, "B"),
    "C2": (29.0, 28.0, 0, "B"),
    "C3": (29.0, 32.0, 0, "B"),
    "C1": (29.0, 36.0, 0, "B"),
}


def P(x, y):
    return pcbnew.VECTOR2I(mm(OX + x), mm(OY + y))


def read_netlist(path):
    text = Path(path).read_text()
    comps = {}
    for m in re.finditer(r'\(comp \(ref "([^"]+)"\)(.*?)\(tstamps "([^"]+)"\)\)', text, re.S):
        body = m.group(2)
        value = re.search(r'\(value "([^"]*)"\)', body).group(1)
        footprint = re.search(r'\(footprint "([^"]*)"\)', body).group(1)
        comps[m.group(1)] = (value, footprint, m.group(3))
    pads = {}
    for m in re.finditer(r'\(net \(code "?\d+"?\) \(name "((?:[^"\\]|\\.)*)"\)(.*?)\)\s*(?=\(net \(code|\)\s*\)\s*$)', text, re.S):
        name = m.group(1)
        if name.startswith("unconnected-"):
            continue
        for ref, pin in re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', m.group(2)):
            pads[(ref, pin)] = name
    return comps, pads


def silk_text(board, text, x, y, size=0.8, layer=pcbnew.F_SilkS, justify=0, angle=0, bold=False):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(text)
    t.SetLayer(layer)
    t.SetTextSize(pcbnew.VECTOR2I(mm(size), mm(size)))
    t.SetTextThickness(mm(max(0.12, size * 0.15)))
    t.SetBold(bold)
    t.SetPosition(P(x, y))
    t.SetTextAngleDegrees(angle)
    t.SetHorizJustify({-1: pcbnew.GR_TEXT_H_ALIGN_LEFT, 0: pcbnew.GR_TEXT_H_ALIGN_CENTER,
                       1: pcbnew.GR_TEXT_H_ALIGN_RIGHT}[justify])
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)


def silk_line(board, x1, y1, x2, y2, layer=pcbnew.F_SilkS, width=0.15):
    s = pcbnew.PCB_SHAPE(board)
    s.SetShape(pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(P(x1, y1))
    s.SetEnd(P(x2, y2))
    s.SetLayer(layer)
    s.SetWidth(mm(width))
    board.Add(s)


def build(netlist):
    comps, pad_nets = read_netlist(netlist)
    board = pcbnew.BOARD()
    ds = board.GetDesignSettings()
    ds.SetBoardThickness(mm(1.6))
    ds.m_TrackMinWidth = mm(0.15)
    ds.m_MinClearance = mm(0.15)
    ds.m_ViasMinSize = mm(0.6)
    ds.m_MinThroughDrill = mm(0.3)
    ds.m_ViasMinAnnularWidth = mm(0.13)
    ds.m_CopperEdgeClearance = mm(0.4)
    ds.m_HoleToHoleMin = mm(0.5)
    ds.m_HoleClearance = mm(0.25)
    ds.m_SilkClearance = mm(0)
    # The socket's 0.635 mm pads can only take one thermal spoke from a pour.
    ds.m_MinResolvedSpokes = 1
    nc = ds.m_NetSettings.m_DefaultNetClass
    nc.SetTrackWidth(mm(TRACK))
    nc.SetClearance(mm(CLEAR))
    nc.SetViaDiameter(mm(VIA))
    nc.SetViaDrill(mm(DRILL))

    nets = {}

    def net(name):
        if name not in nets:
            n = pcbnew.NETINFO_ITEM(board, name)
            board.Add(n)
            nets[name] = n
        return nets[name]

    for ref, (value, fpid, stamp) in comps.items():
        lib, name = fpid.split(":")
        path = HERE / f"{lib}.pretty" if lib == "cf-board" else Path(FP) / f"{lib}.pretty"
        fp = pcbnew.FootprintLoad(str(path), name)
        fp.SetFPID(pcbnew.LIB_ID(lib, name))
        fp.SetReference(ref)
        fp.SetValue(value)
        fp.SetPath(pcbnew.KIID_PATH(f"/{stamp}"))
        board.Add(fp)
        x, y, rot, side = PLACE[ref]
        fp.SetPosition(P(x, y))
        if side == "B":
            fp.Flip(fp.GetPosition(), False)
        fp.SetOrientationDegrees(rot)
        for pad in fp.Pads():
            n = pad_nets.get((ref, pad.GetNumber()))
            if n:
                pad.SetNet(net(n))
        # Small parts: reference only, on their own side's silkscreen.
        fp.Value().SetVisible(False)
        if ref in ("H1", "H2", "J1", "J2"):
            fp.Reference().SetVisible(False)
        else:
            fp.Reference().SetTextSize(pcbnew.VECTOR2I(mm(0.8), mm(0.8)))
            # Beside the part, clear of its neighbours above and below.
            fp.Reference().SetPosition(P(x + 2.4, y))
            # Underside text is mirrored: "right" grows away from the part there.
            fp.Reference().SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_RIGHT if side == "B"
                                           else pcbnew.GR_TEXT_H_ALIGN_LEFT)
            fp.Reference().SetTextAngleDegrees(0)
            fp.Reference().SetTextThickness(mm(0.12))

    # No tracks or vias under a screw head or washer at the mounting holes.
    import math
    for ref in ("H1", "H2"):
        hx, hy, _, _ = PLACE[ref]
        area = pcbnew.ZONE(board)
        area.SetIsRuleArea(True)
        area.SetDoNotAllowTracks(True)
        area.SetDoNotAllowVias(True)
        area.SetDoNotAllowPads(False)
        area.SetDoNotAllowCopperPour(False)
        area.SetDoNotAllowFootprints(False)
        layers = pcbnew.LSET()
        layers.AddLayer(pcbnew.F_Cu)
        layers.AddLayer(pcbnew.B_Cu)
        area.SetLayerSet(layers)
        area.SetZoneName(f"{ref} keep-out")
        outline = area.Outline()
        outline.NewOutline()
        for i in range(24):
            a = 2 * math.pi * i / 24
            outline.Append(mm(OX + hx + KEEPOUT * math.cos(a)), mm(OY + hy + KEEPOUT * math.sin(a)))
        board.Add(area)

    # Outline.
    edge = pcbnew.PCB_SHAPE(board)
    edge.SetShape(pcbnew.SHAPE_T_RECT)
    edge.SetStart(P(0, 0))
    edge.SetEnd(P(W, H))
    edge.SetLayer(pcbnew.Edge_Cuts)
    edge.SetWidth(mm(0.1))
    board.Add(edge)

    # The wire pads: each labelled with its CN503 leg on top, its signal underneath.
    for k in range(1, 22):
        y = J2Y + 2 * (k - 1)
        a, b = f"A{k}", f"B{k}"
        silk_text(board, "nc" if a == "A17" else a, J2X - 1.45, y, justify=1)
        silk_text(board, "nc" if b == "B2" else b, J2X + 3.45, y, justify=-1)
        for leg, px in ((a, J2X), (b, J2X + 2)):
            pin = 2 * k - 1 if leg[0] == "A" else 2 * k
            n = pad_nets.get(("J2", str(pin)), "")
            label = n.lstrip("/").replace("~{", "/").replace("}", "")
            if label:
                # Mirrored text on the underside: "left" grows outward from column A,
                # "right" outward from column B.
                a_side = leg[0] == "A"
                silk_text(board, label, px - 1.05 if a_side else px + 1.05, y, size=0.8, layer=pcbnew.B_SilkS,
                          justify=-1 if a_side else 1)
    silk_text(board, "CN503", J2X + 1, J2Y - 2.3, size=0.8)
    silk_text(board, "A", J2X - 2.4, J2Y - 2.3, size=0.8)
    silk_text(board, "B", J2X + 4.4, J2Y - 2.3, size=0.8)
    # Card direction and the board's name.
    silk_text(board, "K-UI CF  G1 slave", 45.0, 3.0, size=1.0, bold=True)
    silk_text(board, "rev 1  2026-09", 45.0, 51.2, size=0.8)
    silk_line(board, 58.0, 27.0, 64.0, 27.0)
    silk_line(board, 58.0, 27.0, 60.0, 25.8)
    silk_line(board, 58.0, 27.0, 60.0, 28.2)
    silk_text(board, "card in", 61.0, 29.2, size=0.8)
    # A 50 mm bar below the board on the drawing layer, to check that the
    # printed fit template came out at 1:1.
    bar_y = H + 6.0
    silk_line(board, 0, bar_y, 50, bar_y, layer=pcbnew.Dwgs_User, width=0.3)
    for tick in range(0, 51, 10):
        silk_line(board, tick, bar_y - (1.5 if tick % 50 == 0 else 0.8), tick, bar_y, layer=pcbnew.Dwgs_User, width=0.2)
    silk_text(board, "50 mm: measure this bar to check the print is 1:1", 0, bar_y + 2.5, size=1.5,
              layer=pcbnew.Dwgs_User, justify=-1)
    silk_text(board, "K-UI CF board: fit template (board 66.5 x 54 mm, card slides in from the right)", 0,
              -4.0, size=1.5, layer=pcbnew.Dwgs_User, justify=-1)
    # Underside notes.
    notes = [("JP1: 1-2 = 3.3 V (as made)", 0), ("cut 1-2, bridge 2-3 = 5 V", 1),
             ("JP2 open = slave (Dreamcast)", 2.3), ("wire each pad to the", 3.6),
             ("CN503 leg of the same name", 4.6), ("github.com/TPMJB/K-UI-NeXT", 6.2)]
    for text, dy in notes:
        silk_text(board, text, 50.0, 38.0 + dy * 1.25, size=0.8, layer=pcbnew.B_SilkS)
    silk_text(board, "ACT", 26.2, 16.0, size=0.8, layer=pcbnew.B_SilkS, justify=-1)
    return board


def export_dsn(board, dsn):
    ok = pcbnew.ExportSpecctraDSN(board, str(dsn))
    if not ok:
        raise SystemExit("Specctra export failed")


# ---- Freerouting's session file ----
def parse_ses(path):
    """Wires and vias per net from a Specctra session file."""
    text = Path(path).read_text()
    res = re.search(r'\(resolution (\w+) (\d+)\)', text)
    unit, per = res.group(1), int(res.group(2))
    scale = {"um": 0.001, "mm": 1.0, "mil": 0.0254, "inch": 25.4}[unit] / per
    out = {}
    body = text[text.index("(network_out"):]
    for m in re.finditer(r'\(net ("(?:[^"\\]|\\.)*"|\S+)(.*?)(?=\(net ("|\S)|\Z)', body, re.S):
        name = m.group(1).strip('"')
        chunk = m.group(2)
        wires = []
        for w in re.finditer(r'\(path (\S+) (\d+(?:\.\d+)?)((?:\s+-?\d+(?:\.\d+)?)+)\s*\)', chunk):
            layer, width = w.group(1), float(w.group(2)) * scale
            nums = [float(v) * scale for v in w.group(3).split()]
            pts = list(zip(nums[0::2], nums[1::2]))
            wires.append((layer, width, pts))
        vias = [(float(x) * scale, float(y) * scale) for x, y in
                re.findall(r'\(via \S+ (-?\d+(?:\.\d+)?) (-?\d+(?:\.\d+)?)', chunk)]
        out[name] = (wires, vias)
    return out


def add_routes(board, ses):
    nets = {n.GetNetname(): n for n in board.GetNetsByName().values()}
    layers = {"F.Cu": pcbnew.F_Cu, "B.Cu": pcbnew.B_Cu}
    count_t = count_v = 0
    for name, (wires, vias) in parse_ses(ses).items():
        net = nets.get(name)
        if net is None:
            raise SystemExit(f"session names an unknown net {name}")
        for layer, width, pts in wires:
            for (x1, y1), (x2, y2) in zip(pts, pts[1:]):
                t = pcbnew.PCB_TRACK(board)
                # Specctra's y axis points up: KiCad's export negates y.
                t.SetStart(pcbnew.VECTOR2I(mm(x1), mm(-y1)))
                t.SetEnd(pcbnew.VECTOR2I(mm(x2), mm(-y2)))
                t.SetWidth(mm(width))
                t.SetLayer(layers[layer])
                t.SetNet(net)
                board.Add(t)
                count_t += 1
        for x, y in vias:
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(mm(x), mm(-y)))
            v.SetWidth(mm(VIA))
            v.SetDrill(mm(DRILL))
            v.SetNet(net)
            board.Add(v)
            count_v += 1
    return count_t, count_v


def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = 0.0 if dx == dy == 0 else max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return ((px - ax - t * dx) ** 2 + (py - ay - t * dy) ** 2) ** 0.5


def add_stitching(board):
    """Ground vias across the card area, tying the top pour there to the
    underside pour; each only where it clears every track, pad and hole."""
    gnd = board.FindNet("GND")
    tracks = []
    for t in board.GetTracks():
        a, b = t.GetStart(), t.GetEnd()
        tracks.append((pcbnew.ToMM(a.x), pcbnew.ToMM(a.y), pcbnew.ToMM(b.x), pcbnew.ToMM(b.y),
                       pcbnew.ToMM(t.GetWidth()) / 2))
    pads = []
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            box = pad.GetBoundingBox()
            pads.append((pcbnew.ToMM(box.GetX()), pcbnew.ToMM(box.GetY()),
                         pcbnew.ToMM(box.GetRight()), pcbnew.ToMM(box.GetBottom())))
    placed = 0
    for x in (37.0, 45.0, 53.0, 61.0):
        for y in (9.0, 18.0, 27.0, 36.0, 45.0):
            cx, cy = OX + x, OY + y
            reach = VIA / 2 + CLEAR + 0.05
            if any(seg_dist(cx, cy, ax, ay, bx, by) < reach + w for ax, ay, bx, by, w in tracks):
                continue
            if any(l - reach < cx < r + reach and t - reach < cy < b + reach for l, t, r, b in pads):
                continue
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(mm(cx), mm(cy)))
            v.SetWidth(mm(VIA))
            v.SetDrill(mm(DRILL))
            v.SetNet(gnd)
            board.Add(v)
            placed += 1
    return placed


def add_ground_pours(board):
    gnd = board.FindNet("GND")
    for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(gnd)
        z.SetLocalClearance(mm(0.25))
        z.SetMinThickness(mm(0.2))
        z.SetThermalReliefGap(mm(0.25))
        z.SetThermalReliefSpokeWidth(mm(0.3))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
        outline = z.Outline()
        outline.NewOutline()
        inset = 0.5
        for x, y in ((inset, inset), (W - inset, inset), (W - inset, H - inset), (inset, H - inset)):
            outline.Append(mm(OX + x), mm(OY + y))
        board.Add(z)
    # Filled by drc.py: KiCad's zone filler needs a board loaded the usual way.


def main():
    mode, netlist = sys.argv[1], sys.argv[2]
    board = build(netlist)
    if mode == "place":
        pcbnew.SaveBoard(str(BOARD), board)
        export_dsn(board, sys.argv[3])
        print("placed; wrote", BOARD, "and", sys.argv[3])
    elif mode == "route":
        t, v = add_routes(board, sys.argv[3])
        v += add_stitching(board)
        add_ground_pours(board)
        pcbnew.SaveBoard(str(BOARD), board)
        print(f"routed: {t} track segments, {v} vias; wrote {BOARD}")
    else:
        raise SystemExit(__doc__)


if __name__ == "__main__":
    main()
