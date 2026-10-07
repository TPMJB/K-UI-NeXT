#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bundle compact SCI first-failure and receive-paced DMA retail diagnostics."""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import zipfile
import zlib

from check_loader_layout import inspect_elf
from check_retail_loader_layout import code_symbol
from package_cdda_calibration import (
    ROOT, archive_name, build_config, git, object_id, read_file, sha, write_archive,
)
from package_cdda_mixed import document_copy
from package_cdda_observe_fix import SAMPLER_MMIO_LITERALS, SOUND_LEGENDS
from package_cdda_preflight import (
    PREFLIGHT_LABEL, TOY_DESCRIPTOR_BYTES, TOY_DESCRIPTOR_SHA256, observation_layout,
)
from package_cdda_read_compare import DMAC_CHANNEL1_LITERALS
from retail_package import (
    LOW_RESIDENT_ADDRESS, LOW_RESIDENT_LIMIT, STAGE_ADDRESS, STAGE_MEMORY_END,
    inspect_retail,
)
from runtime_package import flatten_elf

OUTPUT_NAME = 'K-UI-CDDA-SCI-Fault-Tests.zip'
README_SOURCE = 'docs/cdda-sci-fault-test.md'
HARDWARE_RECORD = 'docs/evidence/cdda-retail-read-comparison-hardware-2026-10-07.md'
COMPARISON_BUILD = '482efe87c56d'
VARIANTS = (
    {'name': 'fault', 'order': 1, 'build_directory': 'build/retail-sci-fault',
     'file': 'observation/14-sci-fault.kui', 'paced': '0',
     'label': b'PROFILE14 SCI FAULT /',
     'mode': 'Existing DMA-eligible feed with compact first-failure capture'},
    {'name': 'paced', 'order': 2, 'build_directory': 'build/retail-sci-paced',
     'file': 'observation/14-sci-paced.kui', 'paced': '1',
     'label': b'PROFILE14 SCI PACED /',
     'mode': 'Receive-progress-paced DMA feed with the same compact capture'},
)
# Filled against the retained implementation, rather than only the outer
# envelope or a title string. CPU/TMU/audio observations must not come back.
TRACE_LEGENDS = (
    b'FN ARG FLAG G BAD', b'CMD LBA N BPS', b'IO LBA N DONE STEP',
    b'IMG PRE STOP SD', b'DST BLK ERR CARD',
    b'PHASE REASON EXPECT POLLS', b'SSR SCR SMR BRR',
    b'CHCR1 TCR1 DMAOR CHCR2 TCR2', b'DMA START OK FALLBACK',
    b'CALL PLAY20 PLAY21 BAD', b'PHASE 0: NO SCI FAULT CAPTURED',
)
OLD_OBSERVATION_LEGENDS = (
    b'PROFILE14 OBSERVE /', b'PROFILE14 PIO /', b'PROFILE14 DMA /',
    b'CPU TMU', b'CPU TMU AICA SKIP', b'KEY0 KEY1', b'DMA F L OR CHG',
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
        raise ValueError('Commit the SCI fault checklist and comparison hardware record')
    if b'SCI_FAULT_CHECKLIST_DRAFT' in read_file(ROOT / README_SOURCE):
        raise ValueError('Freeze the SCI fault checklist against both native implementations')
    files, inputs = {}, {}

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
        required_config = {
            'BUILD': commit[:12], 'PROFILE': '14', 'LOW': '1', 'SLOTS': '64',
            'SCI': '1', 'AUDIO': '0', 'SCI_COMPARE': '0', 'SCI_PIO': '0',
            'SCI_OBSERVE': '0', 'STAGE_SCI_OBSERVE': '0',
            'SCI_FAULT': '1', 'SCI_PACED': variant['paced'],
            'OPT': '-Os -fno-tree-scev-cprop',
        }
        if config != required_config:
            raise ValueError('Wrong complete build configuration for ' + variant['name'])
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
        if (any(legend + b'\0' not in payload for legend in TRACE_LEGENDS) or
                any(legend + b'\0' in payload for legend in
                    (*OLD_OBSERVATION_LEGENDS, *SOUND_LEGENDS))):
            raise ValueError('Compact SCI trace must retain read details and omit old samplers')
        if any(struct.pack('<I', address) in payload for address in SAMPLER_MMIO_LITERALS):
            raise ValueError('Compact SCI trace retains a live audio sampler device literal')
        for name in ('stage.elf', 'resident-sci.elf', 'stage.bin', 'resident-sci.bin'):
            remember(build / name)
        layout = observation_layout(build, entry_elf)
        resident = inspect_elf(remember(build / 'resident-sci.elf'),
                               LOW_RESIDENT_ADDRESS, LOW_RESIDENT_LIMIT)
        if (variant['label'] + b'\0' not in resident['payload'] or
                any(legend + b'\0' not in resident['payload'] for legend in TRACE_LEGENDS)):
            raise ValueError('Compact SCI legends must be retained in the actual low resident')
        for symbol in ('_diagnostic_fault', '_observe_report', '_observe_fault_report'):
            code_symbol(resident, symbol, LOW_RESIDENT_ADDRESS)
        diagnostic_address = resident['symbols'].get('_diagnostic', 0)
        if not (resident['symbols']['__retail_resident_bss_begin'] <= diagnostic_address and
                diagnostic_address + 16 * 4 <= resident['symbols']['__retail_resident_bss_end']):
            raise ValueError('Actual resident lacks its 16-word first-failure state in BSS')
        if any(symbol in ('_observe', '_observe_words') or
               symbol.startswith(('_observe_audio', '_observe_pair_page',
                                  '_kui_retail_observe_sample'))
               for symbol in resident['symbols']):
            raise ValueError('Compact SCI resident retains the previous observation sampler')
        stage = inspect_elf(remember(build / 'stage.elf'), STAGE_ADDRESS, STAGE_MEMORY_END)
        code_symbol(stage, '_kui_retail_observe_native_ip', STAGE_ADDRESS)
        present_dma = [f'0x{address:08x}' for address in DMAC_CHANNEL1_LITERALS
                       if struct.pack('<I', address) in payload]
        if len(present_dma) != len(DMAC_CHANNEL1_LITERALS):
            raise ValueError('DMA-eligible SCI variant lacks its expected channel1 literals')
        layout['diagnostic_literal_checks'] = {
            'live_sound_sampler_known_literals_absent': True,
            'old_cpu_tmu_audio_sampler_symbols_and_legends_absent': True,
            'channel1_dmac_literals_present': present_dma,
            'scope': 'Supplemental linked-build guards, not an exhaustive computed-address proof',
        }
        layout['first_failure_capture'] = {
            'capture_code': f"0x{resident['symbols']['_diagnostic_fault']:08x}",
            'resident_state_address': f'0x{diagnostic_address:08x}',
            'resident_state_words': 16,
            'compact_report_code': f"0x{resident['symbols']['_observe_report']:08x}",
            'terminal_read_trace_code': f"0x{resident['symbols']['_observe_fault_report']:08x}",
            'validity': 'Nonzero phase; phase zero invalidates the fault fields',
            'register_snapshot_atomic': False,
            'counter_scope': 'Lifetime totals of the resident bus instance, excluding the high stage',
        }
        add(variant['file'], runtime)
        prefix = 'evidence/retail-sci-' + variant['name'] + '/'
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
    configurations = [{key: value for key, value in item['build_configuration'].items()
                       if key != 'SCI_PACED'} for item in variants]
    if any(config != configurations[0] for config in configurations[1:]):
        raise ValueError('SCI fault configurations must match except for SCI_PACED')
    if files[VARIANTS[0]['file']] == files[VARIANTS[1]['file']]:
        raise ValueError('Baseline and paced variants must contain distinct native bytes')

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
        'scope': 'SCI DMA baseline first-failure capture and separate receive-paced comparison; no integrated CDDA playback',
        'profile_number': 14, 'test_order': ['fault', 'paced'],
        'hardware_tested': False, 'numerical_pass_target': None,
        'game_files_or_descriptor_installed': False,
        'card_path_configuration_installed': False,
        'retail_observation_installed_on_extraction': False,
        'working_runtime_replaced_in_bundle': False,
        'prior_artifacts_overwritten': False,
        'previous_comparison': {
            'build': COMPARISON_BUILD, 'source_package': 'K-UI-CDDA-Read-Comparison.zip',
            'included': False, 'evidence': HARDWARE_RECORD,
            'evidence_source_sha256': document_hashes[HARDWARE_RECORD],
            'result': 'PIO got past prior stops with reported lag and one accepted track14 PLAY; DMA-eligible variant stopped with image I/O and SCI TIMEOUT before an accepted PLAY',
        },
        'required_image': {
            'file': 'TOY_COMMANDER.gdi', 'descriptor_bytes': TOY_DESCRIPTOR_BYTES,
            'descriptor_sha256': TOY_DESCRIPTOR_SHA256, 'complete_backing_tracks': 15,
            'audio_tracks': 12, 'data_tracks': 3,
            'location': 'same existing complete original game folder used for passing test13',
            'copied_game_content_included': False,
            'repeat_tests00_through13_required_for_unchanged_image_card': False,
        },
        'diagnostic_contract': {
            'manual_installation_only': True,
            'each_variant_requires_separate_cold_boot': True,
            'test_paced_after_fault_regardless_of_baseline_outcome': True,
            'reader': 'native standard SCI', 'complete_manifest_slot_limit': 64,
            'complete_audio_extent_admission_required': True,
            'baseline': 'Existing DMA-eligible feed plus compact first-failure capture',
            'paced': 'Wait for receive DMAC progress before clocking the next byte',
            'ordinary_reader_default_changed': False,
            'protocol_and_crc_retained': True, 'hidden_retry_added': False,
            'first_failing_register_wait_captured': True,
            'cpu_tmu_audio_observation_removed_for_native_fit': True,
            'integrated_cdda_playback': False, 'live_aica_g2_sampling': False,
            'new_sound_or_timer_ownership': False,
            'return_control': 'A+B+X+Y+Start',
            'normal_return_report_pages': 1,
            'terminal_fault_trace_pages': 1,
            'resident_fault_total_pages': 2,
            'each_page_frames': 1200,
            'report_pages_repeat': False,
            'resident_fault_final_page_held_until_power_off': True,
            'normal_report_returns_to_firmware': True,
            'software_poll_counts_are_calibrated_elapsed_time': False,
            'aggregate_dma_counters_identify_failing_block_by_themselves': False,
            'numerical_pass_target': None,
        },
        'document_source_sha256': document_hashes, 'profiles': variants,
    }
    add('build.json', (json.dumps(metadata, indent=2) + '\n').encode())
    if ({name for name in files if name.endswith('.kui')} != {variant['file'] for variant in VARIANTS} or
            any(name.startswith(('KUI/', 'runtimes/', 'source-fatfs/')) for name in files)):
        raise ValueError('Bundle must contain only the two manually installed SCI fault variants')
    if (git('status', '--porcelain') or git('rev-parse', 'HEAD') != checkpoint or
            git('rev-parse', 'HEAD^{tree}') != tree):
        raise ValueError('Source changed while collecting the SCI fault package')
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
        if args.output.is_symlink():
            raise ValueError('Output must not be a symlink')
        output = args.output.resolve()
        if output.name != OUTPUT_NAME:
            raise ValueError('SCI fault filename must be ' + OUTPUT_NAME)
        if output.exists():
            raise ValueError('Use a new output path; preserve an existing SCI fault ZIP')
        if output.is_relative_to(ROOT):
            result = subprocess.run(['git', 'ls-files', '--error-unmatch', '--',
                                     output.relative_to(ROOT).as_posix()], cwd=ROOT,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if result.returncode == 0:
                raise ValueError('Output must not overwrite a tracked repository file')
        write_archive(output, collect(args.source_commit, args.published_tree))
        print(json.dumps({'file': str(output), 'bytes': output.stat().st_size,
                          'sha256': sha(output.read_bytes()), 'build': args.source_commit[:12],
                          'profile': 14, 'test_order': ['fault', 'paced']}, indent=2))
    except (OSError, ValueError, UnicodeError, struct.error, subprocess.SubprocessError,
            zipfile.BadZipFile) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
