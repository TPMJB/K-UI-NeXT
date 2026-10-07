#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle only replacement test14 with CPU/TMU observations and failed-read details."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import zipfile

from package_cdda_calibration import (
    ROOT, archive_name, build_config, git, object_id, read_file, sha, write_archive,
)
from package_cdda_mixed import document_copy
from package_cdda_preflight import (
    OBSERVATION_FILE, OBSERVATION_LABEL, PREFLIGHT_LABEL,
    TOY_DESCRIPTOR_BYTES, TOY_DESCRIPTOR_SHA256, observation_layout,
)
from retail_package import inspect_retail
from runtime_package import flatten_elf

OUTPUT_NAME = 'K-UI-CDDA-Retail-Diagnostic.zip'
BUILD_DIRECTORY = 'build/retail-observe-fix'
README_SOURCE = 'docs/cdda-observe-fix-test.md'
HARDWARE_RECORD = 'docs/evidence/cdda-retail-observe-hardware-2026-10-07.md'
ORIGINAL_OBSERVER_BUILD = '324c330bdb6c'
ORIGINAL_OBSERVER_SHA256 = '110140acc2f8b77b12260e0d1eaa3f911902819bbd3ff49889db8f222de69677'
DIAGNOSTIC_LEGENDS = (
    b'AUDIO NOT SAMPLED', b'CALL 20 21 BAD', b'FN ARG FLAG G BAD',
    b'CMD LBA N BPS', b'IO LBA N DONE STEP',
    b'IMG PRE STOP SD', b'DST BLK ERR CARD',
)
SOUND_LEGENDS = (b'CALL 20 21 BAD N', b'CPU TMU AICA SKIP', b'KEY0 KEY1', b'DMA F L OR CHG')
# These are the live sampler's known device literals, including its DMA and
# channel bases. Their absence is an additional build guard, not an exhaustive
# proof about every address an arbitrary instruction sequence could compute.
SAMPLER_MMIO_LITERALS = (
    0xa05f688c, 0xa05f7800, 0xa05f7814, 0xa05f7818,
    0xa0700000, 0xa0702800, 0xa0702c00, 0xa070289c, 0xa07028a0,
)


def collect(commit, published_tree):
    if git('status', '--porcelain'):
        raise ValueError('Commit reviewed source before packaging')
    checkpoint = git('rev-parse', 'HEAD')
    tree = git('rev-parse', 'HEAD^{tree}')
    if published_tree != tree:
        raise ValueError('Published source tree differs from the archived checkpoint')
    tracked = {name for name in subprocess.check_output(
        ['git', 'ls-files', '-z'], cwd=ROOT).decode('utf-8').split('\0') if name}
    for name in tracked:
        archive_name(name)
    if README_SOURCE not in tracked or HARDWARE_RECORD not in tracked:
        raise ValueError('Commit the replacement checklist and original hardware record')
    files = {}
    inputs = {}

    def add(name, data):
        archive_name(name)
        if name in files:
            raise ValueError('Duplicate archive path: ' + name)
        files[name] = data

    def remember(path):
        data = read_file(path)
        digest = sha(data)
        if path in inputs and inputs[path] != digest:
            raise ValueError('Build input changed while collecting: ' + str(path.relative_to(ROOT)))
        inputs[path] = digest
        return data

    build = ROOT / BUILD_DIRECTORY
    config_bytes = remember(build / 'build-config')
    config = build_config(build / 'build-config')
    required_config = {'BUILD': commit[:12], 'PROFILE': '14', 'LOW': '1',
                       'SLOTS': '64', 'SCI': '1', 'AUDIO': '0'}
    if any(config.get(key) != value for key, value in required_config.items()):
        raise ValueError('Wrong source/profile/transport/audio configuration for replacement test14')
    runtime = remember(build / 'retail-observe.kui')
    envelope = inspect_retail(runtime)
    entry_elf = remember(build / 'entry.elf')
    payload, memory = flatten_elf(entry_elf)
    if (envelope['build'] != commit[:12] or runtime[64:] != payload or
            envelope['memory_bytes'] != memory or OBSERVATION_LABEL not in payload or
            PREFLIGHT_LABEL in payload or commit[:12].encode() + b'\0' not in payload):
        raise ValueError('Envelope/ELF/profile identity mismatch for replacement test14')
    if (any(legend + b'\0' not in payload for legend in DIAGNOSTIC_LEGENDS) or
            any(legend + b'\0' in payload for legend in SOUND_LEGENDS)):
        raise ValueError('Replacement must retain failed-read details and report audio as unsampled')
    if any(struct.pack('<I', address) in payload for address in SAMPLER_MMIO_LITERALS):
        raise ValueError('Replacement retains a live audio sampler device-address literal')
    # observation_layout reads and cross-checks these exact three linked images,
    # their embedded copies, manifest, guards, LTO stack reports and instructions.
    for name in ('stage.elf', 'resident-sci.elf', 'stage.bin', 'resident-sci.bin'):
        remember(build / name)
    layout = observation_layout(build, entry_elf)
    layout['disabled_audio_sampler_check'] = {
        'known_device_literals_absent': [f'0x{address:08x}' for address in SAMPLER_MMIO_LITERALS],
        'old_sound_report_legends_absent': True,
        'scope': 'Additional literal/legend build guard; not an exhaustive computed-address proof',
    }
    add(OBSERVATION_FILE, runtime)
    add('evidence/retail-observe/build-config', config_bytes)
    evidence = {}
    for path in sorted(build.rglob('*')):
        if not path.is_file() or path.suffix not in ('.elf', '.map', '.su'):
            continue
        relative = path.relative_to(build).as_posix()
        data = remember(path)
        evidence[relative] = sha(data)
        add('evidence/retail-observe/' + relative, data)
    if not all(name in evidence for name in ('entry.elf', 'entry.map', 'stage.elf',
                                             'stage.map', 'resident-sci.elf', 'resident-sci.map')):
        raise ValueError('Missing actual linked ELF/map evidence for replacement test14')
    if not any(name.endswith('.su') for name in evidence):
        raise ValueError('Missing compiler stack reports for replacement test14')

    # Test14 has no FatFs dependency. Its corresponding repository source is
    # included in full; only the linked native SCI reader is distributed here.
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
    documents.extend(((README_SOURCE, 'README.md'), ('THIRD_PARTY.md', 'THIRD_PARTY.md')))
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
        'scope': 'Replacement test14: CPU/TMU and accepted PLAY observations, terminal failed-read trace; live AICA/G2 sampling disabled',
        'default_profile_number': 14, 'hardware_tested': False,
        'numerical_pass_target': None,
        'game_files_or_descriptor_installed': False,
        'card_path_configuration_installed': False,
        'retail_observation_installed_on_extraction': False,
        'working_runtime_replaced_in_bundle': False,
        'previous_observer': {
            'profile': 14, 'build': ORIGINAL_OBSERVER_BUILD,
            'runtime_sha256': ORIGINAL_OBSERVER_SHA256,
            'source_package': 'K-UI-CDDA-Integration-Tests.zip',
            'file': OBSERVATION_FILE, 'included_in_replacement': False,
            'superseded_by_this_diagnostic': True,
            'hardware_result': 'Early GD EXEC image-service I/O stop; underlying failed read was not printed',
            'accepted_play20_requests': 0, 'accepted_play21_requests': 0,
            'evidence': HARDWARE_RECORD,
            'evidence_source_sha256': document_hashes[HARDWARE_RECORD],
        },
        'required_image': {
            'file': 'TOY_COMMANDER.gdi', 'descriptor_bytes': TOY_DESCRIPTOR_BYTES,
            'descriptor_sha256': TOY_DESCRIPTOR_SHA256, 'complete_backing_tracks': 15,
            'audio_tracks': 12, 'data_tracks': 3,
            'location': 'same existing complete original game folder used for passing test13',
            'copied_game_content_included': False,
            'repeat_tests00_through13_required_for_unchanged_image_card': False,
        },
        'observation_contract': {
            'installation': 'manual only after complete profile13 PASS',
            'storage_transport': 'SCI', 'reader': 'native standard',
            'complete_manifest_slot_limit': 64,
            'audio_extents_required': True, 'silent_audio_extent_drop_allowed': False,
            'integrated_cdda_playback': False, 'live_aica_g2_sampling': False,
            'new_sound_or_timer_ownership': False,
            'return_control': 'A+B+X+Y+Start', 'report_pages': 4,
            'terminal_fault_trace_pages': 1,
            'each_page_frames': 1200, 'report_pages_repeat': False,
            'report_returns_to_firmware': True,
            'resident_fault_report_preserved': True,
            'resident_fault_final_page_held_until_power_off': True,
            'terminal_failed_read_trace': True,
            'observations': ['accepted PLAY20/PLAY21 parameters', 'caller CPU state', 'TMU state'],
            'resource_samples_establish_ownership': False,
            'call_bound_sampling_bounds_idle_service_gaps': False,
            'artificial_console_pass_counters': False,
            'numerical_pass_target': None,
        },
        'document_source_sha256': document_hashes,
        'profiles': [{
            'profile': 14, 'file': OBSERVATION_FILE,
            'install_only_after_profile13_pass': True,
            'manual_card_target': '/KUI/apps/games/retail-boot.kui',
            'required_runtime': 'retained working 1.8.5 runtime',
            'build': envelope['build'], 'runtime_sha256': sha(runtime), 'envelope': envelope,
            'linked_layout': layout, 'build_configuration': config,
            'build_configuration_sha256': sha(config_bytes), 'evidence_sha256': evidence,
            'hardware_tested': False, 'numerical_pass_target': None,
        }],
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    if ({name for name in files if name.endswith('.kui')} != {OBSERVATION_FILE} or
            any(name.startswith('KUI/') for name in files) or
            any(name.startswith('runtimes/') for name in files) or
            any(name.startswith('source-fatfs/') for name in files)):
        raise ValueError('Replacement must contain only manually installed observation14')
    if (git('status', '--porcelain') or git('rev-parse', 'HEAD') != checkpoint or
            git('rev-parse', 'HEAD^{tree}') != tree):
        raise ValueError('Source changed while collecting the replacement package')
    for path, digest in inputs.items():
        if sha(read_file(path)) != digest:
            raise ValueError('Build input changed while collecting: ' + str(path.relative_to(ROOT)))
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
                          'profiles': [14], 'superseded_observer_build': ORIGINAL_OBSERVER_BUILD}, indent=2))
    except (OSError, ValueError, UnicodeError, struct.error, subprocess.SubprocessError,
            zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
