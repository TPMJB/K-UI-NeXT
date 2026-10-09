#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle independent generated-disc and selected-owned-track CDDA tests."""
import argparse
import json
from pathlib import Path
import re
import struct
import subprocess
import zipfile

from package_cdda_calibration import (
    FATFS_FILES, MARKER, ROOT, archive_name, build_config, git, object_id,
    read_file, sha, valid_boot_markers, write_archive,
)
from package_cdda_mixed import TMU_HZ, document_copy
from cdda_disc_fixture import (
    TOY_AUDIO_BYTES, TOY_AUDIO_SHA256, TOY_DESCRIPTOR_BYTES,
    TOY_DESCRIPTOR_SHA256, fixture_metadata, make_fixture,
)
from runtime_package import ADDRESS, flatten_elf, verify

from package_cdda_service import (
    CLIENT_BEGIN, CLIENT_LIMIT, MEMORY_BYTES, SERVICE_STACK_BEGIN,
    SERVICE_STACK_LIMIT, client_layout,
)

PROFILE = 11
PROFILES = (
    (11, 'disc-map', 'cdda-disc', 'disc-map'),
    (12, 'toy-track14', 'cdda-disc-toy', 'toy-track14'),
)
GENERATED_FILES = {
    'fixture.gdi': (168, '5d417fc96972c781e842d4254b0efa25e7429ca9002fdbd158446c51d6bb01af'),
    'disc01.bin': (32768, 'c3536962df86bb154adaa083060c844fb3a1c9a46cbde16fd6616ac9b7710063'),
    'disc02.raw': (176400, 'bf17f46a7cf8dc9d96ff3c4831db752cd338c74536bd90ed3bf1e09d8ce80bba'),
    'disc03.bin': (131072, 'ad6e2a76be737161c100185904895cda9355b0b7dee45b0b0d828054f8b69688'),
    'disc04.raw': (353312, 'a5217b2afa3d464335e97e24de6133248be3cc6fa085df84391670bafdd470d9'),
    'disc05.raw': (530224, '523ba5113c44d9ec568bba39f44ece8a70a2c2222d7d3b4eaabfc31b5c99bf2a'),
    'disc06.bin': (75264, '5e4bb95b3c5d6900b584fa44a8ef51132e61fe59de0e51bdabad1ee2a46bdefe'),
}
COMMON_CONTRACT = {
    'completed_stages': 8,
    'bios_requests_equal_completions': True,
    'minimum_integer_abi_checks': 20,
    'abi_checks_equal_vector_calls_minus': 1,
    'expected_nested_exec_refusals': 1,
    'expected_descriptor_refusals': 8,
    'controlled_context_checks': 1,
    'data_canary_bytes_before_and_after': 32,
    'maximum_observed_client_stack_bytes': 65472,
    'maximum_observed_service_stack_bytes': 65472,
    'accepted_queued_aborts': 0,
    'accepted_queued_resets': 0,
    'service_test_errors': 0,
    'final_audio_state': 'STOPPED',
    'bios_vector_restored': True,
    'final_command_queue_state': 'EMPTY',
    'unexpected_failures': 0,
    'client_verified_bytes_equal_engine_checked_bytes': True,
    'client_engine_ledger_counts': [
        'read_checked', 'execs', 'checks', 'drive_checks', 'expected_refusals',
        'polls', 'actions', 'toc_checks', 'map_checks', 'eof_checks', 'track_switches',
    ],
}
TEST_CONTRACT = {
    **COMMON_CONTRACT,
    'maximum_tmu_seconds': 60,
    'client_data_buffer_bytes': 32768,
    'client_toc_buffer_bytes': 408,
    'accepted_requests': 16,
    'successful_terminal_completions': 16,
    'completed_audio_actions': 7,
    'drive_checks': 11,
    'expected_protocol_refusals': 12,
    'expected_stale_checks': 3,
    'checked_bytes': 196608,
    'committed_chunks': 96,
    'positive_prefix_processing_polls': 90,
    'derived_toc_completions': 2,
    'metadata_and_range_checks': 16,
    'actual_eof_checks': 2,
    'active_track_switches': 3,
    'complete_disc_map': True,
    'disc_map_tracks': 6,
    'audio_eof_frames': {'4': 88200, '5': 132300},
    'checked_data_tracks': {'3': 64, '6': 32},
    'non_pcm_audio_prefix_bytes': {'4': 512, '5': 1024},
}
TOY_TEST_CONTRACT = {
    **COMMON_CONTRACT,
    'maximum_tmu_seconds': 90,
    'client_data_buffer_bytes': 2048,
    'accepted_requests': 5,
    'successful_terminal_completions': 5,
    'completed_audio_actions': 4,
    'drive_checks': 6,
    'expected_protocol_refusals': 10,
    'expected_stale_checks': 2,
    'checked_bytes': 0,
    'committed_chunks': 0,
    'positive_prefix_processing_polls': 0,
    'derived_toc_completions': 0,
    'metadata_and_range_checks': 10,
    'actual_eof_checks': 1,
    'active_track_switches': 0,
    'complete_disc_map': False,
    'disc_map_tracks': 1,
    'selected_track': 14,
    'actual_eof_frame': 1805748,
    'backed_fad_range': [374351, 377422],
    'next_descriptor_track_start_fad': 377572,
    'unbacked_gap_sectors': 150,
    'complete_toc_refused': True,
}


def fixture_inputs(directory):
    """Validate frozen input bytes against independent regeneration and pins."""
    generated = make_fixture()
    if set(generated) != set(GENERATED_FILES):
        raise ValueError('Generated disc fixture file set changed')
    files = {}
    for name, (expected_bytes, expected_hash) in GENERATED_FILES.items():
        data = read_file(directory / name)
        if (len(data) != expected_bytes or sha(data) != expected_hash or
                data != generated[name]):
            raise ValueError('Generated disc fixture differs from its frozen input: ' + name)
        files[name] = data
    metadata = fixture_metadata(generated)
    metadata_bytes = read_file(directory / 'fixture.json')
    if metadata_bytes != (json.dumps(metadata, indent=2) + '\n').encode():
        raise ValueError('Generated disc fixture metadata changed')
    descriptor = read_file(directory / 'TOY_COMMANDER.gdi')
    if len(descriptor) != TOY_DESCRIPTOR_BYTES or sha(descriptor) != TOY_DESCRIPTOR_SHA256:
        raise ValueError('Selected Toy descriptor differs from the inspected upload')
    return files, metadata, metadata_bytes, descriptor


def profile_identity(profile, payload, map_bytes):
    identities = {
        11: (b'PROFILE11 DISC /', b'cdda_disc_client.o'),
        12: (b'PROFILE12 TOY /', b'cdda_disc_toy_client.o'),
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
        raise ValueError('Disc profile must use the documented 12468720 Hz clock contract')
    bios_header = read_file(ROOT / 'include/kui/cdda_bios.h')
    for name, value in {
            'KUI_CDDA_BIOS_PLAY': 20, 'KUI_CDDA_BIOS_PAUSE': 22,
            'KUI_CDDA_BIOS_RELEASE': 23}.items():
        if not re.search(rb'^#define\s+' + name.encode() + rb'\s+' +
                         str(value).encode() + rb'u\b', bios_header, flags=re.MULTILINE):
            raise ValueError('Disc profile differs from the frozen audio command: ' + name)
    disc_header = read_file(ROOT / 'include/kui/cdda_disc.h')
    batch_header = read_file(ROOT / 'include/kui/cdda_disc_bios.h')
    for name, value in {
            'KUI_CDDA_DISC_BIOS_MAX_SECTORS': 16,
            'KUI_CDDA_DISC_BIOS_CHUNK_BYTES': 2048,
            'KUI_CDDA_DISC_BIOS_MAX_BYTES': 32768}.items():
        if not re.search(rb'^#define\s+' + name.encode() + rb'\s+' +
                         str(value).encode() + rb'u\b', batch_header, flags=re.MULTILINE):
            raise ValueError('Disc profile differs from the frozen read bound: ' + name)
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
            'test_contract': TEST_CONTRACT if profile == PROFILE else TOY_TEST_CONTRACT,
        })
    if len({entry['runtime_sha256'] for entry in runtimes}) != len(PROFILES):
        raise ValueError('Generated-disc and selected-Toy runtimes must be distinct')
    for name, data in private_fatfs.items():
        add('source-fatfs/' + name, data)

    fixtures, disc_metadata, metadata_bytes, toy_descriptor = fixture_inputs(ROOT / 'build/cdda/disc-fixture')
    fixture_input_hashes = {}
    for name, data in fixtures.items():
        add('KUI/tests/cdda/disc/' + name, data)
        fixture_input_hashes[name] = sha(data)
    add('fixtures/disc.json', metadata_bytes)
    fixture_input_hashes['fixture.json'] = sha(metadata_bytes)
    add('KUI/tests/cdda/TOY_COMMANDER.gdi', toy_descriptor)
    fixture_input_hashes['TOY_COMMANDER.gdi'] = sha(toy_descriptor)
    fixture_generator = read_file(ROOT / 'tools/cdda_disc_fixture.py')

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
    document_paths.extend((('docs/cdda-disc-test.md', 'README.md'),
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
        'scope': 'Controlled SCI clients exercise a complete generated GDI and a separately selected owned audio backing through the owned BIOS vector; no retail boot',
        'default_profile': 'disc-map', 'default_profile_number': PROFILE,
        'hardware_tested': False,
        'runtimes': runtimes,
        'clock_contract': {
            'tmu_hz': TMU_HZ, 'configured_aica_pitch': 0,
            'automatic_tuning': False, 'independent_console_frequency_measurement': False,
            'header': 'include/kui/cdda_clock.h', 'header_sha256': sha(clock_header),
        },
        'owned_game_audio_or_executable_included': False,
        'game_descriptor_metadata_included': True,
        'required_game_files': [{
            'profile': 12, 'file': 'track14.raw',
            'card_path': '/KUI/tests/cdda/track14.raw',
            'bytes': TOY_AUDIO_BYTES, 'sha256': TOY_AUDIO_SHA256,
            'included': False, 'source': 'retain the existing personally supplied backing',
        }],
        'fixtures': [disc_metadata],
        'fixture_generator': 'tools/cdda_disc_fixture.py',
        'fixture_generator_sha256': sha(fixture_generator),
        'selected_game_descriptor': {
            'card_path': '/KUI/tests/cdda/TOY_COMMANDER.gdi',
            'bytes': TOY_DESCRIPTOR_BYTES, 'sha256': TOY_DESCRIPTOR_SHA256,
            'source': 'exact uploaded factual GDI descriptor; metadata only',
            'selected_track': 14, 'complete_map': False,
            'backed_fad_range': [374351, 377422],
            'next_track_start_fad': 377572, 'unbacked_gap_sectors': 150,
        },
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
            'header': 'include/kui/cdda_disc_bios.h', 'header_sha256': sha(batch_header),
            'disc_header': 'include/kui/cdda_disc.h', 'disc_header_sha256': sha(disc_header),
            'base_header': 'include/kui/cdda_bios.h', 'base_header_sha256': sha(bios_header),
            'vector_header': 'include/kui/cdda_vector.h', 'vector_header_sha256': sha(vector_header),
            'gd_header': 'include/kui/gd_service.h', 'gd_header_sha256': sha(gd_header),
            'vector_address': 0x8c0000bc,
            'raw_registers': ['r4', 'r5', 'r6', 'r7'],
            'required_r6': 0,
            'request_function': 0, 'check_function': 1, 'exec_function': 2,
            'play_command': 20, 'pause_command': 22, 'release_command': 23,
            'stop_command': 33, 'nop_command': 29, 'read_command': 16,
            'play_tracks': 'one fully backed audio track; equal start/end track numbers',
            'accepted_repeat_values': [0, 15],
            'gettoc2_command': 19, 'toc_bytes': 408, 'toc_areas': [0, 1],
            'toc_scope': 'complete generated backing only; no firmware-conformance claim',
            'unsupported_finite_repeat_values': list(range(1, 15)),
            'play2_supported': False, 'datatype_supported': False,
            'dma_supported': False, 'retail_supported': False,
            'interrupt_completion_supported': False,
            'maximum_read_sectors': 16, 'data_sector_bytes': 2048,
            'maximum_read_request_bytes': 32768,
            'maximum_returned_data_bytes_per_exec': 2048,
            'maximum_physical_data_sector_bytes_per_exec': 2352,
            'complete_generated_gdi_map_profile': 11,
            'selected_audio_map_profile': 12,
            'source_extent_required': 'complete requested backing; no track crossing or gap filler',
            'raw_data_stride': 2352, 'raw_mode1_payload_offset': 16,
            'cooked_data_stride': 2048,
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
        'test_contracts': {'11': TEST_CONTRACT, '12': TOY_TEST_CONTRACT},
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
