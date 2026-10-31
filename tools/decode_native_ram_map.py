#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Decode the RAM row (RAM UNUSED in older builds) of the standalone RAM-map test's return screen.

Give the four hex words in the order shown. Bit i of word w is the 128 KiB
page at 0x8c000000 + (32*w + i) * 0x20000; a set bit means every word of that
page still held the stage's pattern when the game returned. A clear bit means
the game wrote the page, or the stage never marked it: everything below the
executable's end, the stage's own pages from 0x8ce00000 and the last page.
"""
import argparse
import json

BEGIN = 0x8c000000
PAGE = 0x20000
LEASE = 384*1024


def unused_ranges(words):
    if len(words) != 4:
        raise ValueError('The row has exactly four words')
    pages = [bool(words[i//32] >> (i % 32) & 1) for i in range(128)]
    ranges, start = [], None
    for i, free in enumerate(pages+[False]):
        if free and start is None:
            start = i
        elif not free and start is not None:
            ranges.append((BEGIN+start*PAGE, BEGIN+i*PAGE))
            start = None
    return ranges


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('words', nargs=4, type=lambda text: int(text, 16))
    args = parser.parse_args()
    ranges = unused_ranges(args.words)
    print(json.dumps(dict(
        unused=[dict(begin=f'0x{a:08x}', end=f'0x{b:08x}', kib=(b-a)//1024) for a, b in ranges],
        holds_384k_lease=[f'0x{a:08x}-0x{b:08x}' for a, b in ranges if b-a >= LEASE],
        note='One session only: a page this game writes in another mode reads used there.'), indent=2))


if __name__ == '__main__':
    main()
