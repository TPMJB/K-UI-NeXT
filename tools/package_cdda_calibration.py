#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle only the new SCI CDDA calibration and command experiments."""
import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import subprocess
import tempfile
import zipfile

from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ((4, 'calibration'), (5, 'commands'))
MARKER = bytes.fromhex('4b554953424f4f540100000003000000fcffffff')
FIXTURE_SHA = '94b1b9e619e9be5a5dc5068d2fd9073eaf86d0c93e0b5b39a76108e5586f65ea'
FATFS_FILES = ('ff.c', 'ff.h', 'ffconf.h', 'ffunicode.c', 'diskio.h')
DOCUMENTS = ('cdda-roadmap.md', 'cdda-next-test.md', 'cdda-harness-test.md',
             'cdda-calibration-commands-test.md')


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, text=True).strip()


def sha(data):
    return hashlib.sha256(data).hexdigest()


def object_id(value):
    if not re.fullmatch(r'[0-9a-f]{40}', value):
        raise argparse.ArgumentTypeError('Expected a 40-character lowercase hexadecimal SHA')
    return value


def archive_name(name):
    """Keep ZIP paths and checksum-manifest filenames unambiguous."""
    if (not isinstance(name, str) or not name or '\\' in name or
            any(ord(char) < 32 or ord(char) == 127 for char in name) or
            PurePosixPath(name).is_absolute() or
            any(part in ('', '.', '..') for part in name.split('/'))):
        raise ValueError('Unsafe archive/source path: ' + repr(name))
    return name


def read_file(path):
    path = Path(path)
    relative = path.relative_to(ROOT)
    archive_name(relative.as_posix())
    # The build copies private FatFs inputs; an external symlink is not an
    # acceptable substitute for the exact files from that build directory.
    for candidate in (path, *path.parents):
        if candidate == ROOT:
            break
        if candidate.is_symlink():
            raise ValueError('Input must not be a symlink: ' + str(relative))
    return path.read_bytes()


def build_config(path):
    config = {}
    for line in read_file(path).decode('utf-8').splitlines():
        key, separator, value = line.partition('=')
        if not separator or not key or key in config:
            raise ValueError('Malformed build configuration: ' + str(path))
        config[key] = value
    return config


def valid_boot_markers(payload):
    count = 0
    cursor = 0
    while True:
        offset = payload.find(MARKER[:8], cursor)
        if offset < 0:
            return count
        cursor = offset + 1
        if offset + len(MARKER) > len(payload):
            continue
        _, _, version, transport, inverse = struct.unpack_from('<5I', payload, offset)
        # Compiler comparison constants can also contain the two magic words.
        # Only the complete version/transport/inverse record is a boot marker.
        if version == 1 and transport <= 3 and inverse == transport ^ 0xffffffff:
            count += 1


def collect(commit, published_tree):
    if git('status', '--porcelain'):
        raise ValueError('Commit reviewed source before packaging')
    source_tree = git('rev-parse', 'HEAD^{tree}')
    if published_tree != source_tree:
        raise ValueError('Published source tree differs from the archived checkpoint')
    # Validate names in the complete source snapshot, too. git archive retains
    # the original repository layout and needs no unavailable parent commit.
    tracked = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT)
    for name in tracked.decode('utf-8').split('\0'):
        if name:
            archive_name(name)

    files = {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive path: ' + name)
        files[name] = data

    runtimes = []
    private_fatfs = None
    for profile, name in PROFILES:
        build = ROOT / ('build/cdda-' + name)
        config = build_config(build / 'build-config')
        if config.get('BUILD') != commit[:12] or config.get('PROFILE') != str(profile):
            raise ValueError('Wrong build configuration for ' + name)
        runtime = read_file(build / 'cdda-harness.kui')
        info = verify(runtime)
        elf = read_file(build / 'cdda-harness.elf')
        payload, memory = flatten_elf(elf)
        if (info['build'] != commit[:12] or runtime[64:] != payload or
                memory != 0x200000 or info['memory_bytes'] != memory or
                payload.count(MARKER) != 1 or valid_boot_markers(payload) != 1):
            raise ValueError('Envelope/source/layout/boot-marker mismatch for ' + name)
        filename = f'runtimes/{profile:02d}-{name}.kui'
        add(filename, runtime)
        if profile == 4:
            add('KUI/runtime.kui', runtime)
        runtimes.append({
            'profile': profile, 'name': name, 'file': filename,
            'build': info['build'], 'hardware_tested': False,
            'runtime_sha256': sha(runtime), 'elf_sha256': sha(elf),
            'payload_bytes': info['payload_bytes'],
            'memory_footprint_bytes': memory, 'payload_crc32': info['crc32'],
            'boot_marker': 'one unpatched AUTO marker; bootstrap must select SCI',
        })
        add(f'evidence/{name}/cdda-harness.elf', elf)
        add(f'evidence/{name}/cdda-harness.map', read_file(build / 'cdda-harness.map'))
        stack_files = sorted(build.rglob('*.su'))
        if not stack_files:
            raise ValueError('Missing compiler stack reports for ' + name)
        for path in stack_files:
            add(f'evidence/{name}/stack/' + path.relative_to(build).as_posix(), read_file(path))
        fatfs = {filename: read_file(build / 'fatfs' / filename)
                 for filename in FATFS_FILES}
        if private_fatfs is not None and fatfs != private_fatfs:
            raise ValueError('Private FatFs inputs differ between calibration and commands')
        private_fatfs = fatfs

    # A private configuration must keep both read-only operation and exFAT.
    configuration = private_fatfs['ffconf.h'].decode('utf-8')
    for key, value in (('FF_FS_READONLY', 1), ('FF_FS_EXFAT', 1)):
        if not re.search(r'^#define\s+' + key + r'\s+' + str(value) + r'\b',
                         configuration, flags=re.MULTILINE):
            raise ValueError('Unexpected private FatFs configuration: ' + key)
    for name, data in private_fatfs.items():
        add('source-fatfs/' + name, data)

    fixture = ROOT / 'build/cdda/fixture'
    data = read_file(fixture / 'stereo.raw')
    fixture_metadata = json.loads(read_file(fixture / 'stereo.json'))
    expected_fields = {
        'file': 'stereo.raw', 'bytes': 2116800, 'sample_rate': 44100,
        'channels': 2, 'bits_per_sample': 16, 'byte_order': 'little',
        'interleaved': True, 'frames': 529200, 'sectors_2352': 900,
        'seconds': 12, 'sha256': FIXTURE_SHA,
    }
    if (len(data) != expected_fields['bytes'] or sha(data) != FIXTURE_SHA or
            any(fixture_metadata.get(key) != value for key, value in expected_fields.items())):
        raise ValueError('Generated stereo fixture differs from the verified original')
    add('KUI/tests/cdda/stereo.raw', data)
    add('fixtures/stereo.json', (json.dumps(fixture_metadata, indent=2) + '\n').encode())

    metadata = {
        'source_commit': commit, 'source_tree': source_tree,
        'local_checkpoint': git('rev-parse', 'HEAD'), 'source_dirty': False,
        'source_publication': 'published; supplied tree matches local checkpoint',
        'source_url': 'https://github.com/TPMJB/K-UI-NeXT/tree/' + commit,
        'source_snapshot': 'source-snapshot.tar',
        'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
        'scope': 'Detached SCI homebrew calibration and commands; no retail CDDA integration',
        'default_profile': 'calibration', 'default_profile_number': 4,
        'hardware_tested': False, 'runtimes': runtimes,
        'owned_game_files_included': False, 'sample_included': False,
        'required_game_files': [], 'fixtures': [fixture_metadata],
        'fatfs_inputs_sha256': {name: sha(data) for name, data in private_fatfs.items()},
        'sound_ring_bytes': 65536, 'private_stack_bytes': 65536,
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    add('README.md', read_file(ROOT / 'docs/cdda-calibration-commands-test.md'))
    for name in DOCUMENTS:
        # Evidence originally links ../cdda-*.md, and these instructions link
        # evidence/cdda-*.md. Keep both usable at the package root.
        add(name, read_file(ROOT / 'docs' / name))
    add('source-url.txt', (metadata['source_url'] + '\n').encode())
    add('source-snapshot.tar', subprocess.check_output(
        ['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT))
    for path in sorted((ROOT / 'docs/evidence').glob('cdda-*.md')):
        add('evidence/' + path.name, read_file(path))
    for path in sorted((ROOT / 'LICENSES').rglob('*')):
        if path.is_file():
            add('LICENSES/' + path.relative_to(ROOT / 'LICENSES').as_posix(), read_file(path))
    for name in ('LICENSE', 'THIRD_PARTY.md'):
        add(name, read_file(ROOT / name))
    # The archived source contains the catalogue code. Preserve its notice
    # even though neither detached executable links the dump databases.
    notice = read_file(ROOT / 'data/known-dumps/README.txt')
    if 'LICENSES/known-dumps-README.txt' in files:
        if files['LICENSES/known-dumps-README.txt'] != notice:
            raise ValueError('Known-dumps notices disagree')
    else:
        add('LICENSES/known-dumps-README.txt', notice)

    if git('status', '--porcelain') or git('rev-parse', 'HEAD^{tree}') != source_tree:
        raise ValueError('Source changed while collecting the package')
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n'
                            for name, data in sorted(files.items())).encode())
    return files


def write_archive(output, files):
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix='.cdda-package-', suffix='.zip',
                                         dir=output.parent, delete=False) as handle:
            temporary = Path(handle.name)
        with zipfile.ZipFile(temporary, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
            for name, data in sorted(files.items()):
                archive.writestr(name, data)
        with zipfile.ZipFile(temporary) as archive:
            if (archive.testzip() or set(archive.namelist()) != set(files) or
                    len(archive.namelist()) != len(files) or
                    any(archive.read(name) != data for name, data in files.items())):
                raise ValueError('ZIP round-trip integrity verification failed')
        os.replace(temporary, output)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-commit', required=True, type=object_id)
    parser.add_argument('--published-tree', required=True, type=object_id,
                        help='Tree SHA independently verified on the published commit')
    args = parser.parse_args()
    try:
        if args.output.is_symlink() or args.output.suffix.lower() != '.zip':
            raise ValueError('Output must be a ZIP filename, not a symlink')
        output = args.output.resolve()
        archive_name(output.name)
        if output.exists() and not output.is_file():
            raise ValueError('Output path is not a regular file')
        if output.is_relative_to(ROOT):
            relative = output.relative_to(ROOT).as_posix()
            tracked = subprocess.run(['git', 'ls-files', '--error-unmatch', '--', relative],
                                     cwd=ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if tracked.returncode == 0:
                raise ValueError('Output must not overwrite a tracked repository file')
        files = collect(args.source_commit, args.published_tree)
        write_archive(output, files)
        print(json.dumps({'file': str(output), 'bytes': output.stat().st_size,
                          'build': args.source_commit[:12],
                          'profiles': [4, 5], 'sha256': sha(output.read_bytes())}, indent=2))
    except (OSError, ValueError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
