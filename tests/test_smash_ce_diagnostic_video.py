#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Execute CE GIN's real early display selection and the terminal renderer.

The actual source function is extracted without rewriting its control/data
logic. Its SH SR read and fixed native GIN/IP addresses are authored CPU/RAM
boundaries. PVR/VRAM are unmapped during quiet adoption and later mapped to
host RAM for the stopped-renderer check; no Dreamcast hardware executes.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT=Path(__file__).resolve().parents[1]
STAGE='src/loader/smash_ce_gin_stage.inc'
MARKER='static void __attribute__((noipa)) smash_ce_gin_begin(void) {'
SR='uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr)::"memory");'
BODY=r'''
#define _GNU_SOURCE 1
#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include "kui/smash_ce_gin.h"
#include "kui/smash_phase_video.h"
#include "kui/retail_image.h"
#include "retail_display.h"
unsigned row;
static struct kui_smash_ce_gin_seed input,smash_ce_gin_saved;
static uint32_t smash_ce_gin_ip_crc,host_sr,host_profile,ip_checks,video_checks;
static struct retail_display_state display;
static volatile struct kui_smash_phase_video_seed smash_ce_return_video_seed;
static uint8_t ip[256];
static jmp_buf failure;
static const char *stop_reason;
static unsigned mutate_after_check;
#define KUI_RETAIL_EXEC_ADDRESS ((uintptr_t)&input-KUI_SMASH_CE_GIN_OFFSET)
#define SMASH_CE_CHILD_PROFILE host_profile
static void stopped(const char *message,uint32_t detail) {
 (void)detail;stop_reason=message;longjmp(failure,1);
}
static uint32_t smash_ce_gin_retained_ip_crc(void) {
 ++ip_checks;return kui_retail_crc32(0,ip,sizeof(ip));
}
extern _Bool __real_kui_smash_phase_video_valid(const struct kui_smash_phase_video_seed *);
_Bool __wrap_kui_smash_phase_video_valid(const struct kui_smash_phase_video_seed *seed) {
 ++video_checks;
 assert((const void *)seed!=(const void *)&smash_ce_return_video_seed);
 _Bool valid=__real_kui_smash_phase_video_valid(seed);
 if(mutate_after_check) {
  smash_ce_return_video_seed.regs[2]=0x100000u;
  smash_ce_return_video_seed.crc32^=1u;
 }
 return valid;
}
FUNCTION
#if !EXPECT_OLD && KUI_SMASH_CE_RETURN
static void reseal(struct kui_smash_phase_video_seed *v) {
 const uint32_t zero=0;
 v->crc32=kui_retail_crc32(0,v,offsetof(struct kui_smash_phase_video_seed,crc32));
 v->crc32=kui_retail_crc32(v->crc32,&zero,sizeof(zero));
 v->crc32=kui_retail_crc32(v->crc32,&v->reserved,sizeof(v->reserved));
}
#endif
static struct kui_smash_phase_video_seed good(unsigned interlace) {
 uint32_t regs[14]={0};regs[0]=5;regs[1]=1;
 regs[3]=interlace?1280u:0;regs[6]=interlace?16u:0;
 regs[4]=319u|((interlace?239u:479u)<<10)|((interlace?321u:1u)<<20);
 struct kui_smash_phase_video_seed v;
 assert(kui_smash_phase_video_capture(&v,regs));
 return v;
}
static struct retail_display_state inherited(void) {
 struct retail_display_state state={0};
 state.regs[0]=5;state.regs[1]=1;state.regs[2]=0x100000u;
 state.regs[3]=0x100000u;state.regs[4]=159u|(239u<<10)|(1u<<20);
 return state;
}
static void prepare(unsigned profile,struct kui_smash_phase_video_seed v) {
 input=(struct kui_smash_ce_gin_seed){.magic0=KUI_SMASH_CE_GIN_MAGIC0,
  .magic1=KUI_SMASH_CE_GIN_MAGIC1,.version=1,.bytes=sizeof(input),.profile=profile};
 uint32_t frame[21]={0};frame[0]=0x28;frame[2]=0x8c000000;
 frame[3]=0x8c00f400;frame[4]=0x700000f0;
 assert(kui_smash_ce_gin_capture(&input,frame,KUI_SMASH_CE_GIN_FRAME_P1,0x21));
 host_sr=0x700000f0;host_profile=profile;
 smash_ce_return_video_seed=v;display=inherited();
 stop_reason=NULL;ip_checks=video_checks=0;mutate_after_check=0;
 retail_display_adoption_quiet(1);
}
static void *mapping(uintptr_t at,size_t bytes) {
 void *p=mmap((void *)at,bytes,PROT_READ|PROT_WRITE,
  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 if(p==MAP_FAILED){perror("isolated diagnostic mapping");exit(2);}return p;
}
int main(void) {
 unsigned cases=0;
 for(unsigned profile=1;profile<=2;profile++)for(unsigned il=0;il<=1;il++) {
  struct kui_smash_phase_video_seed v=good(il);prepare(profile,v);
  if(setjmp(failure))abort();
  smash_ce_gin_begin();
  assert(ip_checks==1 && smash_ce_gin_ip_crc==kui_retail_crc32(0,ip,sizeof(ip)));
  assert(!memcmp(&input,&smash_ce_gin_saved,sizeof(input)));
#if EXPECT_OLD || !KUI_SMASH_CE_RETURN
  struct retail_display_state old=inherited();
  assert(!memcmp(display.regs,old.regs,sizeof(old.regs)) && !video_checks);
#else
  assert(!memcmp(display.regs,v.regs,sizeof(v.regs)) && video_checks==1);
#endif
  /* During successful adoption, any missed quiet guard would touch an
   * unmapped PVR address. No MMIO/VRAM map exists at this point. */
  retail_display_restore(NULL);retail_display_line("QUIET");
  cases++;
 }
#if !EXPECT_OLD && KUI_SMASH_CE_RETURN
 for(unsigned which=0;which<6;which++) {
  struct kui_smash_phase_video_seed v=good(0);
  if(which==0)v.crc32^=1;
  if(which==1){v.regs[4]=159u|(239u<<10)|(1u<<20);reseal(&v);}
  if(which==2){v.regs[2]=0x100000u;reseal(&v);}
  if(which==3){v.regs[4]=0xffffffffu;reseal(&v);}
  if(which==4){v.bytes+=4;reseal(&v);}
  if(which==5){v.reserved=1;reseal(&v);}
  prepare(1,v);struct retail_display_state old=display;
  if(!setjmp(failure)){smash_ce_gin_begin();abort();}
  assert(!strcmp(stop_reason,"SMASH CE DIAGNOSTIC VIDEO REFUSED"));
  assert(video_checks==1 && ip_checks==1 && !memcmp(&display,&old,sizeof(old)));
  cases++;
 }
 for(unsigned which=0;which<3;which++) {
  prepare(1,good(0));
  if(which==0)input.crc32^=1;
  if(which==1)host_sr^=0x10000000;
  if(which==2)host_profile=2;
  if(!setjmp(failure)){smash_ce_gin_begin();abort();}
  assert(!strcmp(stop_reason,"SMASH CE NEEDS ORIGINAL GIN CONTINUATION"));
  assert(!ip_checks && !video_checks);cases++;
 }
 prepare(1,good(0));struct kui_smash_phase_video_seed checked=good(0);
 mutate_after_check=1;
 if(setjmp(failure))abort();
 smash_ce_gin_begin();
 assert(video_checks==1 && smash_ce_return_video_seed.regs[2]==0x100000u);
 assert(!memcmp(display.regs,checked.regs,sizeof(checked.regs)));cases++;
#endif
 /* The actual stopped renderer exposes the original mismatch: it restores
  * inherited scanout1MiB/320px yet draws640RGB565 at framebuffer0. */
 prepare(1,good(0));
 if(setjmp(failure))abort();
 smash_ce_gin_begin();
 volatile uint32_t *regs=mapping(0xa05f8000u,4096u);
 uint16_t *vram=mapping(0xa5000000u,0x800000u);
 for(unsigned i=0;i<0x400000u;i++)vram[i]=0x1357;
 retail_display_adoption_quiet(0);retail_display_restore(&display);
 assert(vram[0]==0x0864);
#if EXPECT_OLD || !KUI_SMASH_CE_RETURN
 assert(regs[0x50/4]==0x100000u && vram[0x100000/2]==0x1357);
#else
 assert(regs[0x50/4]==0 && vram[0]==0x0864);
#endif
 printf("{\"cases\":%u,\"old_fault_frame_invisible\":%s,\"return_profile\":%u}\n",
  cases+1,(EXPECT_OLD||!KUI_SMASH_CE_RETURN)?"true":"false",KUI_SMASH_CE_RETURN);
 return 0;
}
'''

def sha(body):return hashlib.sha256(body).hexdigest()

def begin(source):
    at=source.index(MARKER);end=source.index('\n}\n',at)+3
    body=source[at:end]
    if body.count(SR)!=1:raise ValueError('Native SR capture changed')
    return body.replace(SR,'uint32_t sr=host_sr;')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--negative-source',type=Path)
    parser.add_argument('--report',type=Path)
    args=parser.parse_args()
    sources=[('current',(ROOT/STAGE).read_bytes(),False)]
    if args.negative_source:sources.append(('old',args.negative_source.read_bytes(),True))
    report=dict(schema='kui.smash.ce-diagnostic-video.v1',status='FAIL',
        cpu_and_MMIO_modeled=True,console_verified=False,runs=[],
        production_source_sha256={p:sha((ROOT/p).read_bytes())for p in
          (STAGE,'src/loader/retail_display.c','src/core/smash_ce_gin.c','src/core/smash_phase_video.c','src/core/retail_image.c')})
    with tempfile.TemporaryDirectory(prefix='kui-ce-diagnostic-video-') as temporary:
        directory=Path(temporary)
        for name,source,old in sources:
            for returning in (1,0):
                src=directory/(name+str(returning)+'.c');binary=src.with_suffix('')
                src.write_text(BODY.replace('FUNCTION',begin(source.decode())))
                flags=['-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
                    '-Wno-clobbered','-ffunction-sections','-fdata-sections',
                    '-Wl,--gc-sections','-fno-pie','-no-pie','-fsanitize=undefined',
                    '-DKUI_SMASH_CE_GIN_ADOPT=1','-DKUI_VC2_CE_VMU=1',
                    '-DKUI_NATIVE_GD_DMA_STREAM=1','-DKUI_RETAIL_ASYNC=1',
                    '-DKUI_BUILD_ID="diagnostic-video-test"',
                    '-DKUI_SMASH_CE_RETURN='+str(returning),'-DEXPECT_OLD='+str(int(old)),
                    '-Iinclude','-Isrc/loader','-Wl,--wrap=kui_smash_phase_video_valid']
                subprocess.run([*shlex.split(os.environ.get('HOST_CC','cc')),*flags,str(src),
                    'src/loader/retail_display.c','src/core/smash_ce_gin.c',
                    'src/core/smash_phase_video.c','src/core/retail_image.c','-o',str(binary)],
                    cwd=ROOT,check=True)
                result=subprocess.run([str(binary)],check=True,capture_output=True,text=True,
                    env={**os.environ,'UBSAN_OPTIONS':'halt_on_error=1'})
                report['runs'].append(dict(name=name,
                    source_sha256=sha(source),**json.loads(result.stdout)))
    report['status']='PASS'
    text=json.dumps(report,indent=2)+'\n'
    if args.report:args.report.write_text(text)
    print(text,end='')

if __name__=='__main__':main()
