/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact Armada CE table checkpoints; no page or sound holder is read. */
#include "kui/armada_ce_bind_probe.h"
#include "kui/armada_ce_table_trace.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"
static struct kui_armada_ce_bind_snapshot snapshot;
static bool read_ram(void *unused,uint32_t address,uint32_t *value) {
    uint32_t p1;
    (void)unused;
    if(!value || !kui_armada_ce_ram_table_p1(address,&p1)) return false;
    *value=*(volatile const uint32_t *)(uintptr_t)p1;
    return true;
}
static void row(uint32_t *dest,const struct kui_armada_ce_table_trace *trace,
                uint32_t status) {
    dest[0]=trace->slot_value;dest[1]=trace->block_address;
    dest[2]=trace->block_value;dest[3]=status;
}
const struct kui_armada_ce_bind_snapshot *kui_armada_ce_bind_probe(
    uint32_t function,uint32_t command,int32_t result,uint32_t sample)
    __attribute__((section(".entry")));
const struct kui_armada_ce_bind_snapshot *kui_armada_ce_bind_probe(
    uint32_t function,uint32_t command,int32_t result,uint32_t sample) {
    if(sample!=1u || function!=KUI_GD_REQUEST || result<=0 ||
       (command!=KUI_RETAIL_GD_PLAY && command!=KUI_RETAIL_GD_PLAY2))
        return &snapshot;
    uint32_t ttb=*(volatile const uint32_t *)(uintptr_t)0xff000008u;
    snapshot.cpu[0]=ttb;
    snapshot.cpu[1]=*(volatile const uint32_t *)(uintptr_t)0xff000000u;
    if(snapshot.cpu[2]!=UINT32_MAX) ++snapshot.cpu[2];
    snapshot.cpu[3]=*(volatile const uint32_t *)(uintptr_t)0xff00001cu;
    snapshot.cpu[4]=snapshot.nominal[4]=snapshot.real[4]=0;
    for(unsigned i=0;i<5u;i++) snapshot.candidate[i]=0;
    unsigned selected=0;
    /* The audited kernel initializes exactly 64 TTB slots. They are address
     * slots, not a claim of 64 processes. All six sound holders have the
     * same second-level index; one two-level trace per slot is sufficient.
     * Never follow a block pointer or read page/holder/audio contents. */
    for(unsigned i=0;i<64u;i++) {
        uint32_t va=(i<<25)|(KUI_ARMADA_CE_HEAP_NOMINAL&0x01ffffffu);
        struct kui_armada_ce_table_trace trace;
        unsigned status=kui_armada_ce_trace_table(ttb,va,read_ram,0,&trace);
        if(i==0u) row(snapshot.nominal,&trace,status);
        if(i==1u) row(snapshot.real,&trace,status);
        if((status==KUI_ARMADA_CE_RAM_BIND_OK || status==KUI_ARMADA_CE_RAM_BIND_BLOCK) &&
           (trace.block_value&0x80000000u)) {
            if(i<32u) snapshot.cpu[4]|=1u<<i;
            else snapshot.nominal[4]|=1u<<(i-32u);
            ++snapshot.real[4];
            unsigned rank=status==KUI_ARMADA_CE_RAM_BIND_OK?2u:1u;
            if(rank>selected) {
                selected=rank;snapshot.candidate[0]=va;
                row(snapshot.candidate+1u,&trace,status);
            }
        }
    }
    return &snapshot;
}
