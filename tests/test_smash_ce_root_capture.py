#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute ROOT capture and compare its IP with original pure GINS setters.

Exact original files remain external inputs. The whole original image has
its existing owner/relay proofs; this test checks the new late ROOT boundary
without substituting its capture, seed CRC, or original source setters.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'tests')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_phase_video_publish import PublishCPU
from test_smash_menu_loader_runtime import returning_seed
from test_smash_menu_handoff_gin import (Gin,LOAD,MENU_CONTEXT,FOLDER_SETTER,
    MODE_SETTER,original_call)
from test_smash_native_gin import OWNERS

def sha(data):return hashlib.sha256(data).hexdigest()
def audit(build,originals):
    build,originals=Path(build),Path(originals)
    elf=read_elf(build/'stage.elf');s=elf[4]
    data={name:(originals/name).read_bytes() for name in ('1ST_READ.BIN','GINXFER.BIN')}
    assert all(sha(value)==OWNERS[name] for name,value in data.items())
    menu,gin=data['1ST_READ.BIN'],data['GINXFER.BIN']
    context=menu[MENU_CONTEXT-LOAD:MENU_CONTEXT-LOAD+8192]
    assert len(context)==8192 and struct.unpack_from('<I',context,24)==(4,) and not context[0x340]
    ip=(originals/'IP.BIN').read_bytes()[0xf0:0x100]
    assert ip==b' '*16
    incoming=bytearray(ip);incoming[12]&=~0x20 # actual cold-stage BOOT2 flag edit
    rows=[]
    for label,argv,raw_ip in (
        ('cold_original_ROOT',context[256:512],bytes(incoming)),
        ('returned_ROOT_1ST_READ',b'\\1ST_READ.BIN\0'.ljust(256,b'\0'),b' '*12+b'\1\4\xa7\xb8'),
        ('prior_child_folder_cleared',context[256:512],b'SWIRL'.ljust(12,b' ')+b'\1\4\x5a\xa5')):
        source=Gin(gin,menu,b'');source.store_block(LOAD,menu)
        source.store_block(0x8c0080f0,raw_ip)
        original_call(source,FOLDER_SETTER,[MENU_CONTEXT+0x340]);assert source.r[0]==0
        original_call(source,MODE_SETTER,[4]);assert source.r[0]==0
        expected_ip=source.block(0x8c0080f0,16)
        assert expected_ip==b' '*12+b'\1\4'+raw_ip[14:]
        cpu=PublishCPU(elf,0xac00b000)
        cpu.store_block(MENU_CONTEXT,context);cpu.store_block(MENU_CONTEXT+256,argv)
        cpu.store_block(0x8c0080f0,raw_ip)
        at=s['_smash_menu_prepared'][0]+264
        cpu.store_block(at,returning_seed())
        assert cpu.invoke('_smash_menu_loader_root_capture')=='returned'
        assert cpu.block(at,368)==returning_seed(True,argv,expected_ip)
        assert cpu.block(0x8c0080f0,16)==raw_ip, 'Capture must not mutate live BOOT2/IP state'
        assert not cpu.hardware_reads
        rows.append(dict(case=label,instructions=cpu.instructions,
            exact_original_setters_IP=True,ROOT_argv_exact=True,live_IP_unchanged=True))
    for label,offset,value in (('non_ROOT_folder',0x340,ord('X')),('wrong_ROOT_mode',24,0)):
        cpu=PublishCPU(elf,0xac00b000);cpu.store_block(MENU_CONTEXT,context)
        cpu.store_block(0x8c0080f0,bytes(incoming));at=s['_smash_menu_prepared'][0]+264
        before=returning_seed();cpu.store_block(at,before);cpu.write(MENU_CONTEXT+offset,value,1)
        assert cpu.invoke('_smash_menu_loader_root_capture')=='refused'
        assert cpu.stop==('SMASH MENU ROOT CONTEXT CAPTURE REFUSED',1)
        assert cpu.block(at,368)==before
        rows.append(dict(case=label,refused_before_capture=True))
    return dict(schema='kui.smash-ce.root-capture.v1',status='PASS',
        stage_elf_sha256=sha(elf[0]),original_sha256={k:sha(v) for k,v in data.items()},
        source_ROOT_context='0x8c036a44',source_ROOT_argv='0x8c036b44',cases=rows,
        limits=['Executes the original pure setters and new linked ROOT capture; media, bootstrap2 and gameplay are separate proofs/hardware boundaries.'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',required=True,type=Path)
    p.add_argument('--originals',required=True,type=Path);p.add_argument('--report',type=Path)
    a=p.parse_args();text=json.dumps(audit(a.build,a.originals),indent=2)+'\n'
    if a.report:a.report.write_text(text)
    print(text,end='')
