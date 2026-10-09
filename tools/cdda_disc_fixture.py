#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Generate the independent six-track GDI fixture for controlled disc tests."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib

SECTOR = 2352
FRAMES_PER_SECTOR = 588
AMPLITUDE = 8192
TRACKS = (
    (1, 0, 4, 2048, 'disc01.bin', 0, 16),
    (2, 20, 0, 2352, 'disc02.raw', 0, 75),
    (3, 45000, 4, 2048, 'disc03.bin', 0, 64),
    (4, 45150, 0, 2352, 'disc04.raw', 512, 150),
    (5, 45300, 0, 2352, 'disc05.raw', 1024, 225),
    (6, 45675, 4, 2352, 'disc06.bin', 0, 32),
)
AUDIO_PERIODS = {2: (64, 96), 4: (100, 150), 5: (80, 120)}
GDI_TEXT = '6\n' + ''.join(f'{n} {lba} {control} {stride} {name} {offset}\n'
                           for n, lba, control, stride, name, offset, _ in TRACKS)
TOY_DESCRIPTOR_BYTES = 451
TOY_DESCRIPTOR_SHA256 = '96e3a54b9aa528c7172121ba3c6cfabf391aacb5a84f292e89843ff2d8853803'
TOY_AUDIO_BYTES = 7222992
TOY_AUDIO_SHA256 = 'ae3d955fc817f433b4c5273581398215e3be5cf2cfd1dee2ec62aaaa677cf338'


def sha(data):
    return hashlib.sha256(data).hexdigest()


def data_byte(track, offset):
    """Track-relative logical 2048-byte payload offset; unsigned32 arithmetic."""
    value = (offset ^ (track * 0x9e3779b9)) & 0xffffffff
    value ^= value >> 16
    value = value * 0x7feb352d & 0xffffffff
    value ^= value >> 15
    value = value * 0x846ca68b & 0xffffffff
    value ^= value >> 16
    return value & 255


def audio_sample(track, frame, channel):
    period = AUDIO_PERIODS[track][channel]
    return AMPLITUDE if frame % period < period // 2 else -AMPLITUDE


def bcd(value):
    if not 0 <= value <= 99:
        raise ValueError('MSF address cannot be represented in BCD')
    return value // 10 * 16 + value % 10


def make_fixture():
    files = {'fixture.gdi': GDI_TEXT.encode('ascii')}
    for number, lba, control, stride, name, offset, sectors in TRACKS:
        data = bytearray(bytes([0xa0 + number]) * offset)
        if control == 0:
            for frame in range(sectors * FRAMES_PER_SECTOR):
                data.extend(struct.pack('<hh', audio_sample(number, frame, 0),
                                        audio_sample(number, frame, 1)))
        else:
            for sector in range(sectors):
                payload = bytes(data_byte(number, sector * 2048 + i) for i in range(2048))
                if stride == 2048:
                    data.extend(payload)
                else:
                    fad = lba + 150 + sector
                    header = (b'\0' + b'\xff' * 10 + b'\0' +
                              bytes((bcd(fad // 4500), bcd(fad // 75 % 60), bcd(fad % 75), 1)))
                    data.extend(header + payload + bytes(SECTOR - 16 - 2048))
        if len(data) != offset + sectors * stride:
            raise ValueError('Generated track size differs from the frozen GDI')
        files[name] = bytes(data)
    return files


def fixture_metadata(files):
    tracks = []
    for number, lba, control, stride, name, offset, sectors in TRACKS:
        entry = {
            'number': number, 'file': name, 'start_lba': lba,
            'start_fad': lba + 150, 'end_fad': lba + 150 + sectors,
            'control': control, 'sector_bytes': stride, 'sectors': sectors,
            'file_offset': offset, 'bytes': len(files[name]),
            'sha256': sha(files[name]), 'crc32': f'{zlib.crc32(files[name]):08x}',
        }
        if control == 0:
            entry.update({'frames': sectors * FRAMES_PER_SECTOR,
                          'sample_rate': 44100, 'channels': 2, 'bits_per_sample': 16,
                          'byte_order': 'little', 'interleaved': True,
                          'wave': 'signed square', 'amplitude': AMPLITUDE,
                          'period_frames_left_right': list(AUDIO_PERIODS[number]),
                          'prefix_byte': 0xa0 + number if offset else None})
        else:
            entry.update({'payload_bytes': sectors * 2048, 'data_offset': 16 if stride == 2352 else 0})
            if stride == 2352:
                entry.update({'raw_mode': 1, 'address': 'BCD actual FAD',
                              'trailing_bytes': 'zero; no EDC/ECC claim'})
        tracks.append(entry)
    return {
        'generated': True, 'owned_game_audio': False,
        'descriptor': {'file': 'fixture.gdi', 'bytes': len(files['fixture.gdi']),
                       'sha256': sha(files['fixture.gdi']),
                       'crc32': f"{zlib.crc32(files['fixture.gdi']):08x}"},
        'tracks': tracks,
        'data_pattern': 'uint32 x = offset ^ (track * 0x9e3779b9); x ^= x >> 16; x *= 0x7feb352d; x ^= x >> 15; x *= 0x846ca68b; x ^= x >> 16; byte = x & 255',
        'data_pattern_offset': 'track-relative logical 2048-byte payload offset',
        'toc_areas': [{'area': 0, 'first_track': 1, 'last_track': 2, 'leadout_fad': 245},
                      {'area': 1, 'first_track': 3, 'last_track': 6, 'leadout_fad': 45857}],
        'toc_scope': 'derived from backed test tracks; no firmware-conformance claim',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output', type=Path)
    parser.add_argument('--toy-descriptor', type=Path,
                        help='copy the exact uploaded Toy GDI metadata; never copies audio')
    args = parser.parse_args()
    try:
        if args.output.is_symlink():
            raise ValueError('Fixture directory must not be a symlink')
        files = make_fixture()
        metadata = fixture_metadata(files)
        if args.toy_descriptor:
            descriptor = args.toy_descriptor.read_bytes()
            if len(descriptor) != TOY_DESCRIPTOR_BYTES or sha(descriptor) != TOY_DESCRIPTOR_SHA256:
                raise ValueError('Toy descriptor differs from the inspected uploaded metadata')
            files['TOY_COMMANDER.gdi'] = descriptor
        args.output.mkdir(parents=True, exist_ok=True)
        files['fixture.json'] = (json.dumps(metadata, indent=2) + '\n').encode()
        for name, data in files.items():
            target = args.output / name
            if target.is_symlink():
                raise ValueError('Fixture output must not overwrite a symlink: ' + name)
            target.write_bytes(data)
        print(json.dumps({'directory': str(args.output.resolve()),
                          'files': {name: {'bytes': len(data), 'sha256': sha(data)}
                                    for name, data in files.items()}}, indent=2))
    except (OSError, ValueError) as error:
        parser.exit(1, 'FAIL: ' + str(error) + '\n')


if __name__ == '__main__':
    main()
