#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect read-only filesystem metadata for the reviewed DOA2 startup.

Usage: python3 kui-doa2-filesystem-probe.py SDROOT
   or: python3 kui-doa2-filesystem-probe.py --gdi /path/to/disc.gdi
Keep kui-native-startup-bundle.py beside this script. Data tracks may contain
2048-byte cooked sectors or 2352-byte original Mode1 sectors. The output ZIP
contains JSON metadata only: no executable, IP, audio or asset payload bytes.
This is an analysis probe; it does not enable VMU emulation or claim compatibility.
"""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shlex
import struct
import zipfile

HELPER_PATH = Path(__file__).resolve().with_name('kui-native-startup-bundle.py')
SPEC = importlib.util.spec_from_file_location('kui_native_metadata_parser', HELPER_PATH)
iso = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(iso)
DATA, RAW = 2048, 2352
EXPECTED_TITLE = 'DEAD OR ALIVE 2'
EXPECTED_BYTES = 1815864
EXPECTED_SHA256 = '105056d9ffef86bf5e470fa28dc5907443156f9ab1c6bc009f73661d9c047a85'
MAX_GDIS, MAX_HOST_DIRECTORIES = 4096, 16384
MAX_ISO_PATH, MAX_ISO_DEPTH = 1024, 32
MAX_RESOURCE_FILES = 128
MAX_RESOURCE_POINTERS, MAX_RESOURCE_RECORDS = 4096, 4096
MAX_RESOURCE_TABLE_BYTES = 65536
MAX_RESOURCE_METADATA_BYTES = 2 * 1024 * 1024
MAX_RESOURCE_SOURCE_BYTES = 4 * 1024 * 1024
RESOURCE_LAYOUT_SCHEMA = 'kui.doa2.chr-layout-observation.v1'


class OtherDisc(ValueError):
    """An otherwise readable IP explicitly selects a different title."""


def gdi_file(gdi):
    path = Path(gdi)
    if path.is_symlink():
        raise ValueError('GDI must be a regular .gdi file; symlinks are not supported')
    if path.is_dir():
        raise ValueError('--gdi requires a regular .gdi file, not a directory. '
                         'Pass the SD root without --gdi to scan it, or give --gdi the full path to disc.gdi')
    if not path.is_file():
        raise ValueError('GDI must be an existing regular .gdi file')
    return path


class Disc:
    def __init__(self, gdi):
        self.gdi = gdi_file(gdi)
        if not 0 < self.gdi.stat().st_size <= 65536:
            raise ValueError('GDI descriptor size is invalid')
        self.gdi_bytes = self.gdi.read_bytes()
        rows = [shlex.split(line) for line in self.gdi_bytes.decode('utf-8-sig').splitlines() if line.strip()]
        if not rows or len(rows[0]) != 1 or not 1 <= int(rows[0][0]) <= 99 or len(rows) != int(rows[0][0]) + 1:
            raise ValueError('GDI track count is invalid')
        self.tracks = []
        for number, row in enumerate(rows[1:], 1):
            if len(row) != 6:
                raise ValueError('Malformed GDI track row')
            index, start, control, stride = map(int, row[:4])
            name, offset = row[4], int(row[5])
            if (index != number or start < 0 or control not in (0, 4) or
                    stride not in (DATA, RAW) or (control == 0 and stride != RAW) or offset or
                    any(c in name for c in '/\\:') or name in ('', '.', '..')):
                raise ValueError('Unsupported GDI geometry, filename or file offset')
            path = self.gdi.parent / name
            if path.is_symlink() or not path.is_file():
                raise ValueError('Track must be a regular file, not a symlink')
            size = path.stat().st_size
            if not size or size % stride:
                raise ValueError('Track is not a whole number of sectors')
            end = start + size // stride
            if end > 719850 or (self.tracks and self.tracks[-1]['end_lba'] > start):
                raise ValueError('GDI range is invalid or overlapping')
            self.tracks.append(dict(number=number, start_lba=start, end_lba=end,
                                    control=control, sector_bytes=stride, file=name, file_bytes=size, path=path))
        sessions = [t['start_lba'] for t in self.tracks if t['control'] == 4 and t['start_lba'] >= 45000]
        if not sessions:
            raise ValueError('No high-density GD data session')
        self.session = sessions[0]
        self.reads = dict(metadata_bytes=0, boot_bytes=0, asset_payload_bytes=0)

    def extent(self, lba, size):
        if size < 0 or size > 0xffffffff:
            raise ValueError('ISO file size is invalid')
        if not size:
            if lba and not self.session <= lba <= 719850:
                raise ValueError('Empty ISO extent leaves the GD address range')
            return []
        if lba < self.session:
            raise ValueError('ISO extent leaves the high-density data session')
        count = (size + DATA - 1) // DATA
        parts = []
        while count:
            track = next((t for t in self.tracks if t['start_lba'] <= lba < t['end_lba']), None)
            if not track or track['control'] != 4:
                raise ValueError('ISO extent leaves the data tracks')
            take = min(count, track['end_lba'] - lba)
            parts.append((track, lba, take))
            lba += take
            count -= take
        return parts

    def read(self, lba, size, purpose='metadata'):
        resource = purpose in ('resource_header', 'resource_table')
        if not 0 < size <= iso.FILE_LIMIT or purpose not in ('metadata', 'boot', 'resource_header', 'resource_table'):
            raise ValueError('Read exceeds the bounded metadata/startup limit')
        parts = self.extent(lba, size)
        if resource:
            requested = self.reads.get('resource_header_bytes', 0) + self.reads.get('resource_table_bytes', 0)
            source_bytes = sum(count * track['sector_bytes'] for track, _, count in parts)
            if (size > MAX_RESOURCE_TABLE_BYTES + DATA or requested + size > MAX_RESOURCE_METADATA_BYTES or
                    self.reads.get('resource_source_bytes', 0) + source_bytes > MAX_RESOURCE_SOURCE_BYTES):
                raise ValueError('Resource metadata global read limit exceeded')
            # Charge attempted logical ranges before opening tracks, including
            # a partial-sector prefix. Physical bytes are counted as read.
            self.reads[purpose + '_bytes'] = self.reads.get(purpose + '_bytes', 0) + size
        result = bytearray()
        for track, first, count in parts:
            stride = track['sector_bytes']
            with track['path'].open('rb') as source:
                source.seek((first - track['start_lba']) * stride)
                for _ in range(count):
                    sector = source.read(stride)
                    if resource:
                        self.reads['resource_source_bytes'] = self.reads.get('resource_source_bytes', 0) + len(sector)
                    if len(sector) != stride:
                        raise ValueError('Truncated data sector')
                    if stride == RAW:
                        if sector[:12] != b'\0' + b'\xff' * 10 + b'\0' or sector[15] != 1:
                            raise ValueError('Non-Mode1 raw data sector')
                        sector = sector[16:16 + DATA]
                    result.extend(sector)
        if not resource:
            self.reads[purpose + '_bytes'] += size
        return bytes(result[:size])

    def resource_range(self, entry, offset, size, purpose):
        """Read only a proved in-file metadata range, preserving sector framing."""
        if (purpose not in ('resource_header', 'resource_table') or entry['directory'] or
                offset < 0 or size <= 0 or offset > entry['bytes'] or size > entry['bytes'] - offset):
            raise ValueError('Resource metadata range leaves its ISO file')
        prefix = offset % DATA
        data = self.read(entry['lba'] + offset // DATA, prefix + size, purpose)
        return data[prefix:prefix + size]

    def identity(self):
        ip = self.read(self.session, 32768)
        if ip[:16] != b'SEGA SEGAKATANA ' or ip[37:43] != b'GD-ROM':
            raise ValueError('Native GD IP header is invalid')
        title = ip[128:256].decode('ascii').strip(' \0')
        if title != EXPECTED_TITLE:
            raise OtherDisc('Different IP title')
        flags = ip[56:63].decode('ascii')
        if ip[63] != 32 or any(c not in '0123456789ABCDEFabcdef' for c in flags):
            raise ValueError('IP peripheral flags are invalid')
        if int(flags, 16) & 1:
            raise ValueError('Windows CE is not the reviewed native startup')
        return dict(title=title, product=ip[64:74].decode('ascii').strip(' \0'),
                    version=ip[74:80].decode('ascii').strip(' \0'),
                    boot_filename=iso.identifier(ip[96:112].strip(b' \0')),
                    ip_sha256=hashlib.sha256(ip).hexdigest())


def resource_layouts(disc, files):
    """Observe the five-word view used by original CHR consumers.

    This exports integer metadata, never models/vertices/textures or a runtime
    format admission. A header is retained even when its table view is unsafe
    or unsupported. No range follows an unchecked offset or count.
    """
    candidates = sorted((entry for entry in files if not entry['directory'] and
                         entry['path'].startswith('DOA2/') and entry['path'].endswith('.CHR')),
                        key=lambda entry: entry['path'])
    result = dict(schema=RESOURCE_LAYOUT_SCHEMA, files=len(candidates), available=0,
                  unavailable=0, selected_field_bytes=0, entries=[])
    if len(candidates) > MAX_RESOURCE_FILES:
        result.update(status='unavailable', unavailable=len(candidates),
                      reason='Resource file count exceeds the bounded limit')
        return result
    for entry in candidates:
        row = dict(path=entry['path'], lba=entry['lba'], file_bytes=entry['bytes'], status='unavailable')
        result['entries'].append(row)
        try:
            if entry['bytes'] < 20:
                raise ValueError('CHR file is shorter than the 20-byte header view')
            header = disc.resource_range(entry, 0, 20, 'resource_header')
            result['selected_field_bytes'] += len(header)
            data_offset, pointer_offset, record_offset, pointers, records = struct.unpack('<5I', header)
            row.update(header_words=[f'{word:08x}' for word in struct.unpack('<5I', header)],
                       header_sha256=hashlib.sha256(header).hexdigest(),
                       data_offset=data_offset, pointer_table_offset=pointer_offset,
                       record_table_offset=record_offset, pointer_count=pointers, record_count=records)
            if (data_offset < 20 or data_offset & 3 or data_offset > entry['bytes'] or
                    pointers > MAX_RESOURCE_POINTERS or records > MAX_RESOURCE_RECORDS):
                raise ValueError('Unsupported CHR header view or count exceeds the bounded limit')
            ranges = [('pointer_table', pointer_offset, pointers, 4), ('record_table', record_offset, records, 12)]
            if sum(count * stride for _, _, count, stride in ranges) > MAX_RESOURCE_TABLE_BYTES:
                raise ValueError('CHR table metadata exceeds the per-file byte limit')
            occupied = []
            for name, offset, count, stride in ranges:
                length = count * stride
                if count and (offset < 20 or offset & 3 or offset > entry['bytes'] or length > entry['bytes'] - offset):
                    raise ValueError(f'{name} is unaligned or leaves its CHR file')
                if count:
                    if any(offset < end and start < offset + length for start, end in occupied):
                        raise ValueError('CHR metadata tables overlap')
                    occupied.append((offset, offset + length))
            # Validate BOTH table ranges before reading EITHER one.
            for name, offset, count, stride in ranges:
                length = count * stride
                raw = disc.resource_range(entry, offset, length, 'resource_table') if length else b''
                result['selected_field_bytes'] += len(raw)
                words = struct.unpack('<' + 'I' * (len(raw) // 4), raw)
                row[name] = dict(offset=offset, entries=count, stride=stride,
                                 bytes=length, sha256=hashlib.sha256(raw).hexdigest(),
                                 words=[f'{word:08x}' for word in words])
            row['status'] = 'observed'
            result['available'] += 1
        except (OSError, ValueError, UnicodeError) as error:
            row['reason'] = str(error)
            result['unavailable'] += 1
    result['status'] = 'observed' if not result['unavailable'] else 'partial'
    return result


def collect(gdi, *, include_resource_layouts=False):
    disc = Disc(gdi)
    identity = disc.identity()
    primary = None
    for index in range(16):
        descriptor = disc.read(disc.session + 16 + index, DATA)
        if descriptor[1:7] != b'CD001\1':
            raise ValueError('Invalid ISO volume descriptor')
        if descriptor[0] == 1:
            primary = descriptor
            break
        if descriptor[0] == 255:
            break
    if primary is None or iso.both(primary, 128, 2) != DATA:
        raise ValueError('No supported ISO primary descriptor')
    root_lba, root_size, flags, name = iso.file_record(primary[156:190])
    if flags != 2 or name != b'\0':
        raise ValueError('Invalid ISO root directory')
    pending = [('', root_lba, root_size)]
    directories, paths, files, boot = set(), set(), [], []
    directory_metadata = []
    scanned = total = 0
    while pending:
        prefix, lba, size = pending.pop(0)
        if (lba, size) in directories:
            raise ValueError('ISO directory cycle or aliased extent')
        directories.add((lba, size));total += size
        if len(directories) > iso.DIRECTORY_COUNT_LIMIT or not 0 < size <= iso.DIR_LIMIT or total > iso.TOTAL_DIR_LIMIT:
            raise ValueError('ISO directory traversal exceeds the bounded limit')
        data = disc.read(lba, size)
        directory_metadata.append(dict(path=prefix.rstrip('/') or '/', lba=lba, bytes=size,
                                       sha256=hashlib.sha256(data).hexdigest()))
        position = 0
        while position < size:
            length = data[position]
            if not length:
                position = (position // DATA + 1) * DATA
                continue
            if length < 34 or length > min(size - position, DATA - position % DATA):
                raise ValueError('ISO record crosses its directory or sector boundary')
            extent, count, flags, raw_name = iso.file_record(data[position:position + length])
            position += length;scanned += 1
            if scanned > iso.SCAN_ENTRY_LIMIT:
                raise ValueError('ISO entry count exceeds the bounded limit')
            if raw_name in (b'\0', b'\1'):
                if flags != 2:
                    raise ValueError('Invalid ISO dot entry')
                continue
            path = prefix + iso.identifier(raw_name)
            if (len(path) > MAX_ISO_PATH or path.count('/') >= MAX_ISO_DEPTH or
                    any(part.rstrip(' .') != part for part in path.split('/')) or path in paths):
                raise ValueError('Unsafe, duplicate or ambiguous ISO path')
            paths.add(path)
            if flags not in (0, 2):
                raise ValueError('Unsupported ISO file flags or multiple extents')
            disc.extent(extent, count)  # Validate every asset's geometry without reading its payload.
            files.append(dict(path=path, lba=extent, bytes=count, directory=bool(flags & 2)))
            if flags == 2:
                if len(directories) + len(pending) >= iso.DIRECTORY_COUNT_LIMIT:
                    raise ValueError('ISO directory count exceeds the bounded limit')
                pending.append((path + '/', extent, count))
            elif path.rsplit('/', 1)[-1] == identity['boot_filename']:
                boot.append((path, extent, count))
    if len(boot) != 1:
        raise ValueError('IP-selected boot path is absent or ambiguous')
    path, lba, count = boot[0]
    if count != EXPECTED_BYTES:
        raise ValueError('DOA2 startup byte length differs from the reviewed input')
    digest = hashlib.sha256(disc.read(lba, count, 'boot')).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError('DOA2 startup SHA256 differs from the reviewed input')
    report = dict(schema='kui.doa2.filesystem-metadata.v1', extractor_version=1,
                purpose='Read-only DOA2 memory-partition analysis; no compatibility admission',
                **identity, boot_path=path, boot_bytes=count, boot_sha256=digest,
                gdi_sha256=hashlib.sha256(disc.gdi_bytes).hexdigest(), session_lba=disc.session,
                tracks=[{key: value for key, value in t.items() if key != 'path'} for t in disc.tracks],
                entries=sorted(files, key=lambda row: row['path']), directory_count=len(directories),
                iso_metadata=dict(primary_lba=disc.session+16+index,
                                  primary_sha256=hashlib.sha256(primary).hexdigest(),
                                  directories=directory_metadata),
                scanned_entries=scanned, resource_header_fields=[], reads=disc.reads,
                reads_scope='Selected-disc collection only; automatic-selection IP probes excluded',
                asset_payload_copied=False, executable_payload_copied=False,
                limits=['All ISO paths, extents and byte lengths are metadata only.',
                        'No resource header fields have yet been selected for a reviewed format.',
                        'Only IP/ISO metadata and the exact boot file are read; no asset or audio payload is read.',
                        'Mode1 framing is checked when metadata/startup sectors are read; no full EDC/ECC or disc integrity proof.',
                        'This report does not reserve RAM, patch a game, or enable the reader/VMU.'])
    if include_resource_layouts:
        layouts = resource_layouts(disc, files)
        report.update(schema='kui.doa2.filesystem-resource-layouts.v2', extractor_version=2,
                      resource_layouts=layouts,
                      resource_header_fields=['data_offset', 'pointer_table_offset', 'record_table_offset',
                                              'pointer_count', 'record_count'],
                      asset_metadata_requested=True,
                      asset_metadata_collected=bool(layouts['selected_field_bytes']),
                      reads_scope='Selected-disc metadata/startup and bounded CHR layout fields; automatic-selection IP probes excluded',
                      limits=['ISO paths and file lengths remain metadata, not a complete disc integrity proof.',
                              'Opt-in CHR observation reads 20-byte headers and bounded in-file integer tables only.',
                              'Header words are preserved when an unknown or unsafe table layout is unavailable.',
                              'Resource read byte counters include attempted logical sector prefixes; resource_source_bytes counts actual track bytes read.',
                              'No model, vertex, texture, audio or executable payload is exported; output consists of integer metadata and hashes.',
                              'Observed layout is not a runtime format admission, decoded-access proof, RAM reservation or compatibility result.'])
    return report


def select(sdroot, output=Path('DOA2-filesystem-metadata.zip'), *, include_resource_layouts=False):
    root = Path(sdroot)
    if root.is_symlink() or not root.is_dir():
        raise ValueError('SD root must be a directory, not a symlink')
    matches, faults = [], []
    seen = candidates = 0
    for directory, folders, names in os.walk(root, followlinks=False):
        seen += 1
        if seen > MAX_HOST_DIRECTORIES:
            raise ValueError('SD directory scan exceeds the bounded limit; use --gdi')
        folders[:] = sorted(name for name in folders if not (Path(directory) / name).is_symlink())
        for name in sorted(names):
            if not name.lower().endswith('.gdi'):
                continue
            candidates += 1
            if candidates > MAX_GDIS:
                raise ValueError('GDI scan exceeds the bounded limit; use --gdi')
            path = Path(directory) / name
            try:
                # A matching title is necessary, then exact boot identity and
                # the complete filesystem traversal independently select it.
                Disc(path).identity()
            except OtherDisc:
                continue
            except (OSError, ValueError, UnicodeError) as error:
                faults.append(type(error).__name__)
                continue
            try:
                report = collect(path, include_resource_layouts=include_resource_layouts)
            except (OSError, ValueError, UnicodeError) as error:
                faults.append(type(error).__name__)
                continue
            matches.append((path, report))
    if len(matches) > 1:
        commands = []
        for path, _ in matches:
            arguments = ['python3', str(Path(__file__).resolve()), '--gdi', str(path), '--output', str(output)]
            if include_resource_layouts:
                arguments.append('--resource-layouts')
            commands.append('  ' + ' '.join(shlex.quote(argument) for argument in arguments))
        raise ValueError('Multiple exact DOA2 discs found. Choose one .gdi file and run its command:\n' +
                         '\n'.join(commands))
    if not matches:
        raise ValueError(f'No unique reviewed DOA2 GDI found ({candidates} descriptors, {len(faults)} unreadable/unsupported); use --gdi')
    return matches[0]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdroot', type=Path, nargs='?', help='SD root to search read only')
    parser.add_argument('--gdi', type=Path, help='Explicit original or cooked GDI instead of scanning')
    parser.add_argument('--output', type=Path, help='Computer-side ZIP; default depends on --resource-layouts')
    parser.add_argument('--resource-layouts', action='store_true',
                        help='Also observe bounded CHR header/table fields needed for retained-memory analysis')
    args = parser.parse_args()
    if args.output is None:
        args.output = Path('DOA2-resource-layout-metadata.zip' if args.resource_layouts else 'DOA2-filesystem-metadata.zip')
    if bool(args.sdroot) == bool(args.gdi):
        parser.error('Supply exactly one SDROOT or --gdi')
    try:
        if os.path.lexists(args.output):
            raise ValueError('Output already exists; choose a new --output filename')
        if args.gdi:
            gdi_file(args.gdi)
        # Keep publication out of the tree being inspected. In explicit-GDI
        # mode this protects its source folder; choose a computer-side output
        # path rather than another folder on the card.
        source = (args.sdroot if args.sdroot else args.gdi.parent).resolve()
        try:
            inside = os.path.commonpath((str(source), str(args.output.resolve()))) == str(source)
        except ValueError:  # Different Windows drives cannot overlap.
            inside = False
        if inside:
            raise ValueError('Output must be outside the source SD/game folder')
        print('Reading DOA2 filesystem' + (' and bounded CHR layout' if args.resource_layouts else '') +
              ' metadata; source tracks will not be modified.', flush=True)
        gdi, report = ((args.gdi, collect(args.gdi, include_resource_layouts=args.resource_layouts))
                       if args.gdi else select(args.sdroot, args.output, include_resource_layouts=args.resource_layouts))
        # Reuse the existing closed/CRC-checked, atomic non-overwrite ZIP writer.
        # An empty payload mapping ensures the ZIP contains JSON metadata only.
        iso.write_bundle(args.output, {}, report)
        print(f'Created {args.output.resolve()} ({len(report["entries"])} ISO entries; exact boot verified).')
        print('Upload this metadata ZIP for analysis. ' +
              ('Only bounded CHR layout integers and hashes were exported; ' if args.resource_layouts else 'No game payloads were copied; ') +
              'VMU compatibility is unchanged.')
    except (OSError, ValueError, UnicodeError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Metadata collection stopped: {error}\n')


if __name__ == '__main__':
    main()
