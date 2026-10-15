/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include "kui/retail_gd.h"
#include "../src/loader/retail_stage.c"

#define OWNER_BYTES 6751168u
#define OWNER_CRC 0x73f4277bu
#define FRAME 0x8c00fea0u
#define HANDLER 0x8c008340u
#define STATE 0xace81000u
#define PARAM 0x8c700100u
#define STATUS 0x8c700200u
#define OUTPUT 0x8c710000u
#define BASE_PARAM 0x8c800100u
#define BASE_STATUS 0x8c800200u
#define BASE_OUTPUT 0x8c810000u
#define RAM_BYTES 0x1000000u
#define CCR 0x00000909u
#define GD_BUDGET 4096u
#if KUI_RETAIL_STARTUP_TRACE >= 3
#define TRACE_HEADER "SONIC MOUNT TRACE - INTENTIONAL STOP"
#define FIRST_READ_STOPS false
#else
#define TRACE_HEADER "SONIC GD TRACE - INTENTIONAL STOP"
#define FIRST_READ_STOPS true
#endif
static uint8_t *owner,reader[256],pvd[2048];
static jmp_buf terminal;
static bool expect_stop;
static struct startup_gd_state *observer=(struct startup_gd_state *)(uintptr_t)STATE;
static uint32_t saved[21];
static struct {uint32_t regs[14];uint8_t vram[4096];} video,original_video;
static struct {
    unsigned restores,lines,hexes,values,pauses,publishes;
    const char *line[32],*legend[32],*hex[16];
    unsigned count[32];uint32_t value[32][8],hex_value[16];
    unsigned samples[3];
} spy;
static struct backend {
    struct kui_retail_gd service;
    unsigned reads,nested_calls;
    int fail_read,reenter;
} actual,baseline;

void kui_retail_startup_trace_scan(void) {abort();}
void kui_retail_startup_trace_g2(void) {abort();}
void kui_retail_startup_trace_pvr(void) {abort();}
void kui_retail_startup_trace_gd(void) {abort();}
#if KUI_RETAIL_STARTUP_TRACE >= 3
void kui_retail_startup_trace_mount(void) {abort();}
#endif
void kui_retail_startup_gd_proxy(void) {abort();}
void kui_retail_startup_gd_return(void) {abort();}
uint32_t kui_retail_startup_trace_stack_address(void) {return 0x8cefe000u;}
void kui_retail_startup_trace_publish(uint32_t address,size_t bytes) {
    assert((bytes==12 && address>=KUI_RETAIL_EXEC_ADDRESS && address<KUI_RETAIL_RAM_END) ||
        (bytes==4 && address==KUI_GD_VECTOR_ADDRESS));
    ++spy.publishes;
}
uint32_t kui_retail_startup_trace_read(uint32_t address) {
    if(address==0xa05f810cu) return ++spy.samples[0]==1?0u:1u;
    if(address==0xa05f688cu) {++spy.samples[1];return 0;}
    assert(address==0xa05f6900u);++spy.samples[2];return 8;
}
void retail_display_restore(const struct retail_display_state *state) {
    ++spy.restores;memcpy(video.regs,state->regs,sizeof(video.regs));memset(video.vram,0,sizeof(video.vram));
}
void retail_display_line(const char *text) {
    assert(spy.lines<32);spy.line[spy.lines++]=text;video.vram[spy.lines]=(uint8_t)text[0];
    if(!strcmp(text,"STORAGE WAS READ ONLY")) {assert(expect_stop);longjmp(terminal,1);}
}
void retail_display_hex(const char *text,uint32_t value) {
    assert(spy.hexes<16);spy.hex[spy.hexes]=text;spy.hex_value[spy.hexes++]=value;
    video.vram[100u+spy.hexes]=(uint8_t)value;
}
void retail_display_values(const char *legend,const uint32_t *values,unsigned count) {
    assert(spy.values<32 && count<=8);spy.legend[spy.values]=legend;spy.count[spy.values]=count;
    memcpy(spy.value[spy.values++],values,count*sizeof(uint32_t));video.vram[200u+spy.values]=(uint8_t)values[0];
}
void retail_display_pause(uint32_t frames) {assert(frames==30);++spy.pauses;}
static bool line(const char *text) {
    for(unsigned i=0;i<spy.lines;i++) if(!strcmp(spy.line[i],text)) return true;
    return false;
}
static uint32_t crc(uint32_t previous,const uint8_t *bytes,size_t count) {
    uint32_t value=~previous;
    for(size_t i=0;i<count;i++) {
        value^=bytes[i];
        for(unsigned bit=0;bit<8;bit++) value=(value>>1)^((value&1u)?0xedb88320u:0u);
    }
    return ~value;
}
static void le32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(value>>(i*8u));
}
static void synthetic_owner(void) {
    owner=malloc(OWNER_BYTES);assert(owner);
    for(size_t i=0;i<OWNER_BYTES;i++) owner[i]=(uint8_t)(i*73u+(i>>8)*19u+5u);
    uint8_t suffix[4]={0};uint32_t basis[32]={0},combination[32]={0};
    uint32_t prefix=crc(0,owner,OWNER_BYTES-4u),base=crc(prefix,suffix,4);
    for(unsigned i=0;i<32;i++) {
        memset(suffix,0,4);suffix[i/8u]=(uint8_t)(1u<<(i%8u));
        uint32_t value=crc(prefix,suffix,4)^base,mask=1u<<i;
        for(int bit=31;bit>=0;bit--) if(value&(1u<<bit)) {
            if(basis[bit]) {value^=basis[bit];mask^=combination[bit];}
            else {basis[bit]=value;combination[bit]=mask;break;}
        }
    }
    uint32_t wanted=OWNER_CRC^base,solution=0;
    for(int bit=31;bit>=0;bit--) if(wanted&(1u<<bit)) {
        assert(basis[bit]);wanted^=basis[bit];solution^=combination[bit];
    }
    assert(!wanted);
    for(unsigned i=0;i<4;i++) owner[OWNER_BYTES-4u+i]=(uint8_t)(solution>>(i*8u));
    assert(crc(0,owner,OWNER_BYTES)==OWNER_CRC);
    for(unsigned i=0;i<sizeof(pvd);i++) pvd[i]=(uint8_t)(i*13u+19u);
    pvd[0]=1;memcpy(pvd+1,"CD001",5);pvd[6]=1;
    pvd[128]=0;pvd[129]=8;le32(pvd+132,1234u);le32(pvd+140,45020u);
    le32(pvd+158,45030u);le32(pvd+166,4096u);
    for(unsigned i=0;i<sizeof(reader);i++) reader[i]=(uint8_t)(i*29u+17u);
}
static void put(uint32_t address,uint32_t value) {memcpy((void *)(uintptr_t)address,&value,4);}
static uint32_t get(uint32_t address) {uint32_t value;memcpy(&value,(void *)(uintptr_t)address,4);return value;}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int access) {
    (void)context;assert(access>=0 && access<=KUI_RETAIL_MAP_VALIDATE);
    assert(address>=KUI_RETAIL_EXEC_ADDRESS && address<KUI_RETAIL_RAM_END && bytes<=KUI_RETAIL_RAM_END-address);
    return (uint8_t *)(uintptr_t)address;
}
static int check(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;return lba>=45000u && lba<45100u && count<=45100u-lba && bytes==2048u?0:-1;
}
static int backing_read(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    struct backend *backend=context;assert(!check(context,lba,count,bytes));++backend->reads;
    if(backend->reenter) {
        uint32_t original[21];memcpy(original,observer->original,sizeof(original));
        assert(kui_retail_gd_dispatch(&backend->service,backend->service.token,0,0,KUI_GD_CHECK)==4);
        if(backend==&actual) assert(observer->active==1 && !memcmp(original,observer->original,sizeof(original)));
        ++backend->nested_calls;
    }
    if(backend->fail_read) return -1;
    for(unsigned n=0;n<count;n++) {
        if(lba+n==45016u) memcpy((uint8_t *)out+n*bytes,pvd,bytes);
        else for(unsigned i=0;i<bytes;i++)
            ((uint8_t *)out)[n*bytes+i]=(uint8_t)((lba+n)*19u+i*17u+7u);
    }
    return 0;
}
static const union kui_retail_slot tracks[]={{.track={.start_lba=45000,.end_lba=45100,.control=4}}};
static uint32_t *fixture(void) {
    uint8_t *boot=(uint8_t *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS;
    memcpy(boot,owner,OWNER_BYTES);memcpy(original_entry,owner,sizeof(original_entry));memset(boot,0xcc,128);
    exec_bytes=OWNER_BYTES;boot_crc=OWNER_CRC;manifest.ip_crc32=0x22de24d8u;
    resident_blob=reader;resident_bytes=sizeof(reader);resident_limit=KUI_RETAIL_STANDARD_LIMIT;
    memcpy((void *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS,reader,sizeof(reader));put(KUI_GD_VECTOR_ADDRESS,HANDLER);
    memset((void *)(uintptr_t)0x8c700000u,0xa5,0x120000u);
    uint32_t *frame=(uint32_t *)(uintptr_t)FRAME;
    for(unsigned i=0;i<21;i++) frame[i]=0x7b000000u+i*0x10101u;
    frame[3]=KUI_RETAIL_BOOT_VBR;frame[4]=0x60000100u;
    for(unsigned i=0;i<14;i++) {display.regs[i]=0x10000000u+i;video.regs[i]=0x5a000000u+i*11u;}
    for(unsigned i=0;i<sizeof(video.vram);i++) video.vram[i]=(uint8_t)(i*37u+23u);
    memcpy(&original_video,&video,sizeof(video));
    struct backend *backends[2]={&actual,&baseline};
    for(unsigned i=0;i<2;i++) {
        struct kui_gd_ops ops={backends[i],map,check,backing_read};
        assert(!kui_retail_gd_init(&backends[i]->service,tracks,1,&ops,KUI_RETAIL_EXEC_ADDRESS,KUI_RETAIL_RAM_END));
    }
    return frame;
}
static void silent(const uint32_t *frame) {
    assert(!spy.restores && !spy.lines && !spy.hexes && !spy.values);
    assert(!memcmp(&video,&original_video,sizeof(video)) && !memcmp(frame,saved,sizeof(saved)));
}
static void installed(uint32_t *frame) {
    uint32_t handler=get(KUI_GD_VECTOR_ADDRESS);
    memcpy(saved,frame,sizeof(saved));kui_retail_stage_relay(frame,CCR);
    for(unsigned point=0;point<4;point++) kui_retail_startup_trace_checkpoint(frame,point,CCR);
    silent(frame);assert(spy.publishes==STARTUP_TRACE_POINTS+5u && spy.pauses==1);
#if KUI_RETAIL_STARTUP_TRACE >= 3
    uint32_t final=startup_trace_address[4]-KUI_RETAIL_EXEC_ADDRESS;
    assert(!memcmp((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,owner,final));
    assert(!memcmp((const void *)(uintptr_t)(KUI_RETAIL_EXEC_ADDRESS+final+12u),owner+final+12u,OWNER_BYTES-final-12u));
    assert(memcmp((const void *)(uintptr_t)startup_trace_address[4],owner+final,12u));
#else
    assert(!memcmp((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,owner,OWNER_BYTES));
#endif
    assert(get(KUI_GD_VECTOR_ADDRESS)==((uint32_t)(uintptr_t)kui_retail_startup_gd_proxy|0x20000000u));
    memcpy(observer,&kui_retail_startup_gd_state,sizeof(*observer));assert(observer->handler==handler);
}
static void params(uint32_t fad,uint32_t count,uint32_t destination) {
    uint32_t values[4]={fad,count,destination,0};memcpy((void *)(uintptr_t)PARAM,values,sizeof(values));
    values[2]=BASE_OUTPUT;memcpy((void *)(uintptr_t)BASE_PARAM,values,sizeof(values));
}
static int32_t observed_call(uint32_t *frame,uint32_t function,uint32_t r4,uint32_t r5,
    uint32_t baseline_r4,uint32_t baseline_r5,uint32_t sr,bool stop) {
    frame[4]=sr;frame[5]=0x8c6086eeu;
    frame[16]=r4;frame[15]=r5;frame[14]=0;frame[13]=function;
    memcpy(saved,frame,sizeof(saved));observer->active=1;
    kui_retail_startup_gd_before(frame,observer,CCR);
    assert(!memcmp(observer->original,saved,sizeof(saved)) && observer->active==1);
    assert(frame[4]==sr && frame[16]==r4 && frame[15]==r5 && frame[14]==0 && frame[13]==function);
    int32_t expected=kui_retail_gd_dispatch(&baseline.service,baseline_r4,baseline_r5,0,function);
    int32_t returned=kui_retail_gd_dispatch(&actual.service,r4,r5,0,function);
    assert(returned==expected);
    /* A real handler can clobber caller-saved integer state. The after phase
     * must use its return R0 while restoring the exact original caller frame. */
    for(unsigned i=0;i<21;i++) frame[i]=0xdead0000u+i;
    frame[20]=(uint32_t)returned;
    expect_stop=stop;
    if(!setjmp(terminal)) {
        kui_retail_startup_gd_after(frame,observer,CCR);
        assert(!stop);
    } else assert(stop);
    if(!stop) {
        saved[20]=(uint32_t)returned;
        assert(!memcmp(frame,saved,sizeof(saved)) && !observer->active);
    } else {
        assert(!memcmp(observer->original,saved,sizeof(saved)) && observer->active==1);
        assert(observer->result==(uint32_t)returned && frame[20]==(uint32_t)returned);
    }
    assert(actual.reads==baseline.reads && actual.nested_calls==baseline.nested_calls);
    if(!stop) silent(frame);
    else assert(spy.restores==1 && line(TRACE_HEADER) &&
        line("LAUNCH STOPPED - PHOTOGRAPH THIS SCREEN"));
    return returned;
}

static uint32_t alias(uint32_t address,unsigned area) {return (address&0x1fffffffu)|area;}
static void status_equal(void) {
    assert(!memcmp((const void *)(uintptr_t)STATUS,(const void *)(uintptr_t)BASE_STATUS,16));
}
static uint32_t submit_read(uint32_t *frame,unsigned area,uint32_t count) {
    params(45166u,count,alias(OUTPUT,area));
    int32_t token=observed_call(frame,KUI_GD_REQUEST,KUI_GD_DMAREAD,alias(PARAM,area),
        KUI_GD_DMAREAD,BASE_PARAM,0x600001f0u,!count);
    if(count) assert(token>0 && observer->read_token==(uint32_t)token);
    return (uint32_t)token;
}
#if KUI_RETAIL_STARTUP_TRACE >= 3
static void mount_return(uint32_t *frame,uint32_t result) {
    /* Assembly supplies the P2 state directly in production. The test uses
     * a separate mapped state to exercise the same uncached observer calls. */
    memcpy(&kui_retail_startup_gd_state,observer,sizeof(*observer));
    frame[16]=result;frame[20]=0x60000100u;frame[4]=0x600001f0u;
    memcpy(saved,frame,sizeof(saved));
    unsigned samples[3];memcpy(samples,spy.samples,sizeof(samples));
    expect_stop=true;
    if(!setjmp(terminal)) {kui_retail_startup_trace_checkpoint(frame,4,CCR);abort();}
    assert(line(TRACE_HEADER) && line("SDK MOUNT RETURN REACHED") && spy.restores==1);
    assert(!memcmp(frame,saved,sizeof(saved)) && !memcmp(samples,spy.samples,sizeof(samples)));
    assert(kui_retail_startup_gd_state.mount_result==result && spy.values==5);
    assert(spy.value[1][0]==result && spy.value[1][1]==observer->read_requests && spy.value[1][2]==observer->completed_reads);
    assert(spy.value[1][3]==observer->path_lba && spy.value[1][4]==observer->path_bytes);
    assert(spy.value[2][0]==observer->pvd_crc && spy.value[2][1]==observer->pvd_valid && spy.value[2][2]==observer->pvd_sector_bytes);
    assert(spy.value[2][3]==observer->root_lba && spy.value[2][4]==observer->root_bytes);
    assert(!memcmp((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,owner,OWNER_BYTES));
    assert(startup_trace.passed==31u && spy.publishes==11);
}
#endif
static void sequence(uint32_t *frame,unsigned area,bool nested,bool finish) {
#if KUI_RETAIL_STARTUP_TRACE < 3
    (void)finish;
#endif
    assert(observed_call(frame,KUI_GD_INIT,0,0,0,0,0x60000100u,false)==0 && observer->init_result==0);
    assert(observed_call(frame,KUI_GD_DRIVE,alias(OUTPUT,area),0,BASE_OUTPUT,0,0x600001f0u,false)==0);
    assert(observer->drive_result==0 && observer->drive_status==get(OUTPUT) && observer->drive_type==get(OUTPUT+4));
    assert(!memcmp((const void *)(uintptr_t)OUTPUT,(const void *)(uintptr_t)BASE_OUTPUT,8));
    int32_t init=observed_call(frame,KUI_GD_REQUEST,KUI_GD_COMMAND_INIT,0,KUI_GD_COMMAND_INIT,0,0x600001f0u,false);
    assert(init>0 && observer->init_token==(uint32_t)init);
    assert(observed_call(frame,KUI_GD_CHECK,(uint32_t)init,alias(STATUS,area),(uint32_t)init,BASE_STATUS,0x600001f0u,false)==1);
    status_equal();
    assert(!observed_call(frame,KUI_GD_EXEC,0,0,0,0,0x600001f0u,false));
    assert(observed_call(frame,KUI_GD_CHECK,(uint32_t)init,alias(STATUS,area),(uint32_t)init,BASE_STATUS,0x600001f0u,false)==2);
    assert(observer->init_status==2);status_equal();
    put(PARAM,alias(OUTPUT,area));put(BASE_PARAM,BASE_OUTPUT);
    int32_t version=observed_call(frame,KUI_GD_REQUEST,KUI_RETAIL_GD_GET_VERS,alias(PARAM,area),KUI_RETAIL_GD_GET_VERS,BASE_PARAM,0x600001f0u,false);
    assert(version>0 && observer->version_token==(uint32_t)version);
    assert(!observed_call(frame,KUI_GD_EXEC,0,0,0,0,0x600001f0u,false));
    assert(observed_call(frame,KUI_GD_CHECK,(uint32_t)version,alias(STATUS,area),(uint32_t)version,BASE_STATUS,0x600001f0u,false)==2);
    assert(observer->version_status==2 && observer->version_crc==crc(0,(const uint8_t *)(uintptr_t)OUTPUT,28));
    assert(!memcmp((const void *)(uintptr_t)OUTPUT,(const void *)(uintptr_t)BASE_OUTPUT,28));status_equal();
    uint32_t token=submit_read(frame,area,1);
    assert(observed_call(frame,KUI_GD_CHECK,token,alias(STATUS,area),token,BASE_STATUS,0x600001f0u,false)==1);
    assert(observer->read_status==1 && observer->check[2]==0);status_equal();
    actual.reenter=baseline.reenter=nested;
    assert(!observed_call(frame,KUI_GD_EXEC,0,0,0,0,0x600001f0u,false));
    assert(observed_call(frame,KUI_GD_CHECK,token,alias(STATUS,area),token,BASE_STATUS,0x600001f0u,FIRST_READ_STOPS)==2);
    assert(observer->read_status==2);
#if KUI_RETAIL_STARTUP_TRACE < 3
    assert(line("FIRST GD READ COMPLETED"));
#endif
    assert(observer->read_command==KUI_GD_DMAREAD && observer->read_fad==45166 && observer->read_count==1);
    assert(observer->read_destination==alias(OUTPUT,area) && observer->pvd_header==get(OUTPUT));
    assert(observer->pvd_crc==crc(0,pvd,sizeof(pvd)) && !memcmp(observer->check,(const void *)(uintptr_t)STATUS,16));
    assert(!memcmp((const void *)(uintptr_t)OUTPUT,pvd,sizeof(pvd)));
    assert(!memcmp((const void *)(uintptr_t)OUTPUT,(const void *)(uintptr_t)BASE_OUTPUT,sizeof(pvd)));status_equal();
#if KUI_RETAIL_STARTUP_TRACE >= 3
    assert(observer->read_requests==1 && observer->completed_reads==1 && observer->read_completed);
    assert(observer->pvd_captured==1 && observer->pvd_valid==(pvd[0]==1 && !memcmp(pvd+1,"CD001",5) && pvd[6]==1));
    assert(observer->pvd_sector_bytes==2048 && observer->root_lba==45030 && observer->root_bytes==4096);
    assert(observer->path_lba==45020 && observer->path_bytes==1234 && observer->mount_result==UINT32_MAX);
    assert(actual.reads==1 && actual.nested_calls==(nested?1u:0u));
    silent(frame);
    if(!finish) return;
    mount_return(frame,0);
#endif
    assert(spy.values==5 && spy.count[0]==5 && spy.value[0][0]==observer->calls);
    assert(spy.value[0][1]==KUI_GD_CHECK && spy.value[0][2]==token && spy.value[0][3]==2 && spy.value[0][4]==0x8c6086eeu);
    assert(observer->ccr==CCR);
    assert(spy.value[3][0]==KUI_GD_DMAREAD && spy.value[3][1]==45166 && spy.value[3][4]==token);
    assert(spy.value[4][0]==2 && spy.value[4][3]==2048);
    assert(actual.reads==1 && actual.nested_calls==(nested?1u:0u));
}
enum scenario {SEQUENCE_P0,SEQUENCE_P1,SEQUENCE_P2,NESTED_BUSY,INIT_NULL,INIT_SHORT,
    VERSION_SHORT,VERSION_NULL,VERSION_BOUND,READ_NULL,READ_SHORT,READ_ALIGN,READ_LOW,
    READ_AREA,READ_IO_FAIL,READ_REJECTED,CHECK_MISMATCH,INFLIGHT_READ,POLL_LIMIT,
    BEFORE_INACTIVE,AFTER_INACTIVE,RETURN_FRAME,RETURN_CCR,VECTOR_LOW,VECTOR_END,
    VECTOR_AREA,VECTOR_ODD,VECTOR_P2,CHECK_NOT_FOUND,SETUP_NULL,READER_FIRST,READER_LAST
#if KUI_RETAIL_STARTUP_TRACE >= 3
    ,MOUNT_FAILURE_NO_READ,MULTIREAD_P0,MULTIREAD_P1,MULTIREAD_P2,
    REJECT_AFTER_COMPLETION,SECOND_INFLIGHT_REJECT,SECOND_IO_FAIL,
    PVD_BAD_TYPE,PVD_BAD_ID,PVD_BAD_VERSION,CHECK_CONSUMED_BEFORE_NEXT_READ
#endif
};
static void stopped_before(uint32_t *frame,const char *message) {
    expect_stop=true;
    if(!setjmp(terminal)) {kui_retail_startup_gd_before(frame,observer,CCR);abort();}
    assert(spy.restores==1 && line(message));
}
static void stopped_after(uint32_t *frame,uint32_t ccr) {
    expect_stop=true;
    if(!setjmp(terminal)) {kui_retail_startup_gd_after(frame,observer,ccr);abort();}
    assert(spy.restores==1 && line("GD TRACE RETURN FRAME INVALID"));
}
static void bad_install(uint32_t *frame,const char *message,unsigned corruption) {
    memcpy(saved,frame,sizeof(saved));kui_retail_stage_relay(frame,CCR);
    for(unsigned point=0;point<3;point++) kui_retail_startup_trace_checkpoint(frame,point,CCR);
    if(corruption!=UINT32_MAX)
        ((uint8_t *)(uintptr_t)(KUI_RETAIL_RESIDENT_ADDRESS|0x20000000u))[corruption]^=0x40u;
    expect_stop=true;
    if(!setjmp(terminal)) {kui_retail_startup_trace_checkpoint(frame,3,CCR);abort();}
    assert(spy.restores==1 && line(message) && spy.publishes==STARTUP_TRACE_POINTS+4u);
    if(corruption!=UINT32_MAX) {
        assert(spy.hexes==1 && !strcmp(spy.hex[0],"DETAIL") && spy.hex_value[0]==corruption);
        assert(((const uint8_t *)(uintptr_t)KUI_RETAIL_RESIDENT_ADDRESS)[corruption]==(uint8_t)(reader[corruption]^0x40u));
    }
}
static void run_case(enum scenario scenario) {
    uint32_t *frame=fixture();
    if(scenario>=VECTOR_LOW && scenario<=VECTOR_ODD) {
        uint32_t invalid=scenario==VECTOR_LOW?KUI_RETAIL_RESIDENT_ADDRESS-2u:
            scenario==VECTOR_END?KUI_RETAIL_RESIDENT_ADDRESS+sizeof(reader):
            scenario==VECTOR_AREA?HANDLER&0x1fffffffu:HANDLER|1u;
        put(KUI_GD_VECTOR_ADDRESS,invalid);bad_install(frame,"GD TRACE VECTOR INVALID",UINT32_MAX);
        assert(get(KUI_GD_VECTOR_ADDRESS)==invalid);return;
    }
    if(scenario==READER_FIRST || scenario==READER_LAST) {
        bad_install(frame,"OWNER STARTUP ALTERED READER",scenario==READER_FIRST?0u:sizeof(reader)-1u);
        assert(get(KUI_GD_VECTOR_ADDRESS)==HANDLER);return;
    }
    if(scenario==VECTOR_P2) put(KUI_GD_VECTOR_ADDRESS,alias(HANDLER,0xa0000000u));
    installed(frame);
    if(scenario==CHECK_NOT_FOUND) {
        int32_t token=observed_call(frame,KUI_GD_REQUEST,KUI_GD_NOP,0,KUI_GD_NOP,0,0x600001f0u,false);
        assert(token>0);
        assert(!observed_call(frame,KUI_GD_EXEC,0,0,0,0,0x600001f0u,false));
        assert(observed_call(frame,KUI_GD_CHECK,(uint32_t)token,STATUS,(uint32_t)token,BASE_STATUS,0x600001f0u,false)==2);
        assert(!observed_call(frame,KUI_GD_CHECK,(uint32_t)token,STATUS,(uint32_t)token,BASE_STATUS,0x600001f0u,false));
        assert(!spy.restores && !observer->active && !observer->read_token && !actual.reads);
        status_equal();return;
    }
    if(scenario==SETUP_NULL) {
        frame[4]=0x60000100u;frame[5]=0x8c6086eeu;frame[16]=16;
        frame[15]=0;frame[14]=UINT32_MAX;frame[13]=KUI_GD_REQUEST;
        memcpy(saved,frame,sizeof(saved));observer->active=1;
        kui_retail_startup_gd_before(frame,observer,CCR);
        int32_t result=kui_retail_gd_dispatch(&actual.service,16,0,UINT32_MAX,KUI_GD_REQUEST);
        assert(result==kui_retail_gd_dispatch(&baseline.service,16,0,UINT32_MAX,KUI_GD_REQUEST) && result==-1);
        for(unsigned i=0;i<21;i++) frame[i]=0xdead0000u+i;
        frame[20]=(uint32_t)result;kui_retail_startup_gd_after(frame,observer,CCR);
        saved[20]=(uint32_t)result;silent(frame);
        assert(!observer->active && !observer->read_token && !actual.reads);return;
    }
    if(scenario<=NESTED_BUSY) {
        sequence(frame,scenario==SEQUENCE_P0?0u:scenario==SEQUENCE_P2?0xa0000000u:0x80000000u,scenario==NESTED_BUSY,true);return;
    }
    if(scenario==VECTOR_P2 || scenario==INIT_NULL || scenario==INIT_SHORT) {
        uint32_t parameter=scenario==INIT_SHORT?KUI_RETAIL_STAGE_ADDRESS-4u:0;
        assert(!observed_call(frame,KUI_GD_INIT,0,parameter,0,parameter,0x60000100u,false));
        assert(observer->init_result==0);return;
    }
    if(scenario==VERSION_SHORT) {
        put(KUI_RETAIL_STAGE_ADDRESS-4u,alias(OUTPUT,0));put(BASE_PARAM,BASE_OUTPUT);
        int32_t token=observed_call(frame,KUI_GD_REQUEST,40u,alias(KUI_RETAIL_STAGE_ADDRESS-4u,0),40u,BASE_PARAM,0x600001f0u,false);
        assert(token>0 && observer->version_destination==alias(OUTPUT,0));
        assert(!observed_call(frame,KUI_GD_EXEC,0,0,0,0,0x600001f0u,false));
        assert(observed_call(frame,KUI_GD_CHECK,(uint32_t)token,alias(STATUS,0),(uint32_t)token,BASE_STATUS,0x600001f0u,false)==2);
        assert(observer->version_crc==crc(0,(const uint8_t *)(uintptr_t)OUTPUT,28));return;
    }
    if(scenario>=VERSION_NULL && scenario<=READ_AREA) {
        frame[13]=KUI_GD_REQUEST;frame[14]=0;
        frame[16]=scenario<=VERSION_BOUND?40u:16u;
        frame[15]=scenario==VERSION_NULL || scenario==READ_NULL?0u:
            scenario==VERSION_BOUND?KUI_RETAIL_STAGE_ADDRESS:
            scenario==READ_SHORT?KUI_RETAIL_STAGE_ADDRESS-4u:
            scenario==READ_ALIGN?PARAM+1u:
            scenario==READ_LOW?KUI_RETAIL_HOOK_STACK-4u:0xcc700100u;
        observer->active=1;
        stopped_before(frame,scenario<=VERSION_BOUND?"GD TRACE VERSION PARAMS INVALID":"GD TRACE READ PARAMS INVALID");
        assert(!actual.service.diag.calls && !actual.reads);return;
    }
    if(scenario==BEFORE_INACTIVE) {
        stopped_before(frame,"GD TRACE FRAME INVALID");assert(!observer->calls);return;
    }
    if(scenario==AFTER_INACTIVE) {stopped_after(frame,CCR);assert(!observer->calls);return;}
    if(scenario==RETURN_FRAME || scenario==RETURN_CCR) {
        observer->active=1;frame[13]=KUI_GD_INIT;frame[14]=0;
        kui_retail_startup_gd_before(frame,observer,CCR);
        frame[20]=0;
        uint32_t *returned=frame;
        if(scenario==RETURN_FRAME) {returned=frame-32;memcpy(returned,frame,21*sizeof(uint32_t));}
        stopped_after(returned,scenario==RETURN_CCR?CCR^1u:CCR);
        assert(observer->result==UINT32_MAX && observer->active==1);return;
    }
    if(scenario==READ_REJECTED) {
        assert(!submit_read(frame,0x80000000u,0));
        assert(line("GD CALL FAILED OR REJECTED") && !actual.reads && !observer->read_token);
        assert(observer->read_command==KUI_GD_DMAREAD && observer->read_fad==45166 && observer->read_destination==OUTPUT);return;
    }
#if KUI_RETAIL_STARTUP_TRACE >= 3
    if(scenario>=MOUNT_FAILURE_NO_READ) {
        if(scenario==MOUNT_FAILURE_NO_READ) {
            assert(observer->mount_result==UINT32_MAX && !observer->calls);
            mount_return(frame,(uint32_t)-5);return;
        }
        if(scenario==PVD_BAD_TYPE || scenario==PVD_BAD_ID || scenario==PVD_BAD_VERSION) {
            pvd[scenario==PVD_BAD_TYPE?0u:scenario==PVD_BAD_ID?5u:6u]^=1;
            sequence(frame,0x80000000u,false,true);
            assert(!observer->pvd_valid);return;
        }
        unsigned area=scenario==MULTIREAD_P0?0u:scenario==MULTIREAD_P2?0xa0000000u:0x80000000u;
        sequence(frame,area,false,false);
        uint32_t first_crc=observer->pvd_crc,first_header=observer->pvd_header;
        if(scenario==REJECT_AFTER_COMPLETION) {
            uint32_t first=observer->read_token;params(45190u,0,OUTPUT+4096u);
            assert(!observed_call(frame,KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM,KUI_GD_PIOREAD,BASE_PARAM,0x600001f0u,true));
            assert(observer->read_token==first && observer->read_fad==45166u && observer->read_destination==alias(OUTPUT,area));
            assert(observer->read_requests==1 && observer->completed_reads==1 && observer->pvd_crc==first_crc);return;
        }
        if(scenario==CHECK_CONSUMED_BEFORE_NEXT_READ) {
            uint32_t first=observer->read_token;
            assert(!observed_call(frame,KUI_GD_CHECK,first,STATUS,first,BASE_STATUS,0x600001f0u,false));
            assert(observer->read_completed && observer->completed_reads==1);status_equal();
        }
        params(45170u,1,alias(OUTPUT,area));
        int32_t path=observed_call(frame,KUI_GD_REQUEST,KUI_GD_PIOREAD,alias(PARAM,area),KUI_GD_PIOREAD,BASE_PARAM,0x600001f0u,false);
        assert(path>0 && observer->read_token==(uint32_t)path && observer->read_command==KUI_GD_PIOREAD);
        assert(observer->read_fad==45170u && observer->read_count==1 && observer->read_destination==alias(OUTPUT,area));
        assert(observer->read_requests==2 && observer->completed_reads==1 && !observer->read_completed && observer->read_status==UINT32_MAX);
        if(scenario==SECOND_INFLIGHT_REJECT) {
            params(45180u,2,OUTPUT+4096u);
            assert(!observed_call(frame,KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM,KUI_GD_DMAREAD,BASE_PARAM,0x600001f0u,true));
            assert(observer->read_token==(uint32_t)path && observer->read_fad==45170 && observer->read_command==KUI_GD_PIOREAD);
            assert(observer->read_requests==2 && observer->completed_reads==1 && observer->pvd_crc==first_crc);return;
        }
        if(scenario==SECOND_IO_FAIL) actual.fail_read=baseline.fail_read=1;
        assert(!observed_call(frame,KUI_GD_EXEC,0,0,0,0,0x600001f0u,false));
        int32_t result=observed_call(frame,KUI_GD_CHECK,(uint32_t)path,alias(STATUS,area),(uint32_t)path,BASE_STATUS,0x600001f0u,scenario==SECOND_IO_FAIL);
        if(scenario==SECOND_IO_FAIL) {
            assert(result==-1 && observer->read_requests==2 && observer->completed_reads==1);
            assert(observer->pvd_crc==first_crc && observer->pvd_header==first_header && observer->check[1]==KUI_GD_ERROR_IO);return;
        }
        assert(result==2 && observer->completed_reads==2 && observer->pvd_crc==first_crc && observer->pvd_header==first_header);
        assert(get(OUTPUT)!=first_header && !memcmp((const void *)(uintptr_t)OUTPUT,(const void *)(uintptr_t)BASE_OUTPUT,2048));status_equal();
        params(45180u,2,alias(OUTPUT,area));
        int32_t root=observed_call(frame,KUI_GD_REQUEST,KUI_GD_DMAREAD,alias(PARAM,area),KUI_GD_DMAREAD,BASE_PARAM,0x600001f0u,false);
        assert(root>0 && observer->read_token==(uint32_t)root && observer->read_requests==3 && observer->read_count==2 && observer->read_fad==45180);
        assert(!observed_call(frame,KUI_GD_EXEC,0,0,0,0,0x600001f0u,false));
        assert(observed_call(frame,KUI_GD_CHECK,(uint32_t)root,alias(STATUS,area),(uint32_t)root,BASE_STATUS,0x600001f0u,false)==2);
        assert(observer->completed_reads==3 && observer->pvd_crc==first_crc && observer->pvd_header==first_header && observer->check[2]==4096);
        assert(observer->path_lba==45020 && observer->root_lba==45030 && !memcmp((const void *)(uintptr_t)OUTPUT,(const void *)(uintptr_t)BASE_OUTPUT,4096));
        status_equal();silent(frame);mount_return(frame,0);return;
    }
#endif
    uint32_t token=submit_read(frame,0x80000000u,1);
    if(scenario==READ_IO_FAIL) {
        actual.fail_read=baseline.fail_read=1;
        assert(!observed_call(frame,KUI_GD_EXEC,0,0,0,0,0x600001f0u,false));
        assert(observed_call(frame,KUI_GD_CHECK,token,STATUS,token,BASE_STATUS,0x600001f0u,true)==-1);
        assert(line("GD CALL FAILED OR REJECTED") && observer->read_status==UINT32_MAX);
        assert(observer->check[1]==KUI_GD_ERROR_IO && !observer->pvd_crc && !observer->pvd_header);status_equal();return;
    }
    if(scenario==CHECK_MISMATCH) {
        assert(observed_call(frame,KUI_GD_CHECK,token+1u,STATUS,token+1u,BASE_STATUS,0x600001f0u,true)==-1);
        assert(observer->read_token==token && observer->read_status==UINT32_MAX && !observer->check[0]);
        assert(actual.service.pending && actual.service.token==token && !actual.reads);return;
    }
    if(scenario==INFLIGHT_READ) {
        params(45170u,2,OUTPUT+4096u);
        assert(!observed_call(frame,KUI_GD_REQUEST,KUI_GD_PIOREAD,PARAM,KUI_GD_PIOREAD,BASE_PARAM,0x600001f0u,true));
        assert(observer->read_token==token && observer->read_command==KUI_GD_DMAREAD);
        assert(observer->read_fad==45166u && observer->read_count==1u && observer->read_destination==OUTPUT);
        assert(actual.service.pending && !actual.reads);return;
    }
    assert(scenario==POLL_LIMIT);
    for(unsigned call=2;call<=GD_BUDGET;call++) {
        assert(observed_call(frame,KUI_GD_CHECK,token,STATUS,token,BASE_STATUS,0x600001f0u,call==GD_BUDGET)==1);
        status_equal();
    }
    assert(observer->calls==GD_BUDGET && line("GD CALL LIMIT - LAST RESULT SHOWN"));
    assert(actual.service.pending && !actual.reads && observer->read_status==1);
}
int main(void) {
    FILE *backing=tmpfile();assert(backing && !ftruncate(fileno(backing),RAM_BYTES));
    /* P0 physical guest numbers deliberately have no host mapping: the
     * observer must normalize them before sampling, as the reader does. */
    const uint32_t aliases[2]={0x8c000000u,0xac000000u};
    for(unsigned i=0;i<2;i++) {
        void *mapped=mmap((void *)(uintptr_t)aliases[i],RAM_BYTES,PROT_READ|PROT_WRITE,
            MAP_SHARED|MAP_FIXED_NOREPLACE,fileno(backing),0);
        if(mapped==MAP_FAILED) {perror("map shared RAM for GD observer");return 1;}
    }
    assert(!fclose(backing));synthetic_owner();
    unsigned count=0;
    for(enum scenario scenario=SEQUENCE_P0;scenario<=
#if KUI_RETAIL_STARTUP_TRACE >= 3
        CHECK_CONSUMED_BEFORE_NEXT_READ
#else
        READER_LAST
#endif
        ;scenario++) {
        pid_t child=fork();assert(child>=0);
        if(!child) {run_case(scenario);_Exit(0);}
        int status=0;assert(waitpid(child,&status,0)==child);
        if(!WIFEXITED(status) || WEXITSTATUS(status)) {
            fprintf(stderr,"GD trace scenario %u failed, status=%d\n",scenario,status);return 1;
        }
        ++count;
    }
    free(owner);
    for(unsigned i=0;i<2;i++) assert(!munmap((void *)(uintptr_t)aliases[i],RAM_BYTES));
    printf("PASS trace mode %u: %u executed cases; real dispatcher/baseline results, safe aliases, exact frames and bounded terminal evidence\n",(unsigned)KUI_RETAIL_STARTUP_TRACE,count);
    return 0;
}
