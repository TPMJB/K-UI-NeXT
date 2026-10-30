#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Audit the separate physical-only Standard loader and its plain residents.

The ordinary instruction, placement and resident-stack gates remain mandatory.
The descriptor is a route capability, not a game compatibility claim.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import subprocess

from check_retail_instructions import IMAGES, audit as instruction_audit
from check_retail_loader_layout import check_directory
from check_vmu_native_worker_gateway import read_elf, symbol_bytes
from package_vmu_native_persistent import closed_callgraph
from retail_package import STAGE_ADDRESS, STAGE_BLOB_OFFSET, inspect_retail, inspect_ram_patch_seed, RAM_PATCH_SEED
from runtime_package import flatten_elf, verify

ROOT=Path(__file__).resolve().parents[1]
SCHEMA='kui.native.physical-standard.audit.v1'
CAPABILITY=struct.pack('<8I',0x5049554b,0x31594850,1,32,0,0,0,0)
MARKER='__kui_native_physical_stage'
SYMBOL='_kui_native_physical_capability'
RESIDENTS=('scif','sci','ide','scia')
GUARD=struct.pack('<12H',0x6043,0x2008,0x8b05,0xe003,0x3502,0x8902,0xe001,0x000b,0x0009,0xe000,0x000b,0x0009)
ARTIFACTS=('retail-physical.kui','entry.elf','entry.map','entry.compile.json',
           'stage.elf','stage.bin','stage.map','stage.compile.json','plain-layout-config')+tuple(
    'resident-'+name+suffix for name in RESIDENTS for suffix in ('.elf','.bin','.map','.compile.json'))


def require(value,message):
    if not value:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def typed_descriptor(stage,module):
    require(stage[4].get(MARKER)==(1,0,0,0xfff1),'Missing exact physical Standard source marker')
    address,body,section=symbol_bytes(stage,SYMBOL,1)
    require(address%4==0 and section[2]&7==2 and body==CAPABILITY,
            'Physical Standard capability differs from its allocated read-only typed object')
    offset=64+STAGE_BLOB_OFFSET+address-STAGE_ADDRESS
    require(module[offset:offset+32]==body and module.count(CAPABILITY)==1,
            'Physical capability is absent, duplicated or outside the linked high-stage owner')
    return dict(module_offset=offset,bytes=32,allocated_read_only=True,unique_complete_descriptor=True)


def no_virtual_runtime(images,module):
    for name,image in images.items():
        for symbol in image[4]:
            own=symbol==MARKER or symbol.startswith('_kui_native_physical_')
            require(own or not symbol.startswith(('_kui_vmu_','__kui_vmu_','__vmu_',
                    '_kui_native_','__kui_native_','_native_','__native_')),
                    'Physical loader contains a VMU/native-worker symbol: '+name+':'+symbol)
        require(not any(word in symbol.lower() for symbol in image[4]
                        for word in ('vmu_sd_profile','native_worker','native_ra_','runtime_mode','resolution_cache','game_candidates')),
                'Physical loader retains virtual-card/cache ownership symbols: '+name)
    for magic in (b'KUIVMUP2',b'KUIVMU04',b'KUINVM01',b'KUINVS01',b'KUINVR01'):
        require(magic not in module,'Physical loader contains a VMU/cache/patch template: '+repr(magic))


def execute_guard(body,reader,transport):
    require(body==GUARD,'Physical Standard admission guard differs from its reviewed SH instructions')
    r0=0;flag=False;pc=0
    for _ in range(16):
        op=struct.unpack_from('<H',body,pc)[0];next_pc=pc+2
        if op==0x6043:r0=reader&0xffffffff
        elif op==0x2008:flag=r0==0
        elif op&0xff00 in (0x8b00,0x8900):
            take=(not flag) if op&0xff00==0x8b00 else flag
            if take:
                displacement=op&255
                if displacement&128:displacement-=256
                next_pc=pc+4+2*displacement
        elif op&0xff00==0xe000:r0=op&255
        elif op==0x3502:flag=(transport&0xffffffff)>=r0
        elif op==0x000b:
            require(struct.unpack_from('<H',body,pc+2)[0]==9,'Physical guard has an unsafe return slot')
            return r0
        else:raise ValueError('Unexpected physical Standard guard instruction')
        pc=next_pc
    raise ValueError('Physical Standard admission guard did not return')


def guard_witness(stage):
    from check_loader_layout import inspect_elf,region
    address,body,section=symbol_bytes(stage,'_kui_native_physical_stage_admit',2)
    require(section[2]&7==6 and body==GUARD,'Physical Standard admission guard is not its exact immutable code')
    main,caller,_=symbol_bytes(stage,'_kui_retail_stage_main',2)
    payload=inspect_elf(stage[0],STAGE_ADDRESS,0x8cfe0000)['payload']
    calls=[]
    for offset in range(0,len(caller)-1,2):
        op=struct.unpack_from('<H',caller,offset)[0]
        if op&0xf000!=0xd000:continue
        register=op>>8&15;literal=((main+offset+4)&~3)+4*(op&255)
        if not STAGE_ADDRESS<=literal<=STAGE_ADDRESS+len(payload)-4:continue
        if struct.unpack_from('<I',payload,literal-STAGE_ADDRESS)[0]!=address:continue
        for at in range(offset+2,min(offset+66,len(caller)-1),2):
            instruction=struct.unpack_from('<H',caller,at)[0]
            if instruction==0x400b|(register<<8):
                after=[struct.unpack_from('<H',caller,n)[0] for n in range(at+4,min(at+14,len(caller)-1),2)]
                require(len(after)>=2 and after[0]==0x2008 and after[1]&0xff00 in (0x8900,0x8b00,0x8d00,0x8f00),
                        'Physical stage does not condition its early admission on the guard result')
                calls.append(main+at);break
            # Stop if the resolved target register is overwritten before JSR.
            top=instruction>>12;n=instruction>>8&15
            if n==register and top in (4,6,7,9,13,14):break
    require(len(calls)==1,'Physical stage must call its exact admission guard once')
    cases=0
    for reader in (*range(8),0xffffffff):
        for transport in (*range(8),0xffffffff):
            require(execute_guard(body,reader,transport)==int(reader==0 and transport<3),
                    'Physical guard accepts a different reader/transport policy')
            cases+=1
    return dict(exact_immutable_body=True,body_bytes=len(body),body_sha256=sha(body),
        actual_stage_call_bound=True,actual_stage_result_checked=True,
        instruction_cases=cases,reader=0,transport_mask=7)


def source_provenance(directory,images,allow_dirty):
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    dirty=bool(subprocess.check_output(['git','status','--porcelain','--untracked-files=all'],cwd=ROOT))
    require(allow_dirty or not dirty,'Final physical Standard audit requires clean committed source')
    for name,image in images.items():
        record=json.loads((directory/(name+'.compile.json')).read_bytes())
        require(record==dict(commit=commit,source_dirty=dirty,elf_sha256=sha(image[0])),
                'Physical image lacks exact current compile provenance: '+name)
    config=(directory/'plain-layout-config').read_text().split()
    fields=dict(item.split('=',1) for item in config)
    require(len(fields)==len(config) and fields.get('BUILD')==commit[:12] and
        fields.get('LOW') in ('0','1') and fields.get('TRACE')=='0' and fields.get('SONIC')=='0' and
        fields.get('RETURN_TRACE','0')=='0' and fields.get('SKIES_READ_DIAGNOSTIC','0')=='0',
        'Physical Standard residents must use their ordinary non-diagnostic layout/configuration')
    return json.loads((directory/'entry.compile.json').read_bytes())


def audit(directory,*,allow_dirty=False,self_test=True):
    directory=Path(directory).resolve(strict=True)
    for artifact in ARTIFACTS:
        path=directory/artifact
        require(path.is_file() and not path.is_symlink() and path.stat().st_size>0,
                'Missing ordinary nonempty physical artifact: '+artifact)
    images={name:read_elf(directory/(name+'.elf')) for name in IMAGES}
    layout=check_directory(directory)
    module=(directory/'retail-physical.kui').read_bytes();runtime=verify(module);inspect_retail(module)
    payload,memory=flatten_elf(images['entry'][0])
    require(module[64:]==payload and runtime['memory_bytes']==memory,
            'Physical runtime differs from its complete linked entry ELF')
    source=source_provenance(directory,images,allow_dirty)
    require(runtime['build']==source['commit'][:12],'Physical module build differs from clean source')
    descriptor=typed_descriptor(images['stage'],module)
    require(inspect_ram_patch_seed(module[64:],required=True),'Physical ordinary RAM patch capability is missing')
    patch_address,patch_bytes,patch_section=symbol_bytes(images['stage'],'_ram_patch_seed',1)
    patch_offset=64+STAGE_BLOB_OFFSET+patch_address-STAGE_ADDRESS
    require(patch_address%4==0 and patch_section[2]&7==3 and patch_bytes==RAM_PATCH_SEED and
        module[patch_offset:patch_offset+64]==RAM_PATCH_SEED,
        'Physical ordinary RAM patch seed is not its exact initialized blank object')
    no_virtual_runtime(images,module)
    guard=guard_witness(images['stage'])
    resident_hashes={}
    for transport in RESIDENTS:
        name='resident-'+transport
        binary=(directory/(name+'.bin')).read_bytes()
        resident_hashes[transport]=dict(elf_sha256=sha(images[name][0]),binary_sha256=sha(binary))
        # The layout gate has already proved the complete typed stage blob is
        # exactly the ELF load bytes. Compare to its ordinary build input too.
        original=ROOT/'build/retail'/(name+'.elf')
        require(original.is_file() and original.read_bytes()==images[name][0] and
            (original.with_suffix('.bin')).read_bytes()==binary,
            'Physical embedded resident differs from its ordinary source input: '+transport)
    instruction_counts={}
    for name in IMAGES:
        text=subprocess.check_output(['sh-elf-objdump','-d',str(directory/(name+'.elf'))],text=True)
        instruction_counts[name]=instruction_audit(name,text)
    reports=sorted(directory.rglob('*.ci'))
    require(reports,'Physical resident stack proof lacks compiler callgraphs')
    for report in reports:
        require(not report.is_symlink() and closed_callgraph(report.read_text()),
                'Incomplete physical compiler callgraph: '+str(report))
    require(any((directory/'scia').rglob('*.ci')),'Physical retained asynchronous image lacks complete stack callgraphs')
    mutations=0
    if self_test:
        for index in range(len(GUARD)):
            changed=bytearray(GUARD);changed[index]^=1
            try:execute_guard(bytes(changed),0,1)
            except ValueError:mutations+=1
            else:raise ValueError('Physical audit accepted changed immutable guard code')
        address,body,section=symbol_bytes(images['stage'],SYMBOL,1)
        at=section[4]+address-section[3]
        for index in range(32):
            altered=bytearray(images['stage'][0]);altered[at+index]^=1
            changed=(bytes(altered),*images['stage'][1:])
            try:typed_descriptor(changed,module)
            except ValueError:mutations+=1
            else:raise ValueError('Physical audit accepted changed descriptor byte')
        for altered in (module+CAPABILITY,module[:descriptor['module_offset']]+bytes(32)+module[descriptor['module_offset']+32:]):
            try:typed_descriptor(images['stage'],altered)
            except ValueError:mutations+=1
            else:raise ValueError('Physical audit accepted absent/duplicate complete descriptor')
        for report in reports:
            text=report.read_text()
            for altered in (text[:text.rfind('}')],text+'node: {}'):
                require(not closed_callgraph(altered),'Physical audit accepts a truncated/extra compiler graph')
                mutations+=1
    return dict(schema=SCHEMA,source_provenance=source,runtime=runtime,module_sha256=sha(module),
        reader=0,transport_mask=7,lease_bytes=0,worker_bytes=0,virtual_cards=0,
        background_reader_enabled=False,prefetch_enabled=False,descriptor=descriptor,
        existing_plain_resident_bytes_identical=True,resident_images=resident_hashes,
        standard_only_runtime_guard=True,standard_only_guard=guard,ram_patch_seed_blank=True,
        ram_patch_seed=dict(module_offset=patch_offset,bytes=64,initialized_data=True,blank=True),
        layout=layout,instruction_audit=instruction_counts,
        compiler_callgraphs=dict(complete_files=len(reports)),mutation_rejections=mutations,
        artifact_sha256={name:sha((directory/name).read_bytes()) for name in ARTIFACTS},
        console_execution_verified=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path,nargs='?',default=ROOT/'build/retail-physical')
    parser.add_argument('--allow-dirty',action='store_true')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    try:
        report=audit(args.directory,allow_dirty=args.allow_dirty)
        data=json.dumps(report,indent=2)+'\n'
        if args.output:args.output.write_text(data)
        print(data,end='')
    except (ValueError,AssertionError,KeyError,OSError,struct.error,subprocess.CalledProcessError) as error:
        parser.exit(2,'Physical Standard audit refused: '+str(error)+'\n')


if __name__=='__main__':main()
