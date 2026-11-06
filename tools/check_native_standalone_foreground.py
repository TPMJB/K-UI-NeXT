#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the opt-in Standard foreground control to its actual SH artifacts.

This is an artifact/layout and finite compiler-callgraph check. Hardware SCI
timing, all retail execution, and console compatibility are not established.
The optional original startup executes the linked admission/source checker;
SHA compression is the existing exact-input host leaf in that instruction model.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import shlex
import shutil

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_loader_layout import inspect_elf
from check_retail_stack import load,worst
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from runtime_package import flatten_elf,verify

SOURCE_FILES=(
    'Makefile.native-ocram-bootstrap','include/kui/retail_ocram.h','include/kui/retail_gd.h',
    'include/kui/native_ocram_diagnostic.h','src/loader/native_ocram_diagnostic.inc',
    'include/kui/retail_vram_dma.h','src/loader/retail_vram_copy.h','docs/vc2-vram-dma.md',
    'tests/test_native_legacy_stream.c','tools/run_native_legacy_stream.py',
    'tests/test_native_mmu_buffers.c','tools/run_native_mmu_buffers.py',
    'include/kui/native_ce_dma_events.h','src/loader/native_ce_dma_events.inc',
    'tests/test_native_ce_dma_events.c','tools/run_native_ce_dma_events.py',
    'tests/test_native_dma_stream.c','tools/run_native_dma_stream.py',
    'tests/test_native_pio_stream.c','tools/run_native_pio_stream.py',
    'include/kui/retail_loader_layout.h','src/loader/retail_resident_native_pio.ld',
    'tests/test_retail_async_foreground.c','tools/run_native_standalone_foreground.py',
    'tests/test_native_ocram_diagnostic.c','tools/run_native_ocram_diagnostic.py',
    'src/loader/sci_stream.h','src/loader/retail_display.c','docs/native-ocram-bootstrap.md',
    'src/loader/retail_resident_native_dma.ld',
    'Makefile.native-standalone-foreground','Makefile.native-standalone-general',
    'Makefile.native-standalone-doa2','include/kui/native_standalone_foreground.h',
    'src/core/native_standalone_admission.c','src/loader/native_standalone_reader.inc',
    'src/loader/native_standalone_reader_stage.inc','src/loader/native_standalone_foreground.inc',
    'src/loader/retail_async.c','src/loader/retail_async.h','src/loader/retail_resident.c',
    'src/loader/retail_resident.S','src/loader/retail_stage.c','src/loader/retail_stage.S',
    'src/loader/retail_resident_async_low.ld','src/core/retail_image.c',
    'src/core/retail_gd.c','src/core/retail_cursor.c','src/loader/sci_stream.c',
    'src/loader/sci_sd_bus.c','src/core/native_sdk_sci_family.c',
    'tools/check_native_standalone_foreground.py','tools/check_retail_stack.py',
    'tools/check_loader_layout.py','tools/runtime_package.py','tools/native_standalone_symbols.py')


def require(condition,message):
    if not condition:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def payload_at(image,address,count):
    for kind,offset,va,_,size,_,_,_ in image[2]:
        if kind==1 and va<=address and address+count<=va+size:
            return image[0][offset+address-va:offset+address-va+count]
    raise ValueError('Requested initialized bytes escape their ELF')


def protected_report_path(report,build,startup):
    report=report.resolve()
    require(not report.is_relative_to(ROOT) or report.is_relative_to(ROOT/'build'),
            'Report must not overwrite project source')
    protected=[ROOT/name for name in SOURCE_FILES]
    protected.extend(p for p in build.rglob('*') if p.is_file() and
        (p.suffix in ('.elf','.bin','.kui','.o','.ci','.su','.d') or
         p.name.endswith('.compile.json') or p.name=='config'))
    if startup:protected.append(startup.resolve())
    for path in protected:
        require(report!=path.resolve() and
                not(report.exists() and path.exists() and report.samefile(path)),
                'Report aliases a proof input: '+str(path))
    return report


def linked_startup(stage,resident,startup):
    from check_native_standalone_reader import StageCPU,BinderCPU,StageStop,RETURN
    from prove_native_sdk_sci_structural_runtime import original_contract,production_library,PROGRAM_SOURCES
    class ForegroundStage(StageCPU):
        def plain(self,op,pc):
            self.executing=pc
            if op&0xf00f==0x2000: # MOV.B Rm,@Rn; no register/T side effects.
                self.write(self.r[op>>8&15],self.r[op>>4&15]&255,1)
                return
            if op&0xf00f==0x6007: # NOT Rm,Rn; T and source remain unchanged.
                self.r[op>>8&15]=~self.r[op>>4&15]&0xffffffff
                return
            if op&0xf00f==0x600a: # NEGC Rm,Rn; unsigned borrow from0-Rm-T.
                value=self.r[op>>4&15];borrow=self.sr&1
                self.r[op>>8&15]=(-value-borrow)&0xffffffff
                self.flag(value!=0 or borrow!=0)
                return
            if op&0xf0ff==0x4028: # SHLL16 Rn; T is unchanged.
                n=op>>8&15;self.r[n]=self.r[n]<<16&0xffffffff
                return
            if op>>8==0xca: # XOR #imm,R0; unsigned8-bit immediate, T unchanged.
                self.r[0]^=op&255
                return
            if op&0xf00f in (0x0004,0x0005,0x0006): # MOV.B/W/L Rm,@(R0,Rn).
                count=1<<((op&15)-4)
                self.write((self.r[0]+self.r[op>>8&15])&0xffffffff,
                           self.r[op>>4&15]&((1<<(count*8))-1),count)
                return
            if op>>8 in (0x80,0x81): # MOV.B/W R0,@(disp,Rn); scaled displacement.
                count=1 if op>>8==0x80 else 2
                self.write((self.r[op>>4&15]+(op&15)*count)&0xffffffff,
                           self.r[0]&((1<<(count*8))-1),count)
                return
            # The foreground build deliberately has no source publisher.
            return BinderCPU.plain(self,op,pc)
    owner=original_contract(startup)
    import os
    compiler=shlex.split(os.environ.get('HOST_CC','cc'))
    compiler_file=Path(shutil.which(compiler[0]) or compiler[0]).resolve()
    _,library=production_library()
    flags=['-std=c11','-O2','-shared','-fPIC','-I',str(ROOT/'include')]
    command=[*compiler,*flags,*[str(ROOT/name) for name in PROGRAM_SOURCES],'-o',library._name]
    dependencies=subprocess.check_output([*compiler,*flags,'-MM',
        *[str(ROOT/name) for name in PROGRAM_SOURCES]],cwd=ROOT,text=True)
    paths=set(PROGRAM_SOURCES)
    for line in dependencies.replace('\\\n',' ').splitlines():
        for path in shlex.split(line.split(':',1)[1]):
            path=Path(path).resolve()
            if path.is_relative_to(ROOT):paths.add(str(path.relative_to(ROOT)))
    host_binding=dict(command_from_reviewed_helper=command,compiler=str(compiler_file),
        compiler_sha256=sha(compiler_file.read_bytes()),library_sha256=sha(Path(library._name).read_bytes()),
        preprocessor_source_sha256={name:sha((ROOT/name).read_bytes()) for name in sorted(paths)})
    model=ForegroundStage(stage,resident,owner)
    for register in range(16):
        for value in (0,1,0x7fffffff,0x80000000,0xffffffff,0x13579bdf):
            model.r[register]=value;model.sr=0x400000f1
            model.plain(0x6007|register<<8|register<<4,0)
            require(model.r[register]==(value^0xffffffff) and model.sr==0x400000f1,
                    'NOT model scalar self-test failed')
            model.r[register]=value;model.sr=0x400000f1
            model.plain(0x4028|register<<8,0)
            require(model.r[register]==value<<16&0xffffffff and model.sr==0x400000f1,
                    'SHLL16 model scalar self-test failed')
            for borrow in (0,1):
                model.r[register]=value;model.sr=0x400000f0|borrow
                model.plain(0x600a|register<<8|register<<4,0)
                require(model.r[register]==(-value-borrow)&0xffffffff and
                        model.sr==0x400000f0|int(bool(value or borrow)),
                        'NEGC model scalar self-test failed')
    for register in range(1,16):
        for disp in (0,1,15):
            for count in (1,2):
                address=0x8ce70000+register*64
                model.r[register]=address;model.r[0]=0x123456ab;model.sr=0x400000f1
                before=bytes(model.read(address+i,1) for i in range(64))
                model.plain((0x8000 if count==1 else 0x8100)|register<<4|disp,0)
                expected=bytearray(before)
                expected[disp*count:(disp+1)*count]=bytes.fromhex('ab56')[:count]
                require(bytes(model.read(address+i,1) for i in range(64))==expected and
                        model.r[register]==address and model.r[0]==0x123456ab and
                        model.sr==0x400000f1,'MOV.B/W displacement model self-test failed')
    for value in (0,1,0x80000000,0xffffffff,0x12345678):
        for immediate in (0,1,0x80,0xff):
            model.r[0]=value;model.sr=0x400000f1
            model.plain(0xca00|immediate,0)
            require(model.r[0]==value^immediate and model.sr==0x400000f1,
                    'XOR immediate model self-test failed')
    for count in (1,2,4):
        for offset in (0,4,28):
            address=0x8ce71000;model.r[0]=offset;model.r[1]=address
            model.r[2]=0x123456ab;model.sr=0x400000f1
            before=bytes(model.read(address+i,1) for i in range(64))
            model.plain(0x0124+{1:0,2:1,4:2}[count],0)
            expected=bytearray(before)
            expected[offset:offset+count]=bytes.fromhex('ab563412')[:count]
            require(bytes(model.read(address+i,1) for i in range(64))==expected and
                    model.r[:3]==[offset,address,0x123456ab] and model.sr==0x400000f1,
                    'MOV.B/W/L indexed-store model self-test failed')
    model.r[1]=0x8ce72000;model.r[2]=0x123456ab;model.sr=0x400000f1
    before=bytes(model.read(model.r[1]+i,1) for i in range(4))
    model.plain(0x2120,0)
    require(bytes(model.read(model.r[1]+i,1) for i in range(4))==b'\xab'+before[1:] and
            model.r[1:3]==[0x8ce72000,0x123456ab] and model.sr==0x400000f1,
            'MOV.B direct-store model self-test failed')
    address=stage[4]['_manifest'][0]
    cases=[]
    for tracks,extents,reader,transport,flags in (
        (2,2,0,1,4),(1,63,0,1,0),(63,1,0,1,4),
        (0,1,0,1,0),(1,0,0,1,0),(64,1,0,1,0),
        (1,64,0,1,0),(0xffffffff,1,0,1,0),
        (2,2,1,1,0),(2,2,2,1,0),(2,2,0,0,0),
        (2,2,0,1,1),(2,2,0,1,2),(2,2,0,1,8)):
        c=ForegroundStage(stage,resident,owner);data=bytearray(2172)
        for off,value in ((24,tracks),(28,extents),(40,len(owner['image'])),
                          (44,transport),(48,reader),(52,flags)):
            struct.pack_into('<I',data,off,value)
        c.put_block(address,data)
        c.call('_kui_native_standalone_launch_prepare',(address,))
        accepted=bool(tracks and tracks<=64 and extents and extents<=64-tracks and
                      reader==0 and transport==1 and not(flags&~4))
        require(c.r[0]==accepted,'Linked foreground selector mismatch')
        require(bytes(c.read(address+i,1) for i in range(len(data)))==data,
                'Linked foreground selector changed its manifest')
        cases.append(dict(tracks=tracks,extents=extents,reader=reader,transport=transport,
                          flags=flags,accepted=accepted))
    source_cases=[]
    for name in ('valid','vector_mutated','extent_truncated'):
        c=ForegroundStage(stage,resident,owner)
        if name=='vector_mutated':c.put(owner['vector'],c.read(owner['vector'])^1)
        if name=='extent_truncated':c.put(stage[4]['_exec_bytes'][0],128)
        refused=False;c.pr=RETURN;c.reset_trace()
        try:c.run_to(c.symbols['_standalone_source_check'][0],RETURN,limit=8000000)
        except StageStop:refused=True
        require(refused==(name!='valid'),'Linked initial SDK family source result differs: '+name)
        require(c.publisher_visits==0,'Foreground source check published callbacks')
        source_cases.append(dict(name=name,refused=refused,instructions=c.instructions,
                                 sha_inputs=c.sha_inputs))
    return dict(startup=str(startup),sha256=sha(startup.read_bytes()),
                selector_cases=cases,source_cases=source_cases,
                original_full_sdk_family_source_check=True,
                callback_publications=0,sha_leaf='exact-input hashlib in existing StageCPU',
                host_original_contract=host_binding,
                all_retail_startup_executed=False)


def audit(args):
    build=args.build.resolve();root=ROOT
    args.report=protected_report_path(args.report,build,args.startup)
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip()
    dirty=bool(subprocess.check_output(['git','status','--porcelain','--untracked-files=no'],
                                      cwd=root,text=True))
    if args.require_clean:require(not dirty,'Source is dirty')
    if args.require_clean:
        subprocess.run(['git','ls-files','--error-unmatch',*SOURCE_FILES],cwd=root,
                       stdout=subprocess.DEVNULL,stderr=subprocess.PIPE,check=True)
    resident=read_elf(build/'resident-sci.elf');stage=read_elf(build/'stage.elf')
    entry=read_elf(build/'entry.elf');rs=resident[4];ss=stage[4]
    ocram=rs.get('__kui_native_ocram_bootstrap_resident')==(1,0,0,0xfff1)
    dma=rs.get('__kui_native_gd_dma_stream')==(1,0,0,0xfff1)
    pio=rs.get('__kui_native_gd_pio_stream')==(1,0,0,0xfff1)
    transfer=rs.get('__kui_native_transfer_diagnostic')==(1,0,0,0xfff1)
    vram=rs.get('__kui_native_gd_vram_dma')==(1,0,0,0xfff1)
    stack_bottom=0x8c007ea0 if vram else 0x8c007e20 if pio and transfer else 0x8c007dc0 if pio else 0x8c007ba0
    stack_top=0x8c008000 if vram else 0x8c007f80 if pio and transfer else 0x8c007f20 if pio else 0x8c007d00
    if args.require_ocram:
        require(ocram,'Missing native OCRAM bootstrap compile marker')
        require('_kui_retail_ce_kernel' not in rs and '_kui_retail_ce_deferred' not in rs,
                'Native bootstrap bridge contains CE entry/state')
        require(rs.get('__kui_native_ocram_bss_publish')==(1,0,0,0xfff1),
                'Missing native cache handoff publication marker')
        require(rs['_kui_native_ocram_trace'][1]==148,'Diagnostic assembly ABI differs')
        require(rs.get('__kui_native_gd_legacy_stream')==(1,0,0,0xfff1),
                'OCRAM profile must contain native legacy DMA streaming')
        require(rs.get('__kui_native_gd_mmu_buffers')==(1,0,0,0xfff1) and
                rs.get('__kui_native_gd_mmu_buffers_entry')==(1,0,0,0xfff1),
                'Missing native CPU virtual-buffer or safe entry marker')
        require(rs['_kui_native_ocram_trace'][0]==rs['_manifest'][0]+68,
                'Trace must occupy only unused copied launch metadata')
        require(not any(s[2]&3==3 and s[1]!=8 and s[5] for s in resident[3]),
                'BSS-only publisher would miss initialized mutable RAM')
    for image,marker in ((resident,'__kui_native_standalone_foreground_resident'),
                         (stage,'__kui_native_standalone_foreground_stage')):
        require(image[4].get(marker)==(1,0,0,0xfff1),'Missing foreground compile marker')
    for path,base,limit in (('resident-sci',0x8c004000,stack_bottom),
                            ('stage',0x8ce00000,0x8cfe0000)):
        inspected=inspect_elf((build/(path+'.elf')).read_bytes(),base,limit)
        require(inspected['payload']==(build/(path+'.bin')).read_bytes(),path+' bin/ELF differs')
    require(rs['_manifest'][1]==1020 and
            (rs.get('__native_dma_region_bytes',(0,))[0]==0x640 if dma else
             rs['_standalone_reader_region'][1]==(0x660 if ocram else 0x760)),
            'Foreground low map/engine geometry changed')
    if dma:
        cold_begin=rs['__native_dma_cold_begin'][0]
        cold_end=rs['__native_dma_cold_end'][0]
        require(cold_begin==rs['_standalone_reader_region'][0] and not cold_begin&31,
                'Cold init must start in the aligned private receive buffers')
        require(0<cold_end-cold_begin<=0x440 and
                cold_begin<=rs['_kui_retail_resident_init'][0]<cold_end,
                'Cold adoption escapes its retired receive-buffer lifetime')
        require(rs['__retail_resident_bss_begin'][0]==cold_end and
                rs['__retail_resident_binary_end'][0]==cold_end and
                rs['_manifest'][0]>=cold_begin+0x640,
                'Cold init must be exempt from BSS clearing; live state must be zeroed')
        for name,offset in (('_kui_retail_hook_sr',228),('_kui_retail_hook_source',232),
                            ('_kui_retail_original_menu',236),('_kui_retail_hook_fault',240),
                            ('_kui_retail_hook_active',244),('_row',248)):
            require(rs[name][0]==rs['_manifest'][0]+offset and rs[name][1]==4,
                    'Gateway/renderer state escaped unused launch metadata: '+name)
    if pio:
        deferred=rs['_kui_native_pio_deferred']
        require(deferred[1]==8 and
                rs['__retail_resident_bss_begin'][0]<=deferred[0] and
                deferred[0]+8<=rs['__retail_resident_bss_end'][0],
                'PIO deferred pair must be covered by reader-state publication')
    require(rs['__retail_hook_stack_bottom'][0]==stack_bottom and
            rs['__retail_hook_stack'][0]==stack_top and stack_top<=0x8c008000,
            'Private guarded stack differs from its profile reservation')
    require(rs['__retail_resident_bss_end'][0]<=rs['__retail_hook_stack_bottom'][0],
            'Low BSS overlaps its stack')
    for name in ('_standalone_source_publish','_kui_native_standalone_reader_capability'):
        require(name not in ss,'Foreground contains a New source publisher/capability')
    require('_kui_native_standalone_bind' not in rs,'Empty foreground binder unexpectedly survived LTO')
    if ocram:
        require('_kui_retail_async_irq' not in rs,
                'Foreground legacy profile retains an unpublished raw IRQ entry')
        for name in ('_kui_retail_sdk_sci_entry','_kui_native_sdk_sci_route_exports',
                     '_kui_native_sdk_sci_route_entry'):
            require(name not in rs,'OCRAM foreground retains an unpublished SDK callback: '+name)
    if not ocram or '_kui_retail_sdk_sci_dispatch' in rs:
        # LTO may retain this unreferenced four-byte noipa leaf in shared text.
        address,dispatch,_=symbol_bytes(resident,'_kui_retail_sdk_sci_dispatch',2)
        require(dispatch==payload_at(resident,address,len(dispatch)),
                'SDK dispatch section bytes differ from its loaded bytes')
        require(dispatch==bytes.fromhex('0b0000e0'),'Dormant SDK dispatcher must be RTS / MOV #0,R0')
    menu_entry=rs['_kui_retail_menu_hook'][0]
    require(struct.unpack('<7H',payload_at(resident,menu_entry,14))==
            (0x6043,0x8801,0x8907,0x8802,0x8b01,0x000b,0xe000),
            'Immutable native E0 function2 must retain its stack-free zero-result leaf')
    address,menu,_=symbol_bytes(resident,'_kui_retail_menu_return',2)
    require(not address&3,'Terminal hold literal schedule requires aligned code')
    require(menu==payload_at(resident,address,len(menu)),
            'Menu return section bytes differ from its loaded bytes')
    # Exact return body: one terminal reporter and an infinite NOP/BRA loop.
    require(len(menu)==24 and struct.unpack('<8H',menu[:16])==
            (0xd103,0xd404,0x4f22,0x410b,0x0009,0x0009,0xaffd,0x0009),
            'Foreground terminal hold schedule changed')
    reporters=[value[0] for name,value in rs.items() if name=='_native_reader_report' or
               name.startswith('_native_reader_report.isra.')]
    require(len(reporters)==1 and struct.unpack_from('<I',menu,16)[0]==reporters[0],
            'Terminal hold reporter target differs')
    label=struct.unpack_from('<I',menu,20)[0]
    require(payload_at(resident,label,4)==b'RET\0','Foreground return diagnostic label differs')
    cap=struct.pack('<8I',0x3146474b,0x574f4c46,1,32,0,64,0,0)
    cap_address=ss['_kui_native_foreground_reader_capability'][0]
    require(payload_at(stage,cap_address,len(cap))==cap,'Foreground capability differs')
    require((build/'stage.bin').read_bytes().count(cap)==1,'Foreground capability is not unique')
    for outer,start,end,path in ((stage,'__retail_resident_sci_blob_start',
                                  '__retail_resident_sci_blob_end','resident-sci.bin'),
                                 (entry,'__retail_stage_blob_start','__retail_stage_blob_end','stage.bin')):
        begin=outer[4][start][0];finish=outer[4][end][0]
        require(payload_at(outer,begin,finish-begin)==(build/path).read_bytes(),
                'Embedded '+path+' differs')
    forbidden=('__native_worker','__native_ra_aux','__disc_support')
    require(not any(any(n.startswith(prefix) for prefix in forbidden) for n in ss),
            'Foreground unexpectedly contains a high worker/auxiliary')
    envelope=(build/'retail-native-foreground.kui').read_bytes();metadata=verify(envelope)
    payload,memory=flatten_elf((build/'entry.elf').read_bytes())
    require(envelope[64:]==payload and metadata['memory_bytes']==memory,'Envelope/entry ELF differs')
    provenance=json.loads((build/'entry.compile.json').read_text())
    require(provenance['commit']==commit and provenance['elf_sha256']==sha(entry[0]),
            'Entry compile provenance does not bind current source/ELF')
    require(metadata['build']==commit[:12],'Runtime build ID differs from source')
    if args.require_clean:require(not provenance['source_dirty'],'Compile provenance is dirty')
    reports=sorted(build.glob('resident-sci.elf.ltrans*.ci'))
    require(reports,'Missing actual compiler call graph')
    for path in reports:
        require(path.read_text().rstrip().endswith('}'),'Truncated compiler graph')
    frames,calls=load(reports)
    paths=worst(frames,calls,roots_override={
        'kui_retail_resident_dispatch':0,'kui_retail_menu_return':0})
    deepest=max(row['bytes'] for row in paths.values())
    require(deepest+64<=352-16,'Foreground GD/return closure exceeds its private stack')
    helper_stack=None
    if ocram:
        # Pin the complete linked SDK division helper, including its two
        # local leaf tails. Its sole stack save is PR (4 bytes); the 64-byte
        # aggregate reserve below includes the 16-byte bottom guard and a
        # further 48 bytes for compiler-library calls, with 16 bytes at top.
        helper=payload_at(resident,rs['___udivsi3'][0]-34,108)
        require(sha(helper)=='dad813c9ae03d34d9a9db7a010ce9892fba22e3d636cceada792e094bb6a5349',
                'Linked compiler division helper differs from its 4-byte stack body')
        helper_stack=dict(bytes=108,sha256=sha(helper),maximum_stack_bytes=4,
                          bottom_guard_bytes=16,library_reserve_bytes=48,
                          library_reserve_above_verified_helper=44)
    result=dict(schema='kui.native.standalone-foreground.artifact.v1',result='PASS',
        commit=commit,source_dirty=dirty,compile_provenance=provenance,runtime=metadata,
        build=str(build),capability=dict(reader='STANDARD',slots=64,heap_lease_bytes=0,
                                        sdk_callbacks_published=False,cpu_ocram_bootstrap=ocram,
                                        native_legacy_dma_stream=ocram,cpu_virtual_buffers=ocram,
                                        native_scatter_dma_stream=dma,native_pio_stream=pio),
        low_geometry={n:hex(rs[n][0]) for n in ('__retail_resident_binary_end',
            '__retail_resident_bss_end','__retail_hook_stack_bottom','__retail_hook_stack',
            '_manifest','_standalone_reader_region')},
        stack=dict(compiler_nodes=len(frames),roots=paths,deepest_bytes=deepest,
                   conservative_margin=64,top_reserved=16,stack_bytes=352,
                   compiler_helper_and_guard=helper_stack,
                   callback_stack='owner' if pio else None,
                   callback_shim_bytes=32 if pio else 0,
                   dormant_unpublished_irq_not_a_root=True,
                   scope='Compiler emitted callgraph with existing closed callback sets; no hardware timing'),
        source_sha256={name:sha((root/name).read_bytes()) for name in SOURCE_FILES},
        artifact_sha256={name:sha((build/name).read_bytes()) for name in (
            'entry.elf','entry.compile.json','stage.elf','stage.bin','resident-sci.elf',
            'resident-sci.bin','retail-native-foreground.kui')},
        compiler_graph_sha256={str(p.relative_to(build)):sha(p.read_bytes()) for p in reports},
        limits=['No SD hardware or console game compatibility is proved.',
                'The full initial SDK family grammar remains a stage precondition.',
                'This is the opt-in physical-VMU control; it adds no virtual VMU or CD audio.',
                'Private-stack result uses emitted compiler graphs and existing callback sets; '
                'it is not a new complete linked call-target audit.',
                'Compiler graphs and dirty-source hashes are not independently attested by ELF bytes. '
                'They require the accompanying trusted fresh-build receipt; the checker does not '
                'establish a hermetic source or compiler-graph rebuild.'])
    if args.startup:result['linked_startup']=linked_startup(stage,resident,args.startup.resolve())
    result['proof_python_dependencies_sha256']={str(Path(module.__file__).resolve().relative_to(root)):
        sha(Path(module.__file__).read_bytes()) for module in tuple(sys.modules.values())
        if getattr(module,'__file__',None) and Path(module.__file__).suffix=='.py' and
        Path(module.__file__).resolve().is_relative_to(root)}
    args.report.parent.mkdir(parents=True,exist_ok=True)
    args.report.write_text(json.dumps(result,indent=2)+'\n')
    print('PASS foreground control artifacts; deepest static stack',deepest,'+64 /336')


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,required=True)
    parser.add_argument('--report',type=Path,required=True)
    parser.add_argument('--startup',type=Path)
    parser.add_argument('--require-clean',action='store_true')
    parser.add_argument('--require-ocram',action='store_true')
    audit(parser.parse_args())


if __name__=='__main__':main()
