#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Original cooked ISO bytes and independently emitted ZISO/CISO v1 fixtures.

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

CSO_CASES = (
    "cso-gd-shift0", "cso-cd11700-shift9", "cso-cd0-shift0",
    "cso-fragments31", "cso-fragments32", "cso-index-underflow",
    "cso-index-descending", "cso-index-past-end", "cso-payload-invalid",
    "cso-header-version", "cso-header-blocksize", "cso-header-reserved",
    "cso-header-shift", "cso-windows-ce", "cso-ce-reader", "cso-cdda",
    "cso-background", "cso-patches", "cso-scif", "cso-ide",
    "cso-source-header-change", "cso-source-index-change",
    "cso-wrong-magic", "cso-wrong-package", "cso-lzo-selection",
    "cso-zlib-wrapped",
)
CSO_PREPARED = (
    "cso-gd-shift0", "cso-cd11700-shift9", "cso-cd0-shift0", "cso-fragments31",
    "cso-source-header-change", "cso-source-index-change",
)
ZSO_CASES = (
    "zso-gd-shift0", "zso-cd11700-shift9", "zso-cd0-shift0",
    "zso-fragments31", "zso-fragments32",
    "zso-index-underflow", "zso-index-descending", "zso-index-past-end",
    "zso-payload-invalid", "zso-header-version", "zso-header-blocksize",
    "zso-header-reserved", "zso-header-shift", "zso-windows-ce",
    "zso-cdda", "zso-background", "zso-patches", "zso-scif",
    "zso-source-header-change", "zso-source-index-change",
    "zso-lzo-gd-shift0", "zso-lzo-cd11700-shift9", "zso-lzo-cd0-shift0",
    "zso-lzo-fragments31", "zso-lzo-fragments32", "zso-lzo-wrong-codec",
    "zso-lz4-as-lzo", "zso-lzo-wrong-package", "zso-lzo-payload-invalid",
) + CSO_CASES
ZSO_PREPARED = (
    "zso-gd-shift0", "zso-cd11700-shift9", "zso-cd0-shift0", "zso-fragments31",
    "zso-source-header-change", "zso-source-index-change",
    "zso-lzo-gd-shift0", "zso-lzo-cd11700-shift9", "zso-lzo-cd0-shift0", "zso-lzo-fragments31",
) + CSO_PREPARED


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


def lzo_block(source):
    """Independent LZO1X emitter: either a distance-one M3 or one literal run.

    Exact original bytes remain the oracle. The public miniLZO test suite
    separately cross-checks these token forms and general compressor output.
    """
    def extended(value):
        result = bytearray()
        while value > 255:
            result.append(0)
            value -= 255
        result.append(value)
        return result
    if len(set(source)) == 1:
        return bytes((18, source[0], 32)) + extended(2048 - 1 - 33) + bytes((0, 0, 17, 0, 0))
    return bytes((0,)) + extended(2048 - 18) + source + bytes((17, 0, 0))


def deflate_block(source):
    """Independent CISO emitter, using raw RFC 1951 blocks from Python zlib."""
    encoder = zlib.compressobj(level=6, wbits=-15)
    data = encoder.compress(source) + encoder.flush()
    decoder = zlib.decompressobj(wbits=-15)
    assert decoder.decompress(data) + decoder.flush() == source
    assert decoder.eof and not decoder.unused_data and not decoder.unconsumed_tail
    return data


def ziso(logical, shift, codec="lz4"):
    count, align = len(logical) // 2048, 1 << shift
    first = (24 + (count + 1) * 4 + align - 1) // align * align
    body, index, raw_count = bytearray(first), [], 0
    for block in range(count):
        source = logical[block * 2048:(block + 1) * 2048]
        compressed = (deflate_block(source) if codec == "deflate" else
                      lzo_block(source) if codec == "lzo" else lz4_block(source))
        if codec == "lz4":
            assert decode_emitted(compressed) == source
        raw = block % 3 == 1 or len(compressed) >= 2048
        raw_count += raw
        index.append((len(body) >> shift) | (0x80000000 if raw else 0))
        body.extend(source if raw else compressed)
        body.extend(bytes((-len(body)) % align))
    index.append(len(body) >> shift)
    assert 0 < raw_count < count
    struct.pack_into("<4sIQIBB2x", body, 0, b"CISO" if codec == "deflate" else b"ZISO",
                     24, len(logical), 2048, 1, shift)
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
    if case.endswith("-windows-ce"):
        sectors[0][62] = ord("1")
    logical = b"".join(sectors)
    shift = 9 if "shift9" in case or "fragments" in case else 0
    cso = case in CSO_CASES
    data, index = ziso(logical, shift, "deflate" if cso else "lzo" if "-lzo-" in case else "lz4")
    if case.endswith("-index-underflow"):
        struct.pack_into("<I", data, 24, 0)
    if case.endswith("-index-descending"):
        struct.pack_into("<I", data, 24 + 32 * 4, index[31])
    if case.endswith("-index-past-end"):
        struct.pack_into("<I", data, 24 + len(sectors) * 4, (len(data) >> shift) + 1)
    if case.endswith("-payload-invalid"):
        offset = (index[0] & 0x7fffffff) << shift
        data[offset:offset + 3] = b"\x07\0\0" if cso else bytes(3)
        if "-lzo-" in case:
            # The first metadata block may be raw; corrupt its IP signature.
            data[offset:offset + 16] = b"INVALID-IP-BLOCK!"
    if case.endswith("-header-version"):
        data[20] = 2
    if case.endswith("-header-blocksize"):
        struct.pack_into("<I", data, 16, 4096)
    if case.endswith("-header-reserved"):
        data[22] = 1
    if case.endswith("-header-shift"):
        data[21] = 10
    if case == "cso-wrong-magic":
        data[:4] = b"ZISO"
    if case == "cso-zlib-wrapped":
        # A CISO block must be raw RFC 1951, without a zlib RFC 1950 wrapper.
        offset = (index[0] & 0x7fffffff) << shift
        wrapped = zlib.compress(logical[:2048])
        data[offset:offset + len(wrapped)] = wrapped
    (folder / ("selected.cso" if cso else "selected.zso")).write_bytes(data)
    (folder / "zso-expected.iso").write_bytes(logical)
    expected_extents = 31 if case.endswith("fragments31") else 0
    (folder / "zso.expect").write_text(
        f"{int(case in ZSO_PREPARED)} {session} 3001 {len(sectors)} {len(data)} {shift} {expected_extents}\n"
        f"{zlib.crc32(logical[:32768]):08x} {zlib.crc32(logical[21 * 2048:21 * 2048 + 3001]):08x}\n",
        encoding="ascii")
