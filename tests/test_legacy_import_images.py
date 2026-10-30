#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Legacy state v1 import against real FAT32/exFAT images, no original writes."""
from pathlib import Path
import hashlib
import shutil
import struct
import tempfile
import zlib
from make_recovery_vectors import sector, mode2_sector
from test_images import run
ROOT = Path(__file__).resolve().parents[1]
CASES = ('clean', 'partial', 'prefix-tail', 'crc-mismatch', 'maps', 'bad-map', 'bad-suspect',
         'baseline', 'baseline-corrupt', 'baseline-nontarget', 'audio-baseline', 'wrong-disc',
         'transient', 'invalid-transient', 'fatal', 'source-unstable', 'missing', 'no-identity', 'guard', 'state-range',
         'state-cooked', 'absent', 'cancel', 'cd-audio', 'cd-mode2', 'cd-silence', 'cd-repeated')

def crc_record(i, start, count, data):
    tag = zlib.crc32(f'{i} {start} {count} 2352'.encode())
    body = f'CRC1 {tag:08x} {len(data)} {zlib.crc32(data):08x}'.encode()
    return b'\n' + body + f' {zlib.crc32(body):08x}\n'.encode()

def fixture(folder, case):
    folder.mkdir()
    cd = case.startswith('cd-')
    tracks = [(1, 150, 4 if not cd or case == "cd-mode2" else 0, 40 if not cd else 4),
              (2, 340 if not cd else 154, 0, 4)]
    if not cd:
        tracks.append((3, 45150, 4, 40))
    state = f'DreamShell GD Ripper state v1\ndisc_type {0 if cd else 128}\nuse_bin {0 if case == "state-cooked" else 1}\ntracks {len(tracks)}\n'
    content = []
    for i, start, control, count in tracks:
        data = b''.join((mode2_sector if case == 'cd-mode2' else sector)(fad, fad) for fad in range(start, start + count)) if control else bytes((j * 31 + i) & 255 for j in range(count * 2352))
        if case == "cd-silence":data=bytes(count*2352)
        if case == "cd-repeated":data=bytes(j&255 for j in range(2352))*count
        content.append(data)
        name = f'track{i:02d}.{"bin" if control else "raw"}'
        declared = count - 2 if cd and control == 4 else count
        state += f'{i} {start} {declared + (1 if case == "state-range" and i == 3 else 0)} {control} 2352 {name}\n'
        (folder / ('optical' + name[5:])).write_bytes(data)
        saved = data[:20 * 2352] if case == 'partial' and i == 3 else data[:declared * 2352]
        if case not in ('missing', 'no-identity') or i != 3:
            (folder / name).write_bytes(saved)
        checkpoint = saved[:20 * 2352] if case == 'prefix-tail' and i == 3 else saved
        (folder / (name + '.crc')).write_bytes(crc_record(i, start, declared, checkpoint))
    if case in ('missing', 'no-identity'):
        (folder / 'track03.bin.crc').unlink()
    if case != 'absent':
        (folder / 'rip.state').write_text(state)
    if not cd:
        (folder / 'alternate.bin').write_bytes(sector(45150, 1))
        if case != 'no-identity':
            (folder / 'rip.disc').write_bytes(content[2][16:2064])
    if case == 'crc-mismatch':
        saved = bytearray((folder / 'track03.bin').read_bytes()); saved[80] ^= 1
        (folder / 'track03.bin').write_bytes(saved)
    if case in ('maps', 'bad-map'):
        (folder / 'track02.raw.bad').write_text('track,track_sector,disc_lba,disc_fad,file_offset\n' + ('2,1,191,341,2352\n' if case == 'maps' else '2,1,191,342,2352\n'))
        (folder / 'track03.bin.suspect').write_text('2 45152 4\n')
    if case == 'bad-suspect':
        (folder / 'track03.bin.suspect').write_text('40 45190 4\n')
    if case.startswith('baseline') or case == 'audio-baseline':
        ti, index = (1, 1) if case == 'audio-baseline' else (2, 2)
        i, start, control, count = tracks[ti]
        name = f'track{i:02d}.{"bin" if control else "raw"}'
        initial = bytearray(content[ti]); initial[index * 2352:(index + 1) * 2352] = bytes(2352)
        tag = zlib.crc32(f'{i} {start} {count} 2352'.encode())
        header = b'GDRCV1\r\n' + struct.pack('<7I', tag, start, count, control, 2352, 1, zlib.crc32(initial))
        record = struct.pack('<2I', index, zlib.crc32(bytes(2352)))
        base = header + record
        (folder / (name + '.recovery-base')).write_bytes(base + struct.pack('<I', zlib.crc32(base) ^ (1 if case == 'baseline-corrupt' else 0)))
        (folder / (name + '.crc')).write_bytes(crc_record(i, start, count, initial))
        if case == 'baseline-nontarget':
            changed = bytearray(content[ti]); changed[7 * 2352 + 40] ^= 1
            (folder / name).write_bytes(changed)
    if case == 'guard':
        (folder / 'readback.unstable').write_text('legacy instability\n')

def main():
    with tempfile.TemporaryDirectory(prefix='kui-legacy-import-') as temp:
        base = Path(temp)
        for kind in ('fat32', 'exfat'):
            seed = base / f'{kind}-seed.img'
            with seed.open('wb') as stream:
                stream.truncate(96 * 1024 * 1024)
            run('mkfs.fat', '-F', '32', str(seed)) if kind == 'fat32' else run('mkfs.exfat', str(seed))
            for case in CASES:
                folder = base / f'{kind}-{case}'; fixture(folder, case)
                image = base / f'{kind}-{case}.img'; shutil.copyfile(seed, image)
                assert 'PASS legacy import' in run(str(ROOT / 'build/legacy-import-image'), str(image), str(folder), case)
                run('fsck.fat' if kind == 'fat32' else 'fsck.exfat', '-n', str(image))
                image.unlink(); shutil.rmtree(folder)
                print(f'PASS {kind} legacy import: {case}', flush=True)
if __name__ == '__main__':
    main()
