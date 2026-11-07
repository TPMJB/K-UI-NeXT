#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute linked epoch import/descriptor/receipt control with explicit fixtures.

Actual SH import, codec/hash, descriptor construction, consume check and receipt
transition execute. Display/GIN setup, transport, map preflight and complete
stage_rebind are fixture boundaries. This does NOT model physical SD, worker
retirement, full journal/image I/O, cache timing or complete game execution.
The separate allocation-rebind sanitizer test qualifies the real core path.
"""
import argparse
import ctypes
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_native_gin_linked import Machine,State,STACK,RETURN
from vmu_persist_profile import decode_profile

PREFIX=0x8cc00000
def sha(data):return hashlib.sha256(data).hexdigest()
def seal(data,offset):
    data=bytearray(data);struct.pack_into('<I',data,offset,0)
    struct.pack_into('<I',data,offset,zlib.crc32(data));return bytes(data)
def ownership_sha(wire):
    parts=[b'KUI/VMU/OWNERSHIP/SHA256/V1\0']
    parts += [wire[o:o+n] for o,n in ((32,8),(40,8),(48,8),(20,4),(24,4),(28,4),
        (56,16),(72,16),(192,96),(288,4),(292,4),(296,4),(300,4),(304,4),(308,4),
        (312,8),(320,8),(328,8),(336,4),(340,4),(344,4),(352,4),(356,4),(360,8),(368,8))]
    for role in range(3):
        at=512+role*448
        parts += [wire[at+o:at+o+n] for o,n in ((0,8),(48,8),(8,4),(12,4),(16,4),(20,4),
            (24,8),(32,4),(36,4),(40,4),(44,4))]
        parts += [wire[at+64+kind*192+i*12:at+76+kind*192+i*12] for kind in range(2) for i in range(16)]
    return hashlib.sha256(b''.join(parts)).digest()
def authored(build,role,profile_path=None):
    wire=Path(profile_path or build/'vmu-sd-profile.bin').read_bytes();p=decode_profile(wire);g=p['geometry']
    spc=g['sectors_per_cluster'];lba=g['data_first_lba']+(g['cluster_count']-2048)*spc
    blocks=577;allocated=((blocks+spc-1)//spc+1)*spc
    config=bytearray(4224);struct.pack_into('<8I',config,0,0x5349554b,0x31304643,1,4224,role,29,295424,0)
    runs=struct.pack('<8I',lba,blocks,0,blocks,lba,allocated,0,allocated)
    config[32:64]=runs;own=ownership_sha(wire);config[64:96]=own
    commit=json.loads((build/'entry.compile.json').read_text())['commit'][:12].encode()
    config[96:108]=commit
    if role==1:config[112:4208]=wire
    config=seal(config,28)
    binding=hashlib.sha256(b'KUI/SMASH/CARD-SESSION/BINDING/V2\0'+commit+
        struct.pack('<2I',29,295424)+runs+own).digest()
    admission=hashlib.sha256(b'KUI/SMASH/LIVE-ALLOCATION-ADMISSION/V2\0'+binding+own).digest()
    return config,binding,admission,p,wire
def receipt(binding,admission,owner=1,sequence=1):
    return seal(struct.pack('<6I',0x5349554b,0x32305243,2,96,1|(owner<<8),29)+
        binding+admission+struct.pack('<2I',sequence,0),92)
def blank_receipt():return struct.pack('<4I',0x5349554b,0x32305243,2,96)+bytes(80)
def return_seed(context):
    if context is None:return struct.pack('<4I',0x5349554b,0x3154524d,1,368)+bytes(352)
    plan=seal(struct.pack('<16I',0x5349554b,0x31444c4d,1,64,2048,2048,0x12345678,128,100,4,0,4,0,0,0,0),60)
    seed=bytearray(struct.pack('<8I',0x5349554b,0x3154524d,1,368,3 if context else 1,0,0,0)+plan+bytes(272))
    if context:seed[352:364]=b'AUTHORED0123';seed[364:366]=bytes((1,4))
    return seal(seed,28)
def symbol(elf,name):return elf[4][name][0]
def patch_return(machine,elf,name,result=None):
    if name not in elf[4]:return
    code=struct.pack('<2H',0x000b,0x0009) if result is None else struct.pack('<3H',0xe000|(result&255),0x000b,0x0009)
    machine.put(symbol(elf,name),code)
def imported_machine(elf,run,config,wire_receipt,outer=False,context=True):
    m=Machine(run);m.load(elf)
    for name in ('_smash_phase_stage_display','_smash_native_gin_begin','_smash_phase_stage_begin',
                 '_retail_display_capture','_retail_display_restore','_smash_outer_return_mark'):
        patch_return(m,elf,name)
    m.put(symbol(elf,'__smash_card_session_config'),config)
    m.put(PREFIX+0x2a0,wire_receipt)
    if outer:m.put(symbol(elf,'__smash_menu_return_seed'),return_seed(context))
    m.c.pc=symbol(elf,'_kui_retail_stage_main');m.c.pr=RETURN;m.c.r[15]=STACK;m.c.r[4]=PREFIX+0x1000
    result=m.execute(stop=symbol(elf,'_stopped'),break_pc=symbol(elf,'_retail_display_line'),budget=10000000)
    imported=struct.unpack('<I',m.block(symbol(elf,'_smash_card_imported'),4))[0]
    assert result in (0,1) and imported==(result==1)
    return m,bool(imported)
def source_guards():
    stage=(ROOT/'src/loader/retail_stage.c').read_text();body=stage[stage.index('void kui_retail_stage_main('):]
    assert body.index('smash_card_session_import(wire);')<body.index('kui_retail_storage_init(&card,')
    source=(ROOT/'src/loader/smash_card_session_stage.inc').read_text()
    child=source[source.index('static int smash_card_session_prepare('):source.index('\n#else\n#include "kui/smash_menu_return.h"')]
    for forbidden in ('kui_vmu_fs_admit(', 'kui_vmu_fs_witness_replay(', 'kui_smash_session_file_load('):assert forbidden not in child
    assert child.index('kui_vmu_persist_stage_rebind_policy(')<child.index('kui_smash_card_receipt_transfer(')
    assert '&kui_vmu_persist_stage_exclusive_checkpoint' in child
    outer=source[source.index('static void smash_card_session_outer_prepare(void)'):]
    assert outer.index('kui_retail_storage_acquire(')<outer.index('smash_card_identity_matches(')<outer.index('kui_smash_card_receipt_transfer(')
    assert outer.index('smash_card_identity_matches(')<outer.index('kui_smash_card_receipt_ce_return(')
    return {'import_before_storage_init':True,'child_metadata_admission_replay_fileload_absent':True,
            'rebind_before_receipt_transition':True,'outer_identity_fence_before_transfer':True,
            'outer_identity_fence_before_dedicated_CE_return':True,
            'source_sha256':sha(source.encode())}
def decoder_source():
    # Add only arithmetic opcodes already modeled by the independent phase
    # clock Python decoder. The shared decoder file remains unchanged.
    source=(ROOT/'tests/sh4_integer_fast.c').read_text()
    source=source.replace('if(op==0x0009u)return;',
        'if(op==0x0019u){c->sr&=~0x301u;return;}\n    if(op==0x0009u)return;')
    source=source.replace('switch(op&0xf00fu) {','''switch(op&0xf00fu) {
    case 0x3004u:{
        uint32_t old_q=(c->sr>>8)&1u,mode=(c->sr>>9)&1u,q=r[n]>>31;
        v=(r[n]<<1)|(c->sr&1u);uint32_t updated=old_q==mode?v-r[m]:v+r[m];
        uint32_t carry=old_q==mode?updated>v:updated<v;
        uint32_t new_q=!mode?(!q?carry:1u-carry):(!q?1u-carry:carry);
        r[n]=updated;c->sr=(c->sr&~0x101u)|(new_q<<8)|(new_q==mode);return;}
    case 0x3005u:wide=(uint64_t)r[n]*r[m];c->macl=(uint32_t)wide;c->mach=(uint32_t)(wide>>32);return;
    case 0x200eu:c->macl=(r[n]&65535u)*(r[m]&65535u);return;
    case 0x200du:r[n]=(r[n]>>16)|(r[m]<<16);return;''')
    return source
def division_vectors(elf,run):
    assert '___udivsi3' in elf[4]
    m=Machine(run);m.load(elf);count=0
    for numerator in (0,1,65535,65536,0x7fffffff,0x80000000,0xffffffff):
        for denominator in (1,2,3,31,512,65535,0x80000000,0xffffffff):
            result,_,status=m.call(symbol(elf,'___udivsi3'),(numerator,denominator))
            assert status==0 and result==numerator//denominator
            count+=1
    return count
def native_descriptor_case(elf,run,build,mode):
    config,binding,admission,p,wire=authored(build,2)
    m,ok=imported_machine(elf,run,config,receipt(binding,admission));assert ok
    for name,result in (('_retail_display_line',None),('_kui_retail_storage_init',0),
        ('_kui_retail_storage_acquire',0),('_kui_retail_storage_release',None),('_kui_retail_storage_stop',0),
        ('_kui_sci_sd_healthy',1),('_kui_sci_sd_faulted',0),('_kui_vmu_persist_stage_admit',0),
        ('_kui_smash_phase_stage_mark',None)):
        patch_return(m,elf,name,result)
    card=symbol(elf,'_card');card_wire=bytearray(72);struct.pack_into('<I',card_wire,0,1)
    struct.pack_into('<Q',card_wire,32,p['card_sectors']);card_wire[40]=p['high_capacity'];card_wire[41]=1
    m.put(card,card_wire);m.c.pc=symbol(elf,'_native_persist_storage_init');m.c.pr=RETURN
    m.c.r[15]=STACK;m.c.r[4]=card;m.c.r[5]=1
    policy_api='_kui_vmu_persist_stage_rebind_policy' in elf[4]
    rebind=symbol(elf,'_kui_vmu_persist_stage_rebind_policy' if policy_api else '_kui_vmu_persist_stage_rebind')
    assert m.execute(stop=RETURN,break_pc=rebind,budget=10000000)==1
    if policy_api:
        policy=struct.unpack('<I',m.block(m.c.r[15],4))[0]
        assert policy==symbol(elf,'_kui_vmu_persist_stage_exclusive_checkpoint')
        assert struct.unpack('<4I',m.block(policy,16))==(1,16,1,0)
    transfer=m.c.r[6];data=m.block(transfer,160)
    assert struct.unpack_from('<4I',data)==(0x31544156,1,160,1)
    assert struct.unpack_from('<3Q',data,16)==(p['card_sectors'],p['partition_first'],p['partition_end'])
    assert struct.unpack_from('<5I',data,40)==(p['high_capacity'],29,2,1,2)
    assert data[60:76]==bytes.fromhex(p['cid']) and data[76:92]==bytes.fromhex(p['image_id'])
    assert data[92:124]==ownership_sha(wire) and data[124:156]==binding and data[156:160]==bytes(4)
    # Explicit complete backend boundary: the separate real-core suite tests
    # journal/image/checkpoint I/O. Here only caller consume/refusal is tested.
    if mode=='success':m.put(transfer+12,struct.pack('<I',3));m.c.r[0]=0
    elif mode=='not_consumed':m.c.r[0]=0
    else:m.put(transfer+12,struct.pack('<I',4));m.c.r[0]=0xfffffffd
    m.c.pc=m.c.pr
    result=m.execute(stop=RETURN,break_pc=symbol(elf,'_retail_display_restore'),budget=10000000)
    after=m.block(symbol(elf,'_smash_card_receipt'),96)
    if mode=='success':
        assert result==0 and m.c.r[0]==0
        assert after==receipt(binding,admission,2,2)
        assert struct.unpack('<I',m.block(symbol(elf,'_smash_card_attaches'),4))[0]==1
    else:assert result==1 and after==receipt(binding,admission)
    return {'case':mode,'descriptor_bytes':160,'sequence':2,'previous_owner':1,'next_owner':2,
            'receipt_advanced':mode=='success','explicit_checkpoint_policy':policy_api,
            'actual_sh_instructions':int(m.c.steps)}
def audit(native_build,outer_build,run):
    native=read_elf(native_build/'stage.elf');outer=read_elf(outer_build/'stage.elf')
    hashes={str(p):sha(p.read_bytes()) for p in (native_build/'stage.elf',outer_build/'stage.elf')}
    vectors=division_vectors(native,run);cases=[]
    for outer_mode,build,elf in ((False,native_build,native),(True,outer_build,outer)):
        config,binding,admission,_,_=authored(build,1 if outer_mode else 2,native_build/'vmu-sd-profile.bin')
        baseline=receipt(binding,admission,2 if outer_mode else 1,2 if outer_mode else 1)
        entries=[('valid',baseline,True,True),('blank',blank_receipt(),True,outer_mode)]
        old=bytearray(blank_receipt());old[4:8]=b'CR01';struct.pack_into('<I',old,8,1)
        entries += [('v1_refused',bytes(old),True,False),
            ('wrong_owner',receipt(binding,admission,1 if outer_mode else 3,2),True,False)]
        entries += [('exhausted_sequence',receipt(binding,admission,2 if outer_mode else 1,0xffffffff),True,False)]
        if outer_mode:
            entries += [('missing_terminal_context',baseline,False,False)]
            for role in (3,4):
                value=receipt(binding,admission,role,2)
                entries += [('CE'+str(role)+'_captured_context',value,True,True),
                    ('CE'+str(role)+'_configured_context_refused',value,False,False),
                    ('CE'+str(role)+'_blank_context_refused',value,None,False),
                    ('CE'+str(role)+'_exhausted_sequence',receipt(binding,admission,role,0xffffffff),True,False)]
        for name,value,context,want in entries:
            machine,ok=imported_machine(elf,run,config,value,outer_mode,context)
            assert ok==want,(outer_mode,name,ok,want)
            if outer_mode and name=='valid':
                value,_,status=machine.call(symbol(elf,'_kui_smash_card_receipt_transfer'),
                    (symbol(elf,'_smash_card_receipt'),1))
                assert status==0 and value==1
                assert machine.block(symbol(elf,'_smash_card_receipt'),96)==receipt(binding,admission,1,3)
            if outer_mode and name.endswith('_captured_context'):
                live=symbol(elf,'_smash_card_receipt');before=machine.block(live,96)
                value,_,status=machine.call(symbol(elf,'_kui_smash_card_receipt_transfer'),(live,1))
                assert status==0 and value==0 and machine.block(live,96)==before
                value,_,status=machine.call(symbol(elf,'_kui_smash_card_receipt_ce_return_valid'),(live,))
                assert status==0 and value==1 and machine.block(live,96)==before
                value,_,status=machine.call(symbol(elf,'_kui_smash_card_receipt_ce_return'),(live,))
                assert status==0 and value==1 and machine.block(live,96)==receipt(binding,admission,1,3)
            cases.append({'owner':'outer' if outer_mode else 'native','case':name,'imported':ok,
                          'stopped_before_storage_init':not ok,'actual_sh_instructions':int(machine.c.steps)})
    descriptors=[native_descriptor_case(native,run,native_build,mode) for mode in ('success','not_consumed','rebind_refused')]
    assert hashes=={p:sha(Path(p).read_bytes()) for p in hashes},'Linked inputs changed during proof'
    return {'schema':'kui.smash.epoch-high-wrapper.v2','status':'PASS','elf_sha256':hashes,
        'source_guards':source_guards(),'import_cases':cases,'descriptor_cases':descriptors,
        'fixture_boundaries':['display, return-progress renderer and GIN setup (renderer separately executes in test_smash_outer_return_visibility.py)','SD init/acquire/stop/release/health',
            'map preflight','complete stage_rebind backend'],
        'actual_crypto':True,'division_oracle_vectors':vectors,'decoder_source_sha256':sha(decoder_source().encode()),
        'physical_io_or_retirement_proved':False,'hardware_timing_measured':False}
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--native',type=Path,required=True)
    parser.add_argument('--outer',type=Path,required=True);parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='kui-epoch-high-') as directory:
        library=Path(directory)/'integer.so';decoder=Path(directory)/'integer.c';decoder.write_text(decoder_source())
        subprocess.run(['cc','-O3','-std=c11','-shared','-fPIC',str(decoder),'-o',str(library)],check=True)
        loaded=ctypes.CDLL(str(library));run=loaded.sh4_fast_run
        run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
        result=audit(args.native,args.outer,run)
    output=json.dumps(result,indent=2)+'\n'
    if args.report:args.report.write_text(output)
    print(output,end='')
if __name__=='__main__':main()
