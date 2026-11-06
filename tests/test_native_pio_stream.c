/* SPDX-License-Identifier: GPL-3.0-only */
/* Production core only. CPU maps, data progress and event draining are
 * authored; no callback, payload reader, target instruction or CE executes. */
#include "kui/retail_gd.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CPU UINT32_C(0x02040000)
#define P3 UINT32_C(0xc0010000)
#define PROCESS UINT32_C(0x0c020000)
#define PHYS UINT32_C(0x8c020000)
#define PARAM (CPU+0x100u)
#define RESULT (P3+0x100u)
#define CALLBACK (CPU+0x200u)
#define FAD 45150u
static _Alignas(32) uint8_t cpu[32768],p3[32768],process[32768],physical[32768];
static struct kui_retail_gd s;
static unsigned checks,cases,reads,maps;
static int ordinary_pio;
#define CHECK(x) do{++checks;if(!(x)){fprintf(stderr,"pio39 line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static uint8_t *map(void *unused,uint32_t address,uint32_t bytes,int writing){
    (void)unused;++maps;
    const uint32_t bases[]={CPU,P3,PROCESS,PHYS};
    uint8_t *banks[]={cpu,p3,process,physical};
    for(unsigned i=0;i<4;i++)if(address>=bases[i] && address-bases[i]<=sizeof(cpu) && bytes<=sizeof(cpu)-(address-bases[i])){
        if(writing==KUI_RETAIL_MAP_VALIDATE && i<3u &&
           !(KUI_NATIVE_GD_PIO_STREAM && (ordinary_pio || s.command==39u)))return NULL;
        return banks[i]+address-bases[i];
    }
    return NULL;
}
static void put(uint32_t address,uint32_t value){uint8_t *p=map(NULL,address,4u,1);CHECK(p);memcpy(p,&value,4u);}
#if KUI_NATIVE_GD_PIO_STREAM
static uint32_t get(uint32_t address){uint32_t v;uint8_t *p=map(NULL,address,4u,0);CHECK(p);memcpy(&v,p,4u);return v;}
#endif
static int range(void *unused,uint32_t lba,uint32_t count,uint32_t bytes){
    (void)unused;return lba>=45000u && lba<500000u && count<=500000u-lba && bytes==2048u?0:-1;
}
static int reader(void *unused,uint32_t lba,uint32_t count,uint32_t bytes,void *out){
    (void)unused;(void)lba;(void)count;(void)bytes;(void)out;++reads;return -1;
}
static int gd(uint32_t fn,uint32_t a,uint32_t b){return kui_retail_gd_dispatch(&s,a,b,0u,fn);}
static void fresh(void){
    static const union kui_retail_slot tracks[]={{.track={.start_lba=45000u,.end_lba=500000u,.control=4u}}};
    const struct kui_gd_ops ops={NULL,map,range,reader};
    memset(cpu,0,sizeof(cpu));memset(p3,0,sizeof(p3));memset(process,0x36,sizeof(process));memset(physical,0xa5,sizeof(physical));ordinary_pio=0;
    CHECK(kui_retail_gd_init(&s,tracks,1u,&ops,0x8c008000u,0x8d000000u)==0);s.cpu_mmu=1u;
}
static int request(uint32_t cmd,uint32_t count,uint32_t hint){put(PARAM,FAD);put(PARAM+4u,count);put(PARAM+8u,hint);return gd(KUI_GD_REQUEST,cmd,PARAM);}
static void reject(uint32_t fn,uint32_t a,uint32_t b,int expected){
    struct kui_retail_gd before=s;CHECK(gd(fn,a,b)==expected);s.diag=before.diag;CHECK(memcmp(&before,&s,sizeof(s))==0);++cases;
}
#if KUI_NATIVE_GD_PIO_STREAM
static void check_status(uint32_t token,int status,uint32_t bytes,uint32_t ata){
    CHECK(gd(KUI_GD_CHECK,token,RESULT)==status);CHECK(get(RESULT+8u)==bytes);CHECK(get(RESULT+12u)==ata);
}
static unsigned events(void){return kui_retail_gd_native_dma_events(&s);}
static int transfer(uint32_t token,uint32_t address,uint32_t bytes){put(PARAM,address);put(PARAM+4u,bytes);return gd(KUI_GD_PIO_TRANSFER,token,PARAM);}
static uint32_t offer(uint32_t token){put(RESULT,0xfeedfaceu);CHECK(gd(KUI_GD_PIO_CHECK,token,RESULT)==0);return get(RESULT);}
static void partial_pieces(void){
    static const uint32_t sizes[]={1u,4095u,2048u};
    static const uint32_t addresses[]={CPU+0x300u,P3+0x1000u,PROCESS+0x2000u};
    fresh();CHECK(gd(KUI_GD_PIO_CALLBACK,CALLBACK,P3+0x700u)==0);
    int token=request(39u,3u,0x12345678u);CHECK(token>0 && s.pending && s.status==3);
    CHECK(s.pio_callback==CALLBACK && s.pio_argument==P3+0x700u && !s.callback_due);
    uint32_t completed=0;
    for(unsigned i=0;i<3u;i++){
        CHECK(offer((uint32_t)token)==(6144u-completed>4096u?4096u:6144u-completed));
        CHECK(transfer((uint32_t)token,addresses[i],sizes[i])==0);
        CHECK(s.destination==addresses[i] && s.outputs[0]==sizes[i] && !s.callback_due && !events());
        CHECK(offer((uint32_t)token)==0u);
        CHECK(gd(KUI_GD_EXEC,0u,0u)==0 && !s.callback_due && !reads);
        /* Author delivery of just this accepted piece; only after this
         * point may a separate adapter schedule the registered callback. */
        kui_retail_gd_native_dma_progress(&s,completed+sizes[i],0u);completed+=sizes[i];
        CHECK(s.completed_bytes==completed && !s.outputs[0] && s.callback_due==1u);
        CHECK(events()==KUI_RETAIL_GD_IRQ_DRIVE && !events());
        CHECK(gd(KUI_GD_EXEC,0u,0u)==0 && s.callback_due==1u);
        /* The core deliberately does not invoke or consume the callback. */
        check_status((uint32_t)token,i==2u?2:3,completed,i==2u?0u:4u);
        CHECK(s.callback_due==1u && s.pio_callback==CALLBACK);
        CHECK(gd(KUI_GD_PIO_CALLBACK,0u,0u)==0 && !s.callback_due);
        CHECK(gd(KUI_GD_PIO_CALLBACK,CALLBACK,P3+0x700u)==0);++cases;
    }
    CHECK(!s.command && !s.pending && offer((uint32_t)token)==0u && !reads);
    CHECK(physical[0x2000u]==0xa5u); /* CPU process slot differs from DMA RAM. */
}
static void refusal_and_cancel(void){
    fresh();reject(KUI_GD_PIO_CALLBACK,CALLBACK+1u,0u,-1);reject(KUI_GD_PIO_CALLBACK,0x70000000u,0u,-1);
    CHECK(gd(KUI_GD_PIO_CALLBACK,CALLBACK,0xabcdef01u)==0);
    int token=request(39u,1u,0u);CHECK(token>0);CHECK(offer((uint32_t)token+1u)==0u && offer(0u)==0u);
    reject(KUI_GD_PIO_CHECK,(uint32_t)token,RESULT+1u,-1);reject(KUI_GD_PIO_TRANSFER,(uint32_t)token,PARAM+1u,-1);
    const uint32_t bad[]={0u,4097u,UINT32_MAX,2049u};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){put(PARAM,P3+0x200u);put(PARAM+4u,bad[i]);reject(KUI_GD_PIO_TRANSFER,(uint32_t)token,PARAM,-1);}
    const uint32_t destinations[]={CPU+1u,CPU+sizeof(cpu)-2u,0x8c004000u,0xfffffffeu};
    for(unsigned i=0;i<sizeof(destinations)/sizeof(destinations[0]);i++){put(PARAM,destinations[i]);put(PARAM+4u,4u);reject(KUI_GD_PIO_TRANSFER,(uint32_t)token,PARAM,-1);}
    CHECK(transfer((uint32_t)token,CPU+0x600u,512u)==0);
    put(PARAM,P3+0x600u);put(PARAM+4u,512u);reject(KUI_GD_PIO_TRANSFER,(uint32_t)token,PARAM,-1);
    reject(KUI_GD_PIO_TRANSFER,(uint32_t)token+1u,PARAM,-1);reject(KUI_GD_REQUEST,39u,PARAM,0);
    struct kui_retail_gd before=s;kui_retail_gd_native_dma_progress(&s,513u,0u);CHECK(memcmp(&before,&s,sizeof(s))==0);
    kui_retail_gd_native_dma_progress(&s,32u,0u);CHECK(!s.callback_due && !events());
    CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==0 && !s.callback_due && !s.outputs[0]);
    CHECK(events()==KUI_RETAIL_GD_IRQ_DRIVE && !events());check_status((uint32_t)token,-1,32u,0u);++cases;
}
static void reset_error_and_native_controls(void){
    fresh();CHECK(gd(KUI_GD_PIO_CALLBACK,CALLBACK,P3)==0);int token=request(39u,1u,500000u);CHECK(token>0);
    CHECK(transfer((uint32_t)token,PROCESS+0x200u,64u)==0);
    kui_retail_gd_native_dma_progress(&s,32u,KUI_GD_ERROR_IO);
    CHECK(!s.pending && s.error==KUI_GD_ERROR_IO && s.callback_due && !s.outputs[0]);CHECK(events()==2u);
    check_status((uint32_t)token,-1,32u,0u);CHECK(s.callback_due);
    CHECK(gd(KUI_GD_INIT,0u,0u)==0 && !s.pio_callback && !s.pio_argument && !s.callback_due);++cases;
    /* Ordinary virtual PIO16 uses the same CPU address space, with its
     * actual resident admission represented by the authored mapper flag. */
    ordinary_pio=1;put(PARAM,FAD);put(PARAM+4u,1u);put(PARAM+8u,PROCESS+0x800u);put(PARAM+12u,0x12345678u);
    token=gd(KUI_GD_REQUEST,16u,PARAM);ordinary_pio=0;CHECK(token>0 && s.destination==PROCESS+0x800u);
    CHECK(gd(KUI_GD_EXEC,0u,0u)==0 && !reads);kui_retail_gd_progress(&s,1u,0u);check_status((uint32_t)token,2,2048u,0u);++cases;
    /* AT0 keeps the native test-flag rejection even with the new profile. */
    s.cpu_mmu=0u;put(PHYS+0x100u,FAD);put(PHYS+0x104u,1u);put(PHYS+0x108u,PHYS+0x800u);put(PHYS+0x10cu,0x12345678u);
    reject(KUI_GD_REQUEST,16u,PHYS+0x100u,0);s.cpu_mmu=1u;
    /* The driver's ordinary DMA17 packet carries the same opaque hint,
     * while its destination still names physical rather than CPU memory. */
    put(PARAM,FAD);put(PARAM+4u,1u);put(PARAM+8u,PROCESS+0x800u);put(PARAM+12u,0x12345678u);
    token=gd(KUI_GD_REQUEST,17u,PARAM);CHECK(token>0 && s.destination==PROCESS+0x800u);
    CHECK(gd(KUI_GD_EXEC,0u,0u)==0 && !reads);check_status((uint32_t)token,1,0u,4u);
    kui_retail_gd_progress(&s,1u,0u);check_status((uint32_t)token,2,2048u,0u);++cases;
    s.cpu_mmu=0u;reject(KUI_GD_REQUEST,17u,PHYS+0x100u,0);s.cpu_mmu=1u;
    /* DMA38 retains its physical target, event21 and byte budget. */
    token=request(38u,1u,0u);CHECK(token>0);put(PARAM,PROCESS+0x100u);put(PARAM+4u,2048u);
    CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM)==0 && s.destination==PROCESS+0x100u);
    kui_retail_gd_native_dma_progress(&s,2048u,0u);CHECK(!s.callback_due && events()==3u);
    check_status((uint32_t)token,2,2048u,0u);++cases;
    CHECK(!reads);
}
#endif
int main(void){
    fresh();
#if KUI_NATIVE_GD_PIO_STREAM
    partial_pieces();refusal_and_cancel();reset_error_and_native_controls();
#else
    CHECK(request(39u,1u,0u)==0);
    reject(KUI_GD_REQUEST,39u,PARAM,0);reject(KUI_GD_PIO_CALLBACK,CALLBACK,CPU,-1);
    reject(KUI_GD_PIO_TRANSFER,1u,PARAM,-1);reject(KUI_GD_PIO_CHECK,1u,RESULT,-1);
    put(PARAM,FAD);put(PARAM+4u,1u);put(PARAM+8u,PHYS+0x800u);put(PARAM+12u,0x12345678u);
    reject(KUI_GD_REQUEST,16u,PARAM,0);
    reject(KUI_GD_REQUEST,17u,PARAM,0);
#endif
    printf("{\"checks\":%u,\"cases\":%u,\"enabled\":%s,\"service_bytes\":%zu,\"io_reads\":%u,\"hardware_executed\":false}\n",checks,cases,KUI_NATIVE_GD_PIO_STREAM?"true":"false",sizeof(s),reads);
    return 0;
}
