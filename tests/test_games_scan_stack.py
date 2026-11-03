#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Mutate actual GCC Scan reports: reject overflow, disagreement and recursion."""
from pathlib import Path
import shutil
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from check_games_scan_stack import SOURCES, WORKER, analyze, artifact, check, load


def main():
    directory = Path(sys.argv[1]).resolve()
    check(directory)
    graph = load(directory)
    with tempfile.TemporaryDirectory(prefix="kui-scan-stack-mutation-") as folder:
        copy = Path(folder)
        for source in SOURCES:
            for suffix in (".ci", ".su"):
                shutil.copyfile(artifact(directory, source, suffix), artifact(copy, source, suffix))
        old = graph[0][WORKER]
        usage = artifact(copy, SOURCES[0], ".su")
        text = usage.read_text()
        needle = f":worker\t{old}\tstatic"
        assert text.count(needle) == 1
        usage.write_text(text.replace(needle, ":worker\t65536\tstatic"))
        try:
            load(copy)
        except ValueError as error:
            assert "GCC .ci/.su disagree" in str(error)
        else:
            raise AssertionError("Contradictory emitted stack evidence was admitted")
        callgraph = artifact(copy, SOURCES[0], ".ci")
        lines = callgraph.read_text().splitlines()
        changed = 0
        for index, line in enumerate(lines):
            if 'label: "worker\\n' in line and f"\\n{old} bytes (static)" in line:
                lines[index] = line.replace(f"\\n{old} bytes (static)", "\\n65536 bytes (static)")
                changed += 1
        assert changed == 1
        callgraph.write_text("\n".join(lines) + "\n")
        try:
            analyze(load(copy), 65536, 3)
        except ValueError as error:
            assert "exceeds worker budget" in str(error)
        else:
            raise AssertionError("Compiler-emitted worker overflow was admitted")
    graph[1].setdefault("kui_games_library_scan", set()).add("kui_games_library_scan")
    try:
        analyze(graph, 65536, 3)
    except ValueError as error:
        assert "Unbounded recursive path" in str(error)
    else:
        raise AssertionError("Unbounded Scan recursion was admitted")
    print("PASS SH-4 Scan stack evidence: actual reports pass; disagreement, emitted-frame overflow and recursion refused")


if __name__ == "__main__":
    main()
