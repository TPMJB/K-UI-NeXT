#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Package the VC2 virtual-VRAM copy fix, optional probe and previous restore."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_vc2_ce_vmu_package import audit
from check_vc2_stage_cached_relay import audit as cache_audit
from check_vc2_texture_probe import audit_low_entry
from check_vmu_native_worker_gateway import read_elf
from package_vc2_ce_vmu_persist import paired_helper
from package_vc2_ce_vmu_late_test import ROOT, git, installed_module, sha, wire
from runtime_package import verify


def previous_files(directory):
    names=('runtime.kui', 'apps/games/retail-physical.kui',
           'apps/games/vc2-vmu-helper.bin')
    files={name:(directory/name).read_bytes() for name in names}
    runtime=verify(files[names[0]])
    reader=verify(files[names[1]])
    if runtime['build']!=reader['build']:
        raise ValueError('Previous runtime and reader build stamps differ')
    pair=paired_helper(files[names[1]],files[names[2]])
    return files,dict(build=runtime['build'],helper_pair=pair,
        files_sha256={name:sha(data) for name,data in files.items()})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,default=ROOT/'build/vc2-ce-vmu-persist')
    parser.add_argument('--probe',type=Path,default=ROOT/'build/vc2-texture-probe')
    parser.add_argument('--previous',type=Path,required=True,
                        help='Previous unmodified normal KUI folder')
    parser.add_argument('--main',required=True)
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.suffix!='.zip':raise ValueError('Use a ZIP output filename')
    if git('status','--porcelain'):raise ValueError('Commit source before packaging')
    commit=git('rev-parse','HEAD').decode().strip();stamp=commit[:12]
    main_commit=git('rev-parse',args.main).decode().strip()
    subprocess.run(['git','merge-base','--is-ancestor',main_commit,commit],
                   cwd=ROOT,check=True)
    normal=audit(args.build);probe=audit(args.probe)
    runtime=normal['helper'].get('persistent_runtime')
    if not (normal['status']=='PASS' and normal['persistent_writes'] and runtime and
            runtime['durable_commit_ack_enabled'] and runtime['storage_services_linked'] and
            normal['helper']['comparison']['mode']==0 and
            not normal['helper'].get('texture_probe')):
        raise ValueError('Top-level install must be the audited ordinary saving profile')
    if not (probe['status']=='PASS' and not probe['persistent_writes'] and
            probe['helper']['comparison']['mode']==4 and probe['helper'].get('texture_probe')):
        raise ValueError('Diagnostics must be the audited read-only reserve-only probe')
    probe['payload_call']=audit_low_entry(read_elf(args.probe/'resident-sci.elf'))
    for build,report in ((args.build,normal),(args.probe,probe)):
        report['stage_cache_policy']=cache_audit(build/'stage.elf')
    files={
        'LICENSE.txt':(ROOT/'LICENSE').read_bytes(),
        'source/change-from-main.patch':git('diff','--binary',main_commit,commit),
        'source/package_vc2_virtual_vram_fix.py':Path(__file__).read_bytes(),
        'evidence/normal-linked.json':wire(normal),
        'evidence/probe-linked.json':wire(probe),
        'evidence/copy-source.json':(ROOT/'docs/evidence/vc2-texture-copy-source.json').read_bytes(),
    }
    for path in args.evidence:
        target='evidence/'+path.name
        if target in files:raise ValueError('Duplicate evidence basename: '+path.name)
        files[target]=path.read_bytes()
    for prefix,build,label in (('',args.build,'normal'),('Diagnostics/',args.probe,'probe')):
        for target,module,elf in (
            ('KUI/runtime.kui',args.build/'runtime.kui',ROOT/'build/kui-runtime.elf'),
            ('KUI/apps/games/retail-physical.kui',build/'retail-native-foreground.kui',build/'entry.elf')):
            data,proof=installed_module(module,elf,commit)
            files[prefix+target]=data
            files['evidence/'+label+'-'+Path(target).name+'.compile.json']=wire(proof)
        helper=(build/'helper/prepared.bin').read_bytes()
        paired_helper(files[prefix+'KUI/apps/games/retail-physical.kui'],helper)
        files[prefix+'KUI/apps/games/vc2-vmu-helper.bin']=helper
    previous,previous_info=previous_files(args.previous)
    for name,data in previous.items():files['Restore-previous/KUI/'+name]=data
    files['BUILD.json']=wire(dict(commit=commit,main_included=main_commit,
        purpose='Use aligned halfword writes for CPU virtual payloads that may map VRAM',
        hardware_verified=False,complete_texture_fix_verified=False,
        virtual_vmu=True,runtime_save_writes=True,helper_reservation_bytes=65536,
        background_reader=False,prefetch_bytes=0,
        observed_failure=dict(diagnostic_build='46577020a054',
            destination_virtual='0x4243e600',destination_physical='0x0433e600',
            first_mismatch_offset=0,expected_byte=2,actual_byte=1,
            retained_source_stable=True),
        copy_policy=dict(cpu_virtual_payload='aligned volatile halfword writes',
            direct_P1_P2_main_RAM='existing memcpy',virtual_pointer_retained=True),
        diagnostics=dict(virtual_vmu=False,runtime_save_writes=False,
            probe=probe['helper']['texture_probe'],payload_call=probe['payload_call']),
        restore_previous=previous_info,
        existing_install_required='Overlay the current Smash Pack installation'))
    files['README.txt']=f'''K-UI Virtua Cop 2 virtual-VRAM copy test {stamp}

The three pages from build 46577020a054 show a stable source and an already
incorrect destination immediately after the copy. The destination virtual
address 4243E600 maps to physical video RAM 0433E600. At offset zero in a
captured chunk, the source was 02 and the destination read back as 01.

The reader recognized only direct video RAM addresses and used a byte copy
for this virtual address. This build uses the existing aligned halfword
writer for CPU virtual payloads, keeping CE's original virtual pointer and
mapping. It enables the normal virtual VMU. The complete texture result still
requires a console test; host and linked checks cannot establish that result.

INSTALL AND TEST
1. Copy this ZIP's TOP-LEVEL KUI folder over the current SD installation.
   Install all THREE files together: runtime.kui, apps/games/retail-physical.kui
   and apps/games/vc2-vmu-helper.bin. No installer script is needed.
2. Remove physical VMUs. Enable global Virtual VMU for the virtual save test.
   Use Native/Default boot with background reader, CD music and RAM patches OFF.
3. Launch Sega Smash Pack normally, then select Virtua Cop 2 from its menu.
4. Check the loading screens and gameplay textures first. Then test saving
   and loading with the virtual VMU. Existing card files are retained.

OPTIONAL DIAGNOSTICS IF TEXTURES ARE STILL CORRUPTED
Power off, then copy Diagnostics/KUI over the SD installation, installing all
three files together. Launch the same way and continue without a memory card.
This profile uses the corrected copy path but disables virtual VMU/save writes.
It intentionally stops during loading and cycles through three numbered pages.
Photograph ALL THREE pages, or the distinct preparation-stop page if shown.
Power off and reinstall the TOP-LEVEL KUI folder before normal play or saving.
The probe compares retained source and destination bytes; it does not have an
independent golden texture file. Its deliberate cache pressure adds overhead.

RESTORE THE PREVIOUS BUILD
Restore-previous/KUI contains the previous normal build {previous_info['build']}.
Copy all three files together to revert this copy fix and its diagnostic.
That restore retains the previous unresolved texture behavior.

This overlay requires the current Smash Pack installation. Main source
{main_commit} is included in the supplied source patch.
Source changes and validation evidence are included. No commercial executable,
texture asset or private save identity is included.
'''.encode()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in files.items():z.writestr(name,data)
    with zipfile.ZipFile(args.output) as z,tempfile.TemporaryDirectory(prefix='vc2-vram-extract-') as temp:
        if z.testzip() is not None:raise ValueError('ZIP CRC failure')
        z.extractall(temp)
        for name,data in files.items():
            if (Path(temp)/name).read_bytes()!=data:raise ValueError('Extracted bytes differ: '+name)
    print(json.dumps(dict(status='PASS',file=str(args.output),bytes=args.output.stat().st_size,
        sha256=sha(args.output.read_bytes()),commit=commit,main_included=main_commit,
        extract_verified=True,hardware_verified=False)))


if __name__=='__main__':main()
