#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle only the detached SCI mixed CDDA/data-job experiment."""
import argparse
import json
from pathlib import Path
import posixpath
import re
import subprocess
from urllib.parse import quote, unquote, urlsplit, urlunsplit
import zipfile
import zlib

from package_cdda_calibration import (
    FATFS_FILES, MARKER, ROOT, archive_name, build_config, git, object_id,
    read_file, sha, valid_boot_markers, write_archive,
)
from runtime_package import flatten_elf, verify

PROFILE = 6
TMU_HZ = 12468720
FIXTURES = {
    'stereo': {
        'file': 'stereo.raw', 'bytes': 2116800, 'sample_rate': 44100,
        'channels': 2, 'bits_per_sample': 16, 'byte_order': 'little',
        'interleaved': True, 'frames': 529200, 'sectors_2352': 900,
        'seconds': 12, 'crc32': '6ab014d1',
        'sha256': '94b1b9e619e9be5a5dc5068d2fd9073eaf86d0c93e0b5b39a76108e5586f65ea',
    },
    'stress': {
        'file': 'stress.bin', 'bytes': 8388608, 'sectors_512': 16384,
        'crc32': '41d8d1ae',
        'sha256': '29299d0448bab8ace4c8a478255ac009ebd8896b85fcf6b4271e7e1059a2fc49',
    },
}
LINK = re.compile(r'(\]\()([^\s)]+)(\))')


def document_copy(data, source, destination, available, tracked, commit):
    """Keep source-layout copies and convenient root aliases navigable.

    All CDDA documents are included. References to other published source
    files use that exact commit rather than introducing broken ZIP links or
    recursively collecting unrelated documentation. The source tar retains
    each original document byte-for-byte.
    """
    def replace(match):
        target = match[2]
        parsed = urlsplit(target)
        if parsed.scheme or parsed.netloc or not parsed.path:
            return match[0]
        original = posixpath.normpath(posixpath.join(
            posixpath.dirname(source), unquote(parsed.path)))
        archive_name(original)
        if original in available:
            path = posixpath.relpath(original, posixpath.dirname(destination) or '.')
            href = urlunsplit(('', '', quote(path, safe='/.-_'), parsed.query, parsed.fragment))
        else:
            is_file = original in tracked
            is_directory = any(name.startswith(original + '/') for name in tracked)
            if not is_file and not is_directory:
                raise ValueError('Unavailable document target: ' + source + ' -> ' + target)
            route = 'blob' if is_file else 'tree'
            href = ('https://github.com/TPMJB/K-UI-NeXT/' + route + '/' + commit +
                    '/' + quote(original, safe='/.-_'))
            if parsed.query:
                href += '?' + parsed.query
            if parsed.fragment:
                href += '#' + parsed.fragment
        return match[1] + href + match[3]

    return LINK.sub(replace, data.decode('utf-8')).encode('utf-8')


def collect(commit, published_tree):
    if git('status', '--porcelain'):
        raise ValueError('Commit reviewed source before packaging')
    source_tree = git('rev-parse', 'HEAD^{tree}')
    if published_tree != source_tree:
        raise ValueError('Published source tree differs from the archived checkpoint')
    names = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode('utf-8')
    tracked = {name for name in names.split('\0') if name}
    for name in tracked:
        archive_name(name)

    files = {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive path: ' + name)
        files[name] = data

    clock_header = read_file(ROOT / 'include/kui/cdda_clock.h')
    if not re.search(rb'^#define\s+KUI_CDDA_TMU_HZ\s+12468720u\s*$',
                     clock_header, flags=re.MULTILINE):
        raise ValueError('Mixed profile must use the documented 12468720 Hz clock contract')

    build = ROOT / 'build/cdda-mixed'
    config_bytes = read_file(build / 'build-config')
    config = build_config(build / 'build-config')
    if config.get('BUILD') != commit[:12] or config.get('PROFILE') != str(PROFILE):
        raise ValueError('Wrong build configuration for mixed jobs')
    runtime = read_file(build / 'cdda-harness.kui')
    info = verify(runtime)
    elf = read_file(build / 'cdda-harness.elf')
    payload, memory = flatten_elf(elf)
    if (info['build'] != commit[:12] or runtime[64:] != payload or
            memory != 0x200000 or info['memory_bytes'] != memory or
            payload.count(MARKER) != 1 or valid_boot_markers(payload) != 1):
        raise ValueError('Envelope/source/layout/boot-marker mismatch for mixed jobs')
    add('runtimes/06-mixed-jobs.kui', runtime)
    add('KUI/runtime.kui', runtime)
    add('evidence/mixed/cdda-harness.elf', elf)
    map_bytes = read_file(build / 'cdda-harness.map')
    add('evidence/mixed/cdda-harness.map', map_bytes)
    add('evidence/mixed/build-config', config_bytes)
    stack_hashes = {}
    stack_files = sorted(build.rglob('*.su'))
    if not stack_files:
        raise ValueError('Missing compiler stack reports for mixed jobs')
    for path in stack_files:
        relative = path.relative_to(build).as_posix()
        data = read_file(path)
        add('evidence/mixed/stack/' + relative, data)
        stack_hashes[relative] = sha(data)

    private_fatfs = {name: read_file(build / 'fatfs' / name) for name in FATFS_FILES}
    configuration = private_fatfs['ffconf.h'].decode('utf-8')
    for key, value in (('FF_FS_READONLY', 1), ('FF_FS_EXFAT', 1)):
        if not re.search(r'^#define\s+' + key + r'\s+' + str(value) + r'\b',
                         configuration, flags=re.MULTILINE):
            raise ValueError('Unexpected private FatFs configuration: ' + key)
    for name, data in private_fatfs.items():
        add('source-fatfs/' + name, data)

    fixture_metadata = []
    fixture_input_hashes = {}
    fixture = ROOT / 'build/cdda/fixture'
    for name, expected in FIXTURES.items():
        data = read_file(fixture / expected['file'])
        metadata_bytes = read_file(fixture / (name + '.json'))
        metadata = json.loads(metadata_bytes)
        if (not isinstance(metadata, dict) or len(data) != expected['bytes'] or
                sha(data) != expected['sha256'] or
                f'{zlib.crc32(data):08x}' != expected['crc32'] or
                any(metadata.get(key) != value for key, value in expected.items())):
            raise ValueError('Generated fixture differs from the verified input: ' + name)
        add('KUI/tests/cdda/' + expected['file'], data)
        add('fixtures/' + name + '.json', metadata_bytes)
        fixture_metadata.append(metadata)
        fixture_input_hashes[expected['file']] = sha(data)
        fixture_input_hashes[name + '.json'] = sha(metadata_bytes)

    for path in sorted((ROOT / 'LICENSES').rglob('*')):
        if path.is_file():
            add('LICENSES/' + path.relative_to(ROOT / 'LICENSES').as_posix(), read_file(path))
    add('LICENSE', read_file(ROOT / 'LICENSE'))
    notice = read_file(ROOT / 'data/known-dumps/README.txt')
    if 'LICENSES/known-dumps-README.txt' in files:
        if files['LICENSES/known-dumps-README.txt'] != notice:
            raise ValueError('Known-dumps notices disagree')
    else:
        add('LICENSES/known-dumps-README.txt', notice)

    source_url = 'https://github.com/TPMJB/K-UI-NeXT/tree/' + commit
    add('source-url.txt', (source_url + '\n').encode())
    snapshot = subprocess.check_output(['git', 'archive', '--format=tar', 'HEAD'], cwd=ROOT)
    add('source-snapshot.tar', snapshot)

    documents = sorted((ROOT / 'docs').glob('cdda-*.md'))
    evidence = sorted((ROOT / 'docs/evidence').glob('cdda-*.md'))
    document_paths = []
    for path in documents:
        source = path.relative_to(ROOT).as_posix()
        document_paths.extend(((source, source), (source, path.name)))
    for path in evidence:
        source = path.relative_to(ROOT).as_posix()
        document_paths.extend(((source, source), (source, 'evidence/' + path.name)))
    document_paths.extend((('docs/cdda-mixed-jobs-test.md', 'README.md'),
                           ('THIRD_PARTY.md', 'THIRD_PARTY.md')))
    available = set(files) | {destination for _, destination in document_paths}
    available.update(('build.json', 'SHA256SUMS'))
    document_input_hashes = {}
    for source, destination in document_paths:
        data = read_file(ROOT / source)
        document_input_hashes[source] = sha(data)
        add(destination, document_copy(data, source, destination, available, tracked, commit))

    metadata = {
        'source_commit': commit, 'source_tree': source_tree,
        'local_checkpoint': git('rev-parse', 'HEAD'), 'source_dirty': False,
        'source_publication': 'published; supplied tree matches local checkpoint',
        'source_url': source_url, 'source_snapshot': 'source-snapshot.tar',
        'source_snapshot_sha256': sha(snapshot),
        'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
        'scope': 'Detached SCI homebrew mixed audio/data/commands; no retail CDDA integration',
        'default_profile': 'mixed-jobs', 'default_profile_number': PROFILE,
        'hardware_tested': False,
        'runtimes': [{
            'profile': PROFILE, 'name': 'mixed-jobs', 'file': 'runtimes/06-mixed-jobs.kui',
            'build': info['build'], 'hardware_tested': False,
            'runtime_sha256': sha(runtime), 'elf_sha256': sha(elf), 'map_sha256': sha(map_bytes),
            'payload_bytes': info['payload_bytes'], 'payload_crc32': info['crc32'],
            'memory_footprint_bytes': memory,
            'boot_marker': 'one unpatched AUTO marker; bootstrap must select SCI',
            'build_configuration': config, 'build_configuration_sha256': sha(config_bytes),
            'stack_reports_sha256': stack_hashes,
        }],
        'clock_contract': {
            'tmu_hz': TMU_HZ, 'configured_aica_pitch': 0,
            'automatic_tuning': False, 'independent_console_frequency_measurement': False,
            'header': 'include/kui/cdda_clock.h', 'header_sha256': sha(clock_header),
        },
        'owned_game_files_included': False, 'sample_included': False,
        'required_game_files': [], 'fixtures': fixture_metadata,
        'fixture_inputs_sha256': fixture_input_hashes,
        'fatfs_inputs_sha256': {name: sha(data) for name, data in private_fatfs.items()},
        'document_source_inputs_sha256': document_input_hashes,
        'package_document_links': 'CDDA source-layout copies and aliases; other targets pin published source',
        'cancellation_scope': 'between synchronous operations; no active SD/DMA transfer abort',
        'test_contract': {
            'completed_stages': 7, 'minimum_tmu_seconds': 180,
            'initialization_excluded': True, 'full_8mib_passes': 1,
            'first_pass_before_tmu_seconds': 120,
            'size_classes_before_tmu_seconds': 150,
            'logical_sizes_bytes': [1, 31, 511, 512, 513, 2048, 4096, 32768],
            'minimum_jobs_per_size': 16, 'maximum_physical_read_bytes': 2048,
            'expected_cancellations': 3, 'expected_stale_refusals': 6,
            'completed_audio_actions': 7, 'explicit_status_checks': 4,
            'no_progress_gap_strictly_below_us': 5000000,
            'data_errors': 0, 'unexpected_failures': 0,
        },
        'sound_ring_bytes': 65536, 'private_stack_bytes': 65536,
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    if git('status', '--porcelain') or git('rev-parse', 'HEAD^{tree}') != source_tree:
        raise ValueError('Source changed while collecting the package')
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n'
                            for name, data in sorted(files.items())).encode())
    return files


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
                          'build': args.source_commit[:12], 'profiles': [PROFILE],
                          'sha256': sha(output.read_bytes())}, indent=2))
    except (OSError, ValueError, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
