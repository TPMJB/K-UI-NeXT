#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Enforce shared cold-start policy across actual normal build families.

This checks effective make flags and the preprocessed common implementations.
Existing linked SH/CRC and storage reports remain separate evidence; this is
the regression guard against making those defaults title/SDK/VMU opt-ins.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
TOY_COMPAT_ARGS=('NATIVE_COMPAT=1','PRIVATE_P2=1','NATIVE_CACHE=0',
    'DATA_PROBE=1','LOADER_TRACE=1','DATA_SCHEDULE_MODE=2','DATA_BUDGET_PROFILE=4',
    'AD_FOLLOWUP_PROFILE=2','DATA_TUNING_PROFILE=1','STARTUP_IRQ_PROFILE=1','DATA_PIPELINE_PROFILE=2')
CE_STAGE_FLAGS='$(RETAIL_FLAGS) -DKUI_RETAIL_CE=1 -DKUI_ARMADA_CE_AUDIO_STAGE=1 $(ARMADA_AUDIO_EXTRA_STAGE_FLAGS)'
TOY_STAGE_FLAGS='$(FLAGS) $(COOKED_STAGE_FLAGS) -DKUI_NATIVE_CDDA_COMPAT=$(NATIVE_COMPAT)'
PROFILES=(
    ('ordinary-native','Makefile.dc','RETAIL_FLAGS',(),()),
    ('ordinary-ce','Makefile.dc','RETAIL_LEGACY_FLAGS',(),('-DKUI_RETAIL_CE=1',)),
    ('compressed-lz4','Makefile.dc','ZSO_FLAGS',(),()),
    ('compressed-lzo','Makefile.dc','LZO_FLAGS',(),()),
    ('foreground-no-vmu','Makefile.native-ocram-bootstrap','STAGE_FLAGS',(),()),
    ('standalone-no-vmu','Makefile.native-standalone-general','STAGE_FLAGS',(),()),
    ('standard-a2','Makefile.vmu-native-vmu-lite-paced','FLAGS',('VMU_TRANSPORT=1','VMU_SCI_RX_POLICY=paced'),()),
    ('background-a2','Makefile.vmu-native-background-compact','FLAGS',(),()),
    ('background-a1','Makefile.vmu-native-background-compact-a1','FLAGS',(),()),
    ('smash-menu','Makefile.smash-menu-loader','STAGE_FLAGS',(),()),
    ('smash-native','Makefile.smash-native-menu','FLAGS',(),()),
    ('vc2','Makefile.vc2-ce-vmu','STAGE_FLAGS',('VC2_LATE=1','VC2_PERSIST=1'),()),
    ('swirl','Makefile.smash-swirl-ce-vmu','STAGE_FLAGS',(),()),
    ('smash-vc2-child','Makefile.smash-vc2-child','STAGE_FLAGS',(),()),
    ('smash-swirl-child','Makefile.smash-swirl-child','STAGE_FLAGS',(),()),
    ('ce-cdda-armada','Makefile.armada-stream',CE_STAGE_FLAGS,(),()),
    ('ce-cdda-bam4','Makefile.bam4-stream',CE_STAGE_FLAGS,(),()),
    ('ce-cdda-worms','Makefile.worms-stream',CE_STAGE_FLAGS,(),()),
    ('ce-cdda-wwp','Makefile.wwp-stream',CE_STAGE_FLAGS,(),()),
    ('native-cdda-toy-raw','Makefile.toy_pilot',TOY_STAGE_FLAGS,TOY_COMPAT_ARGS,()),
    ('native-cdda-toy-cooked','Makefile.toy_pilot',TOY_STAGE_FLAGS,(*TOY_COMPAT_ARGS,'COOKED_DATA_PROFILE=1'),()),
    ('native-cdda-mdk2','Makefile.mdk2_cdda','FLAGS',('NATIVE_COMPAT=1',),('-DKUI_NATIVE_CDDA_COMPAT=1',)),
    ('disc-physical','Makefile.disc-native','FLAGS',(),()),
    ('disc-vmu-a2','Makefile.disc-vmu','DISC_FLAGS',(),()),
    ('disc-vmu-a1','Makefile.disc-vmu-a1','DISC_FLAGS',(),()),
)

def sha(data):return hashlib.sha256(data).hexdigest()
def require(value,message):
    if not value:raise AssertionError(message)

def make_policy_output(name,makefile,field,extra):
    expression=field if field.startswith('$(') else '$('+field+')'
    rule='kui-global-policy:\n\t$(info POLICY='+expression+')\n\t$(info FILES=$(MAKEFILE_LIST))\n\t@:\n'
    command=['make','--no-print-directory','-s','-rR','-f',makefile,
        'BUILD=build/global-policy-'+name,*extra,'--eval='+rule,'kui-global-policy']
    command=['bash','-c','source .deps/kos/environ.sh\nexec "$@"','kui-global-policy',*command]
    output=subprocess.check_output(command,cwd=ROOT,text=True)
    value=re.search(r'^POLICY=(.*)$',output,re.M).group(1)
    files=re.search(r'^FILES=(.*)$',output,re.M).group(1).split()
    return value,files

def effective_flags(profile):
    name,makefile,field,extra,added=profile
    if name.startswith('ce-cdda-'):
        # Use the aggregate recipe's accepted configuration, including the CE
        # resume fix and high resident, rather than wrapper-only make defaults.
        recipe,_=make_policy_output(name+'-recipe','Makefile.dc','WINCE_CDDA_BUILD_FLAGS',())
        extra=(*extra,*shlex.split(recipe))
        require('KUI_CE_SCI_RESUME_FIX=1' in extra and 'KUI_RETAIL_LOW_RESIDENT=0' in extra,
            'Aggregate CE/CDDA recipe lost its accepted resident/resume configuration')
    value,files=make_policy_output(name,makefile,field,extra)
    require(value.strip(),'Missing effective stage flags: '+name)
    return [arg for arg in shlex.split(value) if arg.startswith(('-D','-U'))]+list(added),files

def preprocess(source,defines,assembly=False):
    if assembly and '#include "vc2_ce_vmu_symbols.h"' in source:
        # This source-only policy probe does not assemble helper data or
        # open incbin files. The generated header supplies only the raw byte
        # count in that metadata; actual helper/package admission is separate.
        # Keep every instruction and feature branch of the cold relay intact.
        require(source.count('#include "vc2_ce_vmu_symbols.h"')==1 and
            len(re.findall(r'\bKUI_VC2_VMU_BLOB_BYTES\b',source))==1,
            'Generated helper metadata gained a use outside this policy probe')
        source=source.replace('#include "vc2_ce_vmu_symbols.h"','')
    compiler=ROOT/'.deps/sh-elf/bin/sh-elf-gcc'
    return subprocess.check_output([str(compiler),'-E','-P','-x',
        'assembler-with-cpp' if assembly else 'c','-Iinclude','-Isrc/loader',*defines,'-'],
        cwd=ROOT,input=source,text=True,stderr=subprocess.PIPE)

def compact(text):return ' '.join(text.split())

def relay_contract(text,disc=False):
    p='.Ldisc_resume_' if disc else '.L'
    start='_kui_disc_vmu_game_resume:' if disc else '_kui_retail_game_resume:'
    require(start in text,'Missing real shared cold relay')
    body=text.split(start,1)[1].split(p+'highstack:',1)[0]
    sequence='mov.l '+p+'scan_ccr,r1 mov.l r1,@r0 '+'nop '*8+\
        'mov r8,r4 mov r9,r5 mov.l '+p+'relaymain,r0 jsr @r0 nop'
    require(compact(sequence) in compact(body),'Cold relay lost unconditional ICE/P1 C entry')
    restore='mov.l '+p+'off,r0 or r9,r0 mov.l '+p+'ccr,r1 mov.l r0,@r1 '+'nop '*8
    require(compact(restore) in compact(body),'Cold relay lost exact owner cache restoration')
    require(re.search(re.escape(p+'scan_ccr:')+r'\s*\.long\s+0x00000908\b',text),
        'Cold relay must enable instructions only; operand cache remains off')
    require(re.search(re.escape(p+'off:')+r'\s*\.long\s+0x00000808\b',text),'Cold restore purge bits changed')
    return dict(instruction_cache=True,operand_cache=False,P1_C_entry=True,owner_CCR_restored=True)

def crc_contract(text):
    match=re.search(r'uint32_t global_policy_crc\([^)]*\)\s*\{([^}]+)\}',text)
    require(match and 'return kui_stage_crc32(previous,data,bytes);'==compact(match.group(1)),
        'Cold byte CRC must not depend on a feature/title/VMU flag')
    require('static const uint32_t table[256]' in text,'Full cold byte CRC table is missing')

def current_report(path):
    report=json.loads(Path(path).read_text());hashes=report.get('source_sha256',{})
    require(hashes and all(sha((ROOT/name).read_bytes())==value for name,value in hashes.items()),
        'Stale shared behavior report: '+str(path))
    require(report.get('status')=='PASS' or report.get('ok') is True,'Shared behavior report did not pass')
    return report

def audit(history_report=None,checksum_report=None):
    retail_s=(ROOT/'src/loader/retail_stage.S').read_text()
    retail_c=(ROOT/'src/loader/retail_stage.c').read_text().split('#include "kui/skies_read_diagnostic.h"',1)[0]
    disc_s=(ROOT/'src/loader/disc_vmu_stage_asm.S').read_text()
    inputs={'src/loader/retail_stage.S','src/loader/retail_stage.c','src/loader/disc_stage.S',
        'src/loader/disc_stage.c','src/loader/disc_vmu_stage_asm.S','src/loader/disc_vmu_stage.c',
        'src/loader/stage_fast_crc32.h','tests/test_global_launch_performance_policy.py'}
    rows=[];first_asm=None;first_crc=None
    for profile in PROFILES:
        name=profile[0];defines,makefiles=effective_flags(profile)
        inputs.update(path for path in makefiles if path.startswith(('Makefile','config/')))
        if name=='disc-physical':
            asm=preprocess((ROOT/'src/loader/disc_stage.S').read_text(),defines,True)
            require('.Lcache: .long 0x00000909' in compact(asm),'Physical disc cold entry lost its caches')
            require('_kui_disc_vmu_game_resume' not in asm,'Plain physical disc unexpectedly acquired a VMU relay')
            cold=dict(initial_caches=True,post_bootstrap_relay=False)
        elif name.startswith('disc-vmu'):
            cold=relay_contract(preprocess(disc_s,defines,True),True)
        else:
            asm=preprocess(retail_s,defines,True);cold=relay_contract(asm)
            if first_asm is None:first_asm=asm
        for old_flag in (0,1):
            selected=[x for x in defines if not x.startswith('-DKUI_NATIVE_STAGE_FAST_CRC=')]
            selected.append('-DKUI_NATIVE_STAGE_FAST_CRC='+str(old_flag))
            if name.startswith('disc-'):
                source='src/loader/disc_vmu_stage.c' if name.startswith('disc-vmu') else 'src/loader/disc_stage.c'
                prefix=(ROOT/source).read_text().split('#include "stage_fast_crc32.h"',1)[0]+\
                    '#include "stage_fast_crc32.h"\n'
                call='kui_stage_crc32'
            else:prefix=retail_c;call='kui_retail_crc32'
            prefix+='\nuint32_t global_policy_crc(uint32_t previous,const void *data,size_t bytes){return '+call+'(previous,data,bytes);}\n'
            crc=preprocess(prefix,selected);crc_contract(crc)
            if first_crc is None:first_crc=crc
        rows.append(dict(profile=name,makefile=profile[1],cold_cache_policy=cold,
            full_byte_CRC=True,legacy_fast_CRC_flag_zero_still_optimized=True))
    # The guard itself must catch the old policy and unsafe restoration edits.
    mutations=(first_asm.replace('0x00000908','0x00000808'),
        first_asm.replace('mov.l .Lscan_ccr,r1','mov.l .Loff,r1'),
        first_asm.replace('or r9,r0','nop'))
    for changed in mutations:
        try:relay_contract(changed)
        except AssertionError:pass
        else:raise AssertionError('Global cache policy regression was accepted')
    try:crc_contract(first_crc.replace('return kui_stage_crc32(previous,data,bytes);',
        'return kui_retail_crc32(previous,data,bytes);'))
    except AssertionError:pass
    else:raise AssertionError('Global CRC fallback regression was accepted')
    sys.path[:0]=[str(ROOT/'tests')]
    from test_game_image_batch import audit as image_audit
    image=image_audit()
    if checksum_report:checksum=current_report(checksum_report)
    else:
        from test_games_retail_checksum_batch import audit as checksum_audit
        checksum=checksum_audit()
    require(image['representative_257_raw_sectors']['new_callbacks']==17 and
        checksum['representative_257_sectors']['new_callbacks']==17,'Shared image/hash batching regressed')
    if history_report:
        history=current_report(history_report)
        launch=history['complete_session_launch_io']
        require(history['full_real_fatfs_regressions_executed'] and launch['status']=='PASS',
            'Whole history launch path lacks current durability and I/O evidence')
    else:
        from test_vmu_history_launch_io import audit as launch_audit
        launch=launch_audit()
    expected={(filesystem,cluster,scenario)
        for filesystem,clusters in launch['tested_cluster_bytes'].items()
        for cluster in clusters for scenario in launch['tested_scenarios']}
    actual={(row['filesystem'],row['actual_cluster'],row['scenario'])
        for row in launch['results']['current']}
    require(len(launch['results']['current'])==len(expected) and actual==expected and
        all(row['retained_image_read_sectors']==512 for row in launch['results']['current']),
        'Whole history path omitted a scenario or repeated/skipped verified reads')
    return dict(schema='kui.global-launch-performance-policy.v1',status='PASS',profiles=rows,
        preprocessed_profile_count=len(rows),CRC_flag_variants_per_profile=2,unsafe_policy_mutations_refused=4,
        per_game_or_SDK_opt_in=False,VMU_off_receives_cold_policy=True,
        core_image_batch=image,launcher_checksum_batch=checksum,whole_history_launch=launch,
        source_sha256={name:sha((ROOT/name).read_bytes()) for name in sorted(inputs)},
        hardware_timing_measured=False,
        limits='Effective shared source policy and current behavior reports; no claim every game or console timing was tested.')

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--history-report',type=Path)
    p.add_argument('--checksum-report',type=Path);p.add_argument('--output',type=Path);a=p.parse_args()
    result=audit(a.history_report,a.checksum_report);text=json.dumps(result,indent=2)+'\n'
    if a.output:a.output.write_text(text)
    print(text,end='')
