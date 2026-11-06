#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind Swirl's source receipt to actual shared GD wait scalar admission.

No hardware execution, graphics, interrupt timing, or persistence is modeled.
"""
import argparse
import json
import os
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
from swirl_ce_gd_wait_owner_audit import audit


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--kernel', type=Path, required=True)
    parser.add_argument('--cc', default=os.environ.get('HOST_CC', 'cc'))
    args = parser.parse_args()
    source_report = audit(args.kernel)
    evidence = json.loads((ROOT / 'docs/evidence/swirl-gd-request-busy-source.json').read_text())
    rows = ','.join('{%s,%s}' % (int(r['return_rva'], 16), r['request_command'])
                    for r in evidence['request_calls'])
    fixture = r'''
#include "kui/vc2_ce_vmu_gd_wait.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
static const uint32_t sites[18][2]={ROWS};
static unsigned checks;
#define CHECK(x) do { ++checks; assert(x); } while(0)
int main(void) {
    for (unsigned i=0;i<18u;i++) for (unsigned slot=0;slot<64u;slot++) {
        uint32_t pr=(slot<<25)|0x01cf0000u|sites[i][0], command=sites[i][1];
        CHECK(kui_vc2_ce_vmu_gd_wait_admit(pr,0x40008000u,command,3u)==((slot<<25)|0x01cf581cu));
        CHECK(kui_vc2_ce_vmu_gd_wait_admit(pr,0x60008000u,command,3u)==((slot<<25)|0x01cf581cu));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr^1u,0x40008000u,command,3u));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr^0x10000u,0x40008000u,command,3u));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr|0x80000000u,0x40008000u,command,3u));
        CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,0x40008000u,command^1u,3u));
        for(unsigned source=0;source<5u;source++) if(source!=3u)
            CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,0x40008000u,command,source));
        const uint32_t bad_sr[]={0u,0x50008000u,0x40008010u,0x400080f0u};
        for(unsigned j=0;j<4u;j++) CHECK(!kui_vc2_ce_vmu_gd_wait_admit(pr,bad_sr[j],command,3u));
    }
    printf("%u\n",checks); return 0;
}
'''.replace('ROWS', rows)
    with tempfile.TemporaryDirectory(prefix='swirl-gd-wait-') as directory:
        folder=Path(directory); source=folder/'fixture.c'; binary=folder/'fixture'
        # Reject both a wrong-size owner and a changed byte before any profile access.
        changed=folder/'changed-kernel.bin'
        original=args.kernel.read_bytes()
        for candidate in (original[:-1], bytes([original[0]^1])+original[1:]):
            changed.write_bytes(candidate)
            try:
                audit(changed)
            except ValueError:
                pass
            else:
                raise AssertionError('Swirl source identity mutation was accepted')
        changed.unlink()
        source.write_text(fixture)
        subprocess.run(shlex.split(args.cc) + ['-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
            '-DKUI_VC2_GD_WAIT_HOST_TEST=1','-I'+str(ROOT/'include'),str(source),
            str(ROOT/'src/loader/vc2_ce_vmu_gd_wait.c'),'-o',str(binary)],check=True)
        checks=int(subprocess.check_output([str(binary)],text=True,
            env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0:halt_on_error=1')).strip())
    print(json.dumps(dict(status='PASS',checks=checks,production_admission_executed=True,
        source=source_report,source_identity_negative_cases=2,profile_slots_checked=64,sanitizers=['address','undefined'],
        no_native_handles_used=True,console_compatibility_verified=False),indent=2))


if __name__ == '__main__':
    main()
