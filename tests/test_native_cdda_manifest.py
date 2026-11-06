#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact CDDA cohort generation and production admission without game data."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
from native_cdda_manifest import PROFILES, generate
from check_native_cdda_compat import expected_descriptor
from runtime_package import ADDRESS, envelope
from test_image_loader_layout import executable


class NativeCDDAManifest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='kui-cdda-manifest-')
        self.addCleanup(self.temporary.cleanup)
        self.work = Path(self.temporary.name)
        self.commit = '0123456789abcdef0123456789abcdef01234567'
        self.builds = {}
        self.audits = {}
        for index, (profile, (_, filename, _, _)) in enumerate(PROFILES.items()):
            payload = bytearray(1024)
            payload[:4] = b'\x09\x00\x09\x00'
            payload[32:160] = expected_descriptor(profile)
            payload[512:525] = self.commit[:12].encode() + b'\0'
            payload[-1] = index + 1
            directory = self.work / ('native-compat-' + profile)
            reference = self.work / ('native-reference-' + profile)
            for path in (directory, reference):
                path.mkdir()
                elf = path / 'entry.elf'
                elf.write_bytes(executable(ADDRESS, payload, len(payload), {}))
                elf.with_suffix('.compile.json').write_text(json.dumps(dict(commit=self.commit,
                    source_dirty=False, elf_sha256=hashlib.sha256(elf.read_bytes()).hexdigest())))
            (directory / filename).write_bytes(envelope(payload, len(payload), self.commit[:12]))
            self.builds[profile] = directory
            self.audits[profile] = dict(reference_directory=str(reference), profile=profile)

    def generate(self):
        with patch('native_cdda_manifest.subprocess.check_output', side_effect=[self.commit, b'']), \
             patch('native_cdda_manifest.check_directory', side_effect=lambda directory, profile:self.audits[profile]):
            return generate(self.builds)

    def test_exact_manifest_and_runtime_admission_with_and_without_generated_gate(self):
        header, report = self.generate()
        self.assertEqual(set(report['profiles']), set(PROFILES))
        self.assertFalse(report['installed_module_self_authorization'])
        self.assertFalse(report['routing_precedence_changed'])
        self.assertEqual(sum(len(r['aliases']) for r in report['profiles'].values()), 6)
        include = self.work / 'manifest.h'
        include.write_text(header)
        for enabled in (False, True):
            program = self.work / ('enabled' if enabled else 'historical-only')
            command = ['cc', '-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                '-fsanitize=address,undefined', '-fno-sanitize-recover=all', '-fno-pie', '-no-pie',
                '-Iinclude', '-I.deps/fatfs/source', '-DKUI_RETAIL_EXTENDED_MAP=1']
            if enabled:command.append('-DKUI_NATIVE_CDDA_MANIFEST="' + str(include) + '"')
            command += ['tests/test_native_cdda_manifest.c', 'src/core/retail_image.c',
                        'src/core/hash.c', 'src/core/data.c', '-o', str(program)]
            subprocess.run(command, cwd=ROOT, check=True, capture_output=True)
            subprocess.run([str(program), *(str(self.builds[p] / PROFILES[p][1]) for p in PROFILES)],
                cwd=ROOT, check=True, capture_output=True,
                env={**os.environ, 'ASAN_OPTIONS':'detect_leaks=0', 'UBSAN_OPTIONS':'halt_on_error=1'})

    def test_stale_dirty_foreign_receipt_and_changed_package_refused(self):
        directory = self.builds['toy-raw']
        receipt = directory / 'entry.compile.json'
        original = receipt.read_bytes()
        for changes in (dict(source_dirty=True), dict(commit='f'*40), dict(elf_sha256='0'*64)):
            record = json.loads(original);record.update(changes);receipt.write_text(json.dumps(record))
            with self.assertRaisesRegex(ValueError, 'source receipt'):self.generate()
        receipt.write_bytes(original)
        path = directory / PROFILES['toy-raw'][1]
        body = bytearray(path.read_bytes());body[-1]^=1
        path.write_bytes(body)
        with self.assertRaisesRegex(ValueError, 'checksum'):self.generate()

    def test_missing_profile_refused(self):
        self.builds.pop('mdk2')
        with self.assertRaisesRegex(ValueError, 'all three'):self.generate()


if __name__ == '__main__':unittest.main()
