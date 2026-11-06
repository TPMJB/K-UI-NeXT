#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the actual CE terminal controller with explicit scheduler/card fixtures.

Original CE scheduling and physical SD bytes are model boundaries. Production
entry, authenticated code/runtime checks, helper-bound idle predicate, retry
assembly, hardware quiescence, MMU-off and final publication all execute.
The terminal RAM-clear loop has an independent complete linked-instruction
proof; its bulk memory effect is an explicit boundary in this controller test.
"""
import argparse
import ctypes
from contextlib import contextmanager
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
from smash_ce_return_view import audit as view_audit
from test_smash_menu_return_runtime import ReturnCPU,capsule,blank_return,READ_REGS,WRITE_REGS
from test_smash_menu_loader_runtime import FIRMWARE,FIRMWARE_ADDRESSES,card_receipt,phase_video_seed
from test_smash_ce_return_ram_clear import audit_leaf as ram_clear_audit

ENTRY,RAW,TARGET=0xac008300,0xac008100,0xac010000
def crc(data,previous=0):return zlib.crc32(data,previous)&0xffffffff
def sha(data):return hashlib.sha256(data).hexdigest()

class CECPU(ReturnCPU):
    def __init__(self,data,kind):
        self.track_raw_restore=False
        super().__init__(data,kind)
        self.hardware[0xff000010]=1
        self.freeze_at=None
        self.scheduler_completions=0
        self.wait_owner=0
        self.ram_clear_at=None
        self.ram_clear_calls=0
        self.raw_shadow=RAW
        self.raw_prefix=None
        self.raw_repair=False
        self.track_raw_restore=False
        self.raw_restore_bytes=0
    def translate(self,address):
        # Authored CE stack page: actual MMU hardware is an explicit model
        # boundary, while all saved register values remain the original VA.
        if self.hardware.get(0xff000010)==1 and 0x06123000<=address<0x06124000:
            return 0x8c123000+(address-0x06123000)
        return address
    def read(self,address,count=4):
        return super().read(self.translate(address),count)
    def raw_read(self):
        lba,destination=self.r[4:6]
        assert self.hardware[0xff000010]==4 and self.freeze_at is not None
        assert self.ram_clear_at is not None and self.ram_clear_calls==1
        assert not self.hardware.get(0xa05f6c14,0)
        if self.raw_repair:
            assert self.block(RAW,512)==self.expected_raw
        if not self.raw_calls:
            assert all(self.read(a)==0 for a in self.high_canaries)
        logical={100:0,101:1,200:2,201:3}[lba]
        self.raw_calls.append((lba,destination,1))
        self.store_block(destination,self.capsule[logical*512:(logical+1)*512])
        self.r[0],self.pc=0,self.pr
    def ram_clear(self):
        # audit_leaf executes every instruction/store of this exact linked
        # leaf independently. Apply that proven interval once here so each
        # admission/scheduler case need not interpret its complete store loop.
        assert self.ram_clear_at is None and not self.raw_calls
        assert self.hardware[0xff000010]==4 and self.original_ccr==0
        assert self.freeze_at is not None and self.read(self.view['freeze'])==1
        assert 0xac008300<=self.pc<0xac00b800 and 0xac00b800<=self.r[15]<0xac00c000
        assert not self.hardware.get(0xa05f6c14,0)
        assert not self.hardware.get(0xa05f7c14,0) and not self.hardware.get(0xa05f6820,0)
        assert (0xa05f7c14,0) in self.hardware_writes
        assert {0xa05f7c18,0xa05f6820,0xa05f6808}<=set(self.mmio_reads)
        for channel in range(4):
            assert (0xa05f7814+channel*32,0) in self.hardware_writes
            chcr=0xffa0000c+channel*16
            assert any(a==chcr and not value&1 for a,value in self.hardware_writes)
        for bank in range(3):
            for kind in range(3):assert (0xa05f6910+bank*16+kind*4,0) in self.hardware_writes
        assert self.hardware[0xa0702c00]&1
        if self.raw_repair:
            assert self.block(RAW,512)==self.expected_raw
            authority=(self.block(self.raw_prefix,260)+bytes(252) if self.raw_prefix else self.block(self.raw_shadow,512))
            assert authority==self.expected_raw
        low=self.block(0xac000000,0x10000)
        self.store_block(TARGET,bytes(0xff0000))
        assert self.block(0xac000000,0x10000)==low
        self.ram_clear_at=self.instructions
        self.ram_clear_calls+=1
        self.r[:3]=[0xad000000,0xad000000,0]
        self.flag(True)
        self.pc=self.pr
    def write(self,address,value,count=4):
        if self.track_raw_restore and RAW<=address<RAW+512:
            assert address+count<=RAW+512 and self.freeze_at is not None
            assert self.hardware[0xff000010]==4 and self.original_ccr==0
            assert not self.raw_calls and self.ram_clear_at is None
            self.raw_restore_bytes+=count
        if hasattr(self,'view') and address==self.view['freeze']:
            assert count==4 and value==1 and self.sr&0xf0==0xf0
            self.freeze_at=self.instructions
        super().write(self.translate(address),value,count)

def load(cpu,image):
    for p in image[2]:
        if p[0]==1:
            cpu.store_block(p[3]|0x20000000,image[0][p[1]:p[1]+p[4]]+bytes(p[5]-p[4]))

def fixture(elf,helper,view_report,profile,kind,swirl_stub=None):
    expected=capsule(True,True)
    actual=bytearray(expected)
    if kind=='bad_capsule_crc':actual[1700]^=1
    cpu=CECPU(bytes(actual),kind)
    load(cpu,helper);load(cpu,elf)
    v=view_report['view'];o=view_report['target_layout']['offsets'];cpu.view=v
    def put(base,name,value,bytes_=4):
        cpu.store_block(v[base]+o[base+'.'+name],value.to_bytes(bytes_,'little'))
    cpu.store_block(v['runtime'],bytes(o['runtime_bytes']))
    cpu.store_block(v['loan'],bytes(o['loan_bytes']))
    cpu.store_block(v['region'],bytes(o['region_bytes']))
    cpu.store_block(v['manifest'],bytes(o['manifest_bytes']))
    cpu.store_block(v['saved_region'],struct.pack('<I',v['region']))
    for name,value in dict(region=v['region'],manifest=v['manifest'],active=v['manifest']+244,
            fault=v['manifest']+240,begin=0x8c004000,end=0x8c007ea0,
            magic=0x314c4356,started=1,state=1).items():put('loan',name,value)
    put('region','service.initialized',1)
    put('region','service.ops.context',1)
    put('region','service.tracks',(v['manifest']&~0x20000000)+o['manifest.slots'])
    put('region','engine.manifest',v['manifest']&~0x20000000)
    put('region','card.transport',1)
    put('region','card.sd.ready',1,1)
    put('region','card.sd.blocks',1000000,8)
    put('manifest','storage_transport',1)
    put('manifest','card_sectors',1000000,8)
    # A used, normally released provider: captured is an observational latch,
    # while acquired/selected and every live producer remain zero.
    for name,value in dict(started=1,ready=1,owner=0x01c14000,
            **{'endpoint.self':v['runtime'],'endpoint.magic':0x31574341,
               'endpoint.started':1,'queue.self':v['runtime']+o['runtime.queue.self'],
               'queue.magic':0x314a4d56,'sd.self':v['runtime']+o['runtime.sd.self'],
               'sd.magic':0x31534441,'sd.started':1,
               'sd.pio.self':v['runtime']+o['runtime.sd.pio.self'],
               'sd.pio.magic':0x4b565031}).items():put('runtime',name,value)
    put('runtime','endpoint.protocol.initialized',1,1)
    put('runtime','sd.pio.captured',1,1)
    if kind=='busy_continuation':
        cpu.store_block(v['continuation'],struct.pack('<I',1))
        put('runtime','queue.state',1)
        cpu.store_block(v['fence'],struct.pack('<2I',1,1))
    elif kind=='busy_gd':put('region','engine.active',1)
    elif kind=='faulted_loan':put('loan','first_fault',4)
    elif kind=='unstarted':cpu.store_block(v['runtime'],bytes(o['runtime_bytes']))
    raw=bytes((i*23+0x77)&255 for i in range(512));cpu.store_block(RAW,raw)
    symbols=elf[4];runtime=symbols['_kui_smash_ce_return_runtime'][0]
    cpu.raw_shadow=symbols.get('__smash_ce_return_raw_shadow_begin',(RAW,))[0]
    prefix=symbols.get('_kui_smash_ce_return_raw_prefix')
    if prefix:
        assert prefix[1]==260
        cpu.raw_prefix=prefix[0]
        raw=cpu.block(cpu.raw_prefix,260)+bytes(252)
        cpu.store_block(RAW,raw)
    cpu.expected_raw=raw
    if cpu.raw_shadow!=RAW:
        assert cpu.raw_shadow==0xac00b400 and symbols['__smash_ce_return_raw_shadow_end'][0]==0xac00b600
        cpu.store_block(cpu.raw_shadow,raw)
    cpu.raw_repair=bool(cpu.raw_prefix or cpu.raw_shadow!=RAW)
    begin,end=ENTRY,symbols['__smash_ce_return_binary_end'][0]
    normalized=bytearray(cpu.block(begin,end-begin));normalized[runtime-begin:runtime-begin+632]=bytes(632)
    plan=[0x5349554b,0x31444c4d,1,64,2048,2048,crc(expected),128,100,2,200,4,0,0,0,0]
    plan[-1]=crc(struct.pack('<16I',*plan))
    returning=bytearray(struct.pack('<8I',0x5349554b,0x3154524d,1,368,3,0,0,0)+
        struct.pack('<16I',*plan)+b'\\manifest.bin\0'.ljust(256,b'\0')+b' '*12+b'\x01\x04\xa7\xb8')
    struct.pack_into('<I',returning,28,crc(returning))
    receipt=card_receipt(owner=3 if profile==1 else 4,sequence=2)
    video=phase_video_seed()
    wire=bytearray(returning+struct.pack('<14I',*FIRMWARE,0,crc(raw),profile,
        view_report['helper_bytes'],view_report['helper_crc32'])+
        struct.pack('<8I',*[v[k] for k in ('runtime','loan','continuation','fence','freeze','saved_region','region','manifest')])+receipt+video)
    assert len(wire)==632
    struct.pack_into('<I',wire,404,crc(wire,crc(normalized)))
    if kind=='bad_receipt':wire[456+45]^=1
    elif kind=='bad_video':wire[552+36]^=1
    elif kind=='bad_runtime':wire[424]^=4
    cpu.store_block(runtime,wire)
    if kind=='bad_helper':cpu.write(0xacfc4014,cpu.read(0xacfc4014)^1)
    if kind=='bad_raw':
        at=(cpu.raw_prefix or cpu.raw_shadow)+20
        cpu.write(at,cpu.read(at)^1)
    if kind in ('changed_live_raw_code','changed_live_raw_padding'):
        assert cpu.raw_repair
        at=RAW+(20 if kind.endswith('code') else 511)
        cpu.store_block(at,bytes([cpu.block(at,1)[0]^1]))
    for address in FIRMWARE_ADDRESSES:cpu.store_block(address,struct.pack('<I',0xdeadbeef))
    cpu.r=[0x12340000+0x1111*i for i in range(15)]+[0x8c123450]
    if kind=='busy_continuation':cpu.r[15]=0x06123450
    cpu.r[4]={'forward_reset3':3,'forward_disc':2,'forward_unknown':4}.get(kind,1)
    cpu.store_block(symbols['_kui_smash_ce_return_previous_e0'][0],struct.pack('<I',view_report['old_e0']))
    cpu.sr,cpu.pr=0x40000001,(0x01d21902 if profile==1 else 0x01d2190e)
    if kind in ('busy_gd','forward_disc'):cpu.sr&=~1
    if kind=='wrong_caller':cpu.pr+=2
    if kind.startswith('swirl_private_'):
        assert profile==2
        cpu.r[4],cpu.pr=0,0x000419a6
        cpu.store_block(0xac0080fe,b'\x01')
        cpu.store_block(0xac00e00c,struct.pack('<I',0x8c008300))
        if kind=='swirl_private_bad_flag':cpu.store_block(0xac0080fe,b'\0')
        elif kind=='swirl_private_bad_callback':cpu.store_block(0xac00e00c,struct.pack('<I',0x8c008304))
        elif kind=='swirl_private_wrong_caller':cpu.pr+=2
        elif kind=='swirl_private_wrong_command':cpu.r[4]=2
        elif kind=='swirl_private_bad_sr':cpu.sr|=0x10000000
    if kind.startswith('swirl_fallback_'):
        assert profile==2 and swirl_stub is not None and len(swirl_stub)==32
        cpu.r[4],cpu.pr=3,0x000419a6
        cpu.store_block(0xac0080fe,b'\0')
        cpu.store_block(0xac00e00c,struct.pack('<I',0xac00e100))
        cpu.store_block(0xac000064,struct.pack('<I',0x8c008000))
        stub=bytearray(swirl_stub)
        if kind=='swirl_fallback_bad_stub':stub[12]^=1
        elif kind=='swirl_fallback_bad_flag':cpu.store_block(0xac0080fe,b'\x01')
        elif kind=='swirl_fallback_bad_callback':cpu.store_block(0xac00e00c,struct.pack('<I',0xac00e104))
        elif kind=='swirl_fallback_bad_ip':cpu.store_block(0xac000064,struct.pack('<I',0x8c008004))
        cpu.store_block(0xac00e100,stub)
    cpu.gbr,cpu.vbr=0x02123400,0x8c010000
    cpu.pc=ENTRY
    if kind.startswith('swirl_private_'):cpu.pc=ENTRY&~0x20000000
    cpu.expected_return, cpu.expected_receipt,cpu.expected_video=bytes(returning),receipt,video
    cpu.original_sp,cpu.original_sr,cpu.original_gbr,cpu.original_vbr=cpu.r[15],cpu.sr,cpu.gbr,cpu.vbr
    cpu.original_pr=cpu.pr
    cpu.manifest_before=cpu.block(v['manifest'],o['manifest_bytes'])
    cpu.pending_offsets=o
    cpu.high_canaries=(0xace00000,0xacfd0000,0xacfffffc)
    for address in cpu.high_canaries:cpu.store_block(address,struct.pack('<I',0xa7b8c9da))
    cpu.track_raw_restore=cpu.raw_repair
    return cpu

def _audit(elf,helper_elf,helper_bin,resident_elf,profile,swirl_sample=None):
    paths=[Path(p) for p in (elf,helper_elf,helper_bin,resident_elf)]
    swirl_stub=None;swirl_source=None
    if profile==2 and swirl_sample is not None:
        paths.append(Path(swirl_sample));sample=paths[-1].read_bytes()
        assert len(sample)==204576 and sha(sample)=='82bb1160814548f4656b273353fd6168a691fb5f215b77377860ff9fd2156c8a'
        swirl_stub=sample[0x266a8:0x266c8]
        assert len(swirl_stub)==32 and crc(swirl_stub)==0xe66470fc
        swirl_source=dict(sample_sha256=sha(sample),stub_sha256=sha(swirl_stub),stub_offset=0x266a8,stub_bytes=32)
    before={str(p):sha(p.read_bytes()) for p in paths}
    image,helper=read_elf(paths[0]),read_elf(paths[1])
    vr=view_audit(paths[1],paths[3],paths[2])
    vr['old_e0']=read_elf(paths[3])[4]['_kui_retail_menu_hook'][0]
    sy=image[4]
    clear_proof=ram_clear_audit(image)
    clear_entry=sy['_kui_smash_ce_return_ram_clear'][0]
    assert sy['__kui_smash_ce_return_controller']==(1,0,0,0xfff1)
    assert sy['__smash_ce_return_binary_end'][0]-ENTRY<=7968
    assert sy['__smash_ce_return_bss_end'][0]<=0xac00b800
    assert sy['_kui_smash_ce_return_runtime'][1]==632
    stop=sy['_stopped'][0];wait=sy['__smash_ce_return_wait_owner'][0]
    cases=[]
    kinds=('idle','unstarted','busy_continuation','busy_gd','forward_reset3','forward_disc','forward_unknown','wrong_caller',
           'bad_receipt','bad_video','bad_runtime','bad_helper','faulted_loan','bad_capsule_crc','bad_raw')
    success={'idle','unstarted','busy_continuation','busy_gd'}
    if '__smash_ce_return_raw_shadow_begin' in sy or '_kui_smash_ce_return_raw_prefix' in sy:
        kinds+=('changed_live_raw_code','changed_live_raw_padding','restore_source_fault')
        success.update(('changed_live_raw_code','changed_live_raw_padding'))
    forwarded={'forward_reset3','forward_disc','forward_unknown'}
    if profile==2:
        kinds+=('swirl_private_ok','swirl_private_bad_flag','swirl_private_bad_callback',
                'swirl_private_wrong_caller','swirl_private_wrong_command','swirl_private_bad_sr')
        success.add('swirl_private_ok')
        forwarded.update(('swirl_private_wrong_caller','swirl_private_wrong_command'))
        if swirl_stub is not None:
            kinds+=('swirl_fallback_ok','swirl_fallback_bad_stub','swirl_fallback_bad_flag',
                    'swirl_fallback_bad_callback','swirl_fallback_bad_ip')
            success.add('swirl_fallback_ok')
    failure_details={'bad_receipt':(4,0x500),'bad_video':(4,0x480),'bad_runtime':(4,0x400),
        'bad_helper':(4,0x1000),'bad_raw':(4,0x800),'faulted_loan':(5,0xffffffff),
        'swirl_private_bad_flag':(2,0x8c008300),'swirl_private_bad_callback':(2,0x8c008304),
        'swirl_private_bad_sr':(1,0x8c123450),'swirl_fallback_bad_stub':(2,0xac00e100),
        'swirl_fallback_bad_flag':(2,0xac00e100),'swirl_fallback_bad_callback':(2,0xac00e104),
        'swirl_fallback_bad_ip':(2,0xac00e100)}
    if '_kui_smash_ce_return_raw_prefix' in sy:
        failure_details['bad_raw']=(4,0xc00)
    # The tableless exit-only CRC executes more actual integer instructions.
    # Focused linked replay measures about7.5M on success; retain a finite
    # conservative bound without replacing any CRC/guard with a Python value.
    instruction_bound=12000000 if '__kui_smash_ce_return_crc32_bitwise' in sy else 5000000
    for kind in kinds:
        cpu=fixture(image,helper,vr,profile,kind,swirl_stub)
        restore_fault_crc=None
        for _ in range(instruction_bound):
            if cpu.pc in (TARGET,stop,vr['old_e0']):break
            if cpu.pc==RAW:
                cpu.raw_read()
                continue
            if cpu.pc==clear_entry:
                cpu.ram_clear()
                continue
            if cpu.pc==sy.get('_restore_raw',(None,))[0]:
                assert cpu.hardware[0xff000010]==4 and cpu.original_ccr==0
                assert cpu.freeze_at is not None and not cpu.raw_calls
                if kind=='restore_source_fault':
                    assert restore_fault_crc is None
                    changed=bytearray(cpu.expected_raw)
                    changed[259 if cpu.raw_prefix else 511]^=1
                    if cpu.raw_prefix:cpu.store_block(cpu.raw_prefix,changed[:260])
                    else:cpu.store_block(cpu.raw_shadow,changed)
                    restore_fault_crc=crc(changed)
            if cpu.pc==wait:
                assert kind in ('busy_continuation','busy_gd') and not cpu.scheduler_completions
                assert cpu.r[15]==cpu.original_sp-84 and cpu.sr==cpu.original_sr
                assert cpu.gbr==cpu.original_gbr and cpu.vbr==cpu.original_vbr
                assert cpu.hardware[0xff000010]==1 and not cpu.cache_writes and not cpu.raw_calls
                assert cpu.read(cpu.view['freeze'])==0 and not cpu.hardware_writes
                # Explicit original-scheduler boundary: no production helper
                # or provider is impersonated by the terminal controller.
                cpu.store_block(cpu.view['continuation'],bytes(4))
                cpu.store_block(cpu.view['fence'],bytes(8))
                cpu.store_block(cpu.view['runtime']+cpu.pending_offsets['runtime.queue.state'],bytes(4))
                cpu.store_block(cpu.view['region']+cpu.pending_offsets['region.engine.active'],bytes(4))
                cpu.scheduler_completions+=1
            cpu.step()
        else:raise AssertionError('CE terminal controller did not stop: '+kind)
        ok=kind in success
        if kind in forwarded:
            assert cpu.pc==vr['old_e0'] and cpu.r[4] in (0,2,3,4)
            assert cpu.r[15]==cpu.original_sp and cpu.sr==cpu.original_sr
            assert cpu.pr==cpu.original_pr
            assert not cpu.raw_calls and cpu.freeze_at is None and not cpu.hardware_writes and not cpu.writes
            cases.append(dict(kind=kind,outcome='unchanged_original_hook_boundary',
                instructions=cpu.instructions,raw_blocks=0,stack_untouched=True,
                original_sr_command_pr_sp_preserved=True))
            continue
        assert (cpu.pc==TARGET)==ok,(kind,hex(cpu.pc),hex(cpu.r[4]),hex(cpu.r[5]))
        if kind in failure_details:
            assert cpu.pc==stop and tuple(cpu.r[4:6])==failure_details[kind],(kind,cpu.r[4:6],failure_details[kind])
        if kind=='restore_source_fault':
            assert cpu.pc==stop and tuple(cpu.r[4:6])==(13,restore_fault_crc)
            assert cpu.freeze_at is not None and not cpu.raw_calls and cpu.ram_clear_at is None
            assert [cpu.read(a) for a in FIRMWARE_ADDRESSES]==[0xdeadbeef]*9
        assert cpu.block(cpu.view['manifest'],vr['reader_manifest_bytes'])==cpu.manifest_before
        if ok:
            assert len(cpu.raw_calls)==4 and cpu.ram_clear_calls==1 and cpu.freeze_at is not None
            assert cpu.read(cpu.view['freeze'])==0
            assert cpu.block(0xacfc4000,41152)==bytes(41152)
            assert cpu.hardware[0xff000010]==4 and cpu.original_ccr==0
            assert cpu.block(TARGET+128,368)==cpu.expected_return
            assert cpu.block(TARGET+496,80)==cpu.expected_video
            assert cpu.block(TARGET+0x2a0,96)==cpu.expected_receipt
            assert [cpu.read(a) for a in FIRMWARE_ADDRESSES]==FIRMWARE
            assert cpu.r[15]==0x8cffff00 and cpu.vbr==0x8c000000 and cpu.gbr==0
            assert cpu.sr==0x500000f0
        elif kind not in ('bad_capsule_crc','restore_source_fault'):
            assert not cpu.raw_calls and cpu.freeze_at is None and not cpu.hardware_writes
            assert cpu.ram_clear_at is None
        cases.append(dict(kind=kind,outcome='outer_entry' if ok else 'visible_refusal_boundary',
            instructions=cpu.instructions,raw_blocks=len(cpu.raw_calls),
            actual_crc_decoder_instructions=getattr(cpu,'fast_instructions',0),
            ram_clear_calls=cpu.ram_clear_calls,ram_clear_before_raw=cpu.ram_clear_at is not None,
            raw_restore_bytes=cpu.raw_restore_bytes,
            authenticated_raw_restored=bool(ok and cpu.raw_repair and cpu.block(RAW,512)==cpu.expected_raw),
            scheduler_completion_fixture=cpu.scheduler_completions,old_manifest_unchanged=True,
            failure_reason=None if ok else cpu.r[4],failure_detail=None if ok else cpu.r[5]))
    assert before=={str(p):sha(p.read_bytes()) for p in paths},'Linked inputs changed during audit'
    sources={Path(__file__).resolve()}
    for module in tuple(sys.modules.values()):
        name=getattr(module,'__file__',None)
        if name:
            path=Path(name).resolve()
            if path.is_relative_to(ROOT) and path.suffix=='.py':sources.add(path)
    sources.update(ROOT/p for p in (
        'include/kui/smash_ce_return_loader.h','include/kui/smash_ce_return_view.h',
        'src/loader/smash_ce_return_loader.c','src/core/smash_ce_return_loader.c',
        'src/loader/smash_ce_return_view.c','src/loader/smash_ce_return_entry.S',
        'src/loader/smash_ce_return_copy.S','src/loader/smash_ce_return_mmu.S',
        'src/loader/smash_ce_return_ram_clear.S',
        'src/loader/smash_ce_return_loader.ld','src/loader/native_return_hardware.c',
        'src/loader/native_return_hardware.S','src/loader/smash_return_fault_display.c',
        'src/loader/cold_load_scope.S','src/loader/cold_load_crc32.c',
        'tests/sh4_integer_fast.c',
        'src/loader/ce_return_crc32.h',
        'src/loader/stage_fast_crc32.h','src/core/smash_card_session.c',
        'src/core/smash_menu_return.c','src/core/smash_menu_loader.c',
        'src/core/smash_phase_video.c','src/core/retail_image.c','src/loader/minic.c'))
    source_hashes={str(p.relative_to(ROOT)):sha(p.read_bytes()) for p in sorted(sources)}
    return dict(schema='kui.smash-ce.return-controller.v1',status='PASS',profile=profile,
        elf_sha256=before[str(paths[0])],helper_elf_sha256=before[str(paths[1])],
        helper_sha256=before[str(paths[2])],resident_elf_sha256=before[str(paths[3])],
        test_sha256=sha(Path(__file__).read_bytes()),source_sha256=source_hashes,
        private_state_aliases='Exact helper P2 BSS symbols; immutable P1 code retains P2 data addresses',
        view_binding=vr,
        actual_entry=True,actual_idle_view=True,actual_terminal_quiesce=True,
        actual_mmu_off_and_enter=True,root_context_exact=True,receipt_exact=True,
        ram_clear_independent_linked_proof=clear_proof,ram_clear_effect_boundary=True,
        phase_video_exact=True,no_old_renderer=True,no_old_resume=True,qualified_command=1,
        qualified_commands=[1,0,3] if swirl_source is not None else ([1,0] if profile==2 else [1]),
        non_reset_forwarding_preserved=True,predicate_failure_details_checked=True,
        swirl_private_callback_qualified=profile==2,swirl_fallback_qualified=swirl_source is not None,
        swirl_fallback_source=swirl_source,cases=cases,
        limits=['Original CE scheduler progress is an explicit authored state-completion boundary reached only after the actual retry restores owner SP/SR/GBR/VBR.',
                'The physical raw-card shim is a fixture; complete payload CRC and all publication execute. Separate raw-wire tests qualify the unchanged shim.',
                'The exact linked RAM-clear leaf has a separate complete native SH-instruction proof. This controller test applies its proven bulk effect at the actual leaf entry and verifies terminal ordering, low-state preservation and fresh outer publication.',
                'Unrelated firmware calls stop at the unchanged original hook; terminal assertions apply to reset command1 and the source-checked Swirl private callback.',
                'No physical CE scheduler, MMU/cache coherency, console return success or timing is certified.'])

@contextmanager
def actual_crc_fixture(elf):
    # Execute the same linked CRC instructions using the existing native
    # integer decoder used by the raw-image and failure-to-pixels proofs.
    # No checksum result is substituted: register/stack state, input bytes,
    # write canaries and every executed instruction remain checked.
    from test_smash_card_receipt_transport import ActualPureSH
    from test_smash_native_gin_linked import State
    global CECPU
    original=CECPU
    image=read_elf(Path(elf))
    with tempfile.TemporaryDirectory(prefix='kui-ce-controller-crc-')as folder:
        library_path=Path(folder)/'integer.so'
        subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O2','-shared','-fPIC',
            str(ROOT/'tests/sh4_integer_fast.c'),'-o',str(library_path)],
            check=True,capture_output=True)
        library=ctypes.CDLL(str(library_path));run=library.sh4_fast_run
        run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64]
        run.restype=ctypes.c_int
        class ActualCRCController(ActualPureSH,original):
            def __init__(self,data,kind):
                super().__init__(data,kind)
                self.bind(image,run,TARGET,0)
                # The complete CE fixture already checks every publication.
                # Transport's additional native receipt observer is separate.
                self.observe_receipt=False
        CECPU=ActualCRCController
        try:
            yield
        finally:
            CECPU=original

def audit(elf,helper_elf,helper_bin,resident_elf,profile,swirl_sample=None):
    with actual_crc_fixture(elf):
        report=_audit(elf,helper_elf,helper_bin,resident_elf,profile,swirl_sample)
        report['actual_linked_crc_native_decoder']=True
        return report

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('elf','helper-elf','helper-bin','resident-elf'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--profile',type=int,choices=(1,2),required=True);p.add_argument('--report',type=Path)
    p.add_argument('--swirl-sample',type=Path,help='External exact original SAMPLE for the32-byte fallback-thunk C guards')
    a=p.parse_args();r=audit(a.elf,a.helper_elf,a.helper_bin,a.resident_elf,a.profile,a.swirl_sample)
    out=json.dumps(r,indent=2)+'\n'
    if a.report:a.report.write_text(out)
    print(out,end='')
if __name__=='__main__':main()
