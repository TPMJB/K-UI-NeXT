/* SPDX-License-Identifier: GPL-3.0-only */
/* Production native GD core with authored CPU mappings, no MMIO/TLB or IO. */
#include "kui/retail_gd.h"
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if !KUI_NATIVE_GD_MMU_BUFFERS
#error This test requires the opt-in native MMU CPU-buffer policy
#endif
#define MAIN UINT32_C(0x8c020000)
#define U0 UINT32_C(0x00100000)
#define SLOT UINT32_C(0x0c020000)
#define P3 UINT32_C(0xc0100000)
#define CACHE UINT32_C(0x7dfff000)
#define FAD 45150u
static _Alignas(32) unsigned char main_ram[8192],virtual_ram[4][4096];
static const uint32_t virtual_base[]={U0,SLOT,P3,CACHE};
struct access {uint32_t address,bytes;int writing;};
static struct access access_log[4096];
static unsigned access_count,checks,cases,reads;
static struct kui_retail_gd s;
static bool model_at;
static uint32_t fail_address;
static int fail_writing;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"MMU buffers line %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned char *bank(uint32_t a,uint32_t n){
    if(a>=MAIN && a-MAIN<=sizeof(main_ram) && n<=sizeof(main_ram)-(a-MAIN))return main_ram+a-MAIN;
    for(unsigned i=0;i<4u;i++)
        if(a>=virtual_base[i] && a-virtual_base[i]<=sizeof(virtual_ram[i]) && n<=sizeof(virtual_ram[i])-(a-virtual_base[i]))return virtual_ram[i]+a-virtual_base[i];
    return NULL;
}
static void store(uint32_t a,uint32_t n){unsigned char *p=bank(a,4u);CHECK(p);memcpy(p,&n,4u);}
static uint32_t load(uint32_t a){uint32_t n;unsigned char *p=bank(a,4u);CHECK(p);memcpy(&n,p,4u);return n;}
static unsigned char *map(void *context,uint32_t a,uint32_t n,int writing){
    (void)context;CHECK(access_count<sizeof(access_log)/sizeof(access_log[0]));
    access_log[access_count++]=(struct access){a,n,writing};
    if(a==fail_address && (fail_writing<0 || fail_writing==writing))return NULL;
    if(!n || n>UINT32_MAX-a)return NULL;
    if((a&0xff000000u)==0x8c000000u)return bank(a,n);
    if(!model_at || writing==KUI_RETAIL_MAP_VALIDATE)return NULL;
    return bank(a,n);
}
static int available(void *context,uint32_t lba,uint32_t count,uint32_t bytes){
    (void)context;return lba>=45000u && lba<60000u && count<=60000u-lba && bytes==2048u?0:-1;
}
static int io(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out){
    (void)context;(void)lba;(void)count;(void)bytes;(void)out;++reads;return -1;
}
static int gd(uint32_t fn,uint32_t a,uint32_t b){return kui_retail_gd_dispatch(&s,a,b,0u,fn);}
static void fresh(bool cpu_mmu){
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0u,.end_lba=300u,.control=4u}},
        {.track={.start_lba=45000u,.end_lba=60000u,.control=4u}}};
    const struct kui_gd_ops ops={NULL,map,available,io};
    memset(main_ram,0xa5,sizeof(main_ram));memset(virtual_ram,0,sizeof(virtual_ram));
    access_count=0;fail_address=0;fail_writing=-1;model_at=cpu_mmu;
    CHECK(kui_retail_gd_init(&s,tracks,2u,&ops,0x8c008000u,0x8d000000u)==0);
    CHECK(!s.cpu_mmu);s.cpu_mmu=cpu_mmu;
}
static void same_protocol(const struct kui_retail_gd *old){
    struct kui_retail_gd a,b;memcpy(&a,old,sizeof(a));memcpy(&b,&s,sizeof(b));
    memset(&a.diag,0,sizeof(a.diag));memset(&b.diag,0,sizeof(b.diag));CHECK(!memcmp(&a,&b,sizeof(a)));
}
static void refused(uint32_t fn,uint32_t a,uint32_t b,int expected){
    struct kui_retail_gd old;memcpy(&old,&s,sizeof(old));unsigned before=reads;
    CHECK(gd(fn,a,b)==expected);same_protocol(&old);CHECK(reads==before);++cases;
}
static void mapped(unsigned index,uint32_t a,uint32_t n,int writing){
    CHECK(index<access_count);CHECK(access_log[index].address==a && access_log[index].bytes==n && access_log[index].writing==writing);
}
static void words(uint32_t a,const uint32_t *values,unsigned n){for(unsigned i=0;i<n;i++)store(a+4u*i,values[i]);}
static void cpu_responses(uint32_t base){
    fresh(true);const uint32_t status=base+0x100u,param=base+0x180u,out=base+0x200u;
    CHECK(gd(KUI_GD_INIT,0,0)==0 && s.cpu_mmu);
    int token=gd(KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,0);CHECK(token>0);
    CHECK(gd(KUI_GD_EXEC,0,0)==0);unsigned start=access_count;
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,status)==2 && !s.command);
    CHECK(access_count==start+1u);mapped(start,status,16u,1);
    for(unsigned i=0;i<4u;i++)CHECK(load(status+4u*i)==0u);
    start=access_count;CHECK(gd(KUI_GD_DRIVE,out,0)==0);mapped(start,out,8u,1);
    CHECK(load(out)==1u && load(out+4u)==0x80u);
    CHECK(load(MAIN+0x200u)==0xa5a5a5a5u); /* SLOT is a distinct CPU bank. */
    const uint32_t query[]={1u,0u,0u,0u};words(param,query,4u);start=access_count;
    CHECK(gd(KUI_GD_DATATYPE,param,0)==0);mapped(start,param,16u,0);mapped(start+1u,param,16u,1);
    CHECK(load(param+4u)==0x2000u && load(param+8u)==0u && load(param+12u)==2048u);
    const uint32_t toc_params[]={1u,out};words(param,toc_params,2u);start=access_count;
    token=gd(KUI_GD_REQUEST,KUI_GD_GETTOC2,param);CHECK(token>0);
    mapped(start,param,8u,0);mapped(start+1u,out,KUI_GD_TOC_BYTES,1);
    CHECK(gd(KUI_GD_EXEC,0,0)==0);mapped(start+2u,out,KUI_GD_TOC_BYTES,1);
    CHECK(load(out)==UINT32_MAX && load(out+4u)==(0x41000000u|45150u));
    CHECK(load(out+396u)==0x41020000u && load(out+400u)==0x41020000u && load(out+404u)==(0x41000000u|60150u));
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,status)==2 && load(status+8u)==408u && !s.command);
    CHECK(gd(KUI_GD_RESET,0,0)==0 && s.cpu_mmu && !s.command && !s.pending);
    CHECK(gd(KUI_GD_CHECK,0,status)==0 && load(status)==0u && !reads);++cases;
}
static void invalid_cpu_buffers(void){
    fresh(true);int token=gd(KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,0);CHECK(token>0);
    CHECK(gd(KUI_GD_EXEC,0,0)==0);const uint32_t invalid[]={U0+1u,UINT32_C(0xfffffff8),U0+4092u,UINT32_C(0x90000000)};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++){
        unsigned before=access_count;refused(KUI_GD_CHECK,(uint32_t)token,invalid[i],-1);
        if(i<2u)CHECK(access_count==before);else CHECK(access_count==before+1u);
    }
    fail_address=P3+0x100u;refused(KUI_GD_CHECK,(uint32_t)token,fail_address,-1);fail_address=0;
    refused(KUI_GD_CHECK,0u,U0+0x100u,-1);CHECK(load(U0+0x100u)==5u);
    refused(KUI_GD_CHECK,(uint32_t)token+1u,P3+0x100u,-1);CHECK(load(P3+0x100u)==5u);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,SLOT+0x100u)==2 && !s.command);
    unsigned before=access_count;refused(KUI_GD_DRIVE,UINT32_C(0xfffffffc),0,-1);CHECK(access_count==before);
    const uint32_t datatype[]={1u,0u,0u,0u};words(P3,datatype,4u);
    fail_address=P3;fail_writing=1;refused(KUI_GD_DATATYPE,P3,0,-1);CHECK(load(P3+4u)==0u);fail_address=0;
    model_at=false;refused(KUI_GD_DRIVE,U0+0x100u,0,-1);model_at=true;
    const uint32_t toc[]={1u,UINT32_C(0xffffff00)};words(SLOT,toc,2u);
    before=access_count;refused(KUI_GD_REQUEST,KUI_GD_GETTOC2,SLOT,0);CHECK(access_count==before+1u);
    fail_address=SLOT;refused(KUI_GD_REQUEST,KUI_GD_GETTOC2,SLOT,0);fail_address=0;
    CHECK(!reads);
}
static void at0_cpu_aliases(void){
    const uint32_t aliases[]={SLOT+0x100u,MAIN+0x100u,UINT32_C(0xac020100)};
    for(unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++){
        fresh(false);store(SLOT+0x100u,0x12345678u);store(SLOT+0x104u,0x87654321u);
        CHECK(gd(KUI_GD_DRIVE,aliases[i],0u)==0);CHECK(access_count==1u);
        mapped(0u,MAIN+0x100u,8u,1);
        CHECK(load(MAIN+0x100u)==1u && load(MAIN+0x104u)==0x80u);
        CHECK(load(SLOT+0x100u)==0x12345678u && load(SLOT+0x104u)==0x87654321u);
        CHECK(!s.cpu_mmu && !reads);++cases;
    }
    fresh(false);const uint32_t bad[]={U0+0x100u,P3+0x100u};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        unsigned before=access_count;refused(KUI_GD_DRIVE,bad[i],0u,-1);CHECK(access_count==before);
    }
}
static void virtual_pio_payloads(void){
    const uint32_t destinations[]={U0+0x800u,P3+0x800u,SLOT+0x800u};
    for(unsigned i=0;i<sizeof(destinations)/sizeof(destinations[0]);i++){
        fresh(true);const uint32_t params[]={FAD,1u,destinations[i],0u};words(U0+0x200u,params,4u);
        refused(KUI_GD_REQUEST,KUI_GD_PIOREAD,U0+0x200u,0);CHECK(access_count==2u);
        mapped(0u,U0+0x200u,16u,0);mapped(1u,destinations[i],2048u,KUI_RETAIL_MAP_VALIDATE);
        CHECK(!reads);
    }
    fresh(true);const uint32_t params[]={FAD,1u,MAIN+0x1000u,0u};words(U0+0x200u,params,4u);
    int token=gd(KUI_GD_REQUEST,KUI_GD_PIOREAD,U0+0x200u);CHECK(token>0 && s.pending);
    CHECK(access_count==2u);mapped(0u,U0+0x200u,16u,0);mapped(1u,MAIN+0x1000u,2048u,KUI_RETAIL_MAP_VALIDATE);
    kui_retail_gd_progress(&s,1u,0u);
    CHECK(gd(KUI_GD_CHECK,(uint32_t)token,P3+0x100u)==2 && load(P3+0x108u)==2048u && !reads);++cases;
}
static void physical_dma(void){
    static const uint32_t aliases[]={UINT32_C(0x0c021000),UINT32_C(0x8c021000),UINT32_C(0xac021000)};
    for(unsigned i=0;i<3u;i++){
        fresh(true);const uint32_t params[]={FAD,1u,aliases[i],0u};words(SLOT,params,4u);
        int token=gd(KUI_GD_REQUEST,KUI_GD_DMAREAD,SLOT);CHECK(token>0);
        mapped(0u,SLOT,16u,0);mapped(1u,MAIN+0x1000u,2048u,KUI_RETAIL_MAP_VALIDATE);
        CHECK(s.destination==aliases[i] && s.pending);kui_retail_gd_progress(&s,1u,0u);
        CHECK(gd(KUI_GD_CHECK,(uint32_t)token,P3+0x100u)==2 && load(P3+0x108u)==2048u);++cases;
        fresh(true);const uint32_t stream[]={FAD,2u};words(U0,stream,2u);
        token=gd(KUI_GD_REQUEST,KUI_RETAIL_GD_DMAREAD_LEGACY_STREAM,U0);CHECK(token>0);
        const uint32_t piece[]={aliases[i],2048u};words(P3,piece,2u);unsigned start=access_count;
        CHECK(gd(KUI_GD_DMA_TRANSFER,(uint32_t)token,P3)==0);
        mapped(start,P3,8u,0);mapped(start+1u,MAIN+0x1000u,2048u,KUI_RETAIL_MAP_VALIDATE);
        CHECK(gd(KUI_GD_DMA_CHECK,(uint32_t)token,SLOT+0x100u)==1 && load(SLOT+0x100u)==2048u);
        kui_retail_gd_progress(&s,1u,0u);
        CHECK(gd(KUI_GD_DMA_CHECK,(uint32_t)token,SLOT+0x100u)==0 && load(SLOT+0x100u)==0u);
        CHECK(gd(KUI_GD_ABORT,(uint32_t)token,0)==0);CHECK(gd(KUI_GD_CHECK,(uint32_t)token,P3+0x100u)==2);++cases;
    }
    static const uint32_t bad[]={U0,P3,CACHE,UINT32_C(0x0c021001),UINT32_C(0x0cffffff),UINT32_C(0x8c004000)};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++){
        fresh(true);const uint32_t params[]={FAD,1u,bad[i],0u};words(SLOT,params,4u);unsigned before=access_count;
        refused(KUI_GD_REQUEST,KUI_GD_DMAREAD,SLOT,0);CHECK(access_count==before+1u);mapped(before,SLOT,16u,0);
        const uint32_t stream[]={FAD,2u};words(U0,stream,2u);int token=gd(KUI_GD_REQUEST,KUI_RETAIL_GD_DMAREAD_LEGACY_STREAM,U0);CHECK(token>0);
        const uint32_t piece[]={bad[i],2048u};words(P3,piece,2u);before=access_count;
        refused(KUI_GD_DMA_TRANSFER,(uint32_t)token,P3,-1);CHECK(access_count==before+1u);mapped(before,P3,8u,0);
    }
    fresh(true);const uint32_t params[]={FAD,1u,MAIN+0x1000u,0u};words(U0,params,4u);
    fail_address=MAIN+0x1000u;fail_writing=KUI_RETAIL_MAP_VALIDATE;refused(KUI_GD_REQUEST,KUI_GD_DMAREAD,U0,0);
    CHECK(!reads);
}
int main(void){
    for(unsigned i=0;i<4u;i++)cpu_responses(virtual_base[i]);
    invalid_cpu_buffers();at0_cpu_aliases();virtual_pio_payloads();physical_dma();
    printf("{\"checks\":%u,\"cases\":%u,\"cpu_mmu_offset\":%zu,\"diag_offset\":%zu,\"service_bytes\":%zu,\"io_reads\":%u,\"hardware_executed\":false}\n",checks,cases,offsetof(struct kui_retail_gd,cpu_mmu),offsetof(struct kui_retail_gd,diag),sizeof(s),reads);
    return 0;
}
