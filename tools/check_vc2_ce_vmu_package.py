#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the linked VC2 blank-card experiment, not console compatibility.

Checks actual ELF LMAs/aliases, immutable helper packing, the low map/stack
budget and the emitted finite C callgraphs. No retail instructions or bytes
are read or emitted. Driver-source and live ownership proofs remain separate.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import zlib

from check_loader_layout import EH, PH, SH, inspect_elf, region
from check_vmu_native_worker_gateway import read_elf
import check_retail_stack as stack
import check_armada_ce_vmu_a2 as a2
from check_retail_instructions import audit as instructions
from runtime_package import flatten_elf, verify
from check_vc2_cold_tables import audit as cold_tables
from vmu_persist_profile import template_profile
import armada_ce_cache_snapshot_contract as snapshot_contract

ROOT=Path(__file__).resolve().parents[1]
BEGIN, END, P2=0x8cfc4000,0x8cfd0000,0x20000000
RAW_P1, RAW_P2, RAW_BYTES = 0x8c008100, 0xac008100, 512
WRITE_PROBE_MARKER='__kui_vc2_vmu_write_probe'
WRITE_PROBE_ENTRY='_kui_vc2_ce_vmu_write_probe_handle'
PERSIST_MARKER='__kui_vc2_vmu_persist'
PERSIST_BEGIN=0x8cfc0000
PERSIST_HANDOFF=0xacfce200
PERSIST_SERVICES=0xacfce700
PERSIST_WORKER=(0xacfce800,0xacfcf800)
GD_WAIT_P2=0xacfce000
GD_WAIT_TEMPLATE=bytes.fromhex(
    '7361182101892b430900036b236c28d11b2be07f422f511f621f731f1200041f0a00'
    '051f1a00061f87548855f266936720d0f07f0b400900107f036dd82d2489f4501e40'
    '329eb2600820118902881f8b104e2089f07f01e488500e400b4d0900107f0200f0cb'
    '0e40f4501e40ebaf0900f4501e40f5500a40f6501a40f054f155f256f357207f2b4c'
    '090001e405a0d36503e402a0036502e408958756885707df05d0f07f0b400900feaf'
    '09000004090000000020000000000000000000fefcac')


def require(ok,message):
    if not ok:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def source_profile(image):
    """Select only the exact compile-time source profile carried by the ELF."""
    marker=image[4].get('__kui_smash_swirl_ce_vmu')
    if marker is not None:
        require(marker[0]==1 and marker[3]==0xfff1,'Invalid Sega Swirl source marker')
        return dict(name='Sega Swirl',bridge_begin=0x01ce7c64,bridge_end=0x01ce7cc8,
                    completion=0x01ce6f98,setkmode=0x01ce783c,
                    current_process=0x8c1d1890,current_thread=0x8c1d1894,slot='A1',
                    protected_begin=0x8cfb8000,relocated_text_bytes=0x8000)
    return dict(name='Virtua Cop 2',bridge_begin=0x01ce7c34,bridge_end=0x01ce7c98,
                completion=0x01ce6f64,setkmode=0x01ce7808,
                current_process=0x8c1d5890,current_thread=0x8c1d5894,slot='A2',
                protected_begin=0x8cfc0000,relocated_text_bytes=0)


def bridge_contract(bridge,callback,profile):
    """Pin authored ABI instructions and each source-specific relative call."""
    template=bytearray.fromhex(
        '862f962fa62f224f024f124f134ff07f436801e4debd090003690eda00e593660b4a8'
        '3649364d5bd090081b9836401e4d0bd090001e503660b4a83649364c9bd0900107f'
        '174f164f064f264ff66af6690b00f6688840fcac090009000900090009000900')
    require(len(bridge)==len(template)==100,'Mapped bridge size differs')
    calls=[]
    for offset,key in ((0x14,'setkmode'),(0x26,'setkmode'),(0x2a,'completion'),
                       (0x30,'setkmode'),(0x3e,'setkmode')):
        displacement=profile[key]-(profile['bridge_begin']+offset+4)
        require(displacement%2==0 and -4096<=displacement<=4094,
                'Mapped bridge call is out of BSR reach')
        struct.pack_into('<H',template,offset,0xb000|((displacement//2)&0xfff))
        calls.append(dict(offset=offset,target=hex(profile[key])))
    struct.pack_into('<I',template,0x54,callback)
    require(bridge==template,'Mapped bridge instructions, argument homes, calls or callback differ')
    return dict(bytes=100,argument_home_bytes=16,callback=hex(callback),calls=calls)


def persistent_snapshot_contract(image,profile):
    symbols=image[4]
    address,size,kind,owner=symbols[snapshot_contract.LEAF_SYMBOL]
    job,job_bytes,job_kind,job_owner=symbols[snapshot_contract.JOB_SYMBOL]
    require(kind==2 and size==len(snapshot_contract.BODY) and
            job_kind==1 and job_bytes==snapshot_contract.JOB_BYTES and job%4==0,
            'Persistent cache leaf or typed private job changed')
    require(symbols['__kui_maple_workspace_start'][0]<=job and
            job+job_bytes<=symbols['__kui_maple_workspace_end'][0],
            'Persistent cache job leaves owned P2 workspace')
    expected=bytearray(snapshot_contract.BODY)
    struct.pack_into('<I',expected,snapshot_contract.JOB_LITERAL_OFFSET,job)
    struct.pack_into('<I',expected,snapshot_contract.PRIVATE_LITERAL_OFFSET,profile['protected_begin'])
    require(payload_at(image,address,size)==expected,
            'Persistent cache leaf code, private-span exclusion or job identity differs')
    return dict(complete_bytes_pinned=True,bytes=size,job_p2=hex(job),
                protected_begin=hex(profile['protected_begin']),protected_end=hex(END))


def objdump_tool():
    found=shutil.which('sh-elf-objdump')
    if not found:
        candidate=ROOT.parent/'K-UI-NeXT/.deps/sh-elf/bin/sh-elf-objdump'
        if candidate.exists():found=str(candidate)
    require(found is not None,'Native objdump unavailable')
    return found


def sections(image):
    data,h,_,rows,_=image
    names=rows[h[13]]
    strings=region(data,names[4],names[5],'section names')
    result={}
    for row in rows:
        start=row[0];end=strings.find(b'\0',start)
        require(0<=start<len(strings) and end>=start,'Bad section name')
        name=strings[start:end].decode('ascii')
        require(name not in result,'Duplicate section name')
        require(row[1] not in (4,6,9),'Relocation/dynamic section in final ELF')
        result[name]=row
    return result


def payload_at(image,address,size):
    for kind,offset,va,_,file_size,_,_,_ in image[2]:
        if kind==1 and va<=address and address+size<=va+file_size:
            return region(image[0],offset+address-va,size,'ELF payload')
    raise ValueError('Initialized bytes escape ELF: '+hex(address))


def comparison_profile(image,persistent):
    """Read the actual immutable selector; every control keeps full code."""
    name='_kui_armada_ce_maple_diagnostic_mode'
    record=image[4].get(name)
    require(record is not None,'VC2 diagnostic selector is missing')
    address,size,kind,owner=record
    require(size==4 and kind==1 and not address&3 and owner<len(image[3]),
            'VC2 diagnostic selector must be one aligned typed four-byte object')
    section=image[3][owner]
    require(section[1]==1 and section[2]&7==6 and
            section[3]<=address<address+4<=section[3]+section[5],
            'VC2 diagnostic selector must remain initialized immutable helper data')
    mode=struct.unpack('<I',payload_at(image,address,4))[0]
    require(mode in (0,1,3,4) and (mode not in (3,4) or persistent),
            'Unsupported VC2 diagnostic selector')
    return dict(mode=mode,virtual_card_enabled=mode==0,
                sampler_enabled=mode not in (3,4),dma_fence_enabled=mode not in (3,4),
                maple_hook_installed=mode!=4,maple_text_extended=mode!=4,
                kernel_ramend_reserved=True,
                runtime_storage_enabled=persistent and mode==0)


def audit_comparison_noop(image):
    """Execute the emitted mode3 path with guest/MMIO deliberately absent."""
    from check_vmu_native_stage_stack import Machine
    from check_vmu_native_worker_gateway import symbol_bytes
    selector=image[4]['_kui_armada_ce_maple_diagnostic_mode'][0]
    _,body,_=symbol_bytes(image,'_kui_armada_ce_maple_traffic_run',2)
    class NoOp(Machine):
        def __init__(self,seed,mask,phase,override=None):
            super().__init__(image,'_kui_armada_ce_maple_traffic_run',seed,mask,0x8c61bfc0,body=override)
            self.r[4:8]=[0xdeadc0de,phase,0xfedcba98,0xffffffff]
            self.before=self.r.copy(),self.pr,self.mach,self.macl,self.gbr,self.sr,self.fpscr,self.fr.copy()
            self.selector_reads=0
        def read(self,address):
            if address==selector:self.selector_reads+=1;return 3
            return super().read(address)
        def call(self,target):
            require(target==self.stop_pc or self.base<=target<self.base+len(self.blob),
                    'VC2 no-op comparison calls an owner, sampler, fence or runtime provider')
            return False
        def check(self):
            before,pr,mach,macl,gbr,sr,fpscr,fr=self.before
            require(self.selector_reads==1 and self.r[0]==0 and self.r[8:]==before[8:] and
                    (self.pr,self.mach,self.macl,self.gbr)==(pr,mach,macl,gbr) and
                    self.sr&~1==sr&~1 and self.fpscr==fpscr and self.fr==fr and not self.published,
                    'VC2 no-op comparison changed preserved caller state or performed cache work')
    cases=0
    for seed in range(8):
        for mask in range(16):
            for phase in (0,1,2,0xffffffff):
                NoOp(seed,mask,phase).run().check();cases+=1
    at=body.find(struct.pack('<H',0x8803),0,64)
    require(at>=0,'VC2 comparison lacks early selector-three branch')
    mutations=0
    for offset,word in ((at,0x8802),(at+2,0x0009)):
        changed=bytearray(body);struct.pack_into('<H',changed,offset,word)
        try:NoOp(7,15,0,bytes(changed)).run().check()
        except (ValueError,AssertionError,struct.error):mutations+=1
        else:raise ValueError('VC2 no-op comparison accepted missing or wrong early branch')
    return dict(instruction_cases=cases,mutation_rejections=mutations,
        selector_value_modeled=3,selector_reads=1,guest_or_mmio_reads=0,provider_calls=0,
        cache_operations=0,only_private_c_stack_written=True,
        pre_post_resume_invalid_phase_cases=True,callee_preserved_context_unchanged=True)


def audit_late_resident(build,resident,stage,prepared_crc32,persistent=False):
    """Validate the separate bootstrap reader before checking the low image.

    The raw reader has one separately copied RX segment, not part of the
    ordinary resident payload. Only after pinning its entire segment and
    section do we mask those two headers in an in-memory copy for the
    unchanged single-reservation ELF auditor. The on-disk ELF is untouched.
    """
    data,header,segments,rows,symbols=resident
    manifest=symbols['_manifest']
    require(symbols.get('_row')==(manifest[0]+824,4,1,manifest[3]),
            'Late diagnostic cursor overlaps captured words')
    raw_section=sections(resident).get('.vc2_late_raw')
    require(raw_section is not None,'Missing late raw reader section')
    raw_index=rows.index(raw_section)
    _,kind,flags,address,offset,size,*_=raw_section
    require(kind==1 and flags==6 and address==RAW_P2 and 0<size<=RAW_BYTES and size%4==0,
            'Late raw reader section must be bounded P2 executable bytes')
    entry,length,symbol_kind,index=symbols['_kui_vc2_ce_vmu_read_card']
    require(entry==RAW_P2 and 0<length<=size and symbol_kind==2 and index==raw_index,
            'Late raw reader entry differs')
    loads=[(i,p) for i,p in enumerate(segments) if p[0]==1]
    require(len(loads)==2,'Late resident must have exactly two load segments')
    raw=[(i,p) for i,p in loads if p[2]==RAW_P2]
    require(len(raw)==1,'Late raw reader has no unique load segment')
    raw_ph,segment=raw[0]
    require(segment[1:7]==(offset,RAW_P2,RAW_P1,size,size,5),
            'Late raw reader physical load range/permissions differ')
    content=region(data,offset,size,'late raw reader')
    require(content==payload_at(resident,RAW_P2,size),'Late raw section/segment bytes differ')
    for i,row in enumerate(rows):
        if not row[2]&2 or not row[5] or i==raw_index:continue
        require(0x8c004000<=row[3]<row[3]+row[5]<=0x8c007ea0,
                'Unexpected allocation outside low resident: '+hex(row[3]))
    filtered=bytearray(data)
    # PT_NULL only for the already-audited raw reader; all other headers,
    # symbol definitions and the original entry remain independently checked.
    PH.pack_into(filtered,header[5]+raw_ph*PH.size,0,*segment[1:])
    ignored=list(raw_section);ignored[2]=0
    SH.pack_into(filtered,header[6]+raw_index*SH.size,*ignored)
    checked=inspect_elf(bytes(filtered),0x8c004000,0x8c007ea0)
    padded=content+bytes(RAW_BYTES-size)
    raw_file=(build/'vc2-raw.bin').read_bytes()
    require(raw_file==padded,'Late raw padded file differs from linked section')
    ss=stage[4]
    begin=ss['__vc2_late_raw_start'][0];end=ss['__vc2_late_raw_end'][0]
    require(end-begin==RAW_BYTES and payload_at(stage,begin,RAW_BYTES)==padded,
            'Stage embeds another late raw reader')
    raw_crc=zlib.crc32(padded)
    generated=(build/'vc2_late_symbols.h').read_text()
    numbers={k:int(v,16) for k,v in re.findall(
        r'#define (KUI_VC2_LATE_\w+) (0x[0-9a-f]+)',generated)}
    require(numbers.get('KUI_VC2_LATE_MAP_P2')==symbols['_manifest'][0]+P2 and
            numbers.get('KUI_VC2_LATE_RAW_CRC32')==raw_crc,
            'Late generated map/reader CRC differs')
    seed_address,seed_size,seed_kind,_=ss['_kui_vc2_vmu_late_seed']
    expected=struct.pack('<11I',0x32554d56,0x44414f4c,2 if persistent else 1,44,49152,
                         int(prepared_crc32,16),0,0,0,0,0)
    require(seed_size==44 and seed_kind==1 and seed_address%4==0 and
            payload_at(stage,seed_address,44)==expected,'Late stage seed binding differs')
    stage_bytes=(build/'stage.bin').read_bytes()
    require(stage_bytes.count(expected)==1,'Late stage seed is not unique')
    return checked,dict(raw_runtime=hex(RAW_P2),raw_physical=hex(RAW_P1),
                        raw_code_bytes=size,raw_padded_bytes=RAW_BYTES,
                        raw_crc32=f'{raw_crc:08x}',raw_sha256=sha(padded),
                        seed_bytes=44,helper_file_bytes=49152,
                        diagnostic_snapshot_words=168,diagnostic_cursor_offset=824,
                        raw_island_excluded_from_resident_payload=True,
                        high_helper_loaded_at_pre_nk_gate=True,
                        seed_version=2 if persistent else 1,
                        capsule_blocks=99 if persistent else 96)


def write_probe_profile(symbols):
    marker=symbols.get(WRITE_PROBE_MARKER)
    entry=symbols.get(WRITE_PROBE_ENTRY)
    if marker is None and entry is None:return False
    require(marker is not None and marker[0]==1 and marker[3]==0xfff1,
            'Write probe marker must be exact absolute1')
    require(entry is not None and entry[2]==2 and entry[1]>0,
            'Write probe backend entry is missing')
    forbidden_prefixes=('_kui_armada_ce_vmu_sd_', '_kui_armada_ce_vmu_runtime_',
        '_kui_vmu_store_', '_kui_vmu_durable_', '_kui_vmu_deferred_',
        '_kui_sci_sd_', '_kui_loader_sd_', '_kui_sci_stream_', '_kui_retail_sd_')
    forbidden_names={'__kui_maple_write_runtime','_a2_continuation',
                     '_kui_armada_ce_maple_bridge_return'}
    bad=[name for name in symbols if name in forbidden_names or
         name.startswith(forbidden_prefixes)]
    require(not bad,'Write probe links storage/runtime service: '+', '.join(sorted(bad)))
    for name in (WRITE_PROBE_ENTRY,'_probe_read_blank','_probe_commit_pending',
                 '_kui_vc2_ce_vmu_write_probe_snapshot'):
        require(name in symbols and symbols[name][2]==2 and symbols[name][1]>0,
                'Write probe callback/snapshot is missing: '+name)
        at,size,_,_=symbols[name];physical=at&~P2
        require(BEGIN<=physical<physical+size<=END,
                'Write probe function leaves helper reservation: '+name)
    return True


def persistent_profile(symbols):
    marker=symbols.get(PERSIST_MARKER)
    runtime=symbols.get('__kui_maple_write_runtime')
    if marker is None:
        require(runtime is None,'VC2 runtime lacks its distinct persistent marker')
        return False
    require(marker[0]==1 and marker[3]==0xfff1,
            'Persistent marker must be exact absolute 1')
    require(runtime is not None and runtime[0]==1 and runtime[3]==0xfff1,
            'Persistent VC2 helper lacks writable runtime marker')
    require(WRITE_PROBE_MARKER not in symbols and WRITE_PROBE_ENTRY not in symbols,
            'Persistent VC2 helper retains the volatile write probe')
    required=('_kui_armada_ce_vmu_runtime_bind','_kui_armada_ce_vmu_runtime_handle',
        '_kui_armada_ce_vmu_runtime_work','_kui_armada_ce_maple_runtime_wait',
        '_kui_armada_ce_maple_native_wait','_kui_vc2_ce_vmu_loan_begin',
        '_kui_vc2_ce_vmu_loan_end','_kui_vmu_store_write',
        '_runtime_cached_read','_runtime_cached_commit')
    for name in required:
        require(name in symbols and symbols[name][2]==2 and symbols[name][1]>0,
                'Persistent VC2 function missing: '+name)
    return True


def persistent_call_homes(image):
    """Bind all four runtime C transitions to distinct 16-byte homes."""
    symbols=image[4]
    begin=symbols['_kui_armada_ce_maple_hook'][0]
    end=symbols['__kui_maple_traffic_shim_end'][0]
    code=payload_at(image,begin,end-begin)
    pattern=struct.pack('<4H',0x7ff0,0x400b,0x0009,0x7f10)
    sites=[at for at in range(0,len(code)-len(pattern)+1,2)
           if code[at:at+len(pattern)]==pattern]
    expected=('_kui_armada_ce_maple_traffic_run','_kui_armada_ce_vmu_runtime_work',
              '_kui_armada_ce_maple_runtime_wait','_kui_armada_ce_maple_traffic_run')
    require(len(sites)==len(expected),'Persistent shim needs four complete caller-home call pairs')
    require(sum(word==0x400b for word in struct.unpack('<'+str(len(code)//2)+'H',code))==4,
            'Persistent shim has an unaccounted indirect call')
    calls=[]
    for at,target in zip(sites,expected):
        require(at>=2,'Persistent call lacks its source target load')
        load=struct.unpack_from('<H',code,at-2)[0]
        require(load&0xff00==0xd000,'Persistent C call target must use its retained literal')
        literal=((begin+at-2+4)&~3)+4*(load&255)
        require(struct.unpack('<I',payload_at(image,literal,4))[0]==symbols[target][0],
                'Persistent caller-home call target differs: '+target)
        calls.append(dict(target=target,call_address=hex(begin+at+2),argument_home_bytes=16))
    return dict(all_four_call_sequences_pinned=True,retained_frame_bytes=24,
                proof_frame_and_homes_bytes=40,calls=calls)


def persistent_source_literals(blob,profile=None):
    """Reject stale other-kernel KData/API constants in the linked helper."""
    words=set(struct.unpack('<'+str(len(blob)//4)+'I',blob))
    forbidden=(0x8c145890,0x8c145894,0xac145890,0xac145894,0x8c14e6e0,
               0x01dd67c6,0x01dd6314)
    for value in forbidden:
        require(value not in words,'Persistent VC2 helper retains stale Armada source literal: '+hex(value))
    selected=(profile['current_process'],profile['current_thread']) if profile else (0x8c1d5890,0x8c1d5894)
    if profile and profile['name']=='Sega Swirl':
        require(not {0x8c1d5890,0x8c1d5894,0x8c1de6e0}.intersection(words),
                'Swirl helper retains stale VC2 owner literals')
    require(all(value in words for value in selected),
            'Persistent VC2 helper lacks its selected KData source literals')
    return dict(selected_current_process=hex(selected[0]),selected_current_thread=hex(selected[1]),
                stale_armada_kdata_and_api_literals_absent=True,
                checked_aligned_initialized_words=len(blob)//4)


def audit_gd_wait_gate(image,sec):
    symbols=image[4];at,size,kind,_=symbols['_kui_vc2_ce_vmu_gd_wait']
    require(at==GD_WAIT_P2 and size==len(GD_WAIT_TEMPLATE) and kind==2,
            'GD wait gate fixed entry/size differs')
    expected=bytearray(GD_WAIT_TEMPLATE)
    for offset,name in ((0xb4,'_kui_vc2_ce_vmu_gd_wait_admit'),
                        (0xb8,'_kui_vc2_ce_vmu_gd_wait_fault')):
        require(name in symbols and symbols[name][2]==2 and symbols[name][1]>0,
                'GD wait gate callback missing: '+name)
        struct.pack_into('<I',expected,offset,symbols[name][0])
    require(payload_at(image,at,size)==expected,'GD wait gate authored ABI bytes differ')
    start=sec['.gd_wait'][3];end=start+sec['.gd_wait'][5]
    require(start==symbols['__vc2_maple_bridge_blob_end'][0] and start<=at<end<=PERSIST_HANDOFF,
            'GD wait gate/padding span differs')
    require(not any(payload_at(image,start,at-start)), 'GD wait gate padding must be zero')
    probe='__kui_vc2_texture_probe' in symbols
    if probe:
        from check_vc2_texture_probe import audit_entry
        audit_entry(image)
        require(not any(payload_at(image,at+size,0xacfce100-at-size)),
                'Texture probe entry padding must be zero')
    require(end==symbols['__kui_maple_hook_binary_end'][0] and
            (end>0xacfce100 if probe else end==at+size),
            'GD gate is not inside the immutable helper CRC')
    return dict(entry_p2=hex(at),bytes=size,sha256=sha(expected),loan_flag=2,
        native_yield='source-admitted WSEGACD Sleep(1)',poll_bound=1024,
        raw_gd_argument_bytes=16,additional_saved_context_bytes=16,
        original_low_frame_bytes=36,duplicate_low_frame_pushed=False,
        non_request_legacy_passthrough=True,request_token_synthesized=False,
        caller_homes_bytes=16,ordinary_ce_stack_only_for_sleep=True)


def audit_gd_wait_low(image):
    symbols=image[4]
    retry=symbols['__kui_vc2_gd_retry_before_trace'][0]
    busy=symbols['__kui_vc2_gd_legacy_busy'][0]
    require(0x8c004000<=retry<busy<0x8c007ea0,'GD wait low continuations escape resident')
    start=busy-16;words=struct.unpack('<8H',payload_at(image,start,16))
    require(words[:2]==(0xe202,0x3120) and words[2]&0xff00==0x8b00 and
            words[3]&0xff00==0xd200 and words[4]&0xff00==0xd300 and
            words[5]&0xff00==0xd100 and words[6:]==(0x412b,0x0009),
            'GD wait low loan2 dispatch bytes differ')
    displacement=words[2]&255
    if displacement&128:displacement-=256
    require(start+4+4+2*displacement==busy,'GD wait legacy branch target differs')
    for index,wanted in ((3,retry),(4,busy),(5,GD_WAIT_P2)):
        literal=((start+2*index+4)&~3)+4*(words[index]&255)
        require(struct.unpack('<I',payload_at(image,literal,4))[0]==wanted,
                'GD wait low/high continuation literal differs')
    require(payload_at(image,busy,4)==struct.pack('<2H',0x6073,0x8800),
            'GD wait legacy convention changed')
    return dict(retry_p1=hex(retry),legacy_busy_p1=hex(busy),fixed_high_gate=hex(GD_WAIT_P2),
                loan_flag=2,dispatch_bytes_pinned=True)


def helper_stack(directory,write_probe=False,persistent=False):
    reports=sorted(directory.rglob('*.ci'))
    require(reports,'No helper compiler graphs')
    frames,calls=stack.load(reports,qualified=True)
    asm=dict(a2.ASM_FRAMES)
    low_reader=None
    if persistent:
        low_reports=sorted(directory.parent.glob('resident-sci.elf.ltrans*.ci'))
        require(low_reports,'Persistent helper needs the linked low raw-reader graph')
        low_frames,low_calls=stack.load(low_reports)
        low_reader=stack.worst(low_frames,low_calls,
            roots_override={'kui_vc2_ce_vmu_read_card':0})['kui_vc2_ce_vmu_read_card']
        asm['kui_vc2_ce_vmu_read_card']=low_reader['bytes']
    by_base={}
    for key in frames:by_base.setdefault(stack.base(key),[]).append(key)
    def resolve(key):
        if key in asm or key in frames:return key
        matches=by_base.get(stack.base(key),[])
        exact=[n for n in matches if n.rsplit(':',1)[-1]==key]
        if exact:matches=exact
        require(len(matches)==1,'Unbounded or ambiguous helper call: '+key)
        return matches[0]
    def callback_targets(name,label):
        function=stack.base(name);source=label.split(':',1)[0]
        if function=='read_blank' and source=='vmu_readonly.c':
            # The production A2 adapter refuses a non-NULL optional reader.
            # There is no mounted card/backend or callback in this experiment.
            return ()
        if source=='armada_ce_maple_mapping.h':return a2.TABLE_CALLBACKS
        if source in ('armada_ce_cache_word.h','armada_ce_cache_line.h'):
            raise ValueError('Callback-based cache window in cached VC2 helper')
        if source in ('armada_ce_maple_rx_slice.h','armada_ce_maple_traffic.h'):
            return a2.READ_CALLBACKS
        if source=='armada_ce_maple_fence.h':return a2.MMIO_CALLBACKS
        if function=='kui_vmu_emulation_handle' and source=='vmu_emulation.c':
            if write_probe or persistent:return ('ce_write_read','ce_write_commit')
            return a2.BACKEND_CALLBACKS
        if write_probe and source=='armada_ce_vmu_write.c':
            if function=='ce_write_read':return ('probe_read_blank',)
            if function=='ce_write_commit':return ('probe_commit_pending',)
        if function in ('kui_vc2_ce_vmu_install_body','kui_vc2_ce_vmu_install_body_mode',
                        'kui_vc2_ce_vmu_install_body_relocated') and source=='vc2_ce_vmu_prepare.c':
            return ('publish_kernel',)
        if persistent and function=='kui_vc2_ce_vmu_service_own' and source=='vc2_ce_vmu_service.c':
            return ('kui_vc2_ce_vmu_read_card',)
        if persistent:
            guard=('runtime_guard_enter','runtime_guard_leave','runtime_worker_ready')
            pio=('armada_pio_transfer','armada_pio_select_card','armada_pio_ticks')
            sector=('armada_sd_sector_read','armada_sd_sector_write','armada_sd_sector_sync')
            selected={
                ('ce_write_read','armada_ce_vmu_write.c'):('kui_vmu_deferred_io_read',),
                ('ce_write_commit','armada_ce_vmu_write.c'):('kui_vmu_deferred_io_commit',),
                ('request','vmu_deferred_io.c'):guard[:2],
                ('kui_vmu_deferred_io_poison','vmu_deferred_io.c'):guard[:2],
                ('kui_vmu_deferred_io_work','vmu_deferred_io.c'):guard+
                    ('runtime_cached_read','runtime_cached_commit'),
                ('write_sync','vmu_store.c'):sector[1:],
                ('kui_vmu_store_open','vmu_store.c'):sector,
                ('kui_vmu_store_read','vmu_store.c'):sector[:1],
                ('armada_sd_healthy','armada_ce_vmu_sd.c'):guard[2:],
                ('armada_sd_wire','armada_ce_vmu_sd.c'):pio[:1],
                ('armada_sd_wait_byte','armada_ce_vmu_sd.c'):pio[2:],
                ('armada_sd_read_register','armada_ce_vmu_sd.c'):pio[1:2],
                ('armada_sd_execute','armada_ce_vmu_sd.c'):guard+
                    ('kui_vc2_ce_vmu_loan_begin','kui_vc2_ce_vmu_loan_end'),
                ('transfer','sd_reader.c'):pio[:1],('ticks','sd_reader.c'):pio[2:],
                ('release','sd_reader.c'):pio[1:2],('kui_loader_sd_read','sd_reader.c'):pio[1:2],
                ('sci_storage_byte','sci_sd_storage.c'):pio[:1],
                ('sci_storage_ready','sci_sd_storage.c'):pio[2:],
                ('sci_storage_release','sci_sd_storage.c'):pio[1:2],
                ('kui_sci_sd_sync','sci_sd_storage.c'):pio[1:2],
                ('kui_sci_sd_write','sci_sd_storage.c'):pio[1:2],
            }
            if ((function,source)==('read_payload','sd_reader.c') or
                (function,source)==('kui_sci_sd_write','sci_sd_storage.c') and label=='sci_sd_storage.c:83:17'):
                return ()
            if (function,source) in selected:return selected[(function,source)]
        selected=a2.INDIRECT_A2.get((function,source))
        require(selected is not None,'Unknown helper indirect call: '+name+' at '+label)
        return selected
    memo={};next_node={};visiting=set();seen_sites=set();seen=set();closures={}
    def depth(key):
        key=resolve(key)
        if key in asm:closures[key]={key};return asm[key]
        require(key not in visiting,'Recursive helper call: '+key)
        if key in memo:return memo[key]
        visiting.add(key);seen.add(key);children=[];closures[key]={key}
        for target,label in calls.get(key,[]):
            targets=(target,) if target!='__indirect_call' else callback_targets(key,label)
            if target=='__indirect_call':seen_sites.add((key,label,tuple(targets)))
            for item in targets:
                item=resolve(item);children.append((depth(item),item))
                closures[key].update(closures[item])
        visiting.remove(key)
        child=max(children,key=lambda x:x[0],default=(0,None))
        memo[key]=frames[key]+child[0];next_node[key]=child[1];return memo[key]
    output={}
    roots=[('kui_vc2_ce_vmu_prepare_run',40,1536),
           ('kui_armada_ce_maple_traffic_run',40 if persistent else 16,1536)]
    if persistent:
        roots.extend((('kui_armada_ce_vmu_runtime_work',16,4096),
                      ('kui_vc2_ce_vmu_gd_wait_fault',16,1536)))
    image=read_elf(directory/'helper.elf')
    if '__kui_vc2_texture_probe' in image[4]:
        from check_vc2_texture_probe import audit_entry
        audit_entry(image)
        roots.append(('kui_vc2_texture_probe_run',40,4096))
    for root,shim,available in roots:
        key=resolve(root);total=depth(key);path=[];at=key
        while at is not None:
            path.append(dict(function=at,bytes=frames.get(at,asm.get(at))))
            at=next_node.get(at)
        require(total+shim+64<=available,'High private stack budget exceeded: '+root)
        output[root]=dict(c_bytes=total,assembly_bytes=shim,margin=64,
                          available=available,path=path)
        if persistent and root=='kui_armada_ce_maple_traffic_run':
            no_io={'kui_armada_ce_vmu_sd_init','kui_armada_ce_vmu_sd_pio_init',
                'armada_sd_span','armada_sd_private','armada_sd_overlaps','armada_sd_zero'}
            physical={stack.base(n) for n in closures[key] if stack.base(n).startswith(
                ('armada_sd_','armada_pio_','kui_loader_sd_','kui_sci_sd_','kui_armada_ce_vmu_sd_'))
                and stack.base(n) not in no_io}
            require(not physical,'Masked VC2 proof reaches physical storage: '+','.join(sorted(physical)))
    if persistent:
        require(not {'kui_armada_ce_maple_runtime_wait','kui_armada_ce_maple_native_wait'}.
                intersection(stack.base(n) for n in seen),'Native wait is reachable on private VC2 stack')
        total=depth('kui_armada_ce_maple_runtime_wait')
        output['ordinary_ce_wait']=dict(local_bytes=total+16,c_graph_bytes=total,
            shim_argument_home_bytes=16,native_adapter_bytes=24,
            native_argument_home_bytes=16,external_ce_callee_bound_included=False,
            executes_on_original_ce_stack=True)
        output['ordinary_gd_wait_admission']=dict(
            local_bytes=depth('kui_vc2_ce_vmu_gd_wait_admit')+16+32+36,
            original_low_frame_bytes=36,saved_context_bytes=32,argument_home_bytes=16,
            native_sleep_external_depth_included=False,executes_on_original_ce_stack=True)
    return dict(roots=output,compiler_graphs=len(reports),source_qualified=True,
                pre_nk_low_raw_reader=low_reader,
                indirect_sites=[dict(function=n,site=s,targets=list(t))
                                for n,s,t in sorted(seen_sites)])


def audit_helper(directory):
    image=read_elf(directory/'helper.elf');data,h,segments,_,symbols=image
    profile=source_profile(image)
    persistent=persistent_profile(symbols)
    comparison=comparison_profile(image,persistent)
    write_probe=write_probe_profile(symbols)
    sec=sections(image);value=lambda n:symbols[n][0]
    require(h[4]==BEGIN+P2 and value('_kui_vc2_ce_vmu_prepare')==BEGIN+P2,
            'Wrong fixed initializer entry')
    allocated={n:r for n,r in sec.items() if r[2]&2 and r[5]}
    expected_sections={'.uncached','.hook','.vc2_maple_bridge','.workspace',
                       '.stack','.log','.fence','.a2'}
    if persistent:expected_sections|={'.vmu_handoff','.vmu_services','.worker_stack','.gd_wait'}
    require(set(allocated)==expected_sections,'Unexpected helper allocation')
    require(len(segments)==(6 if persistent else 4) and all(p[0]==1 for p in segments),
            'Unexpected helper load segments')
    if persistent:
        mutable=[p for p in segments if p[6]==6]
        expected_mutable={(sec['.workspace'][3],PERSIST_BEGIN,0,sec['.workspace'][5]),
            (PERSIST_HANDOFF,PERSIST_HANDOFF-P2,0,
             sec['.a2'][3]+sec['.a2'][5]-PERSIST_HANDOFF)}
        require(len(mutable)==2 and {(p[2],p[3],p[4],p[5]) for p in mutable}==expected_mutable,
                'Persistent lower BSS and high records need distinct zero-file load ranges')
    initial=[]
    for name,row in allocated.items():
        _,typ,flags,address,offset,size,*_=row
        matching=[p for p in segments if p[0]==1 and
                  p[2]<=address and address+size<=p[2]+p[5] and
                  p[6]==(6 if flags&1 else 5)]
        require(len(matching)==1,'Section has no unique runtime segment: '+name)
        p=matching[0];physical=p[3]+address-p[2]
        require((PERSIST_BEGIN if persistent else BEGIN)<=physical and physical+size<=END,
                'Helper physical range: '+name)
        if flags&1:
            require(typ==8 and (PERSIST_BEGIN if persistent else BEGIN)+P2<=address<END+P2,
                    'Mutable helper object is not zeroed P2: '+name)
        else:
            require(typ==1,'Initialized helper section changed kind')
            content=region(data,offset,size,name)
            require(payload_at(image,address,size)==content,'Section/segment bytes differ')
            initial.append((physical,content,name))
    initial.sort()
    require(initial[0][0]==BEGIN,'Missing helper first bytes')
    cursor=BEGIN;blob=bytearray()
    for address,content,name in initial:
        require(address==cursor,'Noncontiguous helper physical bytes: '+name)
        blob.extend(content);cursor+=len(content)
    require(bytes(blob)==(directory/'helper.bin').read_bytes(),'Helper binary/ELF mismatch')
    prepared=bytes(blob)+bytes(END-cursor)
    require(prepared==(directory/'prepared.bin').read_bytes(),'Prepared zero tail differs')
    header=(directory/'vc2_ce_vmu_symbols.h').read_text()
    numbers={k:int(v,16) for k,v in re.findall(r'#define (KUI_VC2_VMU_\w+) (0x[0-9a-f]+)',header)}
    require(numbers['KUI_VC2_VMU_BLOB_BYTES']==len(blob),'Generated helper size differs')
    require(numbers['KUI_VC2_VMU_BLOB_CRC32']==zlib.crc32(blob),'Immutable helper CRC differs')
    require(numbers['KUI_VC2_VMU_PREPARED_CRC32']==zlib.crc32(prepared),'Prepared CRC differs')
    require(sha(blob) in header,'Generated helper SHA differs')
    require(value('__kui_maple_hook_binary_end')==BEGIN+P2+len(blob) and len(blob)%4==0,
            'Initializer clear start differs from verified immutable end')
    require(value('_kui_vc2_ce_vmu_prepare_zeroed')==BEGIN+P2+14,
            'Initializer tail reset must precede the saved stack frame')
    # Actual P2 entry: MOV.L start,R0; MOV.L end,R1; MOV #0,R2;
    # MOV.L R2,@R0; ADD #4,R0; CMP/HS R1,R0; BF back to the store.
    entry=payload_at(image,BEGIN+P2,14)
    words=struct.unpack('<7H',entry)
    require(words[0]&0xff00==0xd000 and words[1]&0xff00==0xd100 and
            words[2:]==(0xe200,0x2022,0x7004,0x3012,0x8bfb),
            'Initializer must reset its P2 tail before any stack save')
    for i,expected in ((0,BEGIN+P2+len(blob)),(1,END+P2)):
        literal=((BEGIN+P2+2*i+4)&~3)+4*(words[i]&255)
        require(struct.unpack('<I',payload_at(image,literal,4))[0]==expected,
                'Initializer tail reset literal differs')
    bridge_rows=[row for row in initial if row[2]=='.vc2_maple_bridge']
    require(len(bridge_rows)==1 and value('__vc2_maple_bridge_blob_start')==bridge_rows[0][0]+P2,
            'Bridge source LMA changed')
    bridge=bridge_rows[0][1]
    require(len(bridge)==100 and sec['.vc2_maple_bridge'][3]==profile['bridge_begin'],
            'Mapped bridge layout/packing differs')
    if (directory/'bridge.bin').exists():
        require(bridge==(directory/'bridge.bin').read_bytes(),'Mapped bridge layout/packing differs')
    literal=value('__vc2_maple_bridge_callback_blob')-value('__vc2_maple_bridge_blob_start')
    require(literal==0x54 and struct.unpack_from('<I',bridge,literal)[0]==
            value('_kui_armada_ce_maple_hook'),'Bridge callback literal differs')
    bridge_proof=bridge_contract(bridge,value('_kui_armada_ce_maple_hook'),profile)
    require(BEGIN+P2<=value('_publish_kernel')<value('__kui_maple_uncached_end'),
            'CCR publication does not execute through P2')
    if persistent:
        native_wait=symbols['_kui_armada_ce_maple_native_wait']
        require(native_wait[1]==len(a2.RUNTIME_NATIVE_WAIT_BYTES) and
                payload_at(image,native_wait[0],native_wait[1])==a2.RUNTIME_NATIVE_WAIT_BYTES,
                'Persistent native wait ABI adapter bytes differ')
        require(value('__kui_maple_workspace_start')==PERSIST_BEGIN+P2 and
                value('__kui_maple_workspace_start')<value('__kui_maple_workspace_end')<=BEGIN+P2,
                'Persistent VC2 workspace escapes lower reservation')
        require(cursor+P2<=PERSIST_HANDOFF,'Persistent helper code overlaps handoff')
        expected_layout={'.vmu_handoff':(PERSIST_HANDOFF,1272),
            '.vmu_services':(PERSIST_SERVICES,32),
            '.worker_stack':(PERSIST_WORKER[0],PERSIST_WORKER[1]-PERSIST_WORKER[0])}
        for name,(address,size) in expected_layout.items():
            require(sec[name][3]==address and sec[name][5]==size,
                    'Persistent retained layout differs: '+name)
        require(value('__kui_vmu_worker_stack_bottom')==PERSIST_WORKER[0] and
                value('__kui_vmu_worker_stack_top')==PERSIST_WORKER[1],
                'Persistent worker stack symbols differ')
        workspace_words=struct.unpack('<6H',payload_at(image,BEGIN+P2+14,12))
        require(workspace_words[0]&0xff00==0xd000 and workspace_words[1]&0xff00==0xd100 and
                workspace_words[2:]==(0x2022,0x7004,0x3012,0x8bfb),
                'Persistent workspace clear must precede any stack save')
        for i,symbol in ((0,'__kui_maple_workspace_start'),(1,'__kui_maple_workspace_end')):
            literal=((BEGIN+P2+14+2*i+4)&~3)+4*(workspace_words[i]&255)
            require(struct.unpack('<I',payload_at(image,literal,4))[0]==value(symbol),
                    'Persistent workspace clear literal differs')
    else:
        require(value('__kui_maple_workspace_start')>=cursor+P2 and
                value('__kui_maple_workspace_end')<=0xacfcf800,'Helper workspace overlaps stack')
    require(value('__kui_maple_stack_bottom')==0xacfcf800 and
            value('__kui_maple_stack_top')==0xacfcfe00,'Callback stack differs')
    for name,(_,typ,flags,address,_,size,*_) in allocated.items():
        if name in ('.uncached','.hook','.vc2_maple_bridge','.gd_wait'):continue
        require(not flags&4 and typ==8,'Writable/executable helper allocation: '+name)
    dis=subprocess.check_output([objdump_tool(),'-d',str(directory/'helper.elf')],text=True)
    # The cached section also contains read-only strings/tables. Audit typed
    # function spans plus the two authored assembly islands; decoding those
    # data tables as instructions would manufacture FPU operations.
    code_spans=[(v,v+n) for v,n,kind,owner in symbols.values()
                if kind==2 and n and owner<len(image[3]) and image[3][owner][2]&4]
    code_spans.extend(((value('_kui_armada_ce_maple_hook'),
                        value('__kui_maple_traffic_shim_end')),
                       (profile['bridge_begin'],profile['bridge_end'])))
    filtered=[]
    for line in dis.splitlines():
        address=re.match(r'^\s*([0-9a-f]+):\s',line)
        if address and not any(a<=int(address[1],16)<b for a,b in code_spans):continue
        filtered.append(line)
    count=instructions('helper','\n'.join(filtered))
    result=dict(bytes=len(blob),prepared_bytes=len(prepared),sha256=sha(blob),comparison=comparison,
                source_profile=profile,
                mapped_bridge=bridge_proof,
                prepared_crc32=f'{zlib.crc32(prepared):08x}',
                immutable_crc32=f'{zlib.crc32(blob):08x}',
                entry_instruction_word=f'{struct.unpack_from("<I",blob)[0]:08x}',
                working_tail_zeroed_before_stack=True,
                initialized_end=hex(cursor),workspace_end=hex(value('__kui_maple_workspace_end')),
                stack=helper_stack(directory,write_probe,persistent),integer_instructions=count)
    if '__kui_vc2_texture_probe' in symbols:
        from check_vc2_texture_probe import audit_entry
        require(comparison['mode']==4,'Payload evidence must leave the Maple hook absent')
        result['texture_probe']=audit_entry(image)
    if persistent:
        result['persistent_runtime']=dict(
            marker=PERSIST_MARKER,reserved_begin=hex(profile['protected_begin']),
            reserved_bytes=END-profile['protected_begin'],
            relocated_maple_text_bytes=profile['relocated_text_bytes'],
            prepared_image_bytes=len(prepared),workspace_zeroed_before_stack=True,
            handoff_p2=hex(PERSIST_HANDOFF),handoff_bytes=1272,
            services_p2=hex(PERSIST_SERVICES),services_bytes=32,
            worker_stack_p2=[hex(x) for x in PERSIST_WORKER],worker_stack_bytes=4096,
            caller_homes=persistent_call_homes(image),
            comparison_noop=audit_comparison_noop(image),
            source_literals=persistent_source_literals(blob,profile),
            cache_snapshot=persistent_snapshot_contract(image,profile),
            gd_request_wait=audit_gd_wait_gate(image,sec),
            vmu_read_cache=dict(bytes=4096,blocks=8,provider_callbacks=
                ['runtime_cached_read','runtime_cached_commit'],
                source_scoped_to_vc2_persistence=True),
            native_wait_on_original_ce_stack=True,
            durable_commit_ack_enabled=comparison['runtime_storage_enabled'],
            storage_services_linked=True)
    if write_probe:
        result['write_probe']=dict(snapshot_words=20,staging_bytes=512,
            write_phase_ack='receipt only, phases0..3',
            durable_commit_ack_enabled=False,storage_services_linked=False,
            expected_stop='complete matching phase4 before commit acknowledgement')
    return result


def audit(build,helper_only=False):
    helper=build if helper_only else build/'helper'
    out=dict(schema='kui.vc2.ce-vmu.linked-package.v1',status='PASS',
             helper=audit_helper(helper),hardware_verified=False,persistent_writes=False)
    persistent=bool(out['helper'].get('persistent_runtime'))
    out['persistent_writes']=out['helper']['comparison']['runtime_storage_enabled']
    if helper_only:return out
    resident=read_elf(build/'resident-sci.elf');stage=read_elf(build/'stage.elf')
    require(source_profile(resident)==source_profile(stage)==out['helper']['source_profile'],
            'Resident, stage and helper source profiles differ')
    rs=resident[4];ss=stage[4]
    late=rs.get('__kui_vc2_vmu_late_resident',(0,))[0]==1
    late_loader=None
    if late:
        low_checked,late_loader=audit_late_resident(
            build,resident,stage,out['helper']['prepared_crc32'],persistent)
    for name,base,limit in (('resident-sci',0x8c004000,0x8c007ea0),
                            ('stage',0x8ce00000,0x8cfc4000)):
        checked=(low_checked if late and name=='resident-sci' else
                 inspect_elf((build/(name+'.elf')).read_bytes(),base,limit))
        require(checked['payload']==(build/(name+'.bin')).read_bytes(),name+' binary/ELF differs')
    require(rs['_manifest'][1]==828,'VC2 resident must retain48 map slots')
    require(rs['__native_dma_region_bytes'][0]==1600,'SCI region size differs')
    require(rs['__retail_hook_stack_bottom'][0]==0x8c007ea0 and
            rs['__retail_hook_stack'][0]==0x8c008000,'Low352-byte guarded stack differs')
    cold=cold_tables(build/'resident-sci.elf')
    cap=payload_at(stage,ss['_kui_native_foreground_reader_capability'][0],32)
    flags=3 if persistent else 2 if late else 1
    require(struct.unpack('<8I',cap)==(0x3146474b,0x574f4c46,1,32,0,48,0,flags),
            'VC2 experimental capability differs')
    if persistent:
        require(late,'Persistent VC2 must use the admitted late capsule route')
        begin=ss['__vc2_persist_profile_blob_start'][0]
        finish=ss['__vc2_persist_profile_blob_end'][0]
        expected_profile=template_profile()
        require(begin%4==0 and finish-begin==4096 and
                payload_at(stage,begin,4096)==expected_profile,
                'Persistent VC2 must carry the canonical launch-local UI profile template')
        require((build/'vmu-sd-profile.bin').read_bytes()==expected_profile,
                'Persistent build profile differs from embedded UI template')
        out['launch_profile']=dict(bytes=4096,sha256=sha(expected_profile),
            canonical_ui_template=True,physical_identity_bound_at_launch=True,
            dedicated_product='KUIVC2TEST',existing_game_cards_imported=False)
        out['gd_request_wait_low']=audit_gd_wait_low(resident)
    if not late or '__vc2_vmu_blob_start' in ss or '__vc2_vmu_blob_end' in ss:
        begin=ss['__vc2_vmu_blob_start'][0];end=ss['__vc2_vmu_blob_end'][0]
        require(payload_at(stage,begin,end-begin)==(helper/'helper.bin').read_bytes(),
                'Stage embeds another helper')
    reports=sorted(build.glob('resident-sci.elf.ltrans*.ci'))
    frames,calls=stack.load(reports)
    # The source-bound P2 initializer switches to the high callback stack
    # without writing the low stack. The caller's own C frame is still counted.
    old=stack.INDIRECT.copy()
    try:
        if out['helper'].get('texture_probe'):
            require(rs['_kui_vc2_texture_probe_entry'][0]==0xacfce100,
                    'Low payload probe fixed entry differs')
            # The separately byte-audited P2 shim makes no low-stack save.
            # Its complete C closure is counted on the private4KiB stack above.
            frames['kui_vc2_texture_probe_entry']=0
            calls['kui_vc2_texture_probe_entry']=[]
        stack.INDIRECT['vc2_ce_vmu_gate.inc']=()
        if late:
            # This header has one fixed callback: the separately linked P2
            # card reader. Its entire low SCI callgraph still counts here.
            stack.INDIRECT['vc2_ce_vmu_late.h']=('kui_vc2_ce_vmu_read_card',)
        low=stack.worst(frames,calls,roots_override={
            'kui_retail_resident_dispatch':0,'kui_retail_menu_return':0})
    finally:stack.INDIRECT.clear();stack.INDIRECT.update(old)
    require(max(row['bytes'] for row in low.values())+64<=336,'Low private stack exceeded')
    module=(build/'retail-native-foreground.kui').read_bytes();metadata=verify(module)
    payload,memory=flatten_elf((build/'entry.elf').read_bytes())
    require(module[64:]==payload and metadata['memory_bytes']==memory,'Envelope/entry mismatch')
    provenance=json.loads((build/'entry.compile.json').read_text())
    require(provenance['elf_sha256']==sha((build/'entry.elf').read_bytes()),'ELF provenance differs')
    out.update(low_stack=low,low_bss_end=hex(rs['__retail_resident_bss_end'][0]),
               cold_tables=cold,
               capability=dict(slots=48,flags=flags,virtual_card=(
                   ('shared Smash Pack A1 card' if out['helper']['source_profile']['name']=='Sega Swirl'
                    else 'dedicated persistent KUIVC2TEST A2 card') if persistent else
                   'blank A2 with volatile write-phase staging' if out['helper'].get('write_probe')
                   else 'blank readonly A2')),
               runtime=metadata,compile_provenance=provenance,
               module_sha256=sha(module))
    if late:
        out['late_loader']=late_loader
        out['integer_instructions']={name:instructions(name,subprocess.check_output(
            [objdump_tool(),'-d',str(build/(name+'.elf'))],text=True))
            for name in ('resident-sci','stage')}
    return out


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument('build',type=Path,nargs='?',default=ROOT/'build/vc2-ce-vmu')
    ap.add_argument('--helper-only',action='store_true');ap.add_argument('--output',type=Path)
    args=ap.parse_args()
    try:result=audit(args.build,args.helper_only)
    except (OSError,ValueError,KeyError,struct.error,subprocess.CalledProcessError) as error:
        ap.exit(1,'VC2 package audit failed: '+str(error)+'\n')
    output=json.dumps(result,indent=2)+'\n'
    if args.output:
        destination=args.output.resolve();build=args.build.resolve()
        if destination.is_relative_to(ROOT) and not destination.is_relative_to(ROOT/'build'):
            ap.exit(1,'Report may not overwrite project source\n')
        inputs=[p for p in build.rglob('*') if p.is_file() and
                (p.suffix in ('.elf','.bin','.kui','.ci','.su','.o','.h') or
                 p.name.endswith('.compile.json'))]
        if any(destination==p.resolve() or
               (destination.exists() and destination.samefile(p)) for p in inputs):
            ap.exit(1,'Report aliases a proof input\n')
        args.output.write_text(output)
    print(output,end='')


if __name__=='__main__':main()
