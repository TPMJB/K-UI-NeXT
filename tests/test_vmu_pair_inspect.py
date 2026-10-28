#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Offline virtual-card diagnosis on synthetic pairs: journal replay through
the production store and validator, and the rule-by-rule explanation."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools')]
from vmu_persist_profile import decode_profile, template_profile
from explain_vmu_image import explain

SOURCES = ('tools/vmu_pair_inspect.c', 'src/core/vmu_persist_profile.c', 'src/core/vmu_store.c',
           'src/core/vmu_image.c', 'src/core/retail_image.c', 'src/core/hash.c', 'src/core/data.c')
BLANK = r'''
#include <stdio.h>
#include "kui/vmu_image.h"
int main(int argc,char **argv) {
    FILE *f=fopen(argv[1],"wb");unsigned char b[512];
    for(unsigned i=0;i<256u;i++) {kui_vmu_image_blank_block((uint16_t)i,b);fwrite(b,1,512,f);}
    return argc!=2 || fclose(f);
}
'''


class PairInspect(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='kui-pair-inspect-')
        cls.dir = Path(cls.work.name)
        cc = os.environ.get('CC', 'cc')
        cls.tool = cls.dir/'inspect'
        subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-I', str(ROOT/'include'), *[str(ROOT/s) for s in SOURCES], '-o', str(cls.tool)],
                       check=True)
        (cls.dir/'blank.c').write_text(BLANK)
        subprocess.run([cc, '-std=c11', '-I', str(ROOT/'include'), str(cls.dir/'blank.c'),
                        str(ROOT/'src/core/vmu_image.c'), '-o', str(cls.dir/'blank')], check=True)
        subprocess.run([str(cls.dir/'blank'), str(cls.dir/'card.vmd')], check=True)
        (cls.dir/'profile.bin').write_bytes(template_profile())
        (cls.dir/'card.journal').write_bytes(bytes(1024))

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def inspect(self, image):
        (self.dir/'case.vmd').write_bytes(image)
        before = image
        run = subprocess.run([str(self.tool), str(self.dir/'profile.bin'), str(self.dir/'case.vmd'),
                              str(self.dir/'card.journal'), str(self.dir/'out.vmd')],
                             capture_output=True, text=True)
        self.assertEqual((self.dir/'case.vmd').read_bytes(), before, 'inputs are only read')
        return json.loads(run.stdout), run.returncode

    def test_blank_card_opens_and_validates(self):
        report, code = self.inspect((self.dir/'card.vmd').read_bytes())
        self.assertEqual((code, report['store_open'], report['validate'], report['free_blocks']), (0, 0, 0, 200))
        self.assertEqual(explain((self.dir/'out.vmd').read_bytes())['problems'], [])

    def test_orphan_block_and_entry_tail_are_named(self):
        image = bytearray((self.dir/'card.vmd').read_bytes())
        fat, entry = 512*254, 512*253
        for at, nxt in ((199, 198), (198, 0xfffa), (10, 0xfffa)):
            struct.pack_into('<H', image, fat+2*at, nxt)
        image[entry] = 0x33
        struct.pack_into('<H', image, entry+2, 199)
        image[entry+4:entry+16] = b'EXAMPLE_SAVE'
        struct.pack_into('<H', image, entry+24, 2)
        report, code = self.inspect(bytes(image))
        self.assertEqual((report['store_open'], report['validate']), (0, -5))
        self.assertNotEqual(code, 0)
        self.assertEqual(explain(bytes(image))['problems'],
                         ['FAT[10]=0xfffa is allocated but no file owns block 10',
                          '2 owned + 197 free blocks is not 200'])
        image[entry+28] = 1
        self.assertIn('directory 253 slot 0: bytes 0x1c-0x1f are 01000000, not zero',
                      explain(bytes(image))['problems'])

    def test_template_maps_one_base_and_journal(self):
        profile = decode_profile(template_profile())
        self.assertEqual([sum(e['blocks'] for e in f['useful']) for f in profile['files'][:2]], [256, 2])


if __name__ == '__main__':
    unittest.main()
