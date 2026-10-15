#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Synthetic CE ROM bounds, identity admission and stored-payload grouping."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from armada_ce_rom_audit import (KERNEL_BYTES, MAX_ROM_BYTES, ROM,
                                 ROMError, ROMInventory)
from wince_runtime_inventory import MAX_KERNELS, inventory


def fixture(*, base=0x8C010000, nominal=0x01D50000, payload_offset=0x400,
            payload=b'synthetic sound module', compressed=False,
            modules=1, total_bytes=0x1800):
    """One module, ROM file and copy record; all bytes are authored here."""
    body_offset = 0x800
    body_bytes = total_bytes - body_offset
    data = bytearray(total_bytes)
    at = lambda relative: body_offset + relative
    address = lambda relative: base + relative
    struct.pack_into('<5I', data, 16, 1, base & 0x7FFFFFFF,
                     body_offset, body_bytes, base & 0x7FFFFFFF)
    data[at(0x40):at(0x44)] = b'ECEC'
    struct.pack_into('<I', data, at(0x44), address(0x100))
    header = [0, 0, base, base + body_bytes, modules,
              base + body_bytes, base + body_bytes, 0x8D000000,
              1, address(0x340), 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0]
    struct.pack_into('<21I', data, at(0x100), *header)
    toc = [0, 0, 0, 0, address(0x200), address(0x240),
           address(0x300), nominal]
    for index in range(modules):
        struct.pack_into('<8I', data, at(0x154 + index * 32), *toc)
    struct.pack_into('<7I', data, at(0x154 + modules * 32),
                     0x20, 0, 0, 8, 8, address(0x380), address(0x440))
    data[at(0x200):at(0x20C)] = b'SNDCORE.DLL\0'
    struct.pack_into('<HHII', data, at(0x240), 1, 0x2102, 0x1000, nominal)
    struct.pack_into('<I', data, at(0x254), 0x3000)
    stored = payload.ljust(64, b'\0')
    if len(stored) != 64:
        raise ValueError('Synthetic payload must fit its 64-byte section')
    flags = 0x60000020 | (0x2000 if compressed else 0)
    struct.pack_into('<6I', data, at(0x300),
                     0x100, 0x1000, len(stored), address(payload_offset),
                     nominal + 0x1000, flags)
    struct.pack_into('<4I', data, at(0x340), address(0x460),
                     base + body_bytes, 4, 8)
    data[at(0x380):at(0x38C)] = b'default.fdf\0'
    data[at(payload_offset):at(payload_offset + len(stored))] = stored
    data[at(0x440):at(0x448)] = b'ROM-FILE'
    data[at(0x460):at(0x464)] = b'COPY'
    return data


class RuntimeInventoryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kui-wince-inventory-')
        self.addCleanup(self.temp.cleanup)
        self.directory = Path(self.temp.name)

    def write(self, data, name='kernel.bin'):
        path = self.directory / name
        path.write_bytes(data)
        return path

    def test_valid_unknown_rom_is_inventory_only(self):
        path = self.write(fixture())
        rom = ROMInventory(path)
        module = rom.modules['sndcore.dll']
        self.assertEqual(module['entry_rva'], 0x1000)
        self.assertEqual(rom.rva_read(module, 0x1000, 22),
                         b'synthetic sound module')
        self.assertEqual(module['sections'][0]['stored_sha256'],
                         hashlib.sha256(b'synthetic sound module'.ljust(64, b'\0')).hexdigest())
        self.assertEqual(rom.files[0]['name'], 'default.fdf')
        self.assertEqual(rom.files[0]['stored_sha256'], hashlib.sha256(b'ROM-FILE').hexdigest())
        self.assertEqual(rom.copies[0]['initialized_sha256'], hashlib.sha256(b'COPY').hexdigest())
        self.assertEqual(rom.exports(module), [])
        self.assertEqual(rom.imports(module), [])
        with self.assertRaisesRegex(ROMError, 'exact Armada kernel size'):
            ROM(path)
        result = inventory([path])
        self.assertFalse(result['playback_admitted'])
        self.assertEqual(result['component_groups'][0]['kernel_indices'], [0])

    def test_strict_rom_still_checks_hash_after_size(self):
        path = self.write(fixture(total_bytes=KERNEL_BYTES))
        self.assertEqual(len(ROMInventory(path).modules), 1)
        with self.assertRaisesRegex(ROMError, 'exact Armada kernel hash'):
            ROM(path)

    def test_markerless_legacy_header_requires_exact_file_identity(self):
        data = fixture()
        data[0x840:0x844] = b'\0' * 4
        path = self.write(data)
        with self.assertRaisesRegex(ROMError, 'no exact legacy inventory identity'):
            ROMInventory(path)
        exact_identity = hashlib.sha256(data).hexdigest()
        with patch.dict('armada_ce_rom_audit.LEGACY_INVENTORY_HEADERS',
                        {exact_identity: 0x8C010100}, clear=True):
            rom = ROMInventory(path)
            self.assertEqual(rom.header_address, 0x8C010100)
            self.assertIn('Exact file SHA', rom.header_provenance)
            self.assertIn('sndcore.dll', rom.modules)
            data[0x50] ^= 1
            with self.assertRaisesRegex(ROMError, 'no exact legacy inventory identity'):
                ROMInventory(self.write(data, 'changed.bin'))

    def test_duplicate_module_names_are_rejected(self):
        with self.assertRaisesRegex(ROMError, 'Duplicate ROM module name'):
            ROMInventory(self.write(fixture(modules=2)))

    def test_out_of_range_table_and_payload_pointers_are_rejected(self):
        # Header, module name/e32/o32 and section payload each have separate bounds.
        fields = (0x844, 0x954 + 16, 0x954 + 20, 0x954 + 24, 0xB00 + 12)
        for field in fields:
            with self.subTest(field=hex(field)):
                data = fixture()
                struct.pack_into('<I', data, field, 0x8C011000)
                with self.assertRaisesRegex(ROMError, 'CE load body'):
                    ROMInventory(self.write(data))
        data = fixture()
        struct.pack_into('<I', data, 0x844, 0x8C010FAC)
        struct.pack_into('<21I', data, 0x17AC,
                         0, 0, 0x8C010000, 0x8C011000, 1,
                         0x8C011000, 0x8C011000, 0x8D000000,
                         0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0)
        with self.assertRaisesRegex(ROMError, 'CE load body'):
            ROMInventory(self.write(data))

    def test_compressed_section_can_be_hashed_but_not_read_as_rva(self):
        rom = ROMInventory(self.write(fixture(compressed=True)))
        module = rom.modules['sndcore.dll']
        self.assertTrue(module['sections'][0]['compressed'])
        self.assertEqual(len(module['sections'][0]['stored_sha256']), 64)
        with self.assertRaisesRegex(ROMError, 'uncompressed ROM section'):
            rom.rva_read(module, 0x1000, 1)

    def test_same_payload_groups_across_physical_and_nominal_placement(self):
        first = self.write(fixture(), 'first.bin')
        second = self.write(fixture(base=0x8C020000, nominal=0x02000000,
                                    payload_offset=0x500), 'second.bin')
        result = inventory([first, second])
        self.assertNotEqual(result['kernels'][0]['sha256'], result['kernels'][1]['sha256'])
        modules = [kernel['modules'][0] for kernel in result['kernels']]
        self.assertNotEqual(modules[0]['nominal_virtual_base'], modules[1]['nominal_virtual_base'])
        self.assertNotEqual(modules[0]['sections'][0]['data_address'],
                            modules[1]['sections'][0]['data_address'])
        self.assertEqual(len(result['component_groups']), 1)
        self.assertEqual(result['component_groups'][0]['kernel_indices'], [0, 1])
        self.assertEqual(len(result['section_groups']), 1)
        self.assertEqual(result['section_groups'][0]['kernel_indices'], [0, 1])
        self.assertEqual(len(result['rom_file_groups']), 1)
        self.assertEqual(result['rom_file_groups'][0]['kernel_indices'], [0, 1])

    def test_changed_payload_layout_or_compression_separates_component_groups(self):
        changed_layout = fixture()
        struct.pack_into('<I', changed_layout, 0xB00, 0x180)
        paths = [self.write(fixture(), 'original.bin'),
                 self.write(fixture(payload=b'synthetic sound module!'), 'changed.bin'),
                 self.write(fixture(compressed=True), 'compressed.bin'),
                 self.write(changed_layout, 'layout.bin')]
        result = inventory(paths)
        self.assertEqual(len(result['component_groups']), 4)
        self.assertEqual(sorted(group['kernel_indices'] for group in result['component_groups']),
                         [[0], [1], [2], [3]])
        self.assertEqual(len(result['section_groups']), 4)
        self.assertEqual(result['rom_file_groups'][0]['kernel_indices'], [0, 1, 2, 3])

    def test_cli_success_is_complete_metadata_json(self):
        path = self.write(fixture())
        result = self.cli(path)
        self.assertEqual(result.returncode, 0, result.stderr)
        metadata = json.loads(result.stdout)
        self.assertEqual(metadata['format'], 'kui-wince-runtime-inventory-v1')
        self.assertFalse(metadata['playback_admitted'])
        self.assertEqual(len(metadata['kernels']), 1)
        self.assertEqual(result.stderr, '')

    def test_cli_failure_never_emits_partial_json(self):
        valid = self.write(fixture(), 'valid.bin')
        bad = self.write(b'not a CE image', 'invalid.bin')
        for paths in ((valid, bad), (bad, valid), (valid, self.directory / 'missing.bin')):
            with self.subTest(paths=paths):
                result = self.cli(*paths)
                self.assertNotEqual(result.returncode, 0)
                self.assertEqual(result.stdout, '')
                self.assertIn('Inventory failed:', result.stderr)
                self.assertNotIn('Traceback', result.stderr)

    def test_table_and_name_limits(self):
        mutations = ((0x900 + 4 * 4, 65, 'bounded ROM header'),
                     (0x900 + 8 * 4, 65, 'bounded ROM header'),
                     (0x900 + 12 * 4, 65, 'bounded ROM header'),
                     (0xB00 + 4, 0x3001, 'module virtual range'))
        for field, value, diagnostic in mutations:
            with self.subTest(field=hex(field)):
                data = fixture()
                struct.pack_into('<I', data, field, value)
                with self.assertRaisesRegex(ROMError, diagnostic):
                    ROMInventory(self.write(data))
        data = fixture()
        struct.pack_into('<H', data, 0xA40, 17)
        with self.assertRaisesRegex(ROMError, 'section count'):
            ROMInventory(self.write(data))
        data = fixture()
        struct.pack_into('<I', data, 0x954 + 16, 0x8C010600)
        data[0xE00:0xF00] = b'A' * 256
        with self.assertRaisesRegex(ROMError, 'bounded string length'):
            ROMInventory(self.write(data))

    def test_rom_size_limit_is_enforced_before_parsing(self):
        path = self.directory / 'oversized.bin'
        with path.open('wb') as source:
            source.truncate(MAX_ROM_BYTES + 1)
        with self.assertRaisesRegex(ROMError, '16 MiB inventory limit'):
            ROMInventory(path)

    def test_kernel_count_limits_are_fail_closed(self):
        path = self.write(fixture())
        for paths in ([], [path] * (MAX_KERNELS + 1)):
            with self.subTest(count=len(paths)), self.assertRaisesRegex(ValueError, '1 and 128'):
                inventory(paths)
        result = inventory([path] * MAX_KERNELS)
        self.assertEqual(len(result['kernels']), MAX_KERNELS)
        self.assertEqual(result['component_groups'][0]['kernel_indices'], list(range(MAX_KERNELS)))
        rejected = self.cli(*([path] * (MAX_KERNELS + 1)))
        self.assertNotEqual(rejected.returncode, 0)
        self.assertEqual(rejected.stdout, '')
        self.assertIn('1 and 128', rejected.stderr)

    def cli(self, *paths):
        return subprocess.run([sys.executable, str(ROOT / 'tools/wince_runtime_inventory.py'),
                               *map(str, paths)], text=True, capture_output=True)


if __name__ == '__main__':
    unittest.main()
