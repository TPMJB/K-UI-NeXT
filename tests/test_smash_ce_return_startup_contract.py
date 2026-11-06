#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Contain CE return installation inside the accepted3337 startup contract.

This executes each actual linked late-copy success/refusal path and unrelated
E0 forwarding. It compares addresses, cache publication and caller state with
the pinned accepted artifacts. Return-only payload bytes and their CRC values
may change. The original CE scheduler, physical caches and execution after the
frozen helper initializer are not modeled; passing is not a console boot claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
import test_smash_ce_return_late as late

ARTIFACTS=('ce-return.elf','ce-return.bin','helper/helper.elf','helper/helper.bin',
           'resident-sci.elf','stage.elf','helper/prepared.bin','vc2-raw.bin')
ACCEPTED_BUILD='3337e554b21c'
NEGATIVE_BUILD='cd3f09733f90'
PINS={
 1:{
 'ce-return.elf':'bad2ffcdc93d2d77c8e1677f6e88ed5ebcb903cc281c5b18d7d141e36adc23fb',
 'ce-return.bin':'251dd99ec0c06fb0f7f247e2a230ebcd40d529963fa26c442cf2d968b1d81aba',
 'helper/helper.elf':'1bae997552d9221badefe69f8c040fd2a2f28901329c22dbb64c43c8243a33aa',
 'helper/helper.bin':'85e864ae8d386b4b2d5cec731f788441367fdf9f2bef86f57ccd12a430e214e8',
 'resident-sci.elf':'75eeb0a059b45e9d93436fc4ebfecd2763cc4a7bd5ebc25688b4f533bba3ee50',
 'stage.elf':'89f9b7a66d859c4a9c50276841d77fae322f44c6e415d4e9d51a73b31ba395a6',
 'helper/prepared.bin':'458011653e1b31c536bfd9ea84858b1444f1dedb819671d48ee79ae7cf62bbd5',
 'vc2-raw.bin':'7289dc76bd6ea1d3ddd4ae1f6f65aa1dbd1b31bc4e80f2345727bceb01d325c4'},
 2:{
 'ce-return.elf':'61eef255aba2159aea9ef16126e67e9dd6dc605b638cf42ef0cce4cf02ebaac3',
 'ce-return.bin':'b4f422fea4c50b5130e59a8fa454a32df9c2a7d50338fe8bf3af2eb40a1b1615',
 'helper/helper.elf':'620d289363d39f04f387c7f267caa58f9b637fb7f984f3f722ad0ff235608805',
 'helper/helper.bin':'47b5687e400b25b43ffa378b2b04171e59c039c57e018e83ac4de6bf7a8719e8',
 'resident-sci.elf':'ad02935f0ea0a25ab4a858700715476a073318c539ba47f605935d625e2b235e',
 'stage.elf':'fbe2d66dd1c8acb4423904ab34e37b4a2eb8ff3bf3b4dd3253c8f4a54715353b',
 'helper/prepared.bin':'16c692e7d21b6bc6b0e772977095a8c64cff4879c52943b4402800e33a21af81',
 'vc2-raw.bin':'7289dc76bd6ea1d3ddd4ae1f6f65aa1dbd1b31bc4e80f2345727bceb01d325c4'}}
NEGATIVE_PINS={
 1:{
 'ce-return.elf':'509d6539b02d0a5e2313f8366440b66450c5324882144b9649752015cf5b659b',
 'ce-return.bin':'b9a823afe260d576d045a03c72356c60ac5e540c991f54fcc8246a3444e2642b',
 'helper/helper.elf':PINS[1]['helper/helper.elf'],
 'helper/helper.bin':PINS[1]['helper/helper.bin'],
 'resident-sci.elf':'7f9ca3dcccf40c1c390378cb5e9fa5196199978498caeb5dd163f1def8fefe24',
 'stage.elf':'86eac5a0425d5145a7ec3bbd9fbf6a307202cfcd2164253c7ca9eed6c8982e56',
 'helper/prepared.bin':'cc2afd60e6cc5f032790c95ffdd201eacf0b0e2ca59c3a9a277d51216f65e311',
 'vc2-raw.bin':PINS[1]['vc2-raw.bin']},
 2:{
 'ce-return.elf':'e8981274f8bceeb151dd88b981a935ff2fad593e749cc2540582fc04d908649e',
 'ce-return.bin':'39971466889a86cb4d60f484107882cb922d3337cd40c9e6e7fc9c618982db33',
 'helper/helper.elf':PINS[2]['helper/helper.elf'],
 'helper/helper.bin':PINS[2]['helper/helper.bin'],
 'resident-sci.elf':'d8ed1c7acc2d541407a5c14aa7d364b8e5082ed5db602a87ee71c8df4123bd48',
 'stage.elf':'5d1e24675ec6db3f5bc75dd5054d4d150367046776d91bd6a134b0b3dd8f4c92',
 'helper/prepared.bin':'e298e4f05069ff8d948dd5235221d09f2c0503c35b3610ba9e4b461974c9d4e2',
 'vc2-raw.bin':PINS[2]['vc2-raw.bin']}}
GEOMETRY=('__smash_ce_return_begin','__smash_ce_return_binary_end',
 '__smash_ce_return_bss_begin','__smash_ce_return_bss_end',
 '__smash_ce_return_copy','_kui_smash_ce_return_runtime',
 '_kui_smash_ce_return_previous_e0','__smash_ce_return_stack_bottom',
 '__smash_ce_return_stack_top')


def require(ok,message):
    if not ok:raise AssertionError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def inputs(build):return {str((build/name).resolve()):sha((build/name).read_bytes())for name in ARTIFACTS}


def pinned(build,profile,pins):
    actual=inputs(build)
    require(actual=={str((build/name).resolve()):digest for name,digest in pins[profile].items()},
        'Reference artifacts do not match the exact reviewed '+build.name+' cohort')
    return actual


def section(image,symbol):
    owner=image[4][symbol][3];row=image[3][owner]
    require(row[1]==1 and row[2]&2,'Startup section is not initialized allocated data')
    return image[0][row[4]:row[4]+row[5]]


def geometry(image):
    symbols=image[4]
    return {name:dict(address=hex(symbols[name][0]),bytes=symbols[name][1])for name in GEOMETRY}


def initialized_image(image):
    rows=sorted((p[3],p[1],p[4])for p in image[2]if p[0]==1 and p[4])
    require(rows,'Controller has no initialized linked image')
    first=rows[0][0];end=max(address+count for address,_,count in rows)
    require(0<end-first<=7968,'Controller initialized image exceeds its bounded carrier')
    body=bytearray(end-first);previous=first
    for address,offset,count in rows:
        require(address>=previous,'Controller initialized load segments overlap')
        body[address-first:address-first+count]=image[0][offset:offset+count]
        previous=address+count
    return bytes(body)


def embedded(image,begin,end):
    first=image[4][begin][0];last=image[4][end][0]
    require(first<last,'Stage has an empty prepared transport')
    for segment in image[2]:
        if segment[0]==1 and segment[2]<=first<last<=segment[2]+segment[4]:
            offset=segment[1]+first-segment[2]
            return image[0][offset:offset+last-first]
    raise AssertionError('Prepared transport escapes initialized stage load bytes')


def resident_body(image):
    rows=[p for p in image[2]if p[0]==1 and p[2]==0x8c004000]
    require(len(rows)==1,'Resident lacks its unique native startup image')
    p=rows[0];body=bytearray(image[0][p[1]:p[1]+p[4]])
    matches=list(re.finditer(rb'BUILD ([0-9a-f]{12})\x00',body))
    require(len(matches)==1,'Resident build identity is missing or ambiguous')
    body[matches[0].start(1):matches[0].end(1)]=bytes(12)
    return bytes(body)


class StartupCPU(late.CopyCPU):
    def __init__(self,*args,**kwargs):
        self.observe=False
        super().__init__(*args,**kwargs)
        self.observe=True;self.write_addresses=[];self.mmio=[];self.executed=set()
        self.fpscr,self.fpul=0x000c0001,0xaabbccdd
        self.fr=[0x7fc00000+i*0x10101 for i in range(32)]
        self.sr=0x700080f1
        self.nonvolatile_before=self.nonvolatile()
        self.hook_argument=self.r[4]

    def nonvolatile(self):
        return (self.r[8:].copy(),self.pr,self.gbr,self.vbr,self.mach,self.macl,self.sr&~1,
                self.fpscr,self.fpul,self.fr.copy())

    def read(self,address,count=4):
        if self.observe and not 0x0c000000<=address&0x1fffffff<0x0d000000:
            self.mmio.append(('read',address,count))
            raise AssertionError('Late startup copy read an unauthored hardware address')
        return super().read(address,count)

    def write(self,address,value,count=4):
        if self.observe:
            if not 0x0c000000<=address&0x1fffffff<0x0d000000:
                self.mmio.append(('write',address,count))
                raise AssertionError('Late startup copy wrote a hardware address')
            # Include dirty-cache writeback as well as direct P2 stores. Data
            # content is deliberately excluded: the return payload may change.
            self.write_addresses.append((address,count))
        return super().write(address,value,count)

    def record(self,op,pc):
        require(op>>12!=0xf and op&0xf0ff not in
            (0x005a,0x006a,0x4052,0x4062,0x405a,0x406a,0x4056,0x4066),
            'Startup copy fetched an FPU or FPU-control instruction')
        self.executed.add((pc,op))

    def plain(self,op,pc):
        if self.observe:self.record(op,pc)
        return super().plain(op,pc)

    def step(self):
        self.record(self.read(self.pc,2),self.pc)
        return super().step()


def trace_hash(rows):return sha(b''.join(struct.pack('<2I',*row)for row in rows))


def replay(image,code,helper,raw,profile,kind):
    offset,runtime=late.runtime_fixture(code,image[4],profile,late.crc(raw))
    cpu=StartupCPU(code,helper,offset,runtime,image[4],kind,raw)
    for _ in range(200000):
        if cpu.pc==late.STOP:break
        cpu.step()
    else:raise AssertionError('Actual startup copy exceeded its instruction bound')
    require(cpu.nonvolatile()==cpu.nonvolatile_before and not cpu.mmio,
        'Startup copy changed caller nonvolatile/FPU state or accessed MMIO')
    require(cpu.block(late.RAW,512)==raw,'Startup copy changed the original raw reader')
    expected=0 if kind=='ready'else 0xffffffff
    require(cpu.r[0]==expected,'Startup copy success/refusal changed')
    row=dict(case=kind,result=0 if not expected else-1,instructions=cpu.instructions,
        hook_argument=hex(cpu.hook_argument),caller_nonvolatile_state_exact=True,
        FPU_state_exact=True,no_FPU_instructions=True,MMIO_operations=cpu.mmio,
        write_addresses_count=len(cpu.write_addresses),write_addresses_sha256=trace_hash(cpu.write_addresses),
        cache_publications_count=len(cpu.publications),
        cache_publications_sha256=sha(b''.join(struct.pack('<I',a)for a in cpu.publications)),
        raw512_unchanged=True)
    return row,cpu


def forward_cases(cpu):
    rows=[]
    # Command1 is the return-only route. Swirl0/3 use an unrelated PR here;
    # source-owned private callers remain separately qualified return paths.
    for command in (0,2,3,0xffffffff):
        cpu.r=[0x23450000+i*0x10203 for i in range(16)];cpu.r[4]=command;cpu.r[15]=late.STACK
        cpu.sr,cpu.pr,cpu.gbr,cpu.vbr,cpu.mach,cpu.macl=0x700080f1,late.STOP,0x8c000000,0x8c00f400,17,29
        cpu.pc=late.LOW;before=(cpu.r[2:].copy(),cpu.sr,cpu.pr,cpu.gbr,cpu.vbr,cpu.mach,cpu.macl,
            cpu.fpscr,cpu.fpul,cpu.fr.copy())
        count=len(cpu.write_addresses);mmio=len(cpu.mmio);start=cpu.instructions
        for _ in range(100):
            if cpu.pc==late.OLD_HOOK:break
            cpu.step()
        else:raise AssertionError('Unrelated E0 command failed to reach the original hook')
        after=(cpu.r[2:].copy(),cpu.sr,cpu.pr,cpu.gbr,cpu.vbr,cpu.mach,cpu.macl,
            cpu.fpscr,cpu.fpul,cpu.fr.copy())
        require(after==before and len(cpu.write_addresses)==count and len(cpu.mmio)==mmio,
            'Unrelated E0 command changed caller state, memory or MMIO')
        rows.append(dict(command=hex(command),outcome='unchanged_original_hook_boundary',
            instructions=cpu.instructions-start,writes=0,MMIO_operations=0,caller_state_exact=True))
    return rows


def inspect(build,profile):
    image=read_elf(build/'ce-return.elf');code=(build/'ce-return.bin').read_bytes()
    helper=(build/'helper/helper.bin').read_bytes();raw=(build/'vc2-raw.bin').read_bytes()
    sy=image[4];offset=sy['_kui_smash_ce_return_runtime'][0]-late.LOW
    require(code==initialized_image(image),'Controller binary differs from its actual initialized ELF')
    require(sy['__smash_ce_return_begin'][0]==late.LOW and
        sy['__smash_ce_return_binary_end'][0]-late.LOW==len(code) and
        sy['_kui_smash_ce_return_runtime'][1]==632 and not any(code[offset:offset+632]),
        'Static controller/runtime geometry is malformed')
    expected=helper+struct.pack('<8I',0x5349554b,0x31525443,1,32,41152,len(code),offset,632)+code
    expected+=bytes(49152-len(expected))
    require((build/'helper/prepared.bin').read_bytes()==expected,'Prepared startup transport differs from linked code')
    require(embedded(read_elf(build/'stage.elf'),'__vc2_vmu_blob_start','__vc2_vmu_blob_end')==expected,
        'Native stage embeds a different prepared startup transport')
    require(sha(helper)==PINS[profile]['helper/helper.bin']and sha(raw)==PINS[profile]['vc2-raw.bin'],
        'Startup changed the pinned helper or raw reader')
    resident=read_elf(build/'resident-sci.elf')
    address,prefix,_=symbol_bytes(resident,'_kui_vc2_ce_vmu_read_card',2)
    require(address==late.RAW and len(prefix)==260 and raw==prefix+bytes(252),
        'Current raw file differs from the actual resident reader plus canonical padding')
    rows=[];traces=[];forward=None
    for kind in ('ready','copy_readback_fault','changed_e0_owner'):
        row,cpu=replay(image,code,helper,raw,profile,kind)
        rows.append(row);traces.append((cpu.write_addresses,cpu.publications))
        if kind=='ready':forward=forward_cases(cpu)
    return dict(image=image,geometry=geometry(image),copy=section(image,'__smash_ce_return_copy'),
        entry=section(image,'_kui_smash_ce_return_entry'),resident=resident_body(resident),
        rows=rows,traces=traces,forward=forward,initialized_bytes=len(code))


def compare(candidate,accepted):
    before=accepted['image'][4];after=candidate['image'][4]
    entry=bytearray(accepted['entry']);old_main=struct.pack('<I',before['_kui_smash_ce_return_main'][0])
    slots=[i for i in range(0,len(entry)-3,4)if entry[i:i+4]==old_main]
    require(len(slots)==1,'Accepted entry lacks its unique return-only main-target literal')
    entry[slots[0]:slots[0]+4]=struct.pack('<I',after['_kui_smash_ce_return_main'][0])
    checks=dict(initialized_extent_exact=candidate['initialized_bytes']==accepted['initialized_bytes'],
        copy_entry_runtime_BSS_previousE0_stack_exact=candidate['geometry']==accepted['geometry'],
        linked_copy_template_exact=candidate['copy']==accepted['copy'],
        entry_opcodes_exact_except_return_main_literal=candidate['entry']==bytes(entry),
        resident_startup_bytes_exact_except_build_label=candidate['resident']==accepted['resident'],
        all_pre_initializer_write_addresses_exact=all(a[0]==b[0]for a,b in zip(candidate['traces'],accepted['traces'])),
        all_cache_publications_exact=all(a[1]==b[1]for a,b in zip(candidate['traces'],accepted['traces'])),
        copy_caller_nonvolatile_and_FPU_exact=all(row['caller_nonvolatile_state_exact']and row['FPU_state_exact']for row in candidate['rows']),
        copy_no_MMIO=all(not row['MMIO_operations']for row in candidate['rows']),
        copy_hook_argument_exact=all(a['hook_argument']==b['hook_argument']for a,b in zip(candidate['rows'],accepted['rows'])),
        copy_success_and_refusals_exact=all(a['result']==b['result']for a,b in zip(candidate['rows'],accepted['rows'])),
        non_reset_forwarding_exact=candidate['forward']==accepted['forward'])
    return dict(status='PASS'if all(checks.values())else'FAIL',checks=checks,
        geometry=candidate['geometry'],accepted_geometry=accepted['geometry'],
        initialized_bytes=candidate['initialized_bytes'],accepted_initialized_bytes=accepted['initialized_bytes'],
        allowed_entry_main_literal_offset=slots[0],cases=candidate['rows'],accepted_cases=accepted['rows'],
        non_reset_forwarding=candidate['forward'],
        limits=['Write traces cover actual linked deferred installation, dirty alias publication and E0 publication through its return to the original gate.',
            'Return-only payload contents and their CRC values are allowed to differ; their integrity/return behavior has independent gates.',
            'Original kernel execution, physical cache/MMU/interrupt timing and console startup success are not established.'])


def audit(build,profile,accepted=None,negative=None):
    build=Path(build);role='vc2'if profile==1 else'swirl'
    accepted=Path(accepted)if accepted else ROOT/'build'/('smash-shared-card-'+ACCEPTED_BUILD)/role
    negative=Path(negative)if negative else ROOT/'build'/('smash-shared-card-'+NEGATIVE_BUILD)/role
    before=inputs(build);accepted_inputs=pinned(accepted,profile,PINS);negative_inputs=pinned(negative,profile,NEGATIVE_PINS)
    sources={Path(__file__).resolve(),ROOT/'src/loader/smash_ce_return_copy.S',
        ROOT/'src/loader/smash_ce_return_entry.S',ROOT/'src/loader/smash_ce_return_loader.ld',
        ROOT/'src/loader/vc2_ce_vmu_gate.inc',ROOT/'src/loader/vc2_ce_vmu_late_stage.inc',
        ROOT/'src/loader/smash_ce_child_stage.inc',ROOT/'include/kui/smash_ce_transport.h'}
    for module in tuple(sys.modules.values()):
        name=getattr(module,'__file__',None)
        if name:
            path=Path(name).resolve()
            if path.is_relative_to(ROOT)and path.suffix=='.py':sources.add(path)
    source_before={str(path.relative_to(ROOT)):sha(path.read_bytes())for path in sorted(sources)}
    reference=inspect(accepted,profile);current=inspect(build,profile);growth=inspect(negative,profile)
    result=compare(current,reference);self_control=compare(reference,reference);negative_control=compare(growth,reference)
    require(self_control['status']=='PASS','Accepted3337 startup does not pass its own execution contract')
    require(negative_control['status']=='FAIL'and
        negative_control['checks']['initialized_extent_exact']is False and
        negative_control['checks']['all_pre_initializer_write_addresses_exact']is False,
        'Known cd3f controller growth did not fail geometry and actual write traces')
    require(before==inputs(build)and accepted_inputs==inputs(accepted)and negative_inputs==inputs(negative),
        'Startup-contract artifacts changed during execution')
    require(source_before=={str(path.relative_to(ROOT)):sha(path.read_bytes())for path in sources},
        'Startup-contract sources changed during execution')
    return dict(schema='kui.smash-ce.return-startup-contract.v1',profile=profile,**result,
        test_sha256=source_before['tests/test_smash_ce_return_startup_contract.py'],source_sha256=source_before,
        inputs=before,accepted_inputs=accepted_inputs,negative_inputs=negative_inputs,
        inputs_immutable_during_execution=True,artifact_coupling_verified=True,
        controls=dict(accepted3337_self=dict(status=self_control['status'],all_checks_pass=True),
            cd3f_growth=dict(status=negative_control['status'],checks=negative_control['checks'],
                initialized_bytes=negative_control['initialized_bytes'],accepted_initialized_bytes=negative_control['accepted_initialized_bytes'],
                geometry=negative_control['geometry'],cases=negative_control['cases'])),
        console_startup_claim=False)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--profile',type=int,choices=(1,2),required=True)
    parser.add_argument('--accepted',type=Path)
    parser.add_argument('--negative-build',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args();report=audit(args.build,args.profile,args.accepted,args.negative_build)
    text=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.write_text(text)
    print(text,end='')
    raise SystemExit(0 if report['status']=='PASS'else 1)
