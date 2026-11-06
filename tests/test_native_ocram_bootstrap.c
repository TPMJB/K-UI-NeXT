/* SPDX-License-Identifier: GPL-3.0-only */
/* Production native GD core; authored CPU RAM/cache-RAM map and live mode.
 * No MMIO, resident mapper, SD engine, SH exception or console execution. */
#include "kui/retail_gd.h"
#include "kui/retail_ocram.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define BASE UINT32_C(0x8c010000)
#define STATUS (BASE+64u)
#define DRIVE (BASE+96u)
#define DEST (BASE+1024u)
static _Alignas(4) unsigned char ram[16384],ocram[8192];
static struct kui_retail_gd service;
static unsigned checks,policy_cases,raw_maps,sector_checks,sector_reads,payload_denials;
static uint32_t mmucr,ccr;
static uint32_t trace[256];static unsigned used;
#define CHECK(x) do { ++checks; if(!(x)) {fprintf(stderr,"line %u: %s\n",__LINE__,#x);exit(1);} } while(0)
static unsigned char *cache_at(uint32_t a) {return ocram+(a&0xfffu)+((a&0x2000u)?4096u:0u);}
static void store(unsigned char *p,uint32_t n) {memcpy(p,&n,4u);}
static uint32_t value(const unsigned char *p) {uint32_t n;memcpy(&n,p,4u);return n;}
static uint8_t *map(void *ctx,uint32_t a,uint32_t n,int writing) {
    (void)ctx;
    if(kui_retail_ocram_address(a)) {
        ++raw_maps;
        CHECK(a>=0x7c000000u && a<0x80000000u); /* Original CPU alias reaches mapper. */
        if(writing==KUI_RETAIL_MAP_VALIDATE) {++payload_denials;return NULL;}
        return kui_retail_ocram_span(a,n,mmucr,ccr)?cache_at(a):NULL;
    }
    if(a<BASE || a>BASE+sizeof(ram) || n>BASE+sizeof(ram)-a)return NULL;
    return ram+a-BASE;
}
static int available(void *ctx,uint32_t lba,uint32_t n,uint32_t bytes) {
    (void)ctx;++sector_checks;return lba>=45000u && lba+n<=45200u && bytes==2048u?0:-1;
}
static int read_data(void *ctx,uint32_t lba,uint32_t n,uint32_t bytes,void *out) {
    (void)ctx;(void)lba;(void)n;(void)bytes;(void)out;++sector_reads;return -1;
}
static int call(unsigned fn,uint32_t a,uint32_t b) {return kui_retail_gd_dispatch(&service,a,b,0u,fn);}
static void initialize(void) {
    static const union kui_retail_slot track[]={{.track={.start_lba=45000u,.end_lba=45200u,.control=4u}}};
    const struct kui_gd_ops ops={NULL,map,available,read_data};
    memset(ram,0x5a,sizeof(ram));memset(ocram,0xa5,sizeof(ocram));
    mmucr=0u;ccr=0u;sector_checks=sector_reads=0u;
    CHECK(kui_retail_gd_init(&service,track,1u,&ops,BASE,BASE+sizeof(ram))==0);
    CHECK(call(KUI_GD_INIT,0u,0u)==0);
}
static void record(int result,uint32_t address,unsigned count) {
    CHECK(used+1u+count<=sizeof(trace)/sizeof(trace[0]));trace[used++]=(uint32_t)result;
    for(unsigned i=0;i<count;i++)trace[used++]=value(ram+address-BASE+4u*i);
}
static void ordinary(void) {
    initialize();
    /* Same native P1, U0-physical and P2 aliases as the accepted reader. */
    const uint32_t aliases[]={BASE,BASE&0x1fffffffu,BASE|0x20000000u};
    for(unsigned alias=0;alias<3u;alias++) {
        store(ram,45150u);store(ram+4u,5u);store(ram+8u,DEST);store(ram+12u,0u);
        int token=call(KUI_GD_REQUEST,KUI_GD_DMAREAD,aliases[alias]);CHECK(token>0);
        int n=call(KUI_GD_CHECK,(uint32_t)token,STATUS);CHECK(n==1 && value(ram+64u+12u)==4u);record(n,STATUS,4u);
        n=call(KUI_GD_DRIVE,DRIVE,0u);CHECK(n==0 && value(ram+96u)==1u);record(n,DRIVE,2u);
        CHECK(call(KUI_GD_EXEC,0u,0u)==0);
        kui_retail_gd_progress(&service,2u,0u);
        n=call(KUI_GD_CHECK,(uint32_t)token,STATUS);CHECK(n==1 && value(ram+72u)==4096u);record(n,STATUS,4u);
        kui_retail_gd_progress(&service,5u,0u);
        n=call(KUI_GD_CHECK,(uint32_t)token,STATUS);CHECK(n==2 && value(ram+72u)==10240u);record(n,STATUS,4u);
    }
    for(unsigned command=38u;command<=39u;command++) {int n=call(KUI_GD_REQUEST,command,BASE);CHECK(n==0);record(n,BASE,0u);}
    const unsigned extra[]={KUI_GD_DMA_TRANSFER,KUI_GD_PIO_TRANSFER,KUI_GD_PIO_CHECK,KUI_GD_DMA_CHECK};
    for(unsigned i=0;i<sizeof(extra)/sizeof(extra[0]);i++){int n=call(extra[i],1u,BASE);CHECK(n==-1);record(n,BASE,0u);}
    CHECK(call(KUI_GD_PIO_CALLBACK,BASE+0x100u,0u)==-1);
    CHECK(call(KUI_GD_DMA_CALLBACK,BASE+0x100u,0u)==-1);
    CHECK(call(KUI_GD_PIO_CALLBACK,0u,0u)==0 && call(KUI_GD_DMA_CALLBACK,0u,0u)==0);
    CHECK(sector_reads==0u);
}
static void policy(void) {
    const struct {uint32_t a,n;bool valid;} spans[]={
        {0x7bfffffcu,4u,false},{0x7c000000u,4u,true},{0x7c000000u,4096u,true},
        {0x7c000000u,4100u,false},{0x7c000ffcu,4u,true},{0x7c000ffcu,8u,false},
        {0x7c000ffdu,1u,false},{0x7dfff000u,16u,true},{0x7dfff010u,8u,true},
        {0x7e000ff0u,16u,true},{0x7e000ff4u,16u,false},{0x7e001000u,16u,true},
        {0x7ffff000u,4096u,true},{0x7ffffffcu,4u,true},{0x80000000u,4u,false},
        {0xfffffffcu,8u,false},{0x7c000000u,0u,false},{0x7c000000u,UINT32_MAX,false},
        {0x8c010000u,16u,false},{0x0c010000u,16u,false}};
    const struct {uint32_t n;bool valid;} modes[]={
        {0u,false},{0x20u,true},{0x121u,true},{0x929u,true},{0x80u,false},{0xa0u,false},{0x1a9u,false}};
    const uint32_t mmus[]={0u,1u,2u,0x100u,0x101u};
    for(unsigned a=0;a<sizeof(spans)/sizeof(spans[0]);a++)
        for(unsigned c=0;c<sizeof(modes)/sizeof(modes[0]);c++)
            for(unsigned m=0;m<sizeof(mmus)/sizeof(mmus[0]);m++) {
                CHECK(kui_retail_ocram_span(spans[a].a,spans[a].n,mmus[m],modes[c].n)==
                    (spans[a].valid && modes[c].valid && !(mmus[m]&1u)));++policy_cases;
            }
    CHECK(cache_at(0x7c000020u)==cache_at(0x7c001020u));
    CHECK(cache_at(0x7dfff020u)==cache_at(0x7c002020u));
    CHECK(cache_at(0x7dfff020u)!=cache_at(0x7e000020u));
}
static unsigned cycles;
static int first_check,first_drive;
static unsigned negative_handle_cases;
static unsigned idle_handle_cases;
static const char *idle_case_names[24];
static void cycle(uint32_t status,uint32_t drive,uint32_t mmu,uint32_t cache,bool valid) {
    initialize();mmucr=mmu;ccr=cache;
    int token=call(KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,0u);CHECK(token>0);
    CHECK(call(KUI_GD_EXEC,0u,0u)==0 && !service.pending);
    unsigned before_maps=raw_maps;
    int result=call(KUI_GD_CHECK,(uint32_t)token,status);
    if(!cycles)first_check=result;
    bool admitted=valid && KUI_NATIVE_OCRAM_BOOTSTRAP;
    CHECK(result==(admitted?2:-1));
    CHECK(raw_maps-before_maps==(KUI_NATIVE_OCRAM_BOOTSTRAP?1u:0u));
    if(admitted) {
        const unsigned char *p=cache_at(status);CHECK(!value(p) && !value(p+4u) && !value(p+8u) && !value(p+12u));
    } else {CHECK(service.command==KUI_GD_COMMAND_INIT);CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==2);}
    result=call(KUI_GD_DRIVE,drive,0u);CHECK(result==(admitted?0:-1));
    if(!cycles)first_drive=result;
    if(admitted)CHECK(value(cache_at(drive))==1u && value(cache_at(drive)+4u)==0x80u);
    CHECK(!sector_checks && !sector_reads);++cycles;
}
static void packet_and_payload(void) {
    initialize();ccr=0x121u;
    store(cache_at(0x7dfff020u),45150u);
    int token=call(KUI_GD_REQUEST,KUI_RETAIL_GD_SEEK,0x7dfff020u);
    if(KUI_NATIVE_OCRAM_BOOTSTRAP){CHECK(token>0);CHECK(call(KUI_GD_EXEC,0u,0u)==0);CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==2);}
    else CHECK(token==0);
    CHECK(!sector_checks && !sector_reads);
    const unsigned commands[]={KUI_GD_PIOREAD,KUI_GD_DMAREAD};
    for(unsigned i=0;i<2u;i++) {
        store(ram,45150u);store(ram+4u,1u);store(ram+8u,0x7dfff000u);store(ram+12u,0u);
        unsigned before=payload_denials;
        CHECK(call(KUI_GD_REQUEST,commands[i],BASE)==0);
        CHECK(payload_denials-before==(KUI_NATIVE_OCRAM_BOOTSTRAP?1u:0u));
        CHECK(!sector_checks && !sector_reads);
    }
}
static void malformed_status_and_handles(void) {
    initialize();ccr=0x121u;
    int token=call(KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,0u);CHECK(token>0);
    CHECK(call(KUI_GD_EXEC,0u,0u)==0 && !service.pending);
    unsigned char before[sizeof(ocram)];memcpy(before,ocram,sizeof(before));
    const uint32_t malformed[]={0x7dfff001u,0x7ffffff8u,BASE-4u,BASE+sizeof(ram)-12u};
    for(unsigned i=0;i<sizeof(malformed)/sizeof(malformed[0]);i++) {
        CHECK(call(KUI_GD_CHECK,(uint32_t)token,malformed[i])==-1);
        CHECK(service.command==KUI_GD_COMMAND_INIT && service.token==(uint32_t)token);
        CHECK(!memcmp(before,ocram,sizeof(before)));++negative_handle_cases;
    }
    /* Wrong handles must report illegal request without consuming the real
     * completed command, regardless of the new cache-RAM status support. */
    const uint32_t wrong[]={0u,(uint32_t)token+1u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(wrong)/sizeof(wrong[0]);i++) {
        CHECK(call(KUI_GD_CHECK,wrong[i],STATUS)==-1);
        CHECK(value(ram+64u)==5u && !value(ram+68u) && !value(ram+72u) && !value(ram+76u));
        CHECK(service.command==KUI_GD_COMMAND_INIT && service.token==(uint32_t)token);
        ++negative_handle_cases;
    }
    CHECK(call(KUI_GD_CHECK,(uint32_t)token+1u,0x7dfff000u)==-1);
    if(KUI_NATIVE_OCRAM_BOOTSTRAP) {
        const unsigned char *p=cache_at(0x7dfff000u);
        CHECK(value(p)==5u && !value(p+4u) && !value(p+8u) && !value(p+12u));
    } else CHECK(!memcmp(before,ocram,sizeof(before)));
    CHECK(service.command==KUI_GD_COMMAND_INIT);++negative_handle_cases;
    CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==2 && !service.command);
    CHECK(call(KUI_GD_CHECK,(uint32_t)token,STATUS)==KUI_GD_NOT_FOUND);
    CHECK(!value(ram+64u) && !value(ram+68u) && !value(ram+72u) && !value(ram+76u));
    CHECK(!sector_checks && !sector_reads);++negative_handle_cases;
}
static void snapshot(uint32_t state[9]) {
    state[0]=service.token;state[1]=service.command;state[2]=service.pending;
    state[3]=(uint32_t)service.status;state[4]=service.completed_bytes;
    state[5]=service.error;state[6]=service.executing;
    state[7]=service.position_lba;state[8]=service.drive_status;
}
static void idle_case(const char *name,uint32_t handle,uint32_t address,
                      int expected,uint32_t extended,bool mapped) {
    uint32_t before_state[9],after_state[9];
    unsigned char before_ram[sizeof(ram)],before_ocram[sizeof(ocram)];
    unsigned checks_before=sector_checks,reads_before=sector_reads;
    unsigned char *out=kui_retail_ocram_address(address)?cache_at(address):ram+address-BASE;
    if(mapped)memset(out,0xa5,16u);
    memcpy(before_ram,ram,sizeof(ram));memcpy(before_ocram,ocram,sizeof(ocram));
    snapshot(before_state);
    CHECK(call(KUI_GD_CHECK,handle,address)==expected);
    snapshot(after_state);CHECK(!memcmp(before_state,after_state,sizeof(before_state)));
    CHECK(sector_checks==checks_before && sector_reads==reads_before);
    if(mapped)CHECK(value(out)==extended && !value(out+4u) && !value(out+8u) && !value(out+12u));
    else CHECK(!memcmp(before_ram,ram,sizeof(ram)) && !memcmp(before_ocram,ocram,sizeof(ocram)));
    CHECK(idle_handle_cases<sizeof(idle_case_names)/sizeof(idle_case_names[0]));
    idle_case_names[idle_handle_cases++]=name;
}
static uint32_t submit_one(void) {
    store(ram,45150u);store(ram+4u,1u);store(ram+8u,DEST);store(ram+12u,0u);
    int token=call(KUI_GD_REQUEST,KUI_GD_DMAREAD,BASE);CHECK(token>0);return (uint32_t)token;
}
static void idle_zero_semantics(void) {
    initialize();ccr=0x121u;
    idle_case("cold-zero-main",0u,STATUS,KUI_GD_NOT_FOUND,0u,true);
    idle_case("cold-zero-ocram",0u,0x7e000f00u,KUI_NATIVE_OCRAM_BOOTSTRAP?0:-1,0u,KUI_NATIVE_OCRAM_BOOTSTRAP);
    idle_case("cold-zero-unaligned-main",0u,STATUS+1u,-1,0u,false);
    idle_case("cold-zero-crossing-ocram",0u,0x7e000ff4u,-1,0u,false);
    idle_case("cold-nonzero-mismatch",1u,STATUS,-1,5u,true);
    uint32_t token=submit_one();
    idle_case("pending-zero-rejected",0u,STATUS,-1,5u,true);
    idle_case("pending-nonzero-mismatch",token+1u,STATUS,-1,5u,true);
    idle_case("pending-zero-invalid-buffer",0u,STATUS+1u,-1,0u,false);
    CHECK(call(KUI_GD_RESET,0u,0u)==0 && service.token==token);
    idle_case("reset-zero-main",0u,STATUS,0,0u,true);
    idle_case("reset-nonzero-mismatch",token+1u,STATUS,-1,5u,true);
    idle_case("reset-last-handle-not-found",token,STATUS,0,0u,true);
    token=submit_one();kui_retail_gd_progress(&service,1u,0u);
    CHECK(!service.pending && service.command && service.status==KUI_GD_COMPLETED);
    idle_case("completed-unacknowledged-zero-rejected",0u,STATUS,-1,5u,true);
    CHECK(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED && !service.command);
    idle_case("completed-ack-zero-main",0u,STATUS,0,0u,true);
    idle_case("completed-ack-zero-ocram",0u,0x7e000f00u,KUI_NATIVE_OCRAM_BOOTSTRAP?0:-1,0u,KUI_NATIVE_OCRAM_BOOTSTRAP);
    idle_case("completed-ack-nonzero-mismatch",token+1u,STATUS,-1,5u,true);
    token=submit_one();kui_retail_gd_progress(&service,0u,KUI_GD_ERROR_IO);
    CHECK(!service.pending && service.command && service.status==KUI_GD_FAILED && service.error==KUI_GD_ERROR_IO);
    idle_case("failed-unacknowledged-zero-rejected",0u,STATUS,-1,5u,true);
    CHECK(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED && !service.command);
    idle_case("failed-ack-zero-main",0u,STATUS,0,0u,true);
    idle_case("failed-ack-zero-ocram",0u,0x7e000f00u,KUI_NATIVE_OCRAM_BOOTSTRAP?0:-1,0u,KUI_NATIVE_OCRAM_BOOTSTRAP);
    idle_case("failed-ack-nonzero-mismatch",token+1u,STATUS,-1,5u,true);
    uint32_t following=submit_one();CHECK(following==token+1u);
    CHECK(call(KUI_GD_ABORT,following,0u)==0);
    CHECK(sector_reads==0u);
}
int main(void) {
    ordinary();policy();
    cycle(0x7dfff000u,0x7dfff010u,0u,0x121u,true);
    cycle(0x7e000fc0u,0x7e000fd0u,0u,0x929u,true);
    cycle(0x7e001000u,0x7e001010u,0u,0x20u,true);
    cycle(0x7e000ff4u,0x7e000ffcu,0u,0x20u,false);
    cycle(0x7dfff000u,0x7dfff010u,1u,0x20u,false);
    cycle(0x7dfff000u,0x7dfff010u,0u,0xa0u,false);
    cycle(0x7dfff000u,0x7dfff010u,0u,0u,false);
    packet_and_payload();malformed_status_and_handles();idle_zero_semantics();
    printf("{\"checks\":%u,\"policy_cases\":%u,\"bootstrap_cycles\":%u,\"enabled\":%s,\"raw_mapper_visits\":%u,\"payload_denials\":%u,\"negative_handle_cases\":%u,\"idle_zero_case_count\":%u,\"idle_zero_cases\":[",checks,policy_cases,cycles,KUI_NATIVE_OCRAM_BOOTSTRAP?"true":"false",raw_maps,payload_denials,negative_handle_cases,idle_handle_cases);
    for(unsigned i=0;i<idle_handle_cases;i++)printf("%s\"%s\"",i?",":"",idle_case_names[i]);
    printf("],\"first_check_result\":%d,\"first_drive_result\":%d,\"hardware_executed\":false,\"ordinary_trace\":[",first_check,first_drive);
    for(unsigned i=0;i<used;i++)printf("%s%u",i?",":"",trace[i]);
    puts("]}");return 0;
}
