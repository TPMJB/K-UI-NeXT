#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deletion refusal, shared references and every real-volume write boundary."""
import unittest
import os
import shlex
import struct
import subprocess
import test_vmu_library as fixture

fixture.SOURCES = ('tests/test_vmu_delete.c', *fixture.SOURCES[1:])

class VmuDeletion(fixture.VmuLibrary):
    __unittest_skip__ = False

    def check_volume(self, kind, cluster=None):
        # Use a private FatFs formatter config; no external mkfs installation
        # or production configuration change is needed for these tests.
        root=fixture.ROOT
        config=(root/'config/ffconf.h').read_text()
        marker='#define FF_USE_MKFS 0'
        self.assertEqual(config.count(marker),1)
        header=self.root/'formatter-ffconf.h'
        header.write_text(config.replace(marker,'#define FF_USE_MKFS 1'))
        source=(root/'tests/format_fatfs_fixture.c').read_text()
        self.assertEqual(source.count('exfat?4096u:512u'),1)
        source=source.replace('exfat?4096u:512u',f'exfat?{131072 if cluster=="128K" else 4096}u:512u')
        private=self.root/'formatter.c';private.write_text(source)
        formatter=self.root/'formatter'
        subprocess.run([*shlex.split(os.environ.get('CC','cc')),'-std=c11','-O1','-g',
            '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
            '-fno-omit-frame-pointer','-fno-pie','-no-pie','-include',str(header),
            '-I',str(root/'include'),'-I',str(root/'.deps/fatfs/source'),str(private),
            str(root/'.deps/fatfs/source/ff.c'),str(root/'.deps/fatfs/source/ffunicode.c'),
            '-o',str(formatter)],check=True)
        filesystem=self.root/'volume.fs'
        filesystem.unlink(missing_ok=True)
        subprocess.run([str(formatter),kind,str(filesystem)],check=True,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0'),capture_output=True)
        payload=bytearray(filesystem.read_bytes());mbr=bytearray(512)
        if kind=='fat32':
            # This fixture is placed behind an MBR after formatting.
            backup=struct.unpack_from('<H',payload,50)[0]
            struct.pack_into('<I',payload,28,2048)
            struct.pack_into('<I',payload,backup*512+28,2048)
        struct.pack_into('<B3xB3xII',mbr,446,0,0x0c if kind=='fat32' else 7,2048,len(payload)//512)
        mbr[510:512]=b'\x55\xaa';image=self.root/'disk.img'
        image.write_bytes(mbr+bytes(2048*512-512)+payload+bytes(1024*1024))
        result=subprocess.run([str(self.binary),str(image)],capture_output=True,text=True,timeout=90,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        self.assertIn('write boundaries passed',result.stdout)
        print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
