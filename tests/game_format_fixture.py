#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generated, non-executable CD/GD layouts for detached launch-map tests.

Expected physical sectors are saved separately from the selected container:
the C tests therefore do not use the parser under test to obtain expected data.
Only files named in format.files are copied into the simulated card.
"""
from pathlib import Path
import shutil
import struct
from games_fixture import DATA, RAW, dual32, make_fixture, raw_sector


def payloads(folder, session, cd):
    make_fixture(folder)
    raw = (folder / "track03.bin").read_bytes()
    sectors = [bytearray(raw[n + 16:n + 2064]) for n in range(0, len(raw), RAW)]
    if cd:
        sectors[0][32:48] = b"0000 CD-ROM1/1  "
    dual32(sectors[16], 80, len(sectors))
    dual32(sectors[16], 156 + 2, session + 20)
    for offset, relative in ((0, 20), (34, 20), (68, 21)):
        dual32(sectors[20], offset + 2, session + relative)
    # A non-sector-sized executable checks the detached CRC's exact tail.
    dual32(sectors[20], 68 + 10, 3001)
    return sectors


def mode2_sector(lba, payload):
    raw = raw_sector(lba, payload)
    raw[15] = 2
    raw[16:24] = bytes((1, 2, 8, 0, 1, 2, 8, 0))
    raw[24:2072] = payload
    return bytes(raw)


def write_expect(folder, selected, valid, session, flags, tracks, card_files):
    # Tracks: disc LBA, control, stride, mode, header offset, backing offset,
    # then their independent expected byte span.
    text = f"{selected}\n{int(valid)} {session} {session + 21} 3001 {flags} {len(tracks)}\n"
    for number, (lba, control, stride, mode, header, offset, data) in enumerate(tracks, 1):
        assert len(data) % stride == 0
        text += f"{lba} {len(data) // stride} {control} {stride} {mode} {header} {offset}\n"
        (folder / f"expected-track-{number:02d}.bin").write_bytes(data)
    (folder / "format.expect").write_text(text, encoding="ascii")
    (folder / "format.files").write_text("\n".join(card_files) + "\n", encoding="ascii")


def make_format_fixture(folder, case):
    folder = Path(folder)
    if folder.exists():
        shutil.rmtree(folder)
    folder.mkdir(parents=True)
    if case.startswith("format-gdi-offset-"):
        session = 45000
        sectors = payloads(folder, session, False)
        cooked = case.endswith("cooked")
        data = b"".join(sectors) if cooked else b"".join(raw_sector(session + n, p)
                                                       for n, p in enumerate(sectors))
        stride = DATA if cooked else RAW
        selected, backing, offset = "selected.gdi", "selected-track.bin", 511
        (folder / selected).write_text(f"1\n1 {session} 4 {stride} {backing} {offset}\n", encoding="ascii")
        (folder / backing).write_bytes(bytes([0xD9]) * offset + data)
        write_expect(folder, selected, True, session, 4 if cooked else 0,
                     [(session, 4, stride, 1, 0 if cooked else 16, offset, data)], [selected, backing])
        return
    if case.startswith("format-iso-"):
        session = 45000 if "gd" in case else 11700 if "11700" in case else 0
        cd = session != 45000
        sectors = payloads(folder, session, cd)
        data = b"".join(sectors)
        valid = "bad" not in case
        if case.endswith("bad-extent"):
            sectors[20][68 + 2:68 + 10] = b"\xff" * 8
            data = b"".join(sectors)
        if case.endswith("bad-size"):
            data = data[:-1]
        selected = "selected.iso"
        (folder / selected).write_bytes(data)
        write_expect(folder, selected, valid, session, 5 if cd else 4,
                     [(session, 4, DATA, 1, 0, 0, b"".join(sectors))], [selected])
        return
    if case.startswith("format-raw-"):
        session = 11700
        sectors = payloads(folder, session, True)
        mode2 = "mode2" in case
        data = b"".join((mode2_sector if mode2 else raw_sector)(session + n, p)
                        for n, p in enumerate(sectors))
        selected = "selected.img" if "img" in case else "selected.bin"
        (folder / selected).write_bytes(data)
        write_expect(folder, selected, True, session, 5,
                     [(session, 4, RAW, 2 if mode2 else 1, 24 if mode2 else 16, 0, data)], [selected])
        return
    if case.startswith("format-cue-"):
        gd_density = "gd-density" in case
        session = 45000 if gd_density else 11700
        sectors = payloads(folder, session, not gd_density)
        audio = bytes((n * 13 + 9) & 255 for n in range(4 * RAW))
        stride = 2336 if "2336" in case else 2448 if "2448" in case else RAW
        raw_data = b"".join(mode2_sector(session + n, p) for n, p in enumerate(sectors))
        if stride == 2336:
            raw_data = b"".join(raw_data[n + 16:n + RAW] for n in range(0, len(raw_data), RAW))
        elif stride == 2448:
            raw_data = b"".join(raw_data[n:n + RAW] + bytes((n // RAW + 83) & 255 for _ in range(96))
                                for n in range(0, len(raw_data), RAW))
        shared = stride == RAW and not gd_density
        selected = "selected.cue"
        if shared:
            backing = "shared tracks.bin"
            text = f'FILE "{backing}" BINARY\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n' \
                   ' TRACK 02 MODE2/2352\n  PREGAP 02:35:71\n  INDEX 01 00:00:04\n'
            (folder / backing).write_bytes(audio + raw_data)
            files = [selected, backing]
            offset = len(audio)
        else:
            text = 'FILE "audio.bin" BINARY\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n' \
                   f'FILE "data.bin" BINARY\n TRACK 02 MODE2/{stride}\n  PREGAP 02:35:71\n  INDEX 01 00:00:00\n'
            (folder / "audio.bin").write_bytes(audio)
            (folder / "data.bin").write_bytes(raw_data)
            files = [selected, "audio.bin", "data.bin"]
            offset = 0
            if gd_density:
                text = 'REM SINGLE-DENSITY AREA\nFILE "audio.bin" BINARY\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n' \
                       f'REM HIGH-DENSITY AREA\nFILE "data.bin" BINARY\n TRACK 02 MODE2/{stride}\n  INDEX 01 00:00:00\n'
        valid = "bad" not in case
        if case.endswith("bad-offset"):
            text = text.replace("00:00:04", "00:01:04")
        if case.endswith("bad-form2"):
            # Both copies mark the Form 2 bit; a 2048 read must reject it.
            path = folder / files[-1]
            bytes_ = bytearray(path.read_bytes())
            at = offset + (2 if stride == 2336 else 18)
            bytes_[at] |= 32
            bytes_[at + 4] |= 32
            path.write_bytes(bytes_)
        scramble_marker = "scrambled" in case
        if scramble_marker:
            text = "REM KUI SCRAMBLED 1\n" + text
        (folder / selected).write_text(text, encoding="ascii")
        flags = 4 if gd_density else 7 if scramble_marker else 5
        if "force-plain" in case:
            flags &= ~2
        write_expect(folder, selected, valid, session, flags,
                     [(0, 0, RAW, 0, 16, 0, audio),
                      (session, 4, stride, 2, 8 if stride == 2336 else 24, offset, raw_data)], files)
        return
    if case.startswith("format-cdi-"):
        version = 0x80000004 if "v2" in case else 0x80000005 if "v3-" in case else 0x80000006
        session = 11700
        sectors = payloads(folder, session, True)
        stride = 2336 if "2336" in case else 2448 if "2448" in case else RAW
        audio = bytes((n * 19 + 29) & 255 for n in range(4 * RAW))
        raw = b"".join(mode2_sector(session + n, p) for n, p in enumerate(sectors))
        if stride == 2336:
            data = b"".join(raw[n + 16:n + RAW] for n in range(0, len(raw), RAW))
        elif stride == 2448:
            data = b"".join(raw[n:n + RAW] + bytes((n // RAW + 17) & 255 for _ in range(96))
                            for n in range(0, len(raw), RAW))
        else:
            data = raw
        pregap_count = 150 if "pregap150" in case else 3
        audio_pregap_count = 150 if "pregap150" in case else 0
        audio_pregap = bytes([0xCB]) * (audio_pregap_count * RAW)
        pregap = bytes([0xD3]) * (pregap_count * stride)
        payload = audio_pregap + audio + pregap + data
        marker = bytes((0, 0, 1, 0, 0, 0, 255, 255, 255, 255)) * 2

        def track_header(start, count, mode, stride_, pregap_count=0):
            fields = bytearray(87)
            code = {2048: 0, 2336: 1, 2352: 2, 2448: 4}[stride_]
            struct.pack_into("<II", fields, 0, pregap_count, count)
            struct.pack_into("<I", fields, 14, mode)
            struct.pack_into("<II", fields, 30, start + 150 - pregap_count, pregap_count + count)
            struct.pack_into("<I", fields, 54, code)
            # The optional filename is metadata, never a filesystem path.
            name = b"independent generated track"
            header = struct.pack("<I", 0) + marker + bytes(4) + bytes([len(name)]) + name \
                + bytes(19) + struct.pack("<I", 0) + bytes(2) + fields
            return header + (bytes(9) if version != 0x80000004 else b"")

        trailer = bytes(12 if version == 0x80000004 else 13)
        footer = struct.pack("<HH", 2, 1) + track_header(0, 4, 0, RAW, audio_pregap_count) + trailer \
            + struct.pack("<H", 1) + track_header(session, 64, 2, stride, pregap_count) + trailer
        header_location = len(payload) if version != 0x80000006 else len(footer) + 8
        valid = "bad" not in case
        if case.endswith("bad-footer"):
            header_location = len(payload) + len(footer) + 9
        if case.endswith("bad-payload"):
            payload = payload[:-stride]
            header_location = len(payload) if version != 0x80000006 else len(footer) + 8
        selected = "selected.cdi"
        (folder / selected).write_bytes(payload + footer + struct.pack("<II", version, header_location))
        write_expect(folder, selected, valid, session, 7 if "force-scramble" in case else 5,
                     [(0, 0, RAW, 0, 16, len(audio_pregap), audio),
                      (session, 4, stride, 2, 8 if stride == 2336 else 24,
                       len(audio_pregap) + len(audio) + len(pregap), data)],
                     [selected])
        return
    raise ValueError(case)
