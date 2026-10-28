#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""The PC game-card tool on synthetic SD layouts: journal replay matches the
production store, keys and bindings match the C encoder, and the shared
card is only read."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile
import unittest
import zlib

from games_fixture import RAW, make_fixture

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools')]
from vmu_persist_profile import decode_profile, template_profile  # noqa: E402
SPEC = importlib.util.spec_from_file_location('kui_game_cards', ROOT / 'tools/kui-game-cards.py')
cards = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(cards)

CC = shlex.split(os.environ.get('CC', 'cc'))
BLANK = r'''
#include <stdio.h>
#include "kui/vmu_image.h"
int main(int argc,char **argv) {
    FILE *f=fopen(argv[1],"wb");unsigned char b[512];
    for(unsigned i=0;i<256u;i++) {kui_vmu_image_blank_block((uint16_t)i,b);fwrite(b,1,512,f);}
    return argc!=2 || fclose(f);
}
'''
KEYS = r'''
#include <stdio.h>
#include "kui/vmu_game_card.h"
int main(int argc,char **argv) {
    for(int i=1;i<argc;i++) {
        char key[KUI_VMU_GAME_CARD_KEY_BYTES];uint8_t wire[64];
        if(!kui_vmu_game_card_key(argv[i],key) || !kui_vmu_game_card_binding_encode(key,wire)) {puts("-");continue;}
        printf("%s ",key);for(int j=0;j<64;j++) printf("%02x",wire[j]);puts("");
    }
    return 0;
}
'''
INSPECT = ('tools/vmu_pair_inspect.c', 'src/core/vmu_persist_profile.c', 'src/core/vmu_store.c',
           'src/core/vmu_image.c', 'src/core/retail_image.c', 'src/core/hash.c', 'src/core/data.c')


def add_file(image, slot, name, blocks, vmu, dc, fill):
    """A data file on descending free blocks, as a game would save it."""
    fat = 254 * 512
    free = [b for b in range(199, -1, -1) if struct.unpack_from('<H', image, fat + 2 * b)[0] == 0xfffc]
    chain = free[:blocks]
    for here, there in zip(chain, chain[1:] + [None]):
        struct.pack_into('<H', image, fat + 2 * here, there if there is not None else 0xfffa)
        image[here * 512:here * 512 + 512] = bytes([fill]) * 512
    header = bytearray([fill]) * 512
    header[0:16] = vmu.encode().ljust(16, b' ')
    header[16:48] = dc.encode().ljust(32, b' ')
    header[48:64] = b'K-UI TEST'.ljust(16, b' ')
    image[chain[0] * 512:chain[0] * 512 + 512] = header
    entry = bytearray(32)
    entry[0], entry[1] = 0x33, 0x10
    struct.pack_into('<H', entry, 2, chain[0])
    entry[4:16] = name.encode().ljust(12, b'\0')
    entry[16:24] = bytes.fromhex('2026102809300003')
    struct.pack_into('<HH', entry, 24, blocks, 0)
    at = cards.entry_offset(slot)
    image[at:at + 32] = entry
    return chain


def journal_record(image_id, block, payload, profile):
    card_sectors, extents = cards.decode_profile_extents(profile)
    marker = bytearray(512)
    marker[0:8] = b'KUIVMUJ1'
    struct.pack_into('<II', marker, 8, 1, 512)
    marker[16:32] = image_id
    struct.pack_into('<QIIIIQ', marker, 32, card_sectors, cards.mapping_crc(image_id, card_sectors, extents),
                     block, zlib.crc32(payload), 1, 7)
    struct.pack_into('<I', marker, 508, zlib.crc32(bytes(marker[:508])))
    return payload + bytes(marker)


class GameCardsTool(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.work = tempfile.TemporaryDirectory(prefix='kui-game-cards-')
        cls.dir = Path(cls.work.name)
        for name, source, extra in (('blank', BLANK, ('src/core/vmu_image.c',)),
                                    ('keys', KEYS, ('src/core/vmu_game_card.c', 'src/core/vmu_persist_profile.c',
                                                    'src/core/retail_image.c', 'src/core/hash.c', 'src/core/data.c'))):
            (cls.dir / f'{name}.c').write_text(source)
            subprocess.run([*CC, '-std=c11', '-I', str(ROOT / 'include'), str(cls.dir / f'{name}.c'),
                            *[str(ROOT / s) for s in extra], '-o', str(cls.dir / name)], check=True)
        subprocess.run([*CC, '-std=c11', '-O2', '-I', str(ROOT / 'include'), *[str(ROOT / s) for s in INSPECT],
                        '-o', str(cls.dir / 'inspect')], check=True)
        subprocess.run([str(cls.dir / 'blank'), str(cls.dir / 'blank.vmd')], check=True)

    @classmethod
    def tearDownClass(cls):
        cls.work.cleanup()

    def setUp(self):
        temporary = tempfile.TemporaryDirectory(dir=self.dir)
        self.addCleanup(temporary.cleanup)
        self.sd = Path(temporary.name) / 'SD'
        self.profile = template_profile()
        image_id = bytes.fromhex(decode_profile(self.profile)['image_id'])
        shared = self.sd / 'KUI' / 'vmu-test' / 'p0001'
        shared.mkdir(parents=True)
        image = bytearray((self.dir / 'blank.vmd').read_bytes())
        add_file(image, 0, 'C_TAXI02.SYS', 20, 'CRAZY TAXI 2', 'CT2 System Data', 0x11)
        self.sonic = add_file(image, 1, 'SONICADV2_A', 10, 'SONIC ADV 2', 'Sonic Adventure 2 save', 0x22)
        add_file(image, 2, 'LODOSS_DATA', 30, 'LODOSS WAR', 'Record of Lodoss War', 0x33)
        add_file(image, 3, 'MYSTERY.SAV', 2, 'ZZZ', 'Unknown data', 0x44)
        self.assertEqual(cards.explain(bytes(image))['problems'], [])
        # A committed journal block the base has not received yet.
        self.replayed = self.sonic[3]
        journal = journal_record(image_id, self.replayed, bytes([0x77]) * 512, self.profile)
        for name, data in (('card.vmd', bytes(image)), ('card.journal', journal), ('identity.bin', image_id),
                           ('retail-persistent-profile-0001.bin', self.profile)):
            (shared / name).write_bytes(data)
        self.games = self.sd / 'Games'
        for folder, title, product in (('CT2', b'CRAZY TAXI 2', b'MK-51136  '),
                                       ('SA2', b'SONIC ADVENTURE 2', b'MK-51117  '),
                                       ('Lodoss', b'RECORD OF LODOSS WAR', b'T-15112N  ')):
            make_fixture(self.games / folder, title=title)
            with (self.games / folder / 'track03.bin').open('r+b') as track:
                track.seek(16 + 64)
                track.write(product)
                track.seek(21 * RAW + 16)
                track.write(title)

    def shared_hashes(self):
        folder = self.sd / 'KUI' / 'vmu-test' / 'p0001'
        return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in folder.iterdir()}

    def test_replay_matches_the_production_store(self):
        folder = self.sd / 'KUI' / 'vmu-test' / 'p0001'
        image, block = cards.read_pair(folder)
        self.assertEqual(block, self.replayed)
        self.assertEqual(image[block * 512:block * 512 + 512], bytes([0x77]) * 512)
        out = self.dir / 'effective.vmd'
        run = subprocess.run([str(self.dir / 'inspect'), str(folder / 'retail-persistent-profile-0001.bin'),
                              str(folder / 'card.vmd'), str(folder / 'card.journal'), str(out)],
                             capture_output=True, text=True)
        self.assertEqual(json.loads(run.stdout)['store_open'], 0, run.stdout)
        self.assertEqual(out.read_bytes(), image)
        torn = bytearray((folder / 'card.journal').read_bytes())
        torn[600] ^= 1
        self.assertEqual(cards.effective_image((folder / 'card.vmd').read_bytes(), bytes(torn),
                                               (folder / 'identity.bin').read_bytes(), self.profile),
                         ((folder / 'card.vmd').read_bytes(), None))
        with self.assertRaisesRegex(ValueError, 'another card identity'):
            cards.effective_image((folder / 'card.vmd').read_bytes(), (folder / 'card.journal').read_bytes(),
                                  bytes(16), self.profile)
        moved = bytearray(self.profile)
        struct.pack_into('<I', moved, 512 + 64 + 4, struct.unpack_from('<I', moved, 512 + 64 + 4)[0] + 1)
        with self.assertRaisesRegex(ValueError, 'another card mapping'):
            cards.effective_image((folder / 'card.vmd').read_bytes(), (folder / 'card.journal').read_bytes(),
                                  (folder / 'identity.bin').read_bytes(), bytes(moved))

    def test_unformatted_shared_card_is_refused(self):
        folder = self.sd / 'KUI' / 'vmu-test' / 'p0001'
        before = self.shared_hashes()
        (folder / 'card.journal').write_bytes(bytes(1024))
        noise = bytes((i * 131 + 7) & 255 for i in range(131072))
        (folder / 'card.vmd').write_bytes(noise)
        self.assertIsInstance(cards.saves(noise), list)
        with self.assertRaisesRegex(ValueError, 'does not validate'):
            cards.plan(self.sd, Path('KUI/vmu-test/p0001'), {})
        self.assertNotEqual(self.shared_hashes(), before)

    def test_keys_and_bindings_match_c(self):
        products = ['MK-51136', ' mk-51136 ', 'T-9504D-50', 'HDR-0076', '0000000', 'MK 51136', '', 'ABCDEFGHIJK']
        lines = subprocess.run([str(self.dir / 'keys'), *products], capture_output=True, text=True,
                               check=True).stdout.splitlines()
        for product, line in zip(products, lines):
            key = cards.product_key(product)
            if line == '-':
                self.assertIsNone(key, product)
            else:
                expected_key, wire = line.split()
                self.assertEqual((key, cards.binding(key).hex()), (expected_key, wire))
                self.assertEqual(cards.bound_key(bytes.fromhex(wire)), key)

    def test_plan_suggests_and_apply_creates_cards(self):
        before = self.shared_hashes()
        parent = self.sd / 'KUI' / 'vmu-test'
        # An empty card K-UI made for SA2 is replaced; Lodoss's card holds a save.
        for name, key, holds in (('p0003', 'MK-51117', False), ('p0004', 'T-15112N', True)):
            image = bytearray((self.dir / 'blank.vmd').read_bytes())
            if holds:
                add_file(image, 0, 'LODOSS_NEW', 4, 'LODOSS WAR', 'Newer save', 0x55)
            (parent / name).mkdir()
            for leaf, data in (('card.vmd', bytes(image)), ('card.journal', bytes(1024)),
                               ('identity.bin', os.urandom(16)), ('game.bin', cards.binding(key))):
                (parent / name / leaf).write_bytes(data)
        catalog = cards.games(self.games)
        self.assertEqual(catalog, {'MK-51136': 'CRAZY TAXI 2', 'MK-51117': 'SONIC ADVENTURE 2',
                                   'T-15112N': 'RECORD OF LODOSS WAR'})
        proposal = cards.plan(self.sd, Path('KUI/vmu-test/p0001'), catalog)
        self.assertEqual([(r['name'], r['game']) for r in proposal['saves']],
                         [('C_TAXI02.SYS', 'MK-51136'), ('SONICADV2_A', 'MK-51117'),
                          ('LODOSS_DATA', 'T-15112N'), ('MYSTERY.SAV', None)])
        messages = []
        created = cards.apply(self.sd, Path('KUI/vmu-test/p0001'), proposal['saves'], write=messages.append)
        self.assertEqual(created, {'MK-51117': 'p0002', 'MK-51136': 'p0005'})
        self.assertIn('T-15112N: its card p0004 already holds saves or does not validate; skipped', messages)
        self.assertFalse((parent / 'p0003').exists())
        shared_image, _ = cards.read_pair(parent / 'p0001')
        for key, name, files in (('MK-51136', 'p0005', ['C_TAXI02.SYS']), ('MK-51117', 'p0002', ['SONICADV2_A'])):
            folder = parent / name
            card = (folder / 'card.vmd').read_bytes()
            report = cards.explain(card)
            self.assertEqual((report['problems'], [f['name'] for f in report['files']]), ([], files))
            for row in report['files']:
                for block in row['chain']:
                    self.assertEqual(card[block * 512:block * 512 + 512], shared_image[block * 512:block * 512 + 512])
            self.assertEqual(card[255 * 512:], shared_image[255 * 512:])
            self.assertEqual((folder / 'card.journal').read_bytes(), bytes(1024))
            self.assertEqual(len((folder / 'identity.bin').read_bytes()), 16)
            self.assertEqual(cards.bound_key((folder / 'game.bin').read_bytes()), key)
        sonic = (parent / 'p0002' / 'card.vmd').read_bytes()
        self.assertEqual(sonic[self.replayed * 512:self.replayed * 512 + 512], bytes([0x77]) * 512)
        self.assertEqual(self.shared_hashes(), before)
        # One game's unreadable card does not stop the others.
        (parent / 'p0004' / 'card.journal').write_bytes(journal_record(bytes(16), 3, bytes(512), self.profile))
        messages.clear()
        rows = [dict(r, game='T-15112N') if r['name'] == 'MYSTERY.SAV' else r for r in proposal['saves']]
        created = cards.apply(self.sd, Path('KUI/vmu-test/p0001'), rows, write=messages.append)
        self.assertTrue(any(m.startswith('T-15112N: not copied') for m in messages), messages)
        self.assertEqual(sorted(created), [])
        # A shared card that changed after planning is refused.
        image = bytearray((parent / 'p0001' / 'card.vmd').read_bytes())
        add_file(image, 5, 'LATE.SAV', 1, 'LATE', 'Later', 0x66)
        (parent / 'p0001' / 'card.vmd').write_bytes(bytes(image))
        with self.assertRaisesRegex(ValueError, 'changed since the plan'):
            cards.apply(self.sd, Path('KUI/vmu-test/p0001'), proposal['saves'], write=messages.append)

    def test_command_line_plan_then_apply(self):
        before = self.shared_hashes()
        plan_file = self.dir / f'plan-{os.getpid()}.json'
        base = [sys.executable, str(ROOT / 'tools/kui-game-cards.py'), str(self.sd),
                '--shared', 'KUI/vmu-test/p0001', '--games', str(self.games)]
        subprocess.run([*base, '--plan', str(plan_file)], check=True, capture_output=True)
        proposal = json.loads(plan_file.read_text())
        for row in proposal['saves']:
            if row['name'] == 'MYSTERY.SAV':
                row['game'] = 'MK-51136'
        plan_file.write_text(json.dumps(proposal))
        run = subprocess.run([*base, '--apply', str(plan_file)], capture_output=True, text=True)
        self.assertEqual(run.returncode, 0, run.stderr)
        bound = {key: name for name, key in cards.pairs(self.sd).items() if key}
        self.assertEqual(sorted(bound), ['MK-51117', 'MK-51136', 'T-15112N'])
        card = (self.sd / 'KUI' / 'vmu-test' / bound['MK-51136'] / 'card.vmd').read_bytes()
        self.assertEqual([f['name'] for f in cards.explain(card)['files']], ['C_TAXI02.SYS', 'MYSTERY.SAV'])
        self.assertEqual(self.shared_hashes(), before)


if __name__ == '__main__':
    unittest.main()
