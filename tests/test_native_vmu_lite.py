#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the real reservation and low-reader guards at the VMU-lite size."""
from pathlib import Path
import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('vmu_only', ROOT/'tests/test_native_vmu_only.py')
vmu_only = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(vmu_only)
LITE = {'KUI_NATIVE_VMU_LITE': 1}
SPAN = 0x12000


class VMULite(unittest.TestCase):
    def test_shared_build_recompiles_every_object_family_on_variant_switch(self):
        """Run the real incremental dependencies with a recording compiler.

        This checks rebuild decisions and selected flags, not SH compilation.
        Reusing one directory must not mix the two worker ABIs or retain the
        separate full-submit experiment when another variant is selected.
        """
        with tempfile.TemporaryDirectory(prefix='vmu-mode-switch-') as temporary:
            directory = Path(temporary)
            build, compiler = directory/'build', directory/'record_compiler.py'
            log = directory/'compiles.jsonl'
            compiler.write_text('''import json, pathlib, sys
arguments = sys.argv[1:]
output = pathlib.Path(arguments[arguments.index('-o') + 1])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(arguments))
with pathlib.Path(__file__).with_name('compiles.jsonl').open('a') as stream:
    stream.write(json.dumps(arguments) + '\\n')
''')
            objects = [build/path for path in (
                'native/src/core/vmu_native_heap.o',
                'resident/src/loader/sci_sd_bus.o',
                'stage/src/core/hash.o',
                'worker/src/loader/vmu_maple_observe_worker.o')]

            def run(makefile):
                result = subprocess.run(['make', '--no-print-directory', '-f', makefile,
                    f'BUILD={build}', f'CC={sys.executable} {compiler}',
                    '-o', str(build/'vmu_maple_observe_symbols.h'),
                    '-o', str(build/'worker.bin'),
                    'VMU_TRANSPORT=1', *map(str, objects)],
                    cwd=ROOT, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                return [json.loads(line) for line in log.read_text().splitlines()]

            count = 0
            for variant in ('vmu-only', 'vmu-lite', 'vmu-only',
                            'vmu-lite-full-submit', 'vmu-lite',
                            'vmu-only-full-submit', 'vmu-lite-full-submit', 'vmu-only'):
                with self.subTest(variant=variant):
                    makefile = f'Makefile.vmu-native-{variant}'
                    rows = run(makefile)
                    self.assertEqual(len(rows), count + len(objects),
                                     'Variant switch retained a stale object')
                    for row in rows[count:]:
                        self.assertEqual('-DKUI_NATIVE_VMU_LITE=1' in row,
                                         variant.startswith('vmu-lite'))
                        self.assertEqual('-DKUI_NATIVE_VMU_ONLY_FULL_SUBMIT=1' in row,
                                         variant.endswith('full-submit'))
                    count = len(rows)
                    self.assertEqual(len(run(makefile)), count,
                                     'An unchanged variant rebuilt its objects')

    def test_worker_is_the_complete_bounded_lease(self):
        result = vmu_only.compile_run(f'''
#include "kui/vmu_native_background.h"
#include "kui/vmu_native_passive.h"
#include "kui/vmu_readonly_overlay.h"
_Static_assert(KUI_NATIVE_CURRENT_BOTTOM_HEAP,"Existing bottom allocation");
_Static_assert(KUI_VMU_NATIVE_PASSIVE_SPAN=={SPAN:#x},"72 KiB worker");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES=={SPAN:#x},"72 KiB worker");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES=={SPAN:#x},"Worker-only allocation");
_Static_assert(KUI_VMU_NATIVE_LITE_CHAIN_BYTES==0x1000,"Bounded chain copies");
_Static_assert(KUI_VMU_READONLY_OVERLAY_STAGED_FRAMES==1u,"One staged A2 reply");
int main(void) {{ return 0; }}
''', LITE)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_lite_needs_the_vmu_only_service(self):
        for changed in ({'KUI_NATIVE_VMU_LITE': 2}, {'KUI_NATIVE_VMU_LITE': 1, 'KUI_NATIVE_VMU_ONLY': 0}):
            with self.subTest(changed=changed):
                result = vmu_only.compile_run('#include "kui/native_isolation.h"\n', changed, execute=False)
                self.assertNotEqual(result.returncode, 0, changed)

    def test_actual_low_reader_guards_protect_only_the_lite_allocation(self):
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
''' + match.group(1) + f'''
int main(void) {{
    const uint32_t aliases[]={{0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)}};
    struct kui_vmu_observe_boot_control c={{0}};
    uint32_t begin=0,end=0;
    c.status=KUI_VMU_OBS_BOOT_INSTALLED;c.lease=UINT32_C(0x0c600020);
    c.worker_end=(c.lease|UINT32_C(0xa0000000))+UINT32_C({SPAN:#x});
    c.terminal=(c.lease|UINT32_C(0x80000000))+80u;c.sd_transaction=c.terminal+2u;
    assert(native_resident_lease(&c,&begin,&end));
    assert(begin==c.lease-32u && end==c.lease+UINT32_C({SPAN:#x}));
    assert(native_resident_terminal(&c));
    for(unsigned a=0u;a<3u;a++) {{
        for(uint32_t offset=0u;offset<UINT32_C({SPAN:#x});offset+=32u)
            assert(!native_resident_destination(&c,(c.lease+offset)|aliases[a],32u));
        assert(!native_resident_destination(&c,(c.lease-32u)|aliases[a],32u));
        assert(!native_resident_destination(&c,(end-1u)|aliases[a],2u));
        assert(native_resident_destination(&c,end|aliases[a],512u));
        assert(native_resident_destination(&c,(begin-32u)|aliases[a],32u));
        /* The memory the 112 KiB build reserved past 72 KiB is the game's. */
        assert(native_resident_destination(&c,(c.lease+UINT32_C(0x12000))|aliases[a],512u));
        assert(native_resident_destination(&c,(c.lease+UINT32_C(0x1bfe0))|aliases[a],32u));
    }}
    c.worker_end+=32u;assert(!native_resident_terminal(&c));c.worker_end-=32u;
    c.terminal=(c.lease|UINT32_C(0x80000000))+UINT32_C({SPAN:#x});assert(!native_resident_terminal(&c));
    c.terminal=(c.lease|UINT32_C(0x80000000))+80u;
    c.lease=UINT32_C(0x0d000000)-UINT32_C({SPAN:#x});
    c.worker_end=(c.lease|UINT32_C(0xa0000000))+UINT32_C({SPAN:#x});
    assert(native_resident_lease(&c,&begin,&end) && end==UINT32_C(0x0d000000));
    c.lease+=32u;assert(!native_resident_lease(&c,&begin,&end));
    return 0;
}}
'''
        result = vmu_only.compile_run(program, LITE)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_actual_bottom_heap_transitions_at_the_lite_size(self):
        result = vmu_only.compile_run(f'''
#define main actual_bottom_heap_main
#include "tests/test_vmu_native_heap_bottom.c"
#undef main
_Static_assert(LEASE=={SPAN:#x},"Exercise the delivered lease size");
int main(void) {{ return actual_bottom_heap_main(); }}
''', LITE, sources=('src/core/vmu_native_heap.c', 'src/core/vmu_native_lifetime.c'),
            extra=('-I.', f'-DKUI_NATIVE_HEAP_TEST_LEASE_BYTES={SPAN:#x}u'))
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('checks', result.stdout)

    def test_build_selects_the_lite_layout_and_synchronous_reader(self):
        result = subprocess.run(['make', '-f', 'Makefile.vmu-native-vmu-lite',
                                 'BUILD=build/vmu-lite-selection-dryrun', '-n', '-B', 'all'],
                                cwd=ROOT, text=True, capture_output=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        for expected in ('-DKUI_NATIVE_VMU_ONLY=1', '-DKUI_NATIVE_VMU_LITE=1',
                         '-Tsrc/loader/vmu_native_lite.ld', f'--span-bytes {SPAN:#x}',
                         'src/loader/vmu_native_passive.c', 'retail-native-vmu-lite.kui'):
            self.assertIn(expected, result.stdout)
        for unexpected in ('-DKUI_NATIVE_READAHEAD=1', '-DKUI_NATIVE_DUAL_READERS=1',
                           '-DKUI_VMU_NATIVE_BACKGROUND=1', ' -c src/loader/native_readahead_aux.c '):
            self.assertNotIn(unexpected, result.stdout)

    def test_full_submit_control_preserves_the_lite_layout_for_both_transports(self):
        for transport in (0, 1):
            with self.subTest(transport=transport):
                result = subprocess.run(['make', '-f',
                    'Makefile.vmu-native-vmu-lite-full-submit',
                    'BUILD=build/vmu-lite-full-selection-dryrun',
                    f'VMU_TRANSPORT={transport}', '-n', '-B', 'all'],
                    cwd=ROOT, text=True, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                for expected in ('-DKUI_NATIVE_VMU_LITE=1',
                                 '-DKUI_NATIVE_VMU_ONLY_FULL_SUBMIT=1',
                                 f'-DKUI_NATIVE_VMU_ONLY_TRANSPORT={transport}',
                                 '-Tsrc/loader/vmu_native_lite.ld',
                                 f'--span-bytes {SPAN:#x}',
                                 'retail-native-vmu-lite-full-submit.kui'):
                    self.assertIn(expected, result.stdout)
                for unexpected in ('--span-bytes 0x1c000',
                                   '-DKUI_NATIVE_READAHEAD=1',
                                   '-DKUI_NATIVE_DUAL_READERS=1',
                                   '-DKUI_VMU_NATIVE_BACKGROUND=1'):
                    self.assertNotIn(unexpected, result.stdout)


if __name__ == '__main__':
    unittest.main()
