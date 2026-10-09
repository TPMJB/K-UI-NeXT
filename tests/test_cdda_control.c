/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do {++checks;assert(test);} while(0)
static struct kui_cdda_control_request request(enum kui_cdda_control_command command) {
    return (struct kui_cdda_control_request){.command=command};
}
static struct kui_cdda_control_action issue(struct kui_cdda_control *c, struct kui_cdda_control_request r) {
    struct kui_cdda_control_action a;
    CHECK(kui_cdda_control_request(c,&r,&a)==KUI_CDDA_CONTROL_OK);
    return a;
}
static void finish(struct kui_cdda_control *c, struct kui_cdda_control_action a) {
    CHECK(a.pending);
    CHECK(kui_cdda_control_complete(c,a.epoch,true)==KUI_CDDA_CONTROL_OK);
}
static struct kui_cdda_control_action play(struct kui_cdda_control *c,uint32_t first,uint32_t end,bool repeat) {
    struct kui_cdda_control_request r={KUI_CDDA_CONTROL_PLAY,first,end,0,repeat};
    struct kui_cdda_control_action a=issue(c,r);finish(c,a);return a;
}
static void unchanged(struct kui_cdda_control *c, struct kui_cdda_control_request r,
    enum kui_cdda_control_result expected) {
    struct kui_cdda_control saved=*c;
    struct kui_cdda_control_action a,b;
    memset(&a,0xb7,sizeof(a));b=a;
    CHECK(kui_cdda_control_request(c,&r,&a)==expected);
    CHECK(!memcmp(&saved,c,sizeof(saved)) && !memcmp(&a,&b,sizeof(a)));
}
static void commands(void) {
    struct kui_cdda_control c;
    CHECK(kui_cdda_control_init(&c,2000)==KUI_CDDA_CONTROL_OK);
    struct kui_cdda_control_action a=issue(&c,request(KUI_CDDA_CONTROL_STOP));
    CHECK(!a.pending && c.generation==0 && c.current.state==KUI_CDDA_CONTROL_STOPPED);
    unchanged(&c,request(KUI_CDDA_CONTROL_PAUSE),KUI_CDDA_CONTROL_STATE);
    unchanged(&c,request(KUI_CDDA_CONTROL_RESUME),KUI_CDDA_CONTROL_STATE);
    a=play(&c,100,1000,false);
    CHECK(c.current.frame==100 && c.current.state==KUI_CDDA_CONTROL_PLAYING && c.current.epoch==a.epoch);
    CHECK(kui_cdda_control_observe(&c,a.epoch,237)==KUI_CDDA_CONTROL_OK && c.current.frame==337);
    uint32_t played_epoch=a.epoch;
    struct kui_cdda_control snapshot=c;
    struct kui_cdda_control_status status;
    CHECK(kui_cdda_control_status(&c,&status)==KUI_CDDA_CONTROL_OK);
    a=issue(&c,request(KUI_CDDA_CONTROL_STATUS));
    CHECK(!a.pending && status.frame==337 && !memcmp(&c,&snapshot,sizeof(c)));
    a=issue(&c,request(KUI_CDDA_CONTROL_PAUSE));
    CHECK(c.current.frame==337 && c.current.state==KUI_CDDA_CONTROL_PLAYING && c.current.pending);
    CHECK(kui_cdda_control_observe(&c,played_epoch,238)==KUI_CDDA_CONTROL_BUSY);
    finish(&c,a);
    CHECK(c.current.state==KUI_CDDA_CONTROL_PAUSED && c.current.frame==337);
    uint32_t generation=c.generation;
    a=issue(&c,request(KUI_CDDA_CONTROL_PAUSE));
    CHECK(!a.pending && c.generation==generation && c.current.frame==337);
    a=issue(&c,request(KUI_CDDA_CONTROL_RESUME));finish(&c,a);
    CHECK(kui_cdda_control_observe(&c,a.epoch,23)==KUI_CDDA_CONTROL_OK && c.current.frame==360);
    generation=c.generation;
    CHECK(!issue(&c,request(KUI_CDDA_CONTROL_RESUME)).pending && c.generation==generation);
    struct kui_cdda_control_request r=request(KUI_CDDA_CONTROL_SEEK);r.frame=999;
    a=issue(&c,r);finish(&c,a);
    CHECK(c.current.frame==999 && c.current.state==KUI_CDDA_CONTROL_PLAYING);
    CHECK(kui_cdda_control_observe(&c,a.epoch,1)==KUI_CDDA_CONTROL_ENDED);
    CHECK(c.current.frame==1000 && c.current.state==KUI_CDDA_CONTROL_EOF);
    unchanged(&c,request(KUI_CDDA_CONTROL_RESUME),KUI_CDDA_CONTROL_STATE);
    unchanged(&c,request(KUI_CDDA_CONTROL_LOOP),KUI_CDDA_CONTROL_STATE);
    r.frame=1000;a=issue(&c,r);finish(&c,a);
    CHECK(c.current.state==KUI_CDDA_CONTROL_EOF);
    r.frame=100;a=issue(&c,r);finish(&c,a);
    CHECK(c.current.state==KUI_CDDA_CONTROL_STOPPED && c.current.frame==100);
    a=play(&c,100,1000,false);
    r.frame=99;unchanged(&c,r,KUI_CDDA_CONTROL_INVALID);
    r.frame=1001;unchanged(&c,r,KUI_CDDA_CONTROL_INVALID);
    r.command=(enum kui_cdda_control_command)99;unchanged(&c,r,KUI_CDDA_CONTROL_INVALID);
    r.command=(enum kui_cdda_control_command)-1;unchanged(&c,r,KUI_CDDA_CONTROL_INVALID);
    r=(struct kui_cdda_control_request){KUI_CDDA_CONTROL_PLAY,1000,1000,0,false};
    unchanged(&c,r,KUI_CDDA_CONTROL_INVALID);
    r.end=2001;unchanged(&c,r,KUI_CDDA_CONTROL_INVALID);
    a=issue(&c,request(KUI_CDDA_CONTROL_STOP));finish(&c,a);
    generation=c.generation;CHECK(!issue(&c,request(KUI_CDDA_CONTROL_STOP)).pending && c.generation==generation);
    CHECK(c.current.state==KUI_CDDA_CONTROL_STOPPED);
}
static void cancellation_and_failure(void) {
    struct kui_cdda_control c;
    CHECK(kui_cdda_control_init(&c,1000)==KUI_CDDA_CONTROL_OK);
    struct kui_cdda_control_request r={KUI_CDDA_CONTROL_PLAY,100,900,0,false};
    struct kui_cdda_control_action old=issue(&c,r);
    CHECK(c.current.state==KUI_CDDA_CONTROL_STOPPED && c.current.pending);
    unchanged(&c,request(KUI_CDDA_CONTROL_PAUSE),KUI_CDDA_CONTROL_BUSY);
    struct kui_cdda_control saved=c;
    struct kui_cdda_control_action status=issue(&c,request(KUI_CDDA_CONTROL_STATUS));
    CHECK(!status.pending && !memcmp(&saved,&c,sizeof(c)));
    struct kui_cdda_control_action stop=issue(&c,request(KUI_CDDA_CONTROL_STOP));
    CHECK(stop.epoch>old.epoch);
    saved=c;CHECK(kui_cdda_control_complete(&c,old.epoch,true)==KUI_CDDA_CONTROL_STALE);
    CHECK(!memcmp(&saved,&c,sizeof(c)));finish(&c,stop);
    saved=c;CHECK(kui_cdda_control_complete(&c,stop.epoch,true)==KUI_CDDA_CONTROL_STALE);
    CHECK(!memcmp(&saved,&c,sizeof(c)));
    old=issue(&c,r);r.first=200;
    struct kui_cdda_control_action newer=issue(&c,r);
    CHECK(kui_cdda_control_complete(&c,old.epoch,false)==KUI_CDDA_CONTROL_STALE);
    finish(&c,newer);CHECK(c.current.frame==200);
    saved=c;CHECK(kui_cdda_control_observe(&c,old.epoch,99)==KUI_CDDA_CONTROL_STALE);
    CHECK(!memcmp(&saved,&c,sizeof(c)));
    struct kui_cdda_control_action pause=issue(&c,request(KUI_CDDA_CONTROL_PAUSE));
    CHECK(kui_cdda_control_complete(&c,pause.epoch,false)==KUI_CDDA_CONTROL_IO);
    CHECK(c.current.state==KUI_CDDA_CONTROL_FAULT && !c.current.pending);
    unchanged(&c,request(KUI_CDDA_CONTROL_RESUME),KUI_CDDA_CONTROL_STATE);
    struct kui_cdda_control_request seek=request(KUI_CDDA_CONTROL_SEEK);seek.frame=300;
    unchanged(&c,seek,KUI_CDDA_CONTROL_STATE);
    stop=issue(&c,request(KUI_CDDA_CONTROL_STOP));finish(&c,stop);
    CHECK(c.current.state==KUI_CDDA_CONTROL_STOPPED);
    newer=play(&c,100,900,false);saved=c;
    CHECK(kui_cdda_control_fail(&c,old.epoch)==KUI_CDDA_CONTROL_STALE);
    CHECK(!memcmp(&saved,&c,sizeof(c)));
    CHECK(kui_cdda_control_fail(&c,newer.epoch)==KUI_CDDA_CONTROL_IO);
    CHECK(c.current.state==KUI_CDDA_CONTROL_FAULT && !c.current.pending);
    newer=issue(&c,r);
    CHECK(kui_cdda_control_fail(&c,newer.epoch)==KUI_CDDA_CONTROL_IO);
    CHECK(c.current.state==KUI_CDDA_CONTROL_FAULT && !c.current.pending);
}
static void loop_math(void) {
    static const uint32_t spans[]={1,2,3,588,44100,UINT32_MAX-1u,UINT32_MAX};
    static const uint32_t counts[]={0,1,2,587,588,44100,999999,UINT32_MAX-1u,UINT32_MAX};
    for(unsigned s=0;s<sizeof(spans)/sizeof(spans[0]);s++) {
        uint32_t span=spans[s];
        uint32_t first=UINT32_MAX-span,end=UINT32_MAX;
        for(unsigned p=0;p<sizeof(counts)/sizeof(counts[0]);p++)
            for(unsigned start=0;start<3;start++) {
                uint32_t offset=start==0?0:start==1?span/2u:span-1u;
                struct kui_cdda_control c;
                CHECK(kui_cdda_control_init(&c,UINT32_MAX)==KUI_CDDA_CONTROL_OK);
                play(&c,first,end,true);
                struct kui_cdda_control_request seek=request(KUI_CDDA_CONTROL_SEEK);seek.frame=first+offset;
                struct kui_cdda_control_action a=issue(&c,seek);finish(&c,a);
                uint64_t sum=(uint64_t)offset+counts[p];
                enum kui_cdda_control_result wanted=sum/span>UINT32_MAX?KUI_CDDA_CONTROL_OVERFLOW:KUI_CDDA_CONTROL_OK;
                struct kui_cdda_control saved=c;
                CHECK(kui_cdda_control_observe(&c,a.epoch,counts[p])==wanted);
                if(wanted==KUI_CDDA_CONTROL_OK) {
                    CHECK(c.current.frame==first+sum%span && c.current.loops==sum/span);
                    CHECK(c.current.state==KUI_CDDA_CONTROL_PLAYING);
                } else CHECK(!memcmp(&saved,&c,sizeof(c)));
            }
    }
    struct kui_cdda_control c;
    CHECK(kui_cdda_control_init(&c,1000)==KUI_CDDA_CONTROL_OK);
    struct kui_cdda_control_action a=play(&c,100,900,false);
    struct kui_cdda_control_request loop=request(KUI_CDDA_CONTROL_LOOP);loop.repeat=true;
    a=issue(&c,loop);finish(&c,a);
    CHECK(kui_cdda_control_observe(&c,a.epoch,1607)==KUI_CDDA_CONTROL_OK);
    CHECK(c.current.frame==107 && c.current.loops==2);
    struct kui_cdda_control saved=c;
    CHECK(kui_cdda_control_observe(&c,a.epoch,1606)==KUI_CDDA_CONTROL_INVALID);
    CHECK(!memcmp(&saved,&c,sizeof(c)));
    loop.repeat=false;a=issue(&c,loop);finish(&c,a);
    CHECK(kui_cdda_control_observe(&c,a.epoch,UINT32_MAX)==KUI_CDDA_CONTROL_ENDED);
    CHECK(c.current.frame==900 && c.current.loops==2);
}
static void limits_and_invalid(void) {
    struct kui_cdda_control c;
    CHECK(kui_cdda_control_init(&c,4)==KUI_CDDA_CONTROL_OK);
    struct kui_cdda_control saved=c;
    CHECK(kui_cdda_control_init(&c,0)==KUI_CDDA_CONTROL_INVALID && !memcmp(&saved,&c,sizeof(c)));
    CHECK(kui_cdda_control_init(NULL,4)==KUI_CDDA_CONTROL_INVALID);
    struct kui_cdda_control_request r=request(KUI_CDDA_CONTROL_PLAY);r.end=4;
    struct kui_cdda_control_action a;
    CHECK(kui_cdda_control_request(NULL,&r,&a)==KUI_CDDA_CONTROL_INVALID);
    CHECK(kui_cdda_control_request(&c,NULL,&a)==KUI_CDDA_CONTROL_INVALID);
    CHECK(kui_cdda_control_request(&c,&r,NULL)==KUI_CDDA_CONTROL_INVALID);
    CHECK(kui_cdda_control_complete(NULL,0,true)==KUI_CDDA_CONTROL_INVALID);
    CHECK(kui_cdda_control_observe(NULL,0,0)==KUI_CDDA_CONTROL_INVALID);
    CHECK(kui_cdda_control_status(&c,NULL)==KUI_CDDA_CONTROL_INVALID);
    c.generation=UINT32_MAX;
    unchanged(&c,r,KUI_CDDA_CONTROL_OVERFLOW);
    CHECK(!issue(&c,request(KUI_CDDA_CONTROL_STOP)).pending);
    CHECK(kui_cdda_control_init(&c,1)==KUI_CDDA_CONTROL_OK);
    a=play(&c,0,1,true);c.origin_loops=c.current.loops=UINT32_MAX;
    saved=c;
    CHECK(kui_cdda_control_observe(&c,a.epoch,1)==KUI_CDDA_CONTROL_OVERFLOW);
    CHECK(!memcmp(&saved,&c,sizeof(c)));
}
static void command_sequences(void) {
    /* Deterministic mixed commands check transactional refusal, bounds and
     * cancellation over many different active/paused/pending/EOF states. */
    struct kui_cdda_control c;
    CHECK(kui_cdda_control_init(&c,2000)==KUI_CDDA_CONTROL_OK);
    uint32_t rng=0x7142ab91u;
    for(unsigned i=0;i<30000;i++) {
        rng=rng*1664525u+1013904223u;
        struct kui_cdda_control_request r={(enum kui_cdda_control_command)(rng%7u),100,1000,(rng>>8)%2100u,(rng&16u)!=0};
        struct kui_cdda_control saved=c;
        struct kui_cdda_control_action a,b;memset(&a,0x79,sizeof(a));b=a;
        enum kui_cdda_control_result result=kui_cdda_control_request(&c,&r,&a);
        if(result!=KUI_CDDA_CONTROL_OK) CHECK(!memcmp(&saved,&c,sizeof(c)) && !memcmp(&a,&b,sizeof(a)));
        else if(a.pending && (rng&64u)) {
            CHECK(kui_cdda_control_complete(&c,a.epoch,(rng&128u)!=0)==((rng&128u)?KUI_CDDA_CONTROL_OK:KUI_CDDA_CONTROL_IO));
        }
        if(c.current.state==KUI_CDDA_CONTROL_PLAYING && !c.current.pending) {
            uint32_t played=rng>>16;
            saved=c;result=kui_cdda_control_observe(&c,c.current.epoch,played);
            if(result!=KUI_CDDA_CONTROL_OK && result!=KUI_CDDA_CONTROL_ENDED)
                CHECK(!memcmp(&saved,&c,sizeof(c)));
        }
        CHECK(c.current.first<c.current.end && c.current.end<=c.source_frames);
        CHECK(c.current.frame>=c.current.first && c.current.frame<=c.current.end);
        if(c.current.state==KUI_CDDA_CONTROL_EOF) CHECK(c.current.frame==c.current.end);
        if(c.current.state==KUI_CDDA_CONTROL_PLAYING && c.current.repeat) CHECK(c.current.frame<c.current.end);
        CHECK(c.current.pending==(c.current.pending_epoch!=0));
    }
}
int main(void) {
    commands();cancellation_and_failure();loop_math();limits_and_invalid();command_sequences();
    printf("PASS CDDA control: %u checks; staged commands, actual played cursor, idempotence, cancellation, EOF/loops and overflow\n",checks);
    return 0;
}
