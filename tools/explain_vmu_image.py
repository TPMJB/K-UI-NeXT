#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Explain why a 128 KiB VMU image passes or fails kui_vmu_image_validate.

The production validator returns one code. This mirror reports every rule it
applies, in the same order, plus the files and FAT chains, so an owner's
image (for example the effective image from tools/vmu_pair_inspect.c) can be
diagnosed without guessing. It reads the image only.
"""
import argparse
import json
from pathlib import Path
import struct

ROOT, FAT, DIR_FIRST, DIR_LAST, FREE, END = 255, 254, 253, 241, 0xfffc, 0xfffa


def le16(data, at):
    return struct.unpack_from('<H', data, at)[0]


def explain(image):
    if len(image) != 131072:
        return dict(result='ARGUMENT', problems=['image is %d bytes, not 131072' % len(image)])
    block = lambda n: image[512*n:512*n+512]
    root, fat = block(ROOT), block(FAT)
    problems = []
    geometry = dict(formatted=root[:16] == b'\x55'*16, custom_color=root[16],
                    fat_block=le16(root, 0x46), fat_blocks=le16(root, 0x48),
                    directory_block=le16(root, 0x4a), directory_blocks=le16(root, 0x4c),
                    icon=le16(root, 0x4e), user_blocks=le16(root, 0x50))
    for label, ok in (('root not formatted (first 16 bytes 0x55)', geometry['formatted']),
                      ('custom color flag above 1', geometry['custom_color'] <= 1),
                      ('FAT block is not 254', geometry['fat_block'] == FAT),
                      ('FAT size is not 1 block', geometry['fat_blocks'] == 1),
                      ('directory does not start at 253', geometry['directory_block'] == DIR_FIRST),
                      ('directory is not 13 blocks', geometry['directory_blocks'] == 13),
                      ('icon shape above 123', geometry['icon'] <= 123),
                      ('user area is not 200 blocks', geometry['user_blocks'] == 200)):
        if not ok:
            problems.append('root: '+label)
    for n in range(200, 256):
        want = FREE if n < DIR_LAST else n-1 if DIR_LAST < n <= DIR_FIRST else END
        if le16(fat, 2*n) != want:
            problems.append('FAT[%d] is 0x%04x, system area expects 0x%04x' % (n, le16(fat, 2*n), want))
    free = 0
    for n in range(200):
        nxt = le16(fat, 2*n)
        if nxt == FREE:
            free += 1
        elif nxt != END and nxt >= 200:
            problems.append('FAT[%d] links to %d, outside the user area' % (n, nxt))
    used, files, names = {}, [], set()
    for directory in range(DIR_FIRST, DIR_LAST-1, -1):
        data = block(directory)
        for slot in range(16):
            entry = data[32*slot:32*slot+32]
            where = 'directory %d slot %d' % (directory, slot)
            if any(entry[28:32]):
                problems.append(where+': bytes 0x1c-0x1f are %s, not zero' % entry[28:32].hex())
            if not entry[0]:
                continue
            name = entry[4:16].rstrip(b'\0 ')
            info = dict(where=where, type=hex(entry[0]), copy_protect=hex(entry[1]),
                        first_block=le16(entry, 2), name=name.decode('latin-1'),
                        time=entry[16:24].hex(), blocks=le16(entry, 24), header=le16(entry, 26),
                        tail=entry[28:32].hex())
            files.append(info)
            if entry[0] not in (0x33, 0xcc):
                problems.append(where+': type %s is neither DATA 0x33 nor GAME 0xcc' % hex(entry[0]))
            if entry[1] not in (0, 0xff):
                problems.append(where+': copy-protect byte %s is neither 0 nor 0xff' % hex(entry[1]))
            if not 1 <= info['blocks'] <= 200:
                problems.append(where+': size %d blocks' % info['blocks'])
            if info['header'] >= max(info['blocks'], 1):
                problems.append(where+': header offset %d is not inside its %d blocks' % (info['header'], info['blocks']))
            if not name or any(c < 32 or c == 127 for c in name):
                problems.append(where+': name %r has no printable characters or a control byte' % name)
            if name in names:
                problems.append(where+': duplicate name %r' % name)
            names.add(name)
            if entry[0] == 0xcc and info['first_block']:
                problems.append(where+': GAME file does not start at block 0')
            chain, at = [], info['first_block']
            for n in range(min(info['blocks'], 200)):
                if at >= 200:
                    problems.append(where+': chain reaches block %d after %d blocks' % (at, n))
                    break
                if at in used:
                    problems.append(where+': block %d already belongs to %s' % (at, used[at]))
                    break
                if entry[0] == 0xcc and at != n:
                    problems.append(where+': GAME file is not contiguous at block %d' % at)
                used[at] = info['name'] or where
                chain.append(at)
                nxt = le16(fat, 2*at)
                if n+1 == info['blocks']:
                    if nxt != END:
                        problems.append(where+': last block %d links to 0x%04x, not the end marker' % (at, nxt))
                else:
                    at = nxt
            info['chain'] = chain
    for n in range(200):
        if n not in used and le16(fat, 2*n) != FREE:
            problems.append('FAT[%d]=0x%04x is allocated but no file owns block %d' % (n, le16(fat, 2*n), n))
    if len(used)+free != 200:
        problems.append('%d owned + %d free blocks is not 200' % (len(used), free))
    return dict(result='OK' if not problems else 'FILESYSTEM', geometry=geometry, files=files,
                free_blocks=free, owned_blocks=len(used), problems=problems)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path)
    args = parser.parse_args()
    print(json.dumps(explain(args.image.read_bytes()), indent=2))


if __name__ == '__main__':
    main()
