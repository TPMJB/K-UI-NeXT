#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle the startup files of every Dreamcast GD image below a folder.

Usage: python3 kui-library-startup-bundle.py /path/to/Games --output-dir OUT
Keep this script beside kui-cdda-startup-bundle.py, which reads each image:
.gdi (raw2352 or cooked2048 data tracks) and Redump-style .cue/.bin. Native
and Windows CE games, with or without CD audio, are all bundled: IP.BIN, the
IP-selected boot file, Windows CE EXEs, small runtime/sound-driver files and
the disc's file listing (names and sizes only). No audio, movies or other game
assets are copied. Startups listed in already-sent.json beside this script are
not copied again. Other image formats are listed by name. Writes
Library-startup-01.zip, -02.zip ... of at most --part-mb MiB each, without
overwriting. Requires only Python3's standard library. Source tracks are read
only. This prepares analysis evidence, not a game patch or a compatibility
claim.
"""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import tempfile
import uuid
import zipfile

LIBRARY_VERSION = '2'
SCHEMA = 'kui.library-startup.v2'
DESCRIPTOR_LIMIT = 4096
WALK_LIMIT = 1000000
INNER_LIMIT = 48 * 1024 * 1024
OPTIONAL_BUDGET = 4 * 1024 * 1024
DESCRIPTORS = ('.gdi', '.cue')
OTHER_FORMATS = ('.cdi', '.chd', '.iso', '.ccd', '.mds', '.zso', '.nrg', '.cso', '.bin', '.raw')
TRACK_FORMATS = ('.iso', '.bin', '.raw')
SLUG = re.compile(r'[0-9]{3,4}-[A-Za-z0-9-]{0,56}')
SHA256 = re.compile(r'[0-9a-f]{64}')
KNOWN_NAME = 'already-sent.json'


def load_extractor():
    path = Path(__file__).resolve().with_name('kui-cdda-startup-bundle.py')
    if not path.is_file():
        raise SystemExit(f'{path.name} must be in the same folder as this script')
    spec = importlib.util.spec_from_file_location('kui_disc_startup_bundle', path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    return helper


disc = load_extractor()


def compression():
    try:
        import lzma  # noqa: F401  Python builds without liblzma fall back.
        return zipfile.ZIP_LZMA
    except ImportError:
        return zipfile.ZIP_DEFLATED


def find_images(root):
    """Sorted descriptors and other image files; symlinked folders are not followed.

    Loose .iso/.bin/.raw files beside a descriptor are that descriptor's
    tracks and are not listed again."""
    descriptors, others, visited = [], [], 0
    for folder, directories, files in os.walk(root):
        directories.sort()
        visited += len(directories) + len(files)
        if visited > WALK_LIMIT:
            raise ValueError(f'More than {WALK_LIMIT:,} entries below {root}; choose the games folder')
        names = sorted(name for name in files if not name.startswith('._'))  # macOS sidecars
        has_descriptor = any(Path(name).suffix.lower() in DESCRIPTORS for name in names)
        for name in names:
            path = Path(folder) / name
            suffix = path.suffix.lower()
            if suffix in DESCRIPTORS:
                descriptors.append(path)
            elif suffix in TRACK_FORMATS:
                # Beside a descriptor these are its tracks. Alone, only an
                # .iso can be a whole image; loose .bin/.raw are not listed.
                if suffix == '.iso' and not has_descriptor:
                    others.append(path)
            elif suffix in OTHER_FORMATS:
                others.append(path)
        if len(descriptors) > DESCRIPTOR_LIMIT:
            raise ValueError(f'More than {DESCRIPTOR_LIMIT:,} GDI/CUE files below {root}')
    return descriptors, others


def ip_fields(ip):
    def text(start, end):
        return ip[start:end].decode('ascii', 'replace').strip(' \0')
    return {'title': text(128, 256), 'product': text(64, 74), 'version': text(74, 80),
            'region': text(48, 56), 'disc': text(43, 48)}


def peek_ip(descriptor):
    """Title fields for a row whose extraction failed; best effort only."""
    original = descriptor.read_bytes()
    if len(original) > 64 * 1024:
        return {}
    parser = disc.parse_gdi if descriptor.suffix.lower() == '.gdi' else disc.parse_cue
    tracks, _ = parser(descriptor, original)
    for start, _end, control, path, stride, offset in tracks:
        if control != 4 or start < 45000:
            continue
        with path.open('rb') as source:
            source.seek(offset)
            sector = source.read(stride)
        ip = sector[16:16 + disc.DATA] if stride == disc.RAW else sector
        if ip[:16] != b'SEGA SEGAKATANA ':
            return {}
        return ip_fields(ip)
    return {}


def slug(index, product, title):
    words = re.sub(r'[^A-Za-z0-9]+', '-', f'{product}-{title}').strip('-')[:56].strip('-')
    value = f'{index:03d}-{words}'
    if not SLUG.fullmatch(value):
        raise ValueError('internal slug error')
    return value


def trim_optional(entries, report):
    """Keep required files; optional runtime candidates up to a small budget, no .LIB."""
    required = set(report['required_files']) | {'IP.BIN', 'disc.gdi', 'disc.cue'}
    kept, dropped, budget = {}, [], 0
    files = {item['path']: item for item in report['files']}
    for name, data in entries.items():
        if name in required:
            kept[name] = data
            continue
        if name.endswith('.LIB') or budget + len(data) > OPTIONAL_BUDGET:
            item = files[name]
            dropped.append({'path': name, 'lba': item['lba'], 'bytes': len(data),
                            'reason': 'library_optional_budget'})
            continue
        kept[name] = data
        budget += len(data)
    if dropped:
        report['omitted_files'] = sorted(report['omitted_files'] + dropped,
                                         key=lambda item: item['path'])
        report['files'] = [item for item in report['files'] if item['path'] in kept]
        report['selected_file_count'] = len(kept) - 2
        report['archive_file_count'] = len(kept) + 1
        report['selected_bytes'] = sum(map(len, kept.values()))
        report['library_trim'] = (f'{len(dropped)} optional files left out by the library '
                                  f'bundler (no .LIB, at most {OPTIONAL_BUDGET:,} optional bytes)')
    return kept, report


def startup_key(entries, report):
    """Identity of what a sweep reads: the boot file plus required CE EXEs.

    Windows CE kernels can be byte-identical across games, so a CE startup
    also includes its IP header and is never merged with another title."""
    digest = hashlib.sha256()
    names = sorted(report['required_files']) + (['IP.BIN'] if report['windows_ce'] else [])
    for name in names:
        digest.update(name.encode('ascii') + b'\0' + hashlib.sha256(entries[name]).digest())
    return digest.hexdigest()


def game_zip(entries, report, method):
    """One single-game bundle: the extractor's files plus startup-info.json."""
    payload = dict(entries)
    payload['startup-info.json'] = (json.dumps(report, indent=2) + '\n').encode('utf-8')
    buffer = io.BytesIO()
    with zipfile.ZipFile(buffer, 'w', compression=method) as archive:
        for name, data in payload.items():
            archive.writestr(name, data)
    blob = buffer.getvalue()
    with zipfile.ZipFile(io.BytesIO(blob)) as verify:
        if verify.namelist() != list(payload) or verify.testzip() is not None:
            raise ValueError('Generated game ZIP failed its integrity check')
        for name, data in payload.items():
            if verify.read(name) != data:
                raise ValueError('Generated game ZIP differs from extracted startup bytes')
    if len(blob) > INNER_LIMIT:
        raise ValueError('Game ZIP exceeds the bounded size')
    return blob


def load_known(path):
    """Boot-file SHA256 values whose native startups were already sent."""
    if path is None or not path.is_file():
        return set()
    data = json.loads(path.read_text(encoding='utf-8'))
    values = [item.get('boot_sha256') if isinstance(item, dict) else item
              for item in data.get('startups', [])]
    if any(not SHA256.fullmatch(str(value)) for value in values):
        raise ValueError(f'{path.name} holds a malformed SHA256')
    return set(values)


def classify(descriptor, root, index, method, staging, known):
    """One library row; writes the inner ZIP into staging when bundled."""
    row = {'source': descriptor.relative_to(root).as_posix(), 'status': 'error'}
    try:
        entries, report = disc.extract(descriptor)
        entries, report = trim_optional(entries, report)
    except Exception as error:  # one unreadable disc must not stop the library
        row['error'] = str(error) if isinstance(error, (OSError, ValueError)) else \
            f'{type(error).__name__}: {error}'
        try:
            row.update(peek_ip(descriptor))
        except (OSError, ValueError, UnicodeError):
            pass
        return row
    boot = entries[report['boot_path']]
    tracks = report['tracks']
    row.update(ip_fields(entries['IP.BIN']))
    row.update(boot_filename=report['boot_filename'], boot_path=report['boot_path'],
               windows_ce=report['windows_ce'], descriptor_format=report['descriptor_format'],
               data_encoding=report['data_encoding'],
               boot_bytes=len(boot), boot_sha256=hashlib.sha256(boot).hexdigest(),
               ip_sha256=hashlib.sha256(entries['IP.BIN']).hexdigest(),
               startup_key=startup_key(entries, report),
               tracks=len(tracks), audio_tracks=sum(t['control'] == 0 for t in tracks),
               cdda_tracks=len(report['audio_candidates']['high_density_tracks']),
               disc_files=sum(not item['flags'] & 2 for item in report['inventory']),
               copied_files=len(entries) - 2)
    if report['windows_ce']:
        row['application_candidates'] = report['application_candidates']
    if not report['windows_ce'] and row['boot_sha256'] in known:
        row['status'] = 'already_sent'
        return row
    row['slug'] = slug(index, row['product'], row['title'])
    blob = game_zip(entries, report, method)
    (staging / (row['slug'] + '.zip')).write_bytes(blob)
    row.update(status='bundled', archive=f"games/{row['slug']}.zip", archive_bytes=len(blob))
    return row


def plan_parts(rows, limit):
    """Sequential parts no larger than limit, except a single oversized game."""
    parts, size = [[]], 0
    for row in rows:
        if row['status'] != 'bundled':
            continue
        if limit and parts[-1] and size + row['archive_bytes'] > limit:
            parts.append([])
            size = 0
        parts[-1].append(row)
        size += row['archive_bytes']
    for number, part in enumerate(parts, 1):
        for row in part:
            row['part'] = number
    return parts


def publish(destination, library, rows, staging):
    """Write one closed part ZIP beside destination, then link it into place."""
    names = ['library.json'] + [row['archive'] for row in rows]
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.Library-startup-', suffix='.zip.tmp',
                                         dir=destination.parent, delete=False) as stage:
            temporary = Path(stage.name)
        with zipfile.ZipFile(temporary, 'w', compression=zipfile.ZIP_STORED) as archive:
            archive.writestr('library.json', json.dumps(library, indent=2) + '\n',
                             compress_type=zipfile.ZIP_DEFLATED)
            for row in rows:
                archive.write(staging / (row['slug'] + '.zip'), row['archive'])
        with zipfile.ZipFile(temporary) as verify:
            if verify.namelist() != names or verify.testzip() is not None:
                raise ValueError('Generated library ZIP failed its integrity check')
            for row in rows:
                if verify.read(row['archive']) != (staging / (row['slug'] + '.zip')).read_bytes():
                    raise ValueError('Generated library ZIP differs from its game ZIPs')
        with temporary.open('rb') as archive:
            os.fsync(archive.fileno())
        try:
            os.link(temporary, destination)
        except OSError:
            # exFAT/FAT USB drives refuse hard links; never overwrite instead.
            if os.path.lexists(destination):
                raise
            os.replace(temporary, destination)
            temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def describe(row):
    status = row['status']
    if status == 'error':
        return row['error']
    kind = ('Windows CE' if row['windows_ce'] else 'native') + \
        (f", CD audio ({row['cdda_tracks']} tracks)" if row['cdda_tracks'] else '')
    if status == 'duplicate':
        return f"same startup as {row['duplicate_of']}"
    if status == 'already_sent':
        return f'{kind}; already sent, listed only'
    return f"{kind}; {row['boot_filename']} {row['boot_bytes']:,} bytes, {row['copied_files']} files"


def build(root, output_dir, name, part_mb, known_path=None, log=print):
    root, output_dir = Path(root), Path(output_dir)
    if root.is_symlink() or not root.is_dir():
        raise ValueError(f'{root} is not a folder')
    if not output_dir.is_dir():
        raise ValueError(f'{output_dir} is not a folder')
    if any(output_dir.glob(f'{name}-*.zip')):
        raise ValueError(f'{output_dir} already holds {name}-*.zip files; choose another --output-dir')
    known = load_known(known_path)
    descriptors, others = find_images(root)
    if not descriptors and not others:
        raise ValueError(f'No .gdi or .cue files below {root}')
    method = compression()
    rows, by_key = [], {}
    with tempfile.TemporaryDirectory(prefix='.kui-library-', dir=output_dir) as work:
        staging = Path(work)
        for index, descriptor in enumerate(descriptors, 1):
            row = classify(descriptor, root, index, method, staging, known)
            if row['status'] in ('bundled', 'already_sent'):
                key = row['startup_key']
                if key in by_key:
                    if row['status'] == 'bundled':
                        (staging / (row['slug'] + '.zip')).unlink()
                        for field in ('slug', 'archive', 'archive_bytes'):
                            row.pop(field)
                    row.update(status='duplicate', duplicate_of=by_key[key])
                else:
                    by_key[key] = row.get('slug') or row['source']
            rows.append(row)
            log(f'[{index}/{len(descriptors)}] {row.get("title") or row["source"]}: {describe(row)}')
        for path in others:
            rows.append({'source': path.relative_to(root).as_posix(), 'status': 'other_format',
                         'format': path.suffix.lower().lstrip('.')})
        parts = plan_parts(rows, int(part_mb * 1024 * 1024))
        counts = {}
        for row in rows:
            counts[row['status']] = counts.get(row['status'], 0) + 1
        run_id = uuid.uuid4().hex
        created = []
        for number, part in enumerate(parts, 1):
            library = {'schema': SCHEMA, 'library_version': LIBRARY_VERSION,
                       'extractor_version': disc.EXTRACTOR_VERSION, 'run_id': run_id,
                       'part': number, 'parts': len(parts),
                       'purpose': 'K-UI startup sweep; game bytes unchanged',
                       'compression': 'lzma' if method == zipfile.ZIP_LZMA else 'deflate',
                       'already_sent': len(known), 'summary': counts, 'games': rows}
            destination = output_dir / f'{name}-{number:02d}.zip'
            publish(destination, library, part, staging)
            created.append(destination)
    return rows, created


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    version = f'K-UI library startup bundler v{LIBRARY_VERSION}'
    parser.add_argument('--version', action='version', version=version)
    parser.add_argument('folder', type=Path, help='Folder holding GDI and CUE images')
    parser.add_argument('--output-dir', type=Path, default=Path('.'))
    parser.add_argument('--name', default='Library-startup')
    parser.add_argument('--part-mb', type=int, default=25,
                        help='Largest part in MiB (default 25); 0 writes one ZIP')
    parser.add_argument('--resend-all', action='store_true',
                        help=f'Ignore {KNOWN_NAME} and copy every startup again')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,40}', args.name) or args.part_mb < 0:
        parser.error('--name takes letters, digits, - and _; --part-mb is 0 or more')
    print(version, flush=True)
    known = None if args.resend_all else Path(__file__).resolve().with_name(KNOWN_NAME)
    try:
        rows, created = build(args.folder, args.output_dir, args.name, args.part_mb,
                              known_path=known, log=lambda line: print(line, flush=True))
    except (OSError, ValueError, zipfile.BadZipFile, UnicodeError) as error:
        parser.exit(1, f'Bundling stopped: {error}\n')
    except KeyboardInterrupt:
        parser.exit(130, 'Bundling stopped; no ZIP was published for the unfinished part\n')
    counts = {}
    for row in rows:
        counts[row['status']] = counts.get(row['status'], 0) + 1
    print('Summary: ' + ', '.join(f'{count} {status}' for status, count in sorted(counts.items())))
    for path in created:
        print(f'Created {path.resolve()} ({path.stat().st_size:,} bytes)')
    print(f'Upload every {args.name}-NN.zip file (the first always lists every game). '
          'Source tracks were read only.')


if __name__ == '__main__':
    main()
