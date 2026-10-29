#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Whole-folder startup bundling and the sweep's part reader, on original
synthetic GDIs: native, duplicate, Windows CE, CD audio, broken and CDI."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

from games_fixture import RAW, SESSION, make_fixture, raw_sector

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('library_startup_bundle',
                                              ROOT / 'tools/kui-library-startup-bundle.py')
library = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(library)
sys.path[:0] = [str(ROOT / 'tools')]
import native_library_sweep as sweep  # noqa: E402


def patch(folder, sector, offset, content):
    with (folder / 'track03.bin').open('r+b') as track:
        track.seek(sector * RAW + 16 + offset)
        track.write(content)


class LibraryStartupBundleTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        self.games = self.folder / 'Games'
        make_fixture(self.games / 'Alpha', title=b'Alpha Fixture')
        make_fixture(self.games / 'Alpha copy', title=b'Alpha Fixture')
        patch(self.games / 'Alpha copy', 0, 32, b'0000 GD-ROM2/2  ')
        make_fixture(self.games / 'Beta', title=b'Beta Fixture')
        patch(self.games / 'Beta', 0, 64, b'BETA-0001 ')
        patch(self.games / 'Beta', 21, 0, b'BETA')
        make_fixture(self.games / 'Gamma CE', title=b'Gamma Windows CE Fixture')
        patch(self.games / 'Gamma CE', 0, 56, b'0000001')
        make_fixture(self.games / 'Delta CDDA', title=b'Delta Audio Fixture')
        delta = self.games / 'Delta CDDA'
        patch(delta, 21, 0, b'DELTA')
        (delta / 'track04.raw').write_bytes(bytes(4 * RAW))
        (delta / 'track05.bin').write_bytes(b''.join(raw_sector(SESSION + 80 + i, bytes(2048))
                                                     for i in range(2)))
        (delta / 'disc.gdi').write_text(
            '5\n1 0 4 2352 track01.bin 0\n2 4 0 2352 track02.raw 0\n'
            '3 45000 4 2352 track03.bin 0\n4 45070 0 2352 track04.raw 0\n'
            '5 45080 4 2352 track05.bin 0\n', encoding='ascii')
        (self.games / 'Broken').mkdir()
        (self.games / 'Broken' / 'disc.gdi').write_text('not a gdi\n')
        (self.games / 'Broken' / '._disc.gdi').write_bytes(b'\0\5\26\7')
        (self.games / 'Epsilon').mkdir()
        (self.games / 'Epsilon' / 'game.cdi').write_bytes(b'')
        self.output = self.folder / 'out'
        self.output.mkdir()

    def hashes(self):
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in self.games.rglob('*') if path.is_file()}

    def build(self, **options):
        settings = dict(name='Library-startup', part_mb=0, include_cdda=False)
        settings.update(options)
        return library.build(self.games, self.output, log=lambda line: None, **settings)

    def test_rows_and_single_part(self):
        before = self.hashes()
        rows, created = self.build()
        self.assertEqual(self.hashes(), before, 'source tracks are read only')
        status = {row['source']: row['status'] for row in rows}
        self.assertEqual(status, {
            'Alpha/disc.gdi': 'bundled', 'Alpha copy/disc.gdi': 'duplicate',
            'Beta/disc.gdi': 'bundled', 'Broken/disc.gdi': 'error',
            'Delta CDDA/disc.gdi': 'cdda', 'Gamma CE/disc.gdi': 'windows_ce',
            'Epsilon/game.cdi': 'other_format'})
        by = {row['source']: row for row in rows}
        self.assertEqual(by['Alpha copy/disc.gdi']['duplicate_of'], by['Alpha/disc.gdi']['slug'])
        self.assertEqual(by['Gamma CE/disc.gdi']['title'], 'Gamma Windows CE Fixture')
        self.assertEqual(by['Delta CDDA/disc.gdi']['cdda_tracks'], 1)
        self.assertEqual(by['Beta/disc.gdi']['slug'], '003-BETA-0001-Beta-Fixture')
        self.assertEqual([path.name for path in created], ['Library-startup-01.zip'])
        self.assertEqual(list(self.output.iterdir()), created, 'no staging files remain')
        with zipfile.ZipFile(created[0]) as archive:
            self.assertEqual(archive.namelist(), ['library.json', 'games/001-KUI-TEST-Alpha-Fixture.zip',
                                                  'games/003-BETA-0001-Beta-Fixture.zip'])
            info = json.loads(archive.read('library.json'))
            inner = archive.read('games/001-KUI-TEST-Alpha-Fixture.zip')
        self.assertEqual((info['schema'], info['part'], info['parts']), (library.SCHEMA, 1, 1))
        self.assertEqual(info['games'], rows)
        with zipfile.ZipFile(io.BytesIO(inner)) as game:
            self.assertEqual(game.namelist(), ['IP.BIN', 'disc.gdi', '1ST_READ.BIN', 'startup-info.json'])
            boot = game.read('1ST_READ.BIN')
        self.assertEqual(boot, bytes((i * 17 + 3) & 255 for i in range(2048)) +
                         bytes((i * 29 + 7) & 255 for i in range(2048)))
        self.assertEqual(hashlib.sha256(boot).hexdigest(), by['Alpha/disc.gdi']['boot_sha256'])
        with self.assertRaisesRegex(ValueError, 'already holds'):
            self.build()

    def test_cdda_included_on_request_and_parts_split(self):
        rows, created = self.build(include_cdda=True, part_mb=0.001)
        bundled = [row for row in rows if row['status'] == 'bundled']
        self.assertEqual(len(bundled), 3)
        self.assertEqual(len(created), 3, 'each game exceeds the tiny part limit alone')
        self.assertEqual(sorted(row['part'] for row in bundled), [1, 2, 3])
        for number, path in enumerate(created, 1):
            with zipfile.ZipFile(path) as archive:
                info = json.loads(archive.read('library.json'))
                self.assertEqual((info['part'], info['parts'], info['games']), (number, 3, rows))
                self.assertEqual(len(archive.namelist()), 2)

    def test_plan_parts_packs_in_order(self):
        rows = [dict(status='bundled', archive_bytes=size) for size in (4, 4, 3, 9, 1)]
        parts = library.plan_parts(rows + [dict(status='error')], 8)
        self.assertEqual([[row['archive_bytes'] for row in part] for part in parts], [[4, 4], [3], [9], [1]])

    def test_sweep_reads_parts_and_refuses_tampering(self):
        rows, created = self.build(include_cdda=True, part_mb=0.001)
        info, merged, blobs, missing = sweep.read_parts(created)
        self.assertEqual((merged, missing), (rows, []))
        boots = {row['slug']: sweep.open_game(blobs[row['archive']], row)
                 for row in merged if row['status'] == 'bundled'}
        self.assertEqual({slug: hashlib.sha256(data).hexdigest() for slug, data in boots.items()},
                         {row['slug']: row['boot_sha256'] for row in rows if row['status'] == 'bundled'})
        info, merged, blobs, missing = sweep.read_parts(created[:2])
        self.assertEqual(missing, [3])
        with self.assertRaisesRegex(sweep.SweepError, 'given twice'):
            sweep.read_parts([created[0], created[0]])
        evil = self.folder / 'evil.zip'
        with zipfile.ZipFile(created[0]) as source, zipfile.ZipFile(evil, 'w') as target:
            for name in source.namelist():
                target.writestr(name, source.read(name))
            target.writestr('../escape.zip', b'')
        with self.assertRaisesRegex(sweep.SweepError, 'unexpected entry'):
            sweep.read_parts([evil])
        row = next(row for row in merged if row['status'] == 'bundled')
        changed = io.BytesIO()
        with zipfile.ZipFile(io.BytesIO(blobs[row['archive']])) as source, \
                zipfile.ZipFile(changed, 'w') as target:
            for name in source.namelist():
                data = source.read(name)
                target.writestr(name, data[:-1] + bytes([data[-1] ^ 1]) if name == row['boot_path'] else data)
        with self.assertRaisesRegex(sweep.SweepError, 'differ from its hashes'):
            sweep.open_game(changed.getvalue(), row)

    def test_sweep_classifies_every_row_of_a_mixed_library(self):
        rows, created = self.build()
        known = self.folder / 'known.json'
        known.write_text(json.dumps({'games': []}))
        output, markdown = self.folder / 'sweep.json', self.folder / 'sweep.md'
        report = sweep.sweep(created, self.folder / 'work', 0x8ce8d288, output, markdown, known=known)
        self.assertEqual(json.loads(output.read_text()), report)
        self.assertEqual([row['source'] for row in report['results']], [row['source'] for row in rows])
        for row in report['results']:
            if row['status'] == 'bundled':
                self.assertEqual(row['class'], 'blocked', 'synthetic boot files resolve no launch record')
                self.assertIn(row['first_refusal'], sweep.GATE_NOTES)
            else:
                self.assertEqual(row['class'], 'not_native', row['source'])
        self.assertEqual(report['counts'], {'blocked': 2, 'not_native': 5})
        self.assertEqual(len(markdown.read_text().splitlines()), len(rows) + 2)

    def test_verdicts_follow_launcher_order(self):
        base = dict(status='bundled')
        cases = [
            (dict(structural_384k_models_passed=True, combined_resolution=True, sdk_sci_family=1), 'vmu_and_new'),
            (dict(combined_resolution=False, first_refusal='heap', sdk_sci_family=1), 'new_only'),
            (dict(combined_resolution=False, first_refusal='heap', sdk_sci_family=0), 'blocked'),
            (dict(combined_resolution=True, first_refusal='placement_384k', sdk_sci_family=1), 'blocked'),
            (dict(structural_384k_models_passed=True, combined_resolution=True, sdk_sci_family=0), 'blocked'),
        ]
        for fields, expected in cases:
            self.assertEqual(sweep.verdict({**base, **fields})[1], expected, fields)
        self.assertEqual(sweep.verdict(dict(status='windows_ce'))[1], 'not_native')


if __name__ == '__main__':
    unittest.main()
