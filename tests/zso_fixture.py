#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Original cooked ISO bytes and independently emitted standard LZ4 ZISO v1.

No game payloads or production codec helpers are used. The small greedy encoder
emits overlapping matches and the required final literal sequence; an independent
Python decoder checks every emitted block against its source before publication.
"""
from pathlib import Path
import shutil
import struct
import zlib
from game_format_fixture import payloads
from games_fixture import dual32

ZSO_CASES = (
    "zso-gd-shift0", "zso-cd11700-shift9", "zso-cd0-shift0",
    "zso-fragments31", "zso-fragments32",
    "zso-index-underflow", "zso-index-descending", "zso-index-past-end",
    "zso-payload-invalid", "zso-header-version", "zso-header-blocksize",
    "zso-header-reserved", "zso-header-shift", "zso-windows-ce",
    "zso-cdda", "zso-background", "zso-patches", "zso-scif",
    "zso-source-header-change", "zso-source-index-change",
)
ZSO_PREPARED = (
    "zso-gd-shift0", "zso-cd11700-shift9", "zso-cd0-shift0", "zso-fragments31",
    "zso-source-header-change", "zso-source-index-change",
)


def length_bytes(value):
    out = bytearray()
    while value >= 255:
        out.append(255)
        value -= 255
    out.append(value)
    return out


def lz4_block(source):
    """Independent block emitter: last five bytes are always literals."""
    out, last = bytearray(), {}
    anchor = at = 0
    while at <= len(source) - 12:
        key = source[at:at + 4]
        previous = last.get(key)
        last[key] = at
        if previous is None:
            at += 1
            continue
        match = 4
        while at + match < len(source) - 5 and source[previous + match] == source[at + match]:
            match += 1
        literals = at - anchor
        out.append((min(literals, 15) << 4) | min(match - 4, 15))
        if literals >= 15:
            out.extend(length_bytes(literals - 15))
        out.extend(source[anchor:at])
        out.extend(struct.pack("<H", at - previous))
        if match - 4 >= 15:
            out.extend(length_bytes(match - 19))
        at += match
        anchor = at
    literals = len(source) - anchor
    out.append(min(literals, 15) << 4)
    if literals >= 15:
        out.extend(length_bytes(literals - 15))
    out.extend(source[anchor:])
    return bytes(out)


def decode_emitted(data):
    out, at = bytearray(), 0
    while at < len(data):
        token, at = data[at], at + 1
        literal = token >> 4
        if literal == 15:
            while True:
                byte, at = data[at], at + 1
                literal += byte
                if byte != 255:
                    break
        out.extend(data[at:at + literal])
        at += literal
        if at == len(data):
            break
        distance, = struct.unpack_from("<H", data, at)
        at += 2
        assert 0 < distance <= len(out)
        match = (token & 15) + 4
        if token & 15 == 15:
            while True:
                byte, at = data[at], at + 1
                match += byte
                if byte != 255:
                    break
        for _ in range(match):
            out.append(out[-distance])
    assert at == len(data) and len(out) == 2048
    return bytes(out)


def ziso(logical, shift):
    count, align = len(logical) // 2048, 1 << shift
    first = (24 + (count + 1) * 4 + align - 1) // align * align
    body, index, raw_count = bytearray(first), [], 0
    for block in range(count):
        source = logical[block * 2048:(block + 1) * 2048]
        compressed = lz4_block(source)
        assert decode_emitted(compressed) == source
        raw = block % 3 == 1 or len(compressed) >= 2048
        raw_count += raw
        index.append((len(body) >> shift) | (0x80000000 if raw else 0))
        body.extend(source if raw else compressed)
        body.extend(bytes((-len(body)) % align))
    index.append(len(body) >> shift)
    assert 0 < raw_count < count
    struct.pack_into("<4sIQIBB2x", body, 0, b"ZISO", 24, len(logical), 2048, 1, shift)
    struct.pack_into(f"<{len(index)}I", body, 24, *index)
    return body, index


def make_zso_fixture(folder, case):
    folder = Path(folder)
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    session = 11700 if "11700" in case else 0 if "cd0" in case else 45000
    sectors = payloads(folder, session, session != 45000)
    # Enough physical clusters on both filesystems to test 31 and 32 extents.
    if "fragments" in case:
        sectors.extend(bytearray(bytes((n * 7 + i // 8) & 255 for i in range(2048)))
                       for n in range(64, 256))
        dual32(sectors[16], 80, len(sectors))
    if case == "zso-windows-ce":
        sectors[0][62] = ord("1")
    logical = b"".join(sectors)
    shift = 9 if "shift9" in case or "fragments" in case else 0
    data, index = ziso(logical, shift)
    if case == "zso-index-underflow":
        struct.pack_into("<I", data, 24, 0)
    if case == "zso-index-descending":
        struct.pack_into("<I", data, 24 + 32 * 4, index[31])
    if case == "zso-index-past-end":
        struct.pack_into("<I", data, 24 + len(sectors) * 4, (len(data) >> shift) + 1)
    if case == "zso-payload-invalid":
        offset = (index[0] & 0x7fffffff) << shift
        assert not index[0] & 0x80000000
        data[offset:offset + 3] = bytes(3)  # Zero-distance match before any output.
    if case == "zso-header-version":
        data[20] = 2
    if case == "zso-header-blocksize":
        struct.pack_into("<I", data, 16, 4096)
    if case == "zso-header-reserved":
        data[22] = 1
    if case == "zso-header-shift":
        data[21] = 10
    (folder / "selected.zso").write_bytes(data)
    (folder / "zso-expected.iso").write_bytes(logical)
    expected_extents = 31 if case == "zso-fragments31" else 0
    (folder / "zso.expect").write_text(
        f"{int(case in ZSO_PREPARED)} {session} 3001 {len(sectors)} {len(data)} {shift} {expected_extents}\n"
        f"{zlib.crc32(logical[:32768]):08x} {zlib.crc32(logical[21 * 2048:21 * 2048 + 3001]):08x}\n",
        encoding="ascii")
