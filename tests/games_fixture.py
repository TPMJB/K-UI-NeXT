#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Original, non-executable GDI/IP.BIN/ISO9660 test data; no retail game bytes."""
from pathlib import Path
import struct

RAW = 2352
DATA = 2048
SESSION = 45000


def dual16(data, offset, value):
    struct.pack_into("<H", data, offset, value)
    struct.pack_into(">H", data, offset + 2, value)


def dual32(data, offset, value):
    struct.pack_into("<I", data, offset, value)
    struct.pack_into(">I", data, offset + 4, value)


def record(name, extent, size, directory=False):
    n = 33 + len(name) + (len(name) % 2 == 0)
    data = bytearray(n)
    data[0] = n
    dual32(data, 2, extent)
    dual32(data, 10, size)
    data[25] = 2 if directory else 0
    dual16(data, 28, 1)
    data[32] = len(name)
    data[33:33 + len(name)] = name
    return data


def raw_sector(lba, payload):
    assert len(payload) == DATA
    sector = bytearray(RAW)
    sector[:12] = b"\0" + b"\xff" * 10 + b"\0"
    fad = lba + 150
    minute, remainder = divmod(fad, 75 * 60)
    second, frame = divmod(remainder, 75)
    sector[12:15] = bytes((x // 10 * 16 + x % 10) for x in (minute, second, frame))
    sector[15] = 1
    sector[16:2064] = payload
    # The reader checks sector framing, not integrity. Zero EDC/ECC deliberately
    # demonstrates that inspection cannot claim a full image verification.
    return sector


def make_fixture(folder):
    folder = Path(folder)
    folder.mkdir(parents=True, exist_ok=True)
    sectors = [bytearray(DATA) for _ in range(64)]
    ip = sectors[0]
    ip[:256] = b" " * 256
    ip[:16] = b"SEGA SEGAKATANA "
    ip[16:32] = b"SEGA ENTERPRISES"
    ip[32:48] = b"0000 GD-ROM1/1  "
    ip[48:56] = b"JUE     "
    ip[56:64] = b"0000000 "
    ip[64:74] = b"KUI-TEST  "
    ip[74:80] = b"V1.000"
    ip[80:96] = b"20260924        "
    ip[96:112] = b"1ST_READ.BIN    "
    ip[112:128] = b"K-UI TEST       "
    title = b"Independent Games Fixture"
    ip[128:128 + len(title)] = title
    pvd = sectors[16]
    pvd[:7] = b"\x01CD001\x01"
    pvd[8:40] = b"KUI TEST".ljust(32)
    pvd[40:72] = b"ORIGINAL SYNTHETIC IMAGE".ljust(32)
    dual32(pvd, 80, SESSION + len(sectors))
    dual16(pvd, 120, 1)
    dual16(pvd, 124, 1)
    dual16(pvd, 128, DATA)
    pvd[156:190] = record(b"\0", SESSION + 20, DATA, True)
    pvd[881] = 1
    sectors[17][:7] = b"\xffCD001\x01"
    entries = (record(b"\0", SESSION + 20, DATA, True),
               record(b"\1", SESSION + 20, DATA, True),
               record(b"1ST_READ.BIN;1", SESSION + 21, DATA * 2))
    offset = 0
    for entry in entries:
        sectors[20][offset:offset + len(entry)] = entry
        offset += len(entry)
    sectors[21][:] = bytes((i * 17 + 3) & 255 for i in range(DATA))
    sectors[22][:] = bytes((i * 29 + 7) & 255 for i in range(DATA))
    (folder / "track01.bin").write_bytes(b"".join(raw_sector(i, bytearray(DATA)) for i in range(4)))
    (folder / "track02.raw").write_bytes(bytes((i * 11 + 5) & 255 for i in range(4 * RAW)))
    (folder / "track03.bin").write_bytes(b"".join(raw_sector(SESSION + i, sector) for i, sector in enumerate(sectors)))
    (folder / "disc.gdi").write_text(
        "3\n1 0 4 2352 track01.bin 0\n2 4 0 2352 track02.raw 0\n3 45000 4 2352 track03.bin 0\n",
        encoding="ascii")


if __name__ == "__main__":
    import sys
    make_fixture(sys.argv[1])
