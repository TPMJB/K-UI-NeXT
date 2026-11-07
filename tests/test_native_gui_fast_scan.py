#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind GUI immutable scan defaults to real recipes and byte-reference outcomes.

The host fixtures exercise admission, corruption and unaligned fallbacks. An
optional linked GUI establishes the marker and actual function bodies; neither
check predicts SH-4 elapsed time or changes a live worker's cache policy.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
SOURCES = tuple('src/core/vmu_native_' + name + '.c' for name in ('program', 'heap', 'exit'))
CONTROL = 'src/core/vmu_native_resolution_cache.c'
SUPPORT = ('src/core/vmu_native_patch.c', 'src/core/data.c')
FLAGS = ('-std=c11', '-O2', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
         '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
         '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
         '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-Iinclude', '-I.')
ENV = dict(os.environ, ASAN_OPTIONS='detect_leaks=0', UBSAN_OPTIONS='halt_on_error=1')


def run(command):
    result = subprocess.run(command, cwd=ROOT, env=ENV, text=True,
                            capture_output=True, timeout=180)
    if result.returncode:
        raise AssertionError(f'Command failed ({result.returncode}): {shlex.join(map(str, command))}\n'
                             f'{result.stdout}{result.stderr}')
    return result.stdout


def effective_recipes():
    objects = ['build/dc/' + str(Path(source).with_suffix('.o')) for source in (*SOURCES, CONTROL)]
    output = run(['bash', '-c', 'source .deps/kos/environ.sh\nexec "$@"',
                  'kui-gui-scan-policy', 'make', '--no-print-directory', '-s', '-rR',
                  '-B', '-n', '-f', 'Makefile.dc', *objects])
    commands = {}
    for line in output.splitlines():
        args = shlex.split(line)
        if not args or args[0] != 'kos-cc' or '-c' not in args:
            continue
        source = args[args.index('-c') + 1]
        if source in (*SOURCES, CONTROL):
            assert source not in commands, 'Duplicate object compilation: ' + source
            commands[source] = args
    assert set(commands) == set((*SOURCES, CONTROL)), 'Missing actual GUI compile recipe'
    rows = []
    for source, command in commands.items():
        values = [arg.partition('=')[2] for arg in command
                  if arg.startswith('-DKUI_NATIVE_STAGE_FAST_SCAN=')]
        valid = bool(values) and set(values) == {'1'} if source in SOURCES else not values
        assert valid, 'GUI immutable scan scope changed: ' + source
        # No detached-stage operand-cache wrapper is authorized in the GUI or
        # in a live worker by this decoder-only build change.
        assert not any(arg.startswith(('-DKUI_NATIVE_CACHED_EXTENT=',
                                        '-DKUI_NATIVE_CACHED_RESOLUTION=')) for arg in command)
        rows.append(dict(source=source, aligned_immutable_decoder=source in SOURCES))
    return rows


ALIGNMENT = r'''
#define main kui_exit_fixture_main
#include "tests/test_vmu_native_exit.c"
#undef main
#include "kui/native_scan_bytes.h"
int main(void) {
    /* End every supplied span at ASan's allocation redzone. */
    for(unsigned offset=0u;offset<8u;offset++)for(unsigned length=4u;length<36u;length++) {
        uint8_t *base=malloc(offset+length);CHECK(base);uint8_t *p=base+offset;
        memset(base,0x5a,offset);
        for(unsigned i=0u;i<length;i++)p[i]=(uint8_t)(i*67u+offset*29u+length);
        for(unsigned at=0u;at+2u<=length;at++)
            CHECK(kui_native_scan_le16(p+at)==(uint16_t)((uint16_t)p[at]|(uint16_t)p[at+1u]<<8));
        for(unsigned at=0u;at+4u<=length;at++)
            CHECK(kui_native_scan_le32(p+at)==((uint32_t)p[at]|(uint32_t)p[at+1u]<<8|
                (uint32_t)p[at+2u]<<16|(uint32_t)p[at+3u]<<24));
        for(unsigned i=0u;i<offset;i++)CHECK(base[i]==0x5au);
        free(base);
    }
    for(unsigned family=0u;family<3u;family++) {
        if(family) pvr_fixture(family==2u);else fixture();
        struct kui_vmu_native_exit expected;CHECK(discover(image,sizeof(image),load,&expected));
        for(unsigned offset=0u;offset<8u;offset++) {
            uint8_t *base=malloc(sizeof(image)+offset);CHECK(base);
            memset(base,0x5a,offset);memcpy(base+offset,image,sizeof(image));
            struct kui_vmu_native_exit got;
            CHECK(discover(base+offset,sizeof(image),load,&got));CHECK(!memcmp(&got,&expected,sizeof(got)));
            CHECK(!memcmp(base+offset,image,sizeof(image)));
            base[offset+pair]^=1u;CHECK(!discover(base+offset,sizeof(image),load,&got));
            base[offset+pair]^=1u;CHECK(!memcmp(base+offset,image,sizeof(image)));
            for(unsigned i=0u;i<offset;i++)CHECK(base[i]==0x5au);
            free(base);
        }
    }
    printf("Exact byte/reference decoders and exit metadata: %u checks; alignments0..7, all exact tails, body mutation refused\n",checks);
    return 0;
}
'''


def fixture_equivalence(startups):
    compiler = shlex.split(os.environ.get('HOST_CC', 'cc'))
    startups = tuple(map(str, startups))
    outputs = {}
    with tempfile.TemporaryDirectory(prefix='kui-gui-fast-scan-') as directory:
        folder = Path(directory)
        alignment = folder / 'alignment.c'
        alignment.write_text(ALIGNMENT)
        cases = {
            'program': ('tests/test_vmu_native_program.c', *SOURCES, *SUPPORT),
            'heap': ('tests/test_vmu_native_heap.c',),
            'exit': ('tests/test_vmu_native_exit.c', *SOURCES, *SUPPORT),
            'unaligned': (str(alignment), *SOURCES, *SUPPORT),
        }
        for name, sources in cases.items():
            results = []
            for fast in (0, 1):
                executable = folder / f'{name}-{fast}'
                run([*compiler, *FLAGS, f'-DKUI_NATIVE_STAGE_FAST_SCAN={fast}',
                     *sources, '-o', str(executable)])
                args = list(startups) if name in ('program', 'exit') else []
                results.append(run([str(executable), *args]).strip())
            assert results[0] == results[1], 'Byte/fast fixture outcomes differ: ' + name
            outputs[name] = results[1]
    return outputs


def linked_gui(path):
    sys.path.insert(0, str(ROOT / 'tools'))
    from check_vmu_native_worker_gateway import read_elf, symbol_bytes
    # The KOS GUI owns TLS; this is not a freestanding worker ELF.
    image = read_elf(path, allow_tls=True)
    assert image[4].get('__kui_native_stage_fast_scan') == (1, 0, 0, 0xfff1), \
        'GUI does not contain the compiled immutable fast-scan marker'
    bodies = {}
    for name in ('_kui_vmu_native_program_extent', '_kui_vmu_native_heap_discover',
                 '_kui_vmu_native_exit_discover'):
        _, body, _ = symbol_bytes(image, name, 2)
        assert body, 'GUI immutable proof body missing: ' + name
        bodies[name] = hashlib.sha256(body).hexdigest()
    return dict(elf_sha256=hashlib.sha256(image[0]).hexdigest(), marker=1,
                function_sha256=bodies, actual_SH_execution=False)


def audit(elf=None, startups=()):
    recipes = effective_recipes()
    outputs = fixture_equivalence(startups)
    inputs = ('Makefile.dc', 'include/kui/native_scan_bytes.h', *SOURCES, *SUPPORT,
              'tests/test_vmu_native_program.c', 'tests/test_vmu_native_heap.c',
              'tests/test_vmu_native_exit.c', 'tests/test_native_gui_fast_scan.py')
    return dict(schema='kui.native.gui-fast-scan.v1', status='PASS',
                effective_GUI_compile_policy=recipes, byte_fast_outcomes_identical=True,
                unaligned_and_exact_tail_fallback=True, sanitizers=['address', 'undefined'],
                outputs=outputs, linked_gui=linked_gui(elf) if elf else None,
                source_sha256={name: hashlib.sha256((ROOT / name).read_bytes()).hexdigest() for name in inputs},
                limits='Host admission/equivalence and optional linked marker/body identity; console timing and gameplay are unverified.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--elf', type=Path)
    parser.add_argument('--startup', type=Path, action='append', default=[])
    args = parser.parse_args()
    print(json.dumps(audit(args.elf, map(str, args.startup)), indent=2))
