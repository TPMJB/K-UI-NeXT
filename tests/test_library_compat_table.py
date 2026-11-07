#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The library compatibility table: launcher-order route decisions, runs
merged across bundler uploads, console joins and the Markdown layout, on
original synthetic images (no retail bytes)."""
import importlib.util
import json
from pathlib import Path
import sys
import tempfile
import unittest

from games_fixture import DATA, RAW, SESSION, make_fixture, record

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools')]
import library_compat_table as table  # noqa: E402

SPEC = importlib.util.spec_from_file_location('library_startup_bundle',
                                              ROOT / 'tools/kui-library-startup-bundle.py')
library = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(library)


def patch(folder, sector, offset, content):
    with (folder / 'track03.bin').open('r+b') as track:
        track.seek(sector * RAW + 16 + offset)
        track.write(content)


class RouteTests(unittest.TestCase):
    def test_routes_follow_launcher_order(self):
        passed = dict(structural_384k_models_passed=True, driver=1, layout=True, exits=True)
        self.assertEqual(table.routes(passed, True, False), ('virtual VMU', '—', None))
        self.assertEqual(table.routes(passed, False, False), ('refused', 'refused', 'sdk_sci_family'))
        heap = dict(structural_384k_models_passed=False, first_refusal='heap', driver=1, layout=True, exits=True)
        self.assertEqual(table.routes(heap, True, True), ('virtual VMU (fixed arena)', '—', None))
        self.assertEqual(table.routes(heap, True, False), ('refused', 'New, no virtual VMU', 'heap'))
        self.assertEqual(table.routes(heap, False, False), ('refused', 'refused', 'heap'))
        no_layout = dict(heap, layout=False, first_refusal='layout')
        self.assertEqual(table.routes(no_layout, True, True)[0], 'refused', 'an arena needs the Maple layout')

    def test_product_key_drops_version_suffix(self):
        self.assertEqual(table.product_key('T40301N 00'), 'T40301N')
        self.assertEqual(table.product_key(None), '')


class TableTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)
        games = self.folder / 'Games'
        make_fixture(games / 'Alpha', title=b'Alpha Fixture')
        make_fixture(games / 'Beta', title=b'Beta Fixture')
        patch(games / 'Beta', 0, 64, b'BETA-0001 ')
        patch(games / 'Beta', 21, 0, b'BETA')
        make_fixture(games / 'Gamma CE', title=b'Gamma Windows CE Fixture')
        patch(games / 'Gamma CE', 0, 56, b'0000001')
        first = self.folder / 'first'
        first.mkdir()
        rows, self.first = library.build(games, first, 'Library-startup', 0, log=lambda line: None)
        alpha = next(row for row in rows if row['source'] == 'Alpha/disc.gdi')
        known = self.folder / 'already-sent.json'
        known.write_text(json.dumps({'startups': [alpha['boot_sha256']]}))
        second = self.folder / 'second'
        second.mkdir()
        _, self.second = library.build(games, second, 'Library-startup', 0, known_path=known,
                                       log=lambda line: None)
        self.console = self.folder / 'console.json'
        self.console.write_text(json.dumps({'games': [
            {'product': 'BETA-0001', 'title': 'Beta', 'route': 'standalone New', 'result': 'works',
             'build': '', 'source': 'test', 'known_blocker': 'test limit'}]}))

    def test_runs_merge_and_every_row_is_classified(self):
        report = table.build(self.first + self.second, self.folder / 'work', 0x8cebe748, self.console)
        self.assertEqual(len(report['runs']), 2)
        checked = [g for g in report['games'] if g['class'] in ('background_vmu', 'new_only', 'blocked')]
        self.assertEqual(sorted(g['title'] for g in checked), ['Alpha Fixture', 'Beta Fixture'],
                         'each startup is checked once; repeats across runs are duplicates')
        for g in checked:
            self.assertEqual(g['class'], 'blocked', 'synthetic boot files resolve no launch record')
            self.assertIsNotNone(g['blocker'])
        repeats = [g for g in report['games'] if g['status'] == 'duplicate']
        self.assertEqual(len(repeats), 3, 'Alpha, Beta and the CE game repeat in the second run')
        ce = [g for g in report['games'] if g.get('windows_ce')]
        self.assertEqual([(g['class'], g['status']) for g in ce],
                         [('windows_ce', 'bundled'), ('not_checked', 'duplicate')])
        self.assertEqual((ce[0]['background_vmu'], ce[0]['cd_music']), ('Windows CE reader', '—'))
        beta = next(g for g in checked if g['title'] == 'Beta Fixture')
        self.assertEqual(beta['console']['result'], 'works')
        text = table.markdown(report)
        self.assertIn('known limit: test limit', text)
        self.assertIn('| Beta Fixture | BETA-0001 |', text)
        self.assertNotIn('/', ''.join(g['title'] for g in checked))


class LoaderTests(unittest.TestCase):
    def test_program_files_supersede_a_boot_only_row(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            games = folder / 'Games'
            make_fixture(games / 'Kappa', title=b'Kappa Loader Fixture')
            patch(games / 'Kappa', 0, 64, b'KAPPA-01  ')
            used = sum(len(r) for r in (record(b'\0', SESSION + 20, DATA, True), record(b'\1', SESSION + 20, DATA, True),
                                        record(b'1ST_READ.BIN;1', SESSION + 21, DATA * 2)))
            patch(games / 'Kappa', 20, used, record(b'GAME.BIN;1', SESSION + 22, 2048))
            requests = folder / 'requests.json'
            requests.write_text(json.dumps({'requests': [{'product': 'KAPPA-01', 'paths': ['GAME.BIN']}]}))
            first, second = folder / 'first', folder / 'second'
            first.mkdir()
            second.mkdir()
            _, boot_only = library.build(games, first, 'Library-startup', 0, log=lambda line: None)
            _, with_program = library.build(games, second, 'Library-startup', 0, log=lambda line: None,
                                            requests_path=requests)
            console = folder / 'console.json'
            console.write_text(json.dumps({'games': []}))
            saved = table.REQUESTS
            table.REQUESTS = requests
            try:
                report = table.build(boot_only + with_program, folder / 'work', 0x8cebe748, console)
            finally:
                table.REQUESTS = saved
        rows = [g for g in report['games'] if g.get('title') == 'Kappa Loader Fixture']
        self.assertEqual([(g['status'], g['class']) for g in rows],
                         [('duplicate', 'not_checked'), ('bundled', 'loader')])
        loader = rows[1]
        self.assertEqual(loader['background_vmu'], 'not established (loader)')
        self.assertEqual([program['path'] for program in loader['programs']], ['GAME.BIN'])
        self.assertIn('Its programs: GAME.BIN', table.blocker_text(loader))


if __name__ == '__main__':
    unittest.main()
