#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute canonical initialized raw data and the terminal reconstruction boundary.

All copy, CRC, controller and reconstruction instructions are actual linked SH code.
The retired CE scheduler, physical MMIO, RAM-clear effect and SD arrival are
explicit existing fixture boundaries; no original retail bytes are included.
"""
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_card_receipt_transport import ActualPureSH
from test_smash_native_gin_linked import State
from smash_ce_return_view import audit as view_audit
import test_smash_ce_return_loader as controller
import test_smash_ce_return_late as late
from smash_ce_return_raw_image import verify as image_verify

RAW,PREFIX_BYTES=0xac008100,260
STARTUP_COPY_SHA="a3e472c0a8467861dd9c9af187606f046f066961b119d59d3b7940a458313c91"
ENTRY,TARGET=0xac008300,0xac010000

def require(ok,message):
    if not ok:raise AssertionError(message)
def sha(data):return hashlib.sha256(data).hexdigest()
def crc(data):return zlib.crc32(data)&0xffffffff


class ImageCPU(ActualPureSH,controller.CECPU):
    def __init__(self,data,kind):
        self.image_observe=False
        super().__init__(data,kind)
        self.target_writes=0;self.target_crc_started=0;self.target_crc_finished=0
        self.target_crc_return=None;self.restore_corruption=False;self.corrupted=False

    def write(self,address,value,count=4):
        physical=address&0x1fffffff
        if self.image_observe:
            require(not (self.prefix&0x1fffffff)<=physical<(self.prefix&0x1fffffff)+PREFIX_BYTES,
                'Return changed its initialized raw prefix or refreshed its expected checksum')
            if (RAW&0x1fffffff)<=physical<(RAW&0x1fffffff)+512:
                require(self.freeze_at is not None and self.hardware.get(0xff000010)==4 and
                    self.original_ccr==0 and not self.raw_calls and self.ram_clear_at is None,
                    'Original raw slot restored before terminal quiescence/cache-off/MMU-off')
                require(not self.hardware.get(0xa05f6c14,0) and not self.hardware.get(0xa05f7c14,0),
                    'Original raw slot restored while a DMA producer remained armed')
                require((0xa05f7c14,0)in self.hardware_writes and
                    {0xa05f7c18,0xa05f6820,0xa05f6808}<=set(self.mmio_reads),
                    'Restore preceded actual terminal producer-drain instructions')
                self.target_writes+=count
                if self.restore_corruption and not self.corrupted and (not self.padding_fault or physical>=(RAW&0x1fffffff)+PREFIX_BYTES):
                    value^=1;self.corrupted=True
        return super().write(address,value,count)

    def step(self):
        if self.image_observe:
            require(not (self.prefix&0x1fffffff)<=self.pc&0x1fffffff<(self.prefix&0x1fffffff)+PREFIX_BYTES,
                'Canonical data was executed at a relocated address')
            if self.target_crc_return is not None and self.pc==self.target_crc_return:
                self.target_crc_finished+=1;self.target_crc_return=None
            if self.pc==(self.crc_entry|0xa0000000) and self.r[5:7]==[RAW,512]:
                require(self.target_writes==512,'Destination CRC did not follow the complete512-byte restore')
                self.target_crc_started+=1;self.target_crc_return=self.pr
        return super().step()


def terminal_cases(build,image,helper,view,profile,run,raw):
    sy=image[4];runtime=sy['_kui_smash_ce_return_runtime'][0]
    end=sy['__smash_ce_return_binary_end'][0]
    require(sy.get('__kui_smash_ce_return_raw_image')==(1,0,0,0xfff1),
        'Controller lacks the exact initialized raw-image capability')
    prefix,count,kind,section_index=sy['_kui_smash_ce_return_raw_prefix']
    section=image[3][section_index]
    require(count==PREFIX_BYTES and kind==1 and section[1]==1 and
        ENTRY<=prefix<prefix+count<=end and
        '__smash_ce_return_raw_shadow_begin'not in sy and '__kui_smash_ce_return_raw_shadow'not in sy and
        sy['__smash_ce_return_bss_end'][0]-sy['__smash_ce_return_bss_begin'][0]==16 and
        sy['__smash_ce_return_stack_bottom'][0]==0xac00b800 and
        sy['__smash_ce_return_stack_top'][0]==0xac00c000 and
        sy['_kui_smash_ce_return_runtime'][1]==632,
        'Initialized prefix escaped its code CRC or changed runtime/BSS/stack geometry')
    cases=[]
    for kind in ('unchanged_live','changed_live_code','changed_live_padding',
                 'changed_prefix_code','changed_prefix_last','restore_write_fault','restore_padding_fault'):
        original=controller.CECPU;controller.CECPU=ImageCPU
        try:cpu=controller.fixture(image,helper,view,profile,'idle')
        finally:controller.CECPU=original
        cpu.track_raw_restore=False
        # Prepared state is authored once from the exact accepted linked raw
        # image. Subsequent mutations never refresh this expected CRC or wire.
        cpu.prefix=prefix;cpu.store_block(RAW,raw);cpu.expected_raw=raw
        wire=bytearray(cpu.block(runtime,632));struct.pack_into('<I',wire,408,crc(raw))
        normalized=bytearray(cpu.block(ENTRY,end-ENTRY));normalized[runtime-ENTRY:runtime-ENTRY+632]=bytes(632)
        struct.pack_into('<I',wire,404,0)
        struct.pack_into('<I',wire,404,zlib.crc32(wire,zlib.crc32(normalized))&0xffffffff)
        cpu.store_block(runtime,wire)
        if kind=='changed_live_code':cpu.write(RAW+20,cpu.read(RAW+20)^1)
        elif kind=='changed_live_padding':cpu.write(RAW+508,cpu.read(RAW+508)^1)
        elif kind=='changed_prefix_code':cpu.write(prefix+20,cpu.read(prefix+20)^1,1)
        elif kind=='changed_prefix_last':cpu.write(prefix+259,cpu.read(prefix+259)^1,1)
        prefix_before=cpu.block(prefix,PREFIX_BYTES);live_before=cpu.block(RAW,512)
        cpu.restore_corruption=kind in ('restore_write_fault','restore_padding_fault')
        cpu.padding_fault=kind=='restore_padding_fault'
        cpu.bind(image,run,TARGET,crc(cpu.capsule))
        # This proof ends before any SD payload arrives. The existing bulk
        # RAM-clear boundary necessarily zeros future receipt/video slots;
        # their later publication observer belongs to the full return proof.
        cpu.observe_receipt=False;cpu.track_raw_restore=True;cpu.image_observe=True
        stopped=sy['_stopped'][0];result=None
        for _ in range(5000000):
            if cpu.pc==stopped:
                result=('stopped',*cpu.r[4:6]);break
            if cpu.pc==RAW:
                result=('first_raw_instruction',);break
            if cpu.pc==sy['_kui_smash_ce_return_ram_clear'][0]:
                cpu.ram_clear();continue
            cpu.step()
        else:raise AssertionError('Canonical raw return exceeded its instruction bound')
        require(cpu.block(runtime,632)==bytes(wire) and cpu.block(prefix,PREFIX_BYTES)==prefix_before,
            'Return refreshed expected CRC/runtime or modified the initialized source')
        if kind.startswith('changed_prefix_'):
            require(result==('stopped',4,0xc00)and cpu.freeze_at is None and
                not cpu.target_writes and not cpu.target_crc_started and not cpu.raw_calls and
                cpu.block(RAW,512)==live_before,'Corrupt initialized image reached terminal restore/execution')
        else:
            require(cpu.target_writes==512 and cpu.target_crc_started==cpu.target_crc_finished==1,
                'Return omitted the exact complete restore/destination CRC boundary')
            if kind in ('restore_write_fault','restore_padding_fault'):
                require(result==('stopped',13,crc(cpu.block(RAW,512)))and result[2]!=crc(raw)and
                    cpu.ram_clear_at is None and not cpu.raw_calls,
                    'Failed destination CRC reached RAM replacement or raw execution')
            else:
                require(result==('first_raw_instruction',)and cpu.block(RAW,512)==raw and
                    cpu.ram_clear_calls==1 and not cpu.raw_calls,
                    'Only an exact authenticated restored raw image may execute')
        cases.append(dict(case=kind,result=list(result),actual_instructions=cpu.instructions,
            fast_actual_crc_instructions=cpu.fast_instructions,target_bytes_written=cpu.target_writes,
            full512_destination_crc_started=cpu.target_crc_started,
            full512_destination_crc_finished=cpu.target_crc_finished,
            canonical_prefix_unchanged=True,canonical_prefix_never_executed=True,sealed_expected_crc_unchanged=True,
            no_physical_sd_read_executed=True))
    return cases


def audit(build,profile):
    build=Path(build)
    names=('ce-return.elf','ce-return.bin','helper/helper.elf','helper/helper.bin',
        'resident-sci.elf','stage.elf','helper/prepared.bin','vc2-raw.bin')
    paths=[build/name for name in names];before={str(p):sha(p.read_bytes())for p in paths}
    sources=('tests/test_smash_ce_return_raw_image.py','tests/test_smash_ce_return_loader.py',
        'tests/test_smash_ce_return_late.py','tests/test_smash_card_receipt_transport.py',
        'tests/test_smash_native_gin_linked.py','tests/test_smash_menu_return_runtime.py',
        'tests/test_smash_menu_loader_runtime.py','tests/sh4_integer_fast.c',
        'src/loader/smash_ce_return_copy.S','src/loader/smash_ce_return_loader.c',
        'src/loader/smash_ce_return_loader.ld','src/loader/ce_return_crc32.h','src/loader/smash_ce_return_raw_image.h','tools/smash_ce_return_raw_image.py',
        'src/loader/cold_load_crc32.c','src/core/smash_ce_return_loader.c',
        'src/loader/native_return_hardware.c','src/loader/native_return_hardware.S',
        'src/loader/smash_ce_return_mmu.S','include/kui/smash_ce_return_loader.h',
        'Makefile.smash-ce-return-probe')
    sources=set(sources)
    for module in tuple(sys.modules.values()):
        name=getattr(module,'__file__',None)
        if name:
            path=Path(name).resolve()
            if path.is_relative_to(ROOT)and path.suffix=='.py':sources.add(str(path.relative_to(ROOT)))
    source_before={p:sha((ROOT/p).read_bytes())for p in sorted(sources)}
    image,helper,resident=read_elf(paths[0]),read_elf(paths[2]),read_elf(paths[4])
    raw=paths[7].read_bytes();require(len(raw)==512,'Accepted raw image is not512 bytes')
    section=resident[3][resident[4]['_kui_vc2_ce_vmu_read_card'][3]]
    linked=resident[0][section[4]:section[4]+section[5]]
    require(raw==linked+bytes(512-len(linked)),'Raw source differs from actual linked accepted gate')
    canonical=image_verify(raw,paths[4],ROOT/'src/loader/smash_ce_return_raw_image.h',paths[0])
    capture=late.copy_audit(image,paths[1].read_bytes(),paths[3].read_bytes(),profile,raw)
    view=view_audit(paths[2],paths[4],paths[3]);view['old_e0']=resident[4]['_kui_retail_menu_hook'][0]
    with tempfile.TemporaryDirectory(prefix='kui-ce-raw-image-')as folder:
        libpath=Path(folder)/'integer.so'
        subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O2','-shared','-fPIC',
            str(ROOT/'tests/sh4_integer_fast.c'),'-o',str(libpath)],check=True,capture_output=True)
        library=ctypes.CDLL(str(libpath));run=library.sh4_fast_run
        run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
        terminal=terminal_cases(build,image,helper,view,profile,run,raw)
    require(before=={str(p):sha(p.read_bytes())for p in paths},'Raw-image inputs changed during replay')
    require(source_before=={p:sha((ROOT/p).read_bytes())for p in sources},'Raw-image sources changed during replay')
    require(sha((ROOT/'src/loader/smash_ce_return_copy.S').read_bytes())==STARTUP_COPY_SHA,
        'Startup installer no longer matches the accepted EB6 template')
    require(not capture['protected_raw_shadow']and len(capture['cases'])==3 and
        capture['canonical_raw_image']and capture['no_new_direct_b400_writes']and
        all(case['no_new_direct_b400_writes']for case in capture['cases']),
        'Startup installer retains a new captured raw-data lifetime')
    checks=dict(current_raw_image_byte_exact=True,initialized_prefix_covered_by_code_crc=True,
        canonical_zero_tail_exact=True,startup_template_matches_eb6=True,no_pre_nk_shadow_capture=True,
        prefix_corruption_refused_before_retirement=True,changed_live_requires_terminal_reconstruction=True,
        reconstruction_after_quiesce_cache_off_mmu_off=True,destination_full512_crc_before_execution=True,
        failed_destination_crc_refuses_execution=True,sealed_expected_crc_never_refreshed=True,
        worker_runtime_stack_geometry_unchanged=True)
    return dict(schema='kui.smash-ce.return-raw-image.v1',status='PASS',profile=profile,
        test_sha256=source_before['tests/test_smash_ce_return_raw_image.py'],source_sha256=source_before,
        inputs=before,inputs_immutable_during_execution=True,checks=checks,
        raw_sha256=sha(raw),raw_crc32=f'{crc(raw):08x}',prefix_sha256=sha(raw[:PREFIX_BYTES]),prefix_bytes=PREFIX_BYTES,zero_tail_bytes=252,canonical=canonical,
        capture=capture,terminal_cases=terminal,
        limits=['Actual linked startup copy, sealed predicates, reconstruction and full CRC instructions execute; no CRC/validator substitution.',
            'Coherent RAM, MMIO and retired CE scheduling are fixtures; physical caches, console timing and the retail writer of the old slot are not proved.',
            'Stops before the first restored raw instruction; independent raw-wire tests qualify its SD behavior.'])


if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True)
    p.add_argument('--profile',type=int,choices=(1,2),required=True);p.add_argument('--report',type=Path)
    a=p.parse_args();r=audit(a.build,a.profile);text=json.dumps(r,indent=2)+'\n'
    if a.report:a.report.parent.mkdir(parents=True,exist_ok=True);a.report.write_text(text)
    print(text,end='')
