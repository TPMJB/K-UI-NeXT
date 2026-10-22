#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Check explicit VMU layout profiles and private-range refusal boundaries."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools'))
import armada_ce_cache_snapshot_contract as snapshot
from check_armada_ce_vmu_a2_package import check_runtime_stage_addresses
from test_armada_ce_vmu_runtime_native import runtime_address_marker_mutations

C=r'''
#include <assert.h>
#include <stdint.h>
#include "kui/armada_ce_maple_reservation.h"
#include "kui/armada_ce_maple_traffic.h"
int main(void) {
    const uint32_t begin=KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT?0x8cfc4000u:0x8cfca000u;
    assert(KUI_MAPLE_RESERVE_BEGIN==begin);
    assert(KUI_ARMADA_CE_VMU_RESERVE_BYTES==(KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT?49152u:24576u));
    assert(KUI_ARMADA_CE_VMU_HOOK_P2==begin+0x20000000u);
    assert(KUI_ARMADA_CE_VMU_STACK_BOTTOM==0xacfcf800u);
    assert(KUI_ARMADA_CE_VMU_STACK_TOP==0xacfcfe00u);
    assert(KUI_ARMADA_CE_VMU_HANDOFF_P2==0xacfce200u);
    assert(KUI_ARMADA_CE_VMU_HANDOFF_BYTES==1272u);
    assert(KUI_ARMADA_CE_VMU_SERVICES_ADDRESS==0xacfce700u);
    assert(KUI_ARMADA_CE_VMU_SERVICES_SIZE==32u);
    assert(KUI_ARMADA_CE_VMU_WORKER_STACK_TOP-KUI_ARMADA_CE_VMU_WORKER_STACK_BOTTOM==4096u);
    assert(KUI_ARMADA_CE_VMU_HANDOFF_P2+KUI_ARMADA_CE_VMU_HANDOFF_BYTES<=KUI_ARMADA_CE_VMU_SERVICES_ADDRESS);
    assert(KUI_ARMADA_CE_VMU_SERVICES_ADDRESS+KUI_ARMADA_CE_VMU_SERVICES_SIZE<=KUI_ARMADA_CE_VMU_WORKER_STACK_BOTTOM);
    assert(KUI_ARMADA_CE_VMU_WORKER_STACK_TOP==KUI_ARMADA_CE_VMU_STACK_BOTTOM);
    const uint32_t centers[]={0x8cfc4000u,0x8cfca000u,0x8cfd0000u};
    for(unsigned c=0;c<3;c++) for(int delta=-1024;delta<=1024;delta+=4)
        for(unsigned bytes=4;bytes<=1024;bytes+=4) {
            const uint32_t at=centers[c]+(uint32_t)delta;
            const uint64_t end=(uint64_t)at+bytes;
            const int overlaps=at<0x8cfd0000u && end>begin;
            assert(kui_armada_ce_maple_traffic_owned(at,bytes)==overlaps);
            assert(kui_maple_reservation_destination_safe(at,bytes)==!overlaps);
            assert(kui_maple_reservation_destination_safe(at+0x20000000u,bytes)==!overlaps);
        }
    return 0;
}
'''


def refuses(function,*args):
    try:function(*args)
    except ValueError:return
    raise AssertionError('Accepted mismatched or invalid layout')


def main():
    compiler=os.environ.get('HOST_CC','cc')
    with tempfile.TemporaryDirectory(prefix='kui-vmu-layout-') as temporary:
        directory=Path(temporary);source=directory/'layout.c';source.write_text(C)
        for mode in (0,1):
            binary=directory/('layout'+str(mode))
            flags=['-DKUI_ARMADA_CE_MAPLE_A2=1','-DKUI_ARMADA_CE_MAPLE_A2_STAGE=1',
                   '-DKUI_ARMADA_CE_MAPLE_WRITE_LAYOUT='+str(mode)]
            subprocess.run([compiler,'-std=c11','-Wall','-Wextra','-Werror','-I'+str(ROOT/'include'),
                *flags,str(source),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
        invalid=subprocess.run([compiler,'-E','-x','c','-I'+str(ROOT/'include'),
            '-DKUI_ARMADA_CE_MAPLE_WRITE_LAYOUT=2','-include','kui/armada_ce_vmu_layout.h','-'],
            input='',text=True,capture_output=True)
        assert invalid.returncode and 'explicitly disabled or enabled' in invalid.stderr
        # Only this independent linker profile may enable the experimental ABI.
        script=ROOT/'src/loader/armada_ce_vmu_a2_write.ld.S'
        rendered=subprocess.run([compiler,'-E','-P','-x','c','-I'+str(ROOT/'include'),
            '-DKUI_ARMADA_CE_MAPLE_WRITE_LAYOUT=1',str(script)],check=True,text=True,capture_output=True).stdout
        assert '__kui_maple_write_layout = 1;' in rendered
        assert '0x8cfc4000' in rendered and '0xacfcf800' in rendered
        assert '.vmu_handoff' not in rendered and '.vmu_services' not in rendered
        runtime=subprocess.run([compiler,'-E','-P','-x','c','-I'+str(ROOT/'include'),
            '-DKUI_ARMADA_CE_MAPLE_WRITE_LAYOUT=1','-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME=1',
            str(script)],check=True,text=True,capture_output=True).stdout
        assert '__kui_maple_write_runtime = 1;' in runtime
        assert '.vmu_handoff (NOLOAD)' in runtime and '.vmu_services (NOLOAD)' in runtime
        assert '.worker_stack (NOLOAD)' in runtime and '0xacfce700' in runtime
        assert '. = 0xacfce200;' in runtime
        # Compile the actual source witness block as a freestanding32-bit ELF.
        # No native package is edited or rebuilt by these marker mutations.
        stage=(ROOT/'src/loader/retail_stage.c').read_text()
        start=stage.index('#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME\n#define KUI_VMU_LAYOUT_STRING_INNER')
        finish=stage.index('#endif',start)+len('#endif')
        marker_source=directory/'stage-markers.c'
        marker_source.write_text('#include "kui/armada_ce_vmu_layout.h"\n'+stage[start:finish]+'\n')
        for enabled in (0,1):
            marker_object=directory/('stage-markers'+str(enabled)+'.o')
            subprocess.run([compiler,'-m32','-c','-I'+str(ROOT/'include'),
                '-DKUI_ARMADA_CE_VMU_WRITE_RUNTIME='+str(enabled),str(marker_source),
                '-o',str(marker_object)],check=True)
            check_runtime_stage_addresses(marker_object.read_bytes(),bool(enabled))
            if enabled:assert runtime_address_marker_mutations(marker_object)==23
        wrong=subprocess.run([compiler,'-E','-P','-x','c','-I'+str(ROOT/'include'),str(script)],capture_output=True)
        assert wrong.returncode
        for assignments,message in ((['ARMADA_A2_WRITE_LAYOUT=2'],'must be0 or1'),
                                    (['ARMADA_A2_WRITE_LAYOUT=','ARMADA_A2_WRITE_STAGING=0'],'must be0 or1'),
                                    (['ARMADA_A2_WRITE_LAYOUT=0','ARMADA_A2_WRITE_STAGING=1'],'requires ARMADA_A2_WRITE_LAYOUT=1'),
                                    (['ARMADA_A2_WRITE_RUNTIME=2'],'must be0 or1'),
                                    (['ARMADA_A2_WRITE_RUNTIME=1'],'requires ARMADA_A2_WRITE_LAYOUT=1 and ARMADA_A2_WRITE_STAGING=1'),
                                    (['ARMADA_A2_WRITE_LAYOUT=1','ARMADA_A2_WRITE_STAGING=1','ARMADA_A2_WRITE_RUNTIME=1'],
                                     'requires an explicit SD_PROFILE file')):
            refused=subprocess.run(['make','-f','Makefile.armada-vmu-a2',*assignments,'-n'],
                cwd=ROOT,text=True,capture_output=True)
            assert refused.returncode and message in refused.stderr
    for mode in (False,True):
        begin=0xacfc4000 if mode else 0xacfca000
        for job in (begin,0xacfcf800-snapshot.JOB_BYTES):
            body=snapshot.expected_body(job,mode)
            snapshot.check(body,job,mode)
            refuses(snapshot.check,body,job,not mode)
            for at in range(len(body)):
                changed=bytearray(body);changed[at]^=1
                refuses(snapshot.check,bytes(changed),job,mode)
        refuses(snapshot.expected_body,begin-4,mode)
        refuses(snapshot.expected_body,0xacfcf800-snapshot.JOB_BYTES+4,mode)
    refuses(snapshot.expected_body,0xacfcf000,1)
    print('PASS CE VMU layouts: readonly24KiB / experimental48KiB; source/DMA bounds, linker admission and complete snapshot profile mutations')


if __name__=='__main__':main()
