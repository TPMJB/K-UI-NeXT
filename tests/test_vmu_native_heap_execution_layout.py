# SPDX-License-Identifier: GPL-3.0-only
"""Reject mismatched linked profiles and corrupted private worker-stack bounds."""
from pathlib import Path
import struct
import tempfile
import unittest

from test_vmu_native_heap_execution import ROOT, stage_layout
from check_loader_layout import EH, SH, SYM


def symbol_offset(data, name):
    header=EH.unpack_from(data)
    sections=[SH.unpack_from(data,header[6]+i*header[11]) for i in range(header[12])]
    for table in sections:
        if table[1]!=2:continue
        strings=sections[table[6]]
        names=data[strings[4]:strings[4]+strings[5]]
        for at in range(table[4],table[4]+table[5],table[9]):
            entry=SYM.unpack_from(data,at)
            if names[entry[0]:].split(b'\0',1)[0].decode('ascii')==name:return at
    raise AssertionError('Missing fixture symbol: '+name)


class HeapExecutionLayout(unittest.TestCase):
    def fixtures(self):
        profiles=(('native-background-compact',0xc00,'compact'),
                  ('native-background-compact-a1',0xc00,'compact'),
                  ('native-session-controls',0x1000,'general'),
                  ('native-session-controls-a1',0x1000,'general'))
        found=[]
        for name,stack,profile in profiles:
            directory=ROOT/'build'/name
            if all((directory/file).is_file() for file in ('stage.elf','native-worker.elf','native-worker.bin')):
                found.append((directory,stack,profile))
        if not found:self.skipTest('Build a compact or General linked stage/worker first')
        return found

    def copied(self,directory,temporary):
        target=Path(temporary)
        for name in ('stage.elf','native-worker.elf','native-worker.bin'):
            (target/name).write_bytes((directory/name).read_bytes())
        return target

    def refused(self,directory):
        with self.assertRaises((AssertionError,ValueError,KeyError,struct.error)):
            stage_layout(directory)

    def test_actual_compact_and_general_geometry(self):
        for directory,stack,_ in self.fixtures():
            with self.subTest(profile=directory.name):
                *_,worker=stage_layout(directory)
                words=struct.unpack_from('<20I',worker)
                self.assertEqual(words[10]-words[9],stack)

    def test_every_private_stack_bottom_bit_is_bound(self):
        for directory,_,_ in self.fixtures():
            for bit in range(32):
                with self.subTest(profile=directory.name,bit=bit),tempfile.TemporaryDirectory() as temporary:
                    target=self.copied(directory,temporary)
                    path=target/'native-worker.bin';data=bytearray(path.read_bytes())
                    struct.pack_into('<I',data,36,struct.unpack_from('<I',data,36)[0]^(1<<bit))
                    path.write_bytes(data);self.refused(target)

    def test_unknown_marker_and_mismatched_stage_refuse(self):
        for directory,_,profile in self.fixtures():
            marker=('__kui_native_background_compact_' if profile=='compact' else
                    '__kui_native_general_lean_')
            for filename,name in (('native-worker.elf',marker+'worker'+('' if profile=='compact' else '_layout')),
                                  ('stage.elf',marker+'stage'+('' if profile=='compact' else '_layout')),
                                  ('native-worker.elf','__kui_native_worker_span')):
                with self.subTest(profile=directory.name,symbol=name),tempfile.TemporaryDirectory() as temporary:
                    target=self.copied(directory,temporary);path=target/filename;data=bytearray(path.read_bytes())
                    at=symbol_offset(data,name);struct.pack_into('<I',data,at+4,2)
                    path.write_bytes(data);self.refused(target)

    def test_coherent_bss_end_cannot_enter_private_stack(self):
        for directory,_,_ in self.fixtures():
            with self.subTest(profile=directory.name),tempfile.TemporaryDirectory() as temporary:
                target=self.copied(directory,temporary);path=target/'native-worker.bin';data=bytearray(path.read_bytes())
                end=struct.unpack_from('<I',data,36)[0]+32
                struct.pack_into('<I',data,32,end);path.write_bytes(data)
                path=target/'native-worker.elf';data=bytearray(path.read_bytes())
                at=symbol_offset(data,'__vmu_native_worker_bss_end');struct.pack_into('<I',data,at+4,end)
                path.write_bytes(data);self.refused(target)


if __name__=='__main__':unittest.main()
