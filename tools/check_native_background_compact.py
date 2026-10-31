#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Strict complete linked audit for the compact combined Background module.

The existing persistent, dual-reader, relocation, stack and toggle auditors
remain mandatory. No requested size or CLI policy can select the profile.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

from check_native_dual_readers import compact_component_markers
from check_native_vmu_toggle_profile import exact_toggle_kernels
from check_vmu_native_persistent import check as check_persistent
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from native_runtime_layout import BACKGROUND_COMPACT,layout_for

ROOT=Path(__file__).resolve().parents[1]
SCHEMA='kui.native.background.compact.audit.v1'


def require(value,message):
    if not value:
        raise ValueError(message)


def complete_callgraphs(directory):
    """Reject partial compiler graphs before any edge/stack proof parses them."""
    reports=sorted(Path(directory).rglob('*.ci'))
    require(reports,'Compact Background has no compiler callgraph reports')
    for path in reports:
        data=path.read_text()
        depth=0;quoted=False;escaped=False
        for char in data:
            if quoted:
                if escaped:escaped=False
                elif char=='\\':escaped=True
                elif char=='"':quoted=False
            elif char=='"':quoted=True
            elif char=='{':depth+=1
            elif char=='}':
                depth-=1
                require(depth>=0,'Malformed compiler callgraph: '+str(path))
        require(data.lstrip().startswith('graph:') and data.rstrip().endswith('}') and
                depth==0 and not quoted,
                'Truncated compiler callgraph: '+str(path))
    return dict(reports=len(reports),all_reports_complete=True)


def audit(directory,*,allow_dirty=False,self_test=True):
    directory=Path(directory)
    callgraphs=complete_callgraphs(directory)
    names=('stage','native-worker','resident-sci','resident-legacy','native-ra-aux')
    stage,worker,resident,legacy,aux=(read_elf(directory/(name+'.elf')) for name in names)
    geometry=layout_for(worker[4])
    require(geometry is BACKGROUND_COMPACT,
            'This audit requires the exact compact combined Background profile')
    markers=compact_component_markers(stage,worker,resident,legacy,aux)
    require(markers.get('matching_immutable_component_markers') is True,
            'Combined compact images do not share their exact compiled markers')
    # The synchronous Standard byte-feeding implementation must not be
    # imported into either existing Background SCI engine.
    for image in (resident,legacy):
        require('__kui_native_vmu_sci_rx_paced_bus' not in image[4] and
                not any(name.startswith(('_feed_read','_dma_block')) for name in image[4]),
                'Combined Background unexpectedly contains the Standard RX DMA feeder')
    full=check_persistent(directory,self_test=self_test)
    require(full.get('worker_span')==0x12000 and full.get('worker_stack_bytes')==geometry.WORKER_STACK_BYTES==0xc00 and
            full.get('stage_embedded_worker_verified') is True and
            full.get('maple_interception_installed') is True and
            full.get('scoped_sci_transactions') is True and
            full.get('console_execution_verified') is False,
            'Combined compact persistent audit lacks the full worker/loan proof')
    read_ahead=full.get('read_ahead',{})
    dual=read_ahead.get('dual_reader',{})
    require(read_ahead.get('enabled') is True and read_ahead.get('paired_markers') is True and
            dual.get('enabled') is True and
            dual.get('compact_background',{}).get('matching_immutable_component_markers') is True,
            'Combined compact audit requires both complete embedded-reader proofs')
    if self_test:
        require(dual['compact_background'].get('mutation_rejections')==44,
                'Combined compact audit lacks all five component marker/lease mutations')
    kernels=exact_toggle_kernels(stage,worker)
    # Independent fixed descriptor: On/Off changes emulation while retaining
    # all 284 KiB, including the full 192 KiB cache.
    import struct
    _,capability,section=symbol_bytes(stage,'_kui_vmu_native_toggle_capability',1)
    require(section[2]&7==2 and capability==struct.pack('<8I',
            0x5649554b,0x3130544d,1,32,7,0x47000,0x30000,1),
            'Compact toggle descriptor changes the retained lease/cache contract')
    source=full['source_provenance']
    if not allow_dirty:
        require(source.get('source_dirty') is False and
                subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()==source.get('commit') and
                not subprocess.check_output(['git','status','--porcelain'],cwd=ROOT),
                'Final combined compact audit requires its exact clean source checkpoint')
    return dict(schema=SCHEMA,source_provenance=source,runtime=full['runtime'],
        module_sha256=full['module_sha256'],transport=1,controller_submit='split',
        geometry=dict(worker_bytes=geometry.WORKER_BYTES,lease_bytes=geometry.LEASE_BYTES,
                      cache_bytes=geometry.DATA_BYTES,worker_stack_bytes=geometry.WORKER_STACK_BYTES),
        matching_immutable_component_markers=True,
        component_markers=dual['compact_background'],
        background_reader_enabled=True,prefetch_enabled=True,prefetch_mode='auto',
        receive_engine='background-async-dma',standard_rx_feeder_absent=True,
        emulated_vmu_toggle=True,reader_only_retains_complete_lease=True,
        exact_toggle_kernels=kernels,
        compiler_callgraphs=callgraphs,
        artifact_sha256={name:hashlib.sha256(image[0]).hexdigest()
                         for name,image in zip(names,(stage,worker,resident,legacy,aux))},
        proofs=dict(persistent=full),console_execution_verified=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('build',type=Path)
    parser.add_argument('--allow-dirty',action='store_true')
    parser.add_argument('--output',type=Path)
    args=parser.parse_args()
    try:
        result=audit(args.build,allow_dirty=args.allow_dirty)
        data=json.dumps(result,indent=2)+'\n'
        if args.output:
            args.output.parent.mkdir(parents=True,exist_ok=True)
            args.output.write_text(data)
        print(data,end='')
    except (ValueError,AssertionError,KeyError,FileNotFoundError,subprocess.CalledProcessError) as error:
        parser.exit(2,'Compact combined Background audit refused: '+str(error)+'\n')


if __name__=='__main__':
    main()
