# SPDX-License-Identifier: GPL-3.0-only
"""Bind zero-byte resume metadata to the exact surviving low instructions."""
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from check_loader_layout import EH, SH, SYM
from vmu_maple_observe_symbols import generate


def entries(data):
    header = EH.unpack_from(data)
    sections = [SH.unpack_from(data, header[6] + i * header[11]) for i in range(header[12])]
    result = {}
    for table in sections:
        if table[1] != 2:
            continue
        strings = sections[table[6]]
        names = data[strings[4]:strings[4] + strings[5]]
        for at in range(table[4], table[4] + table[5], table[9]):
            name, value, size, _, _, owner = SYM.unpack_from(data, at)
            if name:
                label = names[name:names.find(b'\0', name)].decode('ascii')
                result[label] = (at, value, size, sections[owner] if owner < len(sections) else None)
    return result


class ResumeSymbols(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = Path(os.environ.get('KUI_NATIVE_ARTIFACT_DIR', ROOT / 'build/native-background-compact'))
        cls.sources = [directory / 'resident-sci.elf', directory / 'resident-legacy.elf']
        if not all(path.is_file() for path in cls.sources):
            raise unittest.SkipTest('Build both linked Background residents first')

    def refused(self, data):
        with tempfile.TemporaryDirectory(prefix='kui-resume-symbol-') as temporary:
            path = Path(temporary) / 'mutant.elf'
            path.write_bytes(data)
            with self.assertRaises((ValueError, KeyError, struct.error)):
                generate(path)

    def test_exact_both_reader_seams(self):
        for path in self.sources:
            with self.subTest(reader=path.name):
                self.assertIn('#define KUI_VMU_NATIVE_HEAP_RESUME_OFFSET 0x000000a4u', generate(path))

    def test_every_offset_bit_and_symbol_role(self):
        for path in self.sources:
            source = path.read_bytes()
            at, value, _, _ = entries(source)['__kui_native_heap_resume_offset']
            for bit in range(32):
                with self.subTest(reader=path.name, bit=bit):
                    data = bytearray(source)
                    struct.pack_into('<I', data, at + 4, value ^ (1 << bit))
                    self.refused(data)
            for field, encoded in ((8, struct.pack('<I', 2)), (12, b'\x00'),
                                   (12, b'\x12'), (13, b'\x02'),
                                   (14, struct.pack('<H', 0)), (14, struct.pack('<H', 1)),
                                   (14, struct.pack('<H', 0xff00))):
                data = bytearray(source)
                data[at + field:at + field + len(encoded)] = encoded
                self.refused(data)

    def test_every_seam_instruction_bit_and_duplicate(self):
        for path in self.sources:
            source = path.read_bytes()
            symbols = entries(source)
            marker, offset, _, _ = symbols['__kui_native_heap_resume_offset']
            _, hook, _, section = symbols['_kui_vmu_observe_heap_hook']
            start = section[4] + hook - section[3] + offset - 4
            for byte in range(8):
                for bit in range(8):
                    data = bytearray(source)
                    data[start + byte] ^= 1 << bit
                    self.refused(data)
            data = bytearray(source)
            data[start - 16:start - 8] = source[start:start + 8]
            struct.pack_into('<I', data, marker + 4, offset - 16)
            self.refused(data)


if __name__ == '__main__':
    unittest.main()
