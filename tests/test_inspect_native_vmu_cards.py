#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read-only inventory and journal replay agree with the production C store."""
import copy
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'tools'), str(ROOT/'tests')]
import inspect_native_vmu_cards as inventory
from runtime_package import envelope
from vmu_persist_profile import decode_profile, encode_profile, template_profile
from test_vmu_pair_inspect import SOURCES, BLANK


def sha(data):
    return hashlib.sha256(data).hexdigest()


def marker_for(profile, payload, block=199, sequence=73):
    marker = bytearray(512)
    marker[:8] = b'KUIVMUJ1'
    struct.pack_into('<II', marker, 8, 1, 512)
    marker[16:32] = bytes.fromhex(profile['image_id'])
    struct.pack_into('<QIIIIQ', marker, 32, profile['card_sectors'], inventory.mapping_crc(profile),
                     block, zlib.crc32(payload), 1, sequence)
    struct.pack_into('<I', marker, 508, zlib.crc32(marker[:508]))
    return bytes(marker)


def seal(marker):
    marker = bytearray(marker)
    struct.pack_into('<I', marker, 508, zlib.crc32(marker[:508]))
    return bytes(marker)


def add_file(image, *, name=b'UNLABELLED01', short=b'Example save', long=b'Example long description',
             app=b'EXAMPLE', first=199, blocks=1, header=0, slot=0):
    image = bytearray(image)
    chain = [first-index for index in range(blocks)]
    fat = 254*512
    for index, block in enumerate(chain):
        struct.pack_into('<H', image, fat+2*block, chain[index+1] if index+1 < blocks else 0xfffa)
    at = (253-slot//16)*512+(slot%16)*32
    image[at] = 0x33
    struct.pack_into('<H', image, at+2, first)
    image[at+4:at+16] = name.ljust(12, b'\0')[:12]
    struct.pack_into('<HH', image, at+24, blocks, header)
    vms = chain[header]*512
    image[vms:vms+16] = short.ljust(16, b'\0')[:16]
    image[vms+16:vms+48] = long.ljust(32, b'\0')[:32]
    image[vms+48:vms+64] = app.ljust(16, b'\0')[:16]
    struct.pack_into('<HHHHI', image, vms+64, 1, 0, 0, 0x1234, 128)
    return bytes(image)


class CardInventory(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='kui-card-inventory-')
        cls.dir = Path(cls.work.name)
        cls.tool = cls.dir/'production-inspect'
        cc = os.environ.get('CC', 'cc')
        subprocess.run([cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Wpedantic',
                        '-I', str(ROOT/'include'), *[str(ROOT/s) for s in SOURCES], '-o', str(cls.tool)],
                       check=True)
        (cls.dir/'blank.c').write_text(BLANK)
        subprocess.run([cc, '-std=c11', '-I', str(ROOT/'include'), str(cls.dir/'blank.c'),
                        str(ROOT/'src/core/vmu_image.c'), '-o', str(cls.dir/'blank')], check=True)
        subprocess.run([str(cls.dir/'blank'), str(cls.dir/'blank.vmd')], check=True)
        cls.blank = (cls.dir/'blank.vmd').read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='kui-card-scan-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)/'card'
        self.root.mkdir()
        self.pair, self.profile = self.new_pair(1)

    def new_pair(self, number, image=None, journal=None):
        name = f'p{number:04d}'
        pair = self.root/'KUI/vmu-test'/name
        pair.mkdir(parents=True)
        profile = decode_profile(template_profile())
        profile['pair_path'] = '/KUI/vmu-test/'+name
        profile['image_id'] = (bytes([number])*16).hex()
        profile['identity_sha256'] = sha(bytes([number])*16)
        profile['cid'] = (bytes([number+10])*16).hex()
        (pair/'retail-persistent-profile-0001.bin').write_bytes(encode_profile(profile))
        (pair/'identity.bin').write_bytes(bytes.fromhex(profile['image_id']))
        (pair/'card.vmd').write_bytes(self.blank if image is None else image)
        (pair/'card.journal').write_bytes(bytes(1024) if journal is None else journal)
        return pair, profile

    def snapshot(self):
        return {str(path.relative_to(self.root)): path.read_bytes()
                for path in self.root.rglob('*') if path.is_file() and not path.is_symlink()}

    def scan(self):
        before = self.snapshot()
        result = inventory.scan(self.root)
        self.assertEqual(self.snapshot(), before, 'Scanner must not mutate any SD file')
        return result

    def production(self, image, journal, profile=None):
        profile = profile or self.profile
        with tempfile.TemporaryDirectory(prefix='kui-production-pair-') as temporary:
            directory = Path(temporary)
            inputs = {'profile.bin': encode_profile(profile), 'card.vmd': image, 'card.journal': journal}
            for name, data in inputs.items():
                (directory/name).write_bytes(data)
            run = subprocess.run([str(self.tool), str(directory/'profile.bin'), str(directory/'card.vmd'),
                                  str(directory/'card.journal'), str(directory/'effective.vmd')],
                                 text=True, capture_output=True)
            self.assertTrue(run.stdout, run.stderr)
            report = json.loads(run.stdout)
            for name, data in inputs.items():
                self.assertEqual((directory/name).read_bytes(), data)
            effective = (directory/'effective.vmd').read_bytes() if not report['store_open'] else None
            return report, effective

    def compare_production(self, image, journal, profile=None):
        profile = profile or self.profile
        c, expected = self.production(image, journal, profile)
        before = image, journal, copy.deepcopy(profile)
        actual, replay = inventory.replay_journal(profile, image, journal)
        self.assertEqual((image, journal, profile), before)
        self.assertEqual(actual, expected)
        for field in ('store_open', 'journal_sequence', 'replay_writes', 'first_changed_block'):
            self.assertEqual(replay[field], c[field], field)
        return replay

    def test_blank_card_reports_all_profiles_and_free_blocks(self):
        result = self.scan()
        card = result['pairs'][0]
        self.assertEqual((result['pair_count'], result['pairs_with_errors']), (1, 0))
        self.assertEqual(card['effective_inventory']['files'], [])
        self.assertEqual(card['effective_inventory']['free_blocks'], 200)
        self.assertEqual(card['journal']['state'], 'clean_marker')
        self.assertEqual(card['profiles'][0]['identity_matches'], True)
        self.assertEqual(card['selected_profile'], 'KUI/vmu-test/p0001/retail-persistent-profile-0001.bin')
        self.compare_production(self.blank, bytes(1024))

    def test_committed_journal_replays_only_in_memory(self):
        image = add_file(self.blank)
        payload = bytearray(image[199*512:200*512]);payload[16:48] = b'Changed description'.ljust(32, b'\0')
        journal = bytes(payload)+marker_for(self.profile, payload)
        replay = self.compare_production(image, journal)
        self.assertEqual(replay['state'], 'committed_record_replayed_in_memory')
        self.assertEqual(replay['journal_sequence'], 73)
        (self.pair/'card.vmd').write_bytes(image)
        (self.pair/'card.journal').write_bytes(journal)
        card = self.scan()['pairs'][0]
        self.assertEqual(card['base_inventory']['files'][0]['vms_header']['long_description']['text'],
                         'Example long description')
        self.assertEqual(card['effective_inventory']['files'][0]['vms_header']['long_description']['text'],
                         'Changed description')
        self.assertEqual(card['effective_inventory']['free_blocks'], 199)

    def test_already_applied_committed_record_still_replays(self):
        image = add_file(self.blank)
        payload = image[199*512:200*512]
        result = self.compare_production(image, payload+marker_for(self.profile, payload))
        self.assertEqual((result['replay_writes'], result['first_changed_block']), (1, -1))

    def test_torn_marker_ignored_before_identity_or_field_checks(self):
        payload = b'X'*512
        committed = marker_for(self.profile, payload)
        for changes in ((0, 1), (16, 1), (32, 1), (40, 1), (52, 1), (100, 1), (508, 1)):
            with self.subTest(offset=changes[0]):
                marker = bytearray(committed);marker[changes[0]] ^= changes[1]
                result = self.compare_production(self.blank, payload+marker)
                self.assertEqual(result['state'], 'uncommitted_marker_ignored')
                self.assertEqual(result['store_open'], 0)

    def test_crc_valid_marker_field_and_identity_refusals_match_c(self):
        payload = b'X'*512
        original = marker_for(self.profile, payload)
        for at, value, expected in ((0, 1, -4), (8, 2, -4), (12, 0, -4),
                                    (44, 255, -4), (52, 0, -4), (64, 1, -4),
                                    (16, 0, -6), (32, 0, -6), (40, 0, -6), (48, 0, -4)):
            with self.subTest(offset=at):
                marker = bytearray(original)
                if at == 44:
                    struct.pack_into('<I', marker, at, 256)
                elif at == 32:
                    struct.pack_into('<Q', marker, at, value)
                elif at in (8, 12, 52, 40, 48):
                    struct.pack_into('<I', marker, at, value)
                else:
                    marker[at] = value
                result = self.compare_production(self.blank, payload+seal(marker))
                self.assertEqual(result['store_open'], expected)

    def test_payload_crc_mismatch_blocks_effective_inventory(self):
        payload = b'Y'*512
        journal = payload+marker_for(self.profile, b'X'*512)
        self.assertEqual(self.compare_production(self.blank, journal)['store_open'], -4)
        (self.pair/'card.journal').write_bytes(journal)
        card = self.scan()['pairs'][0]
        self.assertEqual(card['journal']['store_result'], 'CORRUPT')
        self.assertNotIn('effective_inventory', card)
        self.assertIn('base_inventory', card)

    def test_zero_and_maximum_sequence_match_c(self):
        payload = bytes(512)
        for sequence in (0, 1, 2**64-1):
            with self.subTest(sequence=sequence):
                self.assertEqual(self.compare_production(self.blank,
                    payload+marker_for(self.profile, payload, block=150, sequence=sequence))['journal_sequence'], sequence)

    def test_fragmented_maps_match_production_mapping_crc(self):
        profile = copy.deepcopy(self.profile)
        file = profile['files'][0]
        first = file['useful'][0]['lba']
        file['useful'] = file['allocated'] = [dict(logical_block=0, lba=first, blocks=127),
                                             dict(logical_block=127, lba=first+500, blocks=129)]
        # The profile codec validates canonical ordered ownership before C opens it.
        profile = decode_profile(encode_profile(profile))
        payload = bytes(512)
        self.assertEqual(self.compare_production(self.blank,
            payload+marker_for(profile, payload, block=199), profile)['store_open'], 0)

    def test_all_eight_distinct_card_inventories_are_reported(self):
        titles = ('Evolution 1', 'Rayman 2', 'Lodoss War', 'Sonic Adventure 2',
                  'Power Stone 2', 'Grandia II', 'Skies of Arcadia', 'Other save')
        for number, title in enumerate(titles, 1):
            image = add_file(self.blank, name=f'SAVE{number:08d}'.encode(), short=f'Card {number}'.encode(),
                             long=title.encode(), app=f'APP-{number}'.encode())
            if number == 1:
                (self.pair/'card.vmd').write_bytes(image)
            else:
                self.new_pair(number, image)
        result = self.scan()
        self.assertEqual((result['pair_count'], result['pairs_with_errors']), (8, 0))
        names = [card['effective_inventory']['files'][0]['vms_header']['long_description']['text']
                 for card in result['pairs']]
        self.assertEqual(names, list(titles))
        self.assertEqual(len(set(card['source_files']['card.vmd']['sha256'] for card in result['pairs'])), 8)
        skies = result['pairs'][6]['effective_inventory']['files'][0]
        self.assertEqual(skies['name'], 'SAVE00000007')
        self.assertEqual(skies['keyword_hints'], [dict(field='long_description', keyword='skies'),
                                                dict(field='long_description', keyword='arcadia')])
        self.assertTrue(result['keyword_hints_are_not_game_identification'])

    def test_all_file_entries_and_header_offsets_are_shown(self):
        image = add_file(self.blank, name=b'PLAINFILE001', blocks=2, header=1, app=b'APP-ONE')
        image = add_file(image, name=b'PLAINFILE002', short=b'Second', first=190, slot=17, app=b'APP-TWO')
        (self.pair/'card.vmd').write_bytes(image)
        files = self.scan()['pairs'][0]['effective_inventory']['files']
        self.assertEqual([file['name'] for file in files], ['PLAINFILE001', 'PLAINFILE002'])
        self.assertEqual(files[0]['vms_header']['image_block'], 198)
        self.assertEqual(files[1]['vms_header']['application_id']['text'], 'APP-TWO')
        self.assertEqual(files[0]['vms_header']['short_description']['raw_hex'],
                         b'Example save'.ljust(16, b'\0').hex())

    def test_bad_pair_does_not_hide_other_cards(self):
        self.new_pair(2, add_file(self.blank, long=b'Second card'))
        (self.pair/'identity.bin').write_bytes(b'bad')
        result = self.scan()
        self.assertEqual(result['pair_count'], 2)
        self.assertEqual(result['pairs_with_errors'], 1)
        self.assertEqual(result['pairs'][1]['effective_inventory']['files'][0]['vms_header']['long_description']['text'], 'Second card')

    def test_symlink_pair_and_profile_are_not_followed(self):
        outside = Path(self.temp.name)/'outside';outside.mkdir()
        (self.root/'KUI/vmu-test/p0002').symlink_to(outside, target_is_directory=True)
        profile = self.pair/'retail-persistent-profile-0001.bin'
        source = outside/'profile.bin';source.write_bytes(profile.read_bytes())
        profile.unlink();profile.symlink_to(source)
        before = source.read_bytes()
        result = self.scan()
        self.assertEqual(result['pairs_with_errors'], 2)
        self.assertIn('symlink', ' '.join(result['pairs'][0]['errors']))
        self.assertIn('symlink', ' '.join(result['pairs'][1]['errors']))
        self.assertEqual(source.read_bytes(), before)

    def test_bad_profile_pair_path_is_not_used(self):
        profile = copy.deepcopy(self.profile)
        profile['pair_path'] = '/KUI/vmu-test/p0002'
        (self.pair/'retail-persistent-profile-0001.bin').write_bytes(encode_profile(profile))
        card = self.scan()['pairs'][0]
        self.assertEqual(card['status'], 'error')
        self.assertIn('actual directory', ' '.join(card['errors']))
        self.assertNotIn('effective_inventory', card)

    def test_repeated_exports_match_same_ownership_and_report_all_paths(self):
        newer = copy.deepcopy(self.profile);newer['base_sha256'] = sha(b'historical snapshot')
        (self.pair/'retail-persistent-profile-0002.bin').write_bytes(encode_profile(newer))
        card = self.scan()['pairs'][0]
        self.assertEqual(card['status'], 'ok')
        self.assertEqual(len(card['profiles']), 2)
        self.assertTrue(card['selected_profile'].endswith('0002.bin'))

    def test_disagreeing_profile_maps_refuse_replay(self):
        newer = copy.deepcopy(self.profile);newer['cid'] = (b'Z'*16).hex()
        (self.pair/'retail-persistent-profile-0002.bin').write_bytes(encode_profile(newer))
        card = self.scan()['pairs'][0]
        self.assertIn('ownership', ' '.join(card['errors']))
        self.assertNotIn('effective_inventory', card)

    def test_installed_modules_bind_only_valid_exact_profiles(self):
        modules = self.root/'KUI/apps/games';modules.mkdir(parents=True)
        raw = (self.pair/'retail-persistent-profile-0001.bin').read_bytes()
        payload = bytes(128)+raw+bytes(64)
        (modules/'retail-boot.kui').write_bytes(envelope(payload, len(payload), '123456789abc'))
        malformed = bytearray(raw);malformed[-1] ^= 1
        payload = bytes(128)+malformed+bytes(64)
        (modules/'retail-native-dual.kui').write_bytes(envelope(payload, len(payload), 'abcdefabcdef'))
        result = self.scan()['installed_modules']
        self.assertEqual(result[0]['runtime']['build'], '123456789abc')
        self.assertEqual(result[0]['embedded_profiles'][0]['exact_profile_paths'],
                         ['KUI/vmu-test/p0001/retail-persistent-profile-0001.bin'])
        self.assertEqual(result[1]['embedded_profiles'], [])

    def test_invalid_effective_filesystem_is_reported_without_hiding_entries(self):
        image = bytearray(add_file(self.blank))
        struct.pack_into('<H', image, 254*512+10*2, 0xfffa)
        (self.pair/'card.vmd').write_bytes(image)
        card = self.scan()['pairs'][0]
        self.assertEqual(card['status'], 'error')
        self.assertEqual(card['effective_inventory']['file_count'], 1)
        self.assertIn('filesystem', ' '.join(card['errors']))

    def test_cli_codecs_on_sd_never_create_bytecode_cache(self):
        package = self.root/'inspection-tool';package.mkdir();(package/'tools').mkdir()
        for name in ('vmu_persist_profile.py', 'explain_vmu_image.py', 'runtime_package.py'):
            shutil.copyfile(ROOT/'tools'/name, package/'tools'/name)
        entry = package/'inspect-cards.py';shutil.copyfile(ROOT/'tools/inspect_native_vmu_cards.py', entry)
        before = self.snapshot()
        run = subprocess.run([sys.executable, str(entry), str(self.root)], cwd=package,
                             text=True, capture_output=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse(any(self.root.rglob('__pycache__')))

    def test_extracted_cli_works_with_only_shipped_codecs_and_no_sd_changes(self):
        package = Path(self.temp.name)/'package';package.mkdir();(package/'tools').mkdir()
        for name in ('vmu_persist_profile.py', 'explain_vmu_image.py', 'runtime_package.py'):
            shutil.copyfile(ROOT/'tools'/name, package/'tools'/name)
        entry = package/'inspect-cards.py';shutil.copyfile(ROOT/'tools/inspect_native_vmu_cards.py', entry)
        before = self.snapshot()
        run = subprocess.run([sys.executable, str(entry), str(self.root)], cwd=package,
                             text=True, capture_output=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertEqual(json.loads(run.stdout)['pair_count'], 1)
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(sorted(path.name for path in self.root.iterdir()), ['KUI'])


if __name__ == '__main__':
    unittest.main()
