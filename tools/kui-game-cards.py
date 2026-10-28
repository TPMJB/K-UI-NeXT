#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Copy saves from a shared virtual card onto each game's own card.

Usage: python3 kui-game-cards.py /path/to/SD --shared KUI/vmu-test/p0005 --games /path/to/SD/Games
Keep this script beside kui-library-startup-bundle.py, kui-native-startup-bundle.py
and explain_vmu_image.py. K-UI gives each game its own 200-block virtual card,
a /KUI/vmu-test/pNNNN pair whose game.bin names the game's product number.
This tool reads the shared card (replaying its journal), suggests a game for
each save from the games' IP headers, and after you confirm creates one new
card per game holding that game's saves. The shared card is only read. A game
whose card already holds saves is left alone; an empty card made by K-UI is
replaced. The console maps each new card the first time its game starts.
Requires only Python3's standard library.
"""
import argparse
import difflib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import sys
import zlib

TOOL_VERSION = '1'
CARD_BYTES, JOURNAL_BYTES = 131072, 1024
ROOT, FAT, DIR_FIRST, DIR_LAST, FREE, END = 255, 254, 253, 241, 0xfffc, 0xfffa
PAIR = re.compile(r'p[0-9]{4}')
HERE = Path(__file__).resolve().parent


def load(name, module):
    spec = importlib.util.spec_from_file_location(module, HERE / name)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    return helper


explain = load('explain_vmu_image.py', 'kui_explain_vmu_image').explain


def le16(data, at):
    return struct.unpack_from('<H', data, at)[0]


def product_key(product):
    """kui_vmu_game_card_key: trimmed, upper case, 1..10 of A-Z 0-9 '-'."""
    key = (product or '').strip(' ').upper()
    if (not 1 <= len(key) <= 10 or not re.fullmatch(r'[A-Z0-9-]+', key) or
            not re.search(r'[A-Z1-9]', key)):
        return None
    return key


def binding(key):
    """game.bin: KUIVMUG1, version 1, 64 bytes, key NUL padded to 12, CRC32."""
    if product_key(key) != key:
        raise ValueError(f'{key!r} is not a canonical product key')
    wire = bytearray(64)
    wire[0:8] = b'KUIVMUG1'
    struct.pack_into('<II', wire, 8, 1, 64)
    wire[16:16 + len(key)] = key.encode('ascii')
    struct.pack_into('<I', wire, 60, zlib.crc32(bytes(wire[:60])))
    return bytes(wire)


def bound_key(wire):
    if len(wire) != 64:
        return None
    key = wire[16:28].rstrip(b'\0')
    try:
        text = key.decode('ascii')
    except UnicodeDecodeError:
        return None
    return text if product_key(text) == text and binding(text) == wire else None


def decode_profile_extents(profile):
    """Card size and base/journal useful extents from a P2 profile wire."""
    if len(profile) != 4096 or profile[:8] != b'KUIVMUP2':
        raise ValueError('not a P2 profile')
    card_sectors = struct.unpack_from('<Q', profile, 32)[0]
    extents = []
    for role in range(2):
        record = 512 + 448 * role
        count = struct.unpack_from('<I', profile, record + 40)[0]
        if not 1 <= count <= 16:
            raise ValueError('profile extent count')
        extents.append([struct.unpack_from('<III', profile, record + 64 + 12 * i)[1:]
                        for i in range(count)])
    return card_sectors, extents


def mapping_crc(identity, card_sectors, extents):
    """vmu_store.c map_crc over the identity and the useful extents."""
    crc = zlib.crc32(identity)
    crc = zlib.crc32(struct.pack('<QII', card_sectors, len(extents[0]), len(extents[1])), crc)
    for role in range(2):
        for lba, blocks in extents[role]:
            crc = zlib.crc32(struct.pack('<QI', lba, blocks), crc)
    return crc


def effective_image(card, journal, identity, profile=None):
    """The card as the stage would open it: a committed journal block is
    replayed exactly as kui_vmu_store_open does; a torn marker is ignored."""
    if len(card) != CARD_BYTES or len(journal) != JOURNAL_BYTES or len(identity) != 16:
        raise ValueError('pair files have the wrong lengths')
    marker = journal[512:]
    if zlib.crc32(marker[:508]) != struct.unpack_from('<I', marker, 508)[0]:
        return bytes(card), None
    block = struct.unpack_from('<I', marker, 44)[0]
    if (marker[:8] != b'KUIVMUJ1' or struct.unpack_from('<II', marker, 8) != (1, 512) or
            block >= 256 or struct.unpack_from('<I', marker, 52)[0] != 1 or any(marker[64:508])):
        raise ValueError('the journal marker is malformed; open the game once to recover it')
    if marker[16:32] != identity:
        raise ValueError('the journal belongs to another card identity')
    if profile is not None:
        card_sectors, extents = decode_profile_extents(profile)
        if (struct.unpack_from('<Q', marker, 32)[0] != card_sectors or
                struct.unpack_from('<I', marker, 40)[0] != mapping_crc(identity, card_sectors, extents)):
            raise ValueError('the journal belongs to another card mapping')
    payload = journal[:512]
    if zlib.crc32(payload) != struct.unpack_from('<I', marker, 48)[0]:
        raise ValueError('the journal payload is damaged; open the game once to recover it')
    image = bytearray(card)
    image[block * 512:block * 512 + 512] = payload
    return bytes(image), block


def entry_offset(slot):
    return (DIR_FIRST - slot // 16) * 512 + (slot % 16) * 32


def text(raw):
    return raw.split(b'\0', 1)[0].decode('ascii', 'replace').strip()


def saves(image):
    """Occupied directory entries with their VMS descriptions."""
    rows = []
    for slot in range(208):
        entry = image[entry_offset(slot):entry_offset(slot) + 32]
        if not entry[0]:
            continue
        first, blocks, header = le16(entry, 2), le16(entry, 24), le16(entry, 26)
        at = first
        for _ in range(header):
            at = le16(image, FAT * 512 + 2 * at)
        head = image[at * 512:at * 512 + 512] if at < 200 else bytes(512)
        rows.append(dict(slot=slot, name=text(entry[4:16]), blocks=blocks,
                         game_file=entry[0] == 0xcc, vmu=text(head[0:16]),
                         dc=text(head[16:48]), app=text(head[48:64])))
    return rows


def subset(image, slots):
    """kui_vmu_image_subset: chosen entries keep their bytes and blocks;
    every other entry and block is zero and its FAT entry free."""
    if explain(image)['problems']:
        raise ValueError('the shared card does not validate; nothing was changed')
    used, fat = set(), image[FAT * 512:FAT * 512 + 512]
    for slot in slots:
        entry = image[entry_offset(slot):entry_offset(slot) + 32]
        if not entry[0]:
            raise ValueError('an empty directory slot was chosen')
        at = le16(entry, 2)
        for _ in range(le16(entry, 24)):
            used.add(at)
            at = le16(fat, 2 * at)
    out = bytearray(CARD_BYTES)
    out[FAT * 512:FAT * 512 + 512] = fat
    for block in range(200):
        if block in used:
            out[block * 512:block * 512 + 512] = image[block * 512:block * 512 + 512]
        else:
            struct.pack_into('<H', out, FAT * 512 + 2 * block, FREE)
    for slot in slots:
        out[entry_offset(slot):entry_offset(slot) + 32] = image[entry_offset(slot):entry_offset(slot) + 32]
    out[ROOT * 512:ROOT * 512 + 512] = image[ROOT * 512:ROOT * 512 + 512]
    report = explain(bytes(out))
    if report['problems'] or len(report['files']) != len(slots) or report['owned_blocks'] != len(used):
        raise ValueError('the new card failed validation; nothing was changed')
    return bytes(out)


def read_pair(folder):
    files = {name: (folder / name).read_bytes() for name in ('card.vmd', 'card.journal', 'identity.bin')}
    profile = next((p.read_bytes() for p in sorted(folder.glob('retail-persistent-profile-*.bin'))), None)
    image, replayed = effective_image(files['card.vmd'], files['card.journal'], files['identity.bin'], profile)
    return image, replayed


def pairs(root):
    """Existing pair folders and the game each one is bound to."""
    parent = root / 'KUI' / 'vmu-test'
    found = {}
    if parent.is_dir():
        for folder in sorted(parent.iterdir()):
            if PAIR.fullmatch(folder.name.lower()) and folder.is_dir() and not folder.is_symlink():
                game = folder / 'game.bin'
                found[folder.name] = bound_key(game.read_bytes()) if game.is_file() else None
    return found


def games(folder):
    """Product key and title of every GDI below the games folder."""
    library = load('kui-library-startup-bundle.py', 'kui_library_startup_bundle')
    gdis, _ = library.find_images(folder)
    result = {}
    for gdi in gdis:
        try:
            ip = library.peek_ip(gdi)
        except (OSError, ValueError, UnicodeError):
            continue
        key = product_key(ip.get('product', ''))
        if key and key not in result:
            result[key] = ip.get('title') or key
    return result


def normalized(value):
    return re.sub(r'[^A-Z0-9]+', ' ', value.upper()).strip()


def suggest(save, catalog):
    """The game whose title best matches the save's descriptions, or None."""
    names = [normalized(save[k]) for k in ('vmu', 'dc', 'app', 'name') if save[k]]
    best, score = None, 0.0
    for key, title in catalog.items():
        target = normalized(title)
        tokens = set(target.split())
        for name in names:
            ratio = difflib.SequenceMatcher(None, name, target).ratio()
            words = set(name.split())
            if tokens and words:
                ratio = max(ratio, len(tokens & words) / len(tokens | words) + 0.2 * bool(tokens & words))
            if ratio > score:
                best, score = key, ratio
    return best if score >= 0.55 else None


def plan(root, shared, catalog):
    image, replayed = read_pair(root / shared)
    rows = saves(image)
    for row in rows:
        row['game'] = suggest(row, catalog)
        row['title'] = catalog.get(row['game'])
    return dict(schema='kui.game-cards.plan.v1', tool_version=TOOL_VERSION, shared=str(shared),
                replayed_block=replayed, saves=rows)


def ask(rows, catalog, read=input, write=print):
    """Confirm or change each suggestion; returns the edited rows."""
    keys = sorted(catalog, key=lambda k: catalog[k].upper())
    for number, row in enumerate(rows, 1):
        write(f"\n[{number}/{len(rows)}] {row['name']}  {row['blocks']} blocks  "
              f"\"{row['vmu']}\" / \"{row['dc']}\"")
        while True:
            hint = f"{catalog[row['game']]} ({row['game']})" if row['game'] else 'none'
            answer = read(f'  Game: {hint}. Enter keeps it, a number or product picks another, '
                          '? lists games, s leaves it on the shared card: ').strip()
            if not answer:
                break
            if answer.lower() == 's':
                row['game'] = None
                break
            if answer == '?':
                for index, key in enumerate(keys, 1):
                    write(f'    {index:3d}  {catalog[key]}  ({key})')
                continue
            if answer.isdigit() and 1 <= int(answer) <= len(keys):
                row['game'] = keys[int(answer) - 1]
                break
            if product_key(answer):
                row['game'] = product_key(answer)
                break
            write('    Not understood.')
        row['title'] = catalog.get(row['game'], row['game'])
    return rows


def apply(root, shared, rows, write=print):
    """Create one new pair per game; returns {key: pair name}."""
    image, _ = read_pair(root / shared)
    if ({(r['slot'], r['name'], r['blocks']) for r in saves(image)} !=
            {(r['slot'], r['name'], r['blocks']) for r in rows}):
        raise ValueError('the shared card changed since the plan was made; make a new plan')
    chosen = {}
    for row in rows:
        if row.get('game'):
            chosen.setdefault(row['game'], []).append(row['slot'])
    existing = pairs(root)
    parent = root / 'KUI' / 'vmu-test'
    used = {name.lower() for name in existing}
    created = {}
    for key, slots in sorted(chosen.items()):
        bound = [name for name, game in existing.items() if game == key]
        if len(bound) > 1:
            write(f'{key}: several cards claim this game ({", ".join(bound)}); skipped')
            continue
        try:
            if bound:
                card, _ = read_pair(parent / bound[0])
                if explain(card)['files']:
                    write(f'{key}: its card {bound[0]} already holds saves; skipped')
                    continue
            content = subset(image, slots)
            if bound:
                # Never let two pairs claim one game: the empty card goes first.
                retire(parent / bound[0])
                write(f'{key}: removed the empty card {bound[0]}')
            number = next(n for n in range(2, 10000) if f'p{n:04d}' not in used)
            name = f'p{number:04d}'
            used.add(name)
            folder = parent / name
            folder.mkdir(parents=True)
            # game.bin goes last: a folder left without it binds no game.
            for leaf, data in (('card.vmd', content), ('card.journal', bytes(JOURNAL_BYTES)),
                               ('identity.bin', identity()), ('game.bin', binding(key))):
                with (folder / leaf).open('xb') as out:
                    out.write(data)
                    out.flush()
                    os.fsync(out.fileno())
        except (OSError, ValueError) as error:
            write(f'{key}: not copied ({error}); its saves stay on the shared card')
            continue
        created[key] = name
        write(f'{key}: {name} holds {len(slots)} save(s)')
    return created


def identity():
    while True:
        value = os.urandom(16)
        if any(value):
            return value


def retire(folder):
    """Remove an empty game card K-UI made, before its replacement is made."""
    for path in folder.iterdir():
        if path.is_file() and not path.is_symlink():
            path.unlink()
    folder.rmdir()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    version = f'K-UI game cards v{TOOL_VERSION}'
    parser.add_argument('--version', action='version', version=version)
    parser.add_argument('sd', type=Path, help='The mounted SD card')
    parser.add_argument('--shared', type=Path, required=True, help='Shared pair, e.g. KUI/vmu-test/p0005')
    parser.add_argument('--games', type=Path, required=True, help='Folder holding the GDIs')
    parser.add_argument('--plan', type=Path, help='Write the suggested plan as JSON and stop')
    parser.add_argument('--apply', type=Path, help='Create cards from an edited plan file')
    args = parser.parse_args()
    print(version, flush=True)
    try:
        catalog = games(args.games)
        if args.apply:
            proposal = json.loads(args.apply.read_text())
            if proposal.get('schema') != 'kui.game-cards.plan.v1' or proposal.get('shared') != str(args.shared):
                raise ValueError('the plan file is for another shared card')
            rows = proposal['saves']
        else:
            proposal = plan(args.sd, args.shared, catalog)
            print(f'{args.shared}: {len(proposal["saves"])} saves; {len(catalog)} games found')
            if args.plan:
                args.plan.write_text(json.dumps(proposal, indent=2) + '\n')
                print(f'Wrote {args.plan}; edit "game" for each save, then rerun with --apply')
                return
            rows = ask(proposal['saves'], catalog)
            print('\nPlan:')
            for row in rows:
                print(f"  {row['name']:<12} -> {row['title'] + ' (' + row['game'] + ')' if row['game'] else 'stays on the shared card'}")
            if input('Create these game cards? [y/N] ').strip().lower() != 'y':
                print('Nothing was changed.')
                return
        created = apply(args.sd, args.shared, rows)
        print(f'Done: {len(created)} game card(s). The shared card was only read.')
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as error:
        parser.exit(1, f'Stopped: {error}\n')


if __name__ == '__main__':
    main()
