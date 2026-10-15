/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_loader_schedule_report.h"
#include "kui/toy_loader_schedule.h"
#include "kui/toy_pilot_clock.h"
#include <string.h>
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2
#include "toy_pilot_sci.h"
#endif
#if KUI_TOY_PILOT_DATA_PIPELINE_PROFILE
#include "sci_stream.h"
#endif
#if !KUI_TOY_PILOT_DATA_SCHEDULE_MODE
#error Schedule chronology is confined to enabled diagnostic profiles
#endif
/* Header16, engine32, then eight chronological records16. All storage is
 * private high P2. Timer control and guest bytes are never written here. */
static uint32_t schedule_report[KUI_TOY_SCHEDULE_REPORT_WORDS];
static struct {
    uint32_t initialized,call_active,start,valid,slot,request_start,request_valid;
    uint32_t token,command,credited;
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
    uint32_t call_irq,call_retries,request_irq,request_retries;
#endif
} schedule_visit;
#ifdef KUI_TOY_LOADER_SCHEDULE_REPORT_HOST_TEST
#define READ(a,n) kui_toy_loader_schedule_report_host_read((a),(n))
#else
static uint32_t schedule_mmio(uint32_t a,unsigned n) {
    if(n==1u)return *(volatile const uint8_t *)(uintptr_t)a;
    if(n==2u)return *(volatile const uint16_t *)(uintptr_t)a;
    return *(volatile const uint32_t *)(uintptr_t)a;
}
#define READ(a,n) schedule_mmio((a),(n))
#endif
static void schedule_add(uint32_t *p,uint32_t n) {
    if(n>UINT32_MAX-*p){*p=UINT32_MAX;schedule_report[10]=1u;}else *p+=n;
}
static uint32_t schedule_clock_sample(uint32_t *value) {
    uint32_t valid=kui_toy_pilot_clock_profile(READ(0xffc00000u,2u),
        READ(0xffd80008u,4u),READ(0xffd80010u,2u),READ(0xffd80004u,1u));
    *value=READ(0xffd8000cu,4u);
    if(!valid)schedule_visit.request_valid=0u;
    return valid;
}
static int schedule_data(uint32_t c){return c==KUI_GD_PIOREAD || c==KUI_GD_DMAREAD;}
static void schedule_initialize(void) {
    if(schedule_visit.initialized)return;
    schedule_visit.initialized=1u;schedule_visit.slot=UINT32_MAX;
    schedule_report[0]=UINT32_C(0x4c534331);schedule_report[1]=KUI_TOY_SCHEDULE_REPORT_VERSION;
    schedule_report[2]=KUI_TOY_SCHEDULE_REPORT_WORDS;
    schedule_report[3]=KUI_TOY_PILOT_DATA_SCHEDULE_MODE;
    schedule_report[4]=781250u;schedule_report[8]=UINT32_MAX;
}
static uint32_t *schedule_record(void) {
    return schedule_visit.slot<KUI_TOY_SCHEDULE_RECORDS?schedule_report+48u+
        schedule_visit.slot*KUI_TOY_SCHEDULE_RECORD_WORDS:NULL;
}
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
static void schedule_counter_delta(uint32_t *r) {
    const struct kui_toy_pilot_sci_stats *engine=kui_toy_pilot_sci_snapshot();
    /* Existing engine counters intentionally wrap. A finite request retains
     * its modular increment, including work done by the initial pump. */
    r[16]=engine->irq_blocks-schedule_visit.request_irq;
    r[17]=engine->retries-schedule_visit.request_retries;
    if(r[16]==UINT32_MAX || r[17]==UINT32_MAX)schedule_report[10]=1u;
}
#endif
static uint32_t schedule_crc32(const uint8_t *p,uint32_t n) {
    static const uint32_t nibble[16]={0u,0x1db71064u,0x3b6e20c8u,0x26d930acu,
        0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,0xedb88320u,
        0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,0x9b64c2b0u,0x86d3d2d4u,
        0xa00ae278u,0xbdbdf21cu};
    uint32_t crc=UINT32_MAX;
    for(uint32_t i=0;i<n;i++) {
        crc^=p[i];crc=(crc>>4)^nibble[crc&15u];crc=(crc>>4)^nibble[crc&15u];
    }
    return ~crc;
}
void kui_toy_loader_schedule_report_begin(const struct kui_retail_gd *s) {
    schedule_initialize();
    if(schedule_report[5] || schedule_visit.call_active)return;
    schedule_visit.call_active=1u;schedule_visit.valid=schedule_clock_sample(&schedule_visit.start);
    schedule_visit.token=s?s->token:0u;schedule_visit.command=s?s->command:0u;
    schedule_visit.credited=s?s->completed_bytes:0u;
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
    /* REQUEST dispatch may start a bounded pump before end(). Sample on
     * entry, then adopt these baselines only for an accepted DATA handle. */
    const struct kui_toy_pilot_sci_stats *engine=kui_toy_pilot_sci_snapshot();
    schedule_visit.call_irq=engine->irq_blocks;schedule_visit.call_retries=engine->retries;
#endif
}
void kui_toy_loader_schedule_report_end(const struct kui_retail_gd *s,uint32_t f,
    uint32_t command,int32_t result) {
    if(!schedule_visit.call_active || schedule_report[5])return;
    uint32_t end,valid=schedule_clock_sample(&end);
    schedule_visit.call_active=0u;
    if(s && f==KUI_GD_REQUEST && schedule_data(command) && result>0 &&
       s->command==command && s->token==(uint32_t)result) {
        uint32_t slot=schedule_report[7];
        schedule_visit.slot=slot;schedule_report[7]=(slot+1u)%KUI_TOY_SCHEDULE_RECORDS;
        schedule_add(&schedule_report[6],1u);schedule_report[8]=slot;
        uint32_t *r=schedule_record();
        if(!r){schedule_add(&schedule_report[11],1u);return;}
        memset(r,0,KUI_TOY_SCHEDULE_RECORD_WORDS*sizeof(uint32_t));
        r[0]=schedule_report[6];r[1]=s->token;r[2]=s->lba;r[3]=s->count;
        r[4]=s->destination;r[5]=s->sector_bytes;r[6]=command;
        r[7]=schedule_report[9]?16u:0u;
        r[12]=r[13]=UINT32_MAX;
        schedule_visit.request_start=schedule_visit.start;schedule_visit.request_valid=schedule_visit.valid && valid;
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
        schedule_visit.request_irq=schedule_visit.call_irq;
        schedule_visit.request_retries=schedule_visit.call_retries;
#endif
    }
    uint32_t *r=schedule_record();
    if(r && f==KUI_GD_ABORT && result==0 && command==r[1] &&
       schedule_visit.token==r[1] && schedule_data(schedule_visit.command))r[7]|=128u;
    if(r && (f==KUI_GD_INIT || f==KUI_GD_RESET)) {
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
        schedule_counter_delta(r);
#endif
        r[7]|=256u;
        schedule_report[8]=UINT32_MAX;
        schedule_visit.slot=UINT32_MAX;r=NULL;
    }
    if(r && s && s->token==r[1] && (schedule_data(s->command) ||
       (f==KUI_GD_CHECK && schedule_visit.token==r[1] && schedule_data(schedule_visit.command)))) {
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
        schedule_counter_delta(r);
#endif
        schedule_add(&r[14],1u);
        uint32_t elapsed=schedule_visit.start-end;
        if(schedule_visit.valid && valid && elapsed<=UINT32_C(0x7fffffff)) {
            if(elapsed>r[15])r[15]=elapsed;
        }else r[7]|=32u;
        /* A consumed CHECK clears the core's command but leaves the token.
         * Keep the last credited/status/error values rather than zero them. */
        if(schedule_data(s->command) || (f==KUI_GD_CHECK && command==r[1] &&
           !s->pending && !s->command && schedule_data(schedule_visit.command))) {
            r[8]=s->completed_bytes;r[9]=(uint32_t)s->status;r[10]=s->error;
            if(!(r[7]&(1u|64u)) && s->sector_bytes && s->sector_bytes<=2352u &&
               s->completed_bytes>=s->sector_bytes && s->ops.map) {
                uint32_t before,after,v=schedule_clock_sample(&before);
                uint32_t address=(s->destination&0x00ffffffu)|0x8c000000u;
                const uint8_t *bytes=s->ops.map(s->ops.context,address,s->sector_bytes,0);
                if(bytes){r[11]=schedule_crc32(bytes,s->sector_bytes);r[7]|=1u;}
                else {r[7]|=64u;schedule_add(&schedule_report[11],1u);}
                uint32_t av=schedule_clock_sample(&after),ticks=before-after;
                if(v && av && ticks<=UINT32_C(0x7fffffff)) {
                    schedule_add(&schedule_report[12],ticks);
                    if(ticks>schedule_report[13])schedule_report[13]=ticks;
                }
            }
            uint32_t age=schedule_visit.request_start-end;
            if(s->completed_bytes && !(r[7]&2u) && schedule_visit.request_valid && valid &&
               age<=UINT32_C(0x7fffffff)){r[12]=age;r[7]|=2u;}
            if(!s->pending && !(r[7]&4u) && schedule_visit.request_valid && valid &&
               age<=UINT32_C(0x7fffffff)){r[13]=age;r[7]|=4u;}
        }
        if(f==KUI_GD_CHECK && command==r[1] && !s->pending && !s->command) {
            r[7]|=8u;schedule_report[8]=UINT32_MAX;schedule_visit.slot=UINT32_MAX;
        }
    }
    if(s && f==KUI_GD_REQUEST && result>0 &&
       (command==KUI_RETAIL_GD_PLAY || command==KUI_RETAIL_GD_PLAY2) &&
       s->pending && s->command==command && s->count && !s->error)
        schedule_report[9]=1u;
}
void kui_toy_loader_schedule_report_freeze(void) {
    schedule_initialize();if(schedule_report[5])return;
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 2
    /* Retain pre-cleanup engine ownership before cancellation fences DMA. */
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
    uint32_t *active=schedule_record();
    if(active)schedule_counter_delta(active);
#endif
    memcpy(schedule_report+16u,kui_toy_pilot_sci_snapshot(),64u);
    const struct kui_toy_pilot_sci_startup_stats *startup=kui_toy_pilot_sci_startup_snapshot();
    memcpy(schedule_report+32u,startup,16u);
#if KUI_TOY_PILOT_DATA_BUDGET_PROFILE
    const struct kui_toy_pilot_sci_budget_stats *budget=kui_toy_pilot_sci_budget_snapshot();
    _Static_assert(sizeof(*budget)==48u,"SCHED exact twelve-word budget snapshot");
    memcpy(schedule_report+36u,budget,48u);
    for(uint32_t i=36u;i<48u;i++)
        if(schedule_report[i]==UINT32_MAX)schedule_report[10]=1u;
#endif
#if KUI_TOY_PILOT_DATA_PIPELINE_PROFILE
    /* FLOW is the native stream snapshot before cleanup changes its cause
     * counters. Engine64 and every chronological record retain their ABI. */
    _Static_assert(sizeof(struct kui_sci_stream_stats)==60u,"FLOW native fifteen-word stream snapshot");
    memcpy(schedule_report+192u,kui_sci_stream_stats(),60u);
    schedule_report[207]=KUI_TOY_PILOT_DATA_PIPELINE_PROFILE;
#endif
    if(kui_toy_pilot_sci_quiesce())schedule_report[14]=1u;
    else schedule_report[15]=1u;
#else
    schedule_report[14]=1u;
#endif
    schedule_report[5]=1u;
}
const uint32_t *kui_toy_loader_schedule_report_words(void){schedule_initialize();return schedule_report;}
