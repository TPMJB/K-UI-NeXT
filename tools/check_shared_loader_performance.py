#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind shared loader optimizations to the exact installed build cohort.

This complements the existing per-profile safety audits; it never replaces
ownership, relocation, stack, save-protocol or original-SDK companions.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_loader_layout import SYM,inspect_elf,region
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from runtime_package import envelope,flatten_elf,verify
from native_runtime_layout import layout_for,BACKGROUND_COMPACT

REQUIRED={'standard','background_a2','background_a1','disc_a2','disc_a1'}
OPTIONAL={'standard_scif','smash_outer','smash_child'}
ONE=(1,0,0,0xfff1)


def require(ok,message):
    if not ok:raise ValueError(message)


def sha(data):return hashlib.sha256(data).hexdigest()


def runtime_standard_manifest_binding(image, expected_hex):
    """Bind GCC's actual eight word comparisons after constant folding.

    Optimized SH GCC folds memcmp against the digest into scalar compares
    and discards the now-unused C array. Each comparison must read its exact
    digest stack word; every mismatch must return to the historical fallback.
    """
    address,body,section=symbol_bytes(image,'_kui_native_vmu_standard_recognize',2)
    expected=bytes.fromhex(expected_hex)
    require(len(expected)==32 and section[2]&6==6,'Invalid runtime Standard digest/function')
    words=struct.unpack('<'+'H'*(len(body)//2),body)
    found={};sites=[]
    for i in range(1,len(words)-2):
        load,literal,compare,branch=words[i-1:i+3]
        register=load>>8&15
        if load&0xf0f0!=0x50f0 or not 6<=load&15<=13 or literal&0xff00!=0xd100:
            continue
        if compare!=0x3000|(register<<8)|0x10:continue
        index=(load&15)-6
        at=(((address+2*i+4)&~3)+4*(literal&255))-address
        require(0<=at<=len(body)-4 and index not in found,'Ambiguous/outside runtime digest literal')
        value=body[at:at+4]
        require(value==expected[4*index:4*index+4],'GUI Standard digest word differs from installed cohort')
        found[index]=value;sites.append((i,branch))
    require(set(found)==set(range(8)) and len(sites)==8,'GUI does not compare every Standard digest word')
    failures=[]
    for ordinal,(i,branch) in enumerate(sites):
        require(branch>>8==(0x89 if ordinal==7 else 0x8f),'Runtime digest comparison branch changed')
        if ordinal==7:continue
        target=2*(i+2)+4+2*((branch&255)-256 if branch&128 else branch&255)
        require(0<=target<=len(body)-4,'Runtime digest mismatch escapes its recognizer')
        j=target//2
        if words[j]==0x68a2 and words[j+1]==0x2888:
            failures.append(target+2)
        else:
            op=words[j];delta=op&4095;delta-=4096 if delta&2048 else 0
            require(op>>12==10 and words[j+1]==0x68a2,'Runtime digest mismatch skips package restoration')
            end=target+4+2*delta
            require(0<=end<len(body) and words[end//2]==0x2888,'Runtime mismatch misses historical fallback')
            failures.append(end)
    require(len(set(failures))==1,'Runtime digest mismatch paths do not share the same fallback')
    return dict(normalized_payload_sha256=expected_hex,actual_scalar_word_comparisons=8,
        all_mismatches_use_historical_fallback=True,recognizer_sha256=sha(body),
        compiler_folded_digest_array=True)


def receipt(path,commit,allow_dirty):
    data=path.read_bytes();proof_path=path.with_suffix('.compile.json')
    proof=json.loads(proof_path.read_text())
    require(proof.get('commit')==commit and type(proof.get('source_dirty')) is bool and
            proof.get('elf_sha256')==sha(data),'Stale linked compile receipt: '+str(path))
    require(allow_dirty or not proof['source_dirty'],'Dirty linked source: '+str(path))
    return dict(elf_sha256=sha(data),compile_sha256=sha(proof_path.read_bytes()),provenance=proof)


def compact_background_binding(stage,worker,resident,legacy,auxiliary,slot):
    """Require the current compact contract from all five linked owners.

    A historical General or optional session build must never satisfy the
    normal release gate merely because its dual-reader API has the same name.
    """
    from check_native_dual_readers import compact_component_markers
    require(slot in (1,2),'Unknown compact Background virtual slot')
    images=(stage,worker,resident,legacy,auxiliary)
    require(all(layout_for(image[4]) is BACKGROUND_COMPACT for image in images),
            'Default Background requires the compact72 worker and284KiB lease')
    geometry=BACKGROUND_COMPACT
    require((geometry.WORKER_BYTES,geometry.LEASE_BYTES,geometry.DATA_BYTES,geometry.WORKER_STACK_BYTES)==
            (0x12000,0x47000,0x30000,0xc00),'Compact Background geometry oracle changed')
    markers=compact_component_markers(*images)
    require(markers.get('matching_immutable_component_markers') is True,
            'Default Background components do not share the compact contract')
    require(not any(name in image[4] for image in images for name in (
        '__kui_native_session_controls_worker','__native_session_descriptor',
        '_kui_retail_native_session_control')),
        'Optional session-controls ABI cannot replace default compact Background')
    expected=(slot,0,0,0xfff1)
    require(stage[4].get('__kui_native_virtual_slot_stage')==
            worker[4].get('__kui_native_virtual_slot_worker')==expected,
            'Compact Background stage/worker virtual-card slots differ')
    return dict(profile='compact72',worker_bytes=geometry.WORKER_BYTES,
        lease_bytes=geometry.LEASE_BYTES,cache_bytes=geometry.DATA_BYTES,
        worker_stack_bytes=geometry.WORKER_STACK_BYTES,virtual_slot=slot,
        readers=['new_x','legacy_y'],component_markers=markers,
        optional_session_controls=False)


def module(build,role,commit,allow_dirty):
    disc=role.startswith('disc_');entry='disc-entry.elf' if disc else 'entry.elf'
    stage='disc-stage.elf' if disc else 'stage.elf'
    names=(('native-disc-vmu.kui','native-disc-vmu-a1.kui') if disc else
        ('retail-smash-menu.kui',) if role=='smash_outer' else
        ('retail-smash-native-menu.kui','retail-maple-observe.kui') if role=='smash_child' else
        ('retail-native-background-compact.kui','retail-native-dual.kui','retail-maple-observe.kui')
            if role.startswith('background_') else
        ('retail-native-vmu-lite-paced.kui','retail-native-vmu-lite.kui','retail-native-vmu-only.kui','retail-maple-observe.kui'))
    present=[build/name for name in names if (build/name).is_file()]
    require(present,'Missing expected module in '+str(build))
    provenance=receipt(build/entry,commit,allow_dirty)
    data=present[0].read_bytes();info=verify(data);payload,memory=flatten_elf((build/entry).read_bytes())
    require(info['build']==commit[:12] and data[64:]==payload and info['memory_bytes']==memory,
            'Module differs from current entry/provenance: '+str(present[0]))
    require(all(path.read_bytes()==data for path in present),'Mixed/stale module aliases in '+str(build))
    stage_image=read_elf(build/stage);stage_bytes=(build/Path(stage).with_suffix('.bin')).read_bytes()
    require(payload[0x2000:]==stage_bytes,'Entry embeds a different stage: '+role)
    stage_payload=inspect_elf(stage_image[0],0x8ce00000,0x8cfe0000)['payload']
    require(stage_payload==stage_bytes,'Stage binary differs from linked ELF: '+role)
    artifacts={entry:sha((build/entry).read_bytes()),stage:sha(stage_image[0]),
        str(Path(stage).with_suffix('.bin')):sha(stage_bytes),
        **{path.name:sha(path.read_bytes()) for path in present}}
    for name in ('native-worker.elf','native-worker.bin','native-worker.reloc','resident-sci.elf',
                 'resident-sci.bin','resident-legacy.elf','handoff.elf','handoff.bin'):
        if (build/name).is_file():artifacts[name]=sha((build/name).read_bytes())
    background=None
    if role.startswith('background_'):
        background=compact_background_binding(stage_image,read_elf(build/'native-worker.elf'),
            read_elf(build/'resident-sci.elf'),read_elf(build/'resident-legacy.elf'),
            read_elf(build/'native-ra-aux.elf'),1 if role.endswith('_a1') else 2)
    if role!='smash_outer':
        graph=('disc/src/loader/disc_vmu_stage.ci' if disc else
               'stage/src/loader/vmu_native_heap_probe_stage.ci')
        artifacts[graph]=sha((build/graph).read_bytes())
        worker=read_elf(build/'native-worker.elf')
        slot=1 if role.endswith('_a1') or role=='smash_child' else 2
        stage_slot='__kui_disc_vmu_virtual_slot' if disc else '__kui_native_virtual_slot_stage'
        worker_slot='__kui_native_physical_disc_virtual_slot' if disc else '__kui_native_virtual_slot_worker'
        require(stage_image[4].get(stage_slot)==worker[4].get(worker_slot)==(slot,0,0,0xfff1),
                'Cohort stage/worker virtual-card slot mismatch: '+role)
        require(not any(name in worker[4] for name in ('__kui_native_cached_resolution','__kui_native_cached_extent')),
                'Temporary proof cache scope leaked into the active VMU worker: '+role)
    return stage_image,dict(module_sha256=sha(data),module_bytes=len(data),metadata=info,
        source=provenance,artifacts_sha256=artifacts,background=background)


def named_records(image):
    result=[]
    for table in image[3]:
        if table[1]!=2:continue
        strings=image[3][table[6]];names=region(image[0],strings[4],strings[5],'names')
        for at in range(table[4],table[4]+table[5],SYM.size):
            n,address,size,info,_,owner=SYM.unpack_from(image[0],at)
            if not n:continue
            name=names[n:].split(b'\0',1)[0].decode('ascii')
            if size and owner<len(image[3]):result.append((name,address,size,info&15,owner))
    return result


def disc_fast_crc(path):
    """Execute every retained const-propagated zero-seed byte CRC instance."""
    from test_smash_native_fast_crc import table_bytes
    from test_native_standalone_foreground_gin import Gin
    image=read_elf(path);records=named_records(image);expected=table_bytes();tables={}
    for name,address,size,kind,owner in records:
        section=image[3][owner]
        if kind==1 and size==1024 and section[1]==1:
            body=region(image[0],section[4]+address-section[3],size,name)
            if body==expected:tables[address]=body
    helpers=[]
    for name,address,size,kind,owner in records:
        if not name.startswith('_kui_stage_crc32') or kind!=2:continue
        require(name=='_kui_stage_crc32.constprop.0','Unreviewed physical CRC helper ABI: '+name)
        section=image[3][owner];body=region(image[0],section[4]+address-section[3],size,name)
        linked=[table for table in tables if body.count(struct.pack('<I',table))==1]
        require(len(linked)==1,'Physical fast CRC lacks its complete byte table')
        helpers.append((address,body,linked[0]))
    require(helpers,'Physical stage lost its linked fast CRC helpers')
    class CRC(Gin):
        def __init__(self):super().__init__(b'',b'',b'');self.lookups=self.inputs=0
        def read(self,address,count=4):
            if any(table<=address<table+1024 for table in tables):
                require(count==4 and not address&3,'CRC table access differs');self.lookups+=1
            if 0x8cc10000<=address<0x8cc14000:
                require(count==1,'CRC no longer reads byte-exact input');self.inputs+=1
            return super().read(address,count)
    cpu=CRC()
    for segment in image[2]:
        if segment[0]==1:cpu.store_block(segment[2],image[0][segment[1]:segment[1]+segment[4]])
    data=bytes((i*73+19)&255 for i in range(8192));cpu.store_block(0x8cc10000,data)
    results=[]
    for address,body,table in helpers:
        cases=0
        for offset in range(4):
            for size in (0,1,2,3,4,5,7,8,15,16,31,32,255,256,257,4097):
                cpu.r=[0x53530000+i*0x12345 for i in range(16)]
                cpu.r[4:6]=[0x8cc10000+offset,size];cpu.r[15]=0x8c00d000
                before=cpu.r[8:].copy();cpu.pc=address;cpu.pr=0x8c003000
                cpu.writes=[];cpu.lookups=cpu.inputs=0
                for _ in range(size*24+200):
                    if cpu.pc==0x8c003000:break
                    cpu.step()
                require(cpu.pc==0x8c003000 and cpu.r[0]==zlib.crc32(data[offset:offset+size]) and
                        cpu.r[8:]==before and cpu.lookups==cpu.inputs==size,
                        'Physical linked byte CRC differs from zlib/one-lookup/ABI contract')
                require(all(0x8c00cfc0<=p<p+n<=0x8c00d000 for _,p,_,n in cpu.writes) and
                        not cpu.mmio_reads and not cpu.mmio_writes,'Physical CRC changed non-stack state')
                cases+=1
        results.append(dict(entry=hex(address),bytes=len(body),sha256=sha(body),table=hex(table),actual_SH_cases=cases))
    callers=[]
    for name,address,size,kind,owner in records:
        if name not in ('_kui_disc_vmu_stage_relay','_fingerprint') or kind!=2:continue
        section=image[3][owner];body=region(image[0],section[4]+address-section[3],size,name)
        bound=[hex(entry) for entry,_,_ in helpers if struct.pack('<I',entry) in body]
        if bound:callers.append(dict(name=name,address=hex(address),helpers=bound,sha256=sha(body)))
    require({row['name'] for row in callers}=={'_kui_disc_vmu_stage_relay','_fingerprint'},
            'Physical full-file/packet fingerprint paths bypass the tested fast CRC')
    return dict(status='PASS',elf_sha256=sha(image[0]),helpers=results,source_callers=callers,
        one_table_lookup_per_byte=True,oracle='zlib.crc32',hardware_timing_verified=False)


def history(path):
    if path is None:
        from test_vmu_history_performance import audit as run
        return dict(run(full=True),model_reexecuted=True)
    path=Path(path);report=json.loads(path.read_text())
    require(report.get('ok') is True and report.get('full_real_fatfs_regressions_executed') is True and
            len(report.get('source_sha256',{}))>=11 and
            all((ROOT/name).is_file() and sha((ROOT/name).read_bytes())==digest
                for name,digest in report['source_sha256'].items()),'Stale/incomplete common history evidence')
    return dict(report,model_reexecuted=False,checked_report_sha256=sha(path.read_bytes()))


def checksums(path):
    if path is None:
        from test_games_retail_checksum_batch import audit as run
        report=dict(run(full=True),model_reexecuted=True)
    else:
        path=Path(path);report=json.loads(path.read_text())
        require(report.get('status')=='PASS' and report.get('source_sha256') and
                all((ROOT/name).is_file() and sha((ROOT/name).read_bytes())==digest
                    for name,digest in report['source_sha256'].items()),'Stale common launcher-checksum evidence')
        report=dict(report,model_reexecuted=False,checked_report_sha256=sha(path.read_bytes()))
    real=report.get('real_fatfs',{})
    require(real.get('status')=='PASS' and real.get('production_checksum_crc_and_fault_fixtures') and
            real.get('ordinary_ce_raw_cooked_and_async') and real.get('cases',0)>=50,
            'Common launcher checksum gate requires complete actual FatFs/native/CE regressions')
    require(real.get('source_sha256') and all((ROOT/name).is_file() and sha((ROOT/name).read_bytes())==digest
            for name,digest in real['source_sha256'].items()),'Stale actual FatFs launcher dependencies')
    return report


def selected_scope_calls(image,build,*,disc=False,smash=False):
    """Bind selected C callers to the tested wrappers, not unused assembly."""
    from check_retail_stack import load,base
    graph=build/('disc/src/loader/disc_vmu_stage.ci' if disc else
                 'stage/src/loader/vmu_native_heap_probe_stage.ci')
    require(graph.is_file(),'Missing selected stage callgraph')
    _,calls=load([graph],qualified=True)
    def targets(name):
        owners=[owner for owner in calls if base(owner)==name]
        require(len(owners)==1,'Missing unique selected stage callgraph owner: '+name)
        return {base(target) for target,_ in calls[owners[0]]}
    wrapper=('_kui_smash_native_menu_cached_resolution_replay' if smash else
             '_kui_vmu_native_cached_resolution_replay')
    bindings={'_native_resolution_replay':(wrapper,'_kui_vmu_native_resolution_cache_replay'),
        '_kui_disc_vmu_stage_relay' if disc else '_kui_retail_stage_relay':
            ('_kui_vmu_native_cached_extent','_kui_vmu_native_program_extent')}
    rows=[]
    for caller,(selected,uncached) in bindings.items():
        edges=targets(caller[1:]);address,body,_=symbol_bytes(image,caller,2)
        require(selected[1:] in edges and uncached[1:] not in edges and
                body.count(struct.pack('<I',image[4][selected][0]))==1,
                'Live stage caller bypasses its tested cache scope: '+caller)
        rows.append(dict(caller=caller,address=hex(address),sha256=sha(body),selected=selected,
                         selected_edges=sorted(edges)))
    if smash:
        require('kui_vmu_native_cached_resolution_replay' not in targets('native_resolution_replay') and
                {'kui_vmu_native_cache_seed_read','kui_vmu_native_resolution_cache_replay'}<=
                targets('kui_smash_native_menu_resolution_replay'),
                'Smash dedicated scope no longer wraps the original seed/replay proof')
    return dict(status='PASS',bindings=rows,callgraph_sha256=sha(graph.read_bytes()))


def smash_replay_route(image,build):
    """Execute the selected no-argument route, rather than an unused wrapper."""
    from check_vmu_native_stage_stack import Machine
    wrapper='_kui_smash_native_menu_cached_resolution_replay'
    require(image[4].get('__kui_smash_native_menu_cached_resolution')==ONE and
            '__smash_native_menu_seed' in image[4],
            'Optional Smash child lacks its dedicated menu replay capability')
    selected=selected_scope_calls(image,build,smash=True)
    target,body,_=symbol_bytes(image,wrapper,2)
    class FirstCall(Machine):
        def call(self,dest):
            require(dest==target,'Smash replay first call does not select its tested cache scope')
            require(self.r[15]==self.start_sp-16,'Smash replay selected-call ABI frame changed')
            self.calls.append(dest);self.pc=self.stop_pc;return True
    for seed in range(8):
        for mask in (0,7,15):
            cpu=FirstCall(image,'_native_resolution_replay',seed,mask,0x8cff0000).run()
            require(cpu.calls==[target],'Smash replay did not invoke its selected cache scope once')
    return dict(status='PASS',actual_first_call_cases=24,wrapper=wrapper,
        wrapper_sha256=sha(body),selected_calls=selected,
        no_argument_abi=True,full_smash_architecture_audit_remains_required=True)


def audit(builds,runtime_elf,*,allow_dirty=False,history_report=None,checksum_report=None,focused=False):
    require(__debug__,'Run linked models without Python optimization')
    require(REQUIRED<=set(builds) and set(builds)<=REQUIRED|OPTIONAL,'Missing/unknown shared cohort roles')
    require(('smash_outer' in builds)==('smash_child' in builds),'Smash cohort must supply its matching pair')
    commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    if not allow_dirty:require(not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT),'Final shared audit requires clean source')
    runtime_elf=Path(runtime_elf).resolve();runtime=receipt(runtime_elf,commit,allow_dirty)
    runtime_image=read_elf(runtime_elf,allow_tls=True)
    runtime_payload,runtime_memory=flatten_elf(runtime_image[0])
    runtime['module_sha256']=sha(envelope(runtime_payload,runtime_memory,commit[:12]))
    runtime['shared_functions']={}
    for name in ('_kui_game_image_read','_kui_vmu_history_apply','_kui_vmu_history_session_begin'):
        address,body,_=symbol_bytes(runtime_image,name,2)
        runtime['shared_functions'][name]=dict(address=hex(address),bytes=len(body),sha256=sha(body))
    from test_game_image_batch import audit as batch
    from test_native_stage_proof_scope import audit as scope
    from check_vmu_native_cache_gateway import check_extent_scope
    from test_smash_native_fat_cache_linked import audit as fat
    from test_native_fat_window_linked import audit as window
    from test_smash_native_fast_crc import audit as crc
    from test_native_performance_defaults import audit as defaults
    from test_native_standard_manifest import audit as standard_manifest
    from test_native_background_route import audit as background_route
    standard=standard_manifest(builds['standard'],allow_dirty=allow_dirty)
    runtime['standard_manifest_binding']=runtime_standard_manifest_binding(
        runtime_image,standard['manifest']['normalized_payload_sha256'])
    runtime['standard_manifest']=standard
    cohort={}
    for role,folder in sorted(builds.items()):
        build=Path(folder).resolve();image,binding=module(build,role,commit,allow_dirty)
        if role=='smash_outer':
            cohort[role]=dict(binding,performance_scope='Outer menu binding; paired functional audit proves grouped capsule delivery')
            continue
        disc=role.startswith('disc_');elf=build/('disc-stage.elf' if disc else 'stage.elf')
        symbols=image[4]
        if role=='smash_child':
            from test_smash_native_cached_replay import audit as smash_scope
            route=smash_replay_route(image,build);replay=smash_scope(elf)
            require(replay['actual_instruction_model_cases']==3840 and replay['mutation_rejections']==21,
                    'Incomplete selected Smash no-argument cache-scope baseline')
            extent=check_extent_scope(image,True)
            require(extent['enabled'] and extent['actual_instruction_model_cases']==6144 and
                    extent['mutation_rejections']==20,
                    'Optional Smash child lost the inherited common cached-extent baseline')
            require(symbols.get('__kui_smash_native_fat_read_cache')==ONE,
                    'Smash child lost its fresh FAT cache')
        else:
            require(all(symbols.get(name)==ONE for name in ('__kui_native_cached_resolution','__kui_native_cached_extent',
                    '__kui_native_fat_read_cache')),'Shared optimization marker missing: '+role)
            route=selected_scope_calls(image,build,disc=disc);replay=scope(elf);extent=check_extent_scope(image,True)
            require(replay['actual_SH_cases']==3840 and replay['mutation_rejections']==26 and
                    extent['actual_instruction_model_cases']==6144 and extent['mutation_rejections']==20,
                    'Incomplete actual cache-scope performance baseline: '+role)
        cohort[role]=dict(binding,pure_resolution=replay,pure_extent=extent,
            fast_crc=disc_fast_crc(elf) if disc else crc(elf),
            fresh_fat_cache=fat(elf,'smash' if role=='smash_child' else 'disc' if disc else 'native'),
            verified_fat_window=window(elf,'disc' if disc else 'native',0 if role=='standard_scif' else 1))
        if route is not None:cohort[role]['selected_resolution_route']=route
        stage_sha=binding['artifacts_sha256']['disc-stage.elf' if disc else 'stage.elf']
        require(all(cohort[role][key].get('elf_sha256')==stage_sha for key in
                ('pure_resolution','fast_crc','fresh_fat_cache','verified_fat_window')),
                'Component models did not execute the same current stage: '+role)
    algorithms=dict(image_batch=batch(),launcher_checksums=checksums(checksum_report),
                    history=history(history_report),effective_family_defaults=defaults(),
                    background_reader_routing=background_route())
    require(algorithms['image_batch']['status']=='PASS' and algorithms['history'].get('ok') is True and
            algorithms['history'].get('full_real_fatfs_regressions_executed') is True and
            algorithms['launcher_checksums'].get('status')=='PASS' and
            algorithms['effective_family_defaults'].get('status')=='PASS',
            'Shared filesystem/history performance algorithms did not pass')
    require(algorithms['background_reader_routing'].get('status')=='PASS',
            'Compact New/Legacy capability routing did not pass')
    require(subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()==commit,
            'Source commit changed during shared audit')
    require(sha(runtime_elf.read_bytes())==runtime['elf_sha256'],'Runtime changed during shared audit')
    for role,report in cohort.items():
        require(all(sha((Path(builds[role])/name).read_bytes())==digest
                    for name,digest in report['artifacts_sha256'].items()),
                'Cohort artifacts changed during shared audit: '+role)
    if not allow_dirty:require(not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT),'Source changed during final shared audit')
    return dict(schema='kui.shared-loader.performance.v2',status='PASS',commit=commit,
        allow_dirty=allow_dirty,runtime=runtime,cohort=cohort,shared_algorithms=algorithms,
        hardware_verified=False,console_speedup_measured=False,per_game_constants=False,
        qualification_mode='focused-next-test' if focused else 'historical-family-audits',
        safety_audits_remain_mandatory=not focused,
        unchanged_runtime_reference_qualification_required=focused,
        proof_python_sha256={str(Path(m.__file__).resolve().relative_to(ROOT)):sha(Path(m.__file__).read_bytes())
            for m in tuple(sys.modules.values()) if getattr(m,'__file__',None) and
            Path(m.__file__).suffix=='.py' and Path(m.__file__).resolve().is_relative_to(ROOT)},
        limits=[('This focused gate executes changed shared-stage models and exact bindings; separate unchanged-runtime reference qualification is required.' if focused else
                 'This gate establishes shared optimizations and exact cohort bindings; existing full safety/original-SDK audits remain required.'),
                'Clean compile provenance is trusted to bind the runtime C algorithms; linked stage cache/FAT/CRC instructions execute in bounded models.',
                'Canonical map labels are not commercial-game compatibility or console timing results.'])


def main():
    p=argparse.ArgumentParser(description=__doc__)
    for role in sorted(REQUIRED|OPTIONAL):p.add_argument('--'+role.replace('_','-'),type=Path,required=role in REQUIRED)
    p.add_argument('--runtime-elf',type=Path,required=True);p.add_argument('--allow-dirty',action='store_true')
    p.add_argument('--focused',action='store_true')
    p.add_argument('--history-report',type=Path);p.add_argument('--checksum-report',type=Path)
    p.add_argument('--output',type=Path);a=p.parse_args()
    builds={role:getattr(a,role) for role in REQUIRED|OPTIONAL if getattr(a,role) is not None}
    report=audit(builds,a.runtime_elf,allow_dirty=a.allow_dirty,history_report=a.history_report,
                 checksum_report=a.checksum_report,focused=a.focused)
    text=json.dumps(report,indent=2)+'\n'
    if a.output:a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(text)
    print(text,end='')


if __name__=='__main__':main()
