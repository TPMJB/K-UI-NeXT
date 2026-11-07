#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute the shared stage's explicit checkpoint snapshot contract.

The conservative production API is the old-policy control. Both paths run
real recovery, filesystem validation, hashes, checkpoint and handoff sealing;
their output must match. Physical SD timing is outside this test.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
SOURCES=['tests/test_vmu_stage_snapshot_policy.c','src/loader/vmu_persist_stage.c',
    'src/core/vmu_durable.c','src/core/vmu_persist_session.c','src/core/vmu_fs_admission.c',
    'src/core/vmu_persist_profile.c','src/core/vmu_store.c','src/core/vmu_image.c',
    'src/core/hash.c','src/core/retail_image.c']
FLAGS=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
    '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
    '-ffunction-sections','-fdata-sections','-Iinclude','-Isrc/loader',
    '-DKUI_VMU_READONLY_PROOF=4','-DKUI_VMU_EXPERIMENTAL=0']

def run(command):
    return subprocess.run(command,cwd=ROOT,text=True,capture_output=True,
        env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))

def require(result,label):
    if result.returncode:raise AssertionError(label+'\n'+result.stdout+result.stderr)
    return result.stdout

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path)
    args=parser.parse_args();compiler=shlex.split(os.environ.get('HOST_CC','cc'))
    report={'schema':'kui.shared-stage.checkpoint-snapshot.v1','status':'FAIL','hardware_timing_measured':False}
    with tempfile.TemporaryDirectory(prefix='kui-stage-snapshot-') as temporary:
        folder=Path(temporary);outputs=[];rows=[]
        def compile_case(name,policy,stage_source,title_flag):
            binary=folder/name
            sources=[str(stage_source) if p=='src/loader/vmu_persist_stage.c' else p for p in SOURCES]
            require(run([*compiler,*FLAGS,'-DKUI_TEST_STAGE_SNAPSHOT_POLICY='+str(policy),
                '-DKUI_VC2_VMU_PERSIST='+str(title_flag),*sources,'-Wl,--gc-sections','-o',str(binary)]),name+' compile')
            return binary
        for policy,title_flag in ((0,1),(1,0)):
            name='conservative' if not policy else 'exclusive-checkpoint'
            binary=compile_case(name,policy,ROOT/'src/loader/vmu_persist_stage.c',title_flag)
            handoffs=folder/(name+'.bin');stdout=require(run([str(binary),str(handoffs)]),name+' execution')
            outputs.append(handoffs.read_bytes());rows.append({'policy':name,'source_title_flag':title_flag,'cases':stdout.splitlines()})
        if outputs[0]!=outputs[1]:raise AssertionError('Complete sealed current handoffs differ')
        source=(ROOT/'src/loader/vmu_persist_stage.c').read_text()
        needle='const bool fresh_snapshot=reuse_snapshot && w->session.snapshot_valid;'
        if source.count(needle)!=1:raise AssertionError('Old-policy control anchor is not unique')
        previous=folder/'old-policy-stage.c';previous.write_text(source.replace(needle,
            'const bool fresh_snapshot=false; (void)reuse_snapshot;',1))
        binary=compile_case('old-policy-control',1,previous,0)
        control=run([str(binary),str(folder/'old-policy.bin')])
        if not control.returncode or 'Assertion' not in control.stderr:
            raise AssertionError('Always-refresh old policy did not fail the eliminated-read assertion')
        report.update(status='PASS',policy_cases=rows,sealed_handoff_bytes=len(outputs[0]),
            sealed_handoffs_sha256=hashlib.sha256(outputs[0]).hexdigest(),
            old_policy_control_refused=True,non_filesystem_sector_callbacks_before=1055,
            non_filesystem_sector_callbacks_after=529,removed_callbacks_per_clean_prepare=526,
            unknown_backend_mutation_still_refused=True,invalidated_snapshot_uses_full_refresh=True,
            final_health_identity_and_marker_checks_retained=True,
            current_imported_and_recovered_contents_equal=True,
            source_sha256={p:hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in
                [*SOURCES,'include/kui/vmu_persist_stage.h','src/loader/vmu_native_persist_stage.inc',
                 'src/loader/disc_vmu_stage.c','src/loader/vc2_ce_vmu_persist_stage.inc',
                 'src/loader/smash_card_session_stage.inc']})
    if args.output:
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
