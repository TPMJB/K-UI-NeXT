#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Actual native proof sequence through real FatFs on disposable FS images."""
from pathlib import Path
import hashlib
import os
import re
import shutil
import subprocess
import tempfile
import struct
import zlib
ROOT = Path(__file__).resolve().parents[1]
BINARY = ROOT / 'build/vmu-harness/host-sequence'

def run(*arguments):
    result = subprocess.run([str(value) for value in arguments], text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                            env={**os.environ, "ASAN_OPTIONS": "detect_leaks=0"})
    if result.returncode:
        raise RuntimeError(f'{arguments}: exit {result.returncode}\n{result.stdout}')
    return result.stdout

def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').digest()

def exported_profile(image, output, partition_first=0):
    match = re.search(r'^PROFILE-PATH (.+)\nPROFILE-WIRE ([0-9a-f]+)$', output, re.MULTILINE)
    assert match, 'export did not report its persisted profile bytes/path'
    path, wire = match[1], bytes.fromhex(match[2])
    assert len(wire) == 1536 and wire[:8] == b'KUIVMUR1'
    assert struct.unpack_from('<II', wire, 8) == (1, 1536)
    crc_input = wire[:16] + bytes(4) + wire[20:]
    assert struct.unpack_from('<I', wire, 16)[0] == zlib.crc32(crc_input)
    assert struct.unpack_from('<III', wire, 20) == (1, 131072, 1)
    capacity, first, end = struct.unpack_from('<QQQ', wire, 32)
    assert capacity == image.stat().st_size // 512
    assert first == partition_first and first < end <= capacity
    assert wire[56:72] == bytes.fromhex('123456789abcdef013579bdf2468ace0')
    assert wire[72:84] == b'KUI-VMU-PB01'
    source = struct.unpack_from('<I', wire, 84)[0]
    assert f'/p{source:04d}/' in path
    count = struct.unpack_from('<I', wire, 120)[0]
    assert 1 <= count <= 16 and not any(wire[124:128]) and not any(wire[1344:])
    image_bytes = bytearray()
    previous_end = None
    with image.open('rb') as stream:
        for index in range(count):
            logical, lba, blocks = struct.unpack_from('<III', wire, 128 + index * 12)
            assert logical == len(image_bytes) // 512 and blocks > 0
            assert first <= lba < lba + blocks <= end and lba != previous_end
            stream.seek(lba * 512)
            data = stream.read(blocks * 512)
            assert len(data) == blocks * 512
            image_bytes.extend(data)
            previous_end = lba + blocks
    assert len(image_bytes) == 131072
    assert not any(wire[128 + count * 12:320])
    for block in range(256):
        assert struct.unpack_from('<I', wire, 320 + block * 4)[0] == zlib.crc32(image_bytes[block * 512:(block + 1) * 512])
    assert hashlib.sha256(image_bytes).digest() == wire[88:120]
    assert image_bytes[255 * 512:255 * 512 + 16] == b'\x55' * 16
    assert image_bytes[253 * 512 + 4:253 * 512 + 12] == b'KUIPROOF'
    assert struct.unpack_from('<I', image_bytes, 198 * 512 + 168)[0] == source
    return path, wire, bytes(image_bytes)

def profile_sequence(image, partition_first=0):
    first_path, first_wire, first_card = exported_profile(image, run(BINARY, image, 'export'), partition_first)
    second_path, second_wire, second_card = exported_profile(image, run(BINARY, image, 'export'), partition_first)
    assert first_path.endswith('retail-readonly-profile-0001.bin')
    assert second_path.endswith('retail-readonly-profile-0002.bin') and first_path != second_path
    assert first_wire == second_wire and first_card == second_card, 'export changed source image/mapping'
    for refusal in ('refuse-export-scif', 'refuse-export-raw', 'refuse-export-identity'):
        before = digest(image)
        run(BINARY, image, refusal)
        assert digest(image) == before, f'{refusal} changed storage'

def main():
    for command in ('mkfs.fat', 'fsck.fat', 'mkfs.exfat', 'fsck.exfat'):
        if not shutil.which(command):
            raise SystemExit(f'Missing test prerequisite: {command}')
    with tempfile.TemporaryDirectory(prefix='kui-native-vmu-') as temp:
        base = Path(temp)
        for kind in ('fat32', 'exfat'):
            image = base / f'{kind}.img'
            with image.open('wb') as stream:
                stream.truncate(96 * 1024 * 1024)
            run('mkfs.fat', '-F', '32', image) if kind == 'fat32' else run('mkfs.exfat', image)
            for mode in ('create', 'verify', 'repeat', 'verify'):
                assert 'PASS native harness host sequence' in run(BINARY, image, mode)
            profile_sequence(image)
            checker = 'fsck.fat' if kind == 'fat32' else 'fsck.exfat'
            run(checker, '-n', image)
            for corruption in ('bad-identity', 'short-journal', 'bad-vmu-geometry', 'orphan-vmu-chain', 'bad-vms-crc'):
                bad = base / f'{kind}-{corruption}.img'
                shutil.copyfile(image, bad)
                run(BINARY, bad, corruption)
                before = digest(bad)
                run(BINARY, bad, 'refuse-verify')
                assert digest(bad) == before, 'refused pair changed storage'
                run(BINARY, bad, 'refuse-export')
                assert digest(bad) == before, 'refused profile changed storage'
                run(checker, '-n', bad)
            print(f'PASS {kind}: persistence, unique profile exports, exact raw sector maps/CRC/SHA, unchanged source, identity/I/O/refusal guards, fsck', flush=True)
            # The same physical-map proof must include a real partition offset,
            # rather than relying on the superfloppy's zero sector base.
            partitioned = base / f'{kind}-partitioned.img'
            first = 2048
            sector_zero = bytearray(512)
            sector_zero[446 + 4] = 0x0c if kind == 'fat32' else 0x07
            struct.pack_into('<II', sector_zero, 446 + 8, first, image.stat().st_size // 512)
            sector_zero[510:512] = b'\x55\xaa'
            with partitioned.open('wb') as stream:
                stream.write(sector_zero)
                stream.seek(first * 512)
                with image.open('rb') as source:
                    shutil.copyfileobj(source, stream)
            # This copied filesystem already holds profiles 1 and 2. A fresh
            # logical probe prevents old absolute-map files being reused.
            run(BINARY, partitioned, 'create')
            profile_sequence(partitioned, first)
            extracted = base / f'{kind}-partition-check.img'
            with partitioned.open('rb') as source, extracted.open('wb') as destination:
                source.seek(first * 512)
                shutil.copyfileobj(source, destination)
            run(checker, '-n', extracted)
            print(f'PASS {kind}: nonzero partition offset included in every profile LBA', flush=True)
        image = base / 'fat16-refusal.img'
        with image.open('wb') as stream:
            stream.truncate(32 * 1024 * 1024)
        run('mkfs.fat', '-F', '16', image)
        before = digest(image)
        run(BINARY, image, 'refuse-create')
        assert digest(image) == before, 'unsupported FAT16 changed storage'
        run('fsck.fat', '-n', image)
        print('PASS FAT16: refused without changing image', flush=True)

if __name__ == '__main__':
    main()
