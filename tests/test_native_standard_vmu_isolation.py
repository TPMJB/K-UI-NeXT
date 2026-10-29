#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the Standard control's real resident guards and build selection."""
from pathlib import Path
import os
import re
import shlex
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def compile_source(source, flags=(), run=False):
    with tempfile.TemporaryDirectory(prefix='kui-standard-isolation-') as directory:
        output = Path(directory)/'probe'
        command = [*shlex.split(os.environ.get('HOST_CC','cc')), '-std=c11', '-O1',
                   '-Wall','-Wextra','-Werror','-Wpedantic','-Wno-unused-function',
                   '-I',str(ROOT/'include'),*flags,'-x','c','-']
        command += ['-o',str(output)] if run else ['-fsyntax-only']
        result = subprocess.run(command,input=source,text=True,capture_output=True,cwd=ROOT)
        if run and result.returncode == 0:
            result = subprocess.run([str(output)],text=True,capture_output=True,cwd=ROOT)
        return result


class StandardIsolation(unittest.TestCase):
    def test_geometry_preserves_the_current_reservation(self):
        for flags,bottom,length in (((),0,0x30000),
            (('-DKUI_NATIVE_STANDARD_VMU_ISOLATION=1',),1,0x55000),
            (('-DKUI_NATIVE_DUAL_READERS=1','-DKUI_NATIVE_READAHEAD=1'),1,0x55000),
            (('-DKUI_NATIVE_DUAL_READERS=1','-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1'),1,0x51000),
            (('-DKUI_NATIVE_DUAL_READERS=1','-DKUI_NATIVE_READAHEAD=1',
              '-DKUI_NATIVE_GENERAL_LEAN_LAYOUT=1'),1,0x51000)):
            source = '#include "kui/vmu_native_background.h"\n'
            source += f'_Static_assert(KUI_NATIVE_CURRENT_BOTTOM_HEAP=={bottom},"placement");\n'
            source += f'_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES=={length},"lease");\n'
            result=compile_source(source,flags)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_mixed_or_invalid_selectors_refuse(self):
        for flags in (( '-DKUI_NATIVE_STANDARD_VMU_ISOLATION=2',),
            ('-DKUI_NATIVE_STANDARD_VMU_ISOLATION=1','-DKUI_NATIVE_DUAL_READERS=1'),
            ('-DKUI_NATIVE_STANDARD_VMU_ISOLATION=1','-DKUI_NATIVE_READAHEAD=1'),
            ('-DKUI_NATIVE_STANDARD_VMU_ISOLATION=1','-DKUI_VMU_NATIVE_BACKGROUND=1'),
            ('-DKUI_NATIVE_STANDARD_VMU_ISOLATION=1','-DKUI_NATIVE_READER_ONLY_ISOLATION=1')):
            result=compile_source('#include "kui/vmu_native_background.h"\n',flags)
            self.assertNotEqual(result.returncode,0,flags)

    def test_real_resident_excludes_the_full_selected_lease(self):
        source=(ROOT/'src/loader/vmu_maple_observe_resident.c').read_text()
        match=re.search(r'/\* NATIVE_RESIDENT_GUARDS_BEGIN.*?\*/\n(.*?)/\* NATIVE_RESIDENT_GUARDS_END \*/',source,re.S)
        self.assertIsNotNone(match)
        program='''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "kui/vmu_native_background.h"
#include "kui/vmu_maple_observe_boot.h"
'''+match.group(1)+'''
int main(void) {
    struct kui_vmu_observe_boot_control c={0};
    c.status=KUI_VMU_OBS_BOOT_INSTALLED;c.lease=UINT32_C(0x0c600020);
    c.worker_end=(c.lease|UINT32_C(0xa0000000))+UINT32_C(0x20000);
    c.terminal=(c.lease|UINT32_C(0x80000000))+80u;
    c.sd_transaction=c.terminal+2u;
    uint32_t begin=0,end=0,length=NATIVE_RESIDENT_LEASE_BYTES;
    assert(native_resident_lease(&c,&begin,&end));
    assert(begin==c.lease-32u && end==c.lease+length);
    assert(native_resident_terminal(&c));
    assert(native_resident_transaction(&c,c.sd_transaction));
    assert(!native_resident_transaction(&c,c.sd_transaction+2u));
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned a=0;a<3;a++) {
        for(uint32_t offset=0;offset<length;offset+=512u)
            assert(!native_resident_destination(&c,(c.lease+offset)|aliases[a],512u));
        assert(!native_resident_destination(&c,(c.lease+length-1u)|aliases[a],1u));
        assert(!native_resident_destination(&c,(c.lease-1u)|aliases[a],1u));
        assert(native_resident_destination(&c,(c.lease+length)|aliases[a],512u));
        assert(native_resident_destination(&c,(c.lease-64u)|aliases[a],32u));
        assert(!native_resident_destination(&c,(c.lease-64u)|aliases[a],33u));
#if KUI_NATIVE_STANDARD_VMU_ISOLATION
        assert(!native_resident_destination(&c,(c.lease+UINT32_C(0x30000))|aliases[a],512u));
#else
        assert(native_resident_destination(&c,(c.lease+UINT32_C(0x30000))|aliases[a],512u));
#endif
    }
    c.lease=UINT32_C(0x0d000000)-length;
    assert(native_resident_lease(&c,&begin,&end) && end==UINT32_C(0x0d000000));
    c.lease+=32u;assert(!native_resident_lease(&c,&begin,&end));
    return 0;
}
'''
        for isolation in (0,1):
            flags=('-DKUI_VMU_NATIVE_PERSISTENT_PROBE=1','-DKUI_VMU_NATIVE_CACHED_CODE=1',
                   f'-DKUI_NATIVE_STANDARD_VMU_ISOLATION={isolation}')
            result=compile_source(program,flags,run=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_makefile_selects_synchronous_standard_sources(self):
        command=['make','-f','Makefile.vmu-native-isolation-standard',
                 'BUILD=build/standard-isolation-test-dryrun','-n','-B','all']
        result=subprocess.run(command,cwd=ROOT,text=True,capture_output=True)
        self.assertEqual(result.returncode,0,result.stderr)
        self.assertIn('-DKUI_NATIVE_STANDARD_VMU_ISOLATION=1',result.stdout)
        self.assertIn('src/loader/vmu_maple_observe_resident.c',result.stdout)
        for item in ('-DKUI_VMU_NATIVE_BACKGROUND=1','-DKUI_NATIVE_SDK_SCI_ROUTE=1',
                     '-DKUI_NATIVE_READAHEAD=1','-DKUI_NATIVE_DUAL_READERS=1',
                     ' -c src/loader/retail_async.c ',' -c src/loader/sci_stream.c '):
            self.assertNotIn(item,result.stdout)
        self.assertIn('retail-native-isolation-standard.kui',result.stdout)


if __name__=='__main__':
    unittest.main()
