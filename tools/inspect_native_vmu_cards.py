#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Read every prepared virtual card and replay committed journal data in RAM.

Print JSON only. This command never opens an SD file for writing, creates a
card, clears a journal, or exports an effective image to the SD card. Keyword
hints help locate text; they do not establish which game owns an unknown save.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import zlib

# Codecs may themselves live on the selected card; importing them must not
# create __pycache__ there either.
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent/'tools'))
from explain_vmu_image import explain
from runtime_package import MAX_BYTES, verify
from vmu_persist_profile import decode_profile, same_ownership, template_profile

SCHEMA = 'kui.native.vmu-card-inventory.v1'
PAIR = re.compile(r'p[0-9]{4}')
PROFILE = re.compile(r'retail-persistent-profile-[0-9]{4}\.bin')
IMAGE_BYTES = 131072


def sha(data):
    return hashlib.sha256(data).hexdigest()


def require(value, message):
    if not value:
        raise ValueError(message)


def ordinary(path, root, directory=False):
    path, root = Path(path).absolute(), Path(root).absolute()
    try:
        relative = path.relative_to(root)
    except ValueError as error:
        raise ValueError('Path leaves selected SD root') from error
    require('..' not in relative.parts, 'Parent traversal is not allowed')
    require(stat.S_ISDIR(root.lstat().st_mode), 'SD root must be an ordinary directory, not a symlink')
    current = root
    for index, component in enumerate(relative.parts):
        current = current/component
        mode = current.lstat().st_mode
        want_directory = index+1 < len(relative.parts) or directory
        require(stat.S_ISDIR(mode) if want_directory else stat.S_ISREG(mode),
                'Refusing symlink or non-ordinary path: '+str(path.relative_to(root)))
    return path


def read_regular(path, root, size=None, maximum=None):
    path = ordinary(path, root)
    before = path.lstat()
    require(size is None or before.st_size == size, 'Wrong file length: '+str(path.relative_to(root)))
    require(maximum is None or before.st_size <= maximum, 'Oversized file: '+str(path.relative_to(root)))
    fd = os.open(path, os.O_RDONLY | getattr(os, 'O_NOFOLLOW', 0))
    try:
        opened = os.fstat(fd)
        require(stat.S_ISREG(opened.st_mode) and (opened.st_dev, opened.st_ino) ==
                (before.st_dev, before.st_ino), 'File changed during read admission')
        with os.fdopen(fd, 'rb', closefd=False) as source:
            data = source.read()
        after = os.fstat(fd)
        require(len(data) == before.st_size and (after.st_size, after.st_mtime_ns, after.st_ctime_ns) ==
                (before.st_size, before.st_mtime_ns, before.st_ctime_ns), 'File changed while reading')
    finally:
        os.close(fd)
    return data


def mapping_crc(profile):
    """Same identity/capacity/ordered extent wire as vmu_store.c map_crc()."""
    files = profile['files'][:2]
    crc = zlib.crc32(bytes.fromhex(profile['image_id']))
    crc = zlib.crc32(struct.pack('<QII', profile['card_sectors'],
        len(files[0]['useful']), len(files[1]['useful'])), crc)
    for role in files:
        for extent in role['useful']:
            crc = zlib.crc32(struct.pack('<QI', extent['lba'], extent['blocks']), crc)
    return crc


def replay_journal(profile, image, journal):
    """Mirror production recover(), returning a new effective image or None.

    Invalid marker CRC means uncommitted and is ignored before all other
    marker checks. A valid CRC with malformed fields, a different identity/map,
    or corrupt payload blocks opening the store. Replay never mutates inputs.
    """
    require(len(image) == IMAGE_BYTES and len(journal) == 1024, 'Wrong card/journal length')
    marker, payload = journal[512:], journal[:512]
    get32 = lambda at: struct.unpack_from('<I', marker, at)[0]
    get64 = lambda at: struct.unpack_from('<Q', marker, at)[0]
    wanted_crc, actual_crc = get32(508), zlib.crc32(marker[:508])
    result = dict(state='clean_marker' if not any(marker) else 'uncommitted_marker_ignored',
        store_open=0, store_result='OK', marker_crc_valid=wanted_crc == actual_crc,
        marker_crc_stored=f'{wanted_crc:08x}', marker_crc_computed=f'{actual_crc:08x}',
        journal_sequence=0, replay_writes=0, first_changed_block=-1,
        mapping_crc_expected=f'{mapping_crc(profile):08x}', replayed_in_memory_only=False)
    if wanted_crc != actual_crc:
        return bytes(image), result
    result['marker'] = dict(magic_hex=marker[:8].hex(), version=get32(8), sector_bytes=get32(12),
        image_id=marker[16:32].hex(), card_sectors=get64(32), mapping_crc=f'{get32(40):08x}',
        target_block=get32(44), payload_crc=f'{get32(48):08x}', committed=get32(52), sequence=get64(56))
    fields_ok = marker[:8] == b'KUIVMUJ1' and get32(8) == 1 and get32(12) == 512 and \
        get32(44) < 256 and get32(52) == 1 and not any(marker[64:508])
    if not fields_ok:
        result.update(state='committed_marker_malformed', store_open=-4, store_result='CORRUPT')
        return None, result
    if (marker[16:32].hex() != profile['image_id'] or get64(32) != profile['card_sectors'] or
            get32(40) != mapping_crc(profile)):
        result.update(state='committed_marker_identity_mismatch', store_open=-6, store_result='IDENTITY')
        return None, result
    if get32(48) != zlib.crc32(payload):
        result.update(state='committed_payload_corrupt', store_open=-4, store_result='CORRUPT')
        return None, result
    at = get32(44)*512
    changed = image[at:at+512] != payload
    effective = image[:at]+payload+image[at+512:]
    result.update(state='committed_record_replayed_in_memory', journal_sequence=get64(56),
        replay_writes=1, first_changed_block=get32(44) if changed else -1,
        replayed_in_memory_only=True)
    return effective, result


def text_field(data):
    raw = data.rstrip(b'\0 ')
    for encoding in ('ascii', 'shift_jis', 'latin-1'):
        try:
            return dict(text=raw.decode(encoding), raw_hex=data.hex(), encoding=encoding)
        except UnicodeDecodeError:
            continue
    raise AssertionError('latin-1 represents every byte')


def inventory(image, keywords):
    result = explain(image)
    result['image_sha256'] = sha(image)
    result['file_count'] = len(result.get('files', []))
    result['game_file_count'] = sum(entry['type'] == '0xcc' for entry in result.get('files', []))
    for entry in result.get('files', []):
        chain, header = entry['chain'], entry['header']
        entry['keyword_hints'] = []
        if header < len(chain):
            address = chain[header]*512
            data = image[address:address+128]
            entry['vms_header'] = dict(image_block=chain[header], file_block_offset=header,
                short_description=text_field(data[:16]), long_description=text_field(data[16:48]),
                application_id=text_field(data[48:64]), icon_count=struct.unpack_from('<H', data, 64)[0],
                animation_speed=struct.unpack_from('<H', data, 66)[0],
                eyecatch_type=struct.unpack_from('<H', data, 68)[0],
                crc16=f'{struct.unpack_from("<H", data, 70)[0]:04x}',
                data_bytes=struct.unpack_from('<I', data, 72)[0],
                interpretation='standard VMS fields; game payload/CRC not validated')
        else:
            entry['vms_header'] = None
        values = {'filename': entry['name']}
        if entry['vms_header']:
            values.update({name: entry['vms_header'][name]['text']
                           for name in ('short_description', 'long_description', 'application_id')})
        for field, value in values.items():
            for keyword in keywords:
                if keyword.casefold() in value.casefold():
                    entry['keyword_hints'].append(dict(field=field, keyword=keyword))
    return result


def inspect_pair(root, pair, keywords):
    relative = pair.relative_to(root).as_posix()
    result = dict(pair=pair.name, pair_path=relative, status='ok', errors=[], profiles=[])
    image = journal = identity = None
    try:
        ordinary(pair, root, directory=True)
        require(pair.name != 'p0000', 'p0000 is not a prepared owner card')
        children = sorted(pair.iterdir(), key=lambda path: path.name)
        # Keep the full file-name inventory; no game is inferred from a folder/name.
        result['directory_files'] = [path.name for path in children]
        for name, expected in (('card.vmd', IMAGE_BYTES), ('card.journal', 1024), ('identity.bin', 16)):
            try:
                data = read_regular(pair/name, root, size=expected)
                result.setdefault('source_files', {})[name] = dict(bytes=len(data), sha256=sha(data))
                if name == 'card.vmd':
                    image = data
                elif name == 'card.journal':
                    journal = data
                else:
                    identity = data
            except (OSError, ValueError) as error:
                result['errors'].append(str(error))
        if identity is not None:
            result['identity'] = dict(actual_image_id=identity.hex())
        if image is not None:
            result['base_inventory'] = inventory(image, keywords)
        candidates = []
        for path in children:
            if not PROFILE.fullmatch(path.name):
                continue
            item = dict(path=path.relative_to(root).as_posix(), status='ok')
            result['profiles'].append(item)
            try:
                profile_wire = read_regular(path, root, size=4096)
                profile = decode_profile(profile_wire)
                require(profile['pair_path'] == '/'+relative, 'Profile pair_path differs from its actual directory')
                require(profile_wire != template_profile(), 'Public template is not a prepared owner profile')
                item.update(image_id=profile['image_id'], wire_sha256=sha(profile_wire),
                    card_sectors=profile['card_sectors'], cid=profile['cid'],
                    identity_matches=identity is not None and identity.hex() == profile['image_id'])
                require(identity is not None and identity.hex() == profile['image_id'],
                        'Profile image identity differs from identity.bin')
                candidates.append((path, profile))
            except (OSError, ValueError, KeyError, TypeError) as error:
                item.update(status='error', error=str(error))
                result['errors'].append(path.name+': '+str(error))
        require(candidates, 'No usable existing prepared profile in this card directory')
        require(all(same_ownership(candidates[0][1], profile) for _, profile in candidates[1:]),
                'Prepared profiles disagree about physical card ownership; no replay selected')
        selected, profile = candidates[-1]
        result['selected_profile'] = selected.relative_to(root).as_posix()
        result['profile_image_id'] = profile['image_id']
        if image is not None and journal is not None:
            effective, replay = replay_journal(profile, image, journal)
            result['journal'] = replay
            if effective is not None:
                result['effective_inventory'] = inventory(effective, keywords)
                if result['effective_inventory']['result'] != 'OK':
                    result['errors'].append('Effective VMU filesystem is not valid; see effective_inventory.problems')
            else:
                result['errors'].append('Journal store open refused: '+replay['store_result'])
    except (OSError, ValueError, KeyError, TypeError) as error:
        result['errors'].append(str(error))
    if result['errors']:
        result['status'] = 'error'
    return result


def installed_modules(root, known_profiles):
    modules = []
    for filename in ('retail-boot.kui', 'retail-native-dual.kui'):
        relative = Path('KUI/apps/games')/filename
        item = dict(path=relative.as_posix())
        modules.append(item)
        path = root/relative
        if not path.exists() and not path.is_symlink():
            item['status'] = 'missing'
            continue
        try:
            data = read_regular(path, root, maximum=MAX_BYTES+64)
            item.update(status='ok', module_sha256=sha(data), runtime=verify(data), embedded_profiles=[])
            at = data.find(b'KUIVMUP2', 64)
            while at != -1:
                if not at&3 and len(data)-at >= 4096:
                    try:
                        raw = data[at:at+4096]
                        profile = decode_profile(raw)
                        item['embedded_profiles'].append(dict(module_offset=at, pair_path=profile['pair_path'],
                            image_id=profile['image_id'], wire_sha256=sha(raw),
                            public_template=raw == template_profile(),
                            exact_profile_paths=known_profiles.get(sha(raw), [])))
                    except (ValueError, KeyError, TypeError):
                        pass
                at = data.find(b'KUIVMUP2', at+1)
        except (OSError, ValueError) as error:
            item.update(status='error', error=str(error))
    return modules


def scan(root, keywords=('skies', 'arcadia')):
    root = Path(root).absolute()
    ordinary(root, root, directory=True)
    parent = ordinary(root/'KUI/vmu-test', root, directory=True)
    keywords = tuple(dict.fromkeys(keywords))
    require(all(isinstance(word, str) and 1 <= len(word) <= 80 for word in keywords), 'Invalid keyword hint')
    pairs = [inspect_pair(root, path, keywords) for path in sorted(parent.iterdir(), key=lambda path: path.name)
             if PAIR.fullmatch(path.name)]
    known_profiles = {}
    for pair in pairs:
        for profile in pair['profiles']:
            if profile['status'] == 'ok':
                known_profiles.setdefault(profile['wire_sha256'], []).append(profile['path'])
    return dict(schema=SCHEMA, read_only=True, journal_replay='in_memory_only', sd_root=str(root),
        keywords=list(keywords), keyword_hints_are_not_game_identification=True,
        pair_count=len(pairs), pairs_with_errors=sum(pair['status'] == 'error' for pair in pairs),
        pairs=pairs, installed_modules=installed_modules(root, known_profiles))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('sd_root', type=Path)
    parser.add_argument('--keyword', action='append', help='Optional text hint; repeat to replace default skies/arcadia hints')
    args = parser.parse_args()
    try:
        report = scan(args.sd_root, args.keyword if args.keyword is not None else ('skies', 'arcadia'))
        print(json.dumps(report, indent=2))
    except (OSError, ValueError, KeyError, TypeError) as error:
        print(json.dumps(dict(schema=SCHEMA, read_only=True, status='error', error=str(error)), indent=2))
        return 2
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
