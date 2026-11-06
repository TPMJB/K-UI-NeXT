#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the CE continuation's actual package entry and stage control flow.

This test models SH integer register banking rather than treating SR as a
scalar. Original commercial game files are not supplied: the SAMPLE payload
is authored test data and pure-source authentication is instrumented openly.
No console, WinCE, MMU translation, storage timing or gameplay is claimed.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tests'), str(ROOT / 'tools')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_loader_runtime import RuntimeCPU

SAMPLE_ADDRESS, SAMPLE_BYTES = 0x8cf90000, 204576
IP_BEGIN, IP_BYTES = 0x8c008000, 0x4000
GIN_SR = 0x700000f0
MASK = 0xffffffff
SAMPLE_CRC = 0x655ed733


def require(ok, message):
    if not ok:
        raise AssertionError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def address(value):
    return value & 0xdfffffff


class AdoptionCPU(RuntimeCPU):
    """The existing integer decoder plus physical SR.MD/RB bank changes."""
    def __init__(self, ccr=0x100):
        super().__init__(bytes(2048))
        self.original_ccr = ccr
        self.inactive = [0xb0100000 + n * 0x10203 for n in range(8)]
        self.sr = GIN_SR
        self.sr_loads, self.bank_changes = [], []

    @staticmethod
    def bank(sr):
        return bool((sr & 0x40000000) and (sr & 0x20000000))

    def load_sr(self, value, pc):
        old = self.sr
        if self.bank(old) != self.bank(value):
            self.r[:8], self.inactive = self.inactive, self.r[:8]
            self.bank_changes.append((pc, old, value))
        self.sr = value & MASK
        self.sr_loads.append((pc, old, self.sr))

    def plain(self, op, pc):
        n, form = op >> 8 & 15, op & 0xf0ff
        if form == 0x400e:  # LDC Rn,SR: fetch the old-bank operand first.
            value = self.r[n]
            self.load_sr(value, pc)
            self.instructions += 1
        elif form == 0x4007:  # LDC.L @Rn+,SR.
            value = self.read(self.r[n])
            self.r[n] = (self.r[n] + 4) & MASK
            self.load_sr(value, pc)
            self.instructions += 1
        else:
            super().plain(op, pc)


def load_image(cpu, image):
    data, _, segments, _, _ = image
    for segment in segments:
        if segment[0] == 1:
            cpu.store_block(segment[2], data[segment[1]:segment[1] + segment[4]])


def authored_sample():
    return bytes((i * 37 + (i >> 8) * 13 + 0x59) & 255 for i in range(SAMPLE_BYTES))


def crc_collision(data, wanted=SAMPLE_CRC):
    """Give authored data the original CRC, while keeping a different SHA."""
    data = bytearray(data)
    initial = zlib.crc32(data)
    columns = []
    for bit in range(32):
        changed = bytearray(data)
        changed[-4 + bit // 8] ^= 1 << (bit % 8)
        columns.append(zlib.crc32(changed) ^ initial)
    expected = wanted
    wanted ^= initial
    rows = [sum(((columns[c] >> r) & 1) << c for c in range(32)) |
            (((wanted >> r) & 1) << 32) for r in range(32)]
    for column in range(32):
        pivot = next(r for r in range(column, 32) if rows[r] >> column & 1)
        rows[column], rows[pivot] = rows[pivot], rows[column]
        for r in range(32):
            if r != column and rows[r] >> column & 1:
                rows[r] ^= rows[column]
    mask = sum(((row >> 32) & 1) << ((row & MASK).bit_length() - 1) for row in rows)
    for bit in range(32):
        if mask >> bit & 1:
            data[-4 + bit // 8] ^= 1 << (bit % 8)
    require(zlib.crc32(data) == expected, 'Authored CRC collision construction failed')
    return bytes(data)


def manifest_wire():
    wire = bytearray(4096)
    wire[:8] = b'KUIRTI03'
    struct.pack_into('<6I', wire, 8, 3, 4096, 0, 1, 1, 1)
    struct.pack_into('<3Q', wire, 32, 1000000, 2048, 900000)
    struct.pack_into('<4I', wire, 56, 45000, 45016, SAMPLE_BYTES, 0x6789abcd)
    wire[72:72 + 14] = b'AUTHORED SMASH'
    wire[200:200 + 8] = b'MK-51146'
    wire[216:216 + 10] = b'SAMPLE.BIN'
    wire[240:241] = b'U'
    struct.pack_into('<4I', wire, 256, SAMPLE_CRC, 0x18efc93a, 0, 4)
    struct.pack_into('<2I4B', wire, 320, 45000, 46000, 0x84, 1, 0, 0)
    struct.pack_into('<3I', wire, 332, 0, 10000, 4000)
    struct.pack_into('<I', wire, 16, zlib.crc32(wire))
    return bytes(wire)


def continuation_seed(profile, ccr=0x100):
    frame = [40, 0, 0x8c000000, 0x8c00f400, GIN_SR, 0] + [0] * 7
    frame += [0x98760000 + n * 0x12345 for n in range(8)]
    result = bytearray(struct.pack('<40I', 0x5349554b, 0x314e4947, 1, 160,
                                  profile, ccr, *frame, 0, *([0] * 12)))
    struct.pack_into('<I', result, 108, zlib.crc32(result))
    return bytes(result), frame


def captured_child_seed(profile):
    folder = b'VIRTCOP2' if profile == 1 else b'SWIRL'
    ip = folder.ljust(12, b' ') + b'\x01\x04\xa7\xb8'
    data = bytearray(struct.pack('<8I', 0x5349554b, 0x31454353, 1, 64,
                                 profile, 1, 0, 0) + ip + bytes(16))
    struct.pack_into('<I', data, 28, zlib.crc32(data))
    return bytes(data), ip


class StageCPU(AdoptionCPU):
    """Actual stage C, with declared pure-proof and hardware boundaries."""
    def __init__(self, stage, resident, fixture, kind):
        super().__init__()
        self.stage, self.resident, self.fixture, self.kind = stage, resident, fixture, kind
        self.return_controller=stage[4].get('__kui_smash_ce_return_transport')==(1,0,0,0xfff1)
        self.names = {address(value[0]): name for name, value in stage[4].items()}
        self.hashes, self.events, self.hash_updates, self.owner_checks = {}, [], [], []
        self.resident_calls = self.helper_calls = self.storage_calls = 0
        self.stopped = self.resumed = False
        self.stop_message = None
        self.resume_arguments = None
        self.raw_expected = None
        self.source_ip = crc_collision(bytes((i * 19 + 0x35) & 255 for i in range(32768)), 0x18efc93a)
        self.metadata_reads = []

    def read(self, location, count=4):
        if location in {0xa05f8000 + n for n in
                        (0x44, 0x48, 0x50, 0x54, 0x5c, 0xcc, 0xd0,
                         0xd4, 0xd8, 0xdc, 0xe8, 0xec, 0xf0, 0x40)}:
            require(count == 4, 'Unexpected PVR read width')
            self.mmio_reads.append(location)
            return 0
        return super().read(location, count)

    def write(self, location, value, count=4):
        physical = location & 0x1fffffff
        require(not (0x04000000 <= physical < 0x06000000),
                'Successful CE adoption wrote video RAM or PVR hardware')
        super().write(location, value, count)

    def returned(self, value=None):
        if value is not None:
            self.r[0] = value & MASK
        self.pc = self.pr

    def text(self, pointer):
        data = self.block(pointer, 160)
        return data.split(b'\0', 1)[0].decode('ascii')

    def oracle(self, name):
        """No reader/source disk read, BOOT2 or game execution is allowed."""
        if name in ('_kui_retail_bootstrap_enter', '_load_sectors',
                    '_physical_read', '_physical_run', '_kui_retail_storage_read_run'):
            raise AssertionError('CE adoption reached a retired IP/SAMPLE disk-read or BOOT2 path: ' + name)
        if name in ('_stopped', '_relay_stopped'):
            self.stopped, self.stop_message = True, self.text(self.r[4])
            return True
        if name == '_kui_smash_ce_gin_resume':
            self.resumed = True
            self.resume_arguments = tuple(self.r[4:6])
            return True
        if name in ('_kui_retail_crc32', '_kui_stage_crc32'):
            previous, source, count = self.r[4:7]
            self.events.append(dict(kind='CRC', source=hex(source), bytes=count))
            self.returned(zlib.crc32(self.block(source, count), previous))
            return True
        if name == '_memcpy':
            target, source, count = self.r[4:7]
            self.store_block(target, self.block(source, count))
            self.events.append(dict(kind='pure-copy-boundary', source=hex(source),
                                    destination=hex(target), bytes=count))
            self.returned(target)
            return True
        if name == '_memset':
            target, byte, count = self.r[4:7]
            self.store_block(target, bytes([byte & 255]) * count)
            self.returned(target)
            return True
        if name == '_memcmp':
            left, right, count = self.r[4:7]
            a, b = self.block(left, count), self.block(right, count)
            self.returned(0 if a == b else -1 if a < b else 1)
            return True
        if name == '_kui_sha256_init':
            self.hashes[self.r[4]] = hashlib.sha256()
            self.returned()
            return True
        if name == '_kui_sha256_update':
            context, source, count = self.r[4:7]
            require(context in self.hashes, 'SHA update without matching initialization')
            self.hashes[context].update(self.block(source, count))
            self.hash_updates.append((source, count))
            self.returned()
            return True
        if name == '_kui_sha256_digest':
            context, output = self.r[4:6]
            require(context in self.hashes, 'SHA digest without matching initialization')
            self.store_block(output, self.hashes[context].digest())
            self.returned()
            return True
        if name == '_kui_smash_ce_child_owner':
            count, pointer = self.r[4:6]
            digest = self.block(pointer, 32)
            accepted = count == SAMPLE_BYTES and digest == hashlib.sha256(self.fixture).digest()
            self.owner_checks.append(dict(bytes=count, sha256=digest.hex(), accepted=accepted))
            self.returned(accepted)
            return True
        if name == '_kui_native_sdk_sci_family_find':
            source, count, load, out = self.r[4:8]
            require(address(source) == 0x8c010000 and count == SAMPLE_BYTES and load == 0x8c010000,
                    'SDK proof was applied to a different executable span')
            require(self.block(source, count) == self.fixture,
                    'SDK proof preceded complete original source restoration')
            self.store_block(out, struct.pack('<4I', load + 128, load + 256, load + 384, load + 512))
            self.events.append(dict(kind='authored-SDK-boundary'))
            self.returned(1)
            return True
        if name == '_kui_retail_storage_init':
            require(self.r[5] == 1, 'Non-SCI storage initialization')
            self.storage_calls += 1
            self.write(self.r[4], 1)
            self.events.append(dict(kind='storage-init-boundary'))
            self.returned(0)
            return True
        if name == '_kui_retail_storage_blocks':
            self.r[1] = 0
            self.returned(1000000)
            return True
        if name in ('_kui_retail_storage_acquire', '_kui_retail_storage_stop',
                    '_kui_retail_storage_release'):
            self.returned(0)
            return True
        if name == '_kui_retail_image_read':
            lba, count, mode = self.r[5:8]
            out, capacity = self.read(self.r[15]), self.read(self.r[15] + 4)
            require((lba, count, mode, capacity) == (45000, 16, 1, 32768),
                    'CE adoption reread executable bytes or a different IP span')
            require(out == self.stage[4]['_smash_ce_gin_source_ip'][0] and
                    out >= 0x8ce00000 and out + capacity <= SAMPLE_ADDRESS,
                    'Original IP metadata read can overwrite live IP, stack or staged SAMPLE')
            source = self.source_ip
            if self.kind == 'ip_source_crc':
                source = bytes([source[0] ^ 1]) + source[1:]
            self.store_block(out, source)
            self.metadata_reads.append(dict(lba=lba, sectors=count, bytes=capacity,
                                            destination=hex(out)))
            self.returned(0)
            return True
        if name == '_kui_retail_stage_sync':
            # Existing all-RAM publication is separately checked as SH.
            # This test observes its call in the new C adoption path.
            self.events.append(dict(kind='existing-cache-publication-boundary'))
            self.write(0xff00001c, 0x909)
            self.returned()
            return True
        if name == '_vc2_ce_vmu_late_publish':
            require(self.resident_calls == 1 and len(self.owner_checks) == 2,
                    'Helper publication preceded reader adoption or complete SHA owner barriers')
            start = self.stage[4]['__vc2_late_raw_start'][0]
            end = self.stage[4]['__vc2_late_raw_end'][0]
            raw = self.block(start, end - start)
            require(0 < len(raw) <= 512, 'Actual child raw helper escapes declared replacement interval')
            self.store_block(0xac008100, raw)
            self.raw_expected = raw
            self.helper_calls += 1
            self.events.append(dict(kind='late-persist-preparation-boundary', raw_bytes=len(raw)))
            if self.kind == 'ip_changed':
                self.write(0xac0080a1, self.read(0xac0080a1, 1) ^ 1, 1)
            elif self.kind == 'sr_bank_changed':
                self.load_sr(0x500000f0, self.pc)
            self.returned()
            return True
        return False

    def step(self):
        pc = address(self.pc)
        if pc == 0x8c004000:
            require(len(self.owner_checks) == 2 and all(x['accepted'] for x in self.owner_checks),
                    'Low reader initialization preceded complete source ownership')
            self.resident_calls += 1
            source = self.r[4]
            symbols = self.resident[4]
            begin, end = (symbols[n][0] for n in
                ('__retail_resident_bss_begin', '__retail_resident_bss_end'))
            self.store_block(begin, bytes(end - begin))
            target, size = symbols['_manifest'][:2]
            self.store_block(target, self.block(source, size))
            self.events.append(dict(kind='low-resident-init-boundary'))
            self.returned(0)
            return
        name = self.names.get(pc)
        if name and self.oracle(name):
            return
        super().step()


def exercise_stage(entry, stage, resident, fixture, profile, kind,build_id=None):
    from test_smash_menu_loader_runtime import returning_seed,phase_video_seed
    cpu = StageCPU(stage, resident, fixture, kind)
    load_image(cpu, entry)
    load_image(cpu, stage)
    if '__smash_card_session_config' in stage[4]:
        # The GUI normally fills this child-only descriptor before sealing
        # the capsule. Exercise the actual new importer with an authored
        # descriptor and live owner1 receipt. These are authored fixtures;
        # physical admission/rebind remains the declared boundary below.
        require(build_id is not None and len(build_id)==12,'SESSION1 fixture needs compiled build ID')
        config=bytearray(b'KUISCF01'+struct.pack('<6I',1,4224,2+profile,1,295424,0)+
            struct.pack('<8I',1000,577,0,577,1000,584,0,584)+bytes([0x51])*32+
            build_id.encode()+bytes(4+4096+16))
        require(len(config)==4224,'SESSION1 fixture config size differs')
        struct.pack_into('<I',config,28,zlib.crc32(config))
        cpu.store_block(stage[4]['__smash_card_session_config'][0],config)
        binding=hashlib.sha256(b'KUI/SMASH/CARD-SESSION/BINDING/V2\0'+build_id.encode()+
            struct.pack('<2I',1,295424)+config[32:96]).digest()
        admission=hashlib.sha256(b'KUI/SMASH/LIVE-ALLOCATION-ADMISSION/V2\0'+binding+config[64:96]).digest()
        receipt=bytearray(struct.pack('<6I',0x5349554b,0x32305243,2,96,1|(1<<8),1)+
            binding+admission+struct.pack('<2I',1,0))
        if kind=='blank_epoch':receipt=bytearray(receipt[:16]+bytes(80))
        elif kind=='wrong_epoch_owner':struct.pack_into('<I',receipt,16,1|(2<<8))
        if kind!='blank_epoch':struct.pack_into('<I',receipt,92,zlib.crc32(receipt))
        cpu.store_block(0x8c0102a0,receipt)
    seed, frame = continuation_seed(3 - profile if kind == 'wrong_profile' else profile)
    if kind == 'bad_seed':
        seed = seed[:108] + bytes([seed[108] ^ 1]) + seed[109:]
    cpu.store_block(0x8c010200, seed)
    cpu.store_block(entry[4]['__retail_map'][0], manifest_wire())
    child, ip_tail = captured_child_seed(profile)
    cpu.store_block(stage[4]['__smash_ce_child_seed'][0], child)
    root=returning_seed(captured=cpu.return_controller and kind!='configured_root')
    cpu.store_block(stage[4]['__smash_menu_return_seed'][0],root)
    video=phase_video_seed()
    if cpu.return_controller:
        if kind=='bad_root_video':video=video[:20]+bytes([video[20]^1])+video[21:]
        cpu.store_block(stage[4]['__smash_ce_return_video_seed'][0],video)
    ip = bytearray((i * 29 + 0x67) & 255 for i in range(IP_BYTES))
    ip[0xf0:0x100] = ip_tail
    if kind == 'folder_changed':
        ip[0xf0] ^= 1
    cpu.store_block(IP_BEGIN, ip)
    cpu.store_block(0x8c00c000, bytes((i * 7 + 0x91) & 255 for i in range(0x4000)))
    retained_boot2 = cpu.block(0x8c00c000, 0x33ac)
    retained_after_frame = cpu.block(0x8c00f400, 0xc00)
    sample = bytearray(fixture)
    if kind == 'source_crc':
        sample[7] ^= 1
    elif kind == 'source_sha':
        sample[7] ^= 1
        sample = bytearray(crc_collision(sample))
        require(sha(sample) != sha(fixture), 'SHA refusal fixture did not change ownership')
    cpu.store_block(SAMPLE_ADDRESS, sample)
    for pointer in (0x8c0000b0, 0x8c0000b4, 0x8c0000b8, 0x8c0000bc, 0x8c0000e0):
        cpu.write(pointer, 0x8c000100)
    # Compose with the independently executed package entry above: its exact
    # observed C boundary is high SP, unchanged bank and R4=__retail_map.
    cpu.r[4] = entry[4]['__retail_map'][0]
    cpu.r[15], cpu.sr, cpu.pc = 0x8cff0000, GIN_SR | 1, stage[4]['_kui_retail_stage_main'][0]
    initial_low_image = cpu.block(0x8c010000, SAMPLE_BYTES)
    for _ in range(14000000):
        if cpu.stopped or cpu.resumed:
            break
        cpu.step()
    else:
        raise AssertionError('Linked CE stage adoption exceeded its instruction bound')
    successful = kind == 'valid'
    require(cpu.resumed == successful and cpu.stopped != successful,
            'Unexpected linked adoption outcome for ' + kind + ': ' + str(cpu.stop_message))
    if successful:
        if cpu.return_controller:
            prepared=cpu.block(stage[4]['_smash_ce_return_prepared'][0],632)
            require(prepared[:368]==root and prepared[552:632]==video,
                'New CE return preparation did not preserve exact captured ROOT/VID1')
        require(cpu.resume_arguments == (0x8c00f3ac, 0x100), 'Resume arguments differ from captured GIN')
        require(cpu.block(0x8c00f3ac, 84) == struct.pack('<21I', *frame),
                'Adoption replaced actual GIN integer state with a synthetic frame')
        require(cpu.block(0x8c010000, SAMPLE_BYTES) == fixture,
                'Adoption did not retain the entire original staged executable')
        require(cpu.block(IP_BEGIN, 0x100) == ip[:0x100],
                'Adoption modified the live original IP header/folder')
        require(cpu.block(IP_BEGIN + 0x300, 0x3d00) == cpu.source_ip[0x300:0x4000],
                'Adoption retained the outer handoff instead of authenticated original BOOT1')
        require(cpu.block(0x8c00c000, 0x33ac) == retained_boot2 and
                cpu.block(0x8c00f400, 0xc00) == retained_after_frame,
                'Adoption modified retained original BOOT2/stack outside the captured frame')
        require(cpu.raw_expected is not None and
                cpu.block(0xac008100, len(cpu.raw_expected)) == cpu.raw_expected,
                'Matched raw-helper boundary did not publish actual child helper bytes')
        require(cpu.hash_updates == [(SAMPLE_ADDRESS, SAMPLE_BYTES), (0x8c010000, SAMPLE_BYTES)],
                'Source/loaded owner SHA did not cover exactly both complete executable spans')
        require(cpu.storage_calls == cpu.resident_calls == cpu.helper_calls == 1,
                'Successful adoption repeated or omitted a backend ownership boundary')
        require(len(cpu.metadata_reads) == 1,
                'Successful adoption repeated or omitted the bounded original IP metadata read')
        require(not cpu.bank_changes, 'Adoption changed active GIN register bank')
    elif kind in ('bad_seed', 'wrong_profile', 'ip_source_crc', 'source_crc', 'source_sha'):
        require(cpu.resident_calls == cpu.helper_calls == 0,
                'Rejected source/continuation published a new backend')
        require(cpu.block(0x8c010000, SAMPLE_BYTES) == initial_low_image,
                'Rejected source/continuation overwrote the authenticated cold capsule')
    elif kind in ('blank_epoch','wrong_epoch_owner'):
        require(cpu.storage_calls==cpu.resident_calls==cpu.helper_calls==0,
                'Rejected allocation owner reached storage initialization')
    return dict(case=kind, instructions=cpu.instructions, resumed=cpu.resumed,
                stopped=cpu.stopped, stop_message=cpu.stop_message,
                hash_updates=[dict(source=hex(a), bytes=n) for a, n in cpu.hash_updates],
                owner_checks=cpu.owner_checks, events=cpu.events,
                resident_initializations=cpu.resident_calls, late_helper_publications=cpu.helper_calls,
                original_IP_metadata_reads=cpu.metadata_reads,
                SAMPLE_disk_reads=0, BOOT2_entered=False,
                physical_bank_changes=len(cpu.bank_changes),
                original_frame_resumed=successful)


def entry_cpu(entry, stage, pattern=0):
    cpu = AdoptionCPU()
    load_image(cpu, entry)
    symbols = stage[4]
    bss_begin, bss_end = (symbols[n][0] for n in
        ('__retail_stage_bss_begin', '__retail_stage_bss_end'))
    require(bss_begin <= bss_end <= SAMPLE_ADDRESS,
            'CE stage BSS can overwrite the original staged SAMPLE')
    cpu.store_block(bss_begin, bytes([0xa7]) * (bss_end - bss_begin))
    sample = authored_sample()
    cpu.store_block(SAMPLE_ADDRESS, sample)
    ip = bytes((i * 29 + 0x67) & 255 for i in range(IP_BYTES))
    cpu.store_block(IP_BEGIN, ip)
    cpu.r = [((0x10203040 + n * 0x1010101) ^ pattern) & MASK for n in range(16)]
    cpu.r[15] = 0xac00bff0
    cpu.sr, cpu.pc = GIN_SR | (pattern & 1), entry[4]['_start'][0]
    return cpu, sample, ip, bss_begin, bss_end


def exercise_entry(entry, stage, pattern=0, mutate_old_bank_switch=False):
    cpu, sample, ip, begin, end = entry_cpu(entry, stage, pattern)
    if mutate_old_bank_switch:
        # Actual SH negative control: MOV.L @(0,PC),R0; LDC R0,SR;
        # .LONG 0x500000f0. It reproduces the old package's bank-clearing
        # prefix without needing any original game executable.
        cpu.store_block(cpu.pc, struct.pack('<HHI', 0xd000, 0x400e, 0x500000f0))
    target = stage[4]['_kui_retail_stage_main'][0]
    for _ in range(2500000):
        if cpu.pc == target:
            break
        cpu.step()
        require(not cpu.bank_changes,
                'Package entry changed the physical GIN register bank before stage C')
        require((cpu.sr & ~1) == GIN_SR,
                'Package entry changed GIN SR mode/mask bits before stage C')
    else:
        raise AssertionError('Actual package entry did not reach stage C')
    require(cpu.r[4] == entry[4]['__retail_map'][0],
            'Actual entry passed a different manifest wire address')
    require(cpu.r[15] >= 0x8cfe0000 and cpu.r[15] < 0x8d000000,
            'CE continuation lacks the separate high stack')
    require(cpu.block(begin, end - begin) == bytes(end - begin),
            'Actual stage entry did not clear all its BSS')
    require(cpu.block(SAMPLE_ADDRESS, SAMPLE_BYTES) == sample,
            'Actual entry/BSS clearing overwrote the original staged SAMPLE')
    require(cpu.block(IP_BEGIN, IP_BYTES) == ip,
            'Actual entry rewrote retained original IP/bootstrap bytes')
    require(cpu.cache_writes == [0x909], 'Stage entry cache setup changed')
    return dict(pattern=pattern, instructions=cpu.instructions,
                stage_main=hex(cpu.pc), stage_stack=hex(cpu.r[15]),
                sr=hex(cpu.sr), physical_bank_changes=len(cpu.bank_changes),
                SR_loads=[dict(pc=hex(pc), before=hex(a), after=hex(b))
                          for pc, a, b in cpu.sr_loads],
                staged_SAMPLE_preserved=True, retained_IP_preserved=True,
                complete_BSS_cleared=True)


def audit(entry_path, stage_path, resident_path):
    entry, stage = read_elf(entry_path), read_elf(stage_path)
    resident = read_elf(resident_path)
    loaded = AdoptionCPU()
    load_image(loaded, entry)
    physical = sorted((p[3], p[1], p[4]) for p in stage[2] if p[0] == 1 and p[4])
    require(len(physical) == 1, 'CE continuation stage requires one initialized physical image')
    _, offset, count = physical[0]
    blob = entry[4]['__retail_stage_blob_start'][0]
    require(entry[4]['__retail_stage_blob_end'][0] - blob == count and
            loaded.block(blob, count) == stage[0][offset:offset + count],
            'Package entry embeds a different stage than the one exercised by the C flow test')
    require('__kui_smash_ce_gin_adoption' in stage[4],
            'Stage lacks explicit original-GIN adoption; old BOOT2 child is not accepted')
    cases = [exercise_entry(entry, stage, pattern) for pattern in (0, 1, 0x55aa55aa)]
    try:
        exercise_entry(entry, stage, mutate_old_bank_switch=True)
    except AssertionError as error:
        require('physical GIN register bank' in str(error),
                'Old-bank mutation failed for an unrelated reason')
        mutation = str(error)
    else:
        raise AssertionError('Old SR/RB bank-clearing entry mutation escaped the test')
    profile = stage[4]['__kui_smash_ce_child'][0]
    fixture = crc_collision(authored_sample())
    build_id=None
    if '__smash_card_session_config' in stage[4]:
        build_id=json.loads(Path(entry_path).with_name('entry.compile.json').read_text())['commit'][:12]
    kinds=['valid', 'bad_seed', 'wrong_profile', 'ip_source_crc', 'source_crc', 'source_sha',
           'folder_changed', 'ip_changed', 'sr_bank_changed']
    if build_id:kinds+=['blank_epoch','wrong_epoch_owner']
    if '__smash_ce_return_video_seed' in stage[4]:kinds+=['configured_root','bad_root_video']
    stage_cases = [exercise_stage(entry, stage, resident, fixture, profile, kind,build_id) for kind in kinds]
    return dict(schema='kui.smash-ce.gin-adoption.v1', status='PASS',
                entry_sha256=sha(entry[0]), stage_sha256=sha(stage[0]),
                entry_cases=cases, old_bank_switch_mutation_rejected=mutation,
                stage_cases=stage_cases,
                authored_SAMPLE_sha256=sha(fixture), authored_SAMPLE_crc32=hex(SAMPLE_CRC),
                pure_CRC_SHA_copy_compare_and_authored_owner_oracles=True,
                hardware_boundaries=['SCI initialization/card-size response',
                    'SDK family discovery for authored source',
                    'low-resident initialization with actual manifest/BSS geometry',
                    'persistent card preparation and exact linked raw-helper publication',
                    'existing all-RAM resident cache publication',
                    'one bounded original IP metadata read into high temporary RAM'],
                original_game_assets_present=False, hardware_executed=False,
                limits=['Actual linked SH entry and BSS loops execute with physical register banking.',
                        'Authored SAMPLE/IP bytes establish retention/control flow only; they are not original executable owners.',
                        'Pure CRC/SHA use independent host algorithms, and the authored owner accepts only the fixture SHA.',
                        'Resident initialization and persistence hardware boundaries are substituted explicitly, not claimed as live execution.',
                        'No console timing, storage, SDK, MAINANA, MMU translation, WinCE or gameplay claim.'])


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--entry', required=True, type=Path)
    parser.add_argument('--stage', required=True, type=Path)
    parser.add_argument('--resident', required=True, type=Path)
    parser.add_argument('--report', type=Path)
    args = parser.parse_args()
    if not __debug__:
        parser.error('Python assertions must remain enabled')
    result = audit(args.entry, args.stage, args.resident)
    rendered = json.dumps(result, indent=2) + '\n'
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end='')
