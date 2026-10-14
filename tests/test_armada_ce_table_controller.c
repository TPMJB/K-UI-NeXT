/* SPDX-License-Identifier: GPL-3.0-only */
/* Linux host fixture for the actual retained controller. Fixed anonymous
 * mappings stand in for main RAM/MMIO; block pages are inaccessible. */
#define _GNU_SOURCE
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/mman.h>
#include "kui/armada_ce_bind_probe.h"
#include "kui/armada_ce_table_trace.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"
static void *fixed(uint32_t addr,size_t n) {
 void *p=mmap((void*)(uintptr_t)addr,n,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
 assert(p==(void*)(uintptr_t)addr);return p;
}
static void put(uint32_t addr,uint32_t value) {*(uint32_t*)(uintptr_t)addr=value;}
static const uint32_t root=0x8c1458a0u;
static void slot(unsigned i,uint32_t block) {
 uint32_t table=0x8c180000u+i*0x1000u;
 put(root+4u*i,table);put(table+0x754u,block);
}
int main(void) {
 fixed(0x8c000000u,0x1000000u);fixed(0xff000000u,0x1000u);
 put(0xff000008u,root);put(0xff000000u,0xfffffc03u);put(0xff00001cu,0x105u);
 for(unsigned i=0;i<64;i++)put(root+4u*i,KUI_ARMADA_CE_EMPTY_SLOT_TABLE);
 assert(mprotect((void*)(uintptr_t)0x8c900000u,0x10000u,PROT_NONE)==0);
 slot(0,0xac900000u);slot(31,0x8c900000u);slot(32,0xec900000u);slot(63,0x8c901000u);
 const struct kui_armada_ce_bind_snapshot *s=kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,1);
 assert(s->cpu[0]==root && s->cpu[1]==0xfffffc03u && s->cpu[2]==1 && s->cpu[3]==0x105u);
 assert(s->cpu[4]==0x80000001u && s->nominal[4]==0x80000001u && s->real[4]==4);
 assert(s->nominal[0]==0x8c180000u && s->nominal[1]==0x8c180754u && s->nominal[2]==0xac900000u && s->nominal[3]==KUI_ARMADA_CE_RAM_BIND_BLOCK);
 assert(s->real[0]==KUI_ARMADA_CE_EMPTY_SLOT_TABLE && s->real[1]==0x8c013260u && s->real[2]==0 && s->real[3]==KUI_ARMADA_CE_RAM_BIND_BLOCK);
 assert(s->candidate[0]==0x3fd550acu && s->candidate[1]==0x8c19f000u && s->candidate[2]==0x8c19f754u && s->candidate[3]==0x8c900000u && s->candidate[4]==KUI_ARMADA_CE_RAM_BIND_OK);
 /* Refused requests and terminal reporting must not touch MMIO. */
 assert(munmap((void*)(uintptr_t)0xff000000u,0x1000u)==0);
 assert(kui_armada_ce_bind_probe(0,0,0,0)==s && s->cpu[2]==1);
 assert(kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,0,1)==s && s->cpu[2]==1);
 assert(kui_armada_ce_bind_probe(KUI_GD_DRIVE,KUI_RETAIL_GD_PLAY,1,1)==s && s->cpu[2]==1);
 assert(kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_SEEK,1,1)==s && s->cpu[2]==1);
 fixed(0xff000000u,0x1000u);put(0xff000008u,root);
 slot(31,0);s=kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,2,1);
 assert(s->cpu[2]==2 && s->cpu[4]==1 && s->nominal[4]==0x80000001u && s->real[4]==3);
 assert(s->candidate[0]==0x7fd550acu && s->candidate[3]==0x8c901000u && s->candidate[4]==KUI_ARMADA_CE_RAM_BIND_OK);
 slot(63,0);s=kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,3,1);
 assert(s->real[4]==2 && s->candidate[0]==0x01d550acu && s->candidate[3]==0xac900000u && s->candidate[4]==KUI_ARMADA_CE_RAM_BIND_BLOCK);
 /* A P2 slot must not be followed through its P1 alias. */
 put(root+4u*32u,0xac1a0000u);
 assert(mprotect((void*)(uintptr_t)0x8c1a0000u,0x1000u,PROT_NONE)==0);
 s=kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,4,1);
 assert(s->real[4]==1 && s->nominal[4]==0);
 assert(s->candidate[0]==0x01d550acu && s->candidate[4]==KUI_ARMADA_CE_RAM_BIND_BLOCK);
 assert(mprotect((void*)(uintptr_t)0x8c1a0000u,0x1000u,PROT_READ|PROT_WRITE)==0);
 slot(0,0);slot(32,0);s=kui_armada_ce_bind_probe(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,5,1);
 assert(s->cpu[2]==5 && !s->cpu[4] && !s->nominal[4] && !s->real[4]);
 for(unsigned i=0;i<5;i++)assert(s->candidate[i]==0);
 puts("Actual table controller: mask edges0/31/32/63, candidate ranking/reset, admission and no block dereference passed (UBSan).");
 return 0;
}
