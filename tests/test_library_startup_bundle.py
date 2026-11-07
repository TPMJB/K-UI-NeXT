#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Whole-folder startup bundling (v2) and the sweep's part reader, on original
synthetic images: native raw GDI, duplicate, Windows CE, CD audio, cooked
2048-byte GDI, Redump-style CUE, already-sent, broken, CDI and a loose ISO."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
import zipfile

from games_fixture import DATA, RAW, SESSION, make_fixture, raw_sector, record

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


def add_root_records(folder, *records):
    """Append directory records after the fixture's three root entries."""
    used = sum(len(r) for r in (record(b"\0", SESSION + 20, DATA, True), record(b"\1", SESSION + 20, DATA, True),
                                record(b"1ST_READ.BIN;1", SESSION + 21, DATA * 2)))
    patch(folder, 20, used, b''.join(records))


def cook(folder):
    """The 2048-byte ISO-track copy of a fixture, as common converters write it."""
    for raw_name, iso_name in (('track01.bin', 'track01.iso'), ('track03.bin', 'track03.iso')):
        raw = (folder / raw_name).read_bytes()
        (folder / iso_name).write_bytes(b''.join(raw[i + 16:i + 16 + DATA] for i in range(0, len(raw), RAW)))
        (folder / raw_name).unlink()
    (folder / 'disc.gdi').write_text(
        '3\n1 0 4 2048 track01.iso 0\n2 4 0 2352 track02.raw 0\n3 45000 4 2048 track03.iso 0\n',
        encoding='ascii')


def as_cue(folder):
    """A per-track BIN/CUE descriptor for the same fixture, without the GDI."""
    (folder / 'disc.gdi').unlink()
    (folder / 'game.cue').write_text('\n'.join([
        'REM SINGLE-DENSITY AREA', 'FILE "track01.bin" BINARY', '  TRACK 01 MODE1/2352',
        '    INDEX 01 00:00:00', 'FILE "track02.raw" BINARY', '  TRACK 02 AUDIO',
        '    INDEX 01 00:00:00', 'REM HIGH-DENSITY AREA', 'FILE "track03.bin" BINARY',
        '  TRACK 03 MODE1/2352', '    INDEX 01 00:00:00']) + '\n', encoding='ascii')


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
        make_fixture(self.games / 'Zeta-2048', title=b'Zeta Cooked Fixture')
        patch(self.games / 'Zeta-2048', 21, 0, b'ZETA')
        cook(self.games / 'Zeta-2048')
        make_fixture(self.games / 'Eta (USA)', title=b'Eta Cue Fixture')
        patch(self.games / 'Eta (USA)', 21, 0, b'ETA')
        as_cue(self.games / 'Eta (USA)')
        (self.games / 'Broken').mkdir()
        (self.games / 'Broken' / 'disc.gdi').write_text('not a gdi\n')
        (self.games / 'Broken' / '._disc.gdi').write_bytes(b'\0\5\26\7')
        (self.games / 'Epsilon').mkdir()
        (self.games / 'Epsilon' / 'game.cdi').write_bytes(b'')
        (self.games / 'Loose').mkdir()
        (self.games / 'Loose' / 'game.iso').write_bytes(b'')
        (self.games / 'Loose' / 'stray.bin').write_bytes(b'')
        self.output = self.folder / 'out'
        self.output.mkdir()

    def hashes(self):
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in self.games.rglob('*') if path.is_file()}

    def build(self, **options):
        settings = dict(name='Library-startup', part_mb=0)
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
            'Delta CDDA/disc.gdi': 'bundled', 'Eta (USA)/game.cue': 'bundled',
            'Gamma CE/disc.gdi': 'bundled', 'Zeta-2048/disc.gdi': 'bundled',
            'Epsilon/game.cdi': 'other_format', 'Loose/game.iso': 'other_format'})
        by = {row['source']: row for row in rows}
        self.assertEqual(by['Alpha copy/disc.gdi']['duplicate_of'], by['Alpha/disc.gdi']['slug'])
        self.assertEqual(by['Alpha copy/disc.gdi']['disc'], '2/2')
        self.assertEqual(by['Alpha/disc.gdi']['disc'], '1/1')
        self.assertIs(by['Gamma CE/disc.gdi']['windows_ce'], True)
        self.assertIs(by['Alpha/disc.gdi']['windows_ce'], False)
        self.assertEqual(by['Delta CDDA/disc.gdi']['cdda_tracks'], 1)
        self.assertEqual(by['Zeta-2048/disc.gdi']['data_encoding'], 'cooked2048')
        self.assertEqual(by['Eta (USA)/game.cue']['descriptor_format'], 'cue')
        self.assertEqual(by['Beta/disc.gdi']['slug'], '003-BETA-0001-Beta-Fixture')
        self.assertEqual([path.name for path in created], ['Library-startup-01.zip'])
        self.assertEqual(list(self.output.iterdir()), created, 'no staging files remain')
        with zipfile.ZipFile(created[0]) as archive:
            names = archive.namelist()
            info = json.loads(archive.read('library.json'))
            inner = archive.read(by['Alpha/disc.gdi']['archive'])
            cue_inner = archive.read(by['Eta (USA)/game.cue']['archive'])
        self.assertEqual(names[0], 'library.json')
        self.assertEqual(len(names), 1 + sum(row['status'] == 'bundled' for row in rows))
        self.assertEqual((info['schema'], info['part'], info['parts']), (library.SCHEMA, 1, 1))
        self.assertEqual(info['games'], rows)
        with zipfile.ZipFile(io.BytesIO(inner)) as game:
            self.assertEqual(game.namelist(), ['1ST_READ.BIN', 'IP.BIN', 'disc.gdi', 'startup-info.json'])
            boot = game.read('1ST_READ.BIN')
            report = json.loads(game.read('startup-info.json'))
        self.assertEqual(boot, bytes((i * 17 + 3) & 255 for i in range(2048)) +
                         bytes((i * 29 + 7) & 255 for i in range(2048)))
        self.assertEqual(hashlib.sha256(boot).hexdigest(), by['Alpha/disc.gdi']['boot_sha256'])
        self.assertEqual([item['path'] for item in report['inventory']], ['1ST_READ.BIN'])
        self.assertIs(report['audio_payload_copied'], False)
        with zipfile.ZipFile(io.BytesIO(cue_inner)) as game:
            self.assertIn('disc.cue', game.namelist())
        with self.assertRaisesRegex(ValueError, 'already holds'):
            self.build()

    def test_unusual_names_elsewhere_on_the_disc_do_not_stop_extraction(self):
        theta = self.games / 'Theta'
        make_fixture(theta, title=b'Theta Names Fixture')
        patch(theta, 21, 0, b'THETA')
        add_root_records(theta, record(b'README.;1', SESSION + 22, 16), record(b'AUX.DAT;1', SESSION + 22, 16),
                         record(b'CON.DRV;1', SESSION + 22, 16))
        rows, created = self.build()
        row = next(row for row in rows if row['source'] == 'Theta/disc.gdi')
        self.assertEqual(row['status'], 'bundled', row.get('error'))
        with zipfile.ZipFile(created[0]) as archive:
            with zipfile.ZipFile(io.BytesIO(archive.read(row['archive']))) as game:
                self.assertEqual(game.namelist(), ['1ST_READ.BIN', 'IP.BIN', 'disc.gdi', 'startup-info.json'])
                report = json.loads(game.read('startup-info.json'))
        listed = {item['path']: item for item in report['inventory']}
        self.assertIn('README', listed, 'the separator dot of an extensionless name is dropped')
        self.assertTrue(listed['AUX.DAT'].get('unsafe_name'))
        self.assertEqual([(item['path'], item['reason']) for item in report['omitted_files']],
                         [('CON.DRV', 'unsafe_name')])

    def test_requested_program_files_are_copied_even_for_a_sent_boot(self):
        iota = self.games / 'Iota'
        make_fixture(iota, title=b'Iota Loader Fixture')
        patch(iota, 0, 64, b'IOTA-0001 ')
        patch(iota, 21, 0, b'IOTA')
        add_root_records(iota, record(b'GAME.BIN;1', SESSION + 22, 64))
        first, _ = self.build()
        boot = next(row for row in first if row['source'] == 'Iota/disc.gdi')['boot_sha256']
        known = self.folder / 'already-sent.json'
        known.write_text(json.dumps({'startups': [boot]}))
        requests = self.folder / 'requests.json'
        requests.write_text(json.dumps({'requests': [
            {'product': 'IOTA-0001', 'paths': ['GAME.BIN', 'MISSING.BIN']}]}))
        self.output = self.folder / 'out2'
        self.output.mkdir()
        rows, created = self.build(known_path=known, requests_path=requests)
        row = next(row for row in rows if row['source'] == 'Iota/disc.gdi')
        self.assertEqual((row['status'], row['requested_files']), ('bundled', ['GAME.BIN']),
                         'a requested program is new even when the boot file was sent')
        with zipfile.ZipFile(created[0]) as archive:
            with zipfile.ZipFile(io.BytesIO(archive.read(row['archive']))) as game:
                self.assertIn('GAME.BIN', game.namelist())
        known.write_text(json.dumps({'startups': [{'startup_key': row['startup_key']}]}))
        self.output = self.folder / 'out3'
        self.output.mkdir()
        rows, _ = self.build(known_path=known, requests_path=requests)
        self.assertEqual(next(r for r in rows if r['source'] == 'Iota/disc.gdi')['status'], 'already_sent')

    def test_already_sent_startups_are_listed_not_copied(self):
        first, _ = self.build()
        beta = next(row for row in first if row['source'] == 'Beta/disc.gdi')
        known = self.folder / 'already-sent.json'
        gamma = next(row for row in first if row['source'] == 'Gamma CE/disc.gdi')
        known.write_text(json.dumps({'startups': [{'title': 'Beta', 'boot_sha256': beta['boot_sha256']},
                                                  {'title': 'Gamma', 'startup_key': gamma['startup_key']},
                                                  {'title': 'not CE', 'boot_sha256': gamma['boot_sha256']}]}))
        self.output = self.folder / 'out2'
        self.output.mkdir()
        rows, created = self.build(known_path=known)
        row = next(row for row in rows if row['source'] == 'Beta/disc.gdi')
        self.assertEqual(row['status'], 'already_sent')
        self.assertNotIn('archive', row)
        self.assertEqual(row['boot_sha256'], beta['boot_sha256'])
        self.assertEqual(next(row for row in rows if row['source'] == 'Gamma CE/disc.gdi')['status'],
                         'already_sent', 'a CE startup matches by its startup key')
        with zipfile.ZipFile(created[0]) as archive:
            self.assertEqual(json.loads(archive.read('library.json'))['already_sent'], 3)
            self.assertNotIn(beta['archive'], archive.namelist())
        known.write_text(json.dumps({'startups': ['not-a-hash']}))
        self.output = self.folder / 'out3'
        self.output.mkdir()
        with self.assertRaisesRegex(ValueError, 'malformed SHA256'):
            self.build(known_path=known)

    def test_parts_split_and_every_part_lists_every_game(self):
        rows, created = self.build(part_mb=0.001)
        bundled = [row for row in rows if row['status'] == 'bundled']
        self.assertEqual(len(bundled), 6)
        self.assertEqual(len(created), 6, 'each game exceeds the tiny part limit alone')
        self.assertEqual(sorted(row['part'] for row in bundled), [1, 2, 3, 4, 5, 6])
        for number, path in enumerate(created, 1):
            with zipfile.ZipFile(path) as archive:
                info = json.loads(archive.read('library.json'))
                self.assertEqual((info['part'], info['parts'], info['games']), (number, 6, rows))
                self.assertEqual(len(archive.namelist()), 2)

    def test_plan_parts_packs_in_order(self):
        rows = [dict(status='bundled', archive_bytes=size) for size in (4, 4, 3, 9, 1)]
        parts = library.plan_parts(rows + [dict(status='error')], 8)
        self.assertEqual([[row['archive_bytes'] for row in part] for part in parts], [[4, 4], [3], [9], [1]])

    def test_trim_keeps_required_and_bounds_optional_files(self):
        entries = {'IP.BIN': b'i', 'disc.gdi': b'g', '1ST_READ.BIN': b'b', 'SOUND.DRV': b'd' * 10,
                   'BIG.DLL': b'x' * (library.OPTIONAL_BUDGET + 1), 'DATA.LIB': b'l'}
        report = {'required_files': ['1ST_READ.BIN'], 'omitted_files': [],
                  'files': [{'path': name, 'lba': None} for name in entries]}
        kept, report = library.trim_optional(dict(entries), report)
        self.assertEqual(list(kept), ['IP.BIN', 'disc.gdi', '1ST_READ.BIN', 'SOUND.DRV'])
        self.assertEqual([item['path'] for item in report['omitted_files']], ['BIG.DLL', 'DATA.LIB'])
        self.assertEqual([item['path'] for item in report['files']], list(kept))
        self.assertEqual((report['selected_file_count'], report['archive_file_count']), (2, 5))

    def test_sweep_reads_parts_and_refuses_tampering(self):
        rows, created = self.build(part_mb=0.001)
        info, merged, blobs, missing = sweep.read_parts(created)
        self.assertEqual((merged, missing), (rows, []))
        native = [row for row in merged if row['status'] == 'bundled' and not row['windows_ce']]
        boots = {row['slug']: sweep.open_game(blobs[row['archive']], row) for row in native}
        self.assertEqual({slug: hashlib.sha256(data).hexdigest() for slug, data in boots.items()},
                         {row['slug']: row['boot_sha256'] for row in native})
        info, merged, blobs, missing = sweep.read_parts(created[:2])
        self.assertEqual(missing, [3, 4, 5, 6])
        with self.assertRaisesRegex(sweep.SweepError, 'given twice'):
            sweep.read_parts([created[0], created[0]])
        evil = self.folder / 'evil.zip'
        with zipfile.ZipFile(created[0]) as source, zipfile.ZipFile(evil, 'w') as target:
            for name in source.namelist():
                target.writestr(name, source.read(name))
            target.writestr('../escape.zip', b'')
        with self.assertRaisesRegex(sweep.SweepError, 'unexpected entry'):
            sweep.read_parts([evil])
        row = next(row for row in merged if row['status'] == 'bundled' and row['part'] in (1, 2)
                   and not row['windows_ce'])
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
            if row['status'] == 'bundled' and not row['windows_ce']:
                self.assertEqual(row['class'], 'blocked', 'synthetic boot files resolve no launch record')
                self.assertIn(row['first_refusal'], sweep.GATE_NOTES)
            else:
                self.assertEqual(row['class'], 'not_native', row['source'])
        ce = next(row for row in report['results'] if row['source'] == 'Gamma CE/disc.gdi')
        self.assertEqual(ce['now'], 'Windows CE (separate reader)')
        self.assertEqual(report['counts'], {'blocked': 5, 'not_native': 5})
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
        self.assertEqual(sweep.verdict(dict(status='bundled', windows_ce=True))[1], 'not_native')
        self.assertEqual(sweep.verdict(dict(status='already_sent'))[1], 'not_native')


if __name__ == '__main__':
    unittest.main()
