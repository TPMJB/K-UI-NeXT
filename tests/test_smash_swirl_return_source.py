#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute original Swirl private return sources against the linked controller.

API: audit(build, originals, oldbuild=None), where originals is the supplied
SmashPack-Swirl-runtime directory (containing SWIRL/). No retail bytes are
embedded. File-success callbacks, SetKMode and CE address translation are
explicit model boundaries; original scalar source and linked dispatch execute.
The successful dispatch boundary is controller C entry, not menu/gameplay.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from armada_pe_audio_audit import PE
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_loader_runtime import RuntimeCPU
from test_smash_ce_return_loader import fixture as terminal_fixture,view_audit,TARGET,RAW,actual_crc_fixture
from test_smash_menu_loader_runtime import FIRMWARE,FIRMWARE_ADDRESSES
from test_smash_ce_return_ram_clear import audit_leaf as ram_clear_audit

SAMPLE_BASE=0x8c010000
MAINANA_SOURCE,MAINANA_DEST,MAINANA_BYTES=0x2509c,0x8ce08300,0x3aa4
SOURCE_SELECT,SELECT_END=0x8c036470,0x8c03649a
PRIVATE_CELL,PRIVATE_STUB,IP_FE=0xac00e00c,0xac00e100,0x8c0080fe
LOW,BIOS,SETKMODE=0xac008300,0xa00000e0,0x8c003000
CALLSITE,WRAPPER,PRIVATE_PR=0x15230,0x4198c,0x419a6
ORIGINALS={
    'SAMPLE.BIN':'82bb1160814548f4656b273353fd6168a691fb5f215b77377860ff9fd2156c8a',
    'SWIRLDC.EXE':'a80f177febbd4f3bcd683b1dc960a0c4b8425b88e9159b739398669d079112bc',
    'MAIGO.BIN':'55d89bfb608806138f5c675ba2187a99a0b4e2f11f7f65048c3f406b8993826a',
}

def sha(data):return hashlib.sha256(data).hexdigest()
def require(ok,message):
    if not ok:raise AssertionError(message)

class SourceCPU(RuntimeCPU):
    def translate(self,address):
        # Authored CE mappings. Registers retain original virtual addresses.
        if 0x10000<=address<0xb4000:return 0x8c000000+address
        if 0x06123000<=address<0x06124000:return 0x8c123000+address-0x06123000
        return address
    def read(self,address,count=4):return super().read(self.translate(address),count)
    def write(self,address,value,count=4):return super().write(self.translate(address),value,count)
    def string(self,address):
        out=bytearray()
        for _ in range(128):
            value=self.read(address,1);address+=1
            if not value:return out.decode('ascii')
            out.append(value)
        raise AssertionError('Source pathname lacked a bounded terminator')

def load_elf(cpu,image):
    for p in image[2]:
        if p[0]==1:cpu.store_block(p[2],image[0][p[1]:p[1]+p[4]]+bytes(p[5]-p[4]))

def sample_sources(sample,maigo):
    cpu=SourceCPU(b'');cpu.store_block(SAMPLE_BASE,sample)
    cpu.r=[0x11000000+i*0x101 for i in range(16)]
    cpu.r[15],cpu.pr,cpu.sr,cpu.pc=0x8c00f400,0x8c003100,0x40000000,0x8c010324
    cpu.write(IP_FE,0x20,1)
    handle=0x8c600000;calls=[]
    boundaries={0x8c014b34:'directory',0x8c01542e:'open',0x8c014b9e:'size',
        0x8c014c76:'read',0x8c014b4c:'close'}
    for _ in range(1000):
        if cpu.pc==0x8c01035e:break
        if cpu.pc in boundaries:
            kind=boundaries[cpu.pc];calls.append(kind)
            if kind=='directory':require(cpu.string(cpu.r[4])=='\\','Unexpected original root directory')
            elif kind=='open':
                require(cpu.string(cpu.r[4])=='maigo.bin' and cpu.r[5]==0,'Unexpected original MAIGO open')
                cpu.r[0]=handle
            elif kind=='size':
                require(cpu.r[4]==handle,'Size query changed the admitted handle')
                cpu.write(cpu.r[5],len(maigo))
            elif kind=='read':
                require(cpu.r[4:7]==[handle,len(maigo),0x8c008300],'MAIGO read changed size/destination')
                cpu.store_block(cpu.r[6],maigo)
            else:require(cpu.r[4]==handle,'Close changed the admitted handle')
            cpu.pc=cpu.pr
            continue
        cpu.step()
    else:raise AssertionError('Original SAMPLE did not finish its MAIGO-success branch')
    require(calls==['directory','open','size','read','close'],'Original MAIGO operations changed')
    require(cpu.block(0x8c008300,len(maigo))==maigo and cpu.read(IP_FE,1)==1,
        'Original successful MAIGO load did not set its private-return marker')
    maigo_instructions=cpu.instructions
    rows=[]
    for name,fe in (('maigo_private',1),('firmware_fallback',0x20)):
        cpu.write(IP_FE,fe,1)
        cpu.store_block(MAINANA_DEST,sample[MAINANA_SOURCE:MAINANA_SOURCE+MAINANA_BYTES])
        cpu.pc=SOURCE_SELECT
        start=cpu.instructions
        for _ in range(1000):
            if cpu.pc==SELECT_END:break
            cpu.step()
        else:raise AssertionError('Original callback selection exceeded its bound')
        callback=cpu.read(PRIVATE_CELL)
        require(callback==(0x8c008300 if fe==1 else PRIVATE_STUB),'Original callback destination changed')
        if fe!=1:
            require(zlib.crc32(cpu.block(PRIVATE_STUB,32))==0xe66470fc,'Original fallback reset stub changed')
        rows.append(dict(case=name,marker=fe,callback=hex(callback),instructions=cpu.instructions-start))
    return dict(maigo_success_instructions=maigo_instructions,calls=calls,
        exact_maigo_bytes=len(maigo),maigo_ram_end=hex(0x8c008300+len(maigo)),
        marker_set_by_actual_source=True,callbacks=rows),cpu.block(PRIVATE_STUB,32)

def dispatch(build,pe,stub,kind,old=False):
    build=Path(build);controller=read_elf(build/'ce-return.elf');resident=read_elf(build/'resident-sci.elf')
    cpu=SourceCPU(b'')
    load_elf(cpu,controller);load_elf(cpu,resident)
    for section in pe.sections:
        body=pe.data[section['raw_offset']:section['raw_offset']+section['raw_bytes']]
        cpu.store_block(0x8c000000+pe.image_base+section['rva'],body+
            bytes(max(0,section['virtual_bytes']-len(body))))
    sy=controller[4];old_hook=resident[4]['_kui_retail_menu_hook'][0]
    cpu.write(sy['_kui_smash_ce_return_previous_e0'][0],old_hook)
    cpu.write(resident[4]['_kui_retail_original_menu'][0],BIOS)
    cpu.write(0xac0000e0,LOW)
    direct=kind=='maigo_private'
    cpu.write(IP_FE,1 if direct else 0x20,1)
    cpu.write(PRIVATE_CELL,0x8c008300 if direct else PRIVATE_STUB)
    cpu.store_block(PRIVATE_STUB,stub)
    cpu.write(0x8c000064,0xdeadbeef)
    cpu.write(0x8c05411c,SETKMODE)
    cpu.r=[0x11000000+i*0x101 for i in range(16)]
    cpu.r[15],cpu.sr,cpu.pr,cpu.pc=0x06123450,0,0x8c003100,CALLSITE
    cpu.gbr,cpu.vbr=0x02060000,0x8c010000
    calls=[];visited=[];main=sy['_kui_smash_ce_return_main'][0]
    wrapper_at=None
    for _ in range(1000):
        if cpu.pc in (CALLSITE,WRAPPER,0x8c008300,LOW,PRIVATE_STUB,old_hook,BIOS,main):
            visited.append(dict(pc=hex(cpu.pc),command=cpu.r[4],pr=hex(cpu.pr),sp=hex(cpu.r[15])))
        if cpu.pc==WRAPPER:wrapper_at=cpu.instructions
        if cpu.pc in (main,BIOS):break
        if cpu.pc==SETKMODE:
            calls.append(cpu.r[4]);require(cpu.r[4]==1,'Private wrapper used an unexpected mode switch')
            cpu.r[0]=0;cpu.sr|=0x40000000;cpu.pc=cpu.pr
            continue
        cpu.step()
    else:raise AssertionError('Swirl private return did not reach a bounded dispatch decision')
    require(calls==[1] and wrapper_at is not None,'Original SetKMode wrapper was bypassed')
    require(not cpu.raw_calls and not cpu.mmio_reads and not cpu.mmio_writes,
        'Dispatch performed retirement, card work or MMIO before C authority')
    expected_command=0 if direct else 3
    if old:
        require(direct and cpu.pc==BIOS and cpu.r[4]==0,'Old controller did not reproduce BIOS function0 forwarding')
        require(cpu.instructions-wrapper_at==37,'Old source-bound misdispatch instruction count changed')
        frame=None
    else:
        require(cpu.pc==main,'Original private return escaped to BIOS or the inherited renderer')
        frame=[cpu.read(cpu.r[4]+4*i) for i in range(21)]
        require(frame[16]==expected_command and frame[5]==PRIVATE_PR and frame[4]==0x40000000,
            'Captured private command/caller/SR differs from original Swirl source')
        require(cpu.r[4]+84==0x06123434 and frame[2:4]==[0x02060000,0x8c010000],
            'Private wrapper stack/GBR/VBR changed before authority checks')
        require(all(int(row['pc'],16) not in (old_hook,BIOS) for row in visited),
            'A qualified private return entered the old firmware route')
        require(cpu.read(0x8c000064)==(0xdeadbeef if direct else 0x8c008000),
            'Original fallback boot-vector side effect was lost or fabricated')
    return dict(case=kind,old_controller=old,instructions=cpu.instructions,
        instructions_from_wrapper=cpu.instructions-wrapper_at,visited=visited,
        captured_frame=frame,SetKMode_calls=calls,raw_calls=0,MMIO_operations=0,
        controller_elf_sha256=sha(controller[0]),resident_elf_sha256=sha(resident[0]))

def _terminal_cases(build,pe,stub):
    """Compose original wrapper/stub execution with the actual C guards/body."""
    build=Path(build)
    image=read_elf(build/'ce-return.elf');helper=read_elf(build/'helper/helper.elf')
    resident=read_elf(build/'resident-sci.elf')
    view=view_audit(build/'helper/helper.elf',build/'resident-sci.elf',build/'helper/helper.bin')
    view['old_e0']=resident[4]['_kui_retail_menu_hook'][0]
    sy=image[4];stop=sy['_stopped'][0];main=sy['_kui_smash_ce_return_main'][0]
    clear_proof=ram_clear_audit(image)
    clear_entry=sy['_kui_smash_ce_return_ram_clear'][0]
    cases=[]
    kinds=('maigo_private','firmware_fallback','private_wrong_fe','private_wrong_cell',
        'fallback_bad_stub','fallback_wrong_fe','fallback_wrong_cell','fallback_wrong_boot_vector','unknown_private_caller')
    for kind in kinds:
        cpu=terminal_fixture(image,helper,view,2,'idle')
        old_translate=cpu.translate
        cpu.translate=lambda address,prior=old_translate: (0x8c000000+address
            if 0x10000<=address<0xb4000 else prior(address))
        for section in pe.sections:
            body=pe.data[section['raw_offset']:section['raw_offset']+section['raw_bytes']]
            cpu.store_block(0x8c000000+pe.image_base+section['rva'],body+
                bytes(max(0,section['virtual_bytes']-len(body))))
        direct=kind in ('maigo_private','private_wrong_fe','private_wrong_cell','unknown_private_caller')
        cpu.write(IP_FE,1 if direct else 0x20,1)
        cpu.write(PRIVATE_CELL,0x8c008300 if direct else PRIVATE_STUB)
        cpu.store_block(PRIVATE_STUB,stub)
        cpu.write(0xac0000e0,LOW)
        cpu.write(0x8c000064,0xdeadbeef)
        cpu.write(0x8c05411c,SETKMODE)
        cpu.r[15],cpu.r[4],cpu.sr,cpu.pr,cpu.pc=0x06123450,0,0,0x8c003100,CALLSITE
        cpu.gbr,cpu.vbr=0x02060000,0x8c010000
        switches=[];arrived=False;captured=False
        start_writes=len(cpu.hardware_writes)
        for _ in range(5000000):
            if cpu.pc in (TARGET,stop,view['old_e0']):break
            if cpu.pc==SETKMODE:
                switches.append(cpu.r[4]);require(cpu.r[4]==1,'Private source used an unexpected SetKMode')
                cpu.r[0]=0;cpu.sr|=0x40000000;cpu.pc=cpu.pr
                continue
            if cpu.pc in (LOW,LOW^0x20000000) and not arrived:
                arrived=True
                # Mutation point follows the exact original wrapper/stub.
                # It tests authority independently of source discovery.
                if kind=='private_wrong_fe':cpu.write(IP_FE,0,1)
                elif kind in ('private_wrong_cell','fallback_wrong_cell'):cpu.write(PRIVATE_CELL,0x8c008304)
                elif kind=='fallback_bad_stub':cpu.write(PRIVATE_STUB,cpu.read(PRIVATE_STUB)^1)
                elif kind=='fallback_wrong_fe':cpu.write(IP_FE,1,1)
                elif kind=='fallback_wrong_boot_vector':cpu.write(0x8c000064,0x8c008004)
                elif kind=='unknown_private_caller':cpu.pr+=2
            if cpu.pc==main:
                captured=True
                require(cpu.read(cpu.r[4]+5*4)==PRIVATE_PR,'Captured caller does not match actual Swirl wrapper')
                require(cpu.read(cpu.r[4]+16*4)==(0 if direct else 3),'Captured private command changed')
            if cpu.pc==RAW:
                cpu.raw_read()
                continue
            if cpu.pc==clear_entry:
                cpu.ram_clear()
                continue
            cpu.step()
        else:raise AssertionError('Original Swirl composed terminal case exceeded bound: '+kind)
        success=kind in ('maigo_private','firmware_fallback')
        require(arrived and switches==[1],'Original private frontend was bypassed: '+kind)
        if success:
            require(captured and cpu.pc==TARGET,'Qualified private return did not reach the checked outer entry: '+kind)
            require(cpu.freeze_at is not None and len(cpu.raw_calls)==4 and cpu.hardware[0xff000010]==4,
                'Qualified private return bypassed terminal idle/quiescence/raw transport')
            require(cpu.ram_clear_calls==1,'Qualified private return bypassed terminal RAM retirement')
            require(cpu.block(TARGET+128,368)==cpu.expected_return and
                cpu.block(TARGET+496,80)==cpu.expected_video and
                cpu.block(TARGET+0x2a0,96)==cpu.expected_receipt,'Private return lost ROOT/video/receipt provenance')
            require([cpu.read(a) for a in FIRMWARE_ADDRESSES]==FIRMWARE,'Private return did not restore original firmware')
        else:
            require(cpu.freeze_at is None and not cpu.raw_calls and len(cpu.hardware_writes)==start_writes,
                'Refused private authority touched old producers or storage: '+kind)
            require(cpu.ram_clear_at is None,'Refused private authority cleared old RAM: '+kind)
            if kind=='unknown_private_caller':require(cpu.pc==view['old_e0'] and not captured,'Unrelated caller was claimed')
            else:require(cpu.pc==stop and captured and cpu.r[4]==2,'Malformed private callback was not refused by source guard')
        cases.append(dict(case=kind,instructions=cpu.instructions,source_wrapper_executed=True,
            actual_crc_decoder_instructions=getattr(cpu,'fast_instructions',0),
            controller_C_guards_executed=captured,outcome='outer_entry' if success else
                ('unchanged_old_hook_boundary' if kind=='unknown_private_caller' else 'source_guard_refusal'),
            raw_blocks=len(cpu.raw_calls),terminal_freeze=cpu.freeze_at is not None,
            ram_clear_calls=cpu.ram_clear_calls,
            root_video_receipt_exact=success))
    return cases,clear_proof

def terminal_cases(build,pe,stub):
    # Original wrapper/stub, controller guards and hardware retirement still
    # execute; only the actual linked CRC instruction decoder is shared with
    # the complete controller proof. No checksum values are substituted.
    with actual_crc_fixture(Path(build)/'ce-return.elf'):
        return _terminal_cases(build,pe,stub)

def audit(build,originals,oldbuild=None):
    originals=Path(originals)/'SWIRL'
    paths={name:originals/name for name in ORIGINALS}
    data={name:path.read_bytes() for name,path in paths.items()}
    require(all(sha(data[name])==digest for name,digest in ORIGINALS.items()),'Exact original Swirl source identity differs')
    source,stub=sample_sources(data['SAMPLE.BIN'],data['MAIGO.BIN'])
    pe=PE(paths['SWIRLDC.EXE'])
    require(pe.image_base==0x10000,'Original EXE nominal address changed')
    rows=[dispatch(build,pe,stub,kind) for kind in ('maigo_private','firmware_fallback')]
    if oldbuild:rows.append(dispatch(oldbuild,pe,stub,'maigo_private',old=True))
    terminal,clear_proof=terminal_cases(build,pe,stub)
    require(all(sha(path.read_bytes())==ORIGINALS[name] for name,path in paths.items()),'Original input changed during execution')
    dependencies={'tests/test_smash_swirl_return_source.py','tests/test_smash_menu_loader_runtime.py',
        'tests/test_smash_menu_loader_entry.py','tools/armada_pe_audio_audit.py',
        'src/loader/smash_ce_return_entry.S','src/loader/smash_ce_return_loader.c',
        'src/loader/smash_ce_return_ram_clear.S','src/loader/native_return_hardware.c',
        'src/loader/native_return_hardware.S','Makefile.smash-ce-return-probe',
        'tests/test_smash_ce_return_loader.py','tools/smash_ce_return_view.py',
        'src/loader/ce_return_crc32.h','src/loader/cold_load_crc32.c','tests/sh4_integer_fast.c'}
    for module in tuple(sys.modules.values()):
        name=getattr(module,'__file__',None)
        if name:
            path=Path(name).resolve()
            if path.is_relative_to(ROOT) and path.suffix=='.py':dependencies.add(str(path.relative_to(ROOT)))
    artifacts={name:sha((Path(build)/name).read_bytes()) for name in
        ('ce-return.elf','resident-sci.elf','stage.elf','helper/prepared.bin')}
    return dict(schema='kui.smash-swirl.original-private-return.v1',status='PASS',
        test_sha256=sha(Path(__file__).read_bytes()),original_sha256=ORIGINALS,
        source_sha256={name:sha((ROOT/name).read_bytes()) for name in sorted(dependencies)},
        artifacts_sha256=artifacts,
        sample_original_source=source,cases=rows,composed_terminal_cases=terminal,
        ram_clear_independent_linked_proof=clear_proof,ram_clear_effect_boundary=True,
        limits=['File open/size/read/close success, SetKMode and CE translation are explicit fixtures.',
            'Physical raw SD delivery is the existing four-block fixture; complete CRC and ROOT/video/receipt publication execute.',
            'The RAM-clear leaf has a separate complete linked-instruction proof; its verified bulk effect is substituted at its actual terminal entry here.',
            'No BIOS reset, CE scheduling, storage timing or console compatibility is simulated.'])

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--originals',type=Path,required=True)
    parser.add_argument('--oldbuild',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    result=audit(args.build,args.originals,args.oldbuild)
    rendered=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.write_text(rendered)
    print(rendered,end='')
