#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Record source state immediately after a successful link."""
import hashlib
import json
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
record = {
    "commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip(),
    "source_dirty": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=root)),
    "elf_sha256": hashlib.sha256((root / "build/kui-diagnostic.elf").read_bytes()).hexdigest(),
}
(root / "build/compile.json").write_text(json.dumps(record, indent=2) + "\n")
