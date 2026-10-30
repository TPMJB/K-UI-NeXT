#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute real reservation/low-reader guards for the independent VMU build."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SELECTORS = {
    'KUI_NATIVE_VMU_ONLY': 1,
    'KUI_VMU_NATIVE_PERSISTENT_PROBE': 1,
    'KUI_VMU_NATIVE_CACHED_CODE': 1,
    'KUI_NATIVE_SPLIT_CONTROLLER_ISOLATION': 1,
    'KUI_NATIVE_SPLIT_CONTROLLER_GENERAL': 1,
    'KUI_NATIVE_GENERAL_LEAN_LAYOUT': 1,
}
HOST_FLAGS = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
              '-Wpedantic', '-Wno-unused-function', '-fno-pie', '-no-pie',
              '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
              '-Iinclude', '-Isrc/loader']


def defines(changes=None):
    values = dict(SELECTORS)
    values.update(changes or {})
    return [f'-D{name}={value}' for name, value in values.items()]


def compile_run(source, changes=None, sources=(), extra=(), execute=True):
    with tempfile.TemporaryDirectory(prefix='kui-vmu-only-') as temporary:
        path = Path(temporary)
        program = path/'probe.c'
        program.write_text(source)
        output = path/'probe'
        command = [*shlex.split(os.environ.get('HOST_CC', 'cc')), *HOST_FLAGS,
                   *defines(changes), *extra, str(program), *sources]
        command += ['-o', str(output)] if execute else ['-fsyntax-only']
        result = subprocess.run(command, cwd=ROOT, text=True, capture_output=True)
        if execute and result.returncode == 0:
            result = subprocess.run([str(output)], cwd=ROOT, text=True, capture_output=True,
                                    env=dict(os.environ, ASAN_OPTIONS='detect_leaks=0'))
        return result


class IndependentVMU(unittest.TestCase):
    def test_worker_is_the_complete_lease(self):
        result = compile_run('''
#include <assert.h>
#include "kui/vmu_native_background.h"
#include "kui/vmu_native_passive.h"
_Static_assert(KUI_NATIVE_CURRENT_BOTTOM_HEAP,"Existing bottom allocation");
_Static_assert(KUI_VMU_NATIVE_PASSIVE_SPAN==0x1c000,"112 KiB worker");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES==0x1c000,"112 KiB worker");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==0x1c000,"Worker-only allocation");
#if KUI_NATIVE_DUAL_READERS || KUI_NATIVE_READAHEAD || KUI_VMU_NATIVE_BACKGROUND
#error "Independent VMU unexpectedly includes a background reader"
#endif
int main(void) { return 0; }
''')
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_incompatible_or_incomplete_builds_refuse(self):
        changes = [
            {'KUI_NATIVE_VMU_ONLY': 2},
            {'KUI_NATIVE_DUAL_READERS': 1},
            {'KUI_NATIVE_READAHEAD': 1},
            {'KUI_VMU_NATIVE_BACKGROUND': 1},
            {'KUI_NATIVE_STANDARD_VMU_ISOLATION': 1},
            {'KUI_NATIVE_RA_TICK_OFF_ISOLATION': 1},
            {'KUI_NATIVE_SPLIT_CONTROLLER_PREFETCH': 1},
            {'KUI_NATIVE_SPLIT_CONTROLLER_STREAM_GUARD': 1},
            {'KUI_NATIVE_READER_ONLY_ISOLATION': 1},
            {'KUI_NATIVE_METADATA_CACHE_ISOLATION': 1},
            {'KUI_NATIVE_MAPLE_BYPASS_ISOLATION': 1},
            {'KUI_NATIVE_E1_WAIT_CAPTURE': 1},
            *({name: 0} for name in SELECTORS if name != 'KUI_NATIVE_VMU_ONLY'),
        ]
        for changed in changes:
            with self.subTest(changed=changed):
                result = compile_run('#include "kui/vmu_native_background.h"\n', changed,
                                     execute=False)
                self.assertNotEqual(result.returncode, 0, changed)

    def test_actual_low_reader_guards_protect_only_the_small_allocation(self):
        source = (ROOT/'src/loader/vmu_maple_observe_resident.c').read_text()
        match = re.search(r'/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/',
                          source, re.S)
        self.assertIsNotNone(match)
        program = '''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "kui/vmu_native_background.h"
#include "kui/vmu_maple_observe_boot.h"
'''+match.group(1)+'''
int main(void) {
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    struct kui_vmu_observe_boot_control c={0};
    uint32_t begin=0,end=0;
    c.status=KUI_VMU_OBS_BOOT_INSTALLED;c.lease=UINT32_C(0x0c600020);
    c.worker_end=(c.lease|UINT32_C(0xa0000000))+UINT32_C(0x1c000);
    c.terminal=(c.lease|UINT32_C(0x80000000))+80u;c.sd_transaction=c.terminal+2u;
    assert(native_resident_lease(&c,&begin,&end));
    assert(begin==c.lease-32u && end==c.lease+UINT32_C(0x1c000));
    assert(native_resident_terminal(&c));
    assert(native_resident_transaction(&c,c.sd_transaction));
    assert(!native_resident_transaction(&c,c.sd_transaction+2u));
    for(unsigned a=0u;a<3u;a++) {
        for(uint32_t offset=0u;offset<UINT32_C(0x1c000);offset+=32u)
            assert(!native_resident_destination(&c,(c.lease+offset)|aliases[a],32u));
        assert(!native_resident_destination(&c,(c.lease-32u)|aliases[a],32u));
        assert(!native_resident_destination(&c,(end-1u)|aliases[a],2u));
        assert(!native_resident_destination(&c,(begin-1u)|aliases[a],2u));
        assert(native_resident_destination(&c,end|aliases[a],512u));
        assert(native_resident_destination(&c,(begin-32u)|aliases[a],32u));
        /* The removed map/cache tail is available to original game reads. */
        assert(native_resident_destination(&c,(c.lease+UINT32_C(0x20000))|aliases[a],512u));
        assert(native_resident_destination(&c,(c.lease+UINT32_C(0x30000))|aliases[a],512u));
    }
    c.worker_end+=32u;assert(!native_resident_terminal(&c));
    assert(!native_resident_destination(&c,end,512u));c.worker_end-=32u;
    c.terminal=(c.lease|UINT32_C(0x80000000))+UINT32_C(0x1c000);
    assert(!native_resident_terminal(&c));
    c.terminal=(c.lease|UINT32_C(0x80000000))+78u;assert(!native_resident_terminal(&c));
    c.terminal=(c.lease|UINT32_C(0xa0000000))+80u;assert(!native_resident_terminal(&c));
    c.lease=UINT32_C(0x0d000000)-UINT32_C(0x1c000);
    assert(native_resident_lease(&c,&begin,&end) && end==UINT32_C(0x0d000000));
    c.lease+=32u;assert(!native_resident_lease(&c,&begin,&end));
    c.lease=UINT32_C(0x0c010000);assert(!native_resident_lease(&c,&begin,&end));
    c.lease=UINT32_C(0x0c600021);assert(!native_resident_lease(&c,&begin,&end));
    return 0;
}
'''
        result = compile_run(program)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_bottom_heap_transitions_and_release_guards_at_small_size(self):
        result = compile_run('''
#define main actual_bottom_heap_main
#include "tests/test_vmu_native_heap_bottom.c"
#undef main
_Static_assert(LEASE==0x1c000,"Exercise the delivered lease size");
int main(void) { return actual_bottom_heap_main(); }
''', sources=('src/core/vmu_native_heap.c', 'src/core/vmu_native_lifetime.c'),
            extra=('-I.', '-DKUI_NATIVE_HEAP_TEST_LEASE_BYTES=0x1c000u'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('checks', result.stdout)

    def test_packed_diagnostic_font_matches_every_original_glyph_row(self):
        # Compile the actual renderer twice and call its own glyph decoder.
        # Section GC removes uncalled hardware rendering; no font/decoder is
        # copied into the test, and ASAN checks the final packed row access.
        program = '''
#include <stdio.h>
#include "src/loader/retail_display.c"
int main(void) {
    for(unsigned glyph=0u;glyph<36u;glyph++)
        for(unsigned row_index=0u;row_index<7u;row_index++)
            printf("%02x",glyph_row(glyph,row_index));
    putchar('\\n');return 0;
}
'''
        outputs = []
        for enabled in (0, 1):
            with self.subTest(vmu_only=enabled):
                result = compile_run(program, {'KUI_NATIVE_VMU_ONLY': enabled},
                                     extra=('-I.', '-ffunction-sections', '-fdata-sections',
                                            '-Wl,--gc-sections'))
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(len(result.stdout.strip()), 36*7*2)
                outputs.append(result.stdout)
        self.assertEqual(outputs[0], outputs[1])

    def test_build_uses_synchronous_reader_without_auxiliary_or_cache(self):
        result = subprocess.run(['make', '-f', 'Makefile.vmu-native-vmu-only',
                                 'BUILD=build/vmu-only-selection-dryrun', '-n', '-B', 'all'],
                                cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('-DKUI_NATIVE_VMU_ONLY=1', result.stdout)
        self.assertIn('src/loader/vmu_maple_observe_resident.c', result.stdout)
        self.assertIn('src/loader/vmu_native_passive.c', result.stdout)
        self.assertIn('src/core/vmu_durable.c', result.stdout)
        for unexpected in ('-DKUI_NATIVE_READAHEAD=1', '-DKUI_NATIVE_DUAL_READERS=1',
                           '-DKUI_VMU_NATIVE_BACKGROUND=1', ' -c src/loader/retail_async.c ',
                           ' -c src/loader/sci_stream.c ', ' -c src/loader/native_readahead_aux.c '):
            self.assertNotIn(unexpected, result.stdout)
        self.assertIn('retail-native-vmu-only.kui', result.stdout)


if __name__ == '__main__':
    unittest.main()
