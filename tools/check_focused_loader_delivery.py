#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind the focused next test without claiming a historical-suite rerun.

The old complete reports authenticate unchanged gameplay components. Current
entry/stage embeddings, source receipts and relocation tables are checked here;
changed cold-stage algorithms require their separate current linked models.
"""
import json
from pathlib import Path
import zipfile

from check_loader_layout import inspect_elf
from check_native_compact_delivery import component_equivalence
from check_native_background_compact import complete_callgraphs
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from native_runtime_layout import layout_for
from package_native_shutdown_test import immutable_blob
from package_vc2_ce_vmu_late_test import installed_module,sha
from runtime_package import flatten_elf
from vmu_native_relocations import generate

BASELINE='945adcf880297b619c5189cd452f25ea66e8332c'
BASELINE_ZIP_SHA256='0d2df7558fe88dbb6a58991e710c2b7a43bbf76404bacb4db686d75d184dd923'
COMPONENTS={
    'standard':('native-worker.elf','resident-sci.elf'),
    'disc_a1':('native-worker.elf','support/support.elf'),
    'disc_a2':('native-worker.elf','support/support.elf'),
    'smash_outer':('resident-sci.elf','smash-menu-raw.bin'),
    'smash_child':('native-worker.elf','resident-sci.elf','smash-return-raw.bin'),
}


def require(ok,message):
    if not ok:raise ValueError(message)


def baseline_records(archive):
    archive=Path(archive)
    require(sha(archive.read_bytes())==BASELINE_ZIP_SHA256,'Focused baseline archive differs from the delivered945 ZIP')
    with zipfile.ZipFile(archive) as z:
        names=('shared_performance','smash_functional','standard_safety','disc_a1_safety','disc_a2_safety')
        evidence={name:z.read('evidence/'+name+'.json') for name in names}
    reports={name:json.loads(body) for name,body in evidence.items()}
    shared=reports['shared_performance'];smash=reports['smash_functional']
    require(shared['commit']==smash['commit']==BASELINE and shared['status']==smash['status']=='PASS',
            '945 baseline reports do not authenticate the delivered cohort')
    expected={}
    for role,names in COMPONENTS.items():
        expected[role]={}
        for name in names:
            if name.startswith('support/'):
                value=reports[role+'_safety']['artifacts_sha256']['artifacts/disc-vmu/'+name]
            else:
                row=shared['cohort'][role]['artifacts_sha256']
                value=row.get(name)
                if value is None:
                    value=smash['artifacts_sha256'][('outer/' if role=='smash_outer' else 'child/')+name]
            expected[role][name]=value
    return expected,evidence


def compare_components(role,directory,reference,expected,commit):
    directory,reference=Path(directory),Path(reference)
    require(role in COMPONENTS and set(expected)==set(COMPONENTS[role]),'Unknown focused component set')
    entry='disc-entry' if role.startswith('disc_') else 'entry'
    source=json.loads((directory/(entry+'.compile.json')).read_text())
    require(source.get('commit')==commit and source.get('source_dirty') is False and
            source.get('elf_sha256')==sha((directory/(entry+'.elf')).read_bytes()),
            'Current entry lacks its clean matching source receipt')
    reports={};artifacts={}
    for name,digest in expected.items():
        previous=(reference/role/name).read_bytes()
        require(sha(previous)==digest,'Reference component differs from945 complete report: '+role+'/'+name)
        candidate=(directory/name).read_bytes()
        if name.endswith('.elf'):
            receipt=(directory/name).with_suffix('.compile.json')
            if receipt.is_file():
                record=json.loads(receipt.read_text())
                require(record.get('commit')==commit and record.get('source_dirty') is False and
                        record.get('elf_sha256')==sha(candidate),'Current component has conflicting provenance: '+name)
            proof=component_equivalence(read_elf(directory/name),read_elf(reference/role/name),
                reference_sha256=digest,old_build=BASELINE[:12],new_build=commit[:12])
        else:
            require(candidate==previous,'An unchanged raw runtime component changed: '+name)
            proof=dict(reference_sha256=digest,candidate_sha256=sha(candidate),all_bytes_identical=True)
        reports[name]=proof;artifacts[name]=sha(candidate)
    return dict(schema='kui.focused.unchanged-components.v1',status='PASS',role=role,
        reference_commit=BASELINE,reference_archive_sha256=BASELINE_ZIP_SHA256,
        components=reports,artifacts_sha256=artifacts,entry_source_receipt=source,
        current_embed_chain_required=True,full_historical_suite_reexecuted=False,
        changed_cold_stage_models_required=True,console_execution_verified=False)


def native_embedding(directory,commit,*,disc=False):
    directory=Path(directory)
    stage_name='disc-stage' if disc else 'stage';entry_name='disc-entry' if disc else 'entry'
    stage=read_elf(directory/(stage_name+'.elf'));entry=read_elf(directory/(entry_name+'.elf'))
    stage_bytes=(directory/(stage_name+'.bin')).read_bytes()
    require(inspect_elf(stage[0],0x8ce00000,0x8cfe0000)['payload']==stage_bytes,
            'Current stage binary differs from its ELF')
    payload,_=flatten_elf(entry[0])
    require(payload[0x2000:]==stage_bytes,'Current entry embeds another stage')
    workers=[('native-worker',0x8c100000,0x12000 if disc else
        layout_for(read_elf(directory/'native-worker.elf')[4]).WORKER_BYTES)]
    if disc:workers.append(('support/support',0x8c112000,0x2000))
    for name,link,span in workers:
        image=read_elf(directory/(name+'.elf'));binary=(directory/(name+'.bin')).read_bytes()
        reloc=(directory/(name+'.reloc')).read_bytes()
        require(generate(image[0],binary,link,span)==reloc,'Current worker/ELF/relocation binding changed: '+name)
        prefix='__disc_support' if name.startswith('support/') else '__native_worker'
        immutable_blob(stage,prefix+'_blob_begin',prefix+'_blob_end',binary)
        immutable_blob(stage,prefix+'_reloc_begin',prefix+'_reloc_end',reloc)
    if not disc:
        low=(directory/'resident-sci.bin').read_bytes()
        for transport in ('scif','sci','ide','scia'):
            immutable_blob(stage,'__retail_resident_'+transport+'_blob_start',
                '__retail_resident_'+transport+'_blob_end',low+bytes((-len(low))&3))
    return dict(exact_entry_stage_worker_and_relocation_chain=True,
        current_stage_sha256=sha(stage[0]),all_compiler_callgraphs=complete_callgraphs(directory))


def smash_binding(outer,child,commit):
    """Check current pair ownership and exact embedded unchanged components."""
    from check_smash_menu_loader import (resident,handoff,embedded,unique_blank,
        LOADER_PREFIX,MENU_PREFIX,RETURN_PREFIX,PHASE_PREFIX,VIDEO_PREFIX,PHASE_OUTER_PROFILE,CAPABILITY,FOREGROUND)
    from smash_native_menu_resident import inspect as child_resident
    outer,child=Path(outer),Path(child)
    outer_bytes,_=installed_module(outer/'retail-smash-menu.kui',outer/'entry.elf',commit)
    child_bytes,_=installed_module(child/'retail-smash-native-menu.kui',child/'entry.elf',commit)
    outer_stage=read_elf(outer/'stage.elf');child_stage=read_elf(child/'stage.elf')
    low,raw,_=resident(outer,read_elf(outer/'resident-sci.elf'))
    _,code,_=handoff(outer)
    for data,first,last in ((low,'__retail_resident_sci_blob_start','__retail_resident_sci_blob_end'),
        (raw,'__smash_menu_raw_start','__smash_menu_raw_end'),
        (code,'__smash_menu_handoff_start','__smash_menu_handoff_end')):
        immutable_blob(outer_stage,first,last,data)
    filtered=child_resident((child/'resident-sci.elf').read_bytes())
    require(filtered['low']['payload']==(child/'resident-sci.bin').read_bytes() and
            filtered['raw']==(child/'smash-return-raw.bin').read_bytes(),
            'Native child low/raw extraction differs from its exact ELF')
    _,return_code,_=handoff(child,True)
    for data,first,last in ((filtered['raw'],'__smash_return_raw_start','__smash_return_raw_end'),
        (return_code,'__smash_return_code_start','__smash_return_code_end')):
        immutable_blob(child_stage,first,last,data)
    native_embedding(child,commit)
    loader=unique_blank(outer_bytes[64:],LOADER_PREFIX,64,'outer loader seed')
    menu=unique_blank(child_bytes[64:],MENU_PREFIX,304,'native child selection seed')
    outer_return=unique_blank(outer_bytes[64:],RETURN_PREFIX,368,'outer return seed')
    child_return=unique_blank(child_bytes[64:],RETURN_PREFIX,368,'child return seed')
    require(child_return==menu+304,'Native return seed is not adjacent to its selection')
    require(outer_bytes[64:].count(CAPABILITY)==outer_bytes[64:].count(FOREGROUND)==1,
            'Outer capability is absent or ambiguous')
    for name,expected in (('_kui_smash_menu_loader_capability',CAPABILITY),
                          ('_kui_native_foreground_reader_capability',FOREGROUND)):
        _,actual,owner=symbol_bytes(outer_stage,name,1)
        require(actual==expected and owner[2]&7==2,'Outer typed capability differs: '+name)
    require(PHASE_PREFIX not in child_bytes[64:] and VIDEO_PREFIX not in child_bytes[64:] and
            PHASE_OUTER_PROFILE not in outer_bytes[64:],'Focused play build contains held timing diagnostics')
    require('_kui_smash_menu_loader_phase_trace' not in read_elf(outer/'handoff.elf')[4],
            'Focused play build contains the diagnostic handoff')
    return dict(schema='kui.smash.focused-pair.v1',status='PASS',
        exact_current_pair_and_unchanged_components_bound=True,
        seeds=dict(outer_loader=loader,native_selection=menu,outer_return=outer_return,child_return=child_return),
        timing_diagnostic=dict(enabled=False,status='DISABLED'),
        full_historical_menu_audit_executed=False,original_GIN_reexecution=False,
        artifacts_sha256={'outer/retail-smash-menu.kui':sha(outer_bytes),
                          'child/retail-smash-native-menu.kui':sha(child_bytes)},
        compiler_callgraphs=dict(outer=complete_callgraphs(outer),child=complete_callgraphs(child)),
        console_execution_verified=False)


def cold_performance(outer,child,mde,report=None):
    from check_cold_load_performance import audit,SOURCES
    outer,child=Path(outer),Path(child)
    result=json.loads(Path(report).read_text()) if report else audit(outer,child,mde)
    expected={name+'/'+file:sha((folder/file).read_bytes()) for name,folder,files in (
        ('outer',outer,('stage.elf','entry.elf','handoff.elf','resident-sci.elf')),
        ('child',child,('stage.elf','entry.elf','return-loader.elf','resident-sci.elf','native-worker.elf')))
        for file in files}
    root=Path(__file__).resolve().parents[1]
    require(result.get('schema')=='kui.cold-load-performance.v1' and result.get('status')=='PASS',
            'Focused cold-stage qualification did not pass')
    require(result.get('artifacts_sha256')==expected,'Cold-stage report does not bind every current outer/child component')
    require(result.get('source_sha256')=={name:sha((root/name).read_bytes()) for name in SOURCES},
            'Cold-stage model source dependencies changed')
    require(result.get('no_game_or_worker_RAM_added') is True and
            result.get('every_SD_block_CRC_retained') is True,'Cold-stage report lost its RAM/CRC invariants')
    return dict(result,model_reexecuted=report is None,
        checked_report_sha256=sha(Path(report).read_bytes()) if report else None)
