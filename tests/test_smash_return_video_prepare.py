#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute native return preparation, original VID1 validation and full CRC.

Launch metadata, original KOS video and firmware words are authored inputs.
No preparation, video validation, memcpy or CRC helper is substituted.
"""
import argparse,hashlib,json,struct,sys,zlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools')]
from check_vmu_native_worker_gateway import read_elf
from test_smash_menu_return_runtime import prepare,FIRMWARE,FIRMWARE_ADDRESSES,phase_video_seed

def audit(build,mde_path):
    gate=read_elf(build/'return-loader.elf');stage=read_elf(build/'stage.elf');s=stage[4]
    assert gate[4].get('__kui_smash_return_phase_video_transport')==(1,0,0,0xfff1)
    assert gate[4]['_kui_smash_menu_return_runtime'][1]==s['_smash_return_prepared'][1]==596
    mde=mde_path.read_bytes()
    assert hashlib.sha256(mde).hexdigest()=='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
    code=(build/'return-loader.bin').read_bytes();raw=(build/'smash-return-raw.bin').read_bytes()
    original=phase_video_seed();rows=[]
    for kind in ('original_VID','source_VID_bad_checksum','source_VID_bad_shape_with_valid_checksum'):
        cpu,*_=prepare(gate[0],gate[2],gate[4],mde,'sf_return')
        wire=cpu.block(gate[4]['_kui_smash_menu_return_runtime'][0],596)
        for part in stage[2]:
            if part[0]==1:
                cpu.store_block(part[2],stage[0][part[1]:part[1]+part[4]])
                cpu.store_block(part[2]+part[4],bytes(part[5]-part[4]))
        cpu.store_block(s['__smash_menu_return_seed'][0],wire[:368])
        cpu.store_block(s['_smash_card_receipt'][0],wire[420:516])
        manifest=bytearray(s['_manifest'][1])
        struct.pack_into('<QQQ',manifest,0,1024,1,1024)
        struct.pack_into('<I',manifest,40,len(mde));struct.pack_into('<I',manifest,60,0x18efc93a)
        cpu.store_block(s['_manifest'][0],manifest)
        cpu.store_block(s['_boot_crc'][0],struct.pack('<I',zlib.crc32(mde)))
        for address,value in zip(FIRMWARE_ADDRESSES,FIRMWARE):cpu.store_block(address,struct.pack('<I',value))
        source=bytearray(original)
        if kind=='source_VID_bad_checksum':source[36]^=1
        if kind=='source_VID_bad_shape_with_valid_checksum':
            struct.pack_into('<I',source,24,0x200000)
            source[72:76]=bytes(4);struct.pack_into('<I',source,72,zlib.crc32(source))
        cpu.store_block(s['__smash_phase_video_seed'][0],source)
        low=cpu.block(0xac008100,0x3f00);owner=cpu.block(0x8c010000,len(mde))
        cpu.pc=s['_kui_smash_native_return_stage_prepare'][0];cpu.pr=0x8c003000;cpu.r[15]=0x8cff0000;cpu.sr=0x700000f0
        start=cpu.instructions;saved=cpu.r[8:15].copy()
        for _ in range(5000000):
            if cpu.pc==0x8c003000:outcome='prepared';break
            if cpu.pc==s['_stopped'][0]:outcome='refused';break
            cpu.step()
        else:raise AssertionError('Actual native return preparation exceeded finite instruction bound')
        assert cpu.block(0xac008100,0x3f00)==low and cpu.block(0x8c010000,len(mde))==owner
        assert cpu.block(s['__smash_phase_video_seed'][0],80)==source
        assert outcome==('prepared' if kind=='original_VID' else 'refused')
        if outcome=='prepared':
            assert cpu.r[15]==0x8cff0000 and cpu.r[8:15]==saved
            record=bytearray(cpu.block(s['_smash_return_prepared'][0],596))
            assert record[:368]==wire[:368] and list(struct.unpack_from('<9I',record,368))==FIRMWARE
            assert record[420:516]==wire[420:516] and record[516:596]==original
            assert struct.unpack_from('<I',record,416)[0]==zlib.crc32(raw)
            actual=struct.unpack_from('<I',record,412)[0];record[412:416]=bytes(4)
            assert actual==zlib.crc32(record,zlib.crc32(code))
        rows.append(dict(case=kind,outcome=outcome,actual_instructions=cpu.instructions-start,
                         original_source_unchanged=True,low_return_not_published=True))
    return dict(status='PASS',stage_elf_sha256=hashlib.sha256(stage[0]).hexdigest(),
                return_elf_sha256=hashlib.sha256(gate[0]).hexdigest(),runtime_bytes=596,
                original_video_tail_offset=516,original_video_bytes=80,cases=rows,
                complete_runtime_CRC_includes_original_video=True,
                limits=['Launch metadata, KOS register capture and firmware words are authored fixtures.',
                        'Actual linked preparation/VID validation/CRC execute; publication, worker and hardware do not.'])

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--build',type=Path,required=True)
    p.add_argument('--mde',type=Path,required=True);p.add_argument('--report',type=Path,required=True);a=p.parse_args()
    report=audit(a.build,a.mde);a.report.parent.mkdir(parents=True,exist_ok=True)
    a.report.write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
