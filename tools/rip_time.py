#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Turn a saved K-UI diagnostics report into projected whole-disc rip times.

Usage: python3 tools/rip_time.py REPORT [REPORT ...] [--bytes N]

Feed it any report: a `sections=capture` bench, a real capture, or both. It reads the rates the
console actually measured and prints how long a whole disc takes at each of them, so the choice
between settings is a number of minutes rather than a percentage.

What it reads, all optional:
  BENCH capture ... hash= end= sample= ... kib_s=   one rate per settings combination
  BENCH capture total ... total_kib_s=              the same including the end read-back
  TIMING capture wall_us= / disc us= write us= ...  a real capture: its rate and where time went
  BENCH hash ... crc16-* cyc_b= / sha256 / crc32    the microbenchmarks, for the "what if" lines
  Tn FAD [a,b) data|audio  or  Tnn ctrl=c start= next=   the disc's size

A projection is arithmetic on measured rates, not a prediction: it assumes the rest of the disc
behaves like the part that was measured. The inner radius is the slowest part of a disc, so a
projection from FAD 45150 is conservative. Lines marked PROJECTED were never run end to end.
"""
import argparse
import re
import sys

RAW = 2352
GD_ROM_BYTES = 1185760800 + 1825152 + 1237152   # a typical single-density-plus-GD layout, for reference


def read(paths):
    text = []
    for p in paths:
        raw = open(p, encoding="utf-8", errors="replace").read().split("\n")
        joined, i = [], 0
        while i < len(raw):                      # the on-screen log wraps at 76 characters
            line = raw[i]
            while len(line) == 76 and i + 1 < len(raw):
                i += 1
                line += raw[i]
            joined.append(line)
            i += 1
        text.append("\n".join(joined))
    return "\n".join(text)


def disc_bytes(text):
    """Total raw bytes of the disc, from the capture plan if present, else the TOC."""
    plan = re.findall(r"^T\d\d FAD \[(\d+),(\d+)\) (?:data|audio)", text, re.M)
    if plan:
        return sum((int(b) - int(a)) * RAW for a, b in plan), "the capture plan in this report"
    toc = re.findall(r"^\s*T(\d\d) ctrl=\w+ start=(\d+) next=(\d+)", text, re.M)
    if toc:
        seen, total = set(), 0
        for n, a, b in toc:
            if n in seen:
                continue
            seen.add(n)
            total += (int(b) - int(a)) * RAW
        return total, "the TOC in this report (no gap exclusions, so a little high)"
    return None, None


def settings_rates(text):
    """{(ui, hash, end, sample): (capture KiB/s, finished KiB/s or None)} averaged over repeats.

    Scanned in order, not by regex over the whole file: `BENCH capture total` carries no uihz=,
    so the only thing that says which pass it belongs to is the `BENCH pass ... uihz=` line above
    it. Keying the totals without that averaged every pass together and reported, for instance,
    the same finished time for the full-speed and 2 Hz passes.
    """
    cap, fin, ui = {}, {}, "?"
    for line in text.split("\n"):
        m = re.match(r"BENCH pass \d+/\d+ uihz=(\S+)", line)
        if m:
            ui = m[1]
            continue
        m = re.match(r"BENCH capture uihz=(\S+) hash=(\w+) end=(\w+) sample=(\d+) sectors=\d+ "
                     r"result=ok bytes=\d+ us=\d+ kib_s=([\d.]+)", line)
        if m:
            ui = m[1]
            cap.setdefault((ui, m[2], m[3], m[4]), []).append(float(m[5]))
            continue
        m = re.match(r"BENCH capture total hash=(\w+) end=(\w+) sample=(\d+) verify_us=\d+ "
                     r"total_kib_s=([\d.]+)", line)
        if m:
            fin.setdefault((ui, m[1], m[2], m[3]), []).append(float(m[4]))
    mean = lambda v: sum(v) / len(v)
    return {k: (mean(v), mean(fin[k]) if k in fin else None) for k, v in cap.items()}


def real_capture(text):
    """A real capture's rate and where its time went, if the report holds one."""
    m = re.search(r"TIMING capture wall_us=(\d+)\n((?:\w+ us=\d+ pct=[\d.]+[^\n]*\n)+)", text)
    if not m:
        return None
    wall = int(m[1])
    parts, total = {}, 0
    for line in m[2].strip().split("\n"):
        g = re.match(r"(\w+) us=(\d+) pct=([\d.]+)(?: bytes=(\d+))?", line)
        if g:
            parts[g[1]] = float(g[3])
            if g[1] == "disc" and g[4]:
                total = int(g[4])
    return dict(wall_us=wall, bytes=total, kib_s=total / 1024 / (wall / 1e6) if total and wall else 0, parts=parts)


def minutes(byte_count, kib_s):
    return byte_count / 1024 / kib_s / 60 if kib_s else float("inf")


def savings(text, parts):
    """Candidate changes, each as a percentage of capture time, from measurements in this report."""
    out = []
    cyc = {}
    for m in re.finditer(r"BENCH cpu (crc16-\w+) uihz=\S+ wall=(\d+) wk=(\d+)", text):
        cyc[m[1]] = int(m[3])
    if {"crc16-kos", "crc16-slice2"} <= set(cyc) and cyc["crc16-kos"]:
        # The SD write spends ~13.4% of its cycles in KOS's CRC16 (docs/evidence/sd-crc-read-cost).
        share = 1 - cyc["crc16-slice2"] / cyc["crc16-kos"]
        out.append(("a faster CRC16 inside the SD write (slice2)", parts.get("write", 0) * 0.134 * share))
    if parts.get("crc32"):
        # 22 arithmetic operations a byte today (nibble table) against 12 for a 256-entry table.
        out.append(("a byte-table CRC32 in place of the nibble table", parts["crc32"] * (1 - 12 / 22)))
    return [(name, pct) for name, pct in out if pct > 0.05]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("report", nargs="+")
    ap.add_argument("--bytes", type=int, help="disc size in bytes, if the report has no plan or TOC")
    args = ap.parse_args()
    text = read(args.report)

    size, where = disc_bytes(text)
    if args.bytes:
        size, where = args.bytes, "the --bytes you gave"
    if not size:
        size, where = GD_ROM_BYTES, "a typical GD-ROM (no plan or TOC in the report)"
    print(f"Disc: {size / 1e9:.2f} GB of raw sectors, from {where}.\n")

    real = real_capture(text)
    if real and real["bytes"]:
        print(f"MEASURED, this report's own capture: {real['kib_s']:.1f} KiB/s"
              f" over {real['bytes'] / 1e6:.0f} MB, {minutes(real['bytes'], real['kib_s']):.1f} min for that much.")
        if real["bytes"] < size * 0.9:
            print(f"  PROJECTED to the whole disc at the same rate: {minutes(size, real['kib_s']):.1f} min.")
        print("  where the time went: " + ", ".join(f"{k} {v}%" for k, v in real["parts"].items()) + "\n")

    rates = settings_rates(text)
    if rates:
        print("PROJECTED whole-disc time at each measured setting (capture / finished dump):")
        print(f"  {'ui':>5} {'hash':>6} {'read-back':>10} {'sample':>7} {'KiB/s':>8} {'capture':>9} {'finished':>9}")
        for (ui, h, e, s), (c, f) in sorted(rates.items(), key=lambda kv: -kv[1][0]):
            fin = f"{minutes(size, f):.1f} min" if f else "same"
            print(f"  {ui:>5} {h:>6} {e:>10} {s:>7} {c:>8.1f} {minutes(size, c):>6.1f} min {fin:>9}")
        print()

    if real:
        rest = savings(text, real["parts"])
        if rest:
            base = minutes(size, real["kib_s"])
            print("PROJECTED effect of changes that are measured but NOT yet built:")
            done = 0.0
            for name, pct in rest:
                done += pct
                print(f"  {name}: -{pct:.1f}% of capture time ({base * pct / 100:.1f} min of {base:.1f})")
            print(f"  together: {base * (1 - done / 100):.1f} min, from {base:.1f}.")
            print("  Arithmetic on measured rates. Nothing here has been run end to end.")


if __name__ == "__main__":
    main()
