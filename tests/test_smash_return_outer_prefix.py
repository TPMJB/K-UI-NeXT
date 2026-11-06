#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute native return plus the complete linked outer prefix to Cmain.

Original MDE bytes stay external. Raw SD and worker retirement are authored
boundaries; the return body, parser/setters, cold entry and copied stage
prefix execute actual supplied ELF instructions. No hardware success claimed.
"""
from pathlib import Path
import argparse,sys,struct,zlib,json,types,hashlib
R=Path(__file__).resolve().parents[1];sys.path[:0]=[str(R/'tests'),str(R/'tools')]
from check_vmu_native_worker_gateway import read_elf
from runtime_package import flatten_elf
from test_smash_menu_return_runtime import prepare,TARGET,RAW,RETIRE,phase_video_flow,PVR_DIAGNOSTIC_READS,native_ram_clear_flow,NATIVE_POISON,NATIVE_POISON_BYTES
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--native',type=Path,required=True)
parser.add_argument('--outer',type=Path,required=True)
parser.add_argument('--mde',type=Path,required=True)
parser.add_argument('--report',type=Path,required=True)
a=parser.parse_args();native,outer,out=a.native,a.outer,a.report;mde=a.mde.read_bytes()
assert len(mde)==1848288 and hashlib.sha256(mde).hexdigest()=='04f49d583b39bdc710cec6d0a434b5d710b9bccedd539bedda29755135f0fe61'
ret=read_elf(native/'return-loader.elf');stage=read_elf(outer/'stage.elf')
payload,_=flatten_elf((outer/'entry.elf').read_bytes());full=len(payload);data=payload+bytes((-full)%512)
seed=0x2000+stage[4]['__smash_menu_return_seed'][0]-0x8ce00000
assert data[seed:seed+16]==struct.pack('<4I',0x5349554b,0x3154524d,1,368)
video=phase_video_flow(ret[4])
if video:
 assert stage[4]['__smash_menu_return_video_seed'][0]==stage[4]['__smash_menu_return_seed'][0]+368
 assert data[seed+368:seed+448]==struct.pack('<4I',0x5349554b,0x31444956,1,80)+bytes(64)
cpu,*_=prepare(ret[0],ret[2],ret[4],mde,'sf_return')
cpu.capsule=data
bss=stage[4]['__retail_stage_bss_begin'][0];bss_end=stage[4]['__retail_stage_bss_end'][0]
cpu.store_block(bss,bytes([0xa5])*(bss_end-bss))
# Real complete linked outer body, not the old synthetic2048B stop-at-target fixture.
plan=[0x5349554b,0x31444c4d,1,64,full,len(data),zlib.crc32(data),seed,100,1,200,len(data)//512,0,0,0,0]
plan[-1]=zlib.crc32(struct.pack('<16I',*plan))
runtime=ret[4]['_kui_smash_menu_return_runtime'][0];n=ret[4]['_kui_smash_menu_return_runtime'][1]
assert n==516+80*video,'This composed fixture requires the shared-card native runtime ABI'
wire=bytearray(cpu.block(runtime,n));wire[32:96]=struct.pack('<16I',*plan);wire[28:32]=bytes(4)
wire[28:32]=struct.pack('<I',zlib.crc32(wire[:368]));wire[412:416]=bytes(4)
begin=ret[4]['__smash_menu_return_begin'][0];end=ret[4]['__smash_menu_return_binary_end'][0]
initialized=bytearray(cpu.block(begin,end-begin));initialized[runtime-begin:runtime-begin+n]=bytes(n)
wire[412:416]=struct.pack('<I',zlib.crc32(wire,zlib.crc32(initialized)))
cpu.store_block(runtime,wire)
def raw_read(self):
 self.check_native_clear_before_read()
 lba,dst,count=self.r[4:7];self.raw_calls.append((lba,dst,count))
 logical=0 if lba==100 else lba-200+1
 assert 0<=logical and logical+count<=len(data)//512
 self.store_block(dst,data[logical*512:(logical+count)*512]);self.r[0]=0;self.pc=self.pr
cpu.raw_read=types.MethodType(raw_read,cpu)
helpers={0x8c1598c0,0x8c1ad2c8,0x8c1ad31a};hit_target=None;main=stage[4]['_kui_retail_stage_main'][0]
low_before=None;incoming_before=None
for _ in range(5000000):
 if cpu.pc==RETIRE:cpu.retire_calls+=1;cpu.retired_success=True;cpu.r[0]=1;cpu.pc=cpu.pr
 if cpu.pc in helpers:cpu.original_helpers.append(cpu.pc)
 if cpu.pc==ret[4]['_kui_smash_return_progress'][0] and cpu.r[4]>=0x100:
  cpu.return_progress.append((cpu.r[4],cpu.r[5]))
 if cpu.pc in (RAW,RAW-0x20000000):cpu.raw_read()
 if cpu.pc==TARGET and hit_target is None:
  hit_target=cpu.instructions
  assert cpu.return_progress==[(0x101,0),(0x105,100),(0x106,0),(0x107,0),(0x108,0),(0x109,0),(0x10a,0)]
  assert cpu.read(ret[4]['_kui_smash_menu_return_diagnostic'][0]+12)==0x10a
  progress=ret[4]['_kui_smash_return_progress_state'];assert progress[1]==8
  assert cpu.block(progress[0],8)==struct.pack('<2I',0x10a,0)
  low_before=cpu.block(0x8c008100,0x3f00);incoming_before=cpu.block(TARGET,len(data))
  assert cpu.block(TARGET,0x2a0)==data[:0x2a0] and cpu.block(TARGET+0x300,seed-0x300)==data[0x300:seed]
 if cpu.pc==main:break
 cpu.step()
else:raise AssertionError('actual return + actual complete outer prefix failed to reach Cmain')
assert hit_target is not None and cpu.r[4]==0x8c011000 and cpu.r[15]==0x8cff0000
if native_ram_clear_flow(ret[4]):
 assert cpu.native_ram_clear_calls==1 and cpu.native_ram_clear_low_exact
 assert cpu.native_ram_clear_at<hit_target and cpu.raw_calls
 # The freshly copied outer stage can own high RAM again; the retired GIN
 # upper-window remains zero through its actual prefix to Cmain.
 assert not any(cpu.block(NATIVE_POISON,NATIVE_POISON_BYTES))
assert not cpu.diagnostic_writes and not any(at in PVR_DIAGNOSTIC_READS for at in cpu.mmio_reads), \
 'Successful return touched PVR scanout rather than recording progress only in RAM'
assert cpu.block(0x8ce00000,len(payload)-0x2000)==cpu.block(0x8c012000,len(payload)-0x2000)
bss=stage[4]['__retail_stage_bss_begin'][0];bss_end=stage[4]['__retail_stage_bss_end'][0]
assert cpu.block(bss,bss_end-bss)==bytes(bss_end-bss)
assert cpu.block(0x8c008100,0x3f00)==low_before
assert cpu.block(TARGET,len(data))==incoming_before
# The copied stage contains the captured return seed. Its live receipt stays
# in the low entry reservation where Cmain will later import it.
assert cpu.block(stage[4]['__smash_menu_return_seed'][0],368)==cpu.block(TARGET+seed,368)
assert cpu.block(TARGET+0x2a0,96)==wire[420:516]
if video:
 assert cpu.block(TARGET+seed+368,80)==wire[516:596]
 assert cpu.block(stage[4]['__smash_menu_return_video_seed'][0],80)==wire[516:596]
result={'status':'PASS','kind':'Actual linked native return + real full outer prefix through actual Cmain; raw SD/retirement are explicit authored boundaries',
 'native_ram_clear_calls':cpu.native_ram_clear_calls,'native_ram_clear_low64KiB_exact':cpu.native_ram_clear_low_exact,
 'native_ram_clear_before_outer':bool(cpu.native_ram_clear_at is not None and cpu.native_ram_clear_at<hit_target),
 'linked_native_ram_clear':cpu.native_ram_clear_leaf,
 'old_FD_window_zero_through_Cmain':not any(cpu.block(NATIVE_POISON,NATIVE_POISON_BYTES)),
 'native_build':str(native),'outer_build':str(outer),'outer_payload_bytes':full,'padded_bytes':len(data),'return_seed_offset':seed,
 'outer_entry_sha256':hashlib.sha256((outer/'entry.elf').read_bytes()).hexdigest(),
 'native_return_sha256':hashlib.sha256((native/'return-loader.elf').read_bytes()).hexdigest(),
 'actual_instructions_to_return_target':hit_target,'actual_instructions_through_outer_prefix':cpu.instructions,
 'stage_entry':hex(0x8ce00000),'Cmain':hex(main),'map_argument':hex(cpu.r[4]),'stack':hex(cpu.r[15]),
 'complete_stage_blob_exact':True,'complete_stage_BSS_zero':True,'actual_captured_return_seed_copied':True,
 'live_receipt_carried':True,'last_return_progress_before_outer_prefix':'0x10a',
 'successful_return_progress_RAM_only':True,'PVR_scanout_reads':0,'PVR_border_writes':0,
 'original_KOS_video_carried_into_outer_stage':video,
 'return_progress':[dict(phase=hex(p),detail=hex(v))for p,v in cpu.return_progress],
 'low_return_island_and_stack_unchanged_by_outer_prefix':True,
 'incoming_outer_capsule_unchanged_by_outer_prefix':True,'raw_calls':[dict(lba=l,destination=hex(d),blocks=n)for l,d,n in cpu.raw_calls],
 'limits':['Native RAM-clear bulk effect is used only after its independently executed complete linked-leaf proof.','No physical SD/IRQ/cache behavior executed.','No Cmain body, prepared filesystem map/profile admission or controller menu re-entry executed.','This fixture does not localize a physical raw-transfer or post-Cmain hardware failure.']}
out.parent.mkdir(parents=True,exist_ok=True)
out.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps(dict(status='PASS',report=str(out),outer_payload_bytes=full,actual_instructions=cpu.instructions,last_return_progress='0x10a')))
