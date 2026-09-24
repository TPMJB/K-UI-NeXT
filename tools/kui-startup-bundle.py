#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Extract a small startup-analysis ZIP from an existing native raw GDI dump.

Usage: python3 kui-startup-bundle.py /path/to/disc.gdi
Reads existing track files only. Creates DOA2-startup.zip in the current folder
and refuses to overwrite it. Requires only Python 3's standard library.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shlex
import zipfile
import zlib

RAW, DATA = 2352, 2048
BOOT_LIMIT = 12 * 1024 * 1024


def both(data, offset, width):
    little = int.from_bytes(data[offset:offset + width], 'little')
    big = int.from_bytes(data[offset + width:offset + 2 * width], 'big')
    if little != big:
        raise ValueError('ISO metadata byte orders disagree')
    return little


def extract(gdi):
    text = gdi.read_text(encoding='utf-8-sig')
    lines = [shlex.split(line) for line in text.splitlines() if line.strip()]
    if not lines or len(lines[0]) != 1 or not 1 <= int(lines[0][0]) <= 16:
        raise ValueError('Expected a GDI with 1–16 tracks')
    if len(lines) != int(lines[0][0]) + 1:
        raise ValueError('GDI track count does not match')
    tracks = []
    for index, row in enumerate(lines[1:], 1):
        if len(row) != 6:
            raise ValueError('Malformed GDI track row')
        number, start, control, size = map(int, row[:4])
        name, offset = row[4], int(row[5])
        if (number != index or start < 0 or control not in (0, 4) or
                size != RAW or offset or '/' in name or '\\' in name or
                name in ('', '.', '..')):
            raise ValueError('Requires native raw2352 tracks with zero file offsets')
        path = gdi.parent / name
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
        if size <= 0 or size > BOOT_LIMIT:
            raise ValueError('Requested extent is outside the extraction limit')
        count = (size + DATA - 1) // DATA
        result = bytearray()
        # Open once per contiguous track span, not once per logical sector.
        while count:
            track = next((t for t in tracks if t[0] <= lba < t[1]), None)
            if not track or track[2] != 4:
                raise ValueError('Startup extent leaves the data tracks')
            take = min(count, track[1] - lba)
            with track[3].open('rb') as source:
                source.seek((lba - track[0]) * RAW)
                for _ in range(take):
                    sector = source.read(RAW)
                    if (len(sector) != RAW or sector[:12] != b'\0' + b'\xff' * 10 + b'\0'
                            or sector[15] != 1):
                        raise ValueError('Truncated or non-Mode1 raw data sector')
                    result.extend(sector[16:16 + DATA])
            lba += take
            count -= take
        return bytes(result[:size])

    ip = read(session, 32768)
    if ip[:16] != b'SEGA SEGAKATANA ':
        raise ValueError('Dreamcast IP header not found')
    boot_name = ip[96:112].decode('ascii').strip(' \0')
    if boot_name.upper() != '1ST_READ.BIN':
        raise ValueError('This helper expects the selected 1ST_READ.BIN profile')
    primary = None
    for index in range(16):
        descriptor = read(session + 16 + index, DATA)
        if descriptor[1:7] != b'CD001\1':
            raise ValueError('Invalid ISO volume descriptor')
        if descriptor[0] == 1:
            primary = descriptor
            break
        if descriptor[0] == 255:
            break
    if primary is None or both(primary, 128, 2) != DATA:
        raise ValueError('No supported ISO primary volume descriptor')
    root = primary[156:190]
    if len(root) != 34 or root[0] != 34 or not root[25] & 2:
        raise ValueError('Invalid ISO root directory record')
    root_lba, root_size = both(root, 2, 4), both(root, 10, 4)
    if not 0 < root_size <= 1024 * 1024:
        raise ValueError('Root directory exceeds the bounded extraction limit')
    directory = read(root_lba, root_size)
    match = None
    position = 0
    while position < len(directory):
        length = directory[position]
        if not length:
            position = (position // DATA + 1) * DATA
            continue
        remaining = min(len(directory) - position, DATA - position % DATA)
        if length < 34 or length > remaining:
            raise ValueError('Invalid ISO directory entry')
        entry = directory[position:position + length]
        if 33 + entry[32] > length:
            raise ValueError('Invalid ISO filename length')
        name = entry[33:33 + entry[32]].upper()
        if name in (b'1ST_READ.BIN', b'1ST_READ.BIN;1'):
            if match or entry[1] or entry[25] & 0xfe or entry[26] or entry[27]:
                raise ValueError('Duplicate or unsupported boot-file extent')
            match = both(entry, 2, 4), both(entry, 10, 4)
        position += length
    if not match:
        raise ValueError('1ST_READ.BIN was not found in the ISO root')
    boot = read(*match)
    report = {
        'purpose': 'K-UI DOA2 startup analysis; no game data modified',
        'title': ip[128:256].decode('ascii', errors='replace').strip(' \0'),
        'product': ip[64:74].decode('ascii', errors='replace').strip(' \0'),
        'version': ip[74:80].decode('ascii', errors='replace').strip(' \0'),
        'region': ip[48:56].decode('ascii', errors='replace').strip(' \0'),
        'session_lba': session, 'boot_lba': match[0], 'boot_bytes': len(boot),
        'ip_crc32': f'{zlib.crc32(ip):08x}',
        'boot_crc32': f'{zlib.crc32(boot):08x}',
        'boot_sha256': hashlib.sha256(boot).hexdigest(),
        'reported_caller_pr': '0x8c012450',
        'caller_offset_at_load_base_8c010000': '0x2450',
    }
    return ip, boot, text, report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gdi', type=Path, help='The same DOA2 GDI selected in K-UI')
    parser.add_argument('--output', type=Path, default=Path('DOA2-startup.zip'))
    args = parser.parse_args()
    try:
        if args.output.exists():
            raise ValueError(f'{args.output} already exists; choose a new --output filename')
        ip, boot, gdi, report = extract(args.gdi)
        with zipfile.ZipFile(args.output, 'x', compression=zipfile.ZIP_DEFLATED) as output:
            output.writestr('IP.BIN', ip)
            output.writestr('1ST_READ.BIN', boot)
            output.writestr('disc.gdi', gdi)
            output.writestr('startup-info.json', json.dumps(report, indent=2) + '\n')
        print(f'Created {args.output.resolve()} ({args.output.stat().st_size:,} bytes)')
        print('Upload this ZIP in the chat. The source GDI and tracks were read only.')
    except (OSError, ValueError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Extraction stopped: {error}\n')


if __name__ == '__main__':
    main()
