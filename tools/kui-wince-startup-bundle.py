#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a bounded Windows CE startup-analysis ZIP from an original raw GDI.

Usage: python3 kui-wince-startup-bundle.py /path/to/disc.gdi
Reads the existing dump only. Extracts IP.BIN, the IP-selected boot file and external EXEs under the ISO directories. DLL/DRV/LIB candidates
are included within the byte budget; omitted candidates are listed in the
report. Extensions alone do not establish whether a file contains code.
Other game assets and audio tracks are not copied. Requires Python3's standard
library. Creates WinCE-startup.zip in the current folder without overwriting.
This is an analysis bundle, not a patched game or complete disc verification.
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

# The Mode1/GDI reader and atomic publication follow K-UI's independently
# authored Skies startup helper. This script is self-contained for the owner.
RAW, DATA = 2352, 2048
FILE_LIMIT = 12 * 1024 * 1024
TOTAL_LIMIT = 32 * 1024 * 1024
DIR_LIMIT = 1024 * 1024
DIRECTORY_COUNT_LIMIT = 512
SCAN_ENTRY_LIMIT = 32768
FILE_COUNT_LIMIT = 2048
TOTAL_DIR_LIMIT = 16 * 1024 * 1024
RUNTIME_SUFFIXES = ('.EXE', '.DLL', '.DRV', '.LIB')
EXTRACTOR_VERSION = '2'


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


def extract(gdi):
    gdi = Path(gdi)
    if gdi.is_symlink() or not gdi.is_file():
        raise ValueError('GDI must be a regular file, not a symlink')
    if not 0 < gdi.stat().st_size <= 64 * 1024:
        raise ValueError('GDI descriptor must be between1 byte and64 KiB')
    original_gdi = gdi.read_bytes()
    lines = [shlex.split(line) for line in original_gdi.decode('utf-8-sig').splitlines()
             if line.strip()]
    if not lines or len(lines[0]) != 1 or not 1 <= int(lines[0][0]) <= 99:
        raise ValueError('Expected a GDI with1–99 tracks')
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
    if ip[63] != 32 or len(peripheral) != 7 or any(c not in '0123456789ABCDEFabcdef'
                                                                  for c in peripheral):
        raise ValueError('Invalid IP peripheral flags')
    flags = int(peripheral, 16)
    if not flags & 1:
        raise ValueError('Extraction requires a Windows CE IP profile')
    title = ip[128:256].decode('ascii').strip(' \0')
    product = ip[64:74].decode('ascii').strip(' \0')
    boot_name = identifier(ip[96:112].decode('ascii').strip(' \0').encode('ascii'))
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
    root_lba, root_size, root_flags, root_name = file_record(root)
    if root_flags != 2 or root_name != b'\0':
        raise ValueError('Invalid ISO root directory record')
    pending = [('', root_lba, root_size)]
    seen_directories, seen_paths, selected = set(), set(), {}
    inventory = []
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
            extent, file_size, file_flags, raw_name = file_record(entry)
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
            inventory.append({'path': path, 'lba': extent, 'bytes': file_size,
                              'flags': file_flags})
            if file_flags & 2:
                if file_flags != 2:
                    raise ValueError('Unsupported ISO directory flags')
                if len(pending) + len(seen_directories) >= DIRECTORY_COUNT_LIMIT:
                    raise ValueError('ISO directory count exceeds the bounded extraction limit')
                pending.append((path + '/', extent, file_size))
            elif (name == boot_name or
                  name.endswith(RUNTIME_SUFFIXES)):
                if file_flags:
                    raise ValueError('Selected file has unsupported or multi-extent ISO flags')
                if not file_size:
                    raise ValueError(f'{path} is empty')
                selected[path] = (extent, file_size)
    boot_candidates = sorted(path for path in selected
                             if path.rsplit('/', 1)[-1] == boot_name)
    if boot_name in selected:
        boot_path = boot_name
    elif len(boot_candidates) == 1:
        boot_path = boot_candidates[0]
    elif not boot_candidates:
        raise ValueError(f'IP-selected kernel {boot_name} was not found in the ISO directories')
    else:
        raise ValueError('Ambiguous IP-selected kernel paths: ' + ', '.join(boot_candidates))
    application_candidates = sorted(path for path in selected if path.endswith('.EXE'))
    application_status = ('found' if len(application_candidates) == 1 else
                          'ambiguous' if application_candidates else 'not_found')
    track_inventory = [
        {'number': number, 'start_lba': track[0], 'end_lba': track[1],
         'control': track[2], 'sector_bytes': RAW, 'file': track[3].name,
         'file_bytes': (track[1] - track[0]) * RAW}
        for number, track in enumerate(tracks, 1)]
    high_audio = [item['number'] for item in track_inventory
                  if item['control'] == 0 and item['start_lba'] >= 45000]
    low_audio = [item['number'] for item in track_inventory
                 if item['control'] == 0 and item['start_lba'] < 45000]
    warnings = []
    if low_audio:
        warnings.append('Low-density audio tracks ' + ', '.join(map(str, low_audio)) +
                        ' may be warning tracks; their presence does not prove gameplay CDDA usage.')
    if not high_audio and not low_audio:
        warnings.append('No standalone audio tracks are declared by this GDI.')
    if application_status == 'not_found':
        warnings.append('No external EXE was found. Kernel/runtime files and the ISO '
                        'inventory are included to identify the application location.')
    elif application_status == 'ambiguous':
        warnings.append('Multiple external EXEs were found. All are included with their '
                        'original directory paths; the game entry has not been assumed.')
    # The IP-selected boot file and every external EXE are required. Optional dependency candidates
    # must not prevent the useful startup bundle from being produced; .LIB in
    # particular can also name game assets. Keep the full inventory and explicit
    # omissions so the analyst can request a specific file after inspecting it.
    candidate_count = len(selected)
    required = sorted(set(application_candidates + [boot_path]))
    if len(required) > FILE_COUNT_LIMIT:
        raise ValueError(f'Required startup file count is {len(required)}; '
                         f'the extraction limit is {FILE_COUNT_LIMIT}')
    included, omitted = {}, []
    total_bytes = len(ip) + len(original_gdi)
    for path in required:
        extent, size = selected[path]
        if size > FILE_LIMIT:
            raise ValueError(f'Required startup file {path} is {size:,} bytes; '
                             f'the per-file limit is {FILE_LIMIT:,} bytes')
        included[path] = (extent, size)
        total_bytes += size
    if total_bytes > TOTAL_LIMIT:
        raise ValueError(f'Required startup files total {total_bytes:,} bytes; '
                         f'the extraction budget is {TOTAL_LIMIT:,} bytes')
    optional = sorted((path for path in selected if path not in included),
                      key=lambda path: (path.endswith('.LIB'), path))
    for path in optional:
        extent, size = selected[path]
        reason = ('per_file_limit' if size > FILE_LIMIT else
                  'file_count_limit' if len(included) >= FILE_COUNT_LIMIT else
                  'total_byte_budget' if total_bytes + size > TOTAL_LIMIT else None)
        if reason:
            omitted.append({'path': path, 'lba': extent, 'bytes': size, 'reason': reason})
        else:
            included[path] = (extent, size)
            total_bytes += size
    if omitted:
        warnings.append(f'{len(omitted)} optional DLL/DRV/LIB candidates were omitted '
                        'to keep the startup bundle bounded. See omitted_files and '
                        'inventory; extensions alone do not prove runtime code.')
    selected = included
    reserved = {'ip.bin', 'disc.gdi', 'startup-info.json'}
    if any(path.split('/', 1)[0].casefold() in reserved for path in selected):
        raise ValueError('Selected ISO boot/runtime path conflicts with generated bundle metadata')
    entries = {'IP.BIN': ip, 'disc.gdi': original_gdi}
    entries[boot_path] = read(*selected[boot_path])
    for name, extent in sorted(selected.items()):
        if name != boot_path:
            entries[name] = read(*extent)
    # The JSON hashes every extracted file; hashing the JSON itself would be
    # circular. Original directory paths remain visible for dependency analysis.
    file_info = []
    for name, data in entries.items():
        if name == 'IP.BIN':
            lba = session
        elif name == 'disc.gdi':
            lba = None
        else:
            lba = selected[name][0]
        file_info.append({'path': name, 'bytes': len(data), 'lba': lba,
                          'crc32': f'{zlib.crc32(data):08x}',
                          'sha256': hashlib.sha256(data).hexdigest()})
    report = {'extractor_version': EXTRACTOR_VERSION,
              'purpose': 'K-UI Windows CE/CDDA startup analysis; game bytes unchanged',
              'title': title, 'product': product,
              'version': ip[74:80].decode('ascii').strip(' \0'),
              'region': ip[48:56].decode('ascii').strip(' \0'),
              'windows_ce': True, 'peripheral_flags': flags, 'boot_filename': boot_name,
              'boot_path': boot_path, 'boot_candidates': boot_candidates,
              'application_candidates': application_candidates,
              'application_status': application_status, 'warnings': warnings,
              'candidate_file_count': candidate_count,
              'selected_file_count': len(selected), 'archive_file_count': len(selected) + 3,
              'omitted_files': omitted,
              'inventory': inventory,
              'tracks': track_inventory,
              'audio_candidates': {'high_density_tracks': high_audio,
                                   'low_density_tracks': low_audio,
                                   'usage_verified': False,
                                   'note': 'Track layout identifies candidates only, not whether the game uses CDDA.'},
              'audio_payload_copied': False,
              'limits': {'per_file_bytes': FILE_LIMIT, 'selected_payload_bytes': TOTAL_LIMIT,
                         'per_directory_bytes': DIR_LIMIT, 'directory_count': DIRECTORY_COUNT_LIMIT,
                         'scanned_entries': SCAN_ENTRY_LIMIT, 'selected_file_count': FILE_COUNT_LIMIT,
                         'total_directory_bytes': TOTAL_DIR_LIMIT},
              'session_lba': session, 'selected_bytes': total_bytes,
              'directory_count': len(seen_directories), 'scanned_entries': scanned,
              'selection': 'IP, original GDI, IP-selected boot file and all recursive '
                           'external EXEs; optional DLL/DRV/LIB candidates within the '
                           'byte budget; full ISO file inventory and explicit omissions',
              'verification': 'Selected extents and Mode1 framing only; not a complete disc, '
                              'EDC/ECC, runtime dependency or audio compatibility verification',
              'files': file_info}
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
        with tempfile.NamedTemporaryFile(prefix='.WinCE-startup-', suffix='.zip.tmp',
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
    version = f'K-UI Windows CE startup extractor v{EXTRACTOR_VERSION}'
    parser.add_argument('--version', action='version', version=version)
    parser.add_argument('gdi', type=Path, help='Original, unconverted Windows CE raw2352 GDI')
    parser.add_argument('--output', type=Path, default=Path('WinCE-startup.zip'))
    args = parser.parse_args()
    print(version, flush=True)
    try:
        if os.path.lexists(args.output):
            raise ValueError(f'{args.output} already exists; choose a new --output filename')
        entries, report = extract(args.gdi)
        write_bundle(args.output, entries, report)
        print(f'Created {args.output.resolve()} ({args.output.stat().st_size:,} bytes)')
        for warning in report['warnings']:
            print(f'Note: {warning}')
        print('Upload this ZIP when requested for Windows CE startup analysis. Source tracks were read only.')
    except (OSError, ValueError, zipfile.BadZipFile, UnicodeError) as error:
        parser.exit(1, f'Extraction stopped: {error}\n')


if __name__ == '__main__':
    main()
