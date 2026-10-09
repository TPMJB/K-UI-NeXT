#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract only the exact Toy Commander boot executable verified by preflight."""
import argparse
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import shlex
import stat
import sys
import tempfile
import zlib


@dataclass(frozen=True)
class BootProfile:
    descriptor_bytes: int
    descriptor_sha256: str
    track_number: int
    track_lba: int
    track_bytes: int
    boot_lba: int
    boot_bytes: int
    boot_crc32: int
    boot_sha256: str


TOY_PROFILE = BootProfile(
    descriptor_bytes=451,
    descriptor_sha256="96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803",
    track_number=15,
    track_lba=377422,
    track_bytes=403904256,
    boot_lba=548634,
    boot_bytes=748444,
    boot_crc32=0xCDC493B3,
    boot_sha256="ee68a326da964fec5892b7226311f2293fd3a0bbf5bac496ec82991964d49bbd",
)
RAW_BYTES = 2352
PAYLOAD_BYTES = 2048
SYNC = b"\x00" + b"\xff" * 10 + b"\x00"


class ExtractionError(ValueError):
    """An input, identity or output-publication check failed."""


@dataclass(frozen=True)
class Track:
    number: int
    lba: int
    kind: int
    stride: int
    backing: Path
    offset: int


def _regular_file(handle, label):
    info = os.fstat(handle.fileno())
    if not stat.S_ISREG(info.st_mode):
        raise ExtractionError(f"{label} must be a regular file")
    return info


def _tracks(descriptor, directory, expected_count):
    try:
        lines = [shlex.split(line, comments=False, posix=True)
                 for line in descriptor.decode("utf-8").splitlines() if line.strip()]
        if not lines or len(lines[0]) != 1 or int(lines[0][0]) != expected_count:
            raise ValueError("track count")
        if len(lines) != expected_count + 1:
            raise ValueError("track rows")
        tracks = []
        previous_lba = -1
        for number, row in enumerate(lines[1:], 1):
            if len(row) != 6:
                raise ValueError("six track fields required")
            n, lba, kind, stride = map(int, row[:4])
            offset = int(row[5])
            name = Path(row[4])
            if (n != number or lba <= previous_lba or lba >= 719850 or
                    kind not in (0, 4) or stride != RAW_BYTES or offset < 0 or
                    not row[4] or name.is_absolute() or ".." in name.parts):
                raise ValueError("unsupported track geometry or backing path")
            tracks.append(Track(n, lba, kind, stride, (directory / name).resolve(), offset))
            previous_lba = lba
        return tracks
    except (UnicodeError, ValueError) as error:
        raise ExtractionError(f"Invalid original GDI: {error}") from error


def _bcd(value):
    # Match kui_retail_sector_header, including GD minutes above 99.
    return ((value // 10) << 4 | value % 10) & 255


def _header(raw, lba):
    if raw[:12] != SYNC:
        raise ExtractionError(f"Raw sector {lba}: invalid Mode 1 sync")
    if raw[15] != 1:
        raise ExtractionError(f"Raw sector {lba}: expected Mode 1")
    fad = lba + 150
    address = bytes((_bcd(fad // 4500), _bcd(fad // 75 % 60), _bcd(fad % 75)))
    if raw[12:15] != address:
        raise ExtractionError(f"Raw sector {lba}: header address does not match logical LBA")


def _publish(output, data):
    """Publish complete verified bytes atomically without replacing any path."""
    temporary = None
    try:
        # mkstemp uses exclusive creation; keep the temporary on the same filesystem.
        fd, name = tempfile.mkstemp(prefix=".toy-boot-", suffix=".tmp", dir=output.parent)
        temporary = Path(name)
        with os.fdopen(fd, "wb") as handle:
            handle.write(data)
            handle.flush()
            os.fsync(handle.fileno())
        # A link creates the final directory entry atomically and fails if it exists,
        # including a dangling symlink. No replace/rename fallback may clobber it.
        os.link(temporary, output)
    except FileExistsError as error:
        raise ExtractionError(f"Output already exists; choose a new path: {output}") from error
    except OSError as error:
        raise ExtractionError(f"Could not atomically publish output: {error}") from error
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def _extract_boot(gdi_path, output_path, profile):
    gdi = Path(gdi_path).resolve()
    # Do not resolve the final component: that would follow a destination symlink.
    requested = Path(output_path).absolute()
    output = requested.parent.resolve() / requested.name
    if os.path.lexists(output):
        raise ExtractionError(f"Output already exists; choose a new path: {output}")
    if not output.parent.is_dir():
        raise ExtractionError("Output parent directory must already exist")
    with gdi.open("rb") as handle:
        _regular_file(handle, "GDI")
        descriptor = handle.read(profile.descriptor_bytes + 1)
    if (len(descriptor) != profile.descriptor_bytes or
            hashlib.sha256(descriptor).hexdigest() != profile.descriptor_sha256):
        raise ExtractionError("GDI does not match the original Toy Commander preflight identity")
    tracks = _tracks(descriptor, gdi.parent, profile.track_number)
    if output == gdi or any(output == track.backing for track in tracks):
        raise ExtractionError("Output must not be a GDI or game backing path")
    track = tracks[profile.track_number - 1]
    if (track.lba != profile.track_lba or track.kind != 4 or
            track.stride != RAW_BYTES or track.offset != 0):
        raise ExtractionError("Track 15 does not match the verified raw data-track geometry")
    sectors = (profile.boot_bytes + PAYLOAD_BYTES - 1) // PAYLOAD_BYTES
    if (profile.track_bytes % RAW_BYTES or profile.boot_lba < track.lba or
            profile.boot_lba + sectors > track.lba + profile.track_bytes // RAW_BYTES):
        raise ExtractionError("Boot extent lies outside the verified track")
    data = bytearray()
    with track.backing.open("rb") as handle:
        info = _regular_file(handle, "Track 15 backing")
        if info.st_size != profile.track_bytes:
            raise ExtractionError("Track 15 backing size does not match preflight")
        handle.seek(track.offset + (profile.boot_lba - track.lba) * RAW_BYTES)
        for sector in range(sectors):
            raw = handle.read(RAW_BYTES)
            if len(raw) != RAW_BYTES:
                raise ExtractionError("Truncated raw boot sector")
            _header(raw, profile.boot_lba + sector)
            take = min(PAYLOAD_BYTES, profile.boot_bytes - len(data))
            data.extend(raw[16:16 + take])
    if len(data) != profile.boot_bytes:
        raise ExtractionError("Extracted boot-file length mismatch")
    if (hashlib.sha256(data).hexdigest() != profile.boot_sha256 or
            zlib.crc32(data) & 0xFFFFFFFF != profile.boot_crc32):
        raise ExtractionError("Boot payload does not match the preflight SHA256/CRC32")
    _publish(output, data)
    return output


def extract_boot(gdi_path, output_path):
    return _extract_boot(gdi_path, output_path, TOY_PROFILE)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_gdi", help="Path to the original TOY_COMMANDER.gdi")
    parser.add_argument("output", help="New output file; existing paths are never overwritten")
    args = parser.parse_args(argv)
    try:
        output = extract_boot(args.original_gdi, args.output)
    except (OSError, ExtractionError) as error:
        print(f"Extraction refused: {error}", file=sys.stderr)
        return 1
    print(f"Verified {TOY_PROFILE.boot_bytes} bytes: {output}")
    print(f"SHA256 {TOY_PROFILE.boot_sha256}; CRC32 {TOY_PROFILE.boot_crc32:08x}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
