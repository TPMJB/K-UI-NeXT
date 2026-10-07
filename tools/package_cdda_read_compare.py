#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle two separately installed test14 SCI read diagnostics, PIO then DMA eligible."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import zipfile
import zlib

from package_cdda_calibration import (
    ROOT, archive_name, build_config, git, object_id, read_file, sha, write_archive,
)
from package_cdda_mixed import document_copy
from package_cdda_observe_fix import (
    DIAGNOSTIC_LEGENDS, ORIGINAL_OBSERVER_BUILD, ORIGINAL_OBSERVER_SHA256,
    SAMPLER_MMIO_LITERALS, SOUND_LEGENDS,
)
from package_cdda_preflight import (
    PREFLIGHT_LABEL, TOY_DESCRIPTOR_BYTES, TOY_DESCRIPTOR_SHA256, observation_layout,
)
from retail_package import inspect_retail
from runtime_package import flatten_elf

OUTPUT_NAME = 'K-UI-CDDA-Read-Comparison.zip'
README_SOURCE = 'docs/cdda-read-compare-test.md'
HARDWARE_RECORD = 'docs/evidence/cdda-retail-diagnostic-hardware-2026-10-07.md'
PREVIOUS_DIAGNOSTIC_BUILD = '72802334ebfc'
PREVIOUS_DIAGNOSTIC_SHA256 = 'ae8774c67e8b1c7a28ba94de9c569aa0c6220164fd6740c41e07ad17409cc939'
VARIANTS = (
    {'name': 'pio', 'order': 1, 'build_directory': 'build/retail-read-pio',
     'file': 'observation/14-pio.kui', 'sci_pio': '1', 'label': b'PROFILE14 PIO /',
     'mode': 'Existing programmed block path; SCI payload DMA disabled'},
    {'name': 'dma', 'order': 2, 'build_directory': 'build/retail-read-dma',
     'file': 'observation/14-dma.kui', 'sci_pio': '0', 'label': b'PROFILE14 DMA /',
     'mode': 'Existing DMA-eligible block path; guarded PIO fallback retained'},
)
DMAC_CHANNEL1_LITERALS = (0xffa00010, 0xffa00014, 0xffa00018, 0xffa0001c, 0xffa00040)
REMOVED_SCI_TRACE_LEGENDS = (b'PATH PH POLL SCI', b'CHCR OR LEFT SD')


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
        raise ValueError('Commit the comparison checklist and latest hardware record')
    if b'READ_COMPARE_CHECKLIST_DRAFT' in read_file(ROOT / README_SOURCE):
        raise ValueError('Freeze the comparison checklist against both native implementations')
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

    variants = []
    for variant in VARIANTS:
        build = ROOT / variant['build_directory']
        config_bytes = remember(build / 'build-config')
        config = build_config(build / 'build-config')
        required_config = {'BUILD': commit[:12], 'PROFILE': '14', 'LOW': '1',
                           'SLOTS': '64', 'SCI': '1', 'AUDIO': '0',
                           'SCI_PIO': variant['sci_pio'], 'SCI_COMPARE': '1',
                           'SCI_OBSERVE': '0', 'STAGE_SCI_OBSERVE': '0'}
        if any(config.get(key) != value for key, value in required_config.items()):
            raise ValueError('Wrong source/profile/SCI-mode configuration for ' + variant['name'])
        runtime = remember(build / 'retail-observe.kui')
        envelope = inspect_retail(runtime)
        entry_elf = remember(build / 'entry.elf')
        payload, memory = flatten_elf(entry_elf)
        other_labels = [other['label'] for other in VARIANTS if other['name'] != variant['name']]
        if (envelope['build'] != commit[:12] or runtime[64:] != payload or
                envelope['memory_bytes'] != memory or variant['label'] + b'\0' not in payload or
                any(label + b'\0' in payload for label in other_labels) or
                PREFLIGHT_LABEL in payload or commit[:12].encode() + b'\0' not in payload):
            raise ValueError('Envelope/ELF/variant identity mismatch for ' + variant['name'])
        if (any(legend + b'\0' not in payload for legend in DIAGNOSTIC_LEGENDS) or
                any(legend + b'\0' in payload for legend in SOUND_LEGENDS)):
            raise ValueError('Comparison must retain failed-read details and report audio as unsampled')
        if any(legend + b'\0' in payload for legend in REMOVED_SCI_TRACE_LEGENDS):
            raise ValueError('Comparison must not claim the rejected first-wait trace')
        if any(struct.pack('<I', address) in payload for address in SAMPLER_MMIO_LITERALS):
            raise ValueError('Comparison retains a live audio sampler device-address literal')
        for name in ('stage.elf', 'resident-sci.elf', 'stage.bin', 'resident-sci.bin'):
            remember(build / name)
        layout = observation_layout(build, entry_elf)
        # This supplemental literal guard supplements the source/host/native
        # audits. It does not prove every computed MMIO address or force DMA
        # in the eligible variant when the normal borrowing guards refuse it.
        present_dma = [f'0x{address:08x}' for address in DMAC_CHANNEL1_LITERALS
                       if struct.pack('<I', address) in payload]
        if variant['sci_pio'] == '1' and present_dma:
            raise ValueError('PIO variant retains a channel1 DMAC address literal')
        if variant['sci_pio'] == '0' and not present_dma:
            raise ValueError('DMA-eligible variant does not retain its block DMAC address literals')
        layout['diagnostic_literal_checks'] = {
            'live_sound_sampler_known_literals_absent': True,
            'channel1_dmac_literals_present': present_dma,
            'scope': 'Supplemental build guards; not an exhaustive computed-address proof',
        }
        add(variant['file'], runtime)
        prefix = 'evidence/retail-read-' + variant['name'] + '/'
        add(prefix + 'build-config', config_bytes)
        evidence = {}
        for path in sorted(build.rglob('*')):
            if not path.is_file() or path.suffix not in ('.elf', '.map', '.su'):
                continue
            relative = path.relative_to(build).as_posix()
            data = remember(path)
            evidence[relative] = sha(data)
            add(prefix + relative, data)
        if not all(name in evidence for name in ('entry.elf', 'entry.map', 'stage.elf',
                                                 'stage.map', 'resident-sci.elf', 'resident-sci.map')):
            raise ValueError('Missing actual linked ELF/map evidence for ' + variant['name'])
        if not any(name.endswith('.su') for name in evidence):
            raise ValueError('Missing compiler stack reports for ' + variant['name'])
        variants.append({
            'profile': 14, 'variant': variant['name'], 'test_order': variant['order'],
            'file': variant['file'], 'mode': variant['mode'],
            'manual_card_target': '/KUI/apps/games/retail-boot.kui',
            'required_runtime': 'retained working 1.8.5 runtime',
            'build': envelope['build'], 'runtime_sha256': sha(runtime),
            'payload_crc32': f'{zlib.crc32(payload):08x}', 'envelope': envelope,
            'linked_layout': layout, 'build_configuration': config,
            'build_configuration_sha256': sha(config_bytes), 'evidence_sha256': evidence,
            'hardware_tested': False, 'numerical_pass_target': None,
        })
    configurations = [{key: value for key, value in variant['build_configuration'].items()
                       if key != 'SCI_PIO'} for variant in variants]
    if any(config != configurations[0] for config in configurations[1:]):
        raise ValueError('Comparison build configurations must match except for SCI_PIO')
    if files[VARIANTS[0]['file']] == files[VARIANTS[1]['file']]:
        raise ValueError('PIO and DMA-eligible test variants must contain distinct native bytes')

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
        'scope': 'Paired SCI block-read diagnostics: PIO first, DMA eligible second; no integrated CDDA playback',
        'profile_number': 14, 'test_order': ['pio', 'dma'],
        'hardware_tested': False, 'numerical_pass_target': None,
        'game_files_or_descriptor_installed': False,
        'card_path_configuration_installed': False,
        'retail_observation_installed_on_extraction': False,
        'working_runtime_replaced_in_bundle': False,
        'prior_artifacts_overwritten': False,
        'previous_diagnostics': [
            {'build': ORIGINAL_OBSERVER_BUILD, 'runtime_sha256': ORIGINAL_OBSERVER_SHA256,
             'source_package': 'K-UI-CDDA-Integration-Tests.zip', 'included': False},
            {'build': PREVIOUS_DIAGNOSTIC_BUILD, 'runtime_sha256': PREVIOUS_DIAGNOSTIC_SHA256,
             'source_package': 'K-UI-CDDA-Retail-Diagnostic.zip', 'included': False,
             'result': 'Image I/O failure with storage TIMEOUT before outer stop; outer stop SCI health check also returned TIMEOUT, not a second observed CMD12 timeout; guards intact',
             'evidence': HARDWARE_RECORD, 'evidence_source_sha256': document_hashes[HARDWARE_RECORD]},
        ],
        'required_image': {
            'file': 'TOY_COMMANDER.gdi', 'descriptor_bytes': TOY_DESCRIPTOR_BYTES,
            'descriptor_sha256': TOY_DESCRIPTOR_SHA256, 'complete_backing_tracks': 15,
            'audio_tracks': 12, 'data_tracks': 3,
            'location': 'same existing complete original game folder used for passing test13',
            'copied_game_content_included': False,
            'repeat_tests00_through13_required_for_unchanged_image_card': False,
        },
        'comparison_contract': {
            'manual_installation_only': True,
            'each_variant_requires_separate_cold_boot': True,
            'test_dma_after_pio_regardless_of_pio_outcome': True,
            'reader': 'native standard SCI', 'complete_manifest_slot_limit': 64,
            'complete_audio_extent_admission_required': True,
            'pio_path': 'Existing bounded SCI block payload loop',
            'pio_gate_scope': 'SCI block payloads in both high launch stage and low native game resident',
            'dma_path': 'Existing idle-channel/aligned-buffer guards and PIO fallback',
            'protocol_and_crc_retained': True, 'hidden_retry_added': False,
            'first_failing_register_wait_captured': False,
            'first_wait_trace_omitted_reason': 'Additional native telemetry exceeds unchanged resident or conservative stack limits',
            'integrated_cdda_playback': False, 'live_aica_g2_sampling': False,
            'new_sound_or_timer_ownership': False,
            'return_control': 'A+B+X+Y+Start', 'observation_pages': 4,
            'terminal_fault_trace_pages': 1, 'each_page_frames': 1200,
            'report_pages_repeat': False,
            'resident_fault_final_page_held_until_power_off': True,
            'normal_report_returns_to_firmware': True,
            'resource_samples_establish_ownership': False,
            'numerical_pass_target': None,
        },
        'document_source_sha256': document_hashes, 'profiles': variants,
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    if ({name for name in files if name.endswith('.kui')} != {variant['file'] for variant in VARIANTS} or
            any(name.startswith(('KUI/', 'runtimes/', 'source-fatfs/')) for name in files)):
        raise ValueError('Comparison must contain only two manually installed observation14 variants')
    if (git('status', '--porcelain') or git('rev-parse', 'HEAD') != checkpoint or
            git('rev-parse', 'HEAD^{tree}') != tree):
        raise ValueError('Source changed while collecting the comparison package')
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
            raise ValueError('Comparison filename must be ' + OUTPUT_NAME)
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
                          'profile': 14, 'test_order': ['pio', 'dma']}, indent=2))
    except (OSError, ValueError, UnicodeError, struct.error, subprocess.SubprocessError,
            zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
