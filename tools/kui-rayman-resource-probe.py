#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Collect read-only Rayman 2 FIX.DAT header metadata, without game assets.

Run with SDROOT, or --gdi followed by the full path to an actual .gdi file.
Keep kui-doa2-filesystem-probe.py and kui-native-startup-bundle.py beside this
script. Only the exact reviewed IP/startup is supported. The JSON-only ZIP
contains ISO metadata and 64 integer words from FIX.DAT's first256 bytes.
Pointers are never followed. This does not patch a game or enable compatibility.
"""
import argparse
import hashlib
import importlib.util
import os
from pathlib import Path
import shlex
import struct
import sys
import zipfile

# Import the shared readers without creating bytecode beside their scripts,
# including when somebody runs the collector from a removable source card.
sys.dont_write_bytecode = True
HELPER_PATH = Path(__file__).resolve().with_name('kui-doa2-filesystem-probe.py')
SPEC = importlib.util.spec_from_file_location('kui_rayman_shared_disc_reader', HELPER_PATH)
shared = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(shared)
iso, DATA = shared.iso, shared.DATA
EXPECTED_TITLE = 'RAYMAN 2 THE GREAT ESCAPE'
EXPECTED_IP_SHA256 = '7af200eaa5a84c42dea3df08d90cb0568a3a33b333015502cbd3ddbfa3685d5b'
EXPECTED_BYTES = 935648
EXPECTED_SHA256 = '9e34357880d5fbbca1559ce8797baed66c6b4370e967b2af23f07cba9b3a7099'
HEADER_BYTES = 0x100
RESOURCE_PATH = 'DCDATA/FIX.DAT'
DEFAULT_OUTPUT = Path('Rayman2-FIX-layout-metadata.zip')
MAX_METADATA_BYTES = 2 * 1024 * 1024
MAX_SOURCE_BYTES = 4 * 1024 * 1024
MAX_SCAN_SOURCE_BYTES = 32 * 1024 * 1024


class ReadLimitError(ValueError):
    """The whole bounded scan must stop rather than silently skip a limit."""


class Disc(shared.Disc):
    def __init__(self, gdi, budget=None):
        super().__init__(gdi)
        self.budget = budget if budget is not None else {'source_bytes': 0}
        self.source_bytes = self.metadata_bytes = self.header_reads = 0
        self.boot_verified = False

    def read(self, lba, size, purpose='metadata'):
        if purpose == 'metadata':
            if size <= 0 or self.metadata_bytes + size > MAX_METADATA_BYTES:
                raise ReadLimitError('ISO/IP metadata exceeds the 2 MiB logical read limit')
        elif purpose == 'boot':
            if size != EXPECTED_BYTES or self.reads['boot_bytes']:
                raise ValueError('Only one exact-length startup read is permitted')
        elif purpose == 'resource_header':
            if size != HEADER_BYTES or self.header_reads or not self.boot_verified:
                raise ValueError('Only one verified FIX.DAT prefix read is permitted')
        else:
            raise ValueError('Resource tables, pointer targets and arbitrary asset reads are prohibited')
        source_bytes = sum(count * track['sector_bytes'] for track, _, count in self.extent(lba, size))
        if (self.source_bytes + source_bytes > MAX_SOURCE_BYTES or
                self.budget['source_bytes'] + source_bytes > MAX_SCAN_SOURCE_BYTES):
            raise ReadLimitError('Source read budget exceeded; use --gdi with one actual .gdi file')
        # Charge attempted reads before opening any source. Successful reports
        # have complete reads, so these are also the physical sector byte totals.
        self.source_bytes += source_bytes
        self.budget['source_bytes'] += source_bytes
        if purpose == 'metadata':
            self.metadata_bytes += size
        if purpose == 'resource_header':
            self.header_reads += 1
        return super().read(lba, size, purpose)

    def identity(self):
        ip = self.read(self.session, 32768)
        if ip[:16] != b'SEGA SEGAKATANA ' or ip[37:43] != b'GD-ROM':
            raise ValueError('Native GD IP header is invalid')
        title = ip[128:256].decode('ascii').strip(' \0')
        if title != EXPECTED_TITLE:
            raise shared.OtherDisc('Different IP title')
        flags = ip[56:63].decode('ascii')
        if ip[63] != 32 or any(c not in '0123456789ABCDEFabcdef' for c in flags) or int(flags, 16) & 1:
            raise ValueError('The reviewed Rayman IP must select a native peripheral profile')
        digest = hashlib.sha256(ip).hexdigest()
        if digest != EXPECTED_IP_SHA256:
            raise ValueError('Rayman IP SHA256 differs from the reviewed input')
        return dict(title=title, product=ip[64:74].decode('ascii').strip(' \0'),
                    version=ip[74:80].decode('ascii').strip(' \0'),
                    region=ip[48:56].decode('ascii').strip(' \0'),
                    boot_filename=iso.identifier(ip[96:112].strip(b' \0')), ip_sha256=digest)


def inspect(gdi, budget=None):
    """Verify the disc and exact startup before any asset prefix is read."""
    disc = Disc(gdi, budget)
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
        if (len(directories) > iso.DIRECTORY_COUNT_LIMIT or not 0 < size <= iso.DIR_LIMIT or
                total > MAX_METADATA_BYTES):
            raise ReadLimitError('ISO directory traversal exceeds the bounded limit')
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
                raise ReadLimitError('ISO entry count exceeds the bounded limit')
            if raw_name in (b'\0', b'\1'):
                if flags != 2:
                    raise ValueError('Invalid ISO dot entry')
                continue
            path = prefix + iso.identifier(raw_name)
            if (len(path) > shared.MAX_ISO_PATH or path.count('/') >= shared.MAX_ISO_DEPTH or
                    any(part.rstrip(' .') != part for part in path.split('/')) or path in paths):
                raise ValueError('Unsafe, duplicate or ambiguous ISO path')
            paths.add(path)
            if flags not in (0, 2):
                raise ValueError('Unsupported ISO file flags or multiple extents')
            disc.extent(extent, count)
            entry = dict(path=path, lba=extent, bytes=count, directory=bool(flags & 2))
            files.append(entry)
            if flags == 2:
                if len(directories) + len(pending) >= iso.DIRECTORY_COUNT_LIMIT:
                    raise ReadLimitError('ISO directory count exceeds the bounded limit')
                pending.append((path + '/', extent, count))
            elif path.rsplit('/', 1)[-1] == identity['boot_filename']:
                boot.append(entry)
    if len(boot) != 1:
        raise ValueError('IP-selected boot path is absent or ambiguous')
    entry = boot[0]
    if entry['bytes'] != EXPECTED_BYTES:
        raise ValueError('Rayman startup byte length differs from the reviewed input')
    digest = hashlib.sha256(disc.read(entry['lba'], entry['bytes'], 'boot')).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError('Rayman startup SHA256 differs from the reviewed input')
    resource = [e for e in files if e['path'] == RESOURCE_PATH and not e['directory']]
    if len(resource) != 1 or resource[0]['bytes'] < HEADER_BYTES:
        raise ValueError('Unique DCDATA/FIX.DAT with a complete 256-byte prefix is required')
    disc.boot_verified = True
    report = dict(schema='kui.rayman.fix-header-metadata.v1', extractor_version=1,
                  purpose='Read-only FIX.DAT pointer-layout analysis; no compatibility admission',
                  **identity, boot_path=entry['path'], boot_bytes=entry['bytes'], boot_sha256=digest,
                  gdi_sha256=hashlib.sha256(disc.gdi_bytes).hexdigest(), session_lba=disc.session,
                  tracks=[{k: v for k, v in t.items() if k != 'path'} for t in disc.tracks],
                  entries=sorted(files, key=lambda e: e['path']), scanned_entries=scanned,
                  iso_metadata=dict(primary_lba=disc.session + 16 + index,
                                    primary_sha256=hashlib.sha256(primary).hexdigest(),
                                    directories=directory_metadata))
    return report, disc, resource[0]


def finish(report, disc, entry):
    prefix = disc.resource_range(entry, 0, HEADER_BYTES, 'resource_header')
    words = struct.unpack('<64I', prefix)
    report.update(fix_dat=dict(path=entry['path'], lba=entry['lba'], file_bytes=entry['bytes'],
        prefix_bytes=HEADER_BYTES, prefix_sha256=hashlib.sha256(prefix).hexdigest(),
        words_le=[dict(offset=f'{i*4:04x}', value=f'{v:08x}', unsigned=v) for i, v in enumerate(words)],
        selected_fields={name: dict(offset=f'{offset:04x}', value=f'{words[offset//4]:08x}',
                                   unsigned=words[offset//4]) for name, offset in
                         [('pair_count', 0xa0), ('pair_table_pointer', 0xa4), ('temporary_buffer_pointer', 0xc0)]},
        live_allocation_base=None, pointer_targets_followed=False, table_ranges_read=[]),
        reads=disc.reads, selected_disc_source_bytes=disc.source_bytes,
        scan_source_bytes=disc.budget['source_bytes'], executable_payload_copied=False,
        asset_payload_copied=False, asset_header_integer_bytes_exported=HEADER_BYTES,
        limits=['Exactly the first256 bytes of DCDATA/FIX.DAT are exported as integer observations and a hash.',
                'No pointer is followed, converted to a file offset, or treated as a proved live allocation base.',
                'The game allocation base, live descriptor table and RAM contents are unavailable in this host-only report.',
                'No complete asset, logo image, executable, IP, audio or video payload is exported.',
                'Selected-disc metadata is limited to2MiB logical and all selected-disc source reads to4MiB; the complete scan is limited to32MiB source reads.',
                'Mode1 framing is checked for read sectors; this is not a complete EDC/ECC or disc-integrity proof.',
                'The source tracks are opened read only; this does not patch the game or enable VMU/reader compatibility.'])
    return report


def collect(gdi):
    return finish(*inspect(gdi))


def select(sdroot, output=DEFAULT_OUTPUT):
    root = Path(sdroot)
    if root.is_symlink() or not root.is_dir():
        raise ValueError('SD root must be a directory, not a symlink')
    matches, faults = [], 0
    budget = {'source_bytes': 0}
    seen = candidates = 0
    for directory, folders, names in os.walk(root, followlinks=False):
        seen += 1
        if seen > shared.MAX_HOST_DIRECTORIES:
            raise ReadLimitError('SD directory scan exceeds the bounded limit; use --gdi with a file')
        folders[:] = sorted(n for n in folders if not (Path(directory) / n).is_symlink())
        for name in sorted(names):
            if not name.lower().endswith('.gdi'):
                continue
            candidates += 1
            if candidates > shared.MAX_GDIS:
                raise ReadLimitError('GDI scan exceeds the bounded limit; use --gdi with a file')
            path = Path(directory) / name
            try:
                matches.append((path, inspect(path, budget)))
            except shared.OtherDisc:
                continue
            except ReadLimitError:
                raise
            except (OSError, ValueError, UnicodeError):
                faults += 1
    if len(matches) > 1:
        commands = ['  ' + ' '.join(shlex.quote(a) for a in
                    ['python3', str(Path(__file__).resolve()), '--gdi', str(path), '--output', str(output)])
                    for path, _ in matches]
        raise ValueError('Multiple exact Rayman 2 discs found. Choose one actual .gdi file and run its command:\n' +
                         '\n'.join(commands))
    if not matches:
        raise ValueError(f'No unique reviewed Rayman 2 GDI found ({candidates} descriptors, {faults} unreadable/unsupported); use --gdi with a file')
    path, inspected = matches[0]
    return path, finish(*inspected)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sdroot', type=Path, nargs='?', help='SD root to scan read only; omit --gdi')
    parser.add_argument('--gdi', nargs='?', const='', metavar='DISC.gdi',
                        help='Full path to an actual regular .gdi file, not the SD directory')
    parser.add_argument('--output', type=Path, default=DEFAULT_OUTPUT, help='New computer-side metadata ZIP')
    args = parser.parse_args()
    if args.gdi == '':
        parser.error('--gdi needs the full path to a .gdi file. To scan the SD root, omit --gdi entirely.')
    if bool(args.sdroot) == bool(args.gdi):
        parser.error('Supply exactly one SDROOT or --gdi DISC.gdi')
    args.gdi = Path(args.gdi) if args.gdi else None
    try:
        if os.path.lexists(args.output):
            raise ValueError('Output already exists; choose a new --output filename')
        if args.gdi:
            shared.gdi_file(args.gdi)
        source = (args.sdroot if args.sdroot else args.gdi.parent).resolve()
        try:
            inside = os.path.commonpath((str(source), str(args.output.resolve()))) == str(source)
        except ValueError:
            inside = False
        if inside:
            raise ValueError('Output must be outside the source SD/game folder')
        print('Reading Rayman 2 identity and FIX.DAT header metadata; source tracks will not be modified.', flush=True)
        _, report = ((args.gdi, collect(args.gdi)) if args.gdi else select(args.sdroot, args.output))
        iso.write_bundle(args.output, {}, report)
        print(f'Created {args.output.resolve()}; only256 FIX.DAT header bytes were exported as integers.')
        print('Upload this metadata ZIP only if requested after the hardware test. Live RAM allocation base is not available here.')
    except (OSError, ValueError, UnicodeError, zipfile.BadZipFile) as error:
        parser.exit(1, f'Metadata collection stopped: {error}\n')


if __name__ == '__main__':
    main()
