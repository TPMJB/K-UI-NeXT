#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Create a bounded native or Windows CE CDDA startup-analysis ZIP from GDI/CUE.

Usage: python3 kui-cdda-startup-bundle.py /path/to/disc.cue
Reads the existing dump only. Supports Mode1 raw2352 and cooked2048 DATA
tracks; standalone audio tracks must be raw2352 and are never opened.
CUE supports BINARY files, INDEX 00/01, gaps and density/session REM geometry.
Raw Mode1 addresses anchor CUE DATA LBAs; cooked CUE needs explicit geometry.
Extracts IP.BIN and the IP-selected boot file. Windows CE external EXEs are
required; native EXEs and DLL/DRV/LIB or recognizable sound-firmware BIN
candidates are optional within the byte budget. The report inventories ISO
paths, hashes extracted bytes, and lists omitted candidates. Use --include
ISO/PATH to require another bounded file from that inventory.
Requires only Python3's standard library. Creates CDDA-startup.zip without
overwriting. This collects startup samples for analysis; it does not establish
CDDA use or compatibility, patch a game, or verify a complete disc.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import tempfile
import zipfile
import zlib

# The bounded reader and atomic publication follow K-UI's independently
# authored Windows CE helper. This script is self-contained for the owner.
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
LBA_LIMIT = 719850


def safe_basename(name, label='ISO filename'):
    """Reject ZIP traversal and names ambiguous on common extraction systems."""
    if (not name or name.startswith('.') or '..' in name or name.endswith('.') or
            name != name.strip() or
            any(ord(c) < 32 or ord(c) >= 127 or c in '/\\:<>"|?*' for c in name) or
            name.split('.', 1)[0].upper() in {'CON', 'PRN', 'AUX', 'NUL'} or
            re.fullmatch(r'(?:COM|LPT)[1-9]', name.split('.', 1)[0], re.IGNORECASE)):
        raise ValueError(f'Unsafe {label}')
    return name


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
    return safe_basename(name)


def include_path(path):
    """Resolve an ISO path, never a path in the host filesystem."""
    if not isinstance(path, str) or not path or '\\' in path:
        raise ValueError('--include requires a relative ISO file path from the inventory')
    return '/'.join(identifier(part.encode('ascii')) for part in path.split('/'))


def runtime_candidate(name):
    if name.endswith(RUNTIME_SUFFIXES):
        return True
    if not name.endswith('.BIN'):
        return False
    # Names such as AICA.BIN, ARM7.BIN, SOUND_DRV.BIN and SND_DRV.BIN
    # identify firmware candidates; arbitrary .BIN game assets stay excluded.
    return bool(re.fullmatch(r'(?:AICA|ARM7?|SOUND)(?:[_-]?(?:DRV|DRIVER|FW|FIRMWARE|'
                             r'PROG|PROGRAM|CODE))?|SND[_-](?:DRV|DRIVER|FW|FIRMWARE)',
                             name[:-4]))


def file_record(entry):
    if len(entry) < 34 or entry[0] != len(entry) or not entry[32] or 33 + entry[32] > len(entry):
        raise ValueError('Invalid ISO directory entry')
    if entry[1] or entry[26] or entry[27] or both(entry, 28, 2) != 1:
        raise ValueError('Unsupported ISO extended/interleaved/volume extent')
    return both(entry, 2, 4), both(entry, 10, 4), entry[25], entry[33:33 + entry[32]]


def parse_gdi(gdi, original):
    lines = [shlex.split(line) for line in original.decode('utf-8-sig').splitlines()
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
        if (number != index or start < 0 or control not in (0, 4) or offset or
                stride not in ((RAW, DATA) if control == 4 else (RAW,))):
            raise ValueError('Requires raw2352 or cooked2048 DATA, raw2352 audio, '
                             'and zero GDI file offsets')
        safe_basename(name, 'GDI track filename')
        path = gdi.parent / name
        if path.is_symlink() or not path.is_file():
            raise ValueError('Track must be a regular file, not a symlink')
        length = path.stat().st_size
        if not length or length % stride:
            raise ValueError('Track length is not a whole number of declared sectors')
        end = start + length // stride
        if end > LBA_LIMIT or (tracks and tracks[-1][1] > start):
            raise ValueError('Invalid or overlapping GDI track range')
        tracks.append((start, end, control, path, stride, 0))
    return tracks, [{'geometry': 'GDI absolute LBA'} for _ in tracks]


def cue_frames(value):
    if not re.fullmatch(r'\d{1,4}:\d{2}:\d{2}', value):
        raise ValueError('Invalid CUE MSF timestamp')
    minute, second, frame = map(int, value.split(':'))
    result = minute * 4500 + second * 75 + frame
    if second >= 60 or frame >= 75 or result >= LBA_LIMIT:
        raise ValueError('CUE MSF timestamp is out of range')
    return result


def raw_lba(header):
    """Decode Mode1 address, including GD's extended packed-decimal minutes."""
    if (len(header) < 16 or header[:12] != b'\0' + b'\xff' * 10 + b'\0' or
            header[15] != 1):
        raise ValueError('CUE DATA has a truncated or non-Mode1 raw sector header')
    minute, second, frame = header[12:15]
    if ((minute & 15) > 9 or (second & 15) > 9 or (frame & 15) > 9 or
            (second >> 4) > 5 or (frame >> 4) > 7):
        raise ValueError('Invalid raw Mode1 MSF address')
    minute = (minute >> 4) * 10 + (minute & 15)
    second = (second >> 4) * 10 + (second & 15)
    frame = (frame >> 4) * 10 + (frame & 15)
    lba = minute * 4500 + second * 75 + frame - 150
    if frame >= 75 or not 0 <= lba < LBA_LIMIT:
        raise ValueError('Raw Mode1 MSF address is out of range')
    return lba


def parse_cue(cue, original):
    """Read CUE's file clock separately from disc geometry; never inspect audio."""
    positions, groups = [], []
    group, current = None, None
    session, density, pending_leadin = 1, 0, None
    metadata_commands = {'TITLE', 'PERFORMER', 'SONGWRITER', 'CATALOG', 'ISRC',
                         'FLAGS', 'CDTEXTFILE'}
    for line in original.decode('utf-8-sig').splitlines():
        if any(ord(c) < 32 and c != '\t' for c in line):
            raise ValueError('Control character in CUE descriptor')
        row = shlex.split(line)
        if not row:
            continue
        command = row[0].upper()
        if command == 'FILE':
            if len(row) != 3 or row[2].upper() != 'BINARY':
                raise ValueError('CUE requires FILE basename BINARY')
            if group is not None and (current is None or 'index1' not in current):
                raise ValueError('CUE FILE has no complete track')
            safe_basename(row[1], 'CUE track filename')
            if any(item['name'].casefold() == row[1].casefold() for item in groups):
                raise ValueError('CUE FILE stanzas reuse or alias the same file')
            path = cue.parent / row[1]
            if path.is_symlink() or not path.is_file():
                raise ValueError('CUE track must be a regular file, not a symlink')
            size = path.stat().st_size
            if not size:
                raise ValueError('CUE track file is empty')
            group = {'name': row[1], 'path': path, 'bytes': size, 'tracks': []}
            groups.append(group)
            current = None
        elif command == 'TRACK':
            if (len(row) != 3 or group is None or not row[1].isdigit() or
                    int(row[1]) != len(positions) + 1 or len(positions) >= 99 or
                    (current is not None and 'index1' not in current)):
                raise ValueError('Invalid CUE TRACK sequence')
            modes = {'AUDIO': (0, RAW), 'MODE1/2352': (4, RAW), 'MODE1/2048': (4, DATA)}
            if row[2].upper() not in modes:
                raise ValueError('CUE supports only AUDIO, MODE1/2352 and MODE1/2048')
            control, stride = modes[row[2].upper()]
            current = {'group': group, 'session': session, 'density': density,
                       'control': control, 'stride': stride, 'pregap': 0, 'postgap': 0,
                       'leadin': pending_leadin}
            positions.append(current)
            group['tracks'].append(current)
            pending_leadin = None
        elif command == 'INDEX':
            if (len(row) != 3 or current is None or not row[1].isdigit() or
                    not 0 <= int(row[1]) <= 99):
                raise ValueError('Invalid CUE INDEX')
            index, frame = int(row[1]), cue_frames(row[2])
            if index == 0:
                if 'index0' in current or 'index1' in current:
                    raise ValueError('Duplicate or reversed CUE INDEX 00')
                current['index0'] = frame
            elif index == 1:
                if 'index1' in current or frame < current.get('index0', 0):
                    raise ValueError('Duplicate or reversed CUE INDEX 01')
                current['index1'] = current['last_index'] = frame
                current['last_number'] = 1
            else:
                if ('index1' not in current or frame < current['last_index'] or
                        index <= current['last_number']):
                    raise ValueError('Unordered CUE INDEX')
                current['last_index'], current['last_number'] = frame, index
        elif command in ('PREGAP', 'POSTGAP'):
            key = command.lower()
            if len(row) != 2 or current is None or current.get('have_' + key):
                raise ValueError('Invalid or duplicate CUE gap')
            current[key], current['have_' + key] = cue_frames(row[1]), True
        elif command == 'REM':
            kind = row[1].upper() if len(row) > 1 else ''
            if kind == 'SESSION':
                if (len(row) != 3 or not row[2].isdigit() or
                        not session <= int(row[2]) <= 2):
                    raise ValueError('CUE supports ordered REM SESSION 1/2 only')
                session = int(row[2])
            elif kind in ('SINGLE-DENSITY', 'HIGH-DENSITY'):
                if len(row) != 3 or row[2].upper() != 'AREA':
                    raise ValueError('Invalid CUE density REM')
                next_density = 1 if kind == 'SINGLE-DENSITY' else 2
                if density == 2 and next_density == 1:
                    raise ValueError('CUE density areas are reversed')
                density = next_density
            elif kind in ('LEAD-IN', 'PREGAP'):
                if len(row) != 3:
                    raise ValueError('Invalid CUE lead-in REM')
                pending_leadin = (pending_leadin or 0) + cue_frames(row[2])
                if pending_leadin >= LBA_LIMIT:
                    raise ValueError('CUE lead-in is out of range')
            elif kind == 'LEAD-OUT':
                if len(row) != 3 or current is None or 'leadout' in current:
                    raise ValueError('Invalid CUE lead-out REM')
                current['leadout'] = cue_frames(row[2])
            elif kind == 'KUI' and [part.upper() for part in row[2:]] != ['SCRAMBLED', '0']:
                raise ValueError('Scrambled or unsupported KUI CUE setting')
        elif command not in metadata_commands:
            raise ValueError(f'Unsupported CUE command {command}')
    if (not positions or current is None or 'index1' not in positions[-1] or
            pending_leadin is not None):
        raise ValueError('CUE has an incomplete track or unused lead-in')

    # Determine only program-byte spans first. INDEX 00 bytes belong to a stored
    # pregap and must not become the preceding DATA track's readable tail.
    for item in groups:
        byte_cursor, file_frame = 0, 0
        for index, pos in enumerate(item['tracks']):
            begin = pos.get('index0', pos['index1'])
            if begin < file_frame:
                raise ValueError('Overlapping or reversed CUE file indices')
            skipped = pos['index1'] - file_frame
            offset = byte_cursor + skipped * pos['stride']
            if offset > item['bytes']:
                raise ValueError('CUE INDEX exceeds track file size')
            if index + 1 < len(item['tracks']):
                after = item['tracks'][index + 1]
                end_frame = after.get('index0', after['index1'])
                if 'leadout' in pos:
                    if after['session'] <= pos['session']:
                        raise ValueError('CUE shared-file LEAD-OUT requires a new session')
                    if not pos['index1'] < pos['leadout'] <= end_frame:
                        raise ValueError('CUE shared-file LEAD-OUT is outside its indices')
                    end_frame = pos['leadout']
                sectors = end_frame - pos['index1']
            else:
                if (item['bytes'] - offset) % pos['stride']:
                    raise ValueError('CUE file length is not a whole number of declared sectors')
                sectors = (item['bytes'] - offset) // pos['stride']
            if (sectors <= 0 or pos['last_index'] >= pos['index1'] + sectors or
                    offset + sectors * pos['stride'] > item['bytes']):
                raise ValueError('Invalid CUE track program extent')
            pos.update(offset=offset, sectors=sectors, skipped=skipped)
            byte_cursor, file_frame = offset + sectors * pos['stride'], pos['index1'] + sectors

    tracks, provenance = [], []
    logical, have_hd = 0, False
    for index, pos in enumerate(positions):
        previous = positions[index - 1] if index else None
        new_session = previous is not None and pos['session'] > previous['session']
        separate = previous is not None and pos['group'] is not previous['group']
        explicit_gap = False
        if new_session and separate:
            logical += previous.get('leadout', 6750 if previous['session'] == 1 else 2250)
            logical += pos['leadin'] if pos['leadin'] is not None else 4500
            explicit_gap = 'leadout' in previous or pos['leadin'] is not None
        elif pos['leadin'] is not None:
            raise ValueError('CUE lead-in requires a new session in a separate FILE')
        if previous is not None and 'leadout' in previous and not new_session:
            raise ValueError('CUE LEAD-OUT requires a new session')
        logical += pos['skipped'] + pos['pregap']
        high_boundary = pos['density'] == 2 and (previous is None or previous['density'] != 2)
        if high_boundary:
            if logical > 45000:
                raise ValueError('CUE high-density boundary overlaps previous tracks or gaps')
            logical = 45000
        geometry = 'CUE density/session/index/gap geometry'
        anchor = None
        if pos['control'] == 4 and pos['stride'] == RAW:
            # Header reads are confined to known DATA program bytes, even when
            # one BIN also stores audio. No audio sector is sampled or hashed.
            with pos['group']['path'].open('rb') as source:
                source.seek(pos['offset'])
                anchor = raw_lba(source.read(16))
                for sector_index in sorted(set((min(1, pos['sectors'] - 1), pos['sectors'] - 1))):
                    source.seek(pos['offset'] + sector_index * RAW)
                    if raw_lba(source.read(16)) != anchor + sector_index:
                        raise ValueError('CUE DATA raw MSF addresses disagree with its extent')
            if anchor != logical:
                infer_hd = (anchor == 45000 and logical < 45000 and not have_hd and
                            pos['density'] != 1 and not high_boundary and not explicit_gap)
                initial_anchor = index == 0 and not high_boundary
                if not (infer_hd or initial_anchor):
                    raise ValueError('CUE geometry conflicts with raw Mode1 MSF addresses')
                logical = anchor
                geometry = 'raw Mode1 MSF anchor; CUE relative indices and gaps'
            else:
                geometry += '; checked against raw Mode1 MSF'
        if pos['control'] == 4 and logical >= 45000:
            if pos['stride'] == DATA and pos['density'] != 2 and pos['session'] != 2:
                raise ValueError('Cooked CUE high-density DATA needs explicit density/session geometry')
            have_hd = True
        end = logical + pos['sectors']
        if (logical < 0 or end > LBA_LIMIT or
                (tracks and tracks[-1][1] > logical)):
            raise ValueError('Invalid or overlapping CUE disc track ranges')
        tracks.append((logical, end, pos['control'], pos['group']['path'],
                       pos['stride'], pos['offset']))
        provenance.append({'geometry': geometry, 'session': pos['session'],
                           'density': pos['density'], 'index01_frames': pos['index1'],
                           'index00_frames': pos.get('index0'), 'pregap_frames': pos['pregap'],
                           'postgap_frames': pos['postgap'], 'leadin_frames': pos['leadin'],
                           'leadout_frames': pos.get('leadout'), 'raw_msf_lba': anchor})
        logical = end + pos['postgap']
    if 'leadout' in positions[-1]:
        raise ValueError('CUE final LEAD-OUT has no following session')
    if not have_hd:
        raise ValueError('CUE has no unambiguous GD high-density DATA geometry')
    return tracks, provenance


def extract(gdi, include=()):
    requested = sorted(set(include_path(path) for path in include))
    gdi = Path(gdi)
    descriptor_format = gdi.suffix.lower().lstrip('.')
    if descriptor_format not in ('gdi', 'cue'):
        raise ValueError('Expected a .gdi or .cue descriptor')
    if gdi.is_symlink() or not gdi.is_file():
        raise ValueError('Descriptor must be a regular file, not a symlink')
    if not 0 < gdi.stat().st_size <= 64 * 1024:
        raise ValueError('Descriptor must be between 1 byte and 64 KiB')
    original_gdi = gdi.read_bytes()
    descriptor_name = 'disc.' + descriptor_format
    parser = parse_gdi if descriptor_format == 'gdi' else parse_cue
    tracks, layout_provenance = parser(gdi, original_gdi)
    sessions = [t[0] for t in tracks if t[2] == 4 and t[0] >= 45000]
    if not sessions:
        raise ValueError('No GD high-density data session found')
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
                stride = track[4]
                source.seek(track[5] + (lba - track[0]) * stride)
                for sector_index in range(take):
                    sector = source.read(stride)
                    if len(sector) != stride:
                        raise ValueError('Truncated data sector')
                    if stride == RAW and (sector[:12] != b'\0' + b'\xff' * 10 + b'\0' or
                                          sector[15] != 1):
                        raise ValueError('Truncated or non-Mode1 raw data sector')
                    if (descriptor_format == 'cue' and stride == RAW and
                            raw_lba(sector) != lba + sector_index):
                        raise ValueError('CUE DATA raw MSF address disagrees with the selected extent')
                    result.extend(sector[16:16 + DATA] if stride == RAW else sector)
            lba += take
            count -= take
        return bytes(result[:size])

    ip = read(session, 32768)
    if ip[:16] != b'SEGA SEGAKATANA ' or ip[37:43] != b'GD-ROM':
        raise ValueError('Dreamcast GD IP header not found')
    peripheral = ip[56:63].decode('ascii')
    if ip[63] != 32 or len(peripheral) != 7 or any(c not in '0123456789ABCDEFabcdef'
                                                                  for c in peripheral):
        raise ValueError('Invalid IP peripheral flags')
    flags = int(peripheral, 16)
    windows_ce = bool(flags & 1)
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
            if len(path) > 65535:
                raise ValueError('ISO path exceeds the ZIP filename limit')
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
            elif (name == boot_name or runtime_candidate(name) or path in requested):
                if file_flags:
                    raise ValueError('Selected file has unsupported or multi-extent ISO flags')
                if not file_size:
                    raise ValueError(f'{path} is empty')
                selected[path] = (extent, file_size)
    inventory.sort(key=lambda item: item['path'])
    inventory_files = {item['path']: item for item in inventory if not item['flags'] & 2}
    for path in requested:
        if path not in inventory_files:
            raise ValueError(f'--include ISO file {path} was not found in the inventory')
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
         'control': track[2], 'sector_bytes': track[4], 'file': track[3].name,
         'file_bytes': (track[1] - track[0]) * track[4],
         'file_offset_bytes': track[5], 'source_file_bytes': track[3].stat().st_size,
         'data_encoding': ('raw2352' if track[4] == RAW else 'cooked2048')
                          if track[2] == 4 else None}
        for number, track in enumerate(tracks, 1)]
    data_encodings = sorted(set(item['data_encoding'] for item in track_inventory
                                if item['control'] == 4))
    data_encoding = data_encodings[0] if len(data_encodings) == 1 else 'mixed'
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
    if windows_ce and application_status == 'not_found':
        warnings.append('No external EXE was found. Kernel/runtime files and the ISO '
                        'inventory are included to identify the application location.')
    elif windows_ce and application_status == 'ambiguous':
        warnings.append('Multiple external EXEs were found. All are included with their '
                        'original directory paths; the game entry has not been assumed.')
    # CE external EXEs are required; native executables besides the IP-selected
    # boot file are optional. Explicit ISO requests are required in either case.
    # .LIB in particular can also name assets, so keep optional candidates bounded
    # and report omissions rather than broadening collection to whole game assets.
    candidate_count = len(selected)
    required = sorted(set([boot_path] + requested +
                          (application_candidates if windows_ce else [])))
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
        warnings.append(f'{len(omitted)} optional runtime/firmware candidates were omitted '
                        'to keep the startup bundle bounded. See omitted_files and '
                        'inventory; extensions alone do not prove runtime code.')
    selected = included
    omitted.sort(key=lambda item: item['path'])
    reserved = {'ip.bin', 'disc.gdi', 'disc.cue', 'startup-info.json'}
    if any(path.split('/', 1)[0].casefold() in reserved for path in selected):
        raise ValueError('Selected ISO boot/runtime path conflicts with generated bundle metadata')
    entries = {'IP.BIN': ip, descriptor_name: original_gdi}
    for name, extent in sorted(selected.items()):
        entries[name] = read(*extent)
    entries = dict(sorted(entries.items()))
    # The JSON hashes every extracted file; hashing the JSON itself would be
    # circular. Original directory paths remain visible for dependency analysis.
    file_info = []
    for name, data in sorted(entries.items()):
        if name == 'IP.BIN':
            lba = session
        elif name == descriptor_name:
            lba = None
        else:
            lba = selected[name][0]
        file_info.append({'path': name, 'bytes': len(data), 'lba': lba,
                          'crc32': f'{zlib.crc32(data):08x}',
                          'sha256': hashlib.sha256(data).hexdigest()})
    report = {'extractor_version': EXTRACTOR_VERSION,
              'purpose': 'K-UI native/Windows CE CDDA startup analysis; game bytes unchanged',
              'descriptor_format': descriptor_format, 'descriptor_file': descriptor_name,
              'source_descriptor': gdi.name, 'layout_provenance': layout_provenance,
              'title': title, 'product': product,
              'version': ip[74:80].decode('ascii').strip(' \0'),
              'region': ip[48:56].decode('ascii').strip(' \0'),
              'windows_ce': windows_ce, 'peripheral_flags': flags, 'boot_filename': boot_name,
              'data_encoding': data_encoding,
              'boot_path': boot_path, 'boot_candidates': boot_candidates,
              'application_candidates': application_candidates,
              'application_status': application_status, 'warnings': warnings,
              'candidate_file_count': candidate_count,
              'selected_file_count': len(selected), 'archive_file_count': len(selected) + 3,
              'explicitly_requested_files': requested, 'required_files': required,
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
              'selection': 'IP, original descriptor and IP-selected boot file; all recursive '
                           'external EXEs required for Windows CE; optional native EXEs, '
                           'DLL/DRV/LIB and recognizable sound-firmware BIN candidates '
                           'within the byte budget; explicit ISO file requests required; '
                           'full ISO inventory and explicit omissions',
              'verification': 'Selected extents and raw Mode1 framing only; not a complete disc, '
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
        with tempfile.NamedTemporaryFile(prefix='.CDDA-startup-', suffix='.zip.tmp',
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
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    version = f'K-UI CDDA startup extractor v{EXTRACTOR_VERSION}'
    parser.add_argument('--version', action='version', version=version)
    parser.add_argument('gdi', type=Path, metavar='descriptor',
                        help='Native or Windows CE .gdi/.cue with raw2352/cooked2048 DATA tracks')
    parser.add_argument('--output', type=Path, default=Path('CDDA-startup.zip'),
                        help='New ZIP filename (default: CDDA-startup.zip; never overwritten)')
    parser.add_argument('--include', action='append', default=[], metavar='ISO/PATH',
                        help='Require a file from the reported ISO inventory; repeatable, '
                             'subject to the same byte/count limits; no host filesystem paths')
    args = parser.parse_args()
    print(version, flush=True)
    try:
        if os.path.lexists(args.output):
            raise ValueError(f'{args.output} already exists; choose a new --output filename')
        entries, report = extract(args.gdi, args.include)
        write_bundle(args.output, entries, report)
        print(f'Created {args.output.resolve()} ({args.output.stat().st_size:,} bytes)')
        for warning in report['warnings']:
            print(f'Note: {warning}')
        print('Upload this ZIP when requested for CDDA startup analysis. Source tracks were read only.')
    except (OSError, ValueError, zipfile.BadZipFile, UnicodeError) as error:
        parser.exit(1, f'Extraction stopped: {error}\n')


if __name__ == '__main__':
    main()
