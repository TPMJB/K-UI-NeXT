/* SPDX-License-Identifier: GPL-3.0-only */
/* Production GD core, authored CPU map/progress. No payload IO or hardware. */
#include "kui/retail_gd.h"
#include "kui/retail_ocram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BASE UINT32_C(0x8c020000)
#define PARAM UINT32_C(0x7e000ee0)
#define STATUS UINT32_C(0x7e000f00)
#define LEFT UINT32_C(0x7e000f10)
#define READ28 28u
#define FAD UINT32_C(0x8158d)
#define SECTORS UINT32_C(0x383)
#define BYTES (SECTORS*2048u)
static _Alignas(32) unsigned char ram[4096],ocram[8192];
static struct kui_retail_gd s;
static unsigned checks,cases,map_calls,range_checks,reads;
static bool fail_range,fail_destination;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"legacy line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char *cache(uint32_t a){return ocram+(a&0xfffu)+((a&0x2000u)?4096u:0u);}
static unsigned char *cpu(uint32_t a){return kui_retail_ocram_address(a)?cache(a):ram+a-BASE;}
static void store(uint32_t a,uint32_t value){memcpy(cpu(a),&value,4u);}
static uint32_t load(uint32_t a){uint32_t value;memcpy(&value,cpu(a),4u);return value;}
static uint8_t *map(void *ctx,uint32_t a,uint32_t n,int writing){
    (void)ctx;++map_calls;
    if(kui_retail_ocram_address(a))return writing!=KUI_RETAIL_MAP_VALIDATE && kui_retail_ocram_span(a,n,0u,0x121u)?cache(a):NULL;
    if(a<0x8c008000u || a>=0x8d000000u || n>0x8d000000u-a)return NULL;
    if(writing==KUI_RETAIL_MAP_VALIDATE)return fail_destination?NULL:ram; /* Never dereferenced. */
    return a>=BASE && a<=BASE+sizeof(ram) && n<=BASE+sizeof(ram)-a?ram+a-BASE:NULL;
}
static int available(void *ctx,uint32_t lba,uint32_t count,uint32_t bytes){
    (void)ctx;++range_checks;return !fail_range && lba>=45000u && lba<600000u && count<=600000u-lba && bytes==2048u?0:-1;
}
static int read_data(void *ctx,uint32_t lba,uint32_t count,uint32_t bytes,void *out){
    (void)ctx;(void)lba;(void)count;(void)bytes;(void)out;++reads;return -1;
}
static int gd(uint32_t fn,uint32_t a,uint32_t b){return kui_retail_gd_dispatch(&s,a,b,0u,fn);}
static void fresh(void){
    static const union kui_retail_slot tracks[]={{.track={.start_lba=45000u,.end_lba=600000u,.control=4u}}};
    const struct kui_gd_ops ops={NULL,map,available,read_data};
    memset(ram,0,sizeof(ram));memset(ocram,0,sizeof(ocram));fail_range=fail_destination=false;
    CHECK(kui_retail_gd_init(&s,tracks,1u,&ops,0x8c008000u,0x8d000000u)==0);
    CHECK(gd(KUI_GD_INIT,0u,0u)==0);
}
static int request(uint32_t fad,uint32_t sectors){store(PARAM,fad);store(PARAM+4u,sectors);return gd(KUI_GD_REQUEST,READ28,PARAM);}
#if KUI_NATIVE_GD_LEGACY_STREAM
static void unchanged(const struct kui_retail_gd *before){
    struct kui_retail_gd a,b;memcpy(&a,before,sizeof(a));memcpy(&b,&s,sizeof(b));
    memset(&a.diag,0,sizeof(a.diag));memset(&b.diag,0,sizeof(b.diag));CHECK(!memcmp(&a,&b,sizeof(a)));
}
static void rejected(uint32_t fn,uint32_t a,uint32_t b,int expected){
    struct kui_retail_gd before;memcpy(&before,&s,sizeof(before));unsigned io=reads;
    CHECK(gd(fn,a,b)==expected);unchanged(&before);CHECK(reads==io);++cases;
}
static int piece(uint32_t token,uint32_t destination,uint32_t bytes){store(PARAM,destination);store(PARAM+4u,bytes);return gd(KUI_GD_DMA_TRANSFER,token,PARAM);}
static void poll(uint32_t token,int expected,uint32_t left){
    store(LEFT,0xa5a5a5a5u);CHECK(gd(KUI_GD_DMA_CHECK,token,LEFT)==expected);CHECK(load(LEFT)==left);
}
static void status(uint32_t token,int expected,uint32_t bytes){
    CHECK(gd(KUI_GD_CHECK,token,STATUS)==expected);CHECK(load(STATUS+8u)==bytes);
}
static void exact_bootstrap(void){
    fresh();int token=request(FAD,SECTORS);CHECK(token>0);
    CHECK(s.request_bytes==BYTES && !s.count && !s.pending && s.command==READ28 && s.status==3);
    CHECK(gd(KUI_GD_EXEC,0u,0u)==0);status((uint32_t)token,3,0u);poll((uint32_t)token,0,0u);
    CHECK(piece((uint32_t)token,0x8ced4000u,2048u)==0);
    CHECK(s.pending && s.count==1u && s.lba==FAD-150u && s.destination==0x8ced4000u);
    poll((uint32_t)token,1,2048u);CHECK(gd(KUI_GD_EXEC,0u,0u)==0);
    kui_retail_gd_progress(&s,1u,0u);CHECK(!s.pending && !s.count && s.lba==FAD-149u && s.command==READ28);
    status((uint32_t)token,3,2048u);poll((uint32_t)token,0,0u);
    CHECK(piece((uint32_t)token,0x0c010000u,0x1c1000u)==0);
    CHECK(s.pending && s.count==898u && s.outputs[0]==2048u && !s.completed_bytes);
    poll((uint32_t)token,1,0x1c1000u);
    kui_retail_gd_progress(&s,17u,0u);status((uint32_t)token,3,18u*2048u);poll((uint32_t)token,1,(898u-17u)*2048u);
    kui_retail_gd_progress(&s,10u,0u);CHECK(s.completed_bytes==17u*2048u);
    kui_retail_gd_progress(&s,898u,0u);CHECK(!s.pending && !s.count && s.lba==FAD-150u+SECTORS);
    status((uint32_t)token,3,BYTES);poll((uint32_t)token,0,0u);
    CHECK(gd(KUI_GD_DMA_CHECK,(uint32_t)token,0u)==0);
    CHECK(piece((uint32_t)token,0x0c010000u,2048u)==-1);
    CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==0 && s.status==2 && !s.pending);
    CHECK(gd(KUI_GD_EXEC,0u,0u)==0);status((uint32_t)token,2,BYTES);CHECK(!s.command);
    CHECK(gd(KUI_GD_CHECK,0u,STATUS)==0);CHECK(gd(KUI_GD_DRIVE,STATUS,0u)==0 && load(STATUS)==1u);
    CHECK(gd(KUI_GD_DRIVE,STATUS,0u)==0 && !reads);++cases;
}
static void requests_and_pieces(void){
    fresh();const uint32_t bad[][2]={{149u,1u},{720000u,1u},{FAD,0u},{719999u,2u},{FAD,UINT32_MAX},{FAD,0x1ffu}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){store(PARAM,bad[i][0]);store(PARAM+4u,bad[i][1]);rejected(KUI_GD_REQUEST,READ28,PARAM,0);}
    rejected(KUI_GD_REQUEST,READ28,0x7e000ffcu,0);rejected(KUI_GD_REQUEST,READ28,PARAM+1u,0);
    fail_range=true;store(PARAM,FAD);store(PARAM+4u,SECTORS);rejected(KUI_GD_REQUEST,READ28,PARAM,0);fail_range=false;
    int token=request(FAD,SECTORS);CHECK(token>0);
    rejected(KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,0u,0);rejected(KUI_GD_DATATYPE,BASE,-1u,-1);
    store(PARAM,0x0c010000u);store(PARAM+4u,2048u);
    unsigned maps=map_calls;rejected(KUI_GD_DMA_TRANSFER,0u,PARAM,-1);CHECK(map_calls==maps);
    rejected(KUI_GD_DMA_TRANSFER,(uint32_t)token+1u,PARAM,-1);CHECK(map_calls==maps);
    rejected(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM+1u,-1);
    rejected(KUI_GD_DMA_TRANSFER,(uint32_t)token,0x7e000ffcu,-1);
    const uint32_t bytes[]={0u,1u,2047u,2049u,UINT32_MAX,BYTES+2048u};
    for(unsigned i=0;i<sizeof(bytes)/sizeof(bytes[0]);i++){store(PARAM+4u,bytes[i]);rejected(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);}
    const uint32_t destinations[]={0x0c010001u,0x1c010000u,0x8c004000u,0x8cffffe0u,0x7e000f00u};
    for(unsigned i=0;i<sizeof(destinations)/sizeof(destinations[0]);i++){store(PARAM,destinations[i]);store(PARAM+4u,2048u);rejected(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);}
    store(PARAM,0x0c010000u);fail_destination=true;rejected(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);fail_destination=false;
    CHECK(piece((uint32_t)token,0x8c010000u,4096u)==0);
    rejected(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);
    store(LEFT,0xa5a5a5a5u);rejected(KUI_GD_DMA_CHECK,0u,LEFT,-1);CHECK(load(LEFT)==0xa5a5a5a5u);
    rejected(KUI_GD_DMA_CHECK,(uint32_t)token+1u,LEFT,-1);CHECK(load(LEFT)==0xa5a5a5a5u);
    rejected(KUI_GD_DMA_CHECK,(uint32_t)token,LEFT+1u,-1);
    poll((uint32_t)token,1,4096u);CHECK(!reads);
    CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==0);status((uint32_t)token,2,0u);
}
static void cancellation_and_error(void){
    fresh();int token=request(FAD,SECTORS);CHECK(token>0);
    CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==0);status((uint32_t)token,2,0u);CHECK(!s.command);++cases;
    token=request(FAD,SECTORS);CHECK(token>0 && piece((uint32_t)token,0x8c010000u,4096u)==0);
    kui_retail_gd_progress(&s,1u,0u);CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==0);
    struct kui_retail_gd before;memcpy(&before,&s,sizeof(before));kui_retail_gd_progress(&s,2u,0u);unchanged(&before);
    status((uint32_t)token,2,2048u);CHECK(!s.command);++cases;
    token=request(FAD,SECTORS);CHECK(token>0 && piece((uint32_t)token,0x8c010000u,4096u)==0);
    CHECK(gd(KUI_GD_RESET,0u,0u)==0 && !s.command && !s.pending);
    memcpy(&before,&s,sizeof(before));kui_retail_gd_progress(&s,2u,0u);unchanged(&before);
    rejected(KUI_GD_DMA_CHECK,(uint32_t)token,LEFT,-1);CHECK(gd(KUI_GD_CHECK,0u,STATUS)==0);++cases;
    token=request(FAD,SECTORS);CHECK(token>0 && piece((uint32_t)token,0x8c010000u,4096u)==0);
    kui_retail_gd_progress(&s,1u,KUI_GD_ERROR_IO);CHECK(s.error==KUI_GD_ERROR_IO && !s.pending && s.command==READ28);
    rejected(KUI_GD_ABORT,(uint32_t)token,0u,-1);rejected(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);
    store(LEFT,0xa5a5a5a5u);rejected(KUI_GD_DMA_CHECK,(uint32_t)token,LEFT,-1);CHECK(load(LEFT)==0xa5a5a5a5u);
    status((uint32_t)token,KUI_GD_FAILED,2048u);CHECK(!s.command && s.error==KUI_GD_ERROR_IO);
    CHECK(gd(KUI_GD_CHECK,0u,STATUS)==0 && !reads);++cases;
}
static void ordinary_and_ce_isolation(void){
    fresh();store(PARAM,FAD);store(PARAM+4u,1u);store(PARAM+8u,0x8c010000u);store(PARAM+12u,0u);
    int token=gd(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM);CHECK(token>0 && s.command==KUI_GD_DMAREAD);
    rejected(KUI_GD_DMA_TRANSFER,(uint32_t)token,PARAM,-1);
    rejected(KUI_GD_DMA_CHECK,(uint32_t)token,LEFT,-1);
    kui_retail_gd_progress(&s,1u,0u);CHECK(s.status==KUI_GD_COMPLETED && !s.pending && s.count==1u && s.lba==FAD-150u);
    status((uint32_t)token,KUI_GD_COMPLETED,2048u);CHECK(!s.command);
    token=gd(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM);CHECK(token>0);
    CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0u)==0 && s.error==KUI_GD_ERROR_CANCELLED && s.status==KUI_GD_FAILED);
    status((uint32_t)token,KUI_GD_FAILED,0u);CHECK(!s.command);
#if !KUI_NATIVE_GD_DMA_STREAM
    rejected(KUI_GD_REQUEST,38u,PARAM,0);
#endif
#if !KUI_NATIVE_GD_PIO_STREAM
    rejected(KUI_GD_REQUEST,39u,PARAM,0);
#endif
    CHECK(!reads);++cases;
}
#endif
int main(void){
    fresh();
#if KUI_NATIVE_GD_LEGACY_STREAM
    exact_bootstrap();requests_and_pieces();cancellation_and_error();ordinary_and_ce_isolation();
#else
    CHECK(request(FAD,SECTORS)==0 && load(PARAM)==FAD);CHECK(gd(KUI_GD_DMA_TRANSFER,1u,PARAM)==-1);CHECK(gd(KUI_GD_DMA_CHECK,1u,LEFT)==-1);++cases;
#endif
    printf("{\"checks\":%u,\"cases\":%u,\"enabled\":%s,\"service_bytes\":%zu,\"io_reads\":%u,\"hardware_executed\":false}\n",checks,cases,KUI_NATIVE_GD_LEGACY_STREAM?"true":"false",sizeof(s),reads);
    return 0;
}
