#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the VMU wizard GUI and audited zero-VMU physical Standard reader."""
import argparse
import csv
import io
import json
import os
import re
from pathlib import Path
import subprocess
import tempfile
import zipfile

from install_vmu_wizard import SCHEMA, MAX_BYTES, TARGETS, NATIVE_MODULES, PHYSICAL_DESCRIPTOR
from package_native_vmu_only import checkout_script
from package_vmu_native_dual_delivery import offline_source_recovery_check
from runtime_package import flatten_elf, verify

ROOT = Path(__file__).resolve().parents[1]


def sha(data):
    import hashlib
    return hashlib.sha256(data).hexdigest()


def wire(value):
    return (json.dumps(value,indent=2,sort_keys=True)+'\n').encode()


def require(value, message):
    if not value:
        raise ValueError(message)


def git(*arguments):
    return subprocess.check_output(['git','-C',str(ROOT),*arguments],text=True).strip()


def clean_source():
    require(not git('status','--porcelain','--untracked-files=all'), 'Commit source changes before packaging')
    return git('rev-parse','HEAD'),git('rev-parse','HEAD^{tree}')


def ordinary_bytes(path):
    path=Path(path)
    require(path.is_file() and not path.is_symlink(),'Missing ordinary artifact: '+str(path))
    return path.read_bytes()


def gui_artifacts(gui,commit,elf=None,map_file=None,provenance=None):
    gui=Path(gui)
    files={'runtime.kui':ordinary_bytes(gui),
           'kui-runtime.elf':ordinary_bytes(elf or gui.parent/'kui-runtime.elf'),
           'kui-runtime.map':ordinary_bytes(map_file or gui.parent/'kui-runtime.map'),
           'kui-runtime.compile.json':ordinary_bytes(provenance or gui.parent/'kui-runtime.compile.json')}
    proof=json.loads(files['kui-runtime.compile.json'])
    require(proof == dict(commit=commit,source_dirty=False,elf_sha256=sha(files['kui-runtime.elf'])),
            'GUI linked provenance differs from this exact clean source')
    info=verify(files['runtime.kui']);payload,memory=flatten_elf(files['kui-runtime.elf'])
    require(info['build']==commit[:12] and files['runtime.kui'][64:]==payload and info['memory_bytes']==memory,
            'GUI runtime differs from its exact linked ELF/source checkpoint')
    require(files['kui-runtime.map'],'GUI map is empty')
    return files


def package_metadata(runtime,physical,commit):
    require(isinstance(commit,str) and re.fullmatch('[0-9a-f]{40}',commit) and
        all(len(data)<=MAX_BYTES and verify(data)['build']==commit[:12] for data in (runtime,physical)),
        'GUI/physical build identifier differs from committed source')
    at=physical.find(PHYSICAL_DESCRIPTOR,64)
    require(at>=64 and not at%4 and physical.find(PHYSICAL_DESCRIPTOR,at+1)==-1,
            'Physical reader lacks its unique exact zero-VMU descriptor')
    return dict(schema=SCHEMA,build=commit[:12],source_commit=commit,
        runtime_sha256=sha(runtime),physical_sha256=sha(physical),
        replaces=[str(path) for path in TARGETS.values()],
        preserved_native_modules=[str(path) for path in NATIVE_MODULES],console_compatibility_verified=False)


def validate_resident_stacks(stacks):
    from check_retail_loader_layout import ASSEMBLY_STACK_BYTES
    from check_retail_stack import MARGIN
    require(isinstance(stacks,dict) and set(stacks)=={'scif','sci','ide','scia'},
            'Physical Standard requires all four resident stack proofs')
    for name in ('scif','sci','ide'):
        row=stacks[name]
        require(isinstance(row,dict) and set(row)=={'conservative_bytes','available_bytes',
            'assembly_allowance','retained_c_frames'} and all(type(value) is int for value in row.values()) and
            row['assembly_allowance']==ASSEMBLY_STACK_BYTES and row['retained_c_frames']>0 and
            0<row['conservative_bytes']<=row['available_bytes'] and
            row['conservative_bytes']>=row['assembly_allowance'],
            'Physical conservative resident stack proof differs: '+name)
    row=stacks['scia']
    require(isinstance(row,dict) and set(row)=={'worst_bytes','available_bytes','margin','call_graph'} and
        all(type(row[key]) is int for key in ('worst_bytes','available_bytes','margin')) and
        row['call_graph'] is True and row['margin']==MARGIN and row['worst_bytes']>0 and
        row['worst_bytes']+row['margin']<=row['available_bytes'],
        'Physical asynchronous resident stack/callgraph proof differs')


def validate_physical(report,commit,module):
    from check_retail_physical import GUARD
    from retail_package import RAM_PATCH_SEED
    descriptor=report.get('descriptor',{})
    at=descriptor.get('module_offset')
    require(report.get('schema')=='kui.native.physical-standard.audit.v1' and
        report.get('source_provenance',{}).get('commit')==commit and
        report['source_provenance'].get('source_dirty') is False and
        report.get('runtime',{}).get('build')==commit[:12] and report.get('module_sha256')==sha(module) and
        report.get('reader')==0 and report.get('transport_mask')==7 and report.get('lease_bytes')==0 and
        report.get('worker_bytes')==0 and report.get('virtual_cards')==0 and
        report.get('background_reader_enabled') is False and report.get('prefetch_enabled') is False and
        report.get('console_execution_verified') is False and
        report.get('existing_plain_resident_bytes_identical') is True and
        descriptor.get('bytes')==32 and descriptor.get('allocated_read_only') is True and
        descriptor.get('unique_complete_descriptor') is True and type(at) is int and at>=64 and not at%4 and
        module[at:at+32]==PHYSICAL_DESCRIPTOR and module.find(PHYSICAL_DESCRIPTOR)==at and
        module.find(PHYSICAL_DESCRIPTOR,at+1)==-1,
        'Physical Standard audit/source/zero-VMU descriptor binding differs')
    images={'resident-scif','resident-sci','resident-ide','resident-scia','stage','entry'}
    layout=report.get('layout',{});instructions=report.get('instruction_audit',{})
    stacks=layout.get('resident_stacks',{})
    validate_resident_stacks(stacks)
    require(report.get('standard_only_runtime_guard') is True and images<=set(layout) and
        all(type(layout[name].get('payload_bytes')) is int and layout[name]['payload_bytes']>0 and
            layout[name].get('unresolved_symbols')==0 for name in images) and
        set(instructions)==images and all(type(value) is int and value>0 for value in instructions.values()) and
        type(report.get('compiler_callgraphs',{}).get('complete_files')) is int and
        report['compiler_callgraphs']['complete_files']>0 and type(report.get('mutation_rejections')) is int and
        report['mutation_rejections']>=34,
        'Physical Standard requires complete instruction/layout/stack/descriptor witnesses')
    guard=report.get('standard_only_guard',{})
    require(guard.get('exact_immutable_body') is True and guard.get('body_bytes')==len(GUARD) and
        guard.get('body_sha256')==sha(GUARD) and guard.get('actual_stage_call_bound') is True and
        guard.get('actual_stage_result_checked') is True and guard.get('instruction_cases')==81 and
        guard.get('reader')==0 and guard.get('transport_mask')==7,
        'Physical Standard actual admission guard witness differs')
    patches=report.get('ram_patch_seed',{});at=patches.get('module_offset')
    require(report.get('ram_patch_seed_blank') is True and patches.get('bytes')==64 and
        patches.get('initialized_data') is True and patches.get('blank') is True and type(at) is int and
        at>=64 and not at%4 and module[at:at+64]==RAM_PATCH_SEED and module.count(RAM_PATCH_SEED)==1,
        'Physical ordinary RAM patch seed differs from its typed blank data')


def physical_artifacts(build,commit):
    from check_retail_physical import ARTIFACTS,audit
    build=Path(build)
    report=audit(build,allow_dirty=False,self_test=True)
    files={name:ordinary_bytes(build/name) for name in ARTIFACTS}
    validate_physical(report,commit,files['retail-physical.kui'])
    require(report.get('artifact_sha256')=={name:sha(data) for name,data in files.items()},
            'Physical artifacts changed after the strict linked audit')
    for pattern in ('*.ci','*.su'):
        for path in sorted(build.rglob(pattern)):files[path.relative_to(build).as_posix()]=ordinary_bytes(path)
    files['physical-standard-audit.json']=wire(report)
    return files,report


def source_guide(commit):
    return f'''# Exact wizard and physical Standard source checkpoint

The GUI and physical Standard reader use clean source commit `{commit}`. The full Git bundle
contains repository history with no prerequisites. Recover it offline from the
extracted package folder:

```sh
python3 source/checkout-source.py
```

The helper verifies the bundle hash, exact source tree and clean detached
checkout. With the existing KallistiOS and SH-4 dependencies configured, rebuild
the GUI using `Makefile.dc`. The exact GUI ELF, map, runtime envelope and clean
compile provenance are under `artifacts/gui/`. Targeted host reports are under
`evidence/` and their exact hashes are recorded in `CANDIDATE.json`.

Build the separate physical Standard entry serially with the same configured
toolchain and KallistiOS environment:

```sh
make -C restored-source -f Makefile.dc -j1 retail-physical
```

Its ELF files, maps, compile provenance, emitted callgraph/stack files and strict
linked audit are under `artifacts/physical/`. This plain reader has zero virtual
cards and no VMU worker/lease; it serves Standard with Virtual VMU Off. Existing
Background72, standalone VMU reader and reader-cache modules are preserved.
Their earlier source/build/audit packages remain applicable. No new console
compatibility or VMU-worker execution result is claimed.
'''.encode()


def test_sheet():
    stream=io.StringIO(newline='');writer=csv.writer(stream)
    writer.writerow(('check','game_product','source_or_version','new_pair','result','notes'))
    for name in ('Import source preserved','Two games have distinct 200-block cards',
                 'Source versions stay separate','Launch version choice/default',
                 'Save reboot and load','Unselected assignments preserved','Both Off uses physical Standard','Exact GUI/physical restore'):
        writer.writerow((name,'','','','',''))
    return stream.getvalue().encode()


def package(gui,output,readme=None,evidence=(),elf=None,map_file=None,provenance=None,physical_build=None):
    require(evidence,'Supply targeted host evidence for the wizard/physical-reader change')
    commit,tree=clean_source();build=commit[:12]
    artifacts=gui_artifacts(gui,commit,elf,map_file,provenance)
    physical_files,physical_report=physical_artifacts(physical_build or ROOT/'build/retail-physical',commit)
    metadata=package_metadata(artifacts['runtime.kui'],physical_files['retail-physical.kui'],commit)
    name='K-UI-VMU-Import-Wizard-'+build
    output=Path(output).absolute()
    archive=output if output.suffix=='.zip' else output/(name+'.zip')
    require(not archive.resolve().is_relative_to(ROOT.resolve()),'Save delivery outside the source checkout')
    require(not archive.exists() and not archive.is_symlink(),'Output already exists: '+str(archive))
    files={'gui/runtime.kui':artifacts['runtime.kui'],
           'physical/retail-physical.kui':physical_files['retail-physical.kui'],
           'install.py':ordinary_bytes(ROOT/'tools/install_vmu_wizard.py'),
           'README-FIRST.md':ordinary_bytes(readme or ROOT/'docs/vmu-import-wizard-test.md').replace(b'@BUILD@',build.encode()),
           'LICENSE':ordinary_bytes(ROOT/'LICENSE'),'THIRD_PARTY.md':ordinary_bytes(ROOT/'THIRD_PARTY.md'),
           'wizard.json':wire(metadata),'SOURCE.md':source_guide(commit),'test-results.csv':test_sheet()}
    for artifact,data in artifacts.items():files['artifacts/gui/'+artifact]=data
    for artifact,data in physical_files.items():files['artifacts/physical/'+artifact]=data
    evidence_hashes={}
    for path in evidence:
        path=Path(path);target='evidence/'+path.name
        require(target not in files,'Duplicate evidence filename: '+path.name)
        data=ordinary_bytes(path);files[target]=data;evidence_hashes[target]=sha(data)
    candidate=dict(schema='kui.vmu.wizard.candidate.v3',source_commit=commit,source_tree=tree,build=build,
        gui_sha256=metadata['runtime_sha256'],physical_sha256=metadata['physical_sha256'],
        updated_paths=metadata['replaces'],existing_game_modules_preserved=True,vmu_worker_updated=False,
        card_files_updated_by_installer=False,console_compatibility_verified=False,
        existing_modules_accepted_when_valid=True,missing_native_modules_accepted=True,
        compatibility_reference_builds=['34dc7b5e15ee','1734830d08df'],
        gui_artifact_sha256={key:sha(data) for key,data in artifacts.items()},
        physical_artifact_sha256={key:sha(data) for key,data in physical_files.items()},
        physical_standard_audit_sha256=sha(wire(physical_report)),evidence_sha256=evidence_hashes)
    with tempfile.TemporaryDirectory(prefix='kui-vmu-wizard-source-') as temporary:
        bundle=Path(temporary)/'native-source-full.bundle'
        subprocess.run(['git','-C',str(ROOT),'bundle','create',str(bundle),'HEAD','--all'],check=True,capture_output=True)
        subprocess.run(['git','-C',str(ROOT),'bundle','verify',str(bundle)],check=True,capture_output=True)
        full=bundle.read_bytes()
    candidate['source_full_bundle']=dict(path='source/native-source-full.bundle',bytes=len(full),
        sha256=sha(full),full_history=True,prerequisites=[])
    checkout=checkout_script().replace(b'Verified offline VMU-only source',b'Verified offline GUI source')
    candidate['offline_source_recovery']=offline_source_recovery_check(candidate,checkout,full)
    require(clean_source()==(commit,tree),'Source changed while packaging')
    files['source/native-source-full.bundle']=full;files['source/checkout-source.py']=checkout
    files['CANDIDATE.json']=wire(candidate)
    files['SHA256SUMS.txt']=''.join(sha(data)+'  '+key+'\n' for key,data in sorted(files.items())).encode()
    archive.parent.mkdir(parents=True,exist_ok=True)
    fd,path=tempfile.mkstemp(prefix='.vmu-wizard-',suffix='.zip',dir=archive.parent);os.close(fd)
    temporary=Path(path)
    try:
        with zipfile.ZipFile(temporary,'w',zipfile.ZIP_DEFLATED) as target:
            for key,data in sorted(files.items()):target.writestr(name+'/'+key,data)
        with zipfile.ZipFile(temporary) as target:
            require(len(target.namelist())==len(files) and set(target.namelist())=={name+'/'+key for key in files} and
                all(target.read(name+'/'+key)==data for key,data in files.items()),'ZIP byte round-trip differs')
        require(temporary.stat().st_size<50*1024*1024,'Package exceeds the 50 MiB delivery limit')
        os.replace(temporary,archive)
    finally:temporary.unlink(missing_ok=True)
    result=dict(package=str(archive),bytes=archive.stat().st_size,sha256=sha(archive.read_bytes()),**metadata)
    print(json.dumps(result,indent=2));return result


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--gui',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--readme',type=Path)
    parser.add_argument('--elf',type=Path)
    parser.add_argument('--map',dest='map_file',type=Path)
    parser.add_argument('--provenance',type=Path)
    parser.add_argument('--physical-build',type=Path,default=ROOT/'build/retail-physical')
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    args=parser.parse_args()
    try:package(args.gui,args.output,args.readme,args.evidence,args.elf,args.map_file,args.provenance,args.physical_build)
    except (OSError,ValueError,AssertionError,KeyError,TypeError,subprocess.CalledProcessError) as error:
        parser.exit(2,'VMU wizard GUI packaging refused: '+str(error)+'\n')


if __name__=='__main__':main()
