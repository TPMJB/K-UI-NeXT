#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check the private owner identity and independently derived probe profile."""
import argparse
from pathlib import Path
import re
import shutil
import struct
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from armada_ce_maple_audit import IDENTITIES, audit, check_probe_profile, check_proof_profile
from armada_ce_rom_audit import ROM
from armada_pe_audio_audit import PE


class OwnerTests(unittest.TestCase):
    def test_complete_rom_and_external_text_match(self):
        report = audit(OWNER)
        self.assertTrue(report['probe_profile']['matched'])
        self.assertEqual([r['text_fixups'] for r in report['external_text_equivalence']], [368, 1166])
        self.assertTrue(all(r['complete_stored_text_equal'] for r in report['external_text_equivalence']))
        self.assertEqual(report['memory_conflict']['overlap_bytes'], 0x30000)
        self.assertEqual(report['transport']['trigger_select'], 1)
        self.assertNotEqual(report['transport']['main_write_register'], report['transport']['mdst_register'])

    def test_each_changed_input_refuses(self):
        with tempfile.TemporaryDirectory() as name:
            directory = Path(name)
            for relative in IDENTITIES:
                target = directory / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(OWNER / relative, target)
            for relative in IDENTITIES:
                with self.subTest(relative=relative):
                    target = directory / relative
                    original = target.read_bytes()
                    changed = bytearray(original)
                    changed[len(changed) // 2] ^= 1
                    target.write_bytes(changed)
                    with self.assertRaisesRegex(ValueError, 'SHA256'):
                        audit(directory)
                    target.write_bytes(original)

    def test_each_incorrect_production_identity_constant_refuses(self):
        report = audit(OWNER)
        original = (ROOT / 'include/kui/armada_ce_maple_observe.h').read_text()
        with tempfile.TemporaryDirectory() as name:
            header = Path(name) / 'probe.h'
            for symbol, value in report['probe_profile']['constants'].items():
                with self.subTest(symbol=symbol):
                    pattern = r'(^#define\s+' + re.escape(symbol) + r'\s+UINT32_C\()0x[0-9a-fA-F]+(\))'
                    changed, count = re.subn(pattern, lambda m: m[1] + f'0x{value + 4:x}' + m[2], original, flags=re.M)
                    self.assertEqual(count, 1, symbol)
                    header.write_text(changed)
                    with self.assertRaisesRegex(ValueError, 'Probe constant differs'):
                        check_probe_profile(report['modules'], header)

    def test_proof_fingerprint_and_virtual_data_geometry(self):
        proof = audit(OWNER)['proof_profile']
        self.assertTrue(proof['matched'])
        self.assertEqual(proof['constants'], {
            'KUI_ARMADA_CE_MAPLE_PROOF_CODE_RVA': 0x5b0c,
            'KUI_ARMADA_CE_MAPLE_PROOF_CODE_BYTES': 32,
            'KUI_ARMADA_CE_MAPLE_PROOF_CODE_CRC': 0x7bd7ff55,
            'KUI_ARMADA_CE_MAPLE_PROOF_ROOT_RVA': 0x72b8,
        })
        self.assertEqual(proof['code']['crc32'], 0x7bd7ff55)
        self.assertEqual(proof['code']['sha256'],
                         '394934ea008c9f96b8655aa9d23bfa1e45850c550485cfb02c8ff7fde819180c')
        self.assertEqual(proof['code']['highlow_overlap_count'], 0)
        self.assertTrue(proof['code']['exact_rom_external_bytes_equal'])
        self.assertEqual(proof['root']['reviewed_literal_rva'], 0x1604)
        self.assertEqual(proof['root']['writable_data_rva'], 0x7000)
        self.assertEqual(proof['root']['writable_data_virtual_bytes'], 0x2c0)
        # The global resides in the virtual zero-filled tail, not PE raw data.
        self.assertFalse(proof['root']['pe_file_backed'])

    def test_each_incorrect_production_proof_constant_refuses(self):
        rom, pe = ROM(OWNER / '0WINCEOS.BIN'), PE(OWNER / 'WINCE/MAPLE.DLL')
        proof = check_proof_profile(rom, pe)
        original = (ROOT / 'include/kui/armada_ce_maple_proof.h').read_text()
        with tempfile.TemporaryDirectory() as name:
            header = Path(name) / 'proof.h'
            for symbol, value in proof['constants'].items():
                with self.subTest(symbol=symbol):
                    pattern = r'(^#define\s+' + re.escape(symbol) + r'\s+)[^\n]+'
                    changed, count = re.subn(pattern, lambda m: m[1] + f'UINT32_C(0x{value + 4:x})',
                                             original, flags=re.M)
                    self.assertEqual(count, 1, symbol)
                    header.write_text(changed)
                    with self.assertRaisesRegex(ValueError, 'Proof constant differs'):
                        check_proof_profile(rom, pe, header)
                    # Missing and duplicate declarations cannot silently choose
                    # another accepted value either.
                    header.write_text(re.sub(pattern, '', original, flags=re.M))
                    with self.assertRaisesRegex(ValueError, 'Proof constant differs'):
                        check_proof_profile(rom, pe, header)
                    header.write_text(original + f'\n#define {symbol} UINT32_C(0x{value:x})\n')
                    with self.assertRaisesRegex(ValueError, 'Proof constant differs'):
                        check_proof_profile(rom, pe, header)

    def test_proof_highlow_overlap_refuses_including_boundary_straddles(self):
        rom = ROM(OWNER / '0WINCEOS.BIN')
        for target in (0x5b0a, 0x5b0c, 0x5b2a):
            with self.subTest(target=hex(target)):
                pe = PE(OWNER / 'WINCE/MAPLE.DLL')
                rva, size = pe.directories[5]
                reloc_offset = pe.rva_offset(rva, size)
                changed, cursor, replaced = bytearray(pe.data), 0, False
                while cursor < size:
                    page, block = struct.unpack_from('<II', changed, reloc_offset + cursor)
                    if page == 0x5000:
                        for at in range(cursor + 8, cursor + block, 2):
                            entry_offset = reloc_offset + at
                            entry = struct.unpack_from('<H', changed, entry_offset)[0]
                            # Replace the adjacent real literal fixup so the
                            # straddling case has no separate relocation
                            # overlapping it before the proof guard runs.
                            if entry >> 12 and page + (entry & 0xfff) == 0x5b08:
                                struct.pack_into('<H', changed, entry_offset, 0x3000 | (target - page))
                                replaced = True
                                break
                        break
                    cursor += block
                self.assertTrue(replaced)
                pe.data = bytes(changed)
                with self.assertRaisesRegex(ValueError, 'Proof code overlaps PE HIGHLOW'):
                    check_proof_profile(rom, pe)

    def test_changed_proof_code_root_literal_and_data_range_refuse(self):
        rom = ROM(OWNER / '0WINCEOS.BIN')
        for rva, error in ((0x5b0c, 'Proof fingerprint bytes differ'),
                           (0x1604, 'Proof root literal differs')):
            with self.subTest(rva=hex(rva)):
                pe = PE(OWNER / 'WINCE/MAPLE.DLL')
                changed = bytearray(pe.data)
                changed[pe.rva_offset(rva)] ^= 4
                pe.data = bytes(changed)
                with self.assertRaisesRegex(ValueError, error):
                    check_proof_profile(rom, pe)
        pe = PE(OWNER / 'WINCE/MAPLE.DLL')
        next(s for s in pe.sections if s['name'] == '.data')['virtual_bytes'] = 0x2b8
        with self.assertRaisesRegex(ValueError, 'matching writable virtual data'):
            check_proof_profile(rom, pe)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--startup-dir', required=True, type=Path)
    args, remaining = parser.parse_known_args()
    OWNER = args.startup_dir
    unittest.main(argv=[sys.argv[0]] + remaining)
