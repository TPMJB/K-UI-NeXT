#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check GD yield source identities and execute production scalar admission."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile
import zlib

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
from armada_ce_rom_audit import ROMInventory


def check_source(kernel,evidence):
    if hashlib.sha256(kernel.read_bytes()).hexdigest()!=evidence['kernel_sha256']:
        raise ValueError('GD wait source requires the exact admitted VC2 kernel')
    rom=ROMInventory(kernel);module=rom.modules[evidence['module']]
    windows=evidence['windows']+evidence['firmware_calls']+[evidence['sleep_import']['thunk']]
    for row in windows:
        data=rom.rva_read(module,int(row['rva'],16),row['bytes'])
        if hashlib.sha256(data).hexdigest()!=row['sha256'] or f'{zlib.crc32(data):08x}'!=row['crc32']:
            raise ValueError('GD wait source window changed: '+row['rva'])
    core=rom.modules['coredll.dll'];row=evidence['sleep_import']['native_export']
    data=rom.rva_read(core,int(row['rva'],16),row['bytes'])
    if hashlib.sha256(data).hexdigest()!=row['sha256']:
        raise ValueError('Native Sleep body changed')
    exported={row['ordinal']:row for row in rom.exports(core)}
    if exported[496]['name']!='Sleep' or exported[496]['rva']!=0x8708:
        raise ValueError('Source Sleep export changed')
    return len(windows)+1


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc',default=os.environ.get('HOST_CC','cc'))
    parser.add_argument('--kernel',type=Path)
    parser.add_argument('--json',type=Path)
    args=parser.parse_args()
    evidence=json.loads((ROOT/'docs/evidence/vc2-gd-request-busy-source.json').read_text())
    kernel=args.kernel
    if kernel is None:
        candidate=ROOT.parent/'vc2-runtime-originals/VIRTCOP2/0WINCEOS.BIN'
        if candidate.exists():kernel=candidate
    windows=check_source(kernel,evidence) if kernel else 0
    requests=evidence['request_calls']
    controls=[r for r in evidence['firmware_calls'] if r['function'] not in (0,1,2)]
    if len(controls)!=11:raise ValueError('Expected eleven source control sites')
    if len(requests)!=18 or any(row['function'] or row['firmware_address']!='0x8c0010f0' for row in requests):
        raise ValueError('Expected exactly eighteen source-bound FN0 requests')
    control_rows=','.join('{'+str(int(row['return_rva'],16))+','+str(row['function'])+'}' for row in controls)
    rows=',\n'.join('{'+str(int(row['return_rva'],16))+','+str(row['request_command'])+'}' for row in requests)
    fixture=r'''
#include "kui/vc2_ce_vmu_gd_wait.h"
#include <assert.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do{++checks;assert(x);}while(0)
static const uint32_t sites[18][2]={ROWS};
static const uint32_t controls[11][2]={CONTROLS};
int main(void) {
    for(unsigned i=0;i<18u;i++)for(unsigned slot=0;slot<64u;slot++) {
        uint32_t pr=(slot<<25)|0x01cf0000u|sites[i][0],command=sites[i][1];
        CHECK(kui_vc2_ce_vmu_gd_wait_admit(pr,0x40008000u,command,3u)==((slot<<25)|0x01cf581cu));
        CHECK(kui_vc2_ce_vmu_gd_wait_admit(pr,0x60008000u,command,3u)==((slot<<25)|0x01cf581cu));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,0x40008000u,command^1u,3u));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr^1u,0x40008000u,command,3u));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr|0x80000000u,0x40008000u,command,3u));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr^0x10000u,0x40008000u,command,3u));
        for(unsigned source=0;source<5u;source++)if(source!=3u)
            CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,0x40008000u,command,source));
        const uint32_t bad_sr[]={0u,0x50008000u,0x40008010u,0x400080f0u};
        for(unsigned n=0;n<4u;n++)CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,bad_sr[n],command,3u));
    }
    for(unsigned i=0;i<11u;i++)for(unsigned slot=0;slot<64u;slot++) {
        uint32_t pr=(slot<<25)|0x01cf0000u|controls[i][0], function=controls[i][1];
        uint32_t tag=3u|(function<<8);
        CHECK(kui_vc2_ce_vmu_gd_wait_admit(pr,0x40008000u,0x7e001000u,tag)==((slot<<25)|0x01cf581cu));
        CHECK(kui_vc2_ce_vmu_gd_wait_admit(pr,0x60008000u,0xa6000080u,tag)==((slot<<25)|0x01cf581cu));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr^1u,0x40008000u,0u,tag));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,0x40008000u,0u,tag^0x100u));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,0x400080f0u,0u,tag));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,0x50008000u,0u,tag));
        for(unsigned source=0;source<5u;source++)if(source!=3u)
            CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,0x40008000u,0u,(function<<8)|source));
    }
    CHECK(!kui_vc2_ce_vmu_gd_wait_admit(0x01cf1788u,0x40008000u,16u,3u));
    CHECK(!kui_vc2_ce_vmu_gd_wait_admit(0x01cf30a6u,0x40008000u,28u,3u));
    printf("%u\n",checks);return 0;
}
'''.replace('ROWS',rows).replace('CONTROLS',control_rows)
    with tempfile.TemporaryDirectory(prefix='vc2-gd-wait-') as temporary:
        folder=Path(temporary);source=folder/'test.c';binary=folder/'test'
        source.write_text(fixture)
        subprocess.run(shlex.split(args.cc)+['-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
            '-DKUI_VC2_GD_WAIT_HOST_TEST=1','-I'+str(ROOT/'include'),str(source),
            str(ROOT/'src/loader/vc2_ce_vmu_gd_wait.c'),'-o',str(binary)],check=True)
        checks=int(subprocess.check_output([str(binary)],text=True,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1')).strip())
    report=dict(status='PASS',checks=checks,actual_production_admission_executed=True,
        source_request_sites=18,source_control_sites=11,source_windows_rechecked=windows,
        exact_original_kernel_rechecked=bool(kernel),source_profile_slots_checked=64,
        native_api='source-admitted WSEGACD Sleep(1)',native_handles_used=False,
        sanitizers=['address','undefined'],no_platform_memory_or_api_access=True)
    output=json.dumps(report,indent=2)+'\n'
    if args.json:args.json.write_text(output)
    print(output,end='')


if __name__=='__main__':main()
