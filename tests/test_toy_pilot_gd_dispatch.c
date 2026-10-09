/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot_gd.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define BEGIN UINT32_C(0x8c010000)
#define PARAM (BEGIN+32u)
#define OUT (BEGIN+128u)
#define STATUS (BEGIN+256u)
#define DATA (BEGIN+512u)
static uint8_t ram[4096];
static struct kui_retail_gd service;
static struct kui_toy_pilot_snapshot snapshot;
static unsigned requests,maps,reads,checks;
static uint32_t last_command,deny;
static bool missing,reject;

extern int32_t kui_toy_pilot_gd_dispatch(struct kui_retail_gd *,uint32_t,
    uint32_t,uint32_t,uintptr_t);

static uint8_t *map(void *context,uint32_t address,uint32_t bytes,int writing) {
    (void)context;++maps;
    assert(writing>=0 && writing<=KUI_RETAIL_MAP_VALIDATE);
    if(address<BEGIN || address-BEGIN>sizeof(ram) || bytes>sizeof(ram)-(address-BEGIN) ||
       address==deny) return NULL;
    return writing==KUI_RETAIL_MAP_VALIDATE?ram+sizeof(ram):ram+address-BEGIN;
}
static int check(void *context,uint32_t lba,uint32_t count,uint32_t bytes) {
    (void)context;(void)lba;(void)count;(void)bytes;++checks;return 0;
}
static int read_data(void *context,uint32_t lba,uint32_t count,uint32_t bytes,void *out) {
    (void)context;(void)lba;(void)count;(void)bytes;(void)out;++reads;
    assert(!"pilot scalar dispatch must not read storage");return -1;
}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t p0,uint32_t p1,uint32_t p2) {
    ++requests;last_command=command;
    if(reject) return 0;
    snapshot.command=command;snapshot.parameters[0]=p0;
    snapshot.parameters[1]=p1;snapshot.parameters[2]=p2;
    return ++snapshot.generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) {
    return missing?NULL:&snapshot;
}
static int32_t base(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    return kui_retail_gd_dispatch(&service,r4,r5,r6,r7);
}
static int32_t dispatch(uint32_t r4,uint32_t r5,uint32_t r7) {
    return kui_toy_pilot_gd_dispatch(&service,r4,r5,r7,(uintptr_t)base);
}
static uint32_t get(uint32_t address) {
    const uint8_t *p=ram+address-BEGIN;
    return (uint32_t)p[0] | (uint32_t)p[1]<<8 | (uint32_t)p[2]<<16 | (uint32_t)p[3]<<24;
}
static void put(uint32_t address,uint32_t value) {
    for(unsigned i=0;i<4;i++) ram[address-BEGIN+i]=(uint8_t)(value>>(8u*i));
}
static void init(void) {
    static const union kui_retail_slot tracks[]={
        {.track={.start_lba=0,.end_lba=8,.control=4}},
        {.track={.start_lba=16,.end_lba=24,.control=0}},
        {.track={.start_lba=45000,.end_lba=60000,.control=4}}
    };
    const struct kui_gd_ops ops={NULL,map,check,read_data};
    assert(!kui_retail_gd_init(&service,tracks,3,&ops,BEGIN,BEGIN+sizeof(ram)));
    snapshot=(struct kui_toy_pilot_snapshot){.command=20,.generation=7,
        .applied_generation=7,.state=KUI_TOY_PILOT_PLAYING,.track=2,.position_fad=170};
    requests=maps=reads=checks=0;deny=last_command=0;missing=reject=false;
    memset(ram,0xcc,sizeof(ram));
}
static uint32_t request(uint32_t command) {
    int32_t result=dispatch(command,PARAM,KUI_GD_REQUEST);
    assert(result>0 && service.command==command && service.pending);
    return (uint32_t)result;
}
static void consume(uint32_t token,int32_t expected) {
    assert(dispatch(token,STATUS,KUI_GD_CHECK)==expected);
    assert(!service.command && !service.pending);
    assert(dispatch(token,STATUS,KUI_GD_CHECK)==KUI_GD_NOT_FOUND);
}
static uint32_t drive(void) {
    assert(dispatch(STATUS+32u,0,KUI_GD_DRIVE)==0);return get(STATUS+32u);
}
static void query(uint32_t command) {
    uint32_t token=request(command);
    assert(dispatch(0,0,KUI_GD_EXEC)==0);
    consume(token,KUI_GD_COMPLETED);
}
static void audio_ack_and_handles(void) {
    init();put(PARAM,2);put(PARAM+4u,2);put(PARAM+8u,0);
    uint32_t token=request(KUI_RETAIL_GD_PLAY);
    assert(requests==1u && last_command==20u && service.count==8u);
    assert(snapshot.generation!=snapshot.applied_generation);
    assert(drive()==0u); /* BIOS BUSY while the accepted GD request is pending. */
    unsigned before=maps;
    assert(dispatch(0,0,KUI_GD_EXEC)==0);
    assert(maps==before && reads==0u && checks==0u);
    assert(service.status==KUI_GD_COMPLETED && !service.pending);
    assert(snapshot.applied_generation==7u); /* No worker needed for acceptance. */
    assert(drive()==3u && service.position_lba==16u);
    assert(dispatch(KUI_RETAIL_GD_PAUSE,PARAM,KUI_GD_REQUEST)==0);
    assert(requests==1u); /* A terminal handle still owns the native request. */
    consume(token,KUI_GD_COMPLETED);
    token=request(KUI_RETAIL_GD_PAUSE);assert(dispatch(0,0,KUI_GD_EXEC)==0);
    assert(drive()==1u);consume(token,KUI_GD_COMPLETED);
    token=request(KUI_RETAIL_GD_RELEASE);assert(dispatch(0,0,KUI_GD_EXEC)==0);
    assert(drive()==3u);consume(token,KUI_GD_COMPLETED);
    token=request(KUI_GD_STOP);assert(dispatch(0,0,KUI_GD_EXEC)==0);
    assert(drive()==2u);consume(token,KUI_GD_COMPLETED);
}
static void scalar_outputs(void) {
    init();const uint32_t states[]={KUI_TOY_PILOT_PREFILL,KUI_TOY_PILOT_START_WAIT,KUI_TOY_PILOT_PLAYING};
    for(unsigned i=0;i<sizeof(states)/sizeof(*states);i++) {
        snapshot.state=states[i];assert(drive()==3u);
        for(unsigned n=0;n<4;n++) put(PARAM+4u*n,OUT+4u*n);
        query(KUI_RETAIL_GD_REQ_STAT);
        assert(get(OUT)==3u && get(OUT+4u)==2u && get(OUT+8u)==0x100000aau && get(OUT+12u)==1u);
    }
    for(uint32_t format=0;format<3;format++) {
        uint32_t bytes=format==0?100u:format==1?14u:24u;
        const uint32_t caps[]={1u,2u,7u,100u};
        for(unsigned cap=0;cap<sizeof(caps)/sizeof(*caps);cap++) {
            uint32_t length=caps[cap]<bytes?caps[cap]:bytes;
            memset(ram+OUT-BEGIN,0xcc,104);
            put(PARAM,format);put(PARAM+4u,caps[cap]);put(PARAM+8u,OUT|0x20000000u);
            uint32_t token=request(KUI_RETAIL_GD_GETSCD);
            /* An ordinary encoded response at the same scalar cursor is the
             * independent baseline. Pilot changes precisely the status byte. */
            struct kui_retail_gd baseline=service;
            baseline.position_lba=20u;
            assert(kui_retail_gd_dispatch(&baseline,0,0,0,KUI_GD_EXEC)==0);
            uint8_t expected[100];memcpy(expected,ram+OUT-BEGIN,length);
            if(length>=2u) expected[1]=0x11u;
            memset(ram+OUT-BEGIN,0xcc,104);
            assert(dispatch(0,0,KUI_GD_EXEC)==0);
            assert(!memcmp(expected,ram+OUT-BEGIN,length));
            for(unsigned n=length;n<104u;n++) assert(ram[OUT-BEGIN+n]==0xccu);
            if(format==1 && length==14u) {
                assert(ram[OUT-BEGIN+5u]==2u && ram[OUT-BEGIN+6u]==1u);
                assert(ram[OUT-BEGIN+9u]==4u && ram[OUT-BEGIN+13u]==170u);
            }
            if(format==2 && length==24u) assert(ram[OUT-BEGIN+4u]==2u && ram[OUT-BEGIN+8u]==0u);
            /* The SDK can call the server again before CHECK consumes the
             * terminal handle. Such visits must leave the completed subcode
             * response intact, including its latched logical audio status. */
            unsigned completed_maps=maps;
            for(unsigned repeated=0;repeated<3u;repeated++) {
                assert(dispatch(0,0,KUI_GD_EXEC)==0);
                assert(!memcmp(expected,ram+OUT-BEGIN,length));
                assert(maps==completed_maps);
                assert(service.command==KUI_RETAIL_GD_GETSCD && !service.pending);
            }
            consume(token,KUI_GD_COMPLETED);
        }
    }
    assert(!reads && !checks && !requests);
    snapshot.state=KUI_TOY_PILOT_EOF;snapshot.position_fad=174u;
    put(PARAM,1);put(PARAM+4u,14);put(PARAM+8u,OUT);
    query(KUI_RETAIL_GD_GETSCD);
    assert(drive()==1u && ram[OUT-BEGIN+1u]==0x13u);
    assert(ram[OUT-BEGIN+5u]==2u && ram[OUT-BEGIN+9u]==7u && ram[OUT-BEGIN+13u]==173u);
    snapshot.state=KUI_TOY_PILOT_PAUSED;snapshot.position_fad=170u;
    query(KUI_RETAIL_GD_GETSCD);
    assert(drive()==1u && ram[OUT-BEGIN+1u]==0x12u && ram[OUT-BEGIN+13u]==170u);
    /* Destination protections stay with the base service and checked map. */
    put(PARAM,1);put(PARAM+4u,14);put(PARAM+8u,OUT);
    deny=OUT;assert(dispatch(KUI_RETAIL_GD_GETSCD,PARAM,KUI_GD_REQUEST)==0);
    deny=0;uint32_t token=request(KUI_RETAIL_GD_GETSCD);
    memset(ram+OUT-BEGIN,0xcc,16);deny=OUT;
    assert(dispatch(0,0,KUI_GD_EXEC)==0 && service.error==KUI_GD_ERROR_MEMORY);
    for(unsigned n=0;n<16;n++) assert(ram[OUT-BEGIN+n]==0xccu);
    deny=0;consume(token,KUI_GD_FAILED);
    put(PARAM+8u,BEGIN-2u);assert(dispatch(KUI_RETAIL_GD_GETSCD,PARAM,KUI_GD_REQUEST)==0);
    put(PARAM+8u,BEGIN+sizeof(ram)-1u);assert(dispatch(KUI_RETAIL_GD_GETSCD,PARAM,KUI_GD_REQUEST)==0);
}
static void cancellation_and_failures(void) {
    init();put(PARAM,2);put(PARAM+4u,2);put(PARAM+8u,0);
    uint32_t token=request(KUI_RETAIL_GD_PLAY);
    assert(dispatch(token+1u,0,KUI_GD_ABORT)==-1 && requests==1u && service.pending);
    assert(dispatch(0,0,KUI_GD_ABORT)==-1 && requests==1u && service.pending);
    assert(dispatch(token,0,KUI_GD_ABORT)==0 && requests==2u && last_command==KUI_GD_STOP);
    assert(service.error==KUI_GD_ERROR_CANCELLED);
    consume(token,KUI_GD_FAILED);assert(get(STATUS+4u)==0u); /* Repeat CHECK clears output. */
    token=request(KUI_RETAIL_GD_PLAY);assert(dispatch(0,0,KUI_GD_EXEC)==0);
    unsigned before=requests;assert(dispatch(token,0,KUI_GD_ABORT)==-1 && requests==before);
    consume(token,KUI_GD_COMPLETED);
    for(unsigned fault=0;fault<3;fault++) {
        token=request(KUI_RETAIL_GD_PLAY);
        if(fault==0) missing=true;
        else if(fault==1) snapshot.fault=KUI_TOY_PILOT_FAULT_QUEUE;
        else ++snapshot.generation;
        assert(dispatch(0,0,KUI_GD_EXEC)==0 && service.status==KUI_GD_FAILED);
        assert(service.error==(fault==2?KUI_GD_ERROR_CANCELLED:KUI_GD_ERROR_UNAVAILABLE));
        consume(token,KUI_GD_FAILED);missing=false;snapshot.fault=0;
    }
    assert(dispatch(0,0,KUI_GD_INIT)==0 && last_command==KUI_TOY_PILOT_RESET);
    assert(drive()==1u);
    /* Abort of a pending image request must not revoke the audio mailbox. */
    put(PARAM,150);put(PARAM+4u,1);put(PARAM+8u,DATA);put(PARAM+12u,0);
    token=request(KUI_GD_PIOREAD);before=requests;
    assert(dispatch(token,0,KUI_GD_ABORT)==0 && requests==before);
    assert(service.error==KUI_GD_ERROR_CANCELLED);
    consume(token,KUI_GD_FAILED);assert(checks==1u && !reads);
}
int main(void) {
    audio_ack_and_handles();scalar_outputs();cancellation_and_failures();
    puts("Toy GD adapter: immediate mailbox acceptance, native handle ownership, scalar audio/status layouts, truncation and protected outputs pass");
    return 0;
}
