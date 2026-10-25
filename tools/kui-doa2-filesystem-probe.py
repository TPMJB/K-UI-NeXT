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
        if not 0 < size <= iso.FILE_LIMIT or purpose not in ('metadata', 'boot'):
            raise ValueError('Read exceeds the bounded metadata/startup limit')
        result = bytearray()
        for track, first, count in self.extent(lba, size):
            stride = track['sector_bytes']
            with track['path'].open('rb') as source:
                source.seek((first - track['start_lba']) * stride)
                for _ in range(count):
                    sector = source.read(stride)
                    if len(sector) != stride:
                        raise ValueError('Truncated data sector')
                    if stride == RAW:
                        if sector[:12] != b'\0' + b'\xff' * 10 + b'\0' or sector[15] != 1:
                            raise ValueError('Non-Mode1 raw data sector')
                        sector = sector[16:16 + DATA]
                    result.extend(sector)
        self.reads[purpose + '_bytes'] += size
        return bytes(result[:size])

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


def collect(gdi):
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
    return dict(schema='kui.doa2.filesystem-metadata.v1', extractor_version=1,
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


def select(sdroot, output=Path('DOA2-filesystem-metadata.zip')):
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
                report = collect(path)
            except (OSError, ValueError, UnicodeError) as error:
                faults.append(type(error).__name__)
                continue
            matches.append((path, report))
    if len(matches) > 1:
        commands = []
        for path, _ in matches:
            arguments = ('python3', str(Path(__file__).resolve()), '--gdi', str(path), '--output', str(output))
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
    parser.add_argument('--output', type=Path, default=Path('DOA2-filesystem-metadata.zip'))
    args = parser.parse_args()
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
        print('Reading DOA2 filesystem metadata; source tracks will not be modified.', flush=True)
        gdi, report = (args.gdi, collect(args.gdi)) if args.gdi else select(args.sdroot, args.output)
        # Reuse the existing closed/CRC-checked, atomic non-overwrite ZIP writer.
        # An empty payload mapping ensures the ZIP contains JSON metadata only.
        iso.write_bundle(args.output, {}, report)
        print(f'Created {args.output.resolve()} ({len(report["entries"])} ISO entries; exact boot verified).')
        print('Upload this metadata ZIP for analysis. No game payloads were copied; VMU compatibility is unchanged.')
    except (OSError, ValueError, UnicodeError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Metadata collection stopped: {error}\n')


if __name__ == '__main__':
    main()
