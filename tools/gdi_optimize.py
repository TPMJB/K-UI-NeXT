#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a separate GDI copy with 2048-byte Mode 1 data tracks.

Raw data is framed/address-checked, not EDC/ECC or catalogue-verified. Audio is
copied byte-for-byte. This tool never edits the original dump.
"""
import argparse
import ctypes
import errno
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import sys
import tempfile
from typing import NamedTuple
import zlib

RAW_BYTES = 2352
DATA_BYTES = 2048
GDI_LIMIT = 32768
TRACK_LIMIT = 99
# Matches the console's extended MSF address limit (159:59:74).
LBA_LIMIT = 719999 - 150 + 1
COPY_BYTES = RAW_BYTES * 128
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"
RECORD = re.compile(
    r'[ \t]*(\d+)[ \t]+(\d+)[ \t]+(\d+)[ \t]+(\d+)[ \t]+'
    r'(?:"([^"\r\n]+)"|([^ \t"\r\n]+))[ \t]+(\d+)[ \t]*'
)


class Track(NamedTuple):
    number: int
    start_lba: int
    control: int
    sector_bytes: int
    name: str
    path: Path
    file_bytes: int
    sectors: int
    signature: tuple


def require(condition, message):
    if not condition:
        raise ValueError(message)


def file_signature(path):
    info = path.lstat()
    require(stat.S_ISREG(info.st_mode), f"Not a regular file: {path}")
    return (info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns, info.st_ctime_ns)


def safe_name(name):
    return (0 < len(name) < 128 and not name.startswith((".", " "))
            and not name.endswith((".", " "))
            and all(32 <= ord(c) <= 126 and c not in '\\/:*?"<>|' for c in name))


def load_gdi(path):
    """Validate the whole descriptor and all file sizes before creating output."""
    signature = file_signature(path)
    require(0 < signature[2] <= GDI_LIMIT, "GDI must be 1..32768 bytes")
    with path.open("rb") as source:
        descriptor = source.read(GDI_LIMIT + 1)
    require(len(descriptor) == signature[2], "GDI changed while reading")
    try:
        text = descriptor.decode("ascii")
    except UnicodeDecodeError as error:
        raise ValueError("GDI filenames and fields must be ASCII") from error
    lines = text.split("\n")
    if lines[-1] == "":
        lines.pop()
    lines = [line[:-1] if line.endswith("\r") else line for line in lines]
    require(lines and re.fullmatch(r"[ \t]*[0-9]+[ \t]*", lines[0]),
            "Invalid GDI track count")
    count = int(lines[0])
    require(1 <= count <= TRACK_LIMIT and len(lines) == count + 1,
            "GDI must contain exactly 1..99 consecutive tracks")
    tracks, names = [], set()
    for number, line in enumerate(lines[1:], 1):
        match = RECORD.fullmatch(line)
        require(match is not None, f"Track {number}: invalid GDI row")
        index, lba, control, size = (int(match[i]) for i in range(1, 5))
        name, offset = match[5] or match[6], int(match[7])
        require(index == number, f"Track {number}: track numbers must be consecutive")
        require(control in (0, 4) and offset == 0,
                f"Track {number}: only data/audio with zero file offsets are supported")
        require(size == RAW_BYTES or (size == DATA_BYTES and control == 4),
                f"Track {number}: data must be 2352/2048 bytes; audio must be 2352")
        require(safe_name(name), f"Track {number}: unsafe filename")
        require(name.casefold() not in names, f"Track {number}: duplicate filename")
        require(0 <= lba < LBA_LIMIT, f"Track {number}: LBA out of range")
        source_path = path.parent / name
        source_signature = file_signature(source_path)
        byte_count = source_signature[2]
        require(byte_count > 0 and byte_count % size == 0,
                f"Track {number}: file size is not a positive whole number of sectors")
        sectors = byte_count // size
        require(sectors <= LBA_LIMIT - lba, f"Track {number}: track exceeds LBA limit")
        require(lba >= 45000 or lba + sectors <= 45000,
                f"Track {number}: track crosses the low/high-density session boundary")
        if tracks:
            previous = tracks[-1]
            require(previous.start_lba + previous.sectors <= lba,
                    f"Track {number}: overlapping or out-of-order track")
        names.add(name.casefold())
        tracks.append(Track(number, lba, control, size, name, source_path,
                            byte_count, sectors, source_signature))
    return descriptor, signature, tracks


def digest(data):
    return {"bytes": len(data), "crc32": f"{zlib.crc32(data):08x}",
            "sha256": hashlib.sha256(data).hexdigest()}


def msf(lba):
    fad = lba + 150
    minute, remainder = divmod(fad, 4500)
    second, frame = divmod(remainder, 75)
    # GD-ROM uses the same packed-decimal operation above minute 99.
    return bytes(((value // 10 << 4) | value % 10) for value in (minute, second, frame))


def stream_track(track, output):
    """Bounded streaming; source/output digests refer to their own byte streams."""
    source_sha, output_sha = hashlib.sha256(), hashlib.sha256()
    source_crc, output_crc, consumed, written = 0, 0, 0, 0
    convert = track.control == 4 and track.sector_bytes == RAW_BYTES
    with track.path.open("rb") as source, output.open("xb") as destination:
        require(file_signature(track.path) == track.signature,
                f"Track {track.number}: source changed before reading")
        while consumed < track.file_bytes:
            wanted = min(COPY_BYTES, track.file_bytes - consumed)
            block = source.read(wanted)
            require(len(block) == wanted, f"Track {track.number}: truncated while reading")
            source_sha.update(block)
            source_crc = zlib.crc32(block, source_crc)
            if convert:
                cooked = bytearray()
                for offset in range(0, len(block), RAW_BYTES):
                    sector = block[offset:offset + RAW_BYTES]
                    sector_index = (consumed + offset) // RAW_BYTES
                    address = track.start_lba + sector_index
                    label = f"Track {track.number}, sector {sector_index} (LBA {address})"
                    require(sector[:12] == SYNC, f"{label}: invalid raw sync header")
                    require(sector[15] == 1, f"{label}: unsupported sector mode {sector[15]} (need Mode 1)")
                    require(sector[12:15] == msf(address), f"{label}: sector address does not match GDI")
                    cooked.extend(sector[16:16 + DATA_BYTES])
                result = cooked
            else:
                result = block
            destination.write(result)
            output_sha.update(result)
            output_crc = zlib.crc32(result, output_crc)
            consumed += len(block)
            written += len(result)
        require(not source.read(1), f"Track {track.number}: source grew while reading")
        require(file_signature(track.path) == track.signature,
                f"Track {track.number}: source changed while reading")
        destination.flush()
        os.fsync(destination.fileno())
    return {
        "number": track.number, "start_lba": track.start_lba,
        "end_lba_exclusive": track.start_lba + track.sectors,
        "control": track.control, "sectors": track.sectors,
        "operation": "extract-mode1-payload" if convert else "copy",
        "raw_mode1_headers_checked": convert,
        "source": {"file": track.name, "sector_bytes": track.sector_bytes,
                   "bytes": consumed, "crc32": f"{source_crc:08x}", "sha256": source_sha.hexdigest()},
        "output": {"file": output.name, "sector_bytes": DATA_BYTES if track.control == 4 else RAW_BYTES,
                   "bytes": written, "crc32": f"{output_crc:08x}", "sha256": output_sha.hexdigest()},
    }


def publish_directory(source, destination):
    """Atomically publish without replacing even a newly-created empty folder."""
    if os.name == "nt":
        os.rename(source, destination)  # Windows rename refuses any existing target.
        return
    libc = ctypes.CDLL(None, use_errno=True)
    if sys.platform.startswith("linux") and hasattr(libc, "renameat2"):
        rename = libc.renameat2
        rename.argtypes = (ctypes.c_int, ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_uint)
        rename.restype = ctypes.c_int
        result = rename(-100, os.fsencode(source), -100, os.fsencode(destination), 1)  # RENAME_NOREPLACE
    elif sys.platform == "darwin" and hasattr(libc, "renamex_np"):
        rename = libc.renamex_np
        rename.argtypes = (ctypes.c_char_p, ctypes.c_char_p, ctypes.c_uint)
        rename.restype = ctypes.c_int
        result = rename(os.fsencode(source), os.fsencode(destination), 4)  # RENAME_EXCL
    else:
        raise OSError(errno.ENOTSUP, "Platform cannot guarantee atomic publication without overwriting")
    if result:
        code = ctypes.get_errno()
        raise OSError(code, os.strerror(code), str(destination))


def optimize(gdi, output_directory, progress=None):
    """Return the report only after the complete new directory is published."""
    gdi = Path(gdi).expanduser().absolute()
    require(not gdi.is_symlink(), "GDI must not be a symbolic link")
    gdi = gdi.resolve()
    output = Path(output_directory).expanduser().absolute()
    require(not os.path.lexists(output), f"Output already exists: {output}")
    require(output.parent.is_dir(), f"Output parent directory does not exist: {output.parent}")
    output = output.parent.resolve() / output.name
    require(output != gdi.parent and gdi.parent not in output.parents,
            "Output must be outside the original dump directory")
    descriptor, signature, tracks = load_gdi(gdi)
    require(safe_name(gdi.name) and gdi.suffix.lower() == ".gdi", "Input must have a safe .gdi filename")
    records, rows = [], [str(len(tracks))]
    stage = Path(tempfile.mkdtemp(prefix=f".{output.name}.staging-", dir=output.parent))
    try:
        for track in tracks:
            name = f"track{track.number:02d}.{'iso' if track.control == 4 else 'raw'}"
            if progress:
                progress(f"Track {track.number}/{len(tracks)}: {track.name} -> {name}")
            record = stream_track(track, stage / name)
            records.append(record)
            rows.append(f'{track.number} {track.start_lba} {track.control} '
                        f'{record["output"]["sector_bytes"]} "{name}" 0')
        require(file_signature(gdi) == signature and gdi.read_bytes() == descriptor,
                "GDI changed during conversion")
        for track in tracks:
            require(file_signature(track.path) == track.signature,
                    f"Track {track.number}: source changed during conversion")
        new_descriptor = ("\n".join(rows) + "\n").encode("ascii")
        source_data = sum(t["source"]["bytes"] for t in records if t["control"] == 4)
        output_data = sum(t["output"]["bytes"] for t in records if t["control"] == 4)
        report = {
            "schema": 1, "profile": "kui-gdi-mode1-2048-copy-v1",
            "tool": "tools/gdi_optimize.py", "complete": True,
            "verification": {
                "raw_mode1_checks": ["sync", "mode", "sector-address"],
                "edc_ecc_checked": False, "catalogue_match_checked": False,
                "note": "Conversion hashes describe separate source/output byte streams. Preserve the verified raw dump for archival use.",
            },
            "source_gdi": {"path": str(gdi), **digest(descriptor)},
            "output_gdi": {"file": gdi.name, **digest(new_descriptor)},
            "source_data_bytes": source_data, "output_data_bytes": output_data,
            "saved_data_bytes": source_data - output_data,
            "source_track_bytes": sum(t["source"]["bytes"] for t in records),
            "output_track_bytes": sum(t["output"]["bytes"] for t in records),
            "tracks": records,
        }
        # Both metadata files are private in staging; the directory becomes
        # visible at its requested name in one exclusive rename only when done.
        (stage / "conversion.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        (stage / gdi.name).write_bytes(new_descriptor)
        publish_directory(stage, output)
        return report
    finally:
        if stage.exists():
            shutil.rmtree(stage)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("gdi", type=Path, help="Original .gdi descriptor")
    parser.add_argument("output", type=Path, help="New folder outside the original dump folder; must not exist")
    args = parser.parse_args()
    try:
        report = optimize(args.gdi, args.output, progress=print)
    except (ValueError, OSError) as error:
        print(f"Conversion failed: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print("Conversion cancelled; no completed output folder published.", file=sys.stderr)
        return 130
    saved = report["saved_data_bytes"]
    print(f"Created {args.output}: {saved:,} data bytes saved; originals unchanged.")
    print("Source/output CRC32 and SHA-256 recorded in conversion.json; no catalogue verification performed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
