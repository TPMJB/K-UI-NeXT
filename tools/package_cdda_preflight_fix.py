#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle replacement test13 only; retain the previously supplied test14."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile

from package_cdda_calibration import (
    FATFS_FILES, ROOT, archive_name, build_config, git, object_id,
    read_file, sha, write_archive,
)
from package_cdda_mixed import document_copy
from package_cdda_preflight import (
    OBSERVATION_LABEL, PREFLIGHT_BUILD, PREFLIGHT_FILE, PREFLIGHT_LABEL,
    TOY_DESCRIPTOR_BYTES, TOY_DESCRIPTOR_SHA256, loaded_objects, preflight_layout,
)
from runtime_package import flatten_elf, verify

OUTPUT_NAME = 'K-UI-CDDA-Preflight-Fix.zip'
RETAINED_OBSERVER_BUILD = '324c330bdb6c'
RETAINED_OBSERVER_SHA256 = '110140acc2f8b77b12260e0d1eaa3f911902819bbd3ff49889db8f222de69677'


def collect(commit, published_tree):
    if git('status', '--porcelain'):
        raise ValueError('Commit reviewed source before packaging')
    checkpoint = git('rev-parse', 'HEAD')
    tree = git('rev-parse', 'HEAD^{tree}')
    if tree != published_tree:
        raise ValueError('Published source tree differs from the archived checkpoint')
    tracked = set(git('ls-files').splitlines())
    for name in tracked:
        archive_name(name)
    files = {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive path: ' + name)
        files[name] = data

    build = ROOT / PREFLIGHT_BUILD
    config_bytes = read_file(build / 'build-config')
    config = build_config(build / 'build-config')
    if config.get('BUILD') != commit[:12] or config.get('PROFILE') != '13':
        raise ValueError('Wrong source/profile configuration for replacement test13')
    runtime = read_file(build / 'cdda-harness.kui')
    envelope = verify(runtime)
    elf = read_file(build / 'cdda-harness.elf')
    payload, memory = flatten_elf(elf)
    if (envelope['build'] != commit[:12] or runtime[64:] != payload or
            envelope['memory_bytes'] != memory or PREFLIGHT_LABEL not in payload or
            OBSERVATION_LABEL in payload or commit[:12].encode() + b'\0' not in payload):
        raise ValueError('Envelope/ELF/profile identity mismatch for replacement test13')
    layout = preflight_layout(elf, payload, memory)
    loaded = loaded_objects(read_file(build / 'cdda-harness.map'))
    if any(loaded.count(name) != 1 for name in
           (b'cdda_preflight.o', b'cdda_preflight_storage.o', b'cdda_preflight_main.o')):
        raise ValueError('Replacement must link the complete test13 checker exactly once')
    add('KUI/runtime.kui', runtime)
    add(PREFLIGHT_FILE, runtime)
    add('evidence/image-preflight/build-config', config_bytes)
    evidence = {}
    for path in sorted(build.rglob('*')):
        if not path.is_file() or path.suffix not in ('.elf', '.map', '.su'):
            continue
        relative = path.relative_to(build).as_posix()
        data = read_file(path)
        evidence[relative] = sha(data)
        add('evidence/image-preflight/' + relative, data)
    if not any(name.endswith('.su') for name in evidence):
        raise ValueError('Missing compiler stack reports for replacement test13')
    fatfs = {name: read_file(build / 'fatfs' / name) for name in FATFS_FILES}
    for key, value in (('FF_FS_READONLY', 1), ('FF_FS_EXFAT', 1),
                       ('FF_FS_MINIMIZE', 0), ('FF_USE_FASTSEEK', 1)):
        if not re.search(rb'^#define\s+' + key.encode() + rb'\s+' + str(value).encode() + rb'\b',
                         fatfs['ffconf.h'], flags=re.MULTILINE):
            raise ValueError('Unexpected private preflight FatFs configuration: ' + key)
    for name, data in fatfs.items():
        add('source-fatfs/' + name, data)
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
    documents = []
    for path in sorted((ROOT / 'docs').glob('cdda-*.md')):
        source = path.relative_to(ROOT).as_posix()
        documents.extend(((source, source), (source, path.name)))
    for path in sorted((ROOT / 'docs/evidence').glob('cdda-*.md')):
        source = path.relative_to(ROOT).as_posix()
        documents.extend(((source, source), (source, 'evidence/' + path.name)))
    documents.extend((('docs/cdda-preflight-test.md', 'README.md'), ('THIRD_PARTY.md', 'THIRD_PARTY.md')))
    available = set(files) | {destination for _, destination in documents} | {'build.json', 'SHA256SUMS'}
    document_hashes = {}
    for source, destination in documents:
        data = read_file(ROOT / source)
        document_hashes[source] = sha(data)
        add(destination, document_copy(data, source, destination, available, tracked, commit))
    metadata = {
        'source_commit': commit, 'source_tree': tree, 'local_checkpoint': checkpoint,
        'source_dirty': False,
        'source_publication': 'published; supplied tree matches local checkpoint',
        'source_url': source_url, 'source_snapshot': 'source-snapshot.tar',
        'source_snapshot_sha256': sha(snapshot),
        'compiler': subprocess.check_output(['sh-elf-gcc', '--version'], text=True).splitlines()[0],
        'scope': 'Replacement read-only test13 timer initialization, deadline and report paging; original test14 is retained separately',
        'default_profile_number': 13, 'hardware_tested': False,
        'game_files_or_descriptor_installed': False,
        'card_path_configuration_installed': False,
        'retail_observation_installed_on_extraction': False,
        'retained_observer': {
            'profile': 14, 'build': RETAINED_OBSERVER_BUILD,
            'runtime_sha256': RETAINED_OBSERVER_SHA256,
            'source_package': 'K-UI-CDDA-Integration-Tests.zip',
            'file': 'observation/14-retail-observe.kui',
            'included_in_replacement': False, 'rebuild_required': False,
            'install_only_after_complete_preflight_report': True,
        },
        'required_image': {
            'file': 'TOY_COMMANDER.gdi', 'descriptor_bytes': TOY_DESCRIPTOR_BYTES,
            'descriptor_sha256': TOY_DESCRIPTOR_SHA256, 'complete_backing_tracks': 15,
            'location': 'existing complete original game folder outside /KUI/tests/cdda',
            'optional_path_file': '/KUI/tests/cdda/preflight.cfg',
            'existing_configuration_preserved': True,
        },
        'preflight_contract': {
            'read_only': True, 'game_launched': False, 'audio_started': False,
            'required_completed_stages': 6, 'required_failures': 0,
            'complete_backing_tracks': 15, 'audio_tracks': 12, 'data_tracks': 3,
            'complete_map_slot_limit': 64, 'report_pages': 6,
            'automatic_page_seconds': 15, 'read_deadline_seconds': 180,
            'manual_cancel': False, 'post_report_card_io': False,
        },
        'previous_hardware_result': {
            'build': RETAINED_OBSERVER_BUILD, 'profile': 13,
            'page1_pass': True, 'confirmed_report_pages': [1],
            'pages2_through6_confirmed': False,
            'timer_initialization_missing': True, 'read_deadline_active': False,
            'evidence': 'docs/evidence/cdda-preflight-hardware-2026-10-07.md',
        },
        'source_fatfs_sha256': {name: sha(data) for name, data in fatfs.items()},
        'document_source_sha256': document_hashes,
        'profiles': [{
            'profile': 13, 'file': PREFLIGHT_FILE, 'default_card_runtime': 'KUI/runtime.kui',
            'build': envelope['build'], 'runtime_sha256': sha(runtime), 'envelope': envelope,
            'linked_layout': layout, 'build_configuration': config,
            'build_configuration_sha256': sha(config_bytes), 'evidence_sha256': evidence,
        }],
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    if ({name for name in files if name.startswith('KUI/')} != {'KUI/runtime.kui'} or
            any(name.startswith('observation/') for name in files) or
            {name for name in files if name.endswith('.kui')} != {'KUI/runtime.kui', PREFLIGHT_FILE} or
            files['KUI/runtime.kui'] != files[PREFLIGHT_FILE]):
        raise ValueError('Replacement must change only runtime13 and preserve test14/configuration')
    if (git('status', '--porcelain') or git('rev-parse', 'HEAD') != checkpoint or
            git('rev-parse', 'HEAD^{tree}') != tree):
        raise ValueError('Source changed while collecting the replacement package')
    add('SHA256SUMS', ''.join(sha(data) + '  ' + name + '\n'
                            for name, data in sorted(files.items())).encode())
    return files


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--source-commit', required=True, type=object_id)
    parser.add_argument('--published-tree', required=True, type=object_id)
    args = parser.parse_args()
    try:
        output = args.output.resolve()
        if output.name != OUTPUT_NAME:
            raise ValueError('Replacement filename must be ' + OUTPUT_NAME)
        if output.exists() and not output.is_file():
            raise ValueError('Output must be a regular file')
        if output.is_relative_to(ROOT):
            result = subprocess.run(['git', 'ls-files', '--error-unmatch', '--',
                                     output.relative_to(ROOT).as_posix()], cwd=ROOT,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if result.returncode == 0:
                raise ValueError('Output must not overwrite a tracked repository file')
        write_archive(output, collect(args.source_commit, args.published_tree))
        print(json.dumps({'file': str(output), 'bytes': output.stat().st_size,
                          'sha256': sha(output.read_bytes()), 'build': args.source_commit[:12],
                          'profiles': [13], 'retained_observer_build': RETAINED_OBSERVER_BUILD}, indent=2))
    except (OSError, ValueError, UnicodeError, struct.error, subprocess.SubprocessError,
            zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
