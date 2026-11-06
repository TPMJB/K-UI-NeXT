#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Guard complete global role coverage and exact linked module binding."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from global_loader_rollout import DEFAULT_BUILDS, INSTALL_TARGETS, collect, linked_module, write_rollout
from test_image_loader_layout import executable
from runtime_package import ADDRESS, verify


class GlobalRollout(unittest.TestCase):
    @staticmethod
    def collected_files():
        files = {name: ('built:' + name).encode() for name in INSTALL_TARGETS}
        report = dict(schema='kui.global-loader-rollout.v1', status='PASS',
            installed_files=sorted(INSTALL_TARGETS), installed_sha256={
                name: hashlib.sha256(body).hexdigest() for name, body in files.items()})
        files['evidence/global-loader-rollout.json'] = json.dumps(report).encode()
        return files

    def test_fresh_atomic_output_matches_every_reported_file(self):
        files = self.collected_files()
        with tempfile.TemporaryDirectory() as temporary:
            for precreated in (False, True):
                output = Path(temporary) / str(precreated)
                if precreated:output.mkdir()
                write_rollout(files, output)
                actual = {p.relative_to(output).as_posix(): p.read_bytes()
                          for p in output.rglob('*') if p.is_file()}
                self.assertEqual(actual, files)
            self.assertEqual(sorted(p.name for p in Path(temporary).iterdir()), ['False', 'True'])

    def test_existing_software_or_user_data_is_refused_without_modification(self):
        files = self.collected_files()
        with tempfile.TemporaryDirectory() as temporary:
            for name in ('KUI/apps/games/retail-boot.kui', 'KUI/apps/games/smash-card-session.bin',
                         next(iter(INSTALL_TARGETS)), 'evidence/stale.json'):
                with self.subTest(existing=name):
                    output = Path(temporary) / 'output'
                    old = output / name
                    old.parent.mkdir(parents=True)
                    old.write_bytes(b'preserve me')
                    with self.assertRaisesRegex(ValueError, 'new or empty'):
                        write_rollout(files, output)
                    self.assertEqual(old.read_bytes(), b'preserve me')
                    self.assertEqual([p for p in output.rglob('*') if p.is_file()], [old])
                    import shutil
                    shutil.rmtree(output)

    def test_output_symlink_and_report_byte_disagreement_refuse(self):
        files = self.collected_files()
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            real = root / 'real'
            real.mkdir()
            alias = root / 'alias'
            alias.symlink_to(real, target_is_directory=True)
            with self.assertRaisesRegex(ValueError, 'symlink'):
                write_rollout(files, alias)
            self.assertFalse(list(real.iterdir()))
            files[next(iter(INSTALL_TARGETS))] += b'changed after qualification'
            with self.assertRaisesRegex(ValueError, 'actual installation bytes'):
                write_rollout(files, root / 'output')
            self.assertFalse((root / 'output').exists())

    def test_unsafe_member_path_cannot_escape_the_collection(self):
        with tempfile.TemporaryDirectory() as temporary:
            for name in ('../escape', '/absolute', 'evidence/../escape',
                         'KUI/apps//bad.kui', 'KUI\\apps\\bad.kui', '.'):
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, 'Unsafe'):
                        write_rollout({**self.collected_files(), name: b'bad'}, Path(temporary) / 'output')
            self.assertEqual(list(Path(temporary).iterdir()), [])

    def test_complete_twelve_role_fifteen_file_install_contract(self):
        self.assertEqual(len(DEFAULT_BUILDS), 12)
        self.assertEqual(len(INSTALL_TARGETS), 15)
        for name in ('cdda-toy-raw', 'cdda-toy-cooked', 'retail-cdda-mdk2',
                     'cdda-native-toy-raw', 'cdda-native-toy-cooked', 'cdda-native-mdk2'):
            self.assertIn('KUI/apps/games/' + name + '.kui', INSTALL_TARGETS)
        self.assertNotIn('KUI/apps/games/retail-boot.kui', INSTALL_TARGETS)
        self.assertFalse(any('silver' in name or 'vmu-toy' in name for name in INSTALL_TARGETS))
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            collect({k:v for k,v in DEFAULT_BUILDS.items() if k != 'ce'}, '1'*40)
        with self.assertRaisesRegex(ValueError, 'Incomplete'):
            collect({**DEFAULT_BUILDS, 'old_foreground':Path('irrelevant')}, '1'*40)

    def test_exact_entry_stage_and_clean_receipt_required(self):
        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            commit = '0123456789abcdef0123456789abcdef01234567'
            stage = b'\x09\0\x09\0' * 8
            (directory / 'stage.bin').write_bytes(stage)
            (directory / 'stage.elf').write_bytes(executable(0x8ce00000, stage, len(stage), {}))
            entry = directory / 'entry.elf'
            entry.write_bytes(executable(ADDRESS, b'\x09\0\x09\0' * 2048 + stage, 0x2000 + len(stage), {}))
            receipt = dict(commit=commit, source_dirty=False,
                           elf_sha256=hashlib.sha256(entry.read_bytes()).hexdigest())
            path = entry.with_suffix('.compile.json')
            path.write_text(json.dumps(receipt))
            data, record = linked_module(directory, commit, False)
            self.assertEqual(verify(data)['build'], commit[:12])
            self.assertEqual(record['stage_bin_sha256'], hashlib.sha256(stage).hexdigest())
            for delta in (dict(commit='f'*40), dict(source_dirty=True), dict(elf_sha256='0'*64)):
                path.write_text(json.dumps({**receipt, **delta}))
                with self.assertRaises(ValueError):linked_module(directory, commit, False)
            path.write_text(json.dumps(receipt))
            (directory / 'stage.bin').write_bytes(stage[:-1] + b'\x01')
            with self.assertRaisesRegex(ValueError, 'exact stage'):linked_module(directory, commit, False)
            (directory / 'stage.bin').write_bytes(stage)
            (directory / 'stage.elf').write_bytes(executable(0x8ce00000, stage[:-1] + b'\x01', len(stage), {}))
            with self.assertRaisesRegex(ValueError, 'differs from its ELF'):linked_module(directory, commit, False)


if __name__ == '__main__':unittest.main()
