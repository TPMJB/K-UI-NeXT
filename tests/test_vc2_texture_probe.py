#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exercise the actual terminal readback probe with bounded hostile inputs.

Host tests model supplied MMIO and existing source/destination memory only.
The SH model executes the emitted stack wrapper with an adversarial C callee;
neither model claims physical VRAM, cache, DMA or console acceptance.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import random
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tests'))
from test_armada_ce_maple_hook import CPU as BaseCPU, MASK
from sh4_delay_slots import validate_delay_slot

HARNESS = r'''
#include "kui/vc2_texture_probe.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include "kui/vc2_ce_vmu_prepare.h"
#include "kui/vc2_ce_vmu_diagnostic.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

struct kui_vc2_ce_vmu_prepare_report kui_vc2_ce_vmu_prepare_report;
static _Alignas(32) unsigned char source[544],destination[544];
static uint32_t address_array[64],data_array[64],context[16];
static uint32_t mmucr=1u,pteh=0x2au,sr=0x400080f0u,ccr=0x105u,descriptors=1u;
static uint32_t descriptor_pointer=0x8c200000u,reads,descriptor_reads,stops;
static uint32_t context_calls,non_dma_reads,dma_reads,dma_epoch,mutation_kind,mutation_done,eviction_reads;
static uint32_t eviction_addresses[128];
static struct kui_vc2_texture_probe_snapshot captured;
static struct retail_display_state saved_display;
static jmp_buf terminal_jump;

uint32_t kui_vc2_texture_probe_host_sr(void) {return sr;}
uint32_t kui_vc2_texture_probe_host_read32(uint32_t at) {
    ++reads;
    if(at>=0xffa00010u && at<=0xffa0001cu && !(at&3u)) {
        ++dma_reads;return 0x11100000u+0x1000u*dma_epoch+(at-0xffa00010u)/4u;
    }
    ++non_dma_reads;
    if(mutation_kind==2u && !mutation_done) {destination[16u+94u]^=0x55u;mutation_done=1u;}
    if(at>=0x8cfc4000u && at<0x8cfc8000u) {
        assert(!(at&31u));
        if(eviction_reads<128u)eviction_addresses[eviction_reads]=at;
        ++eviction_reads;
        if(mutation_kind==3u && !mutation_done) {destination[16u+94u]^=0x55u;mutation_done=1u;}
        return at^0x91827364u;
    }
    if(at>=0xf6000000u && at<0xf6004000u) {
        assert(!((at-0xf6000000u)&255u));return address_array[(at-0xf6000000u)>>8];
    }
    if(at>=0xf7000000u && at<0xf7004000u) {
        assert(!((at-0xf7000000u)&255u));return data_array[(at-0xf7000000u)>>8];
    }
    switch(at) {
    case 0xff000010u:return mmucr;
    case 0xff000008u:return 0x8c1058a0u;
    case 0xff00001cu:return ccr;
    case 0xff000000u:return pteh;
    case 0x8c1d0654u:assert(0 && "RAMFREE is outside the narrowed probe");return 0u;
    case 0x8c1d0658u:case 0x8c1dd3dcu:
        assert(0 && "Allocator MMIO is outside the narrowed probe");return 0u;
    case 0x8c1dd430u:case 0x8c1dd434u:case 0x8c1dd438u:case 0x8c1dd43cu:
        assert(0 && "Allocator descriptor summaries must not be read");return 0u;
    default:
        ++descriptor_reads;assert(0 && "Unadmitted MMIO or descriptor read");return 0u;
    }
}
void kui_vc2_ce_vmu_texture_context(uint32_t out[16]) {
    ++context_calls;
    if(mutation_kind==1u && !mutation_done) {destination[16u+94u]^=0x55u;mutation_done=1u;}
    memcpy(out,context,sizeof(context));
}
void kui_vc2_ce_vmu_service_snapshot(uint32_t out[16]) {
    for(unsigned i=0u;i<16u;i++)out[i]=0xa0000000u+i;
}
const struct retail_display_state *kui_vc2_ce_vmu_saved_display(void) {return &saved_display;}
void retail_display_capture(struct retail_display_state *out) {(void)out;assert(0);}
void kui_vc2_texture_probe_host_stop(const struct kui_vc2_texture_probe_snapshot *s) {
    captured=*s;++stops;longjmp(terminal_jump,1);
}
static uint32_t hash(const unsigned char *p,unsigned n) {
    uint32_t h=2166136261u;for(unsigned i=0u;i<n;i++)h=(h^p[i])*16777619u;return h;
}
static void reset(void) {
    kui_vc2_texture_probe_host_reset();memset(&captured,0,sizeof(captured));
    memset(address_array,0,sizeof(address_array));memset(data_array,0,sizeof(data_array));
    memset(context,0,sizeof(context));context[0]=0x31u;context[1]=16u;context[2]=512435u;
    mmucr=1u;pteh=0x2au;sr=0x400080f0u;ccr=0x105u;descriptors=1u;descriptor_pointer=0x8c200000u;
    reads=descriptor_reads=stops=0u;
    context_calls=non_dma_reads=dma_reads=dma_epoch=mutation_kind=mutation_done=eviction_reads=0u;
    for(unsigned i=0u;i<sizeof(source);i++)source[i]=(unsigned char)(i*37u+11u);
    memcpy(destination,source,sizeof(source));
    for(unsigned i=0u;i<14u;i++)saved_display.regs[i]=0x12340000u+i;
}
static void set_entry(unsigned i,uint32_t va,uint32_t pa,uint32_t size,uint32_t shared) {
    static const uint32_t masks[]={0x3ffu,0xfffu,0xffffu,0xfffffu};
    static const uint32_t enc[]={0u,0x10u,0x80u,0x90u};
    address_array[i]=(va&~masks[size])|0x100u|(pteh&255u);
    data_array[i]=(pa&~masks[size])|0x100u|enc[size]|(shared?2u:0u);
}
static void copy_probe(const unsigned char *s,void *d,uint32_t n,uint32_t offset) {
    if(s)kui_vc2_texture_probe_run(s,d,n|KUI_VC2_TEXTURE_PROBE_PRE,offset);
    kui_vc2_texture_probe_run(s,d,n,offset);
}
static void must_stop(const unsigned char *s,void *d,uint32_t n,uint32_t offset) {
    if(!setjmp(terminal_jump)) {copy_probe(s,d,n,offset);assert(0);}
}
static void post_stop(const unsigned char *s,void *d,uint32_t n,uint32_t offset) {
    if(!setjmp(terminal_jump)) {kui_vc2_texture_probe_run(s,d,n,offset);assert(0);}
}
static void folds(void) {
    struct kui_vc2_texture_fold f;
    reset();
    for(unsigned so=0u;so<4u;so++)for(unsigned dest=0u;dest<4u;dest++)
    for(unsigned n=0u;n<=512u;n++) {
        memset(destination,0x5a,sizeof(destination));memcpy(destination+16u+dest,source+16u+so,n);
        kui_vc2_texture_probe_fold(source+16u+so,destination+16u+dest,n,&f);
        assert(f.first_offset==UINT32_MAX && f.expected==0u && f.actual==0u);
        assert(f.source_hash==hash(source+16u+so,n) && f.destination_hash==f.source_hash);
        assert(destination[15u+dest]==0x5au && destination[16u+dest+n]==0x5au);
    }
    memcpy(source+16u,"hello",5u);memcpy(destination+16u,"hello",5u);
    kui_vc2_texture_probe_fold(source+16u,destination+16u,5u,&f);
    assert(f.source_hash==0x4f9f2cabu && f.destination_hash==0x4f9f2cabu);
    reset();destination[16u+401u]^=0x55u;destination[16u+507u]^=0xaau;
    kui_vc2_texture_probe_fold(source+16u,destination+16u,512u,&f);
    assert(f.first_offset==401u && f.expected==source[417u] && f.actual==destination[417u]);
    assert(f.source_hash!=f.destination_hash);
}
static void utlb(void) {
    uint32_t physical=0xfeedfaceu;
    const uint32_t va=0x08345679u,pa=0x04300000u;
    const uint32_t masks[]={0x3ffu,0xfffu,0xffffu,0xfffffu};
    reset();
    for(unsigned size=0u;size<4u;size++) {
        set_entry(0u,va,pa,size,0u);
        assert(kui_vc2_texture_probe_utlb(va,address_array[0],data_array[0],pteh,mmucr,sr,&physical)==1u);
        assert(physical==((pa&~masks[size])|(va&masks[size])));
        assert(!kui_vc2_texture_probe_utlb(va^(masks[size]+1u),address_array[0],data_array[0],pteh,mmucr,sr,&physical));
        assert(!kui_vc2_texture_probe_utlb(va,address_array[0]^0x100u,data_array[0],pteh,mmucr,sr,&physical));
        assert(!kui_vc2_texture_probe_utlb(va,address_array[0],data_array[0]^0x100u,pteh,mmucr,sr,&physical));
        physical=0xfeedfaceu;
        assert(!kui_vc2_texture_probe_utlb(va,address_array[0],data_array[0],pteh^1u,mmucr,sr,&physical));
        assert(physical==0xfeedfaceu);
        assert(kui_vc2_texture_probe_utlb(va,address_array[0],data_array[0]|2u,pteh^1u,mmucr,sr,&physical));
        assert(kui_vc2_texture_probe_utlb(va,address_array[0],data_array[0],pteh^1u,mmucr|0x100u,sr,&physical));
        assert(!kui_vc2_texture_probe_utlb(va,address_array[0],data_array[0],pteh^1u,mmucr|0x100u,0u,&physical));
    }
}
static void runs(void) {
    reset();set_entry(3u,(uint32_t)(uintptr_t)(destination+16u),0x0c300000u,1u,0u);
    copy_probe(source+16u,destination+16u,512u,0u);
    assert(stops==0u);
    destination[477u]^=0x10u;
    must_stop(source+16u,destination+16u,512u,512u);
    assert(captured.stop[0]==KUI_VC2_TEXTURE_MISMATCH && captured.stop[1]==2u);
    assert(captured.fold.first_offset==461u);
    for(unsigned i=0u;i<5u;i++)assert(!captured.coverage[i]);
    for(unsigned i=0u;i<6u;i++)assert(!captured.tlb_last[i]);
    assert(captured.chunk[3]==512u && captured.chunk[4]==512585u);
    assert(captured.tlb_first[0]==1u && captured.tlb_first[1]==3u);
    for(unsigned i=0u;i<8u;i++)assert(captured.memory[i]==0u);
    assert(descriptor_reads==0u);
    assert(!memcmp(&captured.display,&saved_display,sizeof(saved_display)));
    for(unsigned i=0u;i<16u;i++)assert(captured.reader[i]==0u);
    const struct kui_vc2_texture_probe_snapshot saved=captured;
    uint32_t before=reads;
    context[2]=123u;destination[16u]^=1u;
    must_stop(NULL,NULL,UINT32_MAX,UINT32_MAX);
    assert(reads==before && !memcmp(&captured,&saved,sizeof(saved)) && stops==2u);

    reset();context[2]=513035u;set_entry(4u,(uint32_t)(uintptr_t)(destination+16u),0x04000000u,1u,0u);
    must_stop(source+16u,destination+16u,512u,0u);
    assert(captured.stop[0]==KUI_VC2_TEXTURE_TARGET_TAIL && captured.fold.first_offset==UINT32_MAX);
    assert((captured.tlb_first[4]&0x1e800000u)==0x04000000u);

    reset();context[2]=513035u;
    set_entry(0u,(uint32_t)(uintptr_t)(destination+16u),0x04000000u,1u,0u);
    address_array[1]=address_array[0];data_array[1]=data_array[0];
    must_stop(source+16u,destination+16u,512u,0u);
    assert(captured.tlb_first[0]==2u);
    assert(captured.stop[0]==KUI_VC2_TEXTURE_TARGET_TAIL);

    reset();descriptors=4u;context[2]=513035u;
    must_stop(source+16u,destination+16u,512u,0u);
    assert(captured.memory[7]==0u && descriptor_reads==0u);
    reset();descriptor_pointer=0x8c1dd401u;context[2]=513035u;
    must_stop(source+16u,destination+16u,512u,0u);
    assert(captured.memory[7]==0u && descriptor_reads==0u);
    reset();descriptor_pointer=0x8d000000u-16u;context[2]=513035u;
    must_stop(source+16u,destination+16u,512u,0u);
    assert(captured.memory[7]==0u && descriptor_reads==0u);
    reset();descriptors=0u;context[2]=513035u;
    must_stop(source+16u,destination+16u,512u,0u);
    assert(captured.memory[7]==0u && descriptor_reads==0u);

    reset();must_stop(NULL,destination+16u,512u,0u);
    assert(captured.stop[0]==KUI_VC2_TEXTURE_MAP && captured.stop[4]==1u);
    assert(captured.fold.source_hash==0u);
    reset();copy_probe(source+16u,destination+16u,512u,0u);
    must_stop(NULL,destination+16u,512u,512u);
    assert(captured.stop[0]==KUI_VC2_TEXTURE_MAP && captured.stop[1]==2u);
    assert(captured.fold.source_hash==0u && captured.fold.destination_hash==0u);
    assert(captured.fold.first_offset==UINT32_MAX-1u && captured.pre_state[0]==0u);
    reset();descriptors=3u;kui_vc2_ce_vmu_prepare_report.actual=7u;
    kui_vc2_ce_vmu_prepare_report.expected=8u;kui_vc2_ce_vmu_prepare_report.ram_end=0x8cfc0000u;
    if(!setjmp(terminal_jump)) {kui_vc2_ce_vmu_diagnostic_stop(0x302u,4u);assert(0);}
    assert(captured.stop[0]==KUI_VC2_TEXTURE_PREPARE && captured.prepare[0]==0x302u);
    assert(captured.prepare[1]==4u && captured.prepare[2]==7u && captured.prepare[3]==8u);
    assert(captured.prepare[4]==0x8cfc0000u && descriptor_reads==0u);

    reset();context[2]=1u;
    for(unsigned i=0u;i<32767u;i++)copy_probe(source+16u,destination+16u,0u,0u);
    assert(stops==0u);
    must_stop(source+16u,destination+16u,0u,0u);
    assert(captured.stop[0]==KUI_VC2_TEXTURE_LIMIT && captured.stop[1]==32768u && captured.stop[2]==0u);
}
static void check_eviction(void *destination,uint32_t bytes) {
    uintptr_t first=(uintptr_t)destination&~(uintptr_t)31u;
    uintptr_t last=((uintptr_t)destination+(bytes?bytes-1u:0u))&~(uintptr_t)31u;
    uint32_t expected=(uint32_t)((last-first)/32u+1u)*4u;
    assert(expected<=128u && captured.eviction_reads==expected && eviction_reads==expected);
    unsigned step=0u;
    for(uintptr_t at=first;at<=last;at+=32u) {
        uint32_t index=(uint32_t)at&0x3fe0u;
        for(unsigned colour=0u;colour<4u;colour++) {
            assert(eviction_addresses[step++]==0x8cfc4000u+index);
            index=(index+0x1000u)&0x3fffu;
        }
    }
}
static void pre_post(void) {
    reset();
    kui_vc2_texture_probe_run(source+16u,(void *)(uintptr_t)0xdeadbeefu,
                              512u|KUI_VC2_TEXTURE_PROBE_PRE,0u);
    assert(context_calls==0u && non_dma_reads==0u && dma_reads==4u && stops==0u);

    reset();
    kui_vc2_texture_probe_run(source+16u,destination+16u,512u|KUI_VC2_TEXTURE_PROBE_PRE,0x3300u);
    assert(context_calls==0u && non_dma_reads==0u && dma_reads==4u);
    memcpy(destination+16u,source+16u,512u);source[16u+94u]^=0x33u;dma_epoch=1u;
    post_stop(source+16u,destination+16u,512u,0x3300u);
    assert(captured.stop[0]&KUI_VC2_TEXTURE_SOURCE_CHANGED);
    assert(!(captured.stop[0]&KUI_VC2_TEXTURE_MISMATCH));
    assert(captured.fold.first_offset==UINT32_MAX && captured.late.first_offset==UINT32_MAX);
    assert(captured.source.first_offset==94u && captured.source_late.first_offset==94u);
    assert(captured.fold.source_hash==captured.fold.destination_hash);
    assert(captured.source.source_hash!=captured.source.destination_hash);
    assert(!memcmp(captured.saved_source,destination+16u,512u));
    assert(captured.pre_state[1]==(uint32_t)(uintptr_t)(source+16u));
    assert(captured.pre_state[2]==(uint32_t)(uintptr_t)(destination+16u));
    assert(captured.pre_state[3]==512u && captured.pre_state[4]==0x3300u);
    for(unsigned i=0u;i<4u;i++) {
        assert(captured.dma_pre[i]==0x11100000u+i);
        assert(captured.dma_post[i]==0x11101000u+i);
    }

    reset();
    kui_vc2_texture_probe_run(source+16u,destination+16u,512u|KUI_VC2_TEXTURE_PROBE_PRE,0u);
    memcpy(destination+16u,source+16u,512u);destination[16u+123u]^=0x44u;
    post_stop(source+16u,destination+16u,512u,0u);
    assert(captured.stop[0]&KUI_VC2_TEXTURE_MISMATCH);
    assert(!(captured.stop[0]&KUI_VC2_TEXTURE_SOURCE_CHANGED));
    assert(captured.fold.first_offset==123u && captured.late.first_offset==123u);
    assert(captured.source.first_offset==UINT32_MAX && captured.source_late.first_offset==UINT32_MAX);

    for(uint32_t kind=1u;kind<=3u;kind++) {
        reset();
        kui_vc2_texture_probe_run(source+16u,destination+16u,512u|KUI_VC2_TEXTURE_PROBE_PRE,0u);
        assert(context_calls==0u && non_dma_reads==0u);
        memcpy(destination+16u,source+16u,512u);mutation_kind=kind;
        post_stop(source+16u,destination+16u,512u,0u);
        assert(captured.stop[0]&KUI_VC2_TEXTURE_EARLY_LATE_DIFF);
        assert(captured.fold.first_offset==UINT32_MAX && captured.late.first_offset==94u);
        assert(!(captured.stop[0]&KUI_VC2_TEXTURE_SOURCE_CHANGED));
        assert(mutation_done==1u && context_calls==1u && non_dma_reads>0u);
        check_eviction(destination+16u,512u);
    }

    reset();ccr|=0x80u;context[2]=513035u;
    kui_vc2_texture_probe_run(source+16u,destination+16u,512u|KUI_VC2_TEXTURE_PROBE_PRE,0u);
    post_stop(source+16u,destination+16u,512u,0u);
    assert(captured.eviction_reads==0u && eviction_reads==0u);

    reset();post_stop(source+16u,destination+16u,512u,0u);
    assert(captured.stop[0]&KUI_VC2_TEXTURE_PRESTATE);
    reset();
    kui_vc2_texture_probe_run(source+16u,destination+16u,512u|KUI_VC2_TEXTURE_PROBE_PRE,0u);
    post_stop(source+16u,destination+16u,511u,0u);
    assert(captured.stop[0]&KUI_VC2_TEXTURE_PRESTATE);
    reset();
    if(!setjmp(terminal_jump)) {
        kui_vc2_texture_probe_run(source+16u,destination+16u,513u|KUI_VC2_TEXTURE_PROBE_PRE,0u);
        kui_vc2_texture_probe_run(source+16u,destination+16u,513u,0u);assert(0);
    }
    assert(captured.stop[0]&KUI_VC2_TEXTURE_PRESTATE);
}
int main(void) {
    folds();utlb();runs();pre_post();
    puts("PASS actual probe: full-chunk readback/mismatch, 1K/4K/64K/1M UTLB, ASID/SV/shared, ambiguous mapping, frozen snapshot and no allocator traversal");
    return 0;
}
'''


LOW_PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define KUI_VC2_TEXTURE_PROBE 1
#define KUI_NATIVE_GD_VRAM_DMA 1
#define KUI_NATIVE_GD_DMA_STREAM 1
#define KUI_NATIVE_SESSION_CONTROLS 0
#define KUI_RETAIL_GD_DMAREAD_STREAM 38u
#define NATIVE_BYTE_STREAM(command) (((command)&~1u)==KUI_RETAIL_GD_DMAREAD_STREAM)
#define KUI_RETAIL_MAP_DMA_WRITE 7
#define KUI_GD_ERROR_MEMORY 5u
#define TEXTURE_OFFSET(value) ,(value)
struct kui_retail_gd {
    struct {void *context;uint8_t *(*map)(void *,uint32_t,uint32_t,int);} ops;
    uint32_t command,sector_bytes,completed_bytes,outputs[1],cpu_mmu;
};
static struct {
    struct {struct kui_retail_gd service;} shared;
    struct {uint32_t destination,failed;} engine;
} R;
#define e R.engine
static _Alignas(32) uint8_t input[576],output[576];
static struct {
    uint32_t address,count,offset;
    const uint8_t *source;
    int writing,refuse;
} wanted;
static uint32_t map_calls,copy_calls,vram_calls,pre_calls,probe_calls,progress_calls;
static uint32_t delivered,probe_destination,probe_count,probe_offset;
static const uint8_t *probe_source;
static char events[16];static unsigned event_count;
static void event(char value) {assert(event_count<sizeof(events));events[event_count++]=value;}
static uint8_t *map(void *context,uint32_t address,uint32_t count,int writing) {
    assert(context==R.shared.service.ops.context);
    assert(address==wanted.address && count==wanted.count && writing==wanted.writing);
    ++map_calls;event('M');return wanted.refuse?NULL:output+16u;
}
static void *probe_memcpy(void *out,const void *in,size_t count) {
    assert(out==output+16u && in==wanted.source && count==wanted.count);
    assert(map_calls==1u && probe_calls==0u && progress_calls==0u);
    assert(pre_calls==(R.shared.service.ops.context?1u:0u));
    ++copy_calls;event('C');
    for(size_t i=0u;i<count;i++)((uint8_t *)out)[i]=((const uint8_t *)in)[i];
    return out;
}
static void native_vram_write(void *out,const uint8_t *in,uint32_t count) {
    ++vram_calls;probe_memcpy(out,in,count);
}
void kui_vc2_texture_probe_entry(const uint8_t *source,uint32_t destination,uint32_t count,uint32_t offset) {
    assert(R.shared.service.ops.context && map_calls==1u && progress_calls==0u);
    assert((count&0x7fffffffu)==wanted.count && offset==wanted.offset);
    if(count&0x80000000u) {
        assert(!wanted.refuse && source==wanted.source && !copy_calls && !probe_calls);
        assert(destination==(uint32_t)(uintptr_t)(output+16u));
        ++pre_calls;event('B');return;
    }
    ++probe_calls;event('P');probe_source=source;probe_destination=destination;
    probe_count=count;probe_offset=offset;
    if(wanted.refuse) {
        assert(source==NULL && destination==wanted.address && copy_calls==0u && pre_calls==0u);
    } else {
        assert(source==wanted.source && destination==(uint32_t)(uintptr_t)(output+16u));
        assert(copy_calls==1u && memcmp(output+16u,source,count)==0);
    }
}
static void kui_retail_gd_native_dma_progress(struct kui_retail_gd *s,uint32_t done,uint32_t error) {
    assert(s==&R.shared.service && error==0u && !e.failed && map_calls==1u && copy_calls==1u);
    assert(pre_calls==(s->ops.context?1u:0u) && probe_calls==(s->ops.context?1u:0u));
    ++progress_calls;delivered=done;event('G');
}
#define memcpy probe_memcpy
'''

LOW_SUFFIX = r'''
#undef memcpy
static void reset(uint32_t command,int owned) {
    memset(&R,0,sizeof(R));memset(&wanted,0,sizeof(wanted));memset(output,0x5a,sizeof(output));
    map_calls=copy_calls=vram_calls=pre_calls=probe_calls=progress_calls=0u;
    probe_source=NULL;probe_destination=probe_count=probe_offset=delivered=0u;event_count=0u;
    R.shared.service.ops.map=map;R.shared.service.ops.context=owned?(void *)(uintptr_t)1u:NULL;
    R.shared.service.command=command;R.shared.service.sector_bytes=2048u;
    e.destination=0x08302000u;wanted.writing=1;wanted.source=input+16u;
    for(unsigned i=0u;i<sizeof(input);i++)input[i]=(uint8_t)(i*29u+19u);
}
static void ordinary(void) {
    for(uint32_t command=16u;command<=28u;command+=(command==16u?1u:11u))
    for(int owned=0;owned<2;owned++) {
        reset(command,owned);wanted.address=e.destination+37u;wanted.count=100u;wanted.offset=37u;
        write_out(NULL,37u,input+16u,100u);
        assert(map_calls==1u && copy_calls==1u && probe_calls==(uint32_t)owned && progress_calls==0u && !e.failed);
        assert(pre_calls==(uint32_t)owned);
        assert(event_count==(unsigned)(owned?4u:2u) && events[0]=='M' && events[owned?2u:1u]=='C');
        if(owned)assert(events[1]=='B' && events[3]=='P');
        assert(output[15]==0x5au && output[116]==0x5au);
    }
    reset(16u,1);e.destination=0xa4000000u;wanted.writing=KUI_RETAIL_MAP_DMA_WRITE;
    wanted.address=e.destination;wanted.count=128u;wanted.offset=0u;
    write_out(NULL,0u,input+16u,128u);
    assert(vram_calls==1u && copy_calls==1u && pre_calls==1u && probe_calls==1u && progress_calls==0u);
    reset(16u,1);wanted.address=e.destination+9u;wanted.count=77u;wanted.offset=9u;wanted.refuse=1;
    write_out(NULL,9u,input+16u,77u);
    assert(map_calls==1u && !copy_calls && !pre_calls && probe_calls==1u && !progress_calls && e.failed==KUI_GD_ERROR_MEMORY);
    assert(probe_source==NULL && probe_destination==wanted.address && probe_count==77u && probe_offset==9u);
    reset(16u,0);wanted.address=e.destination;wanted.count=32u;wanted.offset=0u;wanted.refuse=1;
    write_out(NULL,0u,input+16u,32u);
    assert(map_calls==1u && !copy_calls && !pre_calls && !probe_calls && !progress_calls && e.failed==KUI_GD_ERROR_MEMORY);
}
static void streamed(void) {
    for(uint32_t command=38u;command<=39u;command++)for(int owned=0;owned<2;owned++) {
        reset(command,owned);uint32_t begin=4224u;
        R.shared.service.completed_bytes=begin;R.shared.service.outputs[0]=1024u;
        wanted.address=e.destination;wanted.count=384u;wanted.offset=begin;wanted.source=input+16u+128u;
        write_out((void *)(uintptr_t)begin,0u,input+16u,512u);
        assert(map_calls==1u && copy_calls==1u && probe_calls==(uint32_t)owned && progress_calls==1u);
        assert(delivered==begin+384u && events[event_count-1u]=='G');
        if(owned)assert(events[event_count-2u]=='P');

        reset(command,owned);begin=4224u;
        R.shared.service.completed_bytes=begin;R.shared.service.outputs[0]=100u;
        wanted.address=e.destination;wanted.count=100u;wanted.offset=begin;
        write_out((void *)(uintptr_t)begin,128u,input+16u,512u);
        assert(delivered==begin+100u && copy_calls==1u && probe_calls==(uint32_t)owned);

        reset(command,owned);begin=4224u;
        R.shared.service.completed_bytes=begin;R.shared.service.outputs[0]=1024u;
        wanted.address=e.destination+384u;wanted.count=512u;wanted.offset=begin+384u;
        write_out((void *)(uintptr_t)begin,512u,input+16u,512u);
        assert(delivered==begin+896u && progress_calls==1u && probe_calls==(uint32_t)owned);

        reset(command,owned);begin=4096u;
        R.shared.service.completed_bytes=begin;R.shared.service.outputs[0]=512u;
        wanted.address=e.destination;wanted.count=512u;wanted.offset=begin;wanted.refuse=1;
        write_out((void *)(uintptr_t)begin,0u,input+16u,512u);
        assert(map_calls==1u && !copy_calls && !pre_calls && probe_calls==(uint32_t)owned && !progress_calls);
        assert(e.failed==KUI_GD_ERROR_MEMORY);
        if(owned)assert(probe_source==NULL && probe_destination==wanted.address && probe_offset==begin);

        reset(command,owned);e.failed=77u;
        write_out((void *)(uintptr_t)4096u,0u,input+16u,512u);
        assert(!map_calls && !copy_calls && !pre_calls && !probe_calls && !progress_calls && e.failed==77u);
        reset(command,owned);
        write_out((void *)(uintptr_t)4224u,0u,input+16u,128u);
        assert(!map_calls && !copy_calls && !pre_calls && !probe_calls && !progress_calls);
        reset(command,owned);R.shared.service.completed_bytes=4096u;R.shared.service.outputs[0]=32u;
        write_out((void *)(uintptr_t)4096u,64u,input+16u,512u);
        assert(!map_calls && !copy_calls && !pre_calls && !probe_calls && !progress_calls);
    }
}
int main(void) {
    ordinary();streamed();
    puts("PASS production low callback: ownership gate, PRE-before-copy and POST-before-progress, original map failure and stream offsets/clipping");
    return 0;
}
'''


CONTEXT_PREFIX = r'''
#define _GNU_SOURCE
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
struct kui_retail_manifest {uint8_t prefix[68];uint32_t title[37];};
struct kui_retail_gd {
    uint32_t token,command,lba,count,destination,request_bytes,completed_bytes;
    uint32_t outputs[4],error,status,pending,cpu_mmu;
};
typedef struct {
    struct {const struct kui_retail_manifest *manifest;} engine;
    struct {struct kui_retail_gd service;} shared;
} kui_retail_async_region_type;
static const kui_retail_async_region_type *vc2_saved_region;
_Static_assert(offsetof(struct kui_retail_manifest,title)==68u,"source trace offset");
'''

CONTEXT_SUFFIX = r'''
static void populate(kui_retail_async_region_type *region,struct kui_retail_manifest *manifest,uint32_t base) {
    region->engine.manifest=manifest;
    struct kui_retail_gd *s=&region->shared.service;
    s->token=base+0u;s->command=base+1u;s->lba=base+2u;s->count=base+3u;
    s->destination=base+4u;s->request_bytes=base+5u;s->completed_bytes=base+6u;
    s->outputs[0]=base+7u;s->error=base+8u;s->status=base+9u;
    s->pending=base+10u;s->cpu_mmu=base+11u;
    for(unsigned i=0u;i<37u;i++)manifest->title[i]=base+100u+i;
}
int main(void) {
    const size_t bytes=0x4000u;
    void *p1=mmap((void *)(uintptr_t)0x8c200000u,bytes,PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    void *p2=mmap((void *)(uintptr_t)0xac200000u,bytes,PROT_READ|PROT_WRITE,
                  MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(p1==(void *)(uintptr_t)0x8c200000u && p2==(void *)(uintptr_t)0xac200000u);
    populate(p1,(void *)(uintptr_t)0x8c201000u,0x00110000u);
    populate(p2,(void *)(uintptr_t)0xac201000u,0x00ee0000u);
    assert(mprotect(p1,bytes,PROT_READ)==0 && mprotect(p2,bytes,PROT_READ)==0);
    struct {uint32_t before,out[16],after;} result={.before=0x12345678u,.after=0x87654321u};
    vc2_saved_region=p2;
    kui_vc2_ce_vmu_texture_context(result.out);
    for(unsigned i=0u;i<12u;i++)assert(result.out[i]==0x00110000u+i);
    assert(result.out[12]==0x00110000u+105u && result.out[13]==0x00110000u+106u);
    assert(result.out[14]==0x00110000u+104u && result.out[15]==0x00110000u+102u);
    assert(result.before==0x12345678u && result.after==0x87654321u);
    uint32_t saved[16];memcpy(saved,result.out,sizeof(saved));
    vc2_saved_region=p1;
    kui_vc2_ce_vmu_texture_context(result.out);
    assert(!memcmp(saved,result.out,sizeof(saved)));
    vc2_saved_region=NULL;
    kui_vc2_ce_vmu_texture_context(result.out);
    for(unsigned i=0u;i<16u;i++)assert(!result.out[i]);
    assert(result.before==0x12345678u && result.after==0x87654321u);
    assert(munmap(p1,bytes)==0 && munmap(p2,bytes)==0);
    puts("PASS production context accessor: current P1 token/LBA and trace despite stale P2, NULL zeroes, read-only maps and output canaries");
    return 0;
}
'''


def actual_function(text,signature):
    """Keep the complete authored function, including preprocessor branches."""
    start=text.index(signature);brace=text.index('{',start);depth=1;cursor=brace+1
    while depth:
        if text[cursor]=='{':depth+=1
        elif text[cursor]=='}':depth-=1
        cursor+=1
    return text[start:cursor]


ENTRY=0xacfce100
RUN=0xacfc9000
STACK=0xacfcf800
RETURN=0x01cf3340


class WrapperCPU(BaseCPU):
    def __init__(self,code,seed):
        super().__init__(seed,ENTRY,1)
        rng=random.Random(seed^0x9238abcd)
        self.r=[rng.getrandbits(32) for _ in range(16)]
        self.r[15]=0x7e00fd00;self.pr=RETURN;self.pc=ENTRY
        self.sr=0x400080f0|(seed&1)
        self.binary=code;self.mem={};self.stores=[];self.calls=0
        self.original=(self.r.copy(),self.pr,self.gbr,self.mach,self.macl,self.sr,self.fpscr,self.fr.copy())

    def code(self,address,size):
        assert ENTRY<=address and address+size<=ENTRY+len(self.binary),hex(address)
        return self.binary[address-ENTRY:address-ENTRY+size]

    def read(self,address):
        assert not address&3
        if address in self.mem:return self.mem[address]
        return struct.unpack('<I',self.code(address,4))[0]

    def write(self,address,value):
        assert not address&3
        assert STACK-40<=address<STACK,'Write escaped private frame/homes '+hex(address)
        self.mem[address]=value&MASK;self.stores.append(address)

    def plain(self,word,pc):
        n=(word>>8)&15
        attrs={0x4022:'pr',0x4002:'mach',0x4012:'macl',0x4013:'gbr'}
        key=word&0xf0ff
        if key in attrs:
            self.r[n]=(self.r[n]-4)&MASK;self.write(self.r[n],getattr(self,attrs[key]));return
        super().plain(word,pc)

    def callback(self):
        self.calls+=1
        self.assertions()
        target=self.pr
        for i in range(4):self.write(self.r[15]+4*i,0xbadc0000+i)
        for i in range(8):self.r[i]=(0xdead0000+i*17)&MASK
        self.gbr^=0x12345678;self.mach^=0xa55aa55a;self.macl^=0x5aa55aa5
        self.sr^=1;self.pr^=0x99887766
        self.pc=target

    def assertions(self):
        assert self.r[15]==STACK-40
        assert self.r[4:8]==self.original[0][4:8]

    def run(self):
        steps=0
        while self.pc!=RETURN:
            steps+=1;assert steps<100
            pc=self.pc;word=struct.unpack('<H',self.code(pc,2))[0]
            if word&0xf0ff==0x400b or word==0x000b:
                if word==0x000b:target=self.pr
                else:target=self.r[(word>>8)&15];self.pr=pc+4
                slot=struct.unpack('<H',self.code(pc+2,2))[0]
                validate_delay_slot(slot,pc);self.plain(slot,pc+2)
                if target==RUN:self.callback()
                else:self.pc=target
            else:self.plain(word,pc);self.pc+=2
        return self


class ProbeTests(unittest.TestCase):
    def test_actual_context_uses_current_p1_view(self):
        text=(ROOT/'src/loader/vc2_ce_vmu_service.c').read_text()
        body=actual_function(text,'void kui_vc2_ce_vmu_texture_context(')
        with tempfile.TemporaryDirectory(prefix='vc2-texture-context-') as d:
            folder=Path(d);h=folder/'context-test.c';h.write_text(CONTEXT_PREFIX+body+CONTEXT_SUFFIX)
            binary=folder/'context'
            # ASan reserves0x8c200000 as shadow memory. This distinct-alias
            # test uses UBSan alone so the actual canonical P1/P2 addresses
            # can be mapped without replacing any existing process mapping.
            subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g',
                '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=undefined',
                '-fno-sanitize-recover=all','-fno-pie','-no-pie',str(h),'-o',str(binary)],check=True)
            env=os.environ.copy();env['UBSAN_OPTIONS']='halt_on_error=1'
            run=subprocess.run([str(binary)],env=env,capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('PASS production context accessor:',run.stdout)

    def test_actual_low_payload_callback(self):
        text=(ROOT/'src/loader/retail_async.c').read_text()
        text=text[text.index('static bool native_payload_write('):]
        bodies='\n'.join(actual_function(text,signature) for signature in (
            'static bool native_payload_write(', 'static void write_out('))
        with tempfile.TemporaryDirectory(prefix='vc2-texture-low-') as d:
            folder=Path(d);h=folder/'low-test.c';h.write_text(LOW_PREFIX+bodies+LOW_SUFFIX);binary=folder/'low'
            subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g',
                '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
                '-fno-sanitize-recover=all','-fno-pie','-no-pie',str(h),'-o',str(binary)],check=True)
            env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0';env['UBSAN_OPTIONS']='halt_on_error=1'
            run=subprocess.run([str(binary)],env=env,capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('PASS production low callback:',run.stdout)

    def test_actual_host_probe(self):
        with tempfile.TemporaryDirectory(prefix='vc2-texture-host-') as d:
            folder=Path(d);h=folder/'probe-test.c';h.write_text(HARNESS);binary=folder/'probe'
            subprocess.run([os.environ.get('HOST_CC','cc'),'-std=c11','-O1','-g',
                '-Wall','-Wextra','-Werror','-Wpedantic','-fsanitize=address,undefined',
                '-fno-sanitize-recover=all','-fno-pie','-no-pie','-I'+str(ROOT/'include'),
                '-I'+str(ROOT/'src/loader'),'-DKUI_VC2_TEXTURE_PROBE_HOST=1',str(h),
                str(ROOT/'src/loader/vc2_texture_probe.c'),'-o',str(binary)],check=True)
            env=os.environ.copy();env['ASAN_OPTIONS']='detect_leaks=0';env['UBSAN_OPTIONS']='halt_on_error=1'
            run=subprocess.run([str(binary)],env=env,capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('PASS actual probe:',run.stdout)

    def test_emitted_stack_wrapper_abi(self):
        gcc=os.environ.get('KUI_SH_GCC') or shutil.which('sh-elf-gcc')
        if not gcc:
            candidate=ROOT.parent/'sdk-restored/.deps/sh-elf/bin/sh-elf-gcc'
            gcc=str(candidate) if candidate.exists() else None
        if not gcc:raise unittest.SkipTest('Native SH toolchain unavailable')
        prefix=Path(gcc).resolve().parent
        with tempfile.TemporaryDirectory(prefix='vc2-texture-wrapper-') as d:
            folder=Path(d);obj=folder/'entry.o';elf=folder/'entry.elf';binary=folder/'entry.bin'
            linker=folder/'test.ld'
            linker.write_text('''OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_kui_vc2_texture_probe_entry)
SECTIONS { . = 0xacfce100; .probe : { KEEP(*(.texture_probe.entry)) }
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) } }
''')
            flags=['-m4-single-only','-ml','-ffreestanding','-I'+str(ROOT/'include')]
            subprocess.run([gcc,*flags,'-c',str(ROOT/'src/loader/vc2_texture_probe_entry.S'),'-o',str(obj)],check=True)
            subprocess.run([gcc,*flags,'-nostdlib','-Wl,--build-id=none',
                '-Wl,--defsym,_kui_vc2_texture_probe_run='+hex(RUN),
                '-T'+str(linker),str(obj),'-o',str(elf)],check=True)
            subprocess.run([str(prefix/'sh-elf-objcopy'),'-O','binary','--only-section=.probe',str(elf),str(binary)],check=True)
            code=binary.read_bytes()
            for seed in range(96):
                cpu=WrapperCPU(code,seed).run();original=cpu.original
                self.assertEqual(cpu.calls,1)
                self.assertEqual(cpu.r[8:16],original[0][8:16])
                self.assertEqual((cpu.pr,cpu.gbr,cpu.mach,cpu.macl),original[1:5])
                self.assertEqual(cpu.sr&~1,original[5]&~1)
                self.assertEqual((cpu.fpscr,cpu.fr),(original[6],original[7]))
                self.assertEqual(min(cpu.stores),STACK-40)


if __name__=='__main__':
    parser=argparse.ArgumentParser(add_help=False)
    parser.add_argument('--json',type=Path)
    args,remaining=parser.parse_known_args()
    program=unittest.main(argv=[sys.argv[0],*remaining],exit=False)
    passed=program.result.wasSuccessful()
    if args.json:
        files=('src/loader/vc2_texture_probe.c','src/loader/vc2_texture_probe_entry.S',
               'include/kui/vc2_texture_probe.h','src/loader/retail_async.c',
               'src/loader/vc2_ce_vmu_service.c',
               'tests/test_vc2_texture_probe.py')
        report=dict(status='PASS' if passed else 'FAIL',hardware_verified=False,
            actual_production_probe_compiled=True,sanitizers=['address','undefined'],
            current_p1_context_accessor_sanitizers=['undefined'],
            pre_source_snapshot_limit_bytes=512,
            source_and_destination_early_late_distinguished=True,
            fold_alignment_length_cases=8208,utlb_page_sizes=[1024,4096,65536,1048576],
            emitted_wrapper_adversarial_cases=0 if program.result.skipped else 96,
            skipped_tests=[str(item) for item in program.result.skipped],
            checks=['full-chunk hash and first mismatch beyond initial sample',
                    'halfword-only destination readback with odd fragments and canaries',
                    'ASID/shared/privileged-SV matching and validity rejection',
                    'duplicate mappings reported ambiguous without VRAM attribution',
                    'frozen terminal snapshot performs no new MMIO/readback',
                    'no allocator MMIO reads or descriptor pointer traversal',
                    'map failure, preparation stop, target tail and 32768-call cap',
                    'actual low payload functions: owned PRE-before-copy and POST-before-progress; failed map only NULL POST',
                    'P2 PRE bounds and DMA epochs; stable-copy source mutation distinguished from bad delivery',
                    'early comparison precedes context/MMIO/P1-cache-pressure mutation callbacks',
                    'early-good/late-bad retained separately; four P1 cache colours per destination line, VA colour first; skipped for OIX',
                    'actual low scatter skip/clipping, original map-failure destination and logical stream offsets',
                    'actual current-context accessor: separate read-only P1/P2 maps with stale P2 token/LBA and trace',
                    'emitted SH wrapper preserves native stack and callee-saved CPU/FPU state'],
            source_sha256={name:hashlib.sha256((ROOT/name).read_bytes()).hexdigest() for name in files},
            limitations=['No physical VRAM or cache/TLB timing modeled',
                         'C callback body tested on host; SH model tests emitted wrapper ABI only'])
        args.json.parent.mkdir(parents=True,exist_ok=True)
        args.json.write_text(json.dumps(report,indent=2)+'\n')
    raise SystemExit(not passed)
