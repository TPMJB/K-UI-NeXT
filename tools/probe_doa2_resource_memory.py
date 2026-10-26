#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay original DOA2 resource-argument arithmetic using external inputs.

This source-only observation never patches the game, reserves memory or enables
the reader. Supply the owner-provided original startup and filesystem metadata
ZIP. No retail instruction bytes or asset payloads are embedded in this tool.
External leaf calls are explicit ABI-return stubs, not game execution proofs.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zipfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_vmu_native_heap_execution import SH, signed

BASE = 0x8c010000
EXPECTED_BYTES = 1815864
EXPECTED_SHA256 = '105056d9ffef86bf5e470fa28dc5907443156f9ab1c6bc009f73661d9c047a85'
SELECTOR_OFFSETS = (0, 10, 2, 12)
# Original call address, descriptor field byte offset, destination word offset.
LOADS = ((0x8c01ead2, 6, 0), (0x8c01eaf8, 4, 20), (0x8c01eb2a, 0, 12),
         (0x8c01eb4c, 8, 8), (0x8c01eb72, 2, None))


class ResourceSH(SH):
    STUBS = {0x8c0145f6, 0x8c0f7e4e, 0x8c01c728, 0x8c015f10}

    def __init__(self, image):
        super().__init__(image)
        self.macl = 0
        self.loads = []

    def read(self, address, count=4):
        if count == 2 and address in self.STUBS:
            if address == 0x8c0145f6:
                self.loads.append((self.pr - 4, self.r[4], self.r[5]))
            self.r[0] = 0  # Explicit leaf-call assumption; no owner work executed.
            return 0x000b
        if count == 2 and address - 2 in self.STUBS:
            return 0x0009
        return super().read(address, count)

    def plain(self, op, pc):
        n, m = (op >> 8) & 15, (op >> 4) & 15
        if op & 0xf00f == 0x0007:  # MUL.L Rm,Rn -> MACL
            self.executing = pc; self.instructions += 1
            self.macl = self.r[n] * self.r[m] & 0xffffffff
        elif op & 0xf0ff == 0x001a:  # STS MACL,Rn
            self.executing = pc; self.instructions += 1
            self.r[n] = self.macl
        elif op & 0xff00 == 0x8500:  # MOV.W @(disp,Rm),R0
            self.executing = pc; self.instructions += 1
            self.r[0] = signed(self.read(self.r[m] + 2 * (op & 15), 2), 16) & 0xffffffff
        elif op & 0xf00f == 0x000e:  # MOV.L @(R0,Rm),Rn
            self.executing = pc; self.instructions += 1
            self.r[n] = self.read(self.r[0] + self.r[m])
        elif op & 0xff00 == 0x8800:
            self.executing = pc; self.instructions += 1
            self.flag(self.r[0] == (signed(op & 255, 8) & 0xffffffff))
        else:
            super().plain(op, pc)


class PartitionSH(ResourceSH):
    def plain(self, op, pc):
        n, m = (op >> 8) & 15, (op >> 4) & 15
        if op & 0xf00f == 0x600c:
            self.executing = pc; self.instructions += 1
            self.r[n] = self.r[m] & 255
        elif op & 0xf00f == 0x0006:
            self.executing = pc; self.instructions += 1
            self.write(self.r[0] + self.r[n], self.r[m])
        elif op & 0xf00f == 0x2000:
            self.executing = pc; self.instructions += 1
            self.write(self.r[n], self.r[m], 1)
        elif op & 0xff00 == 0x8000:
            self.executing = pc; self.instructions += 1
            self.write(self.r[(op >> 4) & 15] + (op & 15), self.r[0], 1)
        elif op & 0xf00f == 0x3003:
            self.executing = pc; self.instructions += 1
            self.flag(signed(self.r[n], 32) >= signed(self.r[m], 32))
        else:
            super().plain(op, pc)


def probe(startup, metadata_zip, full_records=False):
    if not startup.is_file() or startup.stat().st_size != EXPECTED_BYTES:
        raise ValueError('Original startup byte length differs from the reviewed input')
    image = startup.read_bytes()
    digest = hashlib.sha256(image).hexdigest()
    if digest != EXPECTED_SHA256:
        raise ValueError('Original startup SHA256 differs from the reviewed input')
    with zipfile.ZipFile(metadata_zip) as archive:
        info = archive.getinfo('startup-info.json')
        if info.file_size > 2 * 1024 * 1024:
            raise ValueError('Filesystem metadata exceeds the bounded size')
        meta = json.loads(archive.read(info))
    if (meta.get('boot_bytes') != EXPECTED_BYTES or meta.get('boot_sha256') != digest or
            meta.get('title') != 'DEAD OR ALIVE 2'):
        raise ValueError('Filesystem metadata does not identify this exact startup')
    files = {e['path'].upper(): e for e in meta['entries'] if not e['directory']}

    def u16(address):
        return struct.unpack_from('<H', image, address - BASE)[0]

    def u32(address):
        return struct.unpack_from('<I', image, address - BASE)[0]

    def name_at(address):
        assert BASE <= address < BASE + len(image)
        raw = image[address - BASE:].split(b'\0', 1)[0]
        assert 0 < len(raw) <= 1024
        return raw.decode('ascii')

    partitions = []
    for mode, table in enumerate((0x8c1509b4, 0x8c150a5c, 0x8c150af4, 0x8c150b8c)):
        model = PartitionSH(image)
        _, _, steps = model.run(0x8c01b840, [mode], 0x8c00e000)
        slots = [model.read(0x8c21a590 + i * 4) for i in range(38)]
        # Preserve original permutation semantics; the initialization's slot
        # order is independently compared with the immutable source table.
        expected = struct.unpack_from('<38I', image, table - BASE)
        assert sorted(slots) == sorted(expected) and slots[36] == 0x8ce07000
        partitions.append(dict(mode=mode, instructions=steps, slots=slots))

    cases = file_calls = missing_calls = 0
    descriptors, records, spans = [], [], []
    for costume in range(6):
        for character in range(18):
            descriptor = 0x8c151c6c + costume * 216 + character * 12
            fields = []
            for field in range(6):
                index = u16(descriptor + field * 2)
                assert index < 642
                name = name_at(u32(0x8c151264 + index * 4))
                item = files.get(name.removeprefix('c:/').upper())
                fields.append(dict(index=index, name=name,
                    file_bytes=item['bytes'] if item else None,
                    transfer_bytes=(item['bytes'] + 2047) & ~2047 if item else None))
            descriptors.append(dict(costume_row=costume, character_column=character,
                                    descriptor=f'{descriptor:08x}', fields=fields))
            for mode in range(4):
                slots = partitions[mode]['slots']
                for player in range(4):
                    model = ResourceSH(image)
                    for i, value in enumerate(slots):
                        model.write(0x8c21a590 + i * 4, value)
                    offset = SELECTOR_OFFSETS[player]
                    model.write(0x8c21e5f4 + offset, character, 2)
                    model.write(0x8c21e608 + offset, costume, 2)
                    _, _, steps = model.run(0x8c01ea0a, [player], 0x8c00e000)
                    assert len(model.loads) == len(LOADS)
                    observed = []
                    for (pc, destination, filename), (call, field, dest_field) in zip(model.loads, LOADS):
                        assert pc == call
                        index = u16(descriptor + field)
                        assert filename == u32(0x8c151264 + index * 4)
                        slot = player * 7 + dest_field // 4 if dest_field is not None else 28 + player
                        assert destination == slots[slot] | 0xa0000000
                        name = name_at(filename)
                        item = files.get(name.removeprefix('c:/').upper())
                        end = destination + ((item['bytes'] + 2047) & ~2047) if item else None
                        file_calls += 1; missing_calls += item is None
                        if end:
                            spans.append((destination & 0x1fffffff, end & 0x1fffffff))
                        if full_records:
                            observed.append(dict(call=f'{pc:08x}', slot=slot, destination=f'{destination:08x}',
                                file=name, present=item is not None, end=f'{end:08x}' if end else None))
                    cases += 1
                    if full_records:
                        records.append(dict(mode=mode, player=player, character=character,
                                            costume=costume, instructions=steps, loads=observed))
    union = []
    for first, end in sorted(spans):
        if union and first <= union[-1][1]:
            union[-1][1] = max(union[-1][1], end)
        else:
            union.append([first, end])
    result = dict(schema='kui.doa2.resource-selector-argument-execution.v2',
        image_sha256=digest, metadata_zip_sha256=hashlib.sha256(metadata_zip.read_bytes()).hexdigest(),
        entry='8c01ea0a', descriptor_begin='8c151c6c', adjacent_stage_table='8c15217c',
        address_expression='descriptor_begin +216*costume_row +12*character_column',
        dimensions=dict(costume_rows=6, character_columns=18, players=4, partition_modes=4),
        cases=cases, file_calls=file_calls, missing_file_calls=missing_calls,
        original_partition_execution=partitions, resource_descriptors=descriptors,
        conservative_known_character_transfer_union=[[f'{a | 0x80000000:08x}', f'{b | 0x80000000:08x}'] for a,b in union],
        candidate_256k=dict(begin='8cdc0000', end='8ce00000',
            clears_these_raw_transfers=not any(a < 0x0ce00000 and 0x0cdc0000 < b for a,b in spans),
            safe_permanent_interval_proved=False),
        limits=['Dimensions follow the boundary of an adjacent independently referenced stage table; reachable selector ranges are not established.',
                'The cartesian input set is conservative, not proof that every combination is reachable.',
                'Only original filename/destination arithmetic executes. External calls are ABI-return stubs; loading, decoding, relocation and lifecycle are not executed.',
                'Missing filenames are recorded, not treated as proof of unreachable inputs.',
                'No retained-memory reservation, game patch, reader admission or console compatibility follows from this model.'],
        permanent_384k_interval_proved=False, production_changed=False)
    if full_records:
        result['results'] = records
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('startup', type=Path)
    parser.add_argument('metadata_zip', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--full-records', action='store_true', help='Include all 1,728 input cases')
    args = parser.parse_args()
    if args.output.exists():
        parser.error('Output already exists; choose another path')
    result = probe(args.startup, args.metadata_zip, args.full_records)
    args.output.write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps({key: result[key] for key in ('cases', 'file_calls', 'missing_file_calls')}, sort_keys=True))


if __name__ == '__main__':
    main()
