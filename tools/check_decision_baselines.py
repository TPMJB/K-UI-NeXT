#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check uninstrumented Toy/CE controls against pristine accepted source.

Only exact NUL-terminated display build labels may differ. All flattened
instructions, constants, embedded images and load-memory shape must match.
The optional compact check establishes memory/marker/embedding facts only;
it does not replace the separately pinned functional/stack audits.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

from check_vmu_native_worker_gateway import read_elf
from check_native_dual_readers import compact_component_markers
from native_runtime_layout import BACKGROUND_COMPACT, layout_for
from runtime_package import ELF_HEADER, PROGRAM_HEADER, flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]
PRISTINE = '0045dd96937ee7485ecd4fe0432b28babea8646a'


def require(value, message):
    if not value:
        raise ValueError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def normalized(data, stamp):
    require(re.fullmatch(r'[0-9a-f]{12}', stamp) is not None, 'Expected a twelve-hex build stamp')
    identity = stamp.encode()
    label = b'BUILD ' + identity + b'\0'
    count = data.count(label)
    require(data.count(identity) == count, 'Build stamp occurs outside its exact display label')
    return data.replace(label, b'BUILD 000000000000\0'), count


def compare_bytes(label, pristine, candidate, pristine_stamp, candidate_stamp):
    left, a = normalized(pristine, pristine_stamp)
    right, b = normalized(candidate, candidate_stamp)
    require(a == b, label + ': display-label count changed')
    require(left == right, label + ': runtime bytes differ beyond display build labels')
    return dict(bytes=len(right), normalized_sha256=sha(right),
                pristine_sha256=sha(pristine), candidate_sha256=sha(candidate), build_labels=a,
                complete_bytes_identical=pristine == candidate)


def config(path):
    return dict(line.split('=', 1) for line in path.read_text().splitlines() if '=' in line)


def entry(directory):
    payload, memory = flatten_elf((directory / 'entry.elf').read_bytes())
    return payload, memory


def entry_shape(directory):
    data = (directory / 'entry.elf').read_bytes()
    flatten_elf(data)  # Validate every load bound before describing it.
    header = ELF_HEADER.unpack_from(data)
    segments = []
    for i in range(header[10]):
        kind, _, virtual, physical, file_bytes, memory_bytes, flags, align = \
            PROGRAM_HEADER.unpack_from(data, header[5] + i * header[9])
        if kind == 1:
            segments.append((virtual, physical, file_bytes, memory_bytes, flags, align))
    return dict(entry=header[4], load_segments=segments)


def entry_stamp(directory):
    payload, _ = entry(directory)
    stamps = set(re.findall(rb'BUILD ([0-9a-f]{12})\0', payload))
    require(len(stamps) == 1, 'Entry must carry one unambiguous display build identity: ' + str(directory))
    return stamps.pop().decode()


def clean_source(root):
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=root, text=True).strip()
    require(not subprocess.check_output(['git', 'status', '--porcelain'], cwd=root),
            'Source must be clean before checking compiled controls: ' + str(root))
    return commit


def compile_receipt(directory, commit):
    record = json.loads((directory / 'entry.compile.json').read_text())
    require(record == dict(commit=commit, source_dirty=False,
                           elf_sha256=sha((directory / 'entry.elf').read_bytes())),
            'Entry compile receipt does not bind the exact clean source/ELF: ' + str(directory))
    return record


def controls(pristine_root, candidate_root):
    commit = clean_source(pristine_root)
    require(commit == PRISTINE, 'Pristine controls must be built from exact accepted main ' + PRISTINE)
    candidate_commit = clean_source(candidate_root)
    cases = [('toy-raw', 'build/pristine-toy-raw', 'build/decision-toy-0-raw',
              ('resident-sci', 'worker', 'stage')),
             ('toy-cooked', 'build/pristine-toy-cooked', 'build/decision-toy-0-cooked',
              ('resident-sci', 'worker', 'stage')),
             ('armada', 'build/armada-ce-stream', 'build/armada-ce-stream',
              ('resident-scif', 'resident-sci', 'resident-ide', 'resident-scia', 'bind-probe', 'stage'))]
    result = {}
    for name, old_path, new_path, images in cases:
        old, new = pristine_root / old_path, candidate_root / new_path
        compile_receipt(old, commit)
        compile_receipt(new, candidate_commit)
        a, b = entry_stamp(old), entry_stamp(new)
        if name.startswith('toy'):
            for directory in (old, new):
                flags = config(directory / 'build-config')
                require(flags.get('NATIVE_COMPAT') == '1' and flags.get('DATA_PIPELINE_PROFILE') == '2' and
                        flags.get('PRIVATE_P2') == '1' and flags.get('NATIVE_CACHE') == '0',
                        name + ': control does not use the retained compatible AP/P2 reader')
                require(flags.get('WORD_REVERSE_PROFILE', '0') == '0' and
                        flags.get('FMV_TEST', '0') == '0' and
                        flags.get('TOY_DEMAND_TEST_PROFILE', '0') == '0' and
                        flags.get('TOY_DECISION_PROFILE', '0') == '0',
                        name + ': control contains a diagnostic or changed payload policy')
                require(flags.get('COOKED_DATA_PROFILE', '0') == str(int(name == 'toy-cooked')),
                        name + ': raw/cooked control configuration differs')
        compared = {}
        for image in images:
            compared[image] = compare_bytes(name + '/' + image, (old / (image + '.bin')).read_bytes(),
                (new / (image + '.bin')).read_bytes(), a, b)
        old_payload, old_memory = entry(old)
        new_payload, new_memory = entry(new)
        require(old_memory == new_memory and entry_shape(old) == entry_shape(new),
                name + ': entry load-memory shape changed')
        compared['entry'] = compare_bytes(name + '/entry', old_payload, new_payload, a, b)
        result[name] = dict(pristine_directory=str(old), candidate_directory=str(new),
            pristine_build=a, candidate_build=b, memory_bytes=new_memory, load_shape=entry_shape(new),
            same_build_stamp=a == b, images=compared)
    return dict(pristine_source=commit, candidate_source=candidate_commit, source_receipts_verified=True,
                byte_equal_except_display_build_labels=True,
                instruction_and_data_images_checked=True, entry_load_shapes_equal=True, controls=result)


def compact(directory):
    names = ('stage', 'native-worker', 'resident-sci', 'resident-legacy', 'native-ra-aux')
    images = [read_elf(directory / (name + '.elf')) for name in names]
    stage, worker, resident, legacy, auxiliary = images
    require(all(layout_for(image[4]) is BACKGROUND_COMPACT for image in images),
            'Every compact image must select the exact 72 KiB / 284 KiB geometry')
    markers = compact_component_markers(*images)
    require(all('_kui_retail_native_session_control' not in image[4] and
                '__native_session_descriptor' not in image[4] for image in images),
            'Compact restoration unexpectedly contains session-control services')
    one = (1, 0, 0, 0xfff1)
    for image, suffix in ((stage, 'stage'), (worker, 'worker')):
        symbols = image[4]
        require(symbols.get('__kui_native_ra_tick_off_isolation_' + suffix) == one,
                'Compact module lacks its explicit prefetch-Off marker')
        for marker in ('__kui_native_split_controller_prefetch_', '__kui_native_split_controller_stream_guard_',
                       '__kui_native_perf_capture_', '__kui_native_perf_full_submit_'):
            require(marker + suffix not in symbols, 'Compact module includes optional work: ' + marker)
    symbols = worker[4]
    require(not any(name.startswith(('_native_perf_', '_perf_capture')) for name in symbols),
            'Compact worker contains performance collection')
    bottom = symbols['__vmu_native_worker_stack_bottom'][0]
    top = symbols['__vmu_native_worker_stack_top'][0]
    bss_end = symbols['__vmu_native_worker_bss_end'][0]
    require(top == 0xac112000 and bottom == top - 0xc00 and bss_end <= bottom,
            'Compact state/stack leaves its exact 72 KiB reservation')
    low = {}
    for name, image in (('new', resident), ('legacy', legacy)):
        s = image[4]
        require(s['__retail_hook_stack_bottom'][0] == 0x8c007ba0 and
                s['__retail_hook_stack'][0] == 0x8c007d00 and
                s['__retail_resident_bss_end'][0] <= 0x8c007ba0,
                name + ': low reader leaves its guarded bounds')
        low[name] = dict(bss_end=hex(s['__retail_resident_bss_end'][0]),
                         bytes_before_stack=0x8c007ba0-s['__retail_resident_bss_end'][0], guarded_stack_bytes=352)
    aux = auxiliary[4]
    require(aux['__native_ra_image_end'][0] <= 0x8c424000 and
            aux['__native_ra_stack_bottom'][0] == 0xac424000 and
            aux['__native_ra_stack_top'][0] == 0xac425000,
            'Read-ahead auxiliary leaves its separately owned code/stack bounds')
    stage_data = (directory / 'stage.bin').read_bytes()
    for first, last, filename in (('__native_worker_blob_begin', '__native_worker_blob_end', 'native-worker.bin'),
                                  ('_native_ra_aux_blob_start', '_native_ra_aux_blob_end', 'native-ra-aux.bin')):
        begin, end = stage[4][first][0]-0x8ce00000, stage[4][last][0]-0x8ce00000
        require(stage_data[begin:end] == (directory / filename).read_bytes(),
                'Stage does not embed its actual checked ' + filename)
    module = (directory / 'retail-native-background-compact.kui').read_bytes()
    info = verify(module)
    payload, memory = entry(directory)
    require(module[64:] == payload and info['memory_bytes'] == memory,
            'Compact runtime package differs from its exact entry load image')
    return dict(scope='memory, immutable markers, embedding and collector exclusion; not a replacement functional audit',
        directory=str(directory), worker_bytes=0x12000, lease_bytes=0x47000, cache_bytes=0x30000,
        worker_stack_bytes=top-bottom, worker_bss_gap_bytes=bottom-bss_end,
        low_readers=low, component_markers=markers, embedded_worker_and_auxiliary_verified=True,
        prefetch_enabled=False, performance_capture=False, session_controls=False,
        module_sha256=sha(module), runtime=info, console_tested=False)


def compact_parity(pristine_root, candidate, candidate_commit):
    old = pristine_root / 'build/pristine-background72'
    require(clean_source(pristine_root) == PRISTINE, 'Compact reference source is not exact pristine main')
    compile_receipt(old, PRISTINE)
    compile_receipt(candidate, candidate_commit)
    a, b = entry_stamp(old), entry_stamp(candidate)
    require(a == b, 'Compact references must use the same build stamp so relocation hashes remain exact')
    compared = {}
    for name in ('native-worker', 'native-ra-aux', 'resident-sci', 'resident-legacy', 'stage'):
        compared[name] = compare_bytes('compact/' + name, (old / (name + '.bin')).read_bytes(),
            (candidate / (name + '.bin')).read_bytes(), a, b)
    old_payload, old_memory = entry(old)
    new_payload, new_memory = entry(candidate)
    require(old_memory == new_memory and entry_shape(old) == entry_shape(candidate),
            'Compact entry load-memory shape changed')
    compared['entry'] = compare_bytes('compact/entry', old_payload, new_payload, a, b)
    return dict(pristine_source=PRISTINE, candidate_source=candidate_commit, source_receipts_verified=True,
        pristine_directory=str(old), candidate_directory=str(candidate),
        complete_runtime_byte_parity=True, entry_load_shapes_equal=True, images=compared,
        policy='Same explicit compact prefetch-Off wrapper; no claim of equivalence to Auto prefetch',
        meaning='Decision/observer source changes are absent from the complete installed native runtime images')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--pristine-root', type=Path)
    parser.add_argument('--candidate-root', type=Path, default=ROOT)
    parser.add_argument('--compact', type=Path)
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    if not args.pristine_root and not args.compact:
        parser.error('Provide --pristine-root for controls and/or --compact for memory verification')
    result = dict(schema='kui.decision.baseline-check.v1', console_tested=False)
    try:
        candidate_commit = clean_source(args.candidate_root.resolve())
        if args.pristine_root:
            result['accepted_controls'] = controls(args.pristine_root.resolve(), args.candidate_root.resolve())
        if args.compact:
            path = args.compact if args.compact.is_absolute() else args.candidate_root / args.compact
            compile_receipt(path.resolve(), candidate_commit)
            result['compact_memory'] = compact(path.resolve())
            if args.pristine_root:
                result['compact_pristine_parity'] = compact_parity(args.pristine_root.resolve(), path.resolve(), candidate_commit)
    except (OSError, ValueError, KeyError, AssertionError, subprocess.CalledProcessError) as error:
        parser.exit(2, 'Decision baseline check refused: ' + str(error) + '\n')
    data = json.dumps(result, indent=2, sort_keys=True) + '\n'
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(data)
    print(data, end='')


if __name__ == '__main__':
    main()
