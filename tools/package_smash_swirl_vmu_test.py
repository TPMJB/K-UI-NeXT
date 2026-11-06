#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Pair the isolated Swirl save test with the unchanged confirmed VC2 restore."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import zipfile

from check_vc2_ce_vmu_package import audit
from check_vc2_stage_cached_relay import audit as cache_audit
from package_vc2_ce_vmu_late_test import ROOT, git, installed_module, sha, wire
from package_vc2_ce_vmu_persist import paired_helper
from runtime_package import verify

CONFIRMED = '10ef97cc87b7ee4ee393ebd40f83a3d94cce6461'
INSTALL = ('runtime.kui','apps/games/retail-physical.kui','apps/games/vc2-vmu-helper.bin')


def confirmed_files(path):
    with zipfile.ZipFile(path) as archive:
        if archive.testzip() is not None:
            raise ValueError('Confirmed restore ZIP failed CRC verification')
        files={name:archive.read('KUI/'+name) for name in INSTALL}
    if any(verify(files[name])['build']!=CONFIRMED[:12] for name in INSTALL[:2]):
        raise ValueError('Restore must be the user-confirmed VC2 build')
    pairing=paired_helper(files[INSTALL[1]],files[INSTALL[2]])
    return files,dict(commit=CONFIRMED,helper_pair=pairing,
                     sha256={name:sha(data) for name,data in files.items()})


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,default=ROOT/'build/smash-swirl-ce-vmu')
    parser.add_argument('--confirmed-zip',type=Path,required=True)
    parser.add_argument('--main',required=True)
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if args.output.suffix!='.zip':raise ValueError('Use a ZIP output')
    if git('status','--porcelain'):raise ValueError('Commit final source before packaging')
    commit=git('rev-parse','HEAD').decode().strip()
    main_commit=git('rev-parse',args.main).decode().strip()
    for parent in (main_commit,CONFIRMED):
        subprocess.run(['git','merge-base','--is-ancestor',parent,commit],cwd=ROOT,check=True)
    proof=audit(args.build)
    if (proof['status']!='PASS' or not proof['persistent_writes'] or
        proof['helper']['source_profile']['name']!='Sega Swirl' or
        proof['helper']['source_profile']['slot']!='A1'):
        raise ValueError('Need the audited source-bound persistent Swirl A1 profile')
    proof['stage_cache_policy']=cache_audit(args.build/'stage.elf')
    files={
        'LICENSE.txt':(ROOT/'LICENSE').read_bytes(),
        'source/change-from-main.patch':git('diff','--binary',main_commit,commit),
        'source/change-from-confirmed-VC2.patch':git('diff','--binary',CONFIRMED,commit),
        'evidence/swirl-linked.json':wire(proof),
    }
    for path in args.evidence:
        name='evidence/'+path.name
        if name in files:raise ValueError('Duplicate evidence filename: '+name)
        files[name]=path.read_bytes()
    runtime=args.build/'runtime.kui'
    for name,module,elf in (
        (INSTALL[0],runtime,ROOT/'build/kui-runtime.elf'),
        (INSTALL[1],args.build/'retail-native-foreground.kui',args.build/'entry.elf')):
        data,provenance=installed_module(module,elf,commit)
        files['KUI/'+name]=data
        files['evidence/'+Path(name).name+'.compile.json']=wire(provenance)
    helper=(args.build/'helper/prepared.bin').read_bytes()
    files['KUI/'+INSTALL[2]]=helper
    pairing=paired_helper(files['KUI/'+INSTALL[1]],helper)
    restore,restore_info=confirmed_files(args.confirmed_zip)
    for name,data in restore.items():files['Restore-confirmed-VC2/KUI/'+name]=data
    files['BUILD.json']=wire(dict(
        commit=commit,main_included=main_commit,purpose='Sega Swirl shared virtual VMU A1 save test',
        hardware_verified=False,gameplay_verified=False,save_persistence_verified=False,
        virtual_card='existing shared Sega Smash Pack VMU',virtual_slot='A1',
        save_kind='high scores',save_filename='SWIRL_HS.SYS',save_data_bytes=1280,
        background_reader=False,prefetch_bytes=0,helper_pair=pairing,
        confirmed_vc2_restore=restore_info,install_files=list(INSTALL),
        source_game_bytes_included=False))
    files['README.txt']=f'''K-UI Sega Swirl virtual VMU test {commit[:12]}

This is an isolated Sega Swirl save test for the same Sega Smash Pack image.
It reuses the existing shared Sega Smash Pack virtual card. Swirl only opens
Port A Slot 1 (A1); Virtua Cop 2's confirmed profile remains A2 in the restore.
No original game executable, game asset or private card identity is included.
Source and linked/host checks pass; console playback and saving need testing.

INSTALL AND TEST
1. Copy this ZIP's TOP-LEVEL KUI folder over your current SD installation.
   Install all THREE files together. No installer script is needed.
2. Remove physical VMUs. Enable Virtual VMU. Keep background reader,
   CD music and RAM patches OFF. Use Native/Default boot.
3. Launch Sega Smash Pack normally and select SEGA SWIRL.
4. Finish a game with a new high score. Accept Save To VMU and overwrite
   confirmation if shown. Wait for Save Successful and for SD activity to end.
5. Power off, restart the same Swirl route and check that the score loads.
   This tests the game's high-score save, not a gameplay savestate.

If it stops on a blue diagnostic, photograph all pages and report the last
game action. The return/reset combination remains a deliberate test stop;
power off to restart after that screen.

RESTORE THE CONFIRMED VIRTUA COP 2 BUILD
This test reader is for Swirl. Before testing VC2 again, copy the complete
Restore-confirmed-VC2/KUI folder over the SD installation. Its THREE files
are byte-identical to the confirmed {CONFIRMED[:12]} package.
The existing shared card and its saves are retained on either route.

Main source {main_commit} is included in the full source patch. The smaller
patch begins at confirmed VC2 {CONFIRMED}. No console reset or normal-menu
native Genesis save integration is claimed by this package.
'''.encode()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
        for name,data in files.items():archive.writestr(name,data)
    with zipfile.ZipFile(args.output) as archive,tempfile.TemporaryDirectory(prefix='swirl-delivery-') as folder:
        if archive.testzip() is not None:raise ValueError('Output ZIP failed CRC verification')
        archive.extractall(folder)
        for name,data in files.items():
            if (Path(folder)/name).read_bytes()!=data:
                raise ValueError('Extracted output differs: '+name)
    print(json.dumps(dict(status='PASS',file=str(args.output),bytes=args.output.stat().st_size,
        sha256=sha(args.output.read_bytes()),commit=commit,extract_verified=True,
        hardware_verified=False)))


if __name__=='__main__':main()
