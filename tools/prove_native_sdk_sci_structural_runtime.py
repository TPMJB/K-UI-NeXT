#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Independent original SDK ABI and linked dynamic-route proof.

Only compact fingerprints are embedded. Original game bytes are supplied
privately. Production extent/discovery C and independent literal-role checks
bind each original; the original integer lookup and linked RTS callback then
execute directly. Full SDK FPU save/restore remains outside the integer model.
"""
import ctypes
from functools import lru_cache
import hashlib
import os
from pathlib import Path
import shlex
import struct
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from test_native_sdk_sci_owner_contract import contract_cases
from test_native_sdk_sci_helper_contract import helper_contract,helper_cases

LOAD=0x8c010000
PREFIX='483c13828a7ac711daf2d970fc6092da1c3582653614430723ef83e6a02cd696'
VECTOR='4a53c66eecb7d267f40612c8a81702095a816f103a4d9cc6d80d44c662998d07'
INITIALIZER='6c5b89764fba51b37c25a3bfa66c83e60116fa56e0c177a2009cc067f5a49f87'
FAMILIES=(
    dict(name='full-fpu',normal_bytes=0x168,normal_holes=(0x164,),
         normal_sha='56ed5e08b6693a31cbf2bed95fd3170fa2d41c62c3c0997740ac06b87cfd9644',
         fpu_offset=0x1f4,fpu_bytes=0x19c,fpu_holes=(0x198,),
         fpu_sha='f24c0441d0471c7116a151c32df200334e952a831425eaa7442af07761bb250e',
         event_offset=0x398,default_bytes=0xf8,default_holes=(0xe8,0xec,0xf0,0xf4),
         default_sha='b5a425f0649a8f1b5b2703b4b4d168e4cd0251789ed56af185de842ad27e13ff',
         dispatch_offsets=(0x84,0xa8),continuation_offsets=(0xb0,0xd4)),
    dict(name='compact-fpu',normal_bytes=0x138,normal_holes=(0x134,),
         normal_sha='3fba9f81660b83aaa409d520bf3dcdd5e2e568cf4d4abf116cf328e2e317f3ad',
         fpu_offset=0x1c4,fpu_bytes=0x16c,fpu_holes=(0x168,),
         fpu_sha='c7f3aa88c03c991f20078b1edddb67141f6957719c6b3e8f2273dd246eb49251',
         event_offset=0x338,default_bytes=0xfc,default_holes=(0xe8,0xec,0xf0,0xf4,0xf8),
         default_sha='d4bb5d00fb05a11d21443c92f8fe7918da0d929edbd1fbb71d6dae26761852fb',
         dispatch_offsets=(0x72,0x96),continuation_offsets=(0x9e,0xc2)),
)
PROGRAM_SOURCES=('src/core/vmu_native_program.c','src/core/vmu_native_heap.c',
    'src/core/vmu_native_exit.c','src/core/vmu_native_patch.c',
    'src/core/native_sdk_sci_route.c','src/core/hash.c','src/core/data.c')


def require(test,message):
    if not test:raise ValueError(message)


def normalized(body,holes):
    data=bytearray(body)
    for at in holes:
        require(0<=at<=len(data)-4,'normalized literal outside its exact window')
        data[at:at+4]=bytes(4)
    return hashlib.sha256(data).hexdigest()


def owner_at(address,count,n):
    require(address&0xe0000000 in (0,0x80000000),'SDK role uses a forbidden owner alias')
    offset=(address&0x1fffffff)-(LOAD&0x1fffffff)
    require(offset>=0 and offset<=n-count,'SDK role escapes normalized original image')
    return offset


@lru_cache(maxsize=1)
def production_library():
    directory=tempfile.TemporaryDirectory(prefix='kui-sdk-structural-proof-')
    output=Path(directory.name)/'production.so'
    subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),'-std=c11','-O2','-shared','-fPIC',
        '-I',str(ROOT/'include'),*[str(ROOT/name) for name in PROGRAM_SOURCES],'-o',str(output)],
        check=True,capture_output=True)
    library=ctypes.CDLL(str(output))
    library.kui_vmu_native_program_extent.argtypes=(ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint32,
        ctypes.POINTER(ctypes.c_size_t))
    library.kui_vmu_native_program_extent.restype=ctypes.c_bool
    return directory,library


class Plan(ctypes.Structure):
    _fields_=[(name,ctypes.c_uint32) for name in
        ('version','bytes','vbr','common','eri_slot','rxi_slot','callback','previous')]


def original_contract(path):
    """Independently derive the reviewed family, using no title allowlist."""
    source=Path(path).read_bytes();_,library=production_library()
    buffer=ctypes.create_string_buffer(source);count=ctypes.c_size_t()
    require(library.kui_vmu_native_program_extent(buffer,len(source),LOAD,ctypes.byref(count)),
            'production original executable lifetime extent refused')
    require(0<count.value<=len(source) and count.value<=0xc00000,'normalized original extent outside bounds')
    image=source[:count.value];n=len(image)
    starts=[];at=0
    while True:
        at=image.find(b'\x09\x00'*4,at)
        if at<0:break
        if not at&3 and at<=n-64 and hashlib.sha256(image[at:at+64]).hexdigest()==PREFIX:starts.append(at)
        at+=1
    require(len(starts)==1,'SDK original vector prefix is missing or ambiguous')
    vector=LOAD+starts[0];v=image[starts[0]:starts[0]+96]
    require(len(v)==96 and normalized(v,(0x4c,0x58))==VECTOR,'SDK vector normalized instructions differ')
    initializers=[];at=0
    while True:
        at=image.find(struct.pack('<2H',0x9511,0x9411),at)
        if at<0:break
        if not at&1 and at<=n-0x2e and normalized(image[at:at+0x2e],(0x2a,))==INITIALIZER:
            initializers.append(at)
        at+=1
    require(len(initializers)==1,'SDK callback initializer is missing or ambiguous')
    initializer=LOAD+initializers[0];default=struct.unpack_from('<I',image,initializers[0]+0x2a)[0]
    area=default&0xe0000000;default_offset=owner_at(default,0xfc,n)
    require(not default&3,'SDK original default is unaligned')
    candidates=[]
    for family in FAMILIES:
        normal=vector+0x8c;fpu=vector+family['fpu_offset'];event=vector+family['event_offset']
        if fpu-LOAD>n-family['fpu_bytes']:continue
        if (normalized(image[normal-LOAD:normal-LOAD+family['normal_bytes']],family['normal_holes'])!=family['normal_sha'] or
            normalized(image[fpu-LOAD:fpu-LOAD+family['fpu_bytes']],family['fpu_holes'])!=family['fpu_sha'] or
            normalized(image[default_offset:default_offset+family['default_bytes']],family['default_holes'])!=family['default_sha']):continue
        raw_normal=(normal&0x1fffffff)|area;raw_fpu=(fpu&0x1fffffff)|area;raw_event=(event&0x1fffffff)|area
        word=lambda address:struct.unpack_from('<I',image,owner_at(address,4,n))[0]
        require(word(vector+0x4c)==raw_event and word(vector+0x58) in (raw_normal,raw_fpu),
                'SDK vector common/event role or owner alias differs')
        require(word(normal+family['normal_holes'][0])==raw_event and
                word(fpu+family['fpu_holes'][0])==raw_event,'SDK common raw-event role differs')
        require((word(default+0xe8),word(default+0xec),word(default+0xf0))==
                (raw_event+4,raw_event-8,raw_event-4),'SDK default state roles differ')
        if family['default_bytes']==0xfc:require(word(default+0xf8)==raw_event,'compact default event role differs')
        helper=helper_contract(image,LOAD,default)
        commons=(raw_normal,raw_fpu);frames=[]
        save=struct.pack('<5H',0x4f33,0x4f43,0x4f22,0x4f13,0x4f23)
        restore=struct.pack('<5H',0x4f27,0x4f17,0x4f26,0x4f47,0x4f37)
        for common,length in zip(commons,(family['normal_bytes'],family['fpu_bytes'])):
            body=image[owner_at(common,length,n):owner_at(common,length,n)+length]
            saves=[i for i in range(0,len(body)-9,2) if body[i:i+10]==save]
            restores=[i for i in range(0,len(body)-9,2) if body[i:i+10]==restore]
            require(len(saves)==len(restores)==1 and saves[0]<restores[0],'SDK original control-frame pair differs')
            frames.append((common+saves[0],common+restores[0]))
        candidates.append(dict(family=family['name'],image=image,game=Path(path).parent.name,load=LOAD,
            vector=vector,event=raw_event,default=default,helper=helper,commons=commons,
            table_init=initializer,table_stop=initializer+0x1e,frame_pairs=tuple(frames),
            dispatches=tuple(common+offset for common,offset in zip(commons,family['dispatch_offsets'])),
            continuations=tuple(common+offset for common,offset in zip(commons,family['continuation_offsets'])),
            source_bytes=len(source),source_sha256=hashlib.sha256(source).hexdigest(),
            program_bytes=n,program_sha256=hashlib.sha256(image).hexdigest(),digest=hashlib.sha256(image).hexdigest()))
    require(len(candidates)==1,'SDK original whole common/default family differs or is ambiguous')
    result=candidates[0]
    result['dispatch']=result['dispatches'][1];result['continuation']=result['continuations'][1]
    result.update(vbr=0x8c00f400,eri_slot=0x8c00f65c,rxi_slot=0x8c00f660)
    # Replay the real pure production helper with a source-derived live RAM
    # view for each mode. This fixture does not claim observed console RAM.
    read_type=ctypes.CFUNCTYPE(ctypes.c_bool,ctypes.c_void_p,ctypes.c_uint32,ctypes.POINTER(ctypes.c_uint32))
    library.kui_native_sdk_sci_route_discover.argtypes=(ctypes.c_void_p,ctypes.c_size_t,ctypes.c_uint32,
        ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,read_type,ctypes.c_void_p,ctypes.POINTER(Plan))
    library.kui_native_sdk_sci_route_discover.restype=ctypes.c_int
    for common in result['commons']:
        live=bytearray(v);struct.pack_into('<I',live,0x50,0xfedcba98);struct.pack_into('<I',live,0x58,common)
        faults=[]
        def read(_context,address,out):
            try:
                require(not address&3,'production SDK live read unaligned')
                if 0x8c00fa00<=address<=0x8c00fa5c:value=struct.unpack_from('<I',live,address-0x8c00fa00)[0]
                elif address in (0x8c00f65c,0x8c00f660):value=default
                else:value=struct.unpack_from('<I',image,owner_at(address,4,n))[0]
                out[0]=value;return True
            except (ValueError,struct.error) as error:faults.append(str(error));return False
        reader=read_type(read);plan=Plan()
        require(library.kui_native_sdk_sci_route_discover(buffer,n,LOAD,0x8c00f400,0x600000f0,
            0x8c007300,reader,None,ctypes.byref(plan))==1 and not faults,
            'production SDK structural admission refused independently derived original')
        require(tuple(getattr(plan,name) for name,_ in Plan._fields_)==
                (1,32,0x8c00f400,common,0x8c00f65c,0x8c00f660,0x8c007300,default),
                'production SDK structural plan differs from independent owner roles')
    return result


def original_dispatch(resident,path):
    from check_vmu_native_sdk_sci_route import SDKCPU,GUARD
    owner=original_contract(path);image=owner['image'];previous=owner['default'];cases=0
    execution=contract_cases(owner);helper=helper_cases(owner)
    target=resident[4]['_kui_retail_sdk_sci_dispatch'][0]
    for vbr in (0x8c00f400,0x8cc80000):
        for dispatch,continuation in zip(owner['dispatches'],owner['continuations']):
            for mask in range(16):
                for bank in (0,1):
                    for event in (0x4e0,0x500):
                        for mode in ('handled','forward','busy','badguard','postguard'):
                            c=SDKCPU(resident,event,mask,bank,False,image,previous=previous,vbr=vbr)
                            c.write(owner['event'],event);c.r[0]=c.sr;c.targets={target}
                            c.callee_return=previous if mode=='forward' else 0
                            if mode=='busy':c.write(c.active,1)
                            if mode=='badguard':c.write(GUARD,0)
                            if mode=='postguard':c.corrupt_guard=True
                            c.run_to(dispatch,c.callback)
                            require(c.r[4]==event and c.pr==continuation and c.control['vbr']==vbr,
                                    'original SDK lookup failed raw event/JSR/VBR contract')
                            before=c.snapshot();c.writes=[]
                            c.run_symbol('_kui_retail_sdk_sci_entry',previous if mode in ('forward','busy','badguard') else continuation)
                            r,other,pr,sr,_,_,gbr,fpscr,fpul,fr,controls=before
                            require(c.r[8:]==r[8:] and c.other==other and c.r[4]==event and c.pr==pr and c.sr==sr,
                                    'linked callback changed original SDK return/context')
                            require((c.gbr,c.fpscr,c.fpul,c.fr,c.control)==(gbr,fpscr,fpul,fr,controls) and
                                    c.calls==int(mode not in ('busy','badguard')) and not c.vbr_writes and not c.iprb_writes,
                                    'linked callback changed SDK controls/FPU/priority or dispatch choice')
                            cases+=1
    metadata={key:value for key,value in owner.items() if key!='image'}
    return dict(owner_digest=owner['digest'],game=owner['game'],owner_program_bytes=len(image),
        original_sdk_owner_contract=metadata,original_sdk_owner_execution=execution,
        original_sci_default_helper_execution=helper,structural_family=owner['family'],
        actual_sdk_integer_dispatch_to_linked_callback_cases=cases,
        both_original_common_modes_executed=True,dynamic_vbrs=[hex(v) for v in (0x8c00f400,0x8cc80000)],
        actual_sdk_event_table_lookup_and_jsr_executed=True,raw_u0_default_and_continuation_preserved=previous<0x80000000,
        production_extent_and_structural_source_live_models_checked=True,
        source_identity_freshly_hashed=True,full_sdk_fpu_prologue_and_epilogue_executed=False,
        console_performance_verified=False)


def family_tables(stage):
    from check_vmu_native_worker_gateway import symbol_bytes
    expected={'_family_vector_prefix':bytes.fromhex(PREFIX),'_family_vector':bytes.fromhex(VECTOR),
        '_family_init':bytes.fromhex(INITIALIZER)}
    for family,prefix in zip(FAMILIES,('_family_','_compact_')):
        for kind in ('normal','fpu','default'):expected[prefix+kind]=bytes.fromhex(family[kind+'_sha'])
    expected['_helper_windows']=b''.join(struct.pack('<II',offset,count)+bytes.fromhex(digest) for offset,count,digest in (
        (0,0x34,'56fb7aca2af823e85502ad444fd1df4130a93b58536037fd856605b7b35c1423'),
        (0x120,12,'524235e1c936937f199fda4316661a5e48c51116261c205cd9eca3b471feee78'),
        (0x224,20,'fe0a08ca04e79c38dc469e8221aa20f3991dbdb49d8f3df7b78b115392c05940')))
    for base,wire in (('_vector_holes',struct.pack('<2H',0x4c,0x58)),
                      ('_default_holes',struct.pack('<5H',0xe8,0xec,0xf0,0xf4,0xf8)),
                      ('_init_holes',struct.pack('<H',0x2a))):
        matches=[name for name in stage[4] if name==base or name.startswith(base+'.')]
        require(len(matches)==1,'structural SDK typed literal metadata missing/ambiguous: '+base)
        expected[matches[0]]=wire
    _,normalizer,_=symbol_bytes(stage,'_normalized',2)
    zeros=[name for name in stage[4] if name.startswith('_zero.') and
           struct.pack('<I',stage[4][name][0]) in normalizer]
    require(len(zeros)==1,'structural SDK normalizer zero source is missing/ambiguous')
    expected[zeros[0]]=bytes(4)
    result={}
    for name,wire in expected.items():
        address,body,section=symbol_bytes(stage,name,1)
        require(section[2]&7==2 and 0x8ce00000<=address<address+len(body)<=0x8cfe0000 and body==wire,
                'structural SDK immutable family/role metadata differs: '+name)
        result[name]=dict(address=hex(address),bytes=len(body),sha256=hashlib.sha256(body).hexdigest())
    return dict(enabled=True,families=[family['name'] for family in FAMILIES],
        full_instruction_windows_separately_fingerprinted=True,
        only_named_literal_holes_normalized=True,arbitrary_ram_pointer_wildcards=False,
        tables=result)


def dynamic_runtime_cases(resident):
    from check_vmu_native_sdk_sci_route import SDKCPU,COOKIE,COOKIE_AT,OWNER_AT,FRAME,HOOKED,REARMS,assert_c_abi
    readiness=malformed=hooks=transitions=0
    owners=(0x8c000000,0x8c00f400,0x8c010000,0x8c200000,0x8cc80000,0x8cfff900)
    for vbr in owners:
        for mask in range(16):
            for variant in ('ready','cookie','owner','actual','eri','rxi'):
                c=SDKCPU(resident,mask,mask,vbr=vbr)
                if variant=='cookie':c.write(COOKIE_AT,COOKIE^1)
                if variant=='owner':c.write(OWNER_AT,vbr^0x100)
                if variant=='actual':c.control['vbr']=vbr^0x100
                if variant=='eri':c.write(c.eri_slot,c.previous)
                if variant=='rxi':c.write(c.rxi_slot,c.previous)
                c.writes=[];c.table_reads=[];before=c.snapshot();c.run_symbol('_sdk_sci_ready');assert_c_abi(c,before)
                require(c.r[0]==int(variant=='ready') and not c.writes,'dynamic SDK readiness/result/write differs')
                reads=2 if variant in ('ready','rxi') else 1 if variant=='eri' else 0
                require(c.table_reads==[(c.eri_slot,4),(c.rxi_slot,4)][:reads],
                        'dynamic SDK table read occurs before admission or at a fixed foreign VBR')
                readiness+=1
            for field in range(0,256,16):
                c=SDKCPU(resident,field,mask,vbr=vbr);c.iprb=0xa50f|field
                c.writes=[];before=c.snapshot();c.run_symbol('_hook');assert_c_abi(c,before)
                require(c.iprb&0xf0==0x10 and c.read(FRAME+4)==field and
                        c.read(FRAME+8)==0x10 and c.read(HOOKED)==1,'dynamic hook SCI owner differs')
                c.write(c.rxi_slot,c.previous);c.run_symbol('_hook')
                require(c.iprb&0xf0==0 and c.read(FRAME+4)==field and c.read(FRAME+8)==0,
                        'dynamic lost-route hook adopted our SCI field as the owner')
                c.run_symbol('_unhook')
                require(c.iprb&0xf0==field and not c.read(HOOKED) and not c.vbr_writes,
                        'dynamic unhook failed original SCI restoration')
                hooks+=1;transitions+=1
    invalid=(0,0x0c00f400,0xac00f400,0x8bffff00,0x8cfffa00,0x8cffff00,0x8d000000,0xffffffff,
             *range(0x8c00f401,0x8c00f500))
    for vbr in invalid:
        for mask in range(16):
            c=SDKCPU(resident,vbr,mask);c.write(OWNER_AT,vbr);c.control['vbr']=vbr
            c.watched_slots.update(((vbr+0x25c)&0xffffffff,(vbr+0x260)&0xffffffff))
            c.table_reads=[];c.writes=[];before=c.snapshot()
            c.run_symbol('_sdk_sci_ready');assert_c_abi(c,before)
            require(c.r[0]==0 and not c.writes and not c.table_reads,
                    'malformed canonical/alignment/span SDK owner reached a table read')
            malformed+=1
    return dict(actual_linked_readiness_cases=readiness,malformed_owner_cases_before_table_read=malformed,
        actual_linked_dynamic_hook_restoration_cases=hooks,actual_live_route_loss_transitions=transitions,
        vbrs=[hex(value) for value in owners],all16_caller_masks=True,
        canonical_p1_alignment_and_complete_vector_span_checked=True,
        no_callback_read_before_cookie_vbr_bounds_and_equality=True,
        fixed_foreign_callback_table_not_used=True,no_vbr_writes=True)


def check(resident,worker,stage,*,self_test=False,read_ahead_proof=None):
    from check_vmu_native_sdk_sci_route import (STRUCTURAL_MARKER,OWNER_CONSTANTS,MULTI_OWNER_MARKER,
        pin_bodies,stage_pins,multi_owner_table,callback_cases,ownership_cases)
    from check_vmu_native_worker_gateway import symbol_bytes
    from prove_native_sdk_sci_structural_cold import check as cold_check
    require(resident[4].get(STRUCTURAL_MARKER+'resident')==stage[4].get(STRUCTURAL_MARKER+'stage')==(1,0,0,0xfff1),
            'structural SDK stage/resident profile markers differ')
    require(read_ahead_proof and read_ahead_proof.get('structural_sdk_route') is True and
            read_ahead_proof.get('pio_pair_candidate') is True,
            'structural SDK requires the independently proved PIO pair companion')
    pins=pin_bodies(resident)
    cold_pins=stage_pins(stage,diagnostic=True,pio=True,pio_pair=True,multi_owner=True,structural=True)
    exact_table=multi_owner_table(stage);generic_table=family_tables(stage)
    cold=cold_check(stage,resident,self_test=self_test);dynamic=dynamic_runtime_cases(resident)
    callbacks={name:callback_cases(resident,previous=record['default']) for name,record in OWNER_CONSTANTS.items()}
    callbacks['raw_U0']=callback_cases(resident,previous=0x0c2d8dbc)
    ownership={name:ownership_cases(resident,previous=record['default']) for name,record in OWNER_CONSTANTS.items()}
    rejected=marker_refusals=0
    if self_test:
        for label,image,probe,names in (
            ('resident',resident,pin_bodies,tuple(pins)),
            ('stage',stage,lambda value:stage_pins(value,diagnostic=True,pio=True,pio_pair=True,
                multi_owner=True,structural=True),tuple(cold_pins))):
            for name in names:
                address,body,section=symbol_bytes(image,name,2);start=section[4]+address-section[3]
                for at in range(len(body)):
                    changed=bytearray(image[0]);changed[start+at]^=1
                    try:probe((bytes(changed),*image[1:]))
                    except ValueError:rejected+=1
                    else:raise ValueError('structural SDK accepted changed '+label+' instruction/literal: '+name)
        for table,probe in ((generic_table['tables'],family_tables),
                            ({'_owners':exact_table},multi_owner_table)):
            for name in table:
                address,body,section=symbol_bytes(stage,name,1);start=section[4]+address-section[3]
                for at in range(len(body)):
                    changed=bytearray(stage[0]);changed[start+at]^=1
                    try:probe((bytes(changed),*stage[1:]))
                    except ValueError:rejected+=1
                    else:raise ValueError('structural SDK accepted changed typed family/owner table: '+name)
        for image,suffix in ((resident,'resident'),(stage,'stage')):
            for replacement in (None,(0,0,0,0xfff1),(2,0,0,0xfff1),(1,4,1,1),(1,0,0,1)):
                symbols=dict(image[4]);name=STRUCTURAL_MARKER+suffix
                if replacement is None:symbols.pop(name)
                else:symbols[name]=replacement
                try:
                    if suffix=='resident':pin_bodies((*image[:4],symbols))
                    else:stage_pins((*image[:4],symbols),diagnostic=True,pio=True,pio_pair=True,
                        multi_owner=True,structural=True)
                except ValueError:marker_refusals+=1
                else:raise ValueError('structural SDK accepted a changed profile selector')
    totals=tuple(sum(value[i] for value in ownership.values()) for i in range(4))
    return dict(enabled=True,structural_profile=True,multi_owner=True,paired_sdk_sci_markers=True,
        immutable_descriptor_bound=True,multi_owner_admission_table=exact_table,generic_family_table=generic_table,
        cold_binder_and_admission_pins=cold_pins,cold_binder_instruction_modelled=True,
        cold_binder_execution=cold,dynamic_vbr_execution=dynamic,
        callback_cases_by_owner=callbacks,ownership_cases_by_owner=ownership,
        callback_instruction_cases=sum(callbacks.values()),hook_ownership_instruction_cases=totals[0],
        unhook_ownership_instruction_cases=totals[1],dispatcher_instruction_cases=totals[2],
        live_route_loss_transition_cases=totals[3],immutable_function_pins=pins,
        mutation_rejections=rejected,structural_marker_mutation_rejections=marker_refusals,
        exact_known_malformed_never_falls_back=True,unsupported_identity_uses_unique_structural_proof=True,
        arbitrary_owner_hash_bypass=False,callback_private_stack_prefix_bytes=0,sdk_owner_stack_save_bytes=16,
        all_16_sdk_caller_imasks_checked=True,both_gpr_banks_checked=True,
        sdk_original_sr_sp_pr_r4_and_callee_saved_preserved=True,busy_low_stack_never_entered=True,
        bad_guard_forwarded_without_c=True,default_callback_tail_forwarded=True,
        bootstrap_priority_zero=True,retained_priority_one=True,vbr_and_both_sdk_slots_rechecked=True,
        no_vbr_write_or_raw_rte_entry=True,no_fpu_state_changes=True,
        read_ahead_companion_required=True,read_ahead_diagnostic_companion_required=True,
        read_ahead_pio_companion_required=True,read_ahead_pio_pair_companion_required=True,
        cold_binder_source_sha256=hashlib.sha256((ROOT/'src/loader/vmu_native_sdk_sci_route_stage.inc').read_bytes()).hexdigest(),
        native_deep_reader_c_payload_model='ABI-adversarial substitute',
        sdk_full_fpu_context_model='owned by original SDK, outside this bounded integer model',
        console_cause_confirmed=False,console_performance_verified=False)
