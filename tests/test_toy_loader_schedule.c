/* SPDX-License-Identifier: GPL-3.0-only */
/* Production GD core + pilot adapter. Only the low wrapper's hardware
 * pacing opportunity is modeled; it uses the same production step helper.
 * This verifies protocol/payload ownership, not console/IRQ throughput. */
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include "kui/toy_pilot_gd.h"
#include "kui/toy_loader_schedule.h"
extern int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *,uint32_t,
    uint32_t,uint32_t,uintptr_t);

#define BEGIN UINT32_C(0x8c100000)
#define PARAM (BEGIN+0x100u)
#define STATUS (BEGIN+0x200u)
#define OUTPUT (BEGIN+0x1000u)
static uint8_t ram[128u*1024u];
static struct kui_retail_gd service;
static struct kui_toy_pilot_snapshot audio;
static uint32_t deny,fail_lba,reads,mailboxes,refuse_audio,check_exec;
static uint32_t low_play_accepted,accepted_callbacks,max_chunk;
static uint64_t digest=UINT64_C(14695981039346656037);
static uint32_t complete_cases,cancel_cases,boundary_cases;

void kui_toy_loader_schedule_host_play(void) {
    low_play_accepted=1u;++accepted_callbacks;
}
static void hash(const void *bytes,size_t count) {
    const uint8_t *p=bytes;
    for(size_t i=0u;i<count;++i) {digest^=p[i];digest*=UINT64_C(1099511628211);}
}
static void put(uint32_t address,uint32_t value) {memcpy(ram+address-BEGIN,&value,4u);}
static uint32_t get(uint32_t address) {uint32_t value;memcpy(&value,ram+address-BEGIN,4u);return value;}
static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int access) {
    (void)context;assert(access>=0 && access<=KUI_RETAIL_MAP_VALIDATE);
    if(address==deny || address<BEGIN || address-BEGIN>sizeof(ram) ||
       bytes>sizeof(ram)-(address-BEGIN)) return NULL;
    return access==KUI_RETAIL_MAP_VALIDATE?ram+sizeof(ram):ram+address-BEGIN;
}
static int check(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;
    return (bytes==2048u || bytes==2352u) && lba>=45000u &&
        lba<=45100u && count<=45100u-lba?0:-1;
}
static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *output) {
    (void)context;++reads;assert(!check(NULL,lba,count,bytes));
    uint32_t budget=kui_toy_loader_schedule_step(low_play_accepted);
    assert(count && count<=budget);if(count>max_chunk) max_chunk=count;
    if(fail_lba && lba>=fail_lba) return -1;
    for(uint32_t n=0u;n<count;++n) for(uint32_t i=0u;i<bytes;++i)
        ((uint8_t *)output)[n*bytes+i]=(uint8_t)((lba+n)*13u+i*3u);
    return 0;
}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t a,uint32_t b,uint32_t c) {
    ++mailboxes;
    if(refuse_audio && (command==KUI_RETAIL_GD_PLAY || command==KUI_RETAIL_GD_PLAY2)) return 0u;
    audio.command=command;audio.parameters[0]=a;audio.parameters[1]=b;audio.parameters[2]=c;
    return ++audio.generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) {return &audio;}
static int32_t base(uint32_t a,uint32_t b,uint32_t c,uint32_t function) {
    service.step=kui_toy_loader_schedule_step(low_play_accepted);
    if(check_exec && function==KUI_GD_CHECK && service.pending &&
       (service.command==KUI_GD_PIOREAD || service.command==KUI_GD_DMAREAD))
        assert(!kui_retail_gd_dispatch(&service,0u,0u,0u,KUI_GD_EXEC));
    return kui_retail_gd_dispatch(&service,a,b,c,function);
}
static int32_t call(uint32_t function,uint32_t a,uint32_t b) {
    return kui_toy_pilot_gd_dispatch(&service,a,b,function,(uintptr_t)base);
}
static void cold_boot(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0u,.end_lba=16u,.control=4u|KUI_RETAIL_TRACK_COOKED}},
        {.track={.start_lba=16u,.end_lba=32u,.control=0u}},
        {.track={.start_lba=45000u,.end_lba=45100u,.control=4u}}
    };
    const struct kui_gd_ops ops={NULL,map,check,read_data};
    assert(!kui_retail_gd_init(&service,tracks,3u,&ops,BEGIN,BEGIN+sizeof(ram)));
    audio=(struct kui_toy_pilot_snapshot){.generation=1u,.applied_generation=1u,
        .state=KUI_TOY_PILOT_STOPPED,.driver_verified=1u,.sdk_init_result=1u,.position_fad=166u};
    memset(ram,0xa5,sizeof(ram));
    deny=fail_lba=reads=mailboxes=refuse_audio=check_exec=low_play_accepted=accepted_callbacks=max_chunk=0u;
}
static uint32_t submit(uint32_t count,uint32_t command) {
    put(PARAM,45150u);put(PARAM+4u,count);put(PARAM+8u,OUTPUT);put(PARAM+12u,0u);
    int32_t token=call(KUI_GD_REQUEST,command,PARAM);assert(token>0);
    assert(service.pending && service.command==command && !service.completed_bytes);
    return (uint32_t)token;
}
static void datatype(uint32_t bytes) {
    put(PARAM,0u);put(PARAM+4u,bytes==2352u?0x1000u:0x2000u);
    put(PARAM+8u,0u);put(PARAM+12u,bytes);
    assert(!call(KUI_GD_DATATYPE,PARAM,0u));
}
static void validate_bytes(uint32_t sectors,uint32_t bytes) {
    for(uint32_t n=0u;n<sectors;++n) for(uint32_t i=0u;i<bytes;++i)
        assert(ram[OUTPUT-BEGIN+n*bytes+i]==(uint8_t)((45000u+n)*13u+i*3u));
    for(uint32_t i=0u;i<32u;++i) assert(ram[OUTPUT-BEGIN+sectors*bytes+i]==0xa5u);
    hash(ram+OUTPUT-BEGIN,sectors*bytes+32u);
}
static void terminal(uint32_t token,int32_t expected,uint32_t bytes) {
    assert(service.command && !service.pending && service.completed_bytes==bytes);
    assert(call(KUI_GD_REQUEST,KUI_GD_NOP,0u)==0 && service.command);
    assert(call(KUI_GD_CHECK,token+1u,STATUS)==KUI_GD_FAILED && service.command);
    deny=STATUS;assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_FAILED && service.command);deny=0u;
    assert(call(KUI_GD_CHECK,token,STATUS)==expected && !service.command && !service.pending);
    assert(get(STATUS+8u)==bytes && !get(STATUS+12u));
    uint32_t state[]={token,(uint32_t)expected,get(STATUS),get(STATUS+4u),get(STATUS+8u),get(STATUS+12u)};
    hash(state,sizeof(state));assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_NOT_FOUND);
}
static void read_round(uint32_t sectors,uint32_t bytes,uint32_t command,int via_check) {
    cold_boot();datatype(bytes);uint32_t token=submit(sectors,command),credited=0u,visits=0u;
    uint32_t budget=kui_toy_loader_schedule_step(0u);
    if(via_check) check_exec=1u;
    while(service.pending) {
        ++visits;
        int32_t result=call(via_check?KUI_GD_CHECK:KUI_GD_EXEC,via_check?token:0u,via_check?STATUS:0u);
        assert(service.completed_bytes>credited && service.completed_bytes<=sectors*bytes);
        assert(service.completed_bytes-credited<=budget*bytes);credited=service.completed_bytes;
        if(via_check) {
            assert(result==(service.pending?KUI_GD_PROCESSING:KUI_GD_COMPLETED));
            assert(get(STATUS+8u)==credited);
            if(!service.pending) {assert(!service.command);hash(ram+STATUS-BEGIN,16u);}
        } else assert(!result);
    }
    assert(visits==(sectors+budget-1u)/budget && reads==visits);
    assert(max_chunk==(sectors<budget?sectors:budget));
    if(!via_check) terminal(token,KUI_GD_COMPLETED,sectors*bytes);
    else assert(call(KUI_GD_CHECK,token,STATUS)==KUI_GD_NOT_FOUND);
    validate_bytes(sectors,bytes);++complete_cases;
}
static void cancellation(void) {
    for(uint32_t kind=0u;kind<3u;++kind) {
        cold_boot();uint32_t token=submit(5u,KUI_GD_PIOREAD);
        while(service.completed_bytes<4096u) assert(!call(KUI_GD_EXEC,0u,0u));
        assert(service.completed_bytes==4096u && service.pending);
        if(kind==0u) {
            assert(call(KUI_GD_ABORT,token+1u,0u)==-1 && service.pending);
            assert(!call(KUI_GD_ABORT,token,0u) && !service.pending);
            terminal(token,KUI_GD_FAILED,4096u);validate_bytes(2u,2048u);
        } else if(kind==1u) {
            fail_lba=45002u;assert(!call(KUI_GD_EXEC,0u,0u) && !service.pending);
            assert(service.error==KUI_GD_ERROR_IO);terminal(token,KUI_GD_FAILED,4096u);
            validate_bytes(2u,2048u);
        } else {
            assert(!call(KUI_GD_RESET,0u,0u) && !service.command && !service.pending);
            assert(!low_play_accepted && !accepted_callbacks);
            memset(ram+OUTPUT-BEGIN,0xa5,5u*2048u+32u);
            token=submit(3u,KUI_GD_DMAREAD);
            while(service.pending) assert(!call(KUI_GD_EXEC,0u,0u));
            terminal(token,KUI_GD_COMPLETED,3u*2048u);validate_bytes(3u,2048u);
        }
        ++cancel_cases;
    }
}
static void boundary(void) {
    for(uint32_t play=KUI_RETAIL_GD_PLAY;play<=KUI_RETAIL_GD_PLAY2;++play) {
        cold_boot();uint32_t token=submit(1u,KUI_GD_DMAREAD);
        assert(call(KUI_GD_REQUEST,play,PARAM)==0 && !accepted_callbacks);
        assert(!call(KUI_GD_EXEC,0u,0u));terminal(token,KUI_GD_COMPLETED,2048u);
        put(PARAM,play==KUI_RETAIL_GD_PLAY?2u:166u);
        put(PARAM+4u,play==KUI_RETAIL_GD_PLAY?2u:174u);put(PARAM+8u,0u);
        deny=PARAM;assert(call(KUI_GD_REQUEST,play,PARAM)==0 && !accepted_callbacks);deny=0u;
        refuse_audio=1u;int32_t bad=call(KUI_GD_REQUEST,play,PARAM);
        assert(bad>0 && !service.pending && service.error==KUI_GD_ERROR_UNAVAILABLE);
        assert(!accepted_callbacks && !low_play_accepted);
        terminal((uint32_t)bad,KUI_GD_FAILED,0u);refuse_audio=0u;
        int32_t good=call(KUI_GD_REQUEST,play,PARAM);assert(good>0 && service.pending);
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE
        assert(accepted_callbacks==1u && low_play_accepted);
#else
        assert(!accepted_callbacks && !low_play_accepted);
#endif
        assert(kui_toy_loader_schedule_step(low_play_accepted)==2u);
        assert(!call(KUI_GD_EXEC,0u,0u));terminal((uint32_t)good,KUI_GD_COMPLETED,0u);
        /* The same real low core handles later DATA at its original step.
         * GD reset never reopens the startup profile after accepted PLAY. */
        assert(!call(KUI_GD_INIT,0u,0u));assert(!call(KUI_GD_RESET,0u,0u));
        assert(kui_toy_loader_schedule_step(low_play_accepted)==2u);
        memset(ram+OUTPUT-BEGIN,0xa5,5u*2048u+32u);
        token=submit(5u,KUI_GD_DMAREAD);uint32_t before=reads;
        while(service.pending) assert(!call(KUI_GD_EXEC,0u,0u));
        assert(reads-before==3u);terminal(token,KUI_GD_COMPLETED,5u*2048u);
        validate_bytes(5u,2048u);hash(&audio,sizeof(audio));++boundary_cases;
    }
}
int main(void) {
#if KUI_TOY_PILOT_DATA_SCHEDULE_MODE == 1
    assert(kui_toy_loader_schedule_step(0u)==1u);
#else
    assert(kui_toy_loader_schedule_step(0u)==2u);
#endif
    assert(kui_toy_loader_schedule_step(1u)==2u);
    const uint32_t counts[]={1u,2u,3u,5u,32u};
    for(unsigned i=0u;i<sizeof(counts)/sizeof(counts[0]);++i)
        for(uint32_t bytes=2048u;bytes<=2352u;bytes+=304u)
            for(uint32_t command=KUI_GD_PIOREAD;command<=KUI_GD_DMAREAD;++command)
                for(int via_check=0;via_check<2;++via_check)
                    read_round(counts[i],bytes,command,via_check);
    cancellation();boundary();
    printf("RESULT completed=%u cancelled=%u boundaries=%u digest=%016llx\n",
        complete_cases,cancel_cases,boundary_cases,(unsigned long long)digest);
    puts("Production DATA schedule: per-visit credit, full payload, CHECK completion/ack, wrong token/map, read failure, abort/reset, busy/invalid/mailbox-refused PLAY and permanent PLAY/PLAY2 handoff PASS");
    return 0;
}
