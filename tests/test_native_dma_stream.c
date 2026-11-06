/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual production core; CPU map, byte-progress and IRQ drain are authored.
 * No reader payload, target instruction, peripheral or CE kernel executes. */
#include "kui/retail_gd.h"
#include "kui/retail_vram_dma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PHYS UINT32_C(0x8c020000)
#define VIRTUAL UINT32_C(0x0204f000)
#define PARAM (VIRTUAL+UINT32_C(0x8f0))
#define STATUS (VIRTUAL+UINT32_C(0xa10))
#define LEFT (VIRTUAL+UINT32_C(0xb20))
#define FAD UINT32_C(45150)
#define DMA38 38u
static _Alignas(32) unsigned char cpu_ram[8192], physical[32768];
static struct kui_retail_gd s;
static unsigned checks,cases,reads,maps,range_checks;
static uint32_t validated_address,validated_bytes;
static unsigned vram_maps;
static int fail_map,fail_range;
#if KUI_NATIVE_GD_DMA_STREAM
static unsigned refusal_calls;
static uint32_t refusal_reason,refusal_destination,refusal_bytes;
/* Strong replacement for the production core's weak terminal hook. */
void kui_retail_gd_native_transfer_refused(uint32_t reason,
    uint32_t destination,uint32_t bytes) {
    ++refusal_calls;refusal_reason=reason;
    refusal_destination=destination;refusal_bytes=bytes;
}
#endif
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"dma38 line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void store(uint32_t address,uint32_t value){CHECK(address>=VIRTUAL && address+4u<=VIRTUAL+sizeof(cpu_ram));memcpy(cpu_ram+address-VIRTUAL,&value,4u);}
#if KUI_NATIVE_GD_DMA_STREAM
static uint32_t load(uint32_t address){uint32_t value;CHECK(address>=VIRTUAL && address+4u<=VIRTUAL+sizeof(cpu_ram));memcpy(&value,cpu_ram+address-VIRTUAL,4u);return value;}
#endif
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing){
    (void)context;++maps;
    if(writing==KUI_RETAIL_MAP_DMA_VALIDATE || writing==KUI_RETAIL_MAP_DMA_WRITE) {
        ++vram_maps;
        CHECK(address==kui_retail_vram_dma_map(address,bytes,4u));
        /* This actual-core test validates only; payload delivery belongs to
         * the separate reader test. No oversized host backing is fabricated. */
        CHECK(writing==KUI_RETAIL_MAP_DMA_VALIDATE);
        validated_address=address;validated_bytes=bytes;
        return fail_map?NULL:physical;
    }
    if(address>=VIRTUAL && address<=VIRTUAL+sizeof(cpu_ram) && bytes<=VIRTUAL+sizeof(cpu_ram)-address)
        return writing==KUI_RETAIL_MAP_VALIDATE?NULL:cpu_ram+address-VIRTUAL;
    if(address>=PHYS && address<=PHYS+sizeof(physical) && bytes<=PHYS+sizeof(physical)-address){
        if(writing==KUI_RETAIL_MAP_VALIDATE){validated_address=address;validated_bytes=bytes;return fail_map?NULL:physical;}
        return physical+address-PHYS;
    }
    return NULL;
}
static int available(void *context,uint32_t lba,uint32_t count,uint32_t bytes){
    (void)context;++range_checks;return !fail_range && lba>=45000u && lba<500000u && count<=500000u-lba && bytes==2048u?0:-1;
}
static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out){
    (void)context;(void)lba;(void)count;(void)bytes;(void)out;++reads;return -1;
}
static int gd(uint32_t fn,uint32_t a,uint32_t b){return kui_retail_gd_dispatch(&s,a,b,0,fn);}
static void fresh(void){
    static const union kui_retail_slot tracks[]={{.track={.start_lba=45000u,.end_lba=500000u,.control=4u}}};
    const struct kui_gd_ops ops={NULL,map,available,read_data};
    memset(cpu_ram,0,sizeof(cpu_ram));memset(physical,0xa5,sizeof(physical));fail_map=fail_range=0;
    vram_maps=0u;
    CHECK(kui_retail_gd_init(&s,tracks,1u,&ops,0x8c008000u,0x8d000000u)==0);
    CHECK(gd(KUI_GD_INIT,0u,0u)==0);s.cpu_mmu=1;
#if KUI_NATIVE_GD_DMA_STREAM
    refusal_calls=0u;refusal_reason=refusal_destination=refusal_bytes=0u;
#endif
}
static int request(uint32_t fad,uint32_t sectors,uint32_t hint){store(PARAM,fad);store(PARAM+4u,sectors);store(PARAM+8u,hint);return gd(KUI_GD_REQUEST,DMA38,PARAM);}
#if KUI_NATIVE_GD_DMA_STREAM
static void same(const struct kui_retail_gd *before){
    struct kui_retail_gd a=*before,b=s;memset(&a.diag,0,sizeof(a.diag));memset(&b.diag,0,sizeof(b.diag));CHECK(!memcmp(&a,&b,sizeof(a)));
}
static void reject(uint32_t fn,uint32_t a,uint32_t b,int expected){
    struct kui_retail_gd before=s;unsigned old_reads=reads;CHECK(gd(fn,a,b)==expected);same(&before);CHECK(reads==old_reads);++cases;
}
static int piece(uint32_t token,uint32_t destination,uint32_t bytes){store(PARAM,destination);store(PARAM+4u,bytes);return gd(KUI_GD_DMA_TRANSFER,token,PARAM);}
static void poll(uint32_t token,int result,uint32_t left){store(LEFT,0xa5a5a5a5u);CHECK(gd(KUI_GD_DMA_CHECK,token,LEFT)==result);CHECK(load(LEFT)==left);}
static void status(uint32_t token,int result,uint32_t total,uint32_t phase){
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==result);CHECK(load(STATUS+8u)==total && load(STATUS+12u)==phase);
}
static uint32_t events(void){return kui_retail_gd_native_dma_events(&s);}
static void refused(uint32_t token,uint32_t packet,uint32_t reason,
                    uint32_t destination,uint32_t bytes) {
    unsigned calls=refusal_calls;
    reject(KUI_GD_DMA_TRANSFER,token,packet,-1);
    CHECK(refusal_calls==calls+1u && refusal_reason==reason);
    CHECK(refusal_destination==destination && refusal_bytes==bytes);
}
static void transfer_refusal_telemetry(void) {
    fresh();int handle=request(FAD,4u,0u);CHECK(handle>0);
    uint32_t token=(uint32_t)handle;
    store(PARAM,PHYS);store(PARAM+4u,64u);
    refused(token+1u,PARAM,KUI_RETAIL_GD_TRANSFER_OWNERSHIP,0u,0u);
    s.pending=0u;
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_STATE,0u,0u);s.pending=1u;
    s.outputs[0]=32u;
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_STATE,0u,0u);s.outputs[0]=0u;
    refused(token,PARAM+1u,KUI_RETAIL_GD_TRANSFER_PACKET,0u,0u);
    store(PARAM+4u,0u);
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_EMPTY,PHYS,0u);
    store(PARAM+4u,64u);s.completed_bytes=s.request_bytes+32u;
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_CURSOR,PHYS,64u);s.completed_bytes=0u;
    store(PARAM+4u,8224u);
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_REMAINING,PHYS,8224u);
    s.completed_bytes=8160u;store(PARAM+4u,64u);
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_REMAINING,PHYS,64u);s.completed_bytes=0u;
    store(PARAM+4u,33u);
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_BYTE_ALIGNMENT,PHYS,33u);
    /* The exact bad packet is retained whether DMA admission fails because
     * of its address kind, alignment, bounds or the adapter's policy. */
    const uint32_t bad[]={0x1c020000u,PHYS+1u,0x8c004000u,0x8cffffe0u};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        store(PARAM,bad[i]);store(PARAM+4u,64u);
        refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_DESTINATION,bad[i],64u);
    }
    store(PARAM,PHYS);store(PARAM+4u,64u);fail_map=1;
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_DESTINATION,PHYS,64u);fail_map=0;
    unsigned calls=refusal_calls;
    CHECK(piece(token,PHYS,64u)==0 && refusal_calls==calls);
    /* Success never replaces assembly-owned live-entry telemetry. */
    CHECK(s.pending && s.outputs[0]==64u && !s.error && s.status==3);
    fresh();store(PARAM,FAD);store(PARAM+4u,1u);
    handle=gd(KUI_GD_REQUEST,28u,PARAM);CHECK(handle>0);token=(uint32_t)handle;
    store(PARAM,PHYS);store(PARAM+4u,32u);
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_SECTOR_ALIGNMENT,PHYS,32u);
    CHECK(piece(token,PHYS,2048u)==0 && refusal_calls==1u);
    ++cases;
}
static void vram_windows_and_photographed_piece(void) {
    /* Primary map constants preserve the 64-bit/32-bit PVR views. All
     * physical/P1/P2 aliases of each view resolve to that view's P2 window. */
    const uint32_t valid[][3]={
        {0x04000000u,32u,0xa4000000u},{0x84000000u,32u,0xa4000000u},
        {0xa4000000u,32u,0xa4000000u},{0x047fffe0u,32u,0xa47fffe0u},
        {0x05000000u,32u,0xa5000000u},{0x85000000u,32u,0xa5000000u},
        {0xa5000000u,32u,0xa5000000u},{0x057fffe0u,32u,0xa57fffe0u},
        {0x04575600u,0x80000u,0xa4575600u}};
    for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);++i)
        CHECK(kui_retail_vram_dma_map(valid[i][0],valid[i][1],32u)==valid[i][2]);
    const uint32_t bad[][2]={
        {0x04000000u,0u},{0x04000001u,32u},{0x04000000u,33u},
        {0x047fffe0u,64u},{0x04800000u,32u},{0x04ffffe0u,32u},
        {0x057fffe0u,64u},{0x05800000u,32u},{0x05ffffe0u,32u},
        {0x005f8000u,32u},{0x00800000u,32u},{0x06000000u,32u},
        {0x0c020000u,32u},{0x10000000u,32u},{0x11000000u,32u},
        {0x13000000u,32u},{0x24000000u,32u},{0xc4000000u,32u},
        {0xe4000000u,32u},{0xffffffe0u,64u}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i)
        CHECK(!kui_retail_vram_dma_map(bad[i][0],bad[i][1],32u));
    CHECK(!kui_retail_vram_dma_map(0x04000000u,32u,0u));
    CHECK(!kui_retail_vram_dma_map(0x04000000u,32u,3u));
    fresh();
    /* VRAM addresses do not become CPU parameter or result buffers. */
    reject(KUI_GD_REQUEST,DMA38,0xa4575600u,0);
    int handle=request(FAD,257u,0u);CHECK(handle>0);uint32_t token=(uint32_t)handle;
    reject(KUI_GD_CHECK,token,0xa4575600u,-1);CHECK(!vram_maps);
    CHECK(s.request_bytes==0x80800u);
    CHECK(piece(token,PHYS,32u)==0);kui_retail_gd_native_dma_progress(&s,32u,0u);
    CHECK(events()==KUI_RETAIL_GD_IRQ_DMA_END && s.completed_bytes==0x20u);
    store(PARAM,0x04575600u);store(PARAM+4u,0x80000u);
#if KUI_NATIVE_GD_VRAM_DMA
    CHECK(gd(KUI_GD_DMA_TRANSFER,token,PARAM)==0);
    CHECK(validated_address==0xa4575600u && validated_bytes==0x80000u && vram_maps==1u);
    CHECK(s.destination==0x04575600u && s.outputs[0]==0x80000u && s.pending && !s.error);
    kui_retail_gd_native_dma_progress(&s,0x80020u,0u);
    CHECK(events()==KUI_RETAIL_GD_IRQ_DMA_END && s.pending && s.status==3);
    CHECK(piece(token,PHYS,0x7e0u)==0);
    kui_retail_gd_native_dma_progress(&s,0x80800u,0u);
    CHECK(events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));
    CHECK(!s.pending && s.status==2 && !s.error);
    status(token,2,0x80800u,0u);
    /* Every admitted alias uses dedicated DMA validation; PVR32 retains a5. */
    for(unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);++i) {
        fresh();handle=request(FAD,257u,0u);CHECK(handle>0);token=(uint32_t)handle;
        CHECK(piece(token,valid[i][0],valid[i][1])==0 && vram_maps==1u);
        CHECK(validated_address==valid[i][2] && validated_bytes==valid[i][1]);
    }
    fresh();handle=request(FAD,4u,0u);CHECK(handle>0);token=(uint32_t)handle;
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        if(bad[i][0]==0x0c020000u)continue; /* Main RAM retains its separate path. */
        store(PARAM,bad[i][0]);store(PARAM+4u,bad[i][1]);
        reject(KUI_GD_DMA_TRANSFER,token,PARAM,-1);
    }
    CHECK(!vram_maps);
    fail_map=1;store(PARAM,0x04575600u);store(PARAM+4u,64u);
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_DESTINATION,0x04575600u,64u);
    CHECK(vram_maps==1u);fail_map=0;
    /* Ordinary DMA17 can validate the same physical view; PIO16 cannot. */
    fresh();store(PARAM,FAD);store(PARAM+4u,1u);store(PARAM+8u,0x04575600u);store(PARAM+12u,0u);
    CHECK(gd(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM)>0 && vram_maps==1u);
    fresh();store(PARAM,FAD);store(PARAM+4u,1u);store(PARAM+8u,0xa4575600u);store(PARAM+12u,0u);
    reject(KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM,0);CHECK(!vram_maps);
#else
    refused(token,PARAM,KUI_RETAIL_GD_TRANSFER_DESTINATION,0x04575600u,0x80000u);
    CHECK(!vram_maps && s.pending && s.completed_bytes==0x20u && !s.outputs[0]);
#endif
    CHECK(!reads);++cases;
}
static void three_scatter_pieces(void){
    fresh();int token=request(FAD,4u,0x81234u);CHECK(token>0);
    CHECK(s.pending && s.status==3 && s.count==4u && s.lba==45000u && s.request_bytes==8192u);
    CHECK(!s.outputs[0] && !events() && s.outputs[2]==0x81234u);
    CHECK(gd(KUI_GD_EXEC,0u,0u)==0 && !reads);status((uint32_t)token,3,0u,4u);poll((uint32_t)token,0,0u);
    /* Real driver permits physical MDL offsets in 32-byte increments, so
     * the first/final pieces need not be complete 2048-byte sectors. */
    CHECK(piece((uint32_t)token,0x0c020020u,2016u)==0);
    CHECK(validated_address==PHYS+32u && validated_bytes==2016u);
    CHECK(s.destination==0x0c020020u && s.outputs[0]==2016u);poll((uint32_t)token,1,2016u);
    kui_retail_gd_native_dma_progress(&s,32u,0u);CHECK(s.completed_bytes==32u && s.outputs[0]==1984u && !events());
    CHECK(s.destination==0x0c020040u && s.lba==45000u);status((uint32_t)token,3,32u,4u);
    kui_retail_gd_native_dma_progress(&s,2016u,0u);CHECK(events()==KUI_RETAIL_GD_IRQ_DMA_END && !events());
    CHECK(s.pending && !s.outputs[0] && s.status==3 && s.command==DMA38);poll((uint32_t)token,0,0u);
    CHECK(piece((uint32_t)token,0xac021000u,4096u)==0);CHECK(validated_address==PHYS+4096u && validated_bytes==4096u);
    kui_retail_gd_native_dma_progress(&s,4096u,0u);CHECK(!events() && s.outputs[0]==2016u);
    kui_retail_gd_native_dma_progress(&s,6112u,0u);CHECK(events()==KUI_RETAIL_GD_IRQ_DMA_END);status((uint32_t)token,3,6112u,4u);
    CHECK(piece((uint32_t)token,PHYS+8192u,2080u)==0);kui_retail_gd_native_dma_progress(&s,8192u,0u);
    CHECK(events()==(KUI_RETAIL_GD_IRQ_DMA_END|KUI_RETAIL_GD_IRQ_DRIVE));CHECK(!events());
    CHECK(!s.pending && !s.outputs[0] && s.status==2 && s.lba==45000u && s.position_lba==45003u);
    poll((uint32_t)token,0,0u);status((uint32_t)token,2,8192u,0u);CHECK(!s.command);
    CHECK(gd(KUI_GD_CHECK,0u,STATUS)==0 && !events() && !reads);++cases;
}
static void bounds_and_ownership(void){
    fresh();const uint32_t bad[][2]={{149u,1u},{720000u,1u},{FAD,0u},{719999u,2u},{FAD,UINT32_MAX}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);++i){store(PARAM,bad[i][0]);store(PARAM+4u,bad[i][1]);store(PARAM+8u,0u);reject(KUI_GD_REQUEST,DMA38,PARAM,0);}
    reject(KUI_GD_REQUEST,DMA38,PARAM+1u,0);reject(KUI_GD_REQUEST,DMA38,VIRTUAL+sizeof(cpu_ram)-8u,0);
    fail_range=1;CHECK(request(FAD,1u,0u)==0);fail_range=0;
    int token=request(FAD,4u,UINT32_MAX);CHECK(token>0 && s.outputs[2]==UINT32_MAX);
    reject(KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,0u,0);reject(KUI_GD_DATATYPE,PARAM,0u,-1);
    store(PARAM,PHYS);store(PARAM+4u,32u);unsigned count=maps;
    reject(KUI_GD_DMA_TRANSFER,0u,PARAM,-1);reject(KUI_GD_DMA_TRANSFER,(uint32_t)token+1u,PARAM,-1);CHECK(maps==count);
    reject(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM+1u,-1);reject(KUI_GD_DMA_TRANSFER,(uint32_t)token,VIRTUAL+sizeof(cpu_ram)-4u,-1);
    const uint32_t lengths[]={0u,1u,31u,33u,UINT32_MAX,8224u};
    for(unsigned i=0;i<sizeof(lengths)/sizeof(lengths[0]);++i){store(PARAM,PHYS);store(PARAM+4u,lengths[i]);reject(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);}
    const uint32_t destinations[]={PHYS+1u,0x1c020000u,0x8c004000u,0x8cffffe0u,PARAM};
    for(unsigned i=0;i<sizeof(destinations)/sizeof(destinations[0]);++i){store(PARAM,destinations[i]);store(PARAM+4u,64u);reject(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);}
    store(PARAM,PHYS);store(PARAM+4u,64u);fail_map=1;reject(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);fail_map=0;
    CHECK(piece((uint32_t)token,PHYS,64u)==0);reject(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);
    store(LEFT,0xa5a5a5a5u);reject(KUI_GD_DMA_CHECK,0u,LEFT,-1);CHECK(load(LEFT)==0xa5a5a5a5u);
    reject(KUI_GD_DMA_CHECK,(uint32_t)token+1u,LEFT,-1);reject(KUI_GD_DMA_CHECK,(uint32_t)token,LEFT+1u,-1);
    struct kui_retail_gd before=s;kui_retail_gd_native_dma_progress(&s,96u,0u);same(&before);
    kui_retail_gd_native_dma_progress(&s,32u,0u);before=s;kui_retail_gd_native_dma_progress(&s,16u,0u);same(&before);
    CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==0);CHECK(events()==3u);status((uint32_t)token,-1,32u,0u);
}
static void resets_errors_and_isolation(void){
    fresh();int token=request(FAD,511u,0u);CHECK(token>0 && s.request_bytes==511u*2048u);
    CHECK(gd(KUI_GD_RESET,0u,0u)==0 && !s.command && !s.pending);CHECK(!events());++cases;
    token=request(FAD,1u,0u);CHECK(token>0 && piece((uint32_t)token,PHYS,2048u)==0);
    kui_retail_gd_native_dma_progress(&s,64u,KUI_GD_ERROR_IO);CHECK(events()==3u && !s.pending && s.error==KUI_GD_ERROR_IO);
    reject(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);status((uint32_t)token,-1,64u,0u);CHECK(!s.command);++cases;
    token=request(FAD,1u,0u);CHECK(token>0 && piece((uint32_t)token,PHYS,32u)==0);
    CHECK(gd(KUI_GD_RESET,0u,0u)==0);struct kui_retail_gd before=s;kui_retail_gd_native_dma_progress(&s,32u,0u);same(&before);CHECK(!events());++cases;
    /* Legacy28 remains whole-sector based and accepts its successful ABORT.
     * New command38 does not leak a nonzero callback or PIO39 implementation. */
    store(PARAM,FAD);store(PARAM+4u,1u);token=gd(KUI_GD_REQUEST,28u,PARAM);CHECK(token>0 && !s.pending && s.status==3);
    CHECK(piece((uint32_t)token,PHYS,32u)==-1);CHECK(piece((uint32_t)token,PHYS,2048u)==0);
    kui_retail_gd_progress(&s,1u,0u);CHECK(s.status==3 && !events());
    CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==0);status((uint32_t)token,2,2048u,0u);CHECK(!s.command);
    store(PARAM,FAD);store(PARAM+4u,511u);reject(KUI_GD_REQUEST,28u,PARAM,0);
#if !KUI_NATIVE_GD_PIO_STREAM
    reject(KUI_GD_REQUEST,39u,PARAM,0);reject(KUI_GD_PIO_CALLBACK,PHYS,0u,-1);
#endif
    reject(KUI_GD_DMA_CALLBACK,PHYS,0u,-1);
    CHECK(!reads);++cases;
}
#endif
int main(void){
    fresh();
#if KUI_NATIVE_GD_DMA_STREAM
    three_scatter_pieces();bounds_and_ownership();resets_errors_and_isolation();transfer_refusal_telemetry();vram_windows_and_photographed_piece();
#else
    CHECK(request(FAD,1u,0u)==0 && !s.command);CHECK(gd(KUI_GD_DMA_TRANSFER,1u,PARAM)==-1);++cases;
#endif
    printf("{\"checks\":%u,\"cases\":%u,\"enabled\":%s,\"vram_enabled\":%s,\"service_bytes\":%zu,\"io_reads\":%u,\"hardware_executed\":false}\n",checks,cases,KUI_NATIVE_GD_DMA_STREAM?"true":"false",KUI_NATIVE_GD_VRAM_DMA?"true":"false",sizeof(s),reads);
    return 0;
}
