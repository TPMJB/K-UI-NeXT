#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Export only AUDIO64.DRV from the original preflight-verified Toy Commander GDI."""
import argparse
from collections import deque
from contextlib import ExitStack
from dataclasses import dataclass
import hashlib
import os
from pathlib import Path
import re
import sys
import zlib

import extract_cdda_toy_boot as boot


ExtractionError = boot.ExtractionError


@dataclass(frozen=True)
class DataGeometry:
    number: int
    lba: int
    size: int


@dataclass(frozen=True)
class DriverProfile:
    boot: boot.BootProfile
    data_tracks: tuple
    session_lba: int
    ip_sha256: str


TOY_PROFILE = DriverProfile(
    boot=boot.TOY_PROFILE,
    data_tracks=(DataGeometry(1, 0, 16715664),
                 DataGeometry(3, 45000, 368676000),
                 DataGeometry(15, 377422, 403904256)),
    session_lba=45000,
    ip_sha256="dcb2b68f92456fac4261ab8e386875f8f337e40e6e26bd11368f873f55fbc5a6",
)
MAX_DIRECTORIES = 128
MAX_RECORDS = 4096
MAX_DIRECTORY_READ_BYTES = 2 * 1024 * 1024
MAX_DRIVER_BYTES = 1024 * 1024


class Image:
    """Read-only logical-sector view; audio tracks and disc gaps remain unmapped."""
    def __init__(self, tracks, profile):
        self.stack = ExitStack()
        self.handles = {}
        self.geometry = {}
        expected = {geometry.number for geometry in profile.data_tracks}
        if {track.number for track in tracks if track.kind == 4} != expected:
            raise ExtractionError("GDI has unsupported data-track geometry")
        previous_end = -1
        for geometry in profile.data_tracks:
            track = tracks[geometry.number - 1]
            if (track.number != geometry.number or track.lba != geometry.lba or
                    track.kind != 4 or track.stride != boot.RAW_BYTES or track.offset != 0 or
                    geometry.size <= 0 or geometry.size % boot.RAW_BYTES):
                raise ExtractionError(f"Track {geometry.number} does not match verified geometry")
            end = geometry.lba + geometry.size // boot.RAW_BYTES
            if geometry.lba < previous_end or end > 719850:
                raise ExtractionError("Data tracks overlap or exceed disc bounds")
            self.geometry[geometry.number] = (track, geometry, end)
            previous_end = end
        self.last_lba = previous_end

    def __enter__(self):
        return self

    def __exit__(self, *args):
        return self.stack.__exit__(*args)

    def _mapped(self, lba):
        for track, geometry, end in self.geometry.values():
            if geometry.lba <= lba < end:
                return track, geometry, end
        raise ExtractionError(f"Logical sector {lba} is outside verified data tracks")

    def check_extent(self, lba, size, volume_blocks=None):
        blocks = (size + boot.PAYLOAD_BYTES - 1) // boot.PAYLOAD_BYTES
        if size <= 0 or lba < 0 or lba + blocks > 0xFFFFFFFF:
            raise ExtractionError("Invalid ISO9660 file extent")
        if volume_blocks is not None and blocks > volume_blocks:
            raise ExtractionError("ISO9660 extent exceeds volume size")
        # A range may cross adjacent data backings, but never an audio track/gap.
        cursor = lba
        while cursor < lba + blocks:
            _, _, end = self._mapped(cursor)
            cursor = min(end, lba + blocks)

    def sector(self, lba):
        track, geometry, _ = self._mapped(lba)
        handle = self.handles.get(track.number)
        if handle is None:
            handle = self.stack.enter_context(track.backing.open("rb"))
            info = boot._regular_file(handle, f"Track {track.number} backing")
            if info.st_size != geometry.size:
                raise ExtractionError(f"Track {track.number} backing size does not match preflight")
            self.handles[track.number] = handle
        handle.seek((lba - geometry.lba) * boot.RAW_BYTES)
        raw = handle.read(boot.RAW_BYTES)
        if len(raw) != boot.RAW_BYTES:
            raise ExtractionError(f"Truncated raw sector {lba}")
        boot._header(raw, lba)
        return raw[16:16 + boot.PAYLOAD_BYTES]

    def read(self, lba, size, volume_blocks=None):
        self.check_extent(lba, size, volume_blocks)
        result = bytearray()
        for i in range((size + 2047) // 2048):
            result.extend(self.sector(lba + i)[:min(2048, size - len(result))])
        return bytes(result)


def _both(data, offset, width):
    little = int.from_bytes(data[offset:offset + width], "little")
    big = int.from_bytes(data[offset + width:offset + 2 * width], "big")
    if little != big:
        raise ExtractionError("ISO9660 byte-order copies disagree")
    return little


@dataclass(frozen=True)
class Record:
    lba: int
    size: int
    flags: int
    name: bytes
    length: int


def _record(data, available):
    length = data[0] if data else 0
    if available < 34 or length < 34 or length > available or length % 2:
        raise ExtractionError("Invalid ISO9660 directory record length")
    count = data[32]
    name_end = 33 + count
    padded_end = name_end + (0 if count % 2 else 1)
    if not count or padded_end > length or (count % 2 == 0 and data[name_end]):
        raise ExtractionError("Invalid ISO9660 identifier or padding")
    if _both(data, 28, 2) != 1:
        raise ExtractionError("Unsupported ISO9660 volume sequence")
    if data[1] or data[26] or data[27] or data[25] & 0xFC:
        raise ExtractionError("Unsupported ISO9660 extended, interleaved or multi-extent record")
    return Record(_both(data, 2, 4), _both(data, 10, 4), data[25],
                  bytes(data[33:name_end]), length)


def _identifier(name):
    try:
        text = name.decode("ascii")
    except UnicodeError as error:
        raise ExtractionError("Unsupported non-ASCII ISO9660 identifier") from error
    if (not text or any(ord(c) < 32 or ord(c) > 126 for c in text) or
            "/" in text or "\\" in text or text in (".", "..")):
        raise ExtractionError("Unsupported ISO9660 identifier")
    # ISO9660 file versions are decimal suffixes, not part of the base filename.
    pieces = text.split(";")
    if len(pieces) > 2 or (len(pieces) == 2 and not re.fullmatch(r"[1-9][0-9]*", pieces[1])):
        raise ExtractionError("Invalid ISO9660 version suffix")
    if not pieces[0]:
        raise ExtractionError("Empty ISO9660 filename")
    return text, pieces[0].upper()


def _primary(image, session_lba):
    data = None
    for index in range(16):
        candidate = image.sector(session_lba + 16 + index)
        if candidate[1:7] != b"CD001\x01":
            raise ExtractionError("Invalid ISO9660 volume descriptor")
        kind = candidate[0]
        if kind == 1:
            data = candidate
            break
        if kind == 255:
            raise ExtractionError("ISO9660 descriptor terminator precedes primary descriptor")
        if kind > 3:
            raise ExtractionError("Unsupported ISO9660 volume descriptor type")
    if data is None:
        raise ExtractionError("ISO9660 primary descriptor scan limit exceeded")
    if data[7] or data[881] != 1:
        raise ExtractionError("Verified session has no supported primary ISO9660 descriptor")
    volume_blocks = _both(data, 80, 4)
    if not 16 + index < volume_blocks <= image.last_lba:
        raise ExtractionError("Invalid ISO9660 volume block count")
    if (_both(data, 120, 2) != 1 or _both(data, 124, 2) != 1 or
            _both(data, 128, 2) != 2048):
        raise ExtractionError("Unsupported ISO9660 volume geometry")
    root = _record(data[156:190], 34)
    if root.length != 34 or root.name != b"\x00" or not root.flags & 2:
        raise ExtractionError("Invalid ISO9660 root directory")
    # As in game_metadata.c, volume_blocks is a block count, not an absolute
    # end LBA (cdrkit may encode last_extent minus session_start). Directory
    # and file LBAs are already absolute: check their block counts plus the
    # mapped data ranges, without adding a session offset to their extents.
    image.check_extent(root.lba, root.size, volume_blocks)
    return volume_blocks, root


def _find_driver(image, session_lba):
    volume_blocks, root = _primary(image, session_lba)
    queue = deque([(root, root, "")])
    seen = {(root.lba, root.size)}
    records = read_bytes = 0
    found = None
    while queue:
        directory, parent, path = queue.popleft()
        allocated = ((directory.size + 2047) // 2048) * 2048
        if read_bytes + allocated > MAX_DIRECTORY_READ_BYTES:
            raise ExtractionError("ISO9660 directory read limit exceeded")
        image.check_extent(directory.lba, directory.size, volume_blocks)
        read_bytes += allocated
        dot_count = 0
        for done in range(0, directory.size, 2048):
            data = image.sector(directory.lba + done // 2048)
            amount = min(2048, directory.size - done)
            pos = 0
            while pos < amount:
                if not data[pos]:
                    if any(data[pos:amount]):
                        raise ExtractionError("Nonzero ISO9660 sector padding")
                    break
                entry = _record(data[pos:amount], amount - pos)
                records += 1
                if records > MAX_RECORDS:
                    raise ExtractionError("ISO9660 directory record limit exceeded")
                if entry.name in (b"\x00", b"\x01"):
                    expected = directory if dot_count == 0 else parent
                    if (dot_count >= 2 or entry.name != bytes((dot_count,)) or
                            not entry.flags & 2 or entry.lba != expected.lba or
                            entry.size != expected.size or (dot_count == 0 and done + pos != 0)):
                        raise ExtractionError("Invalid ISO9660 self or parent directory record")
                    dot_count += 1
                else:
                    if dot_count != 2:
                        raise ExtractionError("ISO9660 directory lacks self and parent records")
                    text, base_name = _identifier(entry.name)
                    full_path = f"{path}/{text}" if path else text
                    if entry.flags & 2:
                        image.check_extent(entry.lba, entry.size, volume_blocks)
                        key = (entry.lba, entry.size)
                        if key in seen:
                            raise ExtractionError("ISO9660 directory alias or cycle is unsupported")
                        seen.add(key)
                        if len(seen) > MAX_DIRECTORIES:
                            raise ExtractionError("ISO9660 directory count limit exceeded")
                        queue.append((entry, directory, full_path))
                    elif base_name == "AUDIO64.DRV":
                        if found is not None:
                            raise ExtractionError("Multiple AUDIO64.DRV matches; refusing ambiguous export")
                        if not 0 < entry.size <= MAX_DRIVER_BYTES:
                            raise ExtractionError("AUDIO64.DRV exceeds the supported file size")
                        image.check_extent(entry.lba, entry.size, volume_blocks)
                        found = entry, full_path
                pos += entry.length
        if dot_count != 2:
            raise ExtractionError("ISO9660 directory lacks self and parent records")
    if found is None:
        raise ExtractionError("AUDIO64.DRV was not found in the bounded ISO9660 directory walk")
    return found, volume_blocks


def _extract_driver(gdi_path, output_path, profile):
    gdi = Path(gdi_path).resolve()
    requested = Path(output_path).absolute()
    output = requested.parent.resolve() / requested.name
    if os.path.lexists(output):
        raise ExtractionError(f"Output already exists; choose a new path: {output}")
    if not output.parent.is_dir():
        raise ExtractionError("Output parent directory must already exist")
    with gdi.open("rb") as handle:
        boot._regular_file(handle, "GDI")
        descriptor = handle.read(profile.boot.descriptor_bytes + 1)
    if (len(descriptor) != profile.boot.descriptor_bytes or
            hashlib.sha256(descriptor).hexdigest() != profile.boot.descriptor_sha256):
        raise ExtractionError("GDI does not match the original Toy Commander preflight identity")
    tracks = boot._tracks(descriptor, gdi.parent, profile.boot.track_number)
    for track in tracks:
        try:
            track.backing.relative_to(gdi.parent)
        except ValueError as error:
            raise ExtractionError("Game backing path escapes the original GDI directory") from error
    if output == gdi or any(output == track.backing for track in tracks):
        raise ExtractionError("Output must not be a GDI or game backing path")
    with Image(tracks, profile) as image:
        ip = image.read(profile.session_lba, 32768)
        if hashlib.sha256(ip).hexdigest() != profile.ip_sha256:
            raise ExtractionError("IP payload does not match the preflight SHA256")
        executable = image.read(profile.boot.boot_lba, profile.boot.boot_bytes)
        if (hashlib.sha256(executable).hexdigest() != profile.boot.boot_sha256 or
                zlib.crc32(executable) & 0xFFFFFFFF != profile.boot.boot_crc32):
            raise ExtractionError("Boot payload does not match the preflight SHA256/CRC32")
        (entry, iso_path), volume_blocks = _find_driver(image, profile.session_lba)
        data = image.read(entry.lba, entry.size, volume_blocks)
    try:
        boot._publish(output, data)
    except ExtractionError as error:
        if "Could not atomically publish output" in str(error):
            raise ExtractionError(f"{error}. Save the output in your computer home folder; "
                                  "the card may not support hard links") from error
        raise
    return output, iso_path, entry.lba, len(data), hashlib.sha256(data).hexdigest()


def extract_driver(gdi_path, output_path):
    return _extract_driver(gdi_path, output_path, TOY_PROFILE)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("original_gdi", help="Path to the original TOY_COMMANDER.gdi")
    parser.add_argument("output", help="New output file in your computer home folder")
    args = parser.parse_args(argv)
    try:
        output, iso_path, lba, size, sha256 = extract_driver(args.original_gdi, args.output)
    except (OSError, ExtractionError) as error:
        print(f"Extraction refused: {error}", file=sys.stderr)
        return 1
    print(f"Exported {iso_path}: logical LBA {lba}, {size} bytes")
    print(f"SHA256 {sha256}")
    print(f"Output: {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
