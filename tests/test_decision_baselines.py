#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent mutation checks for exact runtime-baseline comparison."""
import sys
import json
import hashlib
from pathlib import Path
import tempfile
import unittest
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from check_decision_baselines import compare_bytes, normalized, compile_receipt


class BaselineComparison(unittest.TestCase):
    old = '0045dd96937e'
    new = '0123456789ab'

    def test_only_display_label_is_normalized(self):
        original = b'\x09\x00BUILD ' + self.old.encode() + b'\0\x0b\x00'
        candidate = original.replace(self.old.encode(), self.new.encode())
        proof = compare_bytes('linked instructions', original, candidate, self.old, self.new)
        self.assertEqual(proof['build_labels'], 1)
        for offset in (0, 1, len(candidate)-2, len(candidate)-1):
            changed = bytearray(candidate)
            changed[offset] ^= 1
            with self.assertRaises(ValueError):
                compare_bytes('instruction mutation', original, bytes(changed), self.old, self.new)

    def test_embedded_images_and_size_are_compared(self):
        original = b'BUILD ' + self.old.encode() + b'\0BODYBUILD ' + self.old.encode() + b'\0'
        candidate = original.replace(self.old.encode(), self.new.encode())
        self.assertEqual(compare_bytes('embedded', original, candidate, self.old, self.new)['build_labels'], 2)
        for changed in (candidate + b'\0', candidate.replace(b'BODY', b'B0DY'), candidate[:-1]):
            with self.assertRaises(ValueError):
                compare_bytes('embedded mutation', original, changed, self.old, self.new)

    def test_unlabelled_or_unterminated_stamp_is_rejected(self):
        for data in (self.old.encode(), b'BUILD ' + self.old.encode() + b'X',
                     b'BUILD ' + self.old.encode() + b'\0' + self.old.encode()):
            with self.assertRaises(ValueError):
                normalized(data, self.old)
        with self.assertRaises(ValueError):
            normalized(b'plain', 'LOCAL')

    def test_compile_receipt_binds_clean_source_and_exact_artifact(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            artifact = b'independently authored artifact bytes'
            (directory / 'entry.elf').write_bytes(artifact)
            expected = dict(commit='a' * 40, source_dirty=False,
                            elf_sha256=hashlib.sha256(artifact).hexdigest())
            receipt = directory / 'entry.compile.json'
            receipt.write_text(json.dumps(expected))
            self.assertEqual(compile_receipt(directory, 'a' * 40), expected)
            for field, value in (('commit', 'b' * 40), ('source_dirty', True),
                                 ('elf_sha256', '0' * 64)):
                receipt.write_text(json.dumps({**expected, field: value}))
                with self.assertRaises(ValueError):
                    compile_receipt(directory, 'a' * 40)
            receipt.write_text(json.dumps(expected))
            (directory / 'entry.elf').write_bytes(artifact + b'changed')
            with self.assertRaises(ValueError):
                compile_receipt(directory, 'a' * 40)


if __name__ == '__main__':
    unittest.main()
