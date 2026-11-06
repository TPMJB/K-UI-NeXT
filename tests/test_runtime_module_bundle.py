#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Compile the production codec and runtime adapter with authored bundle bytes."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import build_runtime_module_bundle as bundle
from runtime_package import envelope


class BundleTests(unittest.TestCase):
    def setUp(self):
        self.members={'apps/games/test-module.kui':envelope(bytes((i*17)&255 for i in range(4096)),4096,'123456789abc')}
        self.wire=bundle.encode(self.members,'123456789abc')

    def test_roundtrip_unique_typed_location_and_currentbuild(self):
        self.assertEqual(bundle.decode(self.wire),self.members)
        self.assertEqual(bundle.locate(b'prefix'+self.wire+b'suffix'),(6,self.wire))
        for payload in (b'no bundle',self.wire+self.wire,self.wire[:-1]):
            with self.assertRaises(ValueError):bundle.locate(payload)
        with self.assertRaises(ValueError):bundle.encode(self.members,'abcdef012345')

    def test_header_manifest_payload_and_trailing_damage(self):
        for at in (0,8,12,16,20,24,28,32,44,64,96,176,180,184,188,192,len(self.wire)-1):
            damaged=bytearray(self.wire);damaged[at]^=1
            with self.subTest(at=at),self.assertRaises((ValueError,zlib.error)):bundle.decode(bytes(damaged))
        damaged=bytearray(self.wire+b'x')
        struct.pack_into('<I',damaged,24,len(damaged));struct.pack_into('<I',damaged,180,struct.unpack_from('<I',damaged,180)[0]+1)
        damaged[64:96]=hashlib.sha256(damaged[96:224]).digest()
        with self.assertRaises(ValueError):bundle.decode(bytes(damaged))

    def test_actual_c_codec_runtime_priority_and_failure_paths(self):
        with tempfile.TemporaryDirectory(prefix='kui-bundle-test-') as tmp:
            directory=Path(tmp);bundle.build(self.members,'123456789abc',directory)
            binary=directory/'check'
            command=['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
                '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
                '-Iinclude','-I.deps/fatfs/source','-Ithird_party/miniz',
                '-DMINIZ_NO_ARCHIVE_APIS','-DMINIZ_NO_STDIO','-DMINIZ_NO_TIME','-DMINIZ_NO_ZLIB_APIS','-DMINIZ_NO_MALLOC',
                'tests/test_runtime_module_bundle.c','src/core/runtime_module_bundle.c',
                'src/core/runtime_file.c','src/core/runtime_image.c','src/core/hash.c','src/core/data.c',
                'third_party/miniz/miniz.c',str(directory/'bundle.S'),
                '-Wl,--wrap=malloc,--wrap=kui_runtime_module_bundle_read','-o',str(binary)]
            subprocess.run(command,cwd=ROOT,check=True,capture_output=True,text=True)
            run=subprocess.run([str(binary),str(directory/'bundle.bin')],check=True,capture_output=True,text=True,
                env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
            self.assertIn('PASS runtime module bundle',run.stdout)
            print(run.stdout.strip())


if __name__=='__main__':unittest.main()
