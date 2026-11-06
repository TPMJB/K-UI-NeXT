#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the linked production menu loader with explicit raw-card hardware.

All loader C and assembly instructions execute from the supplied ELF. Only
the fixed low512-byte raw-card shim is substituted by a bounded four-block-per-call
card fixture. DMA/MMU status and CCR are modeled, while OCBP is counted with
no timing/cache effect. This establishes sequencing and write ownership.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "tests"), str(ROOT / "tools")]
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_loader_entry import LoaderCPU, ENTRY, TARGET, CCR

RAW = 0xac008100
GIN, CONTEXT, IP = 0xacfd0000, 0xac19ee60, 0xac0080f0
FIRMWARE_ADDRESSES = [0xac0000bc, 0xac0000c0, 0xac0000e0,
                      0xac001000, 0xac001004, 0xac001008,
                      0xac0010f0, 0xac0010f4, 0xac0010f8]
FIRMWARE = [0x8c000100, 0x8c000120, 0xa00000e0,
            0x12345678, 0x23456789, 0x3456789a, 0x456789ab, 0x56789abc, 0x6789abcd]
MMIO_ZERO = {0xff000010, 0xa05f7418, 0xa05f6c14, 0xa05f6c18, 0xa05f6808}
MMIO_ZERO |= {0xffa0000c + 16 * i for i in range(4)}
MMIO_ZERO |= {0xa05f7800 + 32 * i + n for i in range(4) for n in (20, 24)}


def crc(data):
    return zlib.crc32(data) & 0xffffffff


def session_flow(symbols):
    marker=symbols.get('__kui_smash_card_session_flow')
    assert marker is None or marker==(1,0,0,0xfff1), 'Invalid collection-session ABI marker'
    return marker is not None


def card_receipt(owner=1,sequence=1):
    """Authored live receipt; no physical card or admission claim."""
    assert 1<=owner<=4 and 1<=sequence<=0xffffffff
    wire=bytearray(struct.pack('<6I',0x5349554b,0x32305243,2,96,1|(owner<<8),7)+
        bytes((i*13+0x51)&255 for i in range(64))+struct.pack('<2I',sequence,0))
    struct.pack_into('<I',wire,92,crc(wire))
    return bytes(wire)


PHASE_TRACE_OFFSET, PHASE_TRACE_BYTES = 736, 992
PHASE_VIDEO_OFFSET, PHASE_VIDEO_BYTES = PHASE_TRACE_OFFSET+PHASE_TRACE_BYTES, 80
CE_GIN_OFFSET, CE_GIN_BYTES, CE_GIN_CRC_OFFSET = 0x200, 160, 108
PHASE_VIDEO_REGS = [0x800005,9,0,0,319|(479<<10)|(1<<20),0x150104,
                    0x100,0x7e0345,0x240204,0x20c0359,0,172,0x280028,0]


def phase_video_seed(regs=PHASE_VIDEO_REGS):
    data=bytearray(struct.pack('<4I',0x5349554b,0x31444956,1,80)+struct.pack('<14I',*regs)+bytes(8))
    struct.pack_into('<I',data,72,crc(data))
    return bytes(data)


def native_layout(session=False):
    # SESSION's fixed receipt occupies672..768. Keep the complete native
    # seed/return/TIM1/VID1 sequence after it; imported legacy offsets remain.
    seed=768 if session else 64
    returning=seed+304
    trace=returning+368
    video=trace+PHASE_TRACE_BYTES
    return seed,returning,trace,video,3072 if session else 2048


def make_capsule(phase=False,profile=0,ce_return=False,native_session=False):
    seed,returning,trace,video,size=native_layout(native_session and not profile)
    data = bytearray((i * 17 + 0x53) & 255 for i in range(size))
    if profile:
        at=768 if ce_return else 64
        data[at:at+64] = struct.pack('<8I',0x5349554b,0x31454353,1,64,profile,0,0,0)+bytes(32)
        data[at+64:at+432] = struct.pack('<8I',0x5349554b,0x3154524d,1,368,0,0,0,0)+bytes(336)
        data[0x1e0:0x1f0]=bytes(16)
        if ce_return:
            data[0x1e0:0x1f0]=struct.pack('<4I',0x5349554b,0x31524543,1,16)
            data[at+432:at+512]=struct.pack('<4I',0x5349554b,0x31444956,1,80)+bytes(64)
        # Current CE capsules carry the blank continuation consumed only
        # after their complete immutable payload CRC has been checked.
        data[CE_GIN_OFFSET:CE_GIN_OFFSET+CE_GIN_BYTES] = \
            struct.pack('<5I',0x5349554b,0x314e4947,1,CE_GIN_BYTES,profile)+bytes(140)
        return bytes(data)
    if native_session:
        # This authored fixture exercises the legacy cold-entry branch.
        # Moving its seeds must leave both fixed adoption declarations blank.
        data[0x1e0:0x1f0]=bytes(16)
        data[CE_GIN_OFFSET:CE_GIN_OFFSET+CE_GIN_BYTES]=bytes(CE_GIN_BYTES)
        assert seed>=0x300 and returning==seed+304 and trace==returning+368
    data[seed:seed+304] = struct.pack("<8I", 0x5349554b, 0x31554e4d, 1, 304, 0, 0, 0, 0) + bytes(272)
    data[returning:returning+368] = struct.pack("<8I", 0x5349554b, 0x3154524d, 1, 368, 0, 0, 0, 0) + bytes(336)
    if phase:
        data[trace:trace+PHASE_TRACE_BYTES] = \
            struct.pack("<8I", 0x5349554b, 0x314d4954, 1, PHASE_TRACE_BYTES, 0, 0, 0, 0) + bytes(PHASE_TRACE_BYTES-32)
        data[video:video+PHASE_VIDEO_BYTES] = \
            struct.pack('<4I',0x5349554b,0x31444956,1,80)+bytes(64)
    return bytes(data)


def returning_seed(captured=False,argv=None,ip=None):
    # A disjoint outer capsule plan. Its payload is not read by this model;
    # the native-return gate has its own complete execution fixture.
    plan = [0x5349554b, 0x31444c4d, 1, 64,
            2048, 2048, 0xface1234, 128, 300, 4, 0, 4, 0, 0, 0, 0]
    plan[15] = crc(struct.pack('<16I', *plan))
    data = bytearray(struct.pack('<8I', 0x5349554b, 0x3154524d, 1, 368,
                               3 if captured else 1, 0, 0, 0) +
                     struct.pack('<16I', *plan) + bytes(272))
    if captured:
        data[96:352] = b'\\manifest.bin\0'.ljust(256, b'\0') if argv is None else argv
        data[352:368] = b' ' * 12 + b'\x01\x04\xa7\xb8' if ip is None else ip
    struct.pack_into('<I', data, 28, crc(data))
    return bytes(data)


def original_firmware(kind):
    words = list(FIRMWARE)
    if kind == "phantasy_star_ii_c0_zero":
        words[1] = 0
    elif kind == "phantasy_star_ii_c0_rom":
        words[1] = 0xa00000e0
    return words


class RuntimeCPU(LoaderCPU):
    def __init__(self, capsule, raw_busy=False):
        super().__init__(0x909)
        self.capsule, self.raw_busy = capsule, raw_busy
        self.raw_calls, self.purges = [], 0
        self.cache_writes = []
        self.phase_counter = 0
        self.raw_map={100:0,101:1,200:2,201:3}

    def read(self, address, count=4):
        # Diagnostic clocks are observations only. Their actual bounded MMIO
        # implementation has an independent model; synthetic advancement here
        # lets all ordinary loader refusal/publication cases execute unchanged.
        if address in (0xff000084,0xffc00000):
            assert count == 2
            self.mmio_reads.append(address)
            return 0xc023 if address == 0xff000084 else 0xe0a
        if address in (0xff100004,0xff100008,0xa0710000,0xa0710004,0xa05f688c):
            assert count == 4
            self.mmio_reads.append(address)
            if address == 0xff100008:
                self.phase_counter += 1000000
                return self.phase_counter
            return 0x1234 if address == 0xa0710000 else 0x5678 if address == 0xa0710004 else 0
        if address in MMIO_ZERO:
            assert count == 4
            self.mmio_reads.append(address)
            return 0
        return super().read(address, count)

    def write(self, address, value, count=4):
        if address == CCR:
            assert count == 4
            self.original_ccr = value & ~0x808
            self.cache_writes.append(value)
            return
        super().write(address, value, count)

    def plain(self, op, pc):
        if op & 0xf00f == 0x200e:  # MULU.W Rm,Rn (bounded trace index * 40).
            n, m = op >> 8 & 15, op >> 4 & 15
            self.macl = (self.r[n] & 0xffff) * (self.r[m] & 0xffff)
            self.instructions += 1
        elif op in (0x0008, 0x0018):  # CLRT / SETT.
            self.flag(op == 0x0018)
            self.instructions += 1
        elif op & 0xf0ff == 0x00a3:  # OCBP @Rn; timing/cache explicitly unmodeled.
            self.purges += 1
            self.instructions += 1
        elif op & 0xf00f == 0x300e:  # ADDC Rm,Rn (the C extent proof uses uint64_t).
            n, m = op >> 8 & 15, op >> 4 & 15
            value = self.r[n] + self.r[m] + (self.sr & 1)
            self.r[n] = value & 0xffffffff
            self.flag(value >> 32)
            self.instructions += 1
        elif op & 0xf00f == 0x300a:  # SUBC Rm,Rn.
            n, m = op >> 8 & 15, op >> 4 & 15
            value = self.r[n] - self.r[m] - (self.sr & 1)
            self.r[n] = value & 0xffffffff
            self.flag(value < 0)
            self.instructions += 1
        else:
            super().plain(op, pc)

    def raw_read(self):
        lba, destination, count = self.r[4], self.r[5], self.r[6]
        self.raw_calls.append((lba, destination, count))
        result = 3 if self.raw_busy else 0
        if not result:
            mapping = self.raw_map
            assert count and count <= 4
            for n in range(count):
                assert lba+n in mapping
                block = self.capsule[mapping[lba+n] * 512:(mapping[lba+n] + 1) * 512]
                for offset in range(0, 512, 4):
                    self.write(destination+n*512+offset, int.from_bytes(block[offset:offset + 4], "little"))
        self.r[0] = result
        self.pc = self.pr


def prepare(image, segments, symbols, kind,sample=None):
    session=session_flow(symbols)
    profile=(2 if 'swirl' in kind else 1) if kind.startswith('ce_') else 0
    ce_return=profile and 'return' in kind
    expected = make_capsule("_kui_smash_menu_loader_phase_trace" in symbols,2 if kind=='ce_wrong_profile' else profile,ce_return,session)
    if kind in ('ce_return_bad_video','ce_return_bad_capability'):
        changed=bytearray(expected)
        changed[768+432+16 if kind=='ce_return_bad_video' else 0x1e8]=2
        expected=bytes(changed)
    actual = bytearray(expected)
    if kind in ("bad_capsule_crc","ce_bad_capsule_crc"):
        actual[1700] ^= 1
    cpu = RuntimeCPU(bytes(actual), kind == "raw_probe_busy")
    for segment in segments:
        if segment[0] == 1:
            cpu.store_block(segment[2], image[segment[1]:segment[1] + segment[4]])
            if segment[5] > segment[4]:
                cpu.store_block(segment[2] + segment[4], bytes(segment[5] - segment[4]))
    begin = symbols["__smash_menu_loader_begin"][0]
    end = symbols["__smash_menu_loader_binary_end"][0]
    runtime = symbols["_kui_smash_menu_loader_runtime"][0]
    runtime_bytes = symbols["_kui_smash_menu_loader_runtime"][1]
    assert runtime_bytes == 632+96*session and begin <= runtime < runtime + runtime_bytes <= end
    initialized = bytearray(cpu.block(begin, end - begin))
    initialized[runtime - begin:runtime - begin + runtime_bytes] = bytes(runtime_bytes)
    raw_image = bytes((i * 29 + 7) & 255 for i in range(512))
    cpu.store_block(RAW, raw_image)
    plans=[]
    for index,(first,second) in enumerate([(100,200),(400,500),(600,700)]):
        payload=expected if index==profile else make_capsule(False,index,native_session=session)
        menu_offset=768 if (index==0 and session) or (index==profile and ce_return) else 64
        seed = [0x5349554b, 0x31444c4d, 1, 64,
                len(payload), len(payload), crc(payload), menu_offset, first, 2, second, len(payload)//512,
                crc(initialized), crc(raw_image), 0, 0]
        seed[15] = crc(struct.pack("<16I", *seed));plans+=seed
        if index==profile:cpu.raw_map={first:0,first+1:1,**{second+n-2:n for n in range(2,len(payload)//512)}}
    collection=[0x5349554b,0x31434d53,1,224,3,7]+plans+[0,0]
    collection[-1]=crc(struct.pack('<56I',*collection))
    runtime_words = collection + original_firmware(kind) + [0xac007000]
    returning = bytearray(returning_seed(kind == 'phantasy_star_ii_after_menu_return' or
        (ce_return and kind!='ce_return_no_root')))
    if kind == 'invalid_return_plan':
        returning[36] ^= 1
    cpu.store_block(runtime, struct.pack("<" + str(len(runtime_words)) + "I", *runtime_words) + returning+
        (card_receipt() if session else b''))
    if '_kui_smash_menu_loader_phase_video' in symbols:
        cpu.store_block(symbols['_kui_smash_menu_loader_phase_video'][0],phase_video_seed())
    if profile:
        assert sample is not None and len(sample)==204576
        assert hashlib.sha256(sample).hexdigest()=='82bb1160814548f4656b273353fd6168a691fb5f215b77377860ff9fd2156c8a'
        source=bytearray(sample)
        if kind=='ce_bad_sample_crc':source[1000]^=1
        cpu.store_block(TARGET,source)
    else:cpu.store_block(TARGET, bytes([0xc7]) * len(expected))
    for address in FIRMWARE_ADDRESSES:
        cpu.store_block(address, struct.pack("<I", 0xdeadbeef))
    words = [0x47494e53, 1, 0x3ac59e76, 8192, 0x8ce00000, 903, 4, 1]
    if profile:words[4:6]=[0x8cf90000,100]
    cpu.store_block(GIN, struct.pack("<8I", *words))
    cpu.store_block(CONTEXT, struct.pack("<8I", *words))
    folder, rom, sound, selection = ("SHFORCE", "SHFORCE.SGA", 1, 6) if kind == "shining_force" else ("PHSTAR2", "PHSTAR2.SGA", 0, 4)
    argv = (b"\0".join(s.encode() for s in ["\\MDE_US.BIN", rom, "MODE0", "SKIP0", "SOUND" + str(sound)]) + b"\0\0").ljust(256, b"\0")
    if kind == "malformed_argv":
        argv = b"X" + argv[1:]
    ip = folder.encode().ljust(12, b" ") + b"\x01\x04\xa7\xb8"
    if profile:
        folder='SWIRL' if profile==2 else 'VIRTCOP2'
        argv=('\\'+folder+'\\SAMPLE.BIN').encode().ljust(256,b'\0')
        ip=folder.encode().ljust(12,b' ')+b'\x01\x04\xa7\xb8'
        if kind=='ce_unknown_fallback':argv=b'X'+argv[1:]
        cpu.store_block(GIN+256,argv)
    cpu.store_block(CONTEXT + 256, argv)
    cpu.store_block(IP, ip)
    cpu.r = [0xa5a50000 + i for i in range(8)] + [0] * 7 + [0x8c00f400]
    cpu.sr, cpu.pr, cpu.mach, cpu.macl = 0x700000f0, 0, 0, 40
    cpu.gbr, cpu.vbr, cpu.pc = 0x8c000000, 0x8c00f400, ENTRY
    if kind == "nonnative_fallback":
        cpu.store_block(GIN + 16, struct.pack("<I", 0x8cfa0000))
        cpu.r[:15] = [0x12340000 + i * 0x1111 for i in range(15)]
    original = (list(cpu.r), cpu.sr, cpu.pr, cpu.mach, cpu.macl, cpu.gbr, cpu.vbr)
    return cpu, expected, argv, ip, selection, original


def exercise(image, segments, symbols, kind,sample=None):
    cpu, expected, argv, ip, selection, original = prepare(image, segments, symbols, kind,sample)
    diagnostic = symbols["_kui_smash_menu_loader_diagnostic"][0]
    reason = 0
    for _ in range(14000000):
        if cpu.pc in (RAW,RAW-0x20000000):
            cpu.raw_read()
        if cpu.pc == TARGET:
            break
        if cpu.pc == 0xac007000:
            assert cpu.read(diagnostic) == 0x4d4c4453
            reason = cpu.read(diagnostic + 4)
            assert cpu.r[4] == reason and cpu.r[5] == cpu.read(diagnostic + 8)
            break
        cpu.step()
    else:
        raise AssertionError("Actual production loader exceeded its instruction bound")
    result = dict(case=kind, instructions=cpu.instructions, reason=reason,
                  raw_calls=[dict(lba=lba, destination=hex(destination),blocks=count)
                             for lba,destination,count in cpu.raw_calls],
                  ram_line_purges=cpu.purges, ccr_writes=[hex(x) for x in cpu.cache_writes])
    firmware = [cpu.read(address) for address in FIRMWARE_ADDRESSES]
    seed_at,return_at,trace_at,video_at,_=native_layout(session_flow(symbols))
    native_raw_calls=[(100,1),(101,1),(200,len(expected)//512-2)]
    if kind.startswith("phantasy_star_ii") or kind == "shining_force":
        assert reason == 0 and cpu.pc == TARGET
        assert [(lba,count) for lba,_,count in cpu.raw_calls] == native_raw_calls
        assert cpu.cache_writes == [0x808,0x908,0x808,0x908,0x808,0x908,0x808]
        assert firmware == original_firmware(kind)
        seed = cpu.block(TARGET + seed_at, 304)
        header = struct.unpack_from("<8I", seed)
        assert header[:7] == (0x5349554b, 0x31554e4d, 1, 304, selection, 0, 0)
        assert seed[32:288] == argv and seed[288:] == ip
        normalized = bytearray(seed)
        normalized[28:32] = bytes(4)
        assert crc(normalized) == header[7]
        carried = cpu.block(TARGET + return_at, 368)
        assert carried == returning_seed(), 'A new native child must clear the preceding menu return arguments'
        phase = "_kui_smash_menu_loader_phase_trace" in symbols
        complete=bytearray(expected)
        complete[seed_at:seed_at+304]=seed
        complete[return_at:return_at+368]=carried
        if session_flow(symbols):complete[0x2a0:0x300]=card_receipt()
        if phase:
            trace = cpu.block(TARGET + trace_at, PHASE_TRACE_BYTES)
            assert struct.unpack_from('<8I',trace) == (0x5349554b,0x314d4954,1,PHASE_TRACE_BYTES,4,0,0,0)
            assert [struct.unpack_from('<I',trace,32+40*i)[0] for i in range(4)] == [2,3,4,5]
            result.update(diagnostic_phase_trace_carried=True,
                          missing_first_mde_callback_explicit=True,phase_ids=[2,3,4,5])
            assert cpu.block(TARGET+video_at,PHASE_VIDEO_BYTES)==phase_video_seed()
            complete[trace_at:trace_at+PHASE_TRACE_BYTES]=trace
            complete[video_at:video_at+PHASE_VIDEO_BYTES]=phase_video_seed()
            result.update(original_outer_video_carried=True)
        assert cpu.block(TARGET,len(expected))==complete,'Authenticated native capsule changed outside declared launch records'
        result.update(cold_entry_reached=True, captured_selection=selection,
                      carried_argv_and_ip_exact=True, firmware_restored=True,
                      original_c0_restored=hex(firmware[1]),
                      only_two_blank_seeds_changed=not phase, return_plan_carried=True,
                      preceding_return_arguments_cleared=True)
    elif kind in ('ce_vc2','ce_swirl','ce_vc2_return','ce_swirl_return'):
        profile=2 if 'swirl' in kind else 1
        ce_return='return' in kind
        at=768 if ce_return else 64
        assert reason==0 and cpu.pc==TARGET and firmware==original_firmware(kind)
        first,second=(600,700) if profile==2 else (400,500)
        assert [(l,n) for l,_,n in cpu.raw_calls]==[(first,1),(first+1,1),(second,2)]
        assert cpu.cache_writes==[0x808]+[0x908,0x808]*4
        seed=cpu.block(TARGET+at,64);words=struct.unpack('<16I',seed)
        assert words[:7]==(0x5349554b,0x31454353,1,64,profile,1,0)
        normalized=bytearray(seed);normalized[28:32]=bytes(4)
        assert words[7]==crc(normalized) and seed[32:48]==ip and seed[48:]==bytes(16)
        assert cpu.block(TARGET+at+64,368)==returning_seed(ce_return)
        if ce_return:
            assert cpu.block(TARGET+at+432,80)==phase_video_seed()
            assert cpu.block(TARGET+at+64+352,16)!=ip, 'Child folder must never become ROOT return context'
        gin=cpu.block(TARGET+CE_GIN_OFFSET,CE_GIN_BYTES)
        gin_words=struct.unpack('<40I',gin)
        assert gin_words[:6]==(0x5349554b,0x314e4947,1,CE_GIN_BYTES,profile,0x909)
        registers,sr,pr,mach,macl,gbr,vbr=original
        frame=[macl,mach,gbr,vbr,sr,pr]+list(reversed(registers[:15]))
        assert list(gin_words[6:27])==frame and gin_words[28:]==(0,)*12
        normalized=bytearray(gin)
        normalized[CE_GIN_CRC_OFFSET:CE_GIN_CRC_OFFSET+4]=bytes(4)
        assert gin_words[27]==crc(normalized)
        # Exactly the child, return and GIN continuation records may change;
        # retain every surrounding authenticated capsule byte verbatim.
        carried=bytearray(expected)
        carried[at:at+64]=seed;carried[at+64:at+432]=returning_seed(ce_return)
        if ce_return:carried[at+432:at+512]=phase_video_seed()
        carried[CE_GIN_OFFSET:CE_GIN_OFFSET+CE_GIN_BYTES]=gin
        if session_flow(symbols):carried[0x2a0:0x300]=card_receipt()
        assert cpu.block(TARGET,len(expected))==carried
        result.update(cold_entry_reached=True,captured_ce_profile=profile,carried_ip_exact=True,
            firmware_restored=True,return_plan_carried=True,complete_original_sample_crc_checked=True,
            captured_original_gin_frame_and_ccr_exact=True,gin_continuation_crc_checked=True,
            only_three_declared_seed_records_changed=not ce_return,
            only_declared_launch_records_changed=True)
        if ce_return:result.update(sealed_ROOT_context_carried=True,original_video_carried=True,
            child_folder_not_reused_for_return=True)
    elif kind in ("nonnative_fallback",'ce_unknown_fallback'):
        assert reason == 0 and cpu.pc == TARGET and not cpu.raw_calls and not cpu.cache_writes
        assert (cpu.r, cpu.sr, cpu.pr, cpu.mach, cpu.macl, cpu.gbr, cpu.vbr) == original
        assert cpu.block(TARGET, len(expected)) == (sample[:len(expected)] if kind=='ce_unknown_fallback' else bytes([0xc7])*len(expected))
        assert firmware == [0xdeadbeef] * 9
        result.update(exact_original_registers_restored=True, executable_unchanged=True)
    elif kind.startswith('ce_'):
        assert reason=={'ce_bad_sample_crc':16,'ce_bad_capsule_crc':11,'ce_wrong_profile':18,
            'ce_return_bad_capability':26,'ce_return_no_root':26,'ce_return_bad_video':26}[kind]
        assert firmware==[0xdeadbeef]*9
        if kind=='ce_bad_sample_crc':
            assert len(cpu.raw_calls)==1 and cpu.cache_writes==[0x808,0x908,0x808]
        elif kind=='ce_return_bad_capability':
            assert len(cpu.raw_calls)==1 and not cpu.cache_writes
        elif not 'return' in kind:
            assert len(cpu.raw_calls)==3 and cpu.cache_writes==[0x808]+[0x908,0x808]*4
            assert cpu.block(TARGET+64,64)==expected[64:128]
            assert cpu.block(TARGET+CE_GIN_OFFSET,CE_GIN_BYTES)==expected[CE_GIN_OFFSET:CE_GIN_OFFSET+CE_GIN_BYTES]
        else:
            assert len(cpu.raw_calls)==3
        result.update(refusal_before_firmware_restore=True,no_cross_backend_entry=True)
    else:
        expected_reason = {"malformed_argv": 8, "raw_probe_busy": 9, "bad_capsule_crc": 11,
                           "invalid_return_plan": 2}[kind]
        assert reason == expected_reason
        assert firmware == [0xdeadbeef] * 9
        if kind == "bad_capsule_crc":
            assert cpu.block(TARGET + seed_at, 304) == expected[seed_at:seed_at+304]
            assert cpu.block(TARGET + return_at, 368) == expected[return_at:return_at+368]
            assert len(cpu.raw_calls) == 3 and cpu.cache_writes == [0x808,0x908,0x808,0x908,0x808,0x908,0x808]
            result.update(blank_menu_seed_unmodified=True)
        else:
            assert cpu.block(TARGET, len(expected)) == bytes([0xc7]) * len(expected)
            assert not cpu.cache_writes
            assert len(cpu.raw_calls) == (1 if kind == "raw_probe_busy" else 0)
            result.update(executable_unchanged=True)
        result.update(refusal_before_firmware_restore=True)
    return result


def exercise_redirect(image, segments, symbols, gin, kind):
    cpu, _, _, _, _, _ = prepare(image, segments, symbols, "phantasy_star_ii")
    code = bytearray(gin[8192:])
    assert len(code) == 244 and crc(code) == 0xa0fc2827
    if kind == "wrong_code_crc":
        code[0] ^= 1
    cpu.store_block(0xacfd2000, code)
    if kind == "invalid_runtime":
        runtime = symbols["_kui_smash_menu_loader_runtime"][0]
        cpu.store_block(runtime, bytes(4))
    before = cpu.block(0xacfd2000, 244)
    sentinel = 0xac007100
    cpu.pc = symbols["_kui_smash_menu_loader_redirect"][0]
    cpu.pr, cpu.r[15] = sentinel, 0xac00c000
    cpu.r[4], cpu.r[5] = 0xacfd2000, 243 if kind == "partial_range" else 512
    for _ in range(200000):
        if cpu.pc == sentinel:
            break
        cpu.step()
    else:
        raise AssertionError("Actual production redirect callback exceeded its instruction bound")
    after = cpu.block(0xacfd2000, 244)
    if kind == "matched_gin":
        expected = bytearray(before)
        struct.pack_into("<I", expected, 0xe4, ENTRY)
        assert after == expected and cpu.r[0] == 1
    else:
        assert after == before
        assert cpu.r[0] == (0 if kind == "invalid_runtime" else 1)
    assert not cpu.raw_calls and not cpu.cache_writes
    return dict(case=kind, instructions=cpu.instructions, accepted=bool(cpu.r[0]),
                target_redirected=kind == "matched_gin",
                only_target_literal_may_change=True, ram_line_purges=cpu.purges)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--elf", required=True, type=Path)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--gin", type=Path,
                        help="Optional exact external GINXFER.BIN for executing the redirect callback")
    parser.add_argument('--sample',type=Path,help='Exact private VC2/Swirl SAMPLE.BIN for both CE selectors')
    args = parser.parse_args()
    image, _, segments, _, symbols = read_elf(args.elf)
    assert symbols["_kui_smash_menu_loader_entry"][0] == ENTRY
    kinds = ["phantasy_star_ii", "shining_force", "phantasy_star_ii_c0_zero", "phantasy_star_ii_c0_rom",
             "phantasy_star_ii_after_menu_return", "invalid_return_plan", "malformed_argv", "raw_probe_busy",
             "bad_capsule_crc", "nonnative_fallback"]
    cases = [exercise(image, segments, symbols, kind) for kind in kinds]
    if args.sample:
        sample=args.sample.read_bytes()
        cases += [exercise(image,segments,symbols,kind,sample) for kind in
            ['ce_vc2','ce_swirl','ce_bad_sample_crc','ce_bad_capsule_crc','ce_wrong_profile','ce_unknown_fallback',
             'ce_vc2_return','ce_swirl_return','ce_return_no_root','ce_return_bad_video','ce_return_bad_capability']]
    redirect_cases = []
    if args.gin:
        gin = args.gin.read_bytes()
        assert hashlib.sha256(gin).hexdigest() == "fa12196993a7c757a78565963e257cd33443f80b3f226729fdc1cf7da51ed5b3"
        redirect_cases = [exercise_redirect(image, segments, symbols, gin, kind)
                          for kind in ("matched_gin", "wrong_code_crc", "partial_range", "invalid_runtime")]
    report = dict(schema="kui.smash-menu.loader-runtime.v1", passed=True,
                  elf_sha256=hashlib.sha256(image).hexdigest(), cases=cases,
                  gin_redirect_cases=redirect_cases,
                  limits=["Actual linked loader C and assembly execute; the fixed raw SD shim is substituted with a four-block-per-call card fixture and an explicit busy response.",
                          "DMA/MMU status reads are modeled as idle, CCR reads/writes are recorded, and OCBP is counted without cache or timing simulation.",
                          "Register banking is not emulated; the separate linked-entry proof establishes capture before any SR bank change.",
                          "The final cold executable entry and resident diagnostic callback are stop boundaries. No downstream SDK, game, real media or VMU execution claim is made."])
    rendered = json.dumps(report, indent=2) + "\n"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(rendered)
    print(rendered, end="")


if __name__ == "__main__":
    main()
