#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a bounded native Dreamcast startup-analysis ZIP from an original GDI.

Usage: python3 kui-native-startup-bundle.py /path/to/disc.gdi
Reads original raw2352 tracks only. Copies IP.BIN and the unique IP-selected
boot file, together with the original GDI and a hashed metadata report.
No other game files or audio payloads are copied. Requires only Python3's
standard library. Creates Native-startup.zip without overwriting.
This is analysis preparation, not a game patch or a compatibility claim.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import tempfile
import zipfile
import zlib

# This self-contained owner helper follows K-UI's independently authored
# raw-GDI/Mode1 reader and atomic ZIP publication, without runtime imports.
RAW, DATA = 2352, 2048
FILE_LIMIT = 12 * 1024 * 1024
DIR_LIMIT = 1024 * 1024
DIRECTORY_COUNT_LIMIT = 512
SCAN_ENTRY_LIMIT = 32768
TOTAL_DIR_LIMIT = 16 * 1024 * 1024
EXTRACTOR_VERSION = '1'


class WindowsCEProfileError(ValueError):
    """A validated original GDI/IP header explicitly selects Windows CE."""


def both(data, offset, width):
    if len(data) < offset + 2 * width:
        raise ValueError('Truncated ISO metadata field')
    little = int.from_bytes(data[offset:offset + width], 'little')
    big = int.from_bytes(data[offset + width:offset + 2 * width], 'big')
    if little != big:
        raise ValueError('ISO metadata byte orders disagree')
    return little


def identifier(raw):
    name = raw.decode('ascii').upper()
    if ';' in name:
        name, version = name.split(';', 1)
        if version != '1':
            raise ValueError('Unsupported or ambiguous ISO file version')
    if (not name or name in ('.', '..') or
            any(ord(c) < 32 or ord(c) >= 127 or c in '/\\:' for c in name)):
        raise ValueError('Unsafe ISO filename')
    return name


def file_record(entry):
    if len(entry) < 34 or entry[0] != len(entry) or not entry[32] or 33 + entry[32] > len(entry):
        raise ValueError('Invalid ISO directory entry')
    if entry[1] or entry[26] or entry[27] or both(entry, 28, 2) != 1:
        raise ValueError('Unsupported ISO extended/interleaved/volume extent')
    return both(entry, 2, 4), both(entry, 10, 4), entry[25], entry[33:33 + entry[32]]


def metadata(path, data, lba=None):
    return {'path': path, 'bytes': len(data), 'lba': lba,
            'crc32': f'{zlib.crc32(data):08x}',
            'sha256': hashlib.sha256(data).hexdigest()}


def extract(gdi):
    gdi = Path(gdi)
    if gdi.is_symlink() or not gdi.is_file():
        raise ValueError('GDI must be a regular file, not a symlink')
    if not 0 < gdi.stat().st_size <= 64 * 1024:
        raise ValueError('GDI descriptor must be between 1 byte and 64 KiB')
    original_gdi = gdi.read_bytes()
    lines = [shlex.split(line) for line in original_gdi.decode('utf-8-sig').splitlines()
             if line.strip()]
    if not lines or len(lines[0]) != 1 or not 1 <= int(lines[0][0]) <= 99:
        raise ValueError('Expected a GDI with 1–99 tracks')
    if len(lines) != int(lines[0][0]) + 1:
        raise ValueError('GDI track count does not match')
    tracks = []
    for index, row in enumerate(lines[1:], 1):
        if len(row) != 6:
            raise ValueError('Malformed GDI track row')
        number, start, control, stride = map(int, row[:4])
        name, offset = row[4], int(row[5])
        if (number != index or start < 0 or control not in (0, 4) or stride != RAW or offset or
                '/' in name or '\\' in name or ':' in name or name in ('', '.', '..')):
            raise ValueError('Requires original raw2352 GDI tracks with zero file offsets')
        path = gdi.parent / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Track must be a regular file, not a symlink')
        length = path.stat().st_size
        if not length or length % RAW:
            raise ValueError('Track length is not a whole number of raw sectors')
        end = start + length // RAW
        if end > 719850 or (tracks and tracks[-1][1] > start):
            raise ValueError('Invalid or overlapping GDI track range')
        tracks.append((start, end, control, path))
    sessions = [t[0] for t in tracks if t[2] == 4 and t[0] >= 45000]
    if not sessions:
        raise ValueError('No native GD high-density data session found')
    session = sessions[0]

    def read(lba, size):
        if size <= 0 or size > FILE_LIMIT:
            raise ValueError('Requested extent exceeds the file extraction limit')
        if lba < session:
            raise ValueError('Startup extent leaves the high-density data session')
        count = (size + DATA - 1) // DATA
        result = bytearray()
        while count:
            track = next((t for t in tracks if t[0] <= lba < t[1]), None)
            if not track or track[2] != 4:
                raise ValueError('Startup extent leaves the data tracks')
            take = min(count, track[1] - lba)
            with track[3].open('rb') as source:
                source.seek((lba - track[0]) * RAW)
                for _ in range(take):
                    sector = source.read(RAW)
                    if (len(sector) != RAW or sector[:12] != b'\0' + b'\xff' * 10 + b'\0' or
                            sector[15] != 1):
                        raise ValueError('Truncated or non-Mode1 raw data sector')
                    result.extend(sector[16:16 + DATA])
            lba += take
            count -= take
        return bytes(result[:size])

    ip = read(session, 32768)
    if ip[:16] != b'SEGA SEGAKATANA ' or ip[37:43] != b'GD-ROM':
        raise ValueError('Dreamcast native GD IP header not found')
    peripheral = ip[56:63].decode('ascii')
    if ip[63] != 32 or any(c not in '0123456789ABCDEFabcdef' for c in peripheral):
        raise ValueError('Invalid IP peripheral flags')
    flags = int(peripheral, 16)
    if flags & 1:
        raise WindowsCEProfileError('Extraction requires a native IP profile; Windows CE is not supported')
    boot_name = identifier(ip[96:112].strip(b' \0'))
    primary = None
    primary_lba = None
    for index in range(16):
        descriptor = read(session + 16 + index, DATA)
        if descriptor[1:7] != b'CD001\1':
            raise ValueError('Invalid ISO volume descriptor')
        if descriptor[0] == 1:
            primary, primary_lba = descriptor, session + 16 + index
            break
        if descriptor[0] == 255:
            break
    if primary is None or both(primary, 128, 2) != DATA:
        raise ValueError('No supported ISO primary volume descriptor')
    root_lba, root_size, root_flags, root_name = file_record(primary[156:190])
    if root_flags != 2 or root_name != b'\0':
        raise ValueError('Invalid ISO root directory record')
    pending = [('', root_lba, root_size)]
    seen_directories, seen_paths, selected = set(), set(), {}
    directory_metadata = []
    scanned, directory_bytes = 0, 0
    while pending:
        prefix, lba, size = pending.pop(0)
        if (lba, size) in seen_directories:
            raise ValueError('ISO directory cycle or aliased directory extent')
        seen_directories.add((lba, size))
        directory_bytes += size
        if (len(seen_directories) > DIRECTORY_COUNT_LIMIT or not 0 < size <= DIR_LIMIT or
                directory_bytes > TOTAL_DIR_LIMIT):
            raise ValueError('ISO directory traversal exceeds the bounded extraction limit')
        directory = read(lba, size)
        directory_metadata.append(metadata(prefix or '/', directory, lba))
        position = 0
        while position < len(directory):
            length = directory[position]
            if not length:
                position = (position // DATA + 1) * DATA
                continue
            remaining = min(len(directory) - position, DATA - position % DATA)
            if length < 34 or length > remaining:
                raise ValueError('Invalid ISO directory entry')
            extent, file_size, file_flags, raw_name = file_record(directory[position:position + length])
            position += length
            scanned += 1
            if scanned > SCAN_ENTRY_LIMIT:
                raise ValueError('ISO entry count exceeds the bounded extraction limit')
            if raw_name in (b'\0', b'\1'):
                if file_flags != 2:
                    raise ValueError('Invalid ISO dot directory entry')
                continue
            name = identifier(raw_name)
            path = prefix + name
            if path in seen_paths:
                raise ValueError('Duplicate or ambiguous ISO path')
            seen_paths.add(path)
            if file_flags & 2:
                if file_flags != 2:
                    raise ValueError('Unsupported ISO directory flags')
                if len(pending) + len(seen_directories) >= DIRECTORY_COUNT_LIMIT:
                    raise ValueError('ISO directory count exceeds the bounded extraction limit')
                pending.append((path + '/', extent, file_size))
            elif name == boot_name:
                if file_flags:
                    raise ValueError('Selected boot file has unsupported or multi-extent ISO flags')
                if not 0 < file_size <= FILE_LIMIT:
                    raise ValueError(f'IP-selected boot file {path} is {file_size:,} bytes; '
                                     f'expected 1–{FILE_LIMIT:,} bytes')
                selected[path] = (extent, file_size)
    boot_candidates = sorted(selected)
    if not boot_candidates:
        raise ValueError(f'IP-selected boot file {boot_name} was not found in the ISO directories')
    if len(boot_candidates) != 1:
        raise ValueError('Ambiguous IP-selected boot paths: ' + ', '.join(boot_candidates))
    boot_path = boot_candidates[0]
    # Some ZIP extractors trim trailing dots/spaces on Windows. A selected
    # IP.BIN. or '.. ' directory must not alias metadata or a parent directory.
    if any(part.rstrip(' .') != part for part in boot_path.split('/')):
        raise ValueError('Selected ISO boot path has unsafe trailing dots or spaces')
    if boot_path.split('/', 1)[0].casefold() in {'ip.bin', 'disc.gdi', 'startup-info.json'}:
        raise ValueError('Selected ISO boot path conflicts with generated bundle metadata')
    entries = {'IP.BIN': ip, 'disc.gdi': original_gdi,
               boot_path: read(*selected[boot_path])}
    # Hash extracted bytes and the ISO metadata used to locate them. The JSON
    # does not hash itself, which would be circular; raw tracks are not hashed.
    report = {
        'extractor_version': EXTRACTOR_VERSION,
        'purpose': 'K-UI native startup analysis; game bytes unchanged',
        'title': ip[128:256].decode('ascii').strip(' \0'),
        'product': ip[64:74].decode('ascii').strip(' \0'),
        'version': ip[74:80].decode('ascii').strip(' \0'),
        'region': ip[48:56].decode('ascii').strip(' \0'),
        'windows_ce': False, 'peripheral_flags': flags,
        'boot_filename': boot_name, 'boot_path': boot_path,
        'boot_candidates': boot_candidates, 'session_lba': session,
        'selected_file_count': 1, 'archive_file_count': 4,
        'selected_bytes': sum(map(len, entries.values())),
        'directory_count': len(seen_directories), 'scanned_entries': scanned,
        'tracks': [
            {'number': number, 'start_lba': track[0], 'end_lba': track[1],
             'control': track[2], 'sector_bytes': RAW, 'file': track[3].name,
             'file_bytes': (track[1] - track[0]) * RAW}
            for number, track in enumerate(tracks, 1)],
        'limits': {'per_file_bytes': FILE_LIMIT, 'per_directory_bytes': DIR_LIMIT,
                   'directory_count': DIRECTORY_COUNT_LIMIT,
                   'scanned_entries': SCAN_ENTRY_LIMIT,
                   'total_directory_bytes': TOTAL_DIR_LIMIT},
        'selection': 'Only IP.BIN and the unique IP-selected boot file, plus original GDI and metadata',
        'verification': 'Selected extents and Mode1 framing only; not complete disc or EDC/ECC '
                        'verification, a game patch, or a compatibility claim',
        'audio_payload_copied': False,
        'iso_metadata': {'primary_volume_descriptor': metadata('PVD', primary, primary_lba),
                         'directories': directory_metadata},
        'files': [metadata(name, data, session if name == 'IP.BIN' else
                           selected[name][0] if name == boot_path else None)
                  for name, data in entries.items()]}
    return entries, report


def write_bundle(destination, entries, report):
    """Publish a closed, CRC-checked and byte-exact ZIP without overwriting."""
    destination = Path(destination)
    if os.path.lexists(destination):
        raise ValueError(f'{destination} already exists; choose a new --output filename')
    payload = dict(entries)
    payload['startup-info.json'] = (json.dumps(report, indent=2) + '\n').encode('utf-8')
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.Native-startup-', suffix='.zip.tmp',
                                         dir=destination.parent, delete=False) as stage:
            temporary = Path(stage.name)
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
            for name, data in payload.items():
                archive.writestr(name, data)
        with zipfile.ZipFile(temporary, 'r') as verify:
            if verify.namelist() != list(payload) or verify.testzip() is not None:
                raise ValueError('Generated ZIP failed its integrity check')
            for name, data in payload.items():
                if verify.read(name) != data:
                    raise ValueError('Generated ZIP differs from extracted startup bytes')
        with temporary.open('rb') as archive:
            os.fsync(archive.fileno())
        os.link(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    version = f'K-UI native startup extractor v{EXTRACTOR_VERSION}'
    parser.add_argument('--version', action='version', version=version)
    parser.add_argument('gdi', type=Path, help='Original, unconverted native raw2352 GDI')
    parser.add_argument('--output', type=Path, default=Path('Native-startup.zip'))
    args = parser.parse_args()
    print(version, flush=True)
    try:
        if os.path.lexists(args.output):
            raise ValueError(f'{args.output} already exists; choose a new --output filename')
        entries, report = extract(args.gdi)
        write_bundle(args.output, entries, report)
        print(f'Created {args.output.resolve()} ({args.output.stat().st_size:,} bytes)')
        print('Upload this ZIP when requested for native startup analysis. Source tracks were read only.')
    except (OSError, ValueError, zipfile.BadZipFile, UnicodeError) as error:
        parser.exit(1, f'Extraction stopped: {error}\n')


if __name__ == '__main__':
    main()
