#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Regression for the exact 32-byte MIL-CD boot header and sector-spanning badge."""
from pathlib import Path
import sys
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from boot_badge import BADGE, verify_cdi_badge

badge = BADGE.read_bytes()
boot = bytearray(32768)
# Pinned mkdcdisc IPBin: hardware_id[16], maker_id[16], then CRC[4].
# Deliberately independent of the scanner constant: no space after ENTERPRISES.
boot[:36] = b"SEGA SEGAKATANA SEGA ENTERPRISES73F7"
boot[0x60:0x70] = b"1ST_READ.BIN    "
boot[0x3820:0x3820 + len(badge)] = badge
pvd = bytearray(2048)
pvd[:7] = b"\x01CD001\x01"
pvd[128:132] = b"\x00\x08\x08\x00"
# CDI Mode2 Form1: eight-byte subheader, 2048 user bytes, 280 EDC/ECC bytes.
sectors = [boot[i:i + 2048] for i in range(0, len(boot), 2048)] + [pvd]
image = b"".join(b"\0" * 8 + sector + b"\0" * 280 for sector in sectors)
result = verify_cdi_badge(image, badge)
assert result["cdi_bootstrap_offset"] == 8 and result["cdi_sector_bytes"] == 2336
bad = bytearray(image)
bad[8 + (0x3820 // 2048) * 2336 + 0x3820 % 2048] ^= 1
try:
    verify_cdi_badge(bad, badge)
except ValueError as error:
    assert "does not contain" in str(error)
else:
    raise AssertionError("Corrupt embedded badge was accepted")
print("PASS boot header and embedded badge regression")
