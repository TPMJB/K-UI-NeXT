# SPDX-License-Identifier: GPL-3.0-only
"""Checks KiCad's netlist against design.py: every net with exactly the pins
the design gives it, every other pin left open, no pin on two nets, and no
net with a single pin. Standing in for ERC, which KiCad 7's command line
lacks. Usage: check_netlist.py NETLIST (kicad-cli sch export netlist
--format kicadsexpr), or a .kicad_pcb to check the board's pads instead."""
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
import design  # noqa: E402


def norm(name):
    return name[1:] if name.startswith("/") else name


def from_netlist(text):
    nets = {}
    for m in re.finditer(r'\(net \(code "?\d+"?\) \(name "((?:[^"\\]|\\.)*)"\)(.*?)\)\s*(?=\(net \(code|\)\s*\)\s*$)', text, re.S):
        nodes = re.findall(r'\(node \(ref "([^"]+)"\) \(pin "([^"]+)"\)', m.group(2))
        nets[norm(m.group(1))] = sorted(nodes)
    return nets


def from_board(text):
    import pcbnew  # noqa: F401  (only for boards)
    board = pcbnew.LoadBoard(text)
    nets = {}
    for fp in board.GetFootprints():
        for pad in fp.Pads():
            name = pad.GetNetname()
            if name and pad.GetNumber():
                nets.setdefault(norm(name), []).append((fp.GetReference(), pad.GetNumber()))
    return {n: sorted(v) for n, v in nets.items()}


def main(path):
    got = from_board(path) if path.endswith(".kicad_pcb") else from_netlist(Path(path).read_text())
    # Single-pin nets KiCad makes for unconnected pins are named "unconnected-(...)".
    stray = {n: v for n, v in got.items() if n.startswith("unconnected-") or n.startswith("Net-(")}
    got = {n: v for n, v in got.items() if n not in stray}
    want = design.nets()
    problems = []
    for net in sorted(set(want) | set(got)):
        if want.get(net) != got.get(net):
            problems.append(f"{net}: design {want.get(net)} but KiCad {got.get(net)}")
    seen = {}
    for net, nodes in got.items():
        for node in nodes:
            if node in seen:
                problems.append(f"{node} is on {seen[node]} and {net}")
            seen[node] = net
        if len(nodes) < 2:
            problems.append(f"{net} has a single pin {nodes}")
    open_pins = {(ref, pin) for ref, (_, _, _, pins, _) in design.PARTS.items() for pin, net in pins.items() if not net}
    for net, nodes in stray.items():
        for node in nodes:
            if node not in open_pins:
                problems.append(f"{node} is unconnected in KiCad ({net}) but the design connects it")
    for p in problems:
        print("PROBLEM", p)
    total = sum(len(v) for v in got.values())
    print(f"{len(got)} nets, {total} pin connections, {len(problems)} problems")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1]))
