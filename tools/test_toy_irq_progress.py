#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Production SCI priority/one-step tests and actual emitted rehook instructions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import struct
import subprocess
import test_toy_data_budget as baseline

ROOT = Path(__file__).resolve().parents[1]
FIXTURE = 'tests/test_toy_irq_progress.c'
WIRE_FIXTURE = 'tests/test_toy_startup_sci_wire.c'
SCI_HEADER = 'src/loader/toy_pilot_sci.h'
IRQ_HEADER = 'include/kui/toy_loader_irq_progress.h'
TUNING_HEADER = 'include/kui/toy_loader_data_tuning.h'
ASM = 'src/loader/toy_pilot_sci_irq.S'
DEFAULT_CROSS = ROOT.parent / 'compiler-recovery/cdda-toolchain/sh-elf/bin/sh-elf-'
DESCRIPTORS = {0: (9, 1, 2048), 1: (13, 7, 2048), 2: (14, 7, 1)}
C_MUTATIONS = [
    ('C hook level reverted to1', 1, IRQ_HEADER,
     '#define KUI_TOY_STARTUP_SCI_LEVEL 0x0070u', '#define KUI_TOY_STARTUP_SCI_LEVEL 0x0010u', 1),
    ('AN service step cap reverted to2048', 2, SCI_HEADER,
     '#define KUI_TOY_SCI_SERVICE_STEPS 1u', '#define KUI_TOY_SCI_SERVICE_STEPS 2048u', 1),
    ('AM service step cap reduced to1', 1, TUNING_HEADER,
     '#define KUI_TOY_TUNING_STEPS 2048u', '#define KUI_TOY_TUNING_STEPS 1u', 1),
    ('hook loses unrelated IPRB fields', 1, baseline.ENGINE[0],
     'wr16(IPRB,(uint16_t)((level&~SCI_FIELD)|SCI_LEVEL));', 'wr16(IPRB,(uint16_t)SCI_LEVEL);', 1),
    ('unhook fails to restore guest SCI priority', 1, baseline.ENGINE[0],
     'wr16(IPRB,(uint16_t)((level&~SCI_FIELD)|kui_toy_pilot_sci_release.sci));',
     'wr16(IPRB,(uint16_t)((level&~SCI_FIELD)|SCI_LEVEL));', 1),
    ('IRQ block credit counted as foreground', 1, baseline.ENGINE[0],
     'if(e.in_irq)++e.stats.irq_blocks;else ++e.stats.call_blocks;',
     '++e.stats.call_blocks;', 2),
    ('IRQ request ownership revalidation removed', 1, baseline.ENGINE[0],
     'if(!live()) {kui_toy_pilot_sci_cancel(e.service);return KUI_TOY_SCI_OK;}',
     'if(0) {kui_toy_pilot_sci_cancel(e.service);return KUI_TOY_SCI_OK;}', 1),
    ('last-block terminal close removed', 1, baseline.ENGINE[0],
     'if(e.cursor.done==e.cursor.count) {', 'if(0) {', 1),
]
ASM_MUTATIONS = [
    ('rehook alone reverted to priority1', 'xor #KUI_TOY_STARTUP_SCI_NIBBLE,r0', 'xor #0x10,r0'),
    ('rehook SCI field mask narrowed', 'xor #KUI_TOY_STARTUP_SCI_NIBBLE,r0\n    and #0xf0,r0', 'xor #KUI_TOY_STARTUP_SCI_NIBBLE,r0\n    and #0xe0,r0'),
    ('rehook IPRB address changed', '.Lrehook_iprb: .long 0xffd00008', '.Lrehook_iprb: .long 0xffd00004'),
    ('rehook IPRB target register changed', 'mov.l .Lrehook_iprb,r1', 'mov.l .Lrehook_iprb,r2'),
]
UNSAFE = [
    {'KUI_TOY_PILOT_STARTUP_IRQ_PROFILE': -1}, {'KUI_TOY_PILOT_STARTUP_IRQ_PROFILE': 3},
    *[{'KUI_TOY_PILOT_DATA_TUNING_PROFILE': x} for x in [0, 2, 3, 4]],
    *[{'KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE': x} for x in [0, 1, 3, 4]],
    *[{'KUI_TOY_PILOT_DATA_BUDGET_PROFILE': x} for x in [0, 1, 2, 3]],
    *[{'KUI_TOY_PILOT_DATA_SCHEDULE_MODE': x} for x in [0, 1, 3]],
    {'KUI_TOY_PILOT_LOADER_TRACE': 0}, {'KUI_TOY_PILOT_DATA_PROBE': 0},
    {'KUI_TOY_PILOT_PRIVATE_P2': 0}, {'KUI_TOY_PILOT_NATIVE_CACHE': 1},
    {'KUI_TOY_PILOT_SHARED_SCI': 1}, {'KUI_TOY_PILOT_ASYNC_CDDA': 1},
    {'KUI_TOY_PILOT_SYNTHETIC_SOURCE': 1}, {'KUI_TOY_PILOT_GD_FIXED_STEP': 1},
    {'KUI_SCI_DMA_REUSE_TDRE': 1}, {'KUI_TOY_PILOT_DATA_PAYLOAD_MODE': 1},
]


def definitions(profile):
    return dict(baseline.DEFINES, KUI_TOY_PILOT_DATA_BUDGET_PROFILE=4,
                KUI_TOY_PILOT_AD_FOLLOWUP_PROFILE=2, KUI_TOY_PILOT_DATA_TUNING_PROFILE=1,
                KUI_TOY_PILOT_STARTUP_IRQ_PROFILE=profile)


def validate_rehook(binary, rehook, expected_priority):
    """Interpret the actual emitted SH field update, including word sign extension."""
    def word(at):
        return struct.unpack_from('<H', binary, at)[0]
    expected = [0x6011, 0xca00 | expected_priority << 4, 0xc9f0,
                0x6211, 0x220a, 0x2121, 0x6211]
    actual = [word(rehook + 0x4a + 2*n) for n in range(7)]
    assert actual == expected, 'actual emitted rehook field instructions differ: ' + repr(actual)
    load = word(rehook + 0x48)
    assert load & 0xff00 == 0xd100, 'actual IPRB literal load must target r1'
    target = ((rehook + 0x48 + 4) & ~3) + (load & 255)*4
    iprb = struct.unpack_from('<I', binary, target)[0]
    assert iprb == 0xffd00008, 'actual emitted IPRB target differs: ' + hex(iprb)
    def signed_word(value):
        return value if value < 32768 else value | 0xffff0000
    for original in range(65536):
        memory = original
        r0 = r2 = 0
        for opcode in actual:
            if opcode == 0x6011:
                r0 = signed_word(memory)
            elif opcode >> 8 == 0xca:
                r0 ^= opcode & 255
            elif opcode >> 8 == 0xc9:
                r0 &= opcode & 255
            elif opcode == 0x6211:
                r2 = signed_word(memory)
            elif opcode == 0x220a:
                r2 ^= r0
            elif opcode == 0x2121:
                memory = r2 & 65535
            else:
                raise AssertionError('Unreviewed emitted field instruction')
        assert memory == (original & 0xff0f) | expected_priority << 4
        assert r2 == signed_word(memory)
    return {'rehook_offset': rehook, 'field_opcodes': [f'{x:04x}' for x in actual],
            'IPRB_address': f'{iprb:08x}', 'IPRB_values_interpreted': 65536,
            'priority': expected_priority, 'outside_SCI_bits_preserved': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    parser.add_argument('--cross-prefix', default=str(DEFAULT_CROSS))
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'build/irq-tests')
    args = parser.parse_args()
    directory = args.build_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    cc = shlex.split(args.cc)
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie']
    env = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')
    results, observations, mutations, assembly, guards = [], {}, [], [], []
    def execute(command, **kwargs):
        return subprocess.run(command, cwd=ROOT, env=env, capture_output=True, text=True,
                              timeout=60, **kwargs)
    def compile_case(name, profile, fixture, overlay=None):
        defines = definitions(profile)
        if fixture == WIRE_FIXTURE:
            defines['KUI_SCI_STREAM_TEST'] = 1
        includes = ([f'-I{overlay}'] if overlay else []) + ['-Iinclude', '-Isrc/loader']
        engine = str(overlay / 'toy_pilot_sci.c') if overlay else baseline.ENGINE[0]
        sources = [fixture, engine, *baseline.ENGINE[1:]]
        executable = directory / name
        command = [*cc, *flags, *includes, *[f'-D{k}={v}' for k,v in defines.items()],
                   *sources, '-o', str(executable)]
        built = execute(command)
        if built.returncode:
            raise RuntimeError(name + ' compilation failed:\n' + built.stdout + built.stderr)
        return executable, command, sources
    for profile in range(3):
        for fixture, suffix in [(FIXTURE, 'engine'), (WIRE_FIXTURE, 'wire')]:
            name = f'irq-{profile}-{suffix}'
            executable, command, sources = compile_case(name, profile, fixture)
            run = execute([str(executable)])
            if run.returncode:
                raise RuntimeError(name + ' failed:\n' + run.stdout + run.stderr)
            print(run.stdout.rstrip(), flush=True)
            results.append({'name': name, 'profile': profile, 'command': command,
                            'stdout': run.stdout, 'stderr': run.stderr, 'exit_code': run.returncode,
                            'source_sha256': {s: hashlib.sha256((ROOT/s).read_bytes()).hexdigest() for s in sources}})
            if fixture == FIXTURE:
                match = re.search(r'report=(\d+) priority=(\d+) steps=(\d+); between-GD IRQ blocks=(\d+) fast-foreground blocks=(\d+) stalled-polls=(\d+)', run.stdout)
                if not match:
                    raise RuntimeError('Missing IRQ fixture observations')
                values = tuple(map(int, match.groups()))
                assert values[:3] == DESCRIPTORS[profile] and values[3] == 32
                assert values[4] == (1 if profile == 2 else 24) and values[5] == DESCRIPTORS[profile][2]
                observations[profile] = dict(zip(['report', 'priority', 'steps', 'between_GD_IRQ_blocks', 'fast_foreground_blocks', 'stalled_polls'], values))
    source_paths = [baseline.ENGINE[0], SCI_HEADER, IRQ_HEADER, TUNING_HEADER]
    texts = {p: (ROOT/p).read_text() for p in source_paths}
    for n, (label, profile, source, needle, replacement, count) in enumerate(C_MUTATIONS):
        assert texts[source].count(needle) == count, 'Mutation anchor changed: ' + label
        overlay = directory / f'c-mutation-{n}'
        (overlay / 'kui').mkdir(parents=True, exist_ok=True)
        names = {baseline.ENGINE[0]: 'toy_pilot_sci.c', SCI_HEADER: 'toy_pilot_sci.h',
                 IRQ_HEADER: 'kui/toy_loader_irq_progress.h', TUNING_HEADER: 'kui/toy_loader_data_tuning.h'}
        for p in source_paths:
            (overlay/names[p]).write_text(texts[p].replace(needle,replacement) if p == source else texts[p])
        executable, command, _ = compile_case(f'irq-mutant-{n}', profile, FIXTURE, overlay)
        run = execute([str(executable)])
        if run.returncode == 0 or 'Assertion' not in run.stderr:
            raise RuntimeError('Behavioral mutation not rejected: ' + label + '\n' + run.stderr)
        mutations.append({'name': label, 'kind': 'production C behavior', 'command': command,
                          'exit_code': run.returncode, 'assertion': run.stderr.strip().splitlines()[-1]})
    def assembled_case(name, profile, source=ASM):
        obj = directory / (name + '.o')
        preprocessed = directory / (name + '.S')
        binary = directory / (name + '.bin')
        defines = definitions(profile)
        command = [args.cross_prefix + 'gcc', '-m4-single-only', '-ml', '-Iinclude',
                   *[f'-D{k}={v}' for k,v in defines.items()]]
        pp = execute([*command, '-E', '-P', '-x', 'assembler-with-cpp', str(source)])
        if pp.returncode:
            raise RuntimeError('Assembly preprocessing failed: ' + pp.stderr)
        preprocessed.write_text(pp.stdout)
        built = execute([*command, '-c', '-x', 'assembler-with-cpp', str(source), '-o', str(obj)])
        if built.returncode:
            raise RuntimeError('Production assembly failed: ' + built.stderr)
        dumped = execute([args.cross_prefix + 'objcopy', '--dump-section', '.text.toy_sci_irq=' + str(binary), str(obj)])
        symbols = execute([args.cross_prefix + 'nm', '--defined-only', str(obj)])
        if dumped.returncode or symbols.returncode:
            raise RuntimeError('Cannot inspect emitted assembly')
        match = re.search(r'^([0-9a-f]+)\s+[Tt]\s+_kui_toy_pilot_sci_rehook$', symbols.stdout, re.M)
        assert match, 'Actual rehook symbol missing'
        return binary.read_bytes(), int(match.group(1),16), command, preprocessed
    for profile in range(3):
        binary, offset, command, pp = assembled_case(f'irq-asm-{profile}', profile)
        verified = validate_rehook(binary, offset, DESCRIPTORS[profile][1])
        assembly.append(dict(verified, profile=profile, command=command, preprocessed_sha256=hashlib.sha256(pp.read_bytes()).hexdigest(), emitted_section_sha256=hashlib.sha256(binary).hexdigest()))
    original = (ROOT/ASM).read_text()
    for n, (label, needle, replacement) in enumerate(ASM_MUTATIONS):
        assert original.count(needle) == 1, 'Assembly mutation anchor changed: ' + label
        source = directory / f'asm-mutation-{n}.S'
        source.write_text(original.replace(needle,replacement))
        binary, offset, command, _ = assembled_case(f'irq-asm-mutant-{n}', 1, source)
        try:
            validate_rehook(binary, offset, 7)
        except AssertionError as failure:
            mutations.append({'name': label, 'kind': 'actual emitted assembly', 'command': command,
                              'validator_rejected': True, 'assertion': str(failure)})
        else:
            raise RuntimeError('Assembly mutation was not rejected: ' + label)
    for changes in UNSAFE:
        defines = definitions(1)
        defines.update(changes)
        command = [*cc, '-std=c11', '-Iinclude', *[f'-D{k}={v}' for k,v in defines.items()], '-x', 'c', '-fsyntax-only', '-']
        built = execute(command, input='#include "kui/toy_loader_irq_progress.h"\n')
        if built.returncode == 0 or '#error' not in built.stderr:
            raise RuntimeError('Unsafe IRQ configuration accepted: ' + repr(changes))
        guards.append({'changed_defines': changes, 'exit_code': built.returncode, 'command': command})
    pins = [FIXTURE, WIRE_FIXTURE, 'tools/test_toy_irq_progress.py', 'tests/test_toy_pilot_sci.c',
            'tests/test_toy_pilot_sci_async_integration.c', 'tests/sci_stream_model.h', ASM,
            *source_paths, 'src/loader/sci_stream.c']
    report = {'schema': 1, 'sanitizers': ['address','undefined'], 'results': results,
              'matrix_observations': observations, 'actual_emitted_rehook_checks': assembly,
              'mutation_results': mutations, 'unsafe_configurations': guards,
              'source_sha256': {p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in pins},
              'limits': ['Guest BL/IMASK delivery is a host eligibility model, not CPU emulation.',
                         'Rehook field instructions are actual assembled opcodes; the host does not execute the full SH interrupt entry.',
                         'Linked executable assurance remains the separate exact-template audit.',
                         'Hardware playback improvement remains unproven.']}
    (directory/'irq-progress-tests.json').write_text(json.dumps(report,indent=2)+'\n')
    print(f'Startup IRQ profiles 0..2 and 3 real-wire suites passed; 3 emitted rehooks verified; {len(mutations)} behavioral/assembly mutations and {len(guards)} unsafe configurations rejected')


if __name__ == '__main__':
    main()
