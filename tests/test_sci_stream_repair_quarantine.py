#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Refuse lost-byte DMA blocks using real SCI and reader production paths.

The SCI/card/CPU boundary is modeled. No CRC result, production function or
payload output is replaced. Neither a CE kernel nor console hardware runs.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
ROOT=Path(__file__).resolve().parents[1]
OLD_SHA="b57e1efdc25e1af74922b5d65da6e9f0370f289129f26b0bcfac6b092961dc41"
NEW_GUARD="""        /* A reconstructed byte consumes eight bits of the CRC's evidence:
         * broader receive damage can then satisfy the remaining checksum.
         * Never publish a block whose DMA reception lost a byte. Keep the
         * existing CRC failure/retry path so the same block is read afresh,
         * with bounded programmed-I/O fallback under repeated contention. */
        if(((uint32_t)crc ^ sent) | lost) {"""
FLAGS=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wpedantic',
 '-fno-pie','-no-pie','-fsanitize=address,undefined','-fno-sanitize-recover=all',
 '-fno-omit-frame-pointer','-Iinclude','-Isrc/loader','-Itests']
PROFILES={'native':[], 'compact-mmu':['KUI_NATIVE_OCRAM_BOOTSTRAP=1',
 'KUI_NATIVE_GD_LEGACY_STREAM=1','KUI_NATIVE_GD_MMU_BUFFERS=1',
 'KUI_NATIVE_GD_DMA_STREAM=1','KUI_NATIVE_GD_PIO_STREAM=1','KUI_NATIVE_GD_VRAM_DMA=1'],
 'ce':['KUI_RETAIL_CE=1']}
def sha(p):return hashlib.sha256(Path(p).read_bytes()).hexdigest()
def run(cmd):
 p=subprocess.run(cmd,cwd=ROOT,text=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE,
  env=dict(os.environ,ASAN_OPTIONS='detect_leaks=0',UBSAN_OPTIONS='halt_on_error=1'))
 if p.returncode or p.stderr:raise AssertionError(' '.join(map(str,cmd))+'\n'+p.stdout+p.stderr)
 return p.stdout

def main():
 a=argparse.ArgumentParser(description=__doc__);a.add_argument('--cc',default='cc');a.add_argument('--report',type=Path);args=a.parse_args()
 cc=shutil.which(args.cc)
 if not cc:raise RuntimeError('C compiler unavailable: '+args.cc)
 names=['src/loader/sci_stream.c','src/loader/sci_stream.h','tests/test_sci_stream_repair_quarantine.c',
  'tests/test_sci_stream.c','tests/sci_stream_model.h','tests/test_native_pio_repair_quarantine.c',
  'tests/test_retail_async_foreground.c','src/loader/retail_async.c','src/core/retail_cursor.c',
  'src/core/retail_image.c','src/core/retail_gd.c']
 before={n:sha(ROOT/n)for n in names}
 current=(ROOT/names[0]).read_text()
 if current.count(NEW_GUARD)!=1:raise AssertionError('Exact quarantine guard changed; audit the reference control')
 # CRC table factoring is independently exhaustively equivalent. Restore its
 # exact, complete-source-hash-bound predecessor before reconstructing this
 # older quarantine witness; retain the original historical hash below.
 from test_ce_crc16_factoring import fragments
 _,pre_crc,_=fragments()
 old=pre_crc.replace(NEW_GUARD,'        if(crc != sent) {').encode()
 if hashlib.sha256(old).hexdigest()!=OLD_SHA:raise AssertionError('Full historical SCI source hash differs')
 report=dict(schema='kui.sci.lost-byte-quarantine.v1',status='FAIL',source_sha256=before,
  baseline_sci_sha256=OLD_SHA,profiles={},hardware_executed=False,
  limits=['SCI/card, CPU mapping and DMA register behavior use authored fixtures.',
   'Production SCI CRC/reconstruction, native reader cursor and GD progress execute.',
   'No CE kernel or complete game runs; modeled wire corruption demonstrates the defect, not a capture of the exact extra corrupted console bytes.'])
 try:
  with tempfile.TemporaryDirectory(prefix='sci-quarantine-')as temp:
   d=Path(temp);reference=d/'old-sci.c';reference.write_bytes(old)
   for name,defs in PROFILES.items():
    rows={}
    for variant,source,mode in [('baseline',reference,['--legacy-unsafe-control']),('current',ROOT/names[0],[])]:
     exe=d/(name+'-'+variant);cmd=[cc,*FLAGS,'-DKUI_SCI_STREAM_TEST=1',*['-D'+x for x in defs],str(source),names[2],'-o',str(exe)]
     run(cmd);rows[variant]=json.loads(run([str(exe),*mode]))
    report['profiles'][name]=rows
   report['retained_sci']={}
   for name in ('native','ce'):
    exe=d/('retained-'+name);run([cc,*FLAGS,'-DKUI_SCI_STREAM_TEST=1',*['-D'+x for x in PROFILES[name]],names[0],names[3],'-o',str(exe)])
    report['retained_sci'][name]=dict(output=run([str(exe)]).strip(),passed=True)
   sys.path.insert(0,str(ROOT/'tools'))
   import run_native_standalone_foreground as foreground
   defs=[*foreground.DEFINITIONS,*PROFILES['compact-mmu']]
   sources=['src/loader/retail_async.c','src/core/retail_cursor.c','src/core/retail_image.c','src/core/retail_gd.c','tests/test_native_pio_repair_quarantine.c']
   report['native_pio_reader']={}
   for variant,source,mode in [('baseline',reference,['--legacy-unsafe-control']),('current',ROOT/names[0],[])]:
    exe=d/('reader-'+variant)
    run([cc,*FLAGS,'-Wno-unused-function',*['-D'+x for x in defs],str(source),*sources,'-Wl,--wrap=kui_sci_stream_stop','-Wl,--wrap=kui_retail_cursor_feed','-o',str(exe)])
    report['native_pio_reader'][variant]=json.loads(run([str(exe),*mode]))
  if before!={n:sha(ROOT/n)for n in names}:raise AssertionError('Test inputs changed during execution')
  report['status']='PASS'
 finally:
  if args.report:
   args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n')
 print('PASS lost-byte quarantine: old source accepts wrong CRC-matching header; native/compact/CE refuse and verify a fresh block; native PIO reader recovers automatically')
if __name__=='__main__':main()
