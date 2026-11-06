#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Render the actual three-page probe terminal with the production font.

Linux guarded mappings replace PVR/VRAM. This checks layout and snapshot
stability, not Dreamcast video timings or physical framebuffer behavior.
"""
import argparse
import array
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

from PIL import Image

ROOT=Path(__file__).resolve().parents[1]


def function(source,signature):
    start=source.index(signature)
    at=source.index('{',start)+1
    depth=1
    while depth:
        depth+=(source[at]=='{')-(source[at]=='}')
        at+=1
    return source[start:at]


HARNESS=r'''
#define _GNU_SOURCE
#include "kui/vc2_texture_probe.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define retail_display_line renderer_line
#define retail_display_values renderer_values
#define retail_display_restore renderer_restore
#define retail_display_pause renderer_pause
#include "retail_display.c"
#undef retail_display_line
#undef retail_display_values
#undef retail_display_restore
#undef retail_display_pause

static struct kui_vc2_texture_probe_snapshot stopped,before;
static unsigned pages,lines,last_bottom,max_bottom;
static unsigned tops[32];
static char texts[32][50];
static const char *output;
static jmp_buf finished;
static void record(const char *text,unsigned top) {
    assert(lines<32u && strlen(text)<50u);
    assert(top+14u<=480u);
    if(lines)assert(top>=last_bottom+4u);
    tops[lines]=top;strcpy(texts[lines++],text);
    last_bottom=top+13u;
    if(last_bottom>max_bottom)max_bottom=last_bottom;
}
void retail_display_restore(const struct retail_display_state *display) {
    assert(display==&stopped.display);lines=0u;last_bottom=0u;
    renderer_restore(display);record("BUILD VISUALQA",20u);
}
void retail_display_line(const char *text) {
    record(text,row);renderer_line(text);
}
void retail_display_values(const char *legend,const uint32_t *values,unsigned count) {
    assert(count>0u && count<=5u);
    if(*legend)record(legend,row);
    char numeric[50];unsigned at=0u;
    for(unsigned i=0u;i<count;i++)
        at+=(unsigned)snprintf(numeric+at,sizeof(numeric)-at,"%08X ",values[i]);
    record(numeric,row+(*legend?18u:0u));renderer_values(legend,values,count);
}
void retail_display_pause(uint32_t frames) {
    assert(frames==480u && pages<3u && lines<25u);
    char header[50];snprintf(header,sizeof(header),"VC2 PROBE %u OF 3 %s",pages+1u,
        pages==0u?"DATA":pages==1u?"CONTEXT":"DMA");
    assert(!strcmp(texts[1],header));
    assert(!strcmp(texts[lines-1u],"PHOTO ALL 3 - POWER OFF"));
    assert(!memcmp(&before,&stopped,sizeof(before)));
    volatile const uint16_t *frame=(const volatile uint16_t *)(uintptr_t)0xa5000000u;
    unsigned pixels=0u,bands[32]={0};
    for(unsigned y=0u;y<480u;y++)for(unsigned x=0u;x<640u;x++) {
        if(frame[y*640u+x]!=0xffffu)continue;
        assert(x>=20u && x<612u);++pixels;
        unsigned found=0u;
        for(unsigned i=0u;i<lines;i++)if(y>=tops[i] && y<tops[i]+14u) {
            ++bands[i];++found;
        }
        assert(found==1u);
    }
    for(unsigned i=0u;i<lines;i++)assert(bands[i]>0u);
    char name[1024];snprintf(name,sizeof(name),"%s/page-%u.rgb565",output,pages+1u);
    FILE *file=fopen(name,"wb");assert(file);
    for(unsigned i=0u;i<640u*480u;i++) {
        uint16_t pixel=frame[i];assert(fwrite(&pixel,2u,1u,file)==1u);
    }
    assert(!fclose(file));
    printf("{\"page\":%u,\"lines\":%u,\"glyph_bottom\":%u,\"bottom_clearance\":%u,\"white_pixels\":%u}\n",
        pages+1u,lines,last_bottom,479u-last_bottom,pixels);
    if(++pages==3u)longjmp(finished,1);
}

/* PRODUCTION_TERMINAL */

int main(int argc,char **argv) {
    assert(argc==2);output=argv[1];
    const uintptr_t fb=0xa5000000u,regs=0xa05f8000u;
    const size_t bytes=640u*480u*2u;
    assert(sysconf(_SC_PAGESIZE)==4096 && !(bytes%4096u));
    void *guard=mmap((void *)(fb-4096u),bytes+8192u,PROT_NONE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(guard==(void *)(fb-4096u));
    assert(!mprotect((void *)fb,bytes,PROT_READ|PROT_WRITE));
    assert(mmap((void *)regs,4096u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)regs);
    for(unsigned i=0u;i<sizeof(stopped)/4u;i++) {
        uint32_t word=0x8c123000u+i;memcpy((unsigned char *)&stopped+4u*i,&word,4u);
    }
    stopped.stop[0]=KUI_VC2_TEXTURE_VIRTUAL_VRAM;
    stopped.stop[1]=6417u;stopped.stop[2]=3285504u;stopped.stop[3]=0u;stopped.stop[4]=0u;
    stopped.fold.first_offset=UINT32_MAX;stopped.fold.expected=stopped.fold.actual=0u;
    before=stopped;
    if(!setjmp(finished))terminal();
    assert(pages==3u && max_bottom<450u);
    assert(!munmap(guard,bytes+8192u));assert(!munmap((void *)regs,4096u));
    return 0;
}
'''


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output-dir',type=Path,default=ROOT/'build/vc2-texture-probe/render')
    parser.add_argument('--cc',default='cc')
    args=parser.parse_args()
    output=args.output_dir.resolve();output.mkdir(parents=True,exist_ok=True)
    production=ROOT/'src/loader/vc2_texture_probe.c'
    terminal=function(production.read_text(),'static void terminal(void) {')
    source=HARNESS.replace('/* PRODUCTION_TERMINAL */',terminal)
    with tempfile.TemporaryDirectory(prefix='vc2-probe-render-') as temporary:
        temporary=Path(temporary);fixture=temporary/'render.c';binary=temporary/'render'
        fixture.write_text(source)
        command=[args.cc,'-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=undefined','-fno-sanitize-recover=all','-fno-pie','-no-pie',
            '-DKUI_VC2_CE_VMU=1','-DKUI_NATIVE_OCRAM_BOOTSTRAP=1',
            '-DKUI_BUILD_ID="VISUALQA"','-I'+str(ROOT/'include'),
            '-I'+str(ROOT/'src/loader'),str(fixture),'-o',str(binary)]
        subprocess.run(command,check=True)
        result=subprocess.run([str(binary),str(output)],check=True,capture_output=True,text=True)
    pages=[json.loads(line) for line in result.stdout.splitlines()]
    assert len(pages)==3 and len({page['page'] for page in pages})==3
    for page in pages:
        raw=output/f"page-{page['page']}.rgb565"
        pixels=array.array('H',raw.read_bytes())
        if sys.byteorder!='little':pixels.byteswap()
        rgb=bytearray()
        for pixel in pixels:
            rgb.extend((((pixel>>11)&31)*255//31,((pixel>>5)&63)*255//63,(pixel&31)*255//31))
        png=output/f"page-{page['page']}.png"
        Image.frombytes('RGB',(640,480),bytes(rgb)).save(png)
        page.update(png=str(png),sha256=hashlib.sha256(png.read_bytes()).hexdigest())
    report=dict(status='PASS',production_terminal_extracted=True,production_renderer=True,
        sanitizer='undefined',guarded_framebuffer=True,immutable_snapshot=True,
        unique_numbered_headers=True,footer_on_every_page=True,glyph_gap_px=4,pages=pages,
        source_sha256={str(path.relative_to(ROOT)):hashlib.sha256(path.read_bytes()).hexdigest()
            for path in (production,ROOT/'src/loader/retail_display.c',Path(__file__))},
        limits=['Authored values and Linux PVR/VRAM mappings; no console display timings executed'])
    (output/'render-audit.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
