#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle the controlled SCI BIOS batch-read and expected-deadline tests."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile
import zlib

from package_cdda_calibration import (
    FATFS_FILES, MARKER, ROOT, archive_name, build_config, git, object_id,
    read_file, sha, valid_boot_markers, write_archive,
)
from package_cdda_mixed import FIXTURES, TMU_HZ, document_copy
from runtime_package import ADDRESS, flatten_elf, verify

from package_cdda_service import (
    CLIENT_BEGIN, CLIENT_LIMIT, MEMORY_BYTES, SERVICE_STACK_BEGIN,
    SERVICE_STACK_LIMIT, client_layout,
)

PROFILE = 9
PROFILES = (
    (9, 'bios-batch-reads', 'cdda-batch', 'batch'),
    (10, 'bios-batch-deadline', 'cdda-batch-fault', 'batch-deadline'),
)
TEST_CONTRACT = {
    'completed_stages': 8,
    'minimum_tmu_seconds': 90,
    'maximum_tmu_seconds': 120,
    'duration_origin': 'after card/control setup; client schedule starts before protocol checks',
    'minimum_client_verified_data_bytes': 8392704,
    'client_verified_bytes_equal_engine_checked_bytes': True,
    'client_engine_ledger_counts': [
        'read_checked', 'execs', 'checks', 'drive_checks', 'expected_refusals',
        'cancels', 'resets', 'partial_cancels', 'partial_resets',
        'progress_polls', 'full_passes', 'class_counts',
    ],
    'checked_bytes_equal_committed_chunks_times': 2048,
    'bios_requests_equal_completions_plus': 2,
    'minimum_integer_abi_checks': 100,
    'abi_checks_equal_vector_calls_minus': 1,
    'completed_audio_actions': 6,
    'drive_checks': 11,
    'expected_protocol_refusals': 8,
    'expected_queued_aborts': 1,
    'expected_stale_checks': 3,
    'accepted_queued_resets': 1,
    'partial_cancels_after_2048_committed_bytes': 1,
    'partial_resets_after_2048_committed_bytes': 1,
    'confirmed_cancel_and_reset_prefix_bytes': 4096,
    'completed_full_8mib_passes': 1,
    'full_pass_counts_only_completed_logical_spans': True,
    'read_size_classes_sectors': [1, 2, 3, 4, 7, 8, 15, 16],
    'covered_size_classes': 8,
    'minimum_completed_reads_per_size_class': 16,
    'minimum_positive_prefix_processing_polls': 32,
    'boundary_reads': 24,
    'minimum_random_reads': 8,
    'maximum_progress_delta_per_exec_bytes': 2048,
    'expected_nested_exec_refusals': 1,
    'expected_descriptor_refusals': 8,
    'controlled_context_checks': 1,
    'client_data_buffer_bytes': 32768,
    'data_canary_bytes_before_and_after': 32,
    'maximum_observed_client_stack_bytes': 65472,
    'maximum_observed_service_stack_bytes': 65472,
    'service_test_errors': 0,
    'final_audio_state': 'STOPPED',
    'bios_vector_restored': True,
    'final_command_queue_state': 'EMPTY',
    'unexpected_failures': 0,
}
FAULT_TEST_CONTRACT = {
    'completed_stages': 8,
    'normal_profile_duration_gate_applies': False,
    'deliberate_no_exec_gap_ms': 200,
    'expected_deadline_refusals': 1,
    'service_test_errors': 0,
    'checked_bytes': 2048,
    'committed_chunks': 1,
    'confirmed_prefix_bytes': 2048,
    'positive_prefix_processing_polls': 2,
    'accepted_requests': 2,
    'successful_terminal_completions': 1,
    'completed_audio_actions': 1,
    'client_vector_calls': 16,
    'engine_vector_calls': 17,
    'integer_abi_checks': 16,
    'abi_checks_equal_vector_calls_minus': 1,
    'client_exec_calls': 3,
    'admitted_engine_execs': 2,
    'check_calls': 7,
    'drive_checks': 2,
    'request_calls': 3,
    'abort_calls': 1,
    'expected_negative_results': 4,
    'expected_negative_cases': [
        'deadline EXEC refusal', 'terminal FAILED/IO CHECK',
        'stale ABORT refusal', 'invalid READ REQUEST refusal',
    ],
    'expected_stale_checks': 1,
    'expected_nested_exec_refusals': 1,
    'expected_descriptor_refusals': 8,
    'controlled_context_checks': 1,
    'failed_read_prefix_bytes': 2048,
    'failed_read_unconfirmed_suffix_untouched': True,
    'data_chunks_after_deadline_refusal': 0,
    'client_verified_bytes_equal_engine_checked_bytes': True,
    'client_engine_ledger_counts': [
        'read_checked', 'checks', 'drive_checks', 'expected_refusals', 'progress_polls',
    ],
    'client_data_buffer_bytes': 32768,
    'data_canary_bytes_before_and_after': 32,
    'maximum_observed_client_stack_bytes': 65472,
    'maximum_observed_service_stack_bytes': 65472,
    'final_owned_audio_keyed_off': True,
    'final_audio_state': 'FAULT',
    'final_service_guard_state': 'FAULT',
    'bios_vector_restored': True,
    'final_command_queue_state': 'EMPTY',
    'unexpected_failures': 0,
}


def profile_identity(profile, payload, map_bytes):
    identities = {
        9: (b'PROFILE9 BATCH /', b'cdda_batch_client.o'),
        10: (b'PROFILE10 DEADLINE /', b'cdda_batch_fault_client.o'),
    }
    label, client_object = identities[profile]
    if label not in payload or any(other_label in payload for number, (other_label, _) in identities.items()
                                   if number != profile):
        raise ValueError('Compiled payload has the wrong profile identity: ' + str(profile))
    loaded = [path.rsplit(b'/', 1)[-1] for path in
              re.findall(rb'^LOAD[ \t]+(\S+)[ \t]*$', map_bytes, flags=re.MULTILINE)]
    if loaded.count(client_object) != 1 or any(other_object in loaded
                                             for number, (_, other_object) in identities.items()
                                             if number != profile):
        raise ValueError('Linked map has the wrong controlled client: ' + str(profile))
    return {'compiled_profile_label': label.decode(), 'linked_client_object': client_object.decode()}


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
        raise ValueError('Batch profile must use the documented 12468720 Hz clock contract')
    bios_header = read_file(ROOT / 'include/kui/cdda_bios.h')
    frozen_constants = {
        'KUI_CDDA_BIOS_PLAY': 20, 'KUI_CDDA_BIOS_PAUSE': 22, 'KUI_CDDA_BIOS_RELEASE': 23,
        'KUI_CDDA_BIOS_AUDIO_FIRST_FAD': 150, 'KUI_CDDA_BIOS_AUDIO_END_FAD': 225,
        'KUI_CDDA_BIOS_AUDIO_FIRST_FRAME': 268128, 'KUI_CDDA_BIOS_AUDIO_END_FRAME': 312228,
        'KUI_CDDA_BIOS_DATA_FIRST_FAD': 45150, 'KUI_CDDA_BIOS_DATA_END_FAD': 49246,
    }
    for name, value in frozen_constants.items():
        if not re.search(rb'^#define\s+' + name.encode() + rb'\s+' +
                         str(value).encode() + rb'u\b', bios_header, flags=re.MULTILINE):
            raise ValueError('BIOS profile differs from the frozen synthetic contract: ' + name)
    batch_header = read_file(ROOT / 'include/kui/cdda_bios_batch.h')
    for name, value in {
            'KUI_CDDA_BIOS_BATCH_MAX_SECTORS': 16,
            'KUI_CDDA_BIOS_BATCH_CHUNK_BYTES': 2048,
            'KUI_CDDA_BIOS_BATCH_MAX_BYTES': 32768}.items():
        if not re.search(rb'^#define\s+' + name.encode() + rb'\s+' +
                         str(value).encode() + rb'u\b', batch_header, flags=re.MULTILINE):
            raise ValueError('Batch profile differs from the frozen read bound: ' + name)
    vector_header = read_file(ROOT / 'include/kui/cdda_vector.h')
    if not re.search(rb'^#define\s+KUI_CDDA_BIOS_GD_VECTOR_ADDRESS\s+0x8c0000bcu\b',
                     vector_header, flags=re.MULTILINE):
        raise ValueError('BIOS profile must own the frozen 0x8c0000bc vector slot')
    gd_header = read_file(ROOT / 'include/kui/gd_service.h')
    if not re.search(rb'^#define\s+KUI_GD_VECTOR_ADDRESS\s+0x8c0000bcu\b',
                     gd_header, flags=re.MULTILINE):
        raise ValueError('Client and adapter GD vector slots must match')

    runtimes = []
    private_fatfs = None
    for profile, name, build_directory, evidence_directory in PROFILES:
        build = ROOT / 'build' / build_directory
        config_bytes = read_file(build / 'build-config')
        config = build_config(build / 'build-config')
        if config.get('BUILD') != commit[:12] or config.get('PROFILE') != str(profile):
            raise ValueError('Wrong build configuration for profile ' + str(profile))
        runtime = read_file(build / 'cdda-harness.kui')
        info = verify(runtime)
        elf = read_file(build / 'cdda-harness.elf')
        payload, memory = flatten_elf(elf)
        if (info['build'] != commit[:12] or runtime[64:] != payload or
                memory != MEMORY_BYTES or info['memory_bytes'] != memory or
                payload.count(MARKER) != 1 or valid_boot_markers(payload) != 1):
            raise ValueError('Envelope/source/layout/boot-marker mismatch for profile ' + str(profile))
        linked_layout = client_layout(elf, payload)
        runtime_name = f'runtimes/{profile:02d}-{name}.kui'
        add(runtime_name, runtime)
        if profile == PROFILE:
            add('KUI/runtime.kui', runtime)
        evidence_prefix = 'evidence/' + evidence_directory + '/'
        add(evidence_prefix + 'cdda-harness.elf', elf)
        map_bytes = read_file(build / 'cdda-harness.map')
        identity = profile_identity(profile, payload, map_bytes)
        add(evidence_prefix + 'cdda-harness.map', map_bytes)
        add(evidence_prefix + 'build-config', config_bytes)
        stack_hashes = {}
        stack_files = sorted(build.rglob('*.su'))
        if not stack_files:
            raise ValueError('Missing compiler stack reports for profile ' + str(profile))
        for path in stack_files:
            relative = path.relative_to(build).as_posix()
            data = read_file(path)
            add(evidence_prefix + 'stack/' + relative, data)
            stack_hashes[relative] = sha(data)

        inputs = {entry: read_file(build / 'fatfs' / entry) for entry in FATFS_FILES}
        configuration = inputs['ffconf.h'].decode('utf-8')
        for key, value in (('FF_FS_READONLY', 1), ('FF_FS_EXFAT', 1)):
            if not re.search(r'^#define\s+' + key + r'\s+' + str(value) + r'\b',
                             configuration, flags=re.MULTILINE):
                raise ValueError('Unexpected private FatFs configuration: ' + key)
        if private_fatfs is not None and inputs != private_fatfs:
            raise ValueError('The two profiles must use identical private FatFs inputs')
        private_fatfs = inputs
        runtimes.append({
            'profile': profile, 'name': name, 'file': runtime_name,
            'build': info['build'], 'hardware_tested': False,
            'elf_file': evidence_prefix + 'cdda-harness.elf',
            'map_file': evidence_prefix + 'cdda-harness.map',
            'build_configuration_file': evidence_prefix + 'build-config',
            'stack_reports_directory': evidence_prefix + 'stack/',
            'runtime_sha256': sha(runtime), 'elf_sha256': sha(elf), 'map_sha256': sha(map_bytes),
            'payload_bytes': info['payload_bytes'], 'payload_crc32': info['crc32'],
            'memory_footprint_bytes': memory,
            'boot_marker': 'one unpatched AUTO marker; bootstrap must select SCI',
            'build_configuration': config, 'build_configuration_sha256': sha(config_bytes),
            'stack_reports_sha256': stack_hashes,
            'linked_client_layout': linked_layout,
            'profile_identity': identity,
            'fatfs_inputs_sha256': {entry: sha(data) for entry, data in inputs.items()},
            'test_contract': TEST_CONTRACT if profile == PROFILE else FAULT_TEST_CONTRACT,
        })
    if len({entry['runtime_sha256'] for entry in runtimes}) != len(PROFILES):
        raise ValueError('Normal and expected-deadline runtimes must be distinct')
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
    document_paths.extend((('docs/cdda-batch-test.md', 'README.md'),
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
        'scope': 'Controlled SCI homebrew client using owned BIOS multi-sector reads alongside CDDA; no retail integration',
        'default_profile': 'bios-batch-reads', 'default_profile_number': PROFILE,
        'hardware_tested': False,
        'runtimes': runtimes,
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
        'service_scope': {
            'entry': 'controlled client actual owned GD BIOS vector with raw r4-r7 calls',
            'interrupt_hook': False, 'retail_game': False,
            'sr_vbr_fpu_game_sharing_established': False,
            'gbr_interrupt_state_sharing_established': False,
            'integer_call_probe': 'r8-r14 and PR; no broken-SP/nonreturn recovery',
            'tested_buffer_address_alias': 'cached P1 only',
            'physical_p2_buffer_cache_coherence_established': False,
            'engine_code_base': ADDRESS,
            'engine_stack': [0x8c200000, 0x8c210000],
            'service_stack': [SERVICE_STACK_BEGIN, SERVICE_STACK_LIMIT],
            'client_code_region': [CLIENT_BEGIN, CLIENT_LIMIT],
            'client_stack': [0x8c310000, 0x8c320000],
        },
        'bios_contract': {
            'header': 'include/kui/cdda_bios_batch.h', 'header_sha256': sha(batch_header),
            'base_header': 'include/kui/cdda_bios.h', 'base_header_sha256': sha(bios_header),
            'vector_header': 'include/kui/cdda_vector.h', 'vector_header_sha256': sha(vector_header),
            'gd_header': 'include/kui/gd_service.h', 'gd_header_sha256': sha(gd_header),
            'vector_address': 0x8c0000bc,
            'raw_registers': ['r4', 'r5', 'r6', 'r7'],
            'required_r6': 0,
            'request_function': 0, 'check_function': 1, 'exec_function': 2,
            'play_command': 20, 'pause_command': 22, 'release_command': 23,
            'stop_command': 33, 'nop_command': 29, 'read_command': 16,
            'play_tracks': [1, 1], 'accepted_repeat_values': [0, 15],
            'unsupported_finite_repeat_values': list(range(1, 15)),
            'play2_supported': False, 'datatype_supported': False,
            'dma_supported': False, 'retail_supported': False,
            'interrupt_completion_supported': False,
            'maximum_read_sectors': 16, 'data_sector_bytes': 2048,
            'maximum_read_request_bytes': 32768,
            'maximum_physical_chunk_bytes_per_exec': 2048,
            'audio_track': {'number': 1, 'fad_range': [150, 225],
                            'fixture_frame_range': [268128, 312228]},
            'data_track': {'number': 2, 'fad_range': [45150, 49246],
                           'fixture_bytes': 8388608},
            'range_endpoints': 'exclusive',
            'queue_reset_admission_states': ['STOPPED', 'EOF'],
            'reset_while_playing_or_paused_refused': True,
            'terminal_success_after_full_exec_lease': True,
            'read_progress_after_full_exec_lease': True,
            'failed_read_accepted_bytes': 'previously committed whole-sector prefix',
            'failed_read_destination': 'retain only confirmed prefix; discard unconfirmed potentially touched remainder',
            'between_chunk_state': 'QUEUED; CHECK reports PROCESSING and committed prefix',
            'queued_abort_retains_committed_prefix': True,
            'queue_reset': 'invalidate pending handles and reported progress; no direct audio change',
            'vector_lifecycle': 'save, install, read back, restore on every returning path',
        },
        'test_contract': TEST_CONTRACT,
        'test_contracts': {'09': TEST_CONTRACT, '10': FAULT_TEST_CONTRACT},
        'sound_ring_bytes': 65536,
        'private_engine_stack_bytes': 65536, 'private_service_stack_bytes': 65536,
        'private_client_stack_bytes': 65536,
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
                          'build': args.source_commit[:12], 'profiles': [profile for profile, *_ in PROFILES],
                          'sha256': sha(output.read_bytes())}, indent=2))
    except (OSError, ValueError, struct.error, subprocess.SubprocessError, zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
