/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual chronology module around the production protocol adapter/core.
 * Timer readings and engine cleanup are independent controlled fixtures. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "kui/toy_pilot_gd.h"
#include "kui/toy_loader_schedule_report.h"
#ifndef KUI_SCHEDULE_REPORT_SOURCE
#define KUI_SCHEDULE_REPORT_SOURCE "../src/loader/toy_loader_schedule_report.c"
#endif
#include KUI_SCHEDULE_REPORT_SOURCE

#define BEGIN UINT32_C(0x8c100000)
#define PARAM (BEGIN+0x100u)
#define STATUS (BEGIN+0x200u)
#define OUTPUT (BEGIN+0x1000u)
#define FLAGS_CRC 1u
#define FLAGS_FIRST 2u
#define FLAGS_COMPLETE 4u
#define FLAGS_ACK 8u
#define FLAGS_AFTER_PLAY 16u
#define FLAGS_INVALID 32u
#define FLAGS_MAP_FAILED 64u
#define FLAGS_ABORT 128u
#define FLAGS_RESET 256u
static uint8_t ram[128u*1024u];
static struct kui_retail_gd service;
static struct kui_toy_pilot_snapshot audio;
static uint32_t timeline,frqcr,tcor,tcr,tstr,read_scope,deny_crc,deny_status;
static uint32_t captured,low_play_accepted,refuse_audio,mmio_reads,map_reads,guest_write_maps;
static uint32_t body_end,crc_map_ticks,check_exec;

#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2
static struct kui_toy_pilot_sci_stats engine;
static struct kui_toy_pilot_sci_startup_stats startup;
static uint32_t cleanup_calls,cleanup_ok;
const struct kui_toy_pilot_sci_stats *kui_toy_pilot_sci_snapshot(void){return &engine;}
const struct kui_toy_pilot_sci_startup_stats *kui_toy_pilot_sci_startup_snapshot(void){return &startup;}
bool kui_toy_pilot_sci_quiesce(void){
    ++cleanup_calls;memset(&engine,0xde,sizeof(engine));memset(&startup,0xad,sizeof(startup));
    return cleanup_ok!=0u;
}
#else
extern int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *,uint32_t,uint32_t,uint32_t,uintptr_t);
#endif
uint32_t kui_toy_loader_schedule_report_host_read(uint32_t address,unsigned bytes) {
    ++mmio_reads;
    switch(address) {
    case 0xffc00000u:assert(bytes==2u);return frqcr;
    case 0xffd80008u:assert(bytes==4u);return tcor;
    case 0xffd80010u:assert(bytes==2u);return tcr;
    case 0xffd80004u:assert(bytes==1u);return tstr;
    case 0xffd8000cu:assert(bytes==4u);return ~timeline;
    default:assert(!"Unexpected telemetry MMIO read");return 0u;
    }
}
void kui_toy_loader_schedule_host_play(void){low_play_accepted=1u;}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t a,uint32_t b,uint32_t c) {
    if(refuse_audio && (command==KUI_RETAIL_GD_PLAY || command==KUI_RETAIL_GD_PLAY2))return 0u;
    audio.command=command;audio.parameters[0]=a;audio.parameters[1]=b;audio.parameters[2]=c;
    return ++audio.generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void){return &audio;}
static void put(uint32_t address,uint32_t value){memcpy(ram+address-BEGIN,&value,4u);}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int access) {
    (void)context;assert(access>=0 && access<=KUI_RETAIL_MAP_VALIDATE);
    if(read_scope){
        assert(access==0);++map_reads;timeline+=crc_map_ticks;
        if(deny_crc)return NULL;
    } else if(access==1)++guest_write_maps;
    if((address==STATUS && deny_status) || address<BEGIN || address-BEGIN>sizeof(ram) ||
       bytes>sizeof(ram)-(address-BEGIN))return NULL;
    return access==KUI_RETAIL_MAP_VALIDATE?ram+sizeof(ram):ram+address-BEGIN;
}
static int check(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;return bytes==2048u && lba>=45000u && lba<=45100u && count<=45100u-lba?0:-1;
}
static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;assert(!check(NULL,lba,count,bytes));timeline+=count*100u;
    for(uint32_t n=0u;n<count;++n)for(uint32_t i=0u;i<bytes;++i)((uint8_t *)out)[n*bytes+i]=(uint8_t)i;
    return 0;
}
static int32_t base(uint32_t a,uint32_t b,uint32_t c,uint32_t f) {
    service.step=kui_toy_loader_schedule_step(low_play_accepted);
    if(check_exec && f==KUI_GD_CHECK && service.pending &&
       (service.command==KUI_GD_PIOREAD || service.command==KUI_GD_DMAREAD))
        assert(!kui_retail_gd_dispatch(&service,0u,0u,0u,KUI_GD_EXEC));
    return kui_retail_gd_dispatch(&service,a,b,c,f);
}
static int32_t call(uint32_t f,uint32_t a,uint32_t b) {
    if(captured)kui_toy_loader_schedule_report_begin(&service);
    timeline+=3u;
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2
    int32_t result=base(a,b,0u,f);
#else
    int32_t result=kui_toy_pilot_gd_dispatch(&service,a,b,f,(uintptr_t)base);
#endif
    body_end=timeline;
    if(captured){read_scope=1u;kui_toy_loader_schedule_report_end(&service,f,a,result);read_scope=0u;}
    return result;
}
static void fresh(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0u,.end_lba=16u,.control=4u|KUI_RETAIL_TRACK_COOKED}},
        {.track={.start_lba=16u,.end_lba=32u,.control=0u}},
        {.track={.start_lba=45000u,.end_lba=45100u,.control=4u}}
    };
    const struct kui_gd_ops ops={NULL,map,check,read_data};
    assert(!kui_retail_gd_init(&service,tracks,3u,&ops,BEGIN,BEGIN+sizeof(ram)));
    audio=(struct kui_toy_pilot_snapshot){.generation=1u,.state=KUI_TOY_PILOT_STOPPED};
    memset(ram,0xa5,sizeof(ram));memset(schedule_report,0,sizeof(schedule_report));memset(&schedule_visit,0,sizeof(schedule_visit));
    timeline=0u;frqcr=0xe0au;tcor=UINT32_MAX;tcr=2u;tstr=1u;
    captured=1u;read_scope=deny_crc=deny_status=low_play_accepted=refuse_audio=mmio_reads=map_reads=guest_write_maps=0u;
    crc_map_ticks=7u;check_exec=0u;
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2
    memset(&engine,0,sizeof(engine));memset(&startup,0,sizeof(startup));cleanup_calls=0u;cleanup_ok=1u;
#endif
}
static uint32_t submit(uint32_t count,uint32_t offset) {
    put(PARAM,45150u+offset);put(PARAM+4u,count);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    int32_t token=call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM);assert(token>0);return (uint32_t)token;
}
static const uint32_t *last(void){return kui_toy_loader_schedule_report_words()+48u+schedule_report[8]*16u;}
static uint64_t hash(const void *data,size_t n) {
    const uint8_t *p=data;uint64_t digest=UINT64_C(14695981039346656037);
    for(size_t i=0u;i<n;++i){digest^=p[i];digest*=UINT64_C(1099511628211);}return digest;
}
static uint64_t differential(int enabled) {
    fresh();captured=(uint32_t)enabled;uint32_t token=submit(5u,0u);
    while(service.pending)assert(!call(KUI_GD_EXEC,0u,0u));
    deny_status=1u;assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED && service.command);deny_status=0u;
    assert(call(KUI_GD_CHECK,token+1u,STATUS)==KUI_GD_FAILED && service.command);
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED && !service.command);
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_NOT_FOUND);
    uint64_t result=hash(ram,sizeof(ram))^hash(&audio,sizeof(audio));
    if(!enabled)assert(!mmio_reads && !map_reads);
    else assert(map_reads==1u);
    return result;
}
static void chronology_and_crc(void) {
    fresh();uint32_t token=submit(3u,0u);const uint32_t *r=last();
    assert(schedule_report[0]==UINT32_C(0x4c534331) && schedule_report[1]==1u && schedule_report[2]==176u);
    assert(schedule_report[3]==KUI_TOY_PILOT_DATA_SCHEDULE_MODE && schedule_report[4]==781250u);
    assert(schedule_report[6]==1u && schedule_report[7]==1u && schedule_report[8]==0u);
    assert(r[0]==1u && r[1]==token && r[2]==45000u && r[3]==3u && r[4]==OUTPUT && r[5]==2048u && r[6]==KUI_GD_DMAREAD);
    assert(!r[7] && r[12]==UINT32_MAX && r[13]==UINT32_MAX);
    timeline+=10u;assert(!call(KUI_GD_EXEC,0u,0u));
    assert((r[7]&(FLAGS_CRC|FLAGS_FIRST))==(FLAGS_CRC|FLAGS_FIRST));
    assert(r[11]==UINT32_C(0x9f5edd58) && r[12]==body_end && map_reads==1u);
    uint32_t first=r[12];timeline+=20u;
    while(service.pending)assert(!call(KUI_GD_EXEC,0u,0u));
    assert((r[7]&(FLAGS_CRC|FLAGS_FIRST|FLAGS_COMPLETE))==(FLAGS_CRC|FLAGS_FIRST|FLAGS_COMPLETE));
    assert(r[8]==6144u && r[9]==KUI_GD_COMPLETED && !r[10] && r[12]==first && r[13]==body_end);
    assert(schedule_report[12]==7u && schedule_report[13]==7u && map_reads==1u);
    assert(call(KUI_GD_ABORT,token,0u)==-1 && !(r[7]&FLAGS_ABORT) && schedule_report[8]==0u);
    assert(call(KUI_GD_CHECK,token+1u,STATUS)==KUI_GD_FAILED && !(r[7]&FLAGS_ACK));
    deny_status=1u;assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED && !(r[7]&FLAGS_ACK));deny_status=0u;
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED && (r[7]&FLAGS_ACK));
    assert(schedule_report[8]==UINT32_MAX && r[8]==6144u && r[9]==KUI_GD_COMPLETED);
    uint32_t consumed[16];memcpy(consumed,r,sizeof(consumed));
    assert(!call(KUI_GD_RESET,0u,0u));assert(!memcmp(consumed,r,sizeof(consumed)));
}
static void paced_check_completes_and_consumes(void) {
    for(uint32_t sectors=1u;sectors<=3u;sectors+=2u) {
        fresh();uint32_t token=submit(sectors,0u);const uint32_t *r=last();check_exec=1u;
        int32_t result;
        do {result=call(KUI_GD_CHECK,token,STATUS);}while(result==KUI_GD_PROCESSING);
        assert(result==KUI_GD_COMPLETED && !service.command && !service.pending);
        assert(r[8]==sectors*2048u && r[9]==KUI_GD_COMPLETED && !r[10]);
        assert((r[7]&(FLAGS_CRC|FLAGS_FIRST|FLAGS_COMPLETE|FLAGS_ACK))==
            (FLAGS_CRC|FLAGS_FIRST|FLAGS_COMPLETE|FLAGS_ACK));
        assert(r[11]==UINT32_C(0x9f5edd58) && r[12]!=UINT32_MAX && r[13]!=UINT32_MAX);
        assert(schedule_report[8]==UINT32_MAX && map_reads==1u);
    }
}
static void map_failure_and_recovery(void) {
    fresh();uint32_t token=submit(3u,0u);deny_crc=1u;assert(!call(KUI_GD_EXEC,0u,0u));
    const uint32_t *r=last();assert(!(r[7]&FLAGS_CRC) && (r[7]&FLAGS_MAP_FAILED) && schedule_report[11]==1u);
    deny_crc=0u;assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_PROCESSING);
    assert(!(r[7]&FLAGS_CRC) && (r[7]&FLAGS_MAP_FAILED) && map_reads==1u && schedule_report[11]==1u);
}
static void timing_wrap_and_invalid(void) {
    fresh();timeline=UINT32_MAX-40u;uint32_t start=timeline;
    (void)submit(1u,0u);assert(!call(KUI_GD_EXEC,0u,0u));const uint32_t *r=last();
    assert(!(r[7]&FLAGS_INVALID) && (r[7]&FLAGS_FIRST) && (r[7]&FLAGS_COMPLETE));
    assert(r[12]==body_end-start && r[13]==body_end-start && r[15]==103u);
    fresh();tstr=0u;(void)submit(1u,0u);assert(!call(KUI_GD_EXEC,0u,0u));r=last();
    assert((r[7]&FLAGS_INVALID) && !(r[7]&(FLAGS_FIRST|FLAGS_COMPLETE)));
    assert(r[12]==UINT32_MAX && r[13]==UINT32_MAX);
    /* A valid start/end cannot salvage request ages across an intervening
     * invalid clock profile. Per-call duration may still become valid. */
    fresh();uint32_t token=submit(3u,0u);tcor=1u;
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_PROCESSING);tcor=UINT32_MAX;
    assert(!call(KUI_GD_EXEC,0u,0u));while(service.pending)assert(!call(KUI_GD_EXEC,0u,0u));r=last();
    assert((r[7]&FLAGS_INVALID) && !(r[7]&(FLAGS_FIRST|FLAGS_COMPLETE)));
    assert(r[12]==UINT32_MAX && r[13]==UINT32_MAX);
}
static void accepted_request_fails_before_exit(void) {
    fresh();captured=0u;kui_toy_loader_schedule_report_begin(&service);
    uint32_t token=submit(3u,0u);
    /* The production core owns the returned positive handle. An initial
     * bounded transport visit may fail before the adapter returns it. */
    service.pending=0u;service.status=KUI_GD_FAILED;service.error=KUI_GD_ERROR_IO;
    read_scope=1u;kui_toy_loader_schedule_report_end(&service,KUI_GD_REQUEST,KUI_GD_DMAREAD,(int32_t)token);read_scope=0u;
    assert(schedule_report[6]==1u);const uint32_t *r=last();
    assert(r[1]==token && !r[8] && r[9]==(uint32_t)KUI_GD_FAILED && r[10]==KUI_GD_ERROR_IO);
    assert((r[7]&FLAGS_COMPLETE) && !(r[7]&(FLAGS_FIRST|FLAGS_CRC)) && r[13]==3u);
    captured=1u;assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED && (r[7]&FLAGS_ACK));
}
static void rolling_and_rejections(void) {
    fresh();put(PARAM,45150u);put(PARAM+4u,101u);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    assert(call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM)==0 && !schedule_report[6]);
    for(uint32_t ordinal=1u;ordinal<=10u;++ordinal) {
        uint32_t token=submit(1u,ordinal-1u);
        assert(call(KUI_GD_REQUEST,KUI_GD_DMAREAD,PARAM)==0 && schedule_report[6]==ordinal);
        assert(!call(KUI_GD_EXEC,0u,0u));assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    }
    assert(schedule_report[6]==10u && schedule_report[7]==2u);
    for(uint32_t i=0u;i<8u;++i) {
        uint32_t slot=(schedule_report[7]+i)%8u;const uint32_t *r=schedule_report+48u+slot*16u;
        assert(r[0]==i+3u && r[2]==45002u+i && (r[7]&FLAGS_ACK));
    }
}
static void abort_and_reset(void) {
    fresh();uint32_t token=submit(5u,0u);assert(!call(KUI_GD_EXEC,0u,0u));const uint32_t *r=last();
    uint32_t bytes=service.completed_bytes;
    assert(call(KUI_GD_ABORT,token+1u,0u)==-1 && !(r[7]&FLAGS_ABORT));
    assert(!call(KUI_GD_ABORT,token,0u));assert((r[7]&FLAGS_ABORT) && (r[7]&FLAGS_COMPLETE));
    assert(r[8]==bytes && r[9]==(uint32_t)KUI_GD_FAILED && r[10]==KUI_GD_ERROR_CANCELLED);
    assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED && (r[7]&FLAGS_ACK));
    fresh();(void)submit(5u,0u);assert(!call(KUI_GD_EXEC,0u,0u));r=last();bytes=service.completed_bytes;
    assert(!call(KUI_GD_RESET,0u,0u));assert((r[7]&FLAGS_RESET) && r[8]==bytes);
    assert(schedule_report[8]==UINT32_MAX && !service.command && !service.pending);
}
static void freeze_report(void) {
    fresh();(void)submit(3u,0u);assert(!call(KUI_GD_EXEC,0u,0u));
    kui_toy_loader_schedule_report_freeze();assert(schedule_report[5] && schedule_report[14]==1u);
    uint32_t frozen[176];memcpy(frozen,schedule_report,sizeof(frozen));uint32_t read_count=mmio_reads;
    assert(!call(KUI_GD_RESET,0u,0u));assert(!memcmp(frozen,schedule_report,sizeof(frozen)) && mmio_reads==read_count);
    kui_toy_loader_schedule_report_freeze();assert(!memcmp(frozen,schedule_report,sizeof(frozen)));
}
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE != 2
static void accepted_play_boundary(void) {
    fresh();uint32_t token=submit(1u,0u);
    assert(call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,PARAM)==0 && !schedule_report[9]);
    assert(!call(KUI_GD_EXEC,0u,0u));assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_COMPLETED);
    put(PARAM,2u);put(PARAM+4u,2u);put(PARAM+8u,0u);refuse_audio=1u;
    int32_t failed=call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,PARAM);assert(failed>0 && !schedule_report[9]);
    assert(call(KUI_GD_CHECK,(uint32_t)failed,STATUS)==KUI_GD_FAILED);refuse_audio=0u;
    int32_t accepted=call(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,PARAM);assert(accepted>0 && schedule_report[9]);
    assert(!call(KUI_GD_EXEC,0u,0u));assert(call(KUI_GD_CHECK,(uint32_t)accepted,STATUS)==KUI_GD_COMPLETED);
    (void)submit(1u,1u);assert(last()[7]&FLAGS_AFTER_PLAY);
}
#else
static void engine_freeze_before_cleanup(void) {
    for(uint32_t ok=0u;ok<2u;++ok) {
        fresh();uint32_t *e=(uint32_t *)(void *)&engine;for(unsigned i=0u;i<16u;++i)e[i]=100u+i;
        startup=(struct kui_toy_pilot_sci_startup_stats){1u,0u,7u,3u};cleanup_ok=ok;
        struct kui_toy_pilot_sci_stats original=engine;struct kui_toy_pilot_sci_startup_stats initial=startup;
        kui_toy_loader_schedule_report_freeze();
        assert(!memcmp(schedule_report+16u,&original,sizeof(original)));
        assert(!memcmp(schedule_report+32u,&initial,sizeof(initial)));
        assert(cleanup_calls==1u && schedule_report[14]==ok && schedule_report[15]==!ok);
        uint32_t frozen[176];memcpy(frozen,schedule_report,sizeof(frozen));
        kui_toy_loader_schedule_report_freeze();assert(cleanup_calls==1u && !memcmp(frozen,schedule_report,sizeof(frozen)));
    }
}
#endif
int main(void) {
    uint64_t baseline=differential(0);uint32_t writes=guest_write_maps;
    uint64_t instrumented=differential(1);assert(baseline==instrumented && guest_write_maps==writes);
    chronology_and_crc();paced_check_completes_and_consumes();map_failure_and_recovery();timing_wrap_and_invalid();accepted_request_fails_before_exit();rolling_and_rejections();
    abort_and_reset();freeze_report();
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE != 2
    accepted_play_boundary();
#else
    engine_freeze_before_cleanup();
#endif
    printf("SCHED protocol/payload digest=%016llx\n",(unsigned long long)baseline);
    puts("SCHED production chronology: read-only CRC, first-credit/terminal/consumed CHECK, wrong token/map, rolling eviction, rejected/accepted PLAY, clock wrap/profile rejection, abort/reset and immutable pre-cleanup freeze PASS");
    return 0;
}
