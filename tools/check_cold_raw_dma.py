#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Bind cold raw DMA hydration to its unchanged ordinary-reader reservation.

The reference is a separately retained reader from before the cold gate's
receive-policy change. Only its printed twelve-hex-digit build label may
differ. Host tests establish DMA/refusal/CRC semantics separately.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
from check_vmu_native_worker_gateway import read_elf,symbol_bytes
from check_smash_menu_loader import combined_stacks

def require(ok,message):
    if not ok:raise ValueError(message)
def sha(data):return hashlib.sha256(data).hexdigest()
def ordinary(image):
    rows=[p for p in image[2]if p[0]==1 and p[2]==0x8c004000]
    require(len(rows)==1,'Reader lacks its unique normal low segment')
    p=rows[0];body=image[0][p[1]:p[1]+p[4]]
    labels=re.findall(rb'BUILD ([0-9a-f]{12})\x00',body)
    require(len(labels)==1 and body.count(labels[0])==1,'Ambiguous normal reader build label')
    return body,body.replace(labels[0],b'0'*12),labels[0].decode()
def audit(build,reference):
    build,reference=Path(build),Path(reference)
    path=build/'resident-sci.elf';image=read_elf(path);old=read_elf(reference)
    before={p:sha(p.read_bytes())for p in (path,build/'handoff.elf')}
    raw_at,raw,section=symbol_bytes(image,'_kui_vc2_ce_vmu_read_card',2)
    require(raw_at==0xac008100 and section[1]==1 and section[2]&7==6 and len(raw)<=512,
        'Cold hydration gate escapes its existing executable raw island')
    # GCC lowers the source's 200000-poll for loop to a 200001 countdown:
    # its first DT is before the first call and zero skips that call.
    require(struct.pack('<I',200001) in raw and struct.pack('<H',0x4e10) in raw,
        'Cold DMA polling lacks its reviewed countdown literal and DT r14')
    whole,normalized,label=ordinary(image);old_whole,old_normalized,old_label=ordinary(old)
    require(normalized==old_normalized,'Ordinary GD reader changed beyond its printed build label')
    symbols=image[4];end=symbols['__retail_resident_bss_end'][0]
    bottom=symbols['__retail_hook_stack_bottom'][0];top=symbols['__retail_hook_stack'][0]
    require(end<=bottom==0x8c007ea0 and top==0x8c008000,'Normal low code/state or352-byte stack grew')
    require(all(symbols[name][0]==old[4][name][0] for name in (
        '__retail_resident_bss_end','__retail_hook_stack_bottom','__retail_hook_stack')),
        'Cold hydration altered ordinary low state or stack boundaries')
    stacks=combined_stacks(build)
    require(stacks['low_deepest_bytes']+stacks['library_and_guard_reserve']<=352,
        'Ordinary GD/redirect stack exceeds its existing reservation')
    require(stacks['handoff_deepest_bytes']+stacks['library_and_guard_reserve']+
        stacks['top_reserved_bytes']<=2048,'Cold hydration caller exceeds its existing handoff stack')
    require(before=={p:sha(p.read_bytes())for p in before},'Linked input changed during cold DMA audit')
    return dict(schema='kui.cold-raw-dma.linked.v1',status='PASS',
        resident_elf_sha256=before[path],handoff_elf_sha256=before[build/'handoff.elf'],
        reference_elf_sha256=sha(old[0]),build_label=label,reference_build_label=old_label,
        raw_gate_address=hex(raw_at),raw_gate_bytes=len(raw),raw_gate_budget=512,
        maximum_dma_polls_per_block=200000,linked_countdown_literal=200001,
        raw_gate_sha256=sha(raw),normal_reader_bytes=len(whole),
        normal_reader_sha256=sha(whole),reference_normal_reader_sha256=sha(old_whole),
        normalized_normal_reader_sha256=sha(normalized),ordinary_reader_identical_except_build=True,
        normal_bss_end=hex(end),normal_spare_bytes=bottom-end,normal_stack_bytes=top-bottom,
        stack_proof=stacks,hardware_timing_measured=False,
        limits='Linked layout and unchanged ordinary code; SCI/card register semantics use the separate production-C host audit.')
def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--build',required=True,type=Path)
    parser.add_argument('--reference',required=True,type=Path);parser.add_argument('--report',type=Path)
    args=parser.parse_args();result=audit(args.build,args.reference);text=json.dumps(result,indent=2)+'\n'
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(text)
    print(text,end='')
if __name__=='__main__':main()
