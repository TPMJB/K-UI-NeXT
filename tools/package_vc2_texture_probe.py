#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Deliver a read-only VC2 payload evidence overlay and matching normal restore."""
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
from package_vc2_ce_vmu_late_test import ROOT,git,installed_module,sha,wire


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build',type=Path,default=ROOT/'build/vc2-texture-probe')
    parser.add_argument('--normal',type=Path,default=ROOT/'build/vc2-ce-vmu-persist')
    parser.add_argument('--main',required=True)
    parser.add_argument('--evidence',type=Path,action='append',default=[])
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args()
    if git('status','--porcelain'):raise ValueError('Commit source before packaging')
    commit=git('rev-parse','HEAD').decode().strip();stamp=commit[:12]
    main_commit=git('rev-parse',args.main).decode().strip()
    subprocess.run(['git','merge-base','--is-ancestor',main_commit,commit],cwd=ROOT,check=True)
    probe=audit(args.build);normal=audit(args.normal)
    if (probe['persistent_writes'] or probe['helper']['comparison']['mode']!=4 or
        not probe['helper'].get('texture_probe') or
        not normal['persistent_writes'] or normal['helper']['comparison']['mode']!=0 or
        normal['helper'].get('texture_probe')):
        raise ValueError('Need read-only reserve-only probe and ordinary saving restore')
    probe['payload_call']=audit_low_entry(read_elf(args.build/'resident-sci.elf'))
    for build,report in ((args.build,probe),(args.normal,normal)):
        report['stage_cache_policy']=cache_audit(build/'stage.elf')
    files={
        'LICENSE.txt':(ROOT/'LICENSE').read_bytes(),
        'source/change-from-main.patch':git('diff','--binary',main_commit,commit),
        'evidence/probe-linked.json':wire(probe),
        'evidence/normal-restore-linked.json':wire(normal),
        'evidence/copy-source.json':(ROOT/'docs/evidence/vc2-texture-copy-source.json').read_bytes(),
        'evidence/9c7-hardware.json':(ROOT/'docs/evidence/vc2-payload-9c7-hardware.json').read_bytes(),
    }
    for path in args.evidence:
        target='evidence/'+path.name
        if target in files:raise ValueError('Duplicate evidence name')
        files[target]=path.read_bytes()
    for prefix,build in (('',args.build),('Restore-normal/',args.normal)):
        for target,module,elf in (
            ('KUI/runtime.kui',args.normal/'runtime.kui',ROOT/'build/kui-runtime.elf'),
            ('KUI/apps/games/retail-physical.kui',build/'retail-native-foreground.kui',build/'entry.elf')):
            data,proof=installed_module(module,elf,commit)
            files[prefix+target]=data
            files['evidence/'+('probe-' if not prefix else 'restore-')+Path(target).name+'.compile.json']=wire(proof)
        helper=(build/'helper/prepared.bin').read_bytes()
        paired_helper(files[prefix+'KUI/apps/games/retail-physical.kui'],helper)
        files[prefix+'KUI/apps/games/vc2-vmu-helper.bin']=helper
    files['BUILD.json']=wire(dict(commit=commit,main_included=main_commit,
        purpose='Stable pre-copy source and immediate uncached-code readback versus later cache activity',
        hardware_verified=False,texture_fix_claimed=False,virtual_vmu=False,
        helper_reservation_bytes=65536,background_reader=False,prefetch_bytes=0,
        runtime_save_writes=False,original_byte_copy_policy_retained=True,
        probe=probe['helper']['texture_probe'],payload_call=probe['payload_call'],
        stop_flags_hex=dict(map='01',mismatch='02',target_tail='08',
                            chunk_limit='10',preparation='20',source_changed='40',early_late_changed='80',pre_state_error='100'),
        readback_scope='Retained pre-copy source, immediate P2-code destination/live-source comparison, then late comparison after controlled read-only cache pressure; no independent source-file reference',
        existing_install_required='Use over the previously supplied current-main Smash Pack installation'))
    files['README.txt']=f'''K-UI VC2 stable-source and cache evidence {stamp}

This is a DIAGNOSTIC, not a texture fix or performance build.
The user reported corrupted textures with profile04 and clean textures with03.
It retains04's reader and64KiB reservation, with NO installed Maple hook or
virtual VMU. It intentionally retains the original copy policy to observe it.
Main {main_commit} is included. Use over the previous Smash Pack installation;
GD Play/ripper files are not replaced because main has not advanced.

INSTALL AND RUN
1. Copy this ZIP's top-level KUI folder over the SD installation. Install all
   THREE files together: runtime.kui, retail-physical.kui, vc2-vmu-helper.bin.
2. Remove physical VMUs. Use Native/Default boot, background reader, CD music
   and RAM patches OFF. Either global Virtual VMU setting is supported.
3. Launch the normal Sega Smash Pack menu, then Virtua Cop2. Continue without
   a memory card. Let it load until the numbered diagnostic pages appear.
4. Photograph ALL THREE pages, which cycle automatically. An intentional stop
   during loading is expected. If it says EARLY STOP/PREPARATION STOP instead,
   photograph that single distinct page. There is no saving test this time.
5. Power off. Copy Restore-normal/KUI over the installation afterward to
   restore ordinary VC2 saving. Existing card files are retained.

WHAT IS MEASURED
The previous9c7 capture stopped on an actual source/readback difference:
WHY02, lengthA6, first differing offset5E, expected48 and actual43. Its unique
UTLB mapping was cached MAIN RAM, not VRAM. The source LBA belongs to
SOUND11.WVP in the supplied catalog. This does not establish a VMU save fault.
That probe compared only AFTER cached C metadata work; one of its own literal
reads shares the destination cache index. We must separate original delivery,
source lifetime and later cache effects before choosing a fix.

The new measuring path preserves up to512 source bytes BEFORE the unchanged
copy. The first destination readback executes through UNCACHED P2 code, with
private P2 reference bytes/state, before cached metadata or translation calls.
It compares the live source against that reference as well. It then records
request/CPU/MMU context and applies read-only pressure at matching cache indices
using all four cache colours of this loader's admitted immutable code image,
with the destination virtual colour first. A final comparison shows
whether the destination or source changes across that later work.
DMA1 SAR/DAR/TCR/CHCR are captured at PRE and POST. No DMA/cache/MMU/TLB register
is written by the measuring shim. Matching-index reads intentionally affect
cache occupancy; this is a controlled diagnostic, not normal game behavior.
The helper uses its own existing4KiB stack and64KiB reservation. No VMU save
writes or prefetch are enabled. Readback adds loading overhead.

PAGE1 shows retained-source EARLY destination results, LATE destination results,
and early/late live-source comparisons. These FNV hashes cover ONE chunk, not
an independently verified asset. FIRST=FFFFFFFF means that comparison matched;
FFFFFFFE means no valid readback was available. PRE state3 means the retained
source tuple was admitted and consumed by POST. PRE state2 means it was invalid.
PRE state0 on a map failure means no PRE measurement was available.
PAGE2 records the disk read, CPU/MMU context and first-address UTLB mapping.
Zero/multiple UTLB matches remain unknown. PAGE3 records DMA1 PRE/POST tuples
and deliberate EVICT read count. The cold allocator receipt from the previous
run is included separately; no allocator pointers are followed in this probe.

WHY is a hexadecimal flag mask, with flags allowed to combine:
01 map failure;02 destination differs from the retained pre-copy source;
08 selected asset tail;10 chunk limit;20 preparation failure;
40 live source differs from its pre-copy snapshot;
80 EARLY and LATE views differ;100 missing/mismatched/oversize PRE tuple.
A source change is distinct from a failed destination copy. No root cause is
claimed until these observations distinguish those cases. The byte-copy policy
remains unchanged so this test does not silently replace the suspected behavior.

The probe deliberately stops on a measured difference/map/PRE failure, otherwise
near the selected asset tail or bounded chunk count. Photograph all three pages.
Matching readback does not prove the right source sector or subsequent graphics
processing. No independent golden asset data is available locally. This build
is for actual measurements, not for gameplay/save testing. No texture fix or
free-RAM/prefetch capacity is claimed. Normal restore retains the previous VMU
behavior, including its unresolved texture issue. Direct RPG routes are unchanged.

The source patch and linked/source-bound validation evidence are included.
No commercial executable, texture asset or private save identity is included.
'''.encode()
    for name in ('vc2_texture_probe.c','vc2_texture_probe_entry.S'):
        files['source/'+name]=(ROOT/'src/loader'/name).read_bytes()
    files['source/vc2_texture_probe.h']=(ROOT/'include/kui/vc2_texture_probe.h').read_bytes()
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.output,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for name,data in files.items():z.writestr(name,data)
    with zipfile.ZipFile(args.output) as z,tempfile.TemporaryDirectory(prefix='vc2-payload-extract-') as temp:
        if z.testzip() is not None:raise ValueError('ZIP CRC failure')
        z.extractall(temp)
        for name,data in files.items():
            if (Path(temp)/name).read_bytes()!=data:raise ValueError('Extracted bytes differ')
    print(json.dumps(dict(status='PASS',file=str(args.output),bytes=args.output.stat().st_size,
                         sha256=sha(args.output.read_bytes()),commit=commit,
                         main_included=main_commit,extract_verified=True)))


if __name__=='__main__':main()
