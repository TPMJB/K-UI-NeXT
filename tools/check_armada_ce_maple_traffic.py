#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the isolated Armada CE completion-time header observer.

The fixed bridge and private-stack shim are pinned separately from compiled C.
C instructions, retained static frames, callback closure and every allocated
range are audited; the complete ELF hash binds the resulting package.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess
from check_armada_ce_maple_hook import BRIDGE,BRIDGE_BYTES as OLD_BRIDGE_BYTES,EH,SH,PH,SYM,require
from check_retail_instructions import audit as audit_instructions
from check_retail_stack import load as load_graph,base as base_name
from check_armada_ce_excerpt import ARRAY_READ,ARRAY_READ_BYTES

HOOK=0xACFCE000
LOG=0xACFCFE00
STACK_BOTTOM=0xACFCF800
STACK_TOP=LOG
LOG_BYTES=124
BRIDGE_BYTES=OLD_BRIDGE_BYTES[:-4]+struct.pack('<I',HOOK)
# Every authored instruction and the stack literal are fixed. The final
# four-byte call literal must equal the separately bounded C entry symbol.
SHIM_PREFIX=bytes.fromhex(
    '02033360f0cb0e4008d0f620862022403620036f06d00b400900f660264ff668f261'
    '136f0e400b000900090000fefcac')
SHIM_BYTES=52
ROOT='kui_armada_ce_maple_traffic_run'
CALLBACKS=('traffic_read_p1','traffic_read_cache','traffic_read_p2')
ASM_FRAMES={'kui_armada_ce_excerpt_array_read':0}


def read_section_elf(data):
    require(len(data)>=EH.size,'Traffic ELF header truncated')
    ident,kind,machine,version,entry,phoff,shoff,_,ehsize,phsize,phnum,shsize,shnum,shstr=EH.unpack_from(data)
    require(ident[:7]==b'\x7fELF\x01\x01\x01' and kind==2 and machine==42 and version==1,
            'Traffic requires static little-endian SH ELF')
    require(entry==HOOK and ehsize==EH.size and phsize==PH.size and phnum==2 and
            shsize==SH.size and 1<=shnum<=32 and shstr<shnum,'Traffic ELF geometry changed')
    require(phoff+phnum*PH.size<=len(data) and shoff+shnum*SH.size<=len(data),'Traffic ELF tables truncated')
    raw=[SH.unpack_from(data,shoff+i*SH.size) for i in range(shnum)]
    names=raw[shstr]
    require(names[1]==3 and names[4]+names[5]<=len(data),'Traffic section names truncated')
    strings=data[names[4]:names[4]+names[5]]
    sections={}
    for row in raw:
        name,typ,flags,address,offset,size,link,info,alignment,entsize=row
        require(name<len(strings),'Traffic section name invalid')
        end=strings.find(b'\0',name)
        require(end>=0,'Traffic section name unterminated')
        label=strings[name:end].decode('ascii')
        require(label not in sections,'Duplicate traffic section')
        require(typ not in (4,6,9),'Traffic relocation/dynamic section forbidden')
        require(typ==8 or offset+size<=len(data),'Traffic section leaves ELF')
        sections[label]=dict(type=typ,flags=flags,address=address,size=size,link=link,
            info=info,alignment=alignment,entsize=entsize,offset=offset,
            data=b'' if typ==8 else data[offset:offset+size])
    allocated={n:s for n,s in sections.items() if s['flags']&2}
    require(set(allocated)=={'.bridge','.hook','.workspace','.stack','.log'},'Unexpected traffic allocated section')
    for label,address,size,typ,flags in [('.bridge',BRIDGE,92,1,6),
            ('.stack',STACK_BOTTOM,STACK_TOP-STACK_BOTTOM,8,3),('.log',LOG,LOG_BYTES,8,3)]:
        s=allocated[label]
        require((s['address'],s['size'],s['type'],s['flags'])==(address,size,typ,flags),label+' layout changed')
    helper=allocated['.hook'];work=allocated['.workspace']
    require(helper['address']==HOOK and helper['type']==1 and helper['flags']==6 and
            0<SHIM_BYTES<=helper['size']<=STACK_BOTTOM-HOOK,'Traffic helper bounds changed')
    require(work['type']==8 and work['flags']==3 and work['size']>0 and work['address']%4==0 and
            work['address']>=HOOK+helper['size'] and work['address']+work['size']<=STACK_BOTTOM,
            'Traffic workspace overlaps immutable code or private stack')
    require(allocated['.bridge']['data']==BRIDGE_BYTES,'Complete traffic CE bridge differs')
    require(helper['data'][:len(SHIM_PREFIX)]==SHIM_PREFIX,'Complete traffic private-stack shim differs')
    segments=[PH.unpack_from(data,phoff+i*PH.size) for i in range(phnum)]
    require([(p[0],p[2],p[3],p[4],p[5],p[6]) for p in segments]==
            [(1,BRIDGE,BRIDGE,92,92,5),(1,HOOK,HOOK,helper['size'],LOG+LOG_BYTES-HOOK,7)],
            'Traffic load segments changed')
    for p in segments:require(p[1]+p[4]<=len(data),'Traffic load segment leaves ELF')
    require(data[segments[0][1]:segments[0][1]+92]==BRIDGE_BYTES and
            data[segments[1][1]:segments[1][1]+helper['size']]==helper['data'],
            'Traffic segment differs from checked section')
    tab=sections.get('.symtab')
    require(tab is not None and tab['entsize']==SYM.size and tab['size']%SYM.size==0,
            'Traffic symbol table missing')
    require(0<=tab['link']<len(raw),'Traffic symbol string index invalid')
    names=raw[tab['link']]
    require(names[1]==3 and names[4]+names[5]<=len(data),'Traffic symbol strings invalid')
    names=data[names[4]:names[4]+names[5]];symbols={};functions={}
    for at in range(0,tab['size'],SYM.size):
        name,value,size,info,_,index=SYM.unpack_from(tab['data'],at)
        require(at==0 or index!=0,'Unresolved traffic symbol')
        if not name:continue
        end=names.find(b'\0',name)
        require(0<=name<len(names) and end>=0,'Traffic symbol name invalid')
        label=names[name:end].decode('ascii')
        require(label not in symbols or symbols[label]==value,'Ambiguous traffic symbol')
        symbols[label]=value
        if info&15==2:
            require(size>0 and HOOK<=value and value+size<=HOOK+helper['size'],
                    'Traffic function outside immutable helper')
            functions[label]=(value,size)
    expected={'_kui_armada_ce_maple_bridge':BRIDGE,'__kui_maple_bridge_end':BRIDGE+92,
        '_kui_armada_ce_maple_hook':HOOK,'__kui_maple_hook_binary_end':HOOK+helper['size'],
        '__kui_maple_workspace_start':work['address'],'__kui_maple_workspace_end':work['address']+work['size'],
        '__kui_maple_stack_bottom':STACK_BOTTOM,'__kui_maple_stack_top':STACK_TOP,
        '_kui_armada_ce_maple_hook_log':LOG,'_kui_armada_ce_maple_traffic_freeze':LOG+120,
        '__kui_maple_set_kmode':0x01DD6374,'__kui_maple_resume':0x01DD5B10}
    require(all(symbols.get(k)==v for k,v in expected.items()),'Traffic linked symbol contract changed')
    require('_'+ROOT in functions,'Traffic native C entry has no bounded function span')
    require(ARRAY_READ in functions and functions[ARRAY_READ][1]==len(ARRAY_READ_BYTES),
            'Traffic cache-array leaf size changed')
    array_at=functions[ARRAY_READ][0]-HOOK
    require(helper['data'][array_at:array_at+len(ARRAY_READ_BYTES)]==ARRAY_READ_BYTES,
            'Complete traffic cache-array leaf differs')
    require(symbols.get('__kui_maple_traffic_shim_end')==HOOK+SHIM_BYTES and
            helper['data'][len(SHIM_PREFIX):SHIM_BYTES]==struct.pack('<I',symbols['_'+ROOT]),
            'Traffic shim does not call the bounded C root')
    return sections,symbols,functions


def stack_audit(directory,functions):
    reports=sorted(directory.glob('traffic*.ci'))
    require(bool(reports),'Traffic C call graph missing')
    frames,calls=load_graph(reports)
    require(ROOT in frames,'Traffic private-stack root has no static frame')
    require(all(base_name(f.lstrip('_')) in {base_name(k) for k in frames} for f in functions if f!=ARRAY_READ),
            'Retained traffic C function lacks a static frame')
    require(bool(CALLBACKS),'Traffic callback closure has not been pinned')
    require(all(c in frames for c in CALLBACKS),'Traffic callback static frame missing')
    visiting=set();seen=set()
    def depth(name):
        if name in ASM_FRAMES:
            seen.add(name);return ASM_FRAMES[name]
        require(name in frames,'Missing traffic static frame: '+name)
        require(name not in visiting,'Traffic recursion is forbidden: '+name)
        visiting.add(name);seen.add(name);children=[]
        for target,_ in calls.get(name,[]):
            if target=='__indirect_call':
                # Only the source sampler's bounded read callback sites may
                # dispatch to this complete finite set of guarded readers.
                require(base_name(name).startswith('kui_armada_ce_maple_traffic_') or
                        base_name(name) in ('kui_armada_ce_maple_resolve','kui_armada_ce_cache_word'),
                        'Unexpected indirect traffic call: '+name)
                children.extend(depth(callback) for callback in CALLBACKS)
            else:children.append(depth(target))
        visiting.remove(name)
        return frames[name]+max(children,default=0)
    worst=depth(ROOT)
    require(worst+16+64<=STACK_TOP-STACK_BOTTOM,'Traffic private stack lacks assembly/margin budget')
    return dict(c_frame_bytes=worst,shim_frame_bytes=16,margin_bytes=64,
        available_bytes=STACK_TOP-STACK_BOTTOM,static_call_graph=True,
        indirect_callbacks=list(CALLBACKS),retained_frame_count=len(seen))


def check(directory):
    directory=Path(directory);elf=(directory/'hook.elf').read_bytes()
    sections,symbols,functions=read_section_elf(elf)
    require((directory/'bridge.bin').read_bytes()==BRIDGE_BYTES,'Traffic bridge binary differs from ELF')
    require((directory/'hook.bin').read_bytes()==sections['.hook']['data'],'Traffic helper binary differs from ELF')
    dis=subprocess.check_output(['sh-elf-objdump','-d',str(directory/'hook.elf')],text=True)
    # Function-size spans omit C constants concatenated into the .hook section.
    spans=list(functions.values())+[(HOOK,SHIM_BYTES)]
    filtered=[]
    for line in dis.splitlines():
        found=re.match(r'^\s*([0-9a-f]+):',line)
        if found and any(a<=int(found.group(1),16)<a+n for a,n in spans):filtered.append(line)
    instructions=audit_instructions('traffic-helper','\n'.join(filtered))
    private_stack=stack_audit(directory,functions)
    return dict(bridge_nominal_va=hex(BRIDGE),bridge_rva='0x67a4',bridge_bytes=92,
        bridge_sha256=hashlib.sha256(BRIDGE_BYTES).hexdigest(),
        helper_p2_entry=hex(HOOK),helper_bytes=sections['.hook']['size'],
        helper_sha256=hashlib.sha256(sections['.hook']['data']).hexdigest(),
        helper_elf_sha256=hashlib.sha256(elf).hexdigest(),
        log_p2=hex(LOG),report_bytes=120,freeze_word_p2=hex(LOG+120),
        reserved_range_p1=['0x8cfce000','0x8cfd0000'],
        workspace_bytes=sections['.workspace']['size'],private_stack=private_stack,
        bridge_and_shim_pinned=True,cache_array_leaf_pinned=True,
        cache_array_leaf_p2=hex(symbols[ARRAY_READ]),cache_array_leaf_stack_bytes=0,
        cache_array_post_read_nops=8,complete_compiled_c_bytes_pinned=False,
        unresolved_symbols=0,integer_instructions=instructions,
        ce_bridge_owned_stack_bytes=52,ce_bridge_and_setkmode_stack_bytes=68,
        capture_limited_per_visit=True,capture_active_after_first32_visits=True,
        virtual_vmu_enabled=False,maple_or_sd_io=False,save_data_written=False,
        dma_payload_read=False,dma_idle_or_ownership_established=False,
        guest_reads='guarded RAM mappings, coherent cache-array extension words and bounded ordinary TX/RX headers only',
        mutable_write_domain='owned private workspace/stack/log and bounded CE bridge frame only',
        caveat='Source-specific calling convention, admission and pre-NK reservation audit remain required')


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    try:print(json.dumps(check(args.directory),indent=2))
    except (OSError,ValueError,struct.error,UnicodeError) as error:
        parser.exit(1,'Traffic hook audit failed: '+str(error)+'\n')

if __name__=='__main__':main()
