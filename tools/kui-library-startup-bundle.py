#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle the native startup files of every GDI below a folder for one sweep.

Usage: python3 kui-library-startup-bundle.py /path/to/Games
Keep this script beside kui-native-startup-bundle.py. Each original raw2352
GDI is read exactly as that single-game helper reads it, and its IP.BIN and
IP-selected boot file become one inner ZIP per game. Windows CE games and
games with CD audio in the high-density area are listed but not copied
(--include-cdda copies the latter). Other image formats are listed by name.
Writes Library-startup-01.zip, -02.zip ... of at most --part-mb MiB each,
without overwriting. Requires only Python3's standard library. Source tracks
are read only. This prepares analysis evidence, not a game patch or a
compatibility claim.
"""
import argparse
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import shlex
import tempfile
import uuid
import zipfile

LIBRARY_VERSION = '1'
SCHEMA = 'kui.native-library-startup.v1'
GDI_LIMIT = 4096
WALK_LIMIT = 1000000
INNER_LIMIT = 16 * 1024 * 1024
OTHER_FORMATS = ('.cdi', '.chd', '.cue', '.iso', '.ccd', '.mds', '.zso', '.nrg')
SLUG = re.compile(r'[0-9]{3,4}-[A-Za-z0-9-]{0,56}')


def load_native():
    path = Path(__file__).resolve().with_name('kui-native-startup-bundle.py')
    spec = importlib.util.spec_from_file_location('kui_native_startup_bundle', path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    return helper


native = load_native()


def compression():
    try:
        import lzma  # noqa: F401  Python builds without liblzma fall back.
        return zipfile.ZIP_LZMA
    except ImportError:
        return zipfile.ZIP_DEFLATED


def find_images(root):
    """Sorted GDIs and other image files; symlinked folders are not followed."""
    gdis, others, visited = [], [], 0
    for folder, directories, files in os.walk(root):
        directories.sort()
        visited += len(directories) + len(files)
        if visited > WALK_LIMIT:
            raise ValueError(f'More than {WALK_LIMIT:,} entries below {root}; choose the games folder')
        for name in sorted(files):
            if name.startswith('._'):
                continue  # macOS resource sidecars, not images
            path = Path(folder) / name
            suffix = path.suffix.lower()
            if suffix == '.gdi':
                gdis.append(path)
            elif suffix in OTHER_FORMATS:
                others.append(path)
        if len(gdis) > GDI_LIMIT:
            raise ValueError(f'More than {GDI_LIMIT:,} GDI files below {root}')
    return gdis, others


def peek_ip(gdi):
    """Title fields from the high-density IP header, for rows not bundled."""
    text = gdi.read_bytes()[:64 * 1024].decode('utf-8-sig')
    for row in [shlex.split(line) for line in text.splitlines() if line.strip()][1:]:
        if (len(row) != 6 or row[2] != '4' or not row[1].isdigit() or int(row[1]) < 45000 or
                row[3] != str(native.RAW) or row[5] != '0'):
            continue
        name = row[4]
        if '/' in name or '\\' in name or ':' in name or name in ('', '.', '..'):
            return {}
        track = gdi.parent / name
        if track.is_symlink() or not track.is_file():
            return {}
        with track.open('rb') as source:
            sector = source.read(native.RAW)
        ip = sector[16:16 + native.DATA]
        if (len(sector) != native.RAW or sector[15] != 1 or
                ip[:16] != b'SEGA SEGAKATANA '):
            return {}
        return {'title': ip[128:256].decode('ascii', 'replace').strip(' \0'),
                'product': ip[64:74].decode('ascii', 'replace').strip(' \0'),
                'version': ip[74:80].decode('ascii', 'replace').strip(' \0'),
                'region': ip[48:56].decode('ascii', 'replace').strip(' \0')}
    return {}


def slug(index, product, title):
    words = re.sub(r'[^A-Za-z0-9]+', '-', f'{product}-{title}').strip('-')[:56].strip('-')
    value = f'{index:03d}-{words}'
    if not SLUG.fullmatch(value):
        raise ValueError('internal slug error')
    return value


def game_zip(entries, report, method):
    """One single-game bundle, as kui-native-startup-bundle.py lays it out."""
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


def classify(gdi, root, index, include_cdda, method, staging):
    """One library row; writes the inner ZIP into staging when bundled."""
    row = {'source': gdi.relative_to(root).as_posix(), 'status': 'error'}
    try:
        entries, report = native.extract(gdi)
    except native.WindowsCEProfileError:
        row['status'] = 'windows_ce'
        try:
            row.update(peek_ip(gdi))
        except (OSError, ValueError, UnicodeError):
            pass
        return row, None
    except Exception as error:  # one unreadable disc must not stop the library
        row['error'] = str(error) if isinstance(error, (OSError, ValueError)) else \
            f'{type(error).__name__}: {error}'
        try:
            row.update(peek_ip(gdi))
        except (OSError, ValueError, UnicodeError):
            pass
        return row, None
    boot = entries[report['boot_path']]
    cdda = [track['number'] for track in report['tracks']
            if track['control'] == 0 and track['start_lba'] >= 45000]
    row.update({key: report[key] for key in ('title', 'product', 'version', 'region',
                                             'boot_filename', 'boot_path')})
    row.update(boot_bytes=len(boot), boot_sha256=hashlib.sha256(boot).hexdigest(),
               ip_sha256=hashlib.sha256(entries['IP.BIN']).hexdigest(),
               tracks=len(report['tracks']),
               audio_tracks=sum(track['control'] == 0 for track in report['tracks']),
               cdda_tracks=len(cdda))
    if cdda and not include_cdda:
        row['status'] = 'cdda'
        return row, None
    row['slug'] = slug(index, report['product'], report['title'])
    blob = game_zip(entries, report, method)
    (staging / (row['slug'] + '.zip')).write_bytes(blob)
    row.update(status='bundled', archive=f"games/{row['slug']}.zip", archive_bytes=len(blob))
    return row, len(blob)


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
        os.link(temporary, destination)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def build(root, output_dir, name, part_mb, include_cdda, log=print):
    root, output_dir = Path(root), Path(output_dir)
    if root.is_symlink() or not root.is_dir():
        raise ValueError(f'{root} is not a folder')
    if not output_dir.is_dir():
        raise ValueError(f'{output_dir} is not a folder')
    if any(output_dir.glob(f'{name}-*.zip')):
        raise ValueError(f'{output_dir} already holds {name}-*.zip files; choose another --output-dir')
    gdis, others = find_images(root)
    if not gdis:
        raise ValueError(f'No .gdi files below {root}')
    method = compression()
    rows, by_boot = [], {}
    with tempfile.TemporaryDirectory(prefix='.kui-library-', dir=output_dir) as work:
        staging = Path(work)
        for index, gdi in enumerate(gdis, 1):
            row, _ = classify(gdi, root, index, include_cdda, method, staging)
            if row['status'] == 'bundled':
                key = row['boot_sha256']  # the gates read only the boot file
                if key in by_boot:
                    (staging / (row['slug'] + '.zip')).unlink()
                    for field in ('slug', 'archive', 'archive_bytes'):
                        row.pop(field)
                    row.update(status='duplicate', duplicate_of=by_boot[key])
                else:
                    by_boot[key] = row['slug']
            rows.append(row)
            label = row.get('title') or row['source']
            detail = {'bundled': lambda: f"{row['boot_filename']} {row['boot_bytes']:,} bytes",
                      'duplicate': lambda: f"same startup as {row['duplicate_of']}",
                      'windows_ce': lambda: 'Windows CE, listed only',
                      'cdda': lambda: f"CD audio ({row['cdda_tracks']} tracks), listed only",
                      'error': lambda: row['error']}[row['status']]()
            log(f'[{index}/{len(gdis)}] {label}: {detail}')
        for path in others:
            rows.append({'source': path.relative_to(root).as_posix(), 'status': 'other_format',
                         'format': path.suffix.lower().lstrip('.')})
        parts = plan_parts(rows, part_mb * 1024 * 1024)
        counts = {}
        for row in rows:
            counts[row['status']] = counts.get(row['status'], 0) + 1
        run_id = uuid.uuid4().hex
        created = []
        for number, part in enumerate(parts, 1):
            library = {'schema': SCHEMA, 'library_version': LIBRARY_VERSION,
                       'extractor_version': native.EXTRACTOR_VERSION, 'run_id': run_id,
                       'part': number, 'parts': len(parts),
                       'purpose': 'K-UI native startup sweep; game bytes unchanged',
                       'compression': 'lzma' if method == zipfile.ZIP_LZMA else 'deflate',
                       'summary': counts, 'games': rows}
            destination = output_dir / f'{name}-{number:02d}.zip'
            publish(destination, library, part, staging)
            created.append(destination)
    return rows, created


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    version = f'K-UI library startup bundler v{LIBRARY_VERSION}'
    parser.add_argument('--version', action='version', version=version)
    parser.add_argument('folder', type=Path, help='Folder holding original native raw2352 GDIs')
    parser.add_argument('--output-dir', type=Path, default=Path('.'))
    parser.add_argument('--name', default='Library-startup')
    parser.add_argument('--part-mb', type=int, default=25,
                        help='Largest part in MiB (default 25); 0 writes one ZIP')
    parser.add_argument('--include-cdda', action='store_true',
                        help='Also copy games with CD audio tracks')
    args = parser.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]{1,40}', args.name) or args.part_mb < 0:
        parser.error('--name takes letters, digits, - and _; --part-mb is 0 or more')
    print(version, flush=True)
    try:
        rows, created = build(args.folder, args.output_dir, args.name, args.part_mb,
                              args.include_cdda, log=lambda line: print(line, flush=True))
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
    print(f'Upload every {args.name}-NN.zip file. Source tracks were read only.')


if __name__ == '__main__':
    main()
