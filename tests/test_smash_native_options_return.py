#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute native publication and preserve original combo/options/Return routes.

Original MDE remains an external exact-hash input. Controller objects and the
two post-emulation SDK calls/UI draw are explicit boundaries. No publication,
CRC, memcpy, original scalar branch, or patched reset instruction is replaced.
"""
import argparse,ctypes,hashlib,json,struct,subprocess,sys,tempfile,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from test_smash_native_gin_linked import Machine,State,RETURN,STACK,read_elf
from test_smash_menu_return_runtime import prepare,FIRMWARE,FIRMWARE_ADDRESSES,phase_video_seed
from check_vmu_native_readonly import relocated

LOAD,RESET,LOW=0x8c010000,0x8c0117f0,0xac008300
FLAG,PAD,PAD6,GETPAD=0x8c48f514,0x8c700000,0x8c700200,0x8c1a9648
MDE_SHA='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
def sha(b):return hashlib.sha256(b).hexdigest()
def word(m,a,v):m.put(a,struct.pack('<I',v))
def u32(m,a):return struct.unpack('<I',m.block(a,4))[0]
def require(ok,msg):
    if not ok:raise AssertionError(msg)

def publication(run,build,stage,source):
    sy=stage[4];gate=read_elf(build/'return-loader.elf')
    require(sy.get('__kui_smash_native_original_options')==(1,0,0,0xfff1),'Missing original-options capability')
    for forbidden in ('__kui_smash_native_combo_direct_outer','_kui_smash_native_combo_plan','_smash_return_combo_publish'):
        require(forbidden not in sy,'Direct-outer combo policy remains linked')
    authored,*_=prepare(gate[0],gate[2],gate[4],source,'sf_return')
    wire=authored.block(gate[4]['_kui_smash_menu_return_runtime'][0],gate[4]['_kui_smash_menu_return_runtime'][1])
    require(len(wire)==sy['_smash_return_prepared'][1]==596,'Native return ABI changed')
    m=Machine(run);m.load(stage);m.put(LOAD,source)
    m.put(sy['__smash_menu_return_seed'][0],wire[:368])
    m.put(sy['_smash_card_receipt'][0],wire[420:516]);m.put(sy['__smash_phase_video_seed'][0],phase_video_seed())
    manifest=bytearray(sy['_manifest'][1]);struct.pack_into('<QQQ',manifest,0,1024,1,1024)
    struct.pack_into('<I',manifest,40,len(source));struct.pack_into('<I',manifest,60,0x18efc93a)
    m.put(sy['_manifest'][0],manifest);word(m,sy['_boot_crc'][0],zlib.crc32(source))
    for a,v in zip(FIRMWARE_ADDRESSES,FIRMWARE):word(m,a,v)
    m.put(0xac008100,bytes([0x6d])*0x3f00)
    stops={v[0]for n,v in sy.items()if n.startswith(('_relay_stopped','_stopped','_native_report'))}
    def call(name,args=()):
        _,steps,status=m.call(sy[name][0],args,break_pc=sy['_relay_stopped'][0])
        require(status==0 and m.c.pc==RETURN and m.c.pc not in stops,'Actual stage refused valid options fixture: '+name)
        return steps
    before_low=m.block(0xac008100,0x3f00);prep_steps=call('_kui_smash_native_return_stage_prepare')
    require(m.block(LOAD,len(source))==source and m.block(0xac008100,0x3f00)==before_low,
        'Preparation published code or changed original MDE')
    prepared=m.block(sy['_smash_return_prepared'][0],596)
    code=(build/'return-loader.bin').read_bytes();raw=(build/'smash-return-raw.bin').read_bytes()
    record=bytearray(prepared);authority=struct.unpack_from('<I',record,412)[0];record[412:416]=bytes(4)
    require(authority==zlib.crc32(record,zlib.crc32(code))&0xffffffff,'Prepared full runtime CRC is wrong')
    snapshot=m.snapshot();negative=[]
    for fault in ('reset_source_crc','return_seed_crc'):
        bad=Machine(run,snapshot)
        at=RESET+16 if fault=='reset_source_crc'else sy['_smash_return_prepared'][0]+28
        bad.put(at,bytes([bad.block(at,1)[0]^1]));before=bad.block(0xac008100,0x3f00);owner=bad.block(LOAD,len(source))
        _,steps,status=bad.call(sy['_kui_smash_native_return_stage_publish'][0],break_pc=sy['_relay_stopped'][0])
        require(status==1 and bad.block(0xac008100,0x3f00)==before and bad.block(LOAD,len(source))==owner,
            'Invalid source/seed published low code or changed MDE')
        negative.append(dict(fault=fault,actual_instructions=steps,no_publication=True))
    m.c.watch_first[0]=LOAD&0x1fffffff;m.c.watch_end[0]=(LOAD+len(source))&0x1fffffff
    publish_steps=call('_kui_smash_native_return_stage_publish')
    expected=bytearray(source);expected[0x17f0:0x17fc]=struct.pack('<4HI',0xd001,0x402b,9,9,LOW)
    require(m.block(LOAD,len(source))==expected,'Publication changed an original MDE byte outside117F0 reset')
    require(m.block(LOAD+0x48e,4)==struct.pack('<2H',0xa1f8,0x2ec2),'Original combo/options branch changed')
    runtime=gate[4]['_kui_smash_menu_return_runtime'][0]
    copied=bytearray(m.block(LOW,len(code)));copied[runtime-LOW:runtime-LOW+596]=bytes(596)
    require(m.block(0xac008100,512)==raw and copied==code and m.block(runtime,596)==prepared,
        'Published low reader/code/runtime differ from exact checked artifacts')
    require(m.block(0xac00b800,2048)==bytes(2048),'Private return stack was not initialized')
    dest,span=0xacbe0000,0x12000;binary=(build/'native-worker.bin').read_bytes()
    require(len(binary)<=span,'Native worker exceeds current72KiB span')
    body=relocated(binary,(build/'native-worker.reloc').read_bytes(),dest)
    m.put(dest,body+bytes(span-len(body)));bind_steps=call('_kui_smash_native_return_stage_bind',(dest,len(body),span))
    final=bytearray(m.block(runtime,596));target=struct.unpack_from('<I',final,408)[0]
    worker=read_elf(build/'native-worker.elf')
    require(target==worker[4]['_kui_smash_native_return_entry'][0]+dest-0xac100000,'Worker retirement binding changed')
    sealed=struct.unpack_from('<I',final,412)[0];final[412:416]=bytes(4)
    require(sealed==zlib.crc32(final,zlib.crc32(code))&0xffffffff,'Worker binding omitted full runtime reseal')
    return bytes(expected),dict(prepare_instructions=prep_steps,publication_instructions=publish_steps,
        worker_bind_instructions=bind_steps,native_options_menu_preserved=True,
        only_original_reset_patched=True,reset_patch_address=hex(RESET),reset_patch_bytes=12,
        original_combo_branch='0xa1f8',actual_low_reader_code_runtime_exact=True,
        full596_runtime_crc_verified=True,actual72KiB_worker_bound=True,private_stack_zero=True,refusals=negative)

def combo(run,image,held,edge,secondary):
    m=Machine(run);m.put(LOAD,image);m.c.r[15]=STACK;m.c.pc=0x8c019894;m.c.pr=RETURN;m.c.sr=0x40000000
    for at in (PAD,PAD6):m.put(at,bytes(256));word(m,at,1)
    chosen=PAD6 if secondary else PAD;word(m,chosen+8,held);word(m,chosen+16,edge)
    word(m,FLAG,0);word(m,0x8c48f40c,0);calls=[]
    m.c.watch_first[0]=0x0c008100;m.c.watch_end[0]=0x0c00c000
    for _ in range(4):
        status=m.execute(stop=RETURN,break_pc=GETPAD,budget=1000000)
        if status==0:break
        require(m.c.pc==GETPAD,'Original input reached a different boundary')
        port=m.c.r[4];require(port in (0,6),'Unexpected original input port');calls.append(port)
        m.c.r[0]=PAD if port==0 else PAD6;m.c.pc=m.c.pr
    else:raise AssertionError('Original input fixture exceeded bounded port visits')
    flag=u32(m,FLAG);require(flag==int(held&0x606==0x606 and edge&8!=0)and not m.c.watch_writes[0],
        'Original combo predicate changed or entered low return code')
    return m,dict(held=hex(held),start_edge=bool(edge&8),secondary=secondary,flag=flag,
        controller_state_boundary_ports=calls,actual_source_instructions=m.c.steps,low_return_gate_writes=0)

def options_route(m):
    sp=STACK-0x100;group=0x8c514000;sram=0x8c48f3e0
    # Exact saved main-loop/containing-options/caller frame topology. SDK
    # callee bodies and menu draw are explicit void-return fixture boundaries.
    main=[0]*4+[0x8c01ae6c,0x8c1a1c4c,0x8c1a2130,sram,5,0x8c47ee1c,0,group]
    opts=[0,0x8c0103a0,0,0,0,0,0,0,sram]
    caller=[0x8c010292,0,sram];outer=[RETURN,0,0,0]
    m.put(sp,struct.pack('<'+str(len(main+opts+caller+outer))+'I',*(main+opts+caller+outer)))
    m.c.r[15]=sp;m.c.r[14]=FLAG;m.c.r[12]=0;m.c.pc=0x8c010488;word(m,group,0x8c720000)
    boundaries=[]
    for target in (0x8c1a2130,0x8c1a1c4c):
        require(m.execute(stop=0x8c01adb0,break_pc=target,budget=1000)==1 and m.c.pc==target,
            'Combo skipped original post-emulation options work')
        boundaries.append(hex(target));m.c.pc=m.c.pr;m.c.r[0]=0
    require(m.execute(stop=0x8c01adb0,budget=1000)==0 and u32(m,FLAG)==0,
        'Original combo did not return to options loop')
    require(m.c.r[15]==sp+48,'Combo changed original options frame')
    # Continue the original explicit Return selection. The separate options
    # eligibility proof covers Save Game gates; this trace covers Return's
    # save-confirm eligibility before the existing reset hook.
    m.c.pc=0x8c01aeaa
    for target in (0x8c010a40,0x8c1a2130,0x8c1a1c4c):
        require(m.execute(stop=0x8c0103a0,break_pc=target,budget=1000)==1,
            'Original Return changed its UI/SDK sequence')
        boundaries.append(hex(target));m.c.pc=m.c.pr;m.c.r[0]=0
    require(m.execute(stop=0x8c0103a0,budget=1000)==0,'Options Return skipped its original caller')
    word(m,sram,1);word(m,sram+8,1)
    require(m.execute(stop=RESET,break_pc=0x8c01834c,budget=1000)==1 and m.c.pc==0x8c01834c,
        'Dirty SRAM Return skipped the original save-confirm function')
    boundaries.append(hex(m.c.pc));m.c.pc=m.c.pr;m.c.r[0]=0
    require(m.execute(stop=RESET,budget=1000)==0,'Original Return did not reach existing reset')
    require(m.execute(stop=LOW,budget=100)==0,'Patched reset did not reach checked low return entry')
    return dict(original_options_entry='0x8c01adb0',explicit_Return_reaches_reset=hex(RESET),
        dirty_save_confirm_preserved=True,low_entry=hex(LOW),authored_UI_SDK_boundaries=boundaries)

def audit(stage_path,mde_path):
    build=stage_path.parent;source=mde_path.read_bytes()
    require(len(source)==1848288 and sha(source)==MDE_SHA,'Unreviewed original MDE')
    paths=[stage_path,build/'return-loader.elf',build/'return-loader.bin',build/'smash-return-raw.bin',
        build/'native-worker.elf',build/'native-worker.bin',build/'native-worker.reloc']
    before={str(p):sha(p.read_bytes())for p in paths};stage=read_elf(stage_path)
    sources={Path(__file__).resolve(),ROOT/'tests/sh4_integer_fast.c',ROOT/'src/loader/smash_native_return_stage.inc',
        ROOT/'Makefile.smash-native-menu'}
    for mod in tuple(sys.modules.values()):
        p=getattr(mod,'__file__',None)
        if p:
            p=Path(p).resolve()
            if p.is_relative_to(ROOT)and p.suffix=='.py':sources.add(p)
    source_before={str(p.relative_to(ROOT)):sha(p.read_bytes())for p in sorted(sources)}
    with tempfile.TemporaryDirectory(prefix='smash-native-options-')as folder:
        library=Path(folder)/'integer.so';subprocess.run(['cc','-std=c11','-O2','-shared','-fPIC',
            str(ROOT/'tests/sh4_integer_fast.c'),'-o',str(library)],check=True,capture_output=True)
        dll=ctypes.CDLL(str(library));run=dll.sh4_fast_run
        run.argtypes=[ctypes.POINTER(State),ctypes.c_uint32,ctypes.c_uint64];run.restype=ctypes.c_int
        published,linked=publication(run,build,stage,source);rows=[]
        for mask in range(16):
            held=sum(bit for i,bit in enumerate((2,4,0x200,0x400))if mask>>i&1)
            for edge in (0,8):
                for secondary in (False,True):
                    m,row=combo(run,published,held,edge,secondary)
                    if row['flag']:row['original_options_return']=options_route(m)
                    else:
                        m.c.pc=0x8c010488;m.c.r[14]=FLAG;m.c.r[12]=0
                        require(m.execute(stop=0x8c010492,budget=100)==0,'Noncombo game loop changed')
                        row['original_noncombo_fallthrough_preserved']=True
                    rows.append(row)
    require(before=={str(p):sha(p.read_bytes())for p in paths},'Linked native inputs changed during proof')
    require(source_before=={str(p.relative_to(ROOT)):sha(p.read_bytes())for p in sources},'Proof source changed during execution')
    return dict(schema='kui.smash-native.options-return.v1',status='PASS',native_options_menu_preserved=True,
        original_mde_sha256=MDE_SHA,stage_sha256=sha(stage[0]),inputs=before,source_sha256=source_before,
        test_sha256=source_before['tests/test_smash_native_options_return.py'],linked=linked,cases=rows,
        limits=['Controller states, menu draw and the two post-emulation SDK callees are explicit boundaries; no renderer, hardware drain or physical save completion is certified.',
            'Actual linked prepare/publication/worker binding and original MDE scalar combo/options/Return/save-confirm routing execute.',
            'Original game files are never changed; only private modeled RAM contains the reset patch.'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--original-mde',type=Path,required=True)
    p.add_argument('--stage',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    result=audit(a.stage,a.original_mde);a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(status='PASS',report=str(a.report),cases=len(result['cases']))))
