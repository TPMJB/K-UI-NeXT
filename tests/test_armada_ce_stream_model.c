/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_stream_model.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define R KUI_ARMADA_CE_STREAM_RING_BYTES
#define S KUI_ARMADA_CE_STREAM_SECTOR_BYTES
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static struct kui_armada_ce_stream_model model;
static uint8_t stage[KUI_ARMADA_CE_STREAM_STAGE_BYTES],ring[R];
static unsigned staged;
static uint8_t source_byte(uint32_t lba,uint32_t byte) {
    uint32_t word=0x91c034a5u^((lba*588u+byte/4u)*0x00010003u);
    return (uint8_t)(word>>(8u*(byte&3u)));
}
static void reset(const struct kui_armada_ce_stream_track *t,unsigned count) {
    CHECK(kui_armada_ce_stream_init(&model,t,count)==KUI_ARMADA_CE_STREAM_OK);
    staged=0;memset(stage,0xa5,sizeof(stage));memset(ring,0xc7,sizeof(ring));
}
static bool pump(void) {
    struct kui_armada_ce_stream_refill f;
    enum kui_armada_ce_stream_result result=kui_armada_ce_stream_refill_plan(&model,KUI_ARMADA_CE_STREAM_STAGE_BYTES,&f);
    if(result==KUI_ARMADA_CE_STREAM_OK) {
        CHECK(!(f.ring_offset&63u) && !(f.bytes&63u) && f.ring_offset+f.bytes<=R);
        CHECK(f.audio_bytes<=staged && f.audio_bytes+f.pad_bytes==f.bytes);
        CHECK(kui_armada_ce_stream_refill_validate(&model,&f)==KUI_ARMADA_CE_STREAM_OK);
        memcpy(ring+f.ring_offset,stage,f.audio_bytes);
        memset(ring+f.ring_offset+f.audio_bytes,0,f.pad_bytes);
        CHECK(kui_armada_ce_stream_refill_complete(&model,&f,true)==KUI_ARMADA_CE_STREAM_OK);
        memmove(stage,stage+f.audio_bytes,staged-f.audio_bytes);staged-=f.audio_bytes;
        CHECK(staged==model.staged);return true;
    }
    CHECK(result==KUI_ARMADA_CE_STREAM_NOTHING);
    if(model.produced-model.consumed>=R-KUI_ARMADA_CE_STREAM_GUARD_BYTES && model.native_running)
        return false;
    struct kui_armada_ce_stream_read read;
    result=kui_armada_ce_stream_read_plan(&model,KUI_ARMADA_CE_STREAM_BATCH_SECTORS,&read);
    if(result==KUI_ARMADA_CE_STREAM_NOTHING) return false;
    CHECK(result==KUI_ARMADA_CE_STREAM_OK && read.sectors>=1u &&
        read.sectors<=KUI_ARMADA_CE_STREAM_BATCH_SECTORS);
    CHECK(read.skip<S && read.bytes+read.skip<=read.sectors*S);
    CHECK(staged+read.bytes<=sizeof(stage));
    CHECK(kui_armada_ce_stream_read_validate(&model,&read)==KUI_ARMADA_CE_STREAM_OK);
    for(unsigned i=0;i<read.bytes;i++) {
        uint32_t raw=read.skip+i;
        stage[staged+i]=source_byte(read.lba+raw/S,raw%S);
    }
    CHECK(kui_armada_ce_stream_read_complete(&model,&read,true,read.bytes)==KUI_ARMADA_CE_STREAM_OK);
    staged+=read.bytes;CHECK(staged==model.staged);return true;
}
static void prime(void) {
    unsigned work=0;
    while(!kui_armada_ce_stream_can_start(&model)) {CHECK(++work<2000u);CHECK(pump());}
    CHECK(kui_armada_ce_stream_start_complete(&model,model.epoch,0u,true)==KUI_ARMADA_CE_STREAM_OK);
}
static void play_frames(unsigned sectors,unsigned repeat,bool infinite) {
    unsigned tracks=sectors>1u?2u:1u;
    struct kui_armada_ce_stream_track t[2]={{1u,0u,1000u,1000u+sectors},
        {2u,0u,1000u+sectors/2u,1000u+sectors}};
    if(tracks==2u) t[0].end_lba=t[1].first_lba;
    reset(t,tracks);CHECK(kui_armada_ce_stream_request(&model,20u,1u,tracks,repeat)==KUI_ARMADA_CE_STREAM_OK);
    prime();uint32_t ms=0;uint64_t physical=0;
    uint64_t end=infinite?3u*(uint64_t)R:(uint64_t)sectors*S*(repeat+1u);
    /* Play may start with half the ring fresh. Everything produced past a
     * finite end is silence; the rest is filled ahead of the cursor. */
    CHECK(model.produced>=KUI_ARMADA_CE_STREAM_START_BYTES && model.produced<=R);
    if(!infinite && end<model.produced) for(unsigned i=(unsigned)end;i<model.produced;i++) CHECK(!ring[i]);
    while(physical<end) {
        unsigned bytes=end-physical>4096u?4096u:(unsigned)(end-physical);
        bytes=(bytes+3u)&~3u;
        for(unsigned i=0;i<bytes;i++) {
            uint64_t relative=(physical+i)%((uint64_t)sectors*S);
            CHECK(ring[(physical+i)%R]==source_byte(t[0].first_lba+(uint32_t)(relative/S),(uint32_t)(relative%S)));
        }
        physical+=bytes;ms+=25u;
        uint32_t p=(uint32_t)(physical%R);
        enum kui_armada_ce_stream_result s=kui_armada_ce_stream_observe(&model,model.ring_epoch,p,(p+2048u)%R,ms);
        if(!infinite && physical>=end) {
            CHECK(s==KUI_ARMADA_CE_STREAM_EOF && model.audible==end && model.stop_needed);
            CHECK(kui_armada_ce_stream_stop_complete(&model,model.epoch,true)==KUI_ARMADA_CE_STREAM_OK);
            CHECK(model.state==KUI_ARMADA_CE_STREAM_ENDED);break;
        }
        CHECK(s==KUI_ARMADA_CE_STREAM_OK && model.audible==physical);
        unsigned work=0;while(pump()) CHECK(++work<1000u);
    }
}
int main(void) {
    const struct kui_armada_ce_stream_track tracks[]={
        {1u,4u,0u,10u},{2u,0u,10u,17u},{3u,0u,17u,23u},{4u,4u,30u,40u},{5u,0u,45u,50u}};
    CHECK(kui_armada_ce_stream_init(NULL,tracks,5u)==KUI_ARMADA_CE_STREAM_ARGUMENT);
    struct kui_armada_ce_stream_track bad[5];memcpy(bad,tracks,sizeof(bad));bad[2].first_lba=16u;
    CHECK(kui_armada_ce_stream_init(&model,bad,5u)==KUI_ARMADA_CE_STREAM_ARGUMENT);
    reset(tracks,5u);
    const uint32_t invalid[][4]={{20,0,2,0},{20,3,2,0},{20,1,1,0},{20,2,4,0},
        {20,2,5,0},{20,2,3,16},{21,159,161,0},{21,160,160,0},{21,172,181,0}};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
        CHECK(kui_armada_ce_stream_request(&model,invalid[i][0],invalid[i][1],invalid[i][2],invalid[i][3])==KUI_ARMADA_CE_STREAM_ARGUMENT);
    CHECK(!model.epoch);
    CHECK(kui_armada_ce_stream_request(&model,21u,160u,173u,0u)==KUI_ARMADA_CE_STREAM_OK && model.first_lba==10u && model.end_lba==23u);
    CHECK(kui_armada_ce_stream_request(&model,22u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK && model.state==KUI_ARMADA_CE_STREAM_PAUSED);
    uint32_t selection=model.selection;
    CHECK(kui_armada_ce_stream_request(&model,23u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK &&
        model.state==KUI_ARMADA_CE_STREAM_PREFILL && model.selection==selection && !model.audible);
    struct kui_armada_ce_stream_read rd,forged;
    CHECK(kui_armada_ce_stream_read_plan(&model,KUI_ARMADA_CE_STREAM_BATCH_SECTORS+1u,&rd)==
        KUI_ARMADA_CE_STREAM_ARGUMENT && !model.read.id);
    CHECK(kui_armada_ce_stream_read_plan(&model,4u,&rd)==KUI_ARMADA_CE_STREAM_OK);
    for(unsigned field=0;field<8u;field++) {
        forged=rd;((uint32_t *)(void *)&forged)[field]^=1u;
        CHECK(kui_armada_ce_stream_read_validate(&model,&forged)==KUI_ARMADA_CE_STREAM_STALE);
    }
    forged=rd;forged.bytes--;
    CHECK(kui_armada_ce_stream_read_complete(&model,&forged,true,forged.bytes)==KUI_ARMADA_CE_STREAM_STALE);
    CHECK(kui_armada_ce_stream_read_complete(&model,&rd,true,rd.bytes-4u)==KUI_ARMADA_CE_STREAM_ARGUMENT);
    CHECK(kui_armada_ce_stream_request(&model,33u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_read_complete(&model,&rd,true,rd.bytes)==KUI_ARMADA_CE_STREAM_STALE && !model.staged);
    for(unsigned sectors=1;sectors<=17u;sectors++) {
        play_frames(sectors,0u,false);play_frames(sectors,1u,false);
        play_frames(sectors,2u,false);play_frames(sectors,14u,false);
    }
    play_frames(1u,15u,true);play_frames(7u,15u,true);play_frames(151u,15u,true);
    /* Source padding is confined to final EOF. Every sector remainder and
     * every repeat above is checked byte-for-byte against an independent
     * generated sample sequence, across native ring boundaries. */
    struct kui_armada_ce_stream_track t={1u,0u,1000u,1200u};
    reset(&t,1u);CHECK(kui_armada_ce_stream_request(&model,20u,1u,1u,15u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_read_plan(&model,KUI_ARMADA_CE_STREAM_BATCH_SECTORS,&rd)==
        KUI_ARMADA_CE_STREAM_OK && rd.sectors==16u && rd.bytes==37632u);
    CHECK(kui_armada_ce_stream_request(&model,33u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_read_complete(&model,&rd,true,rd.bytes)==KUI_ARMADA_CE_STREAM_STALE);
    CHECK(kui_armada_ce_stream_request(&model,20u,1u,1u,15u)==KUI_ARMADA_CE_STREAM_OK);prime();
    uint32_t lease=model.ring_epoch;
    CHECK(kui_armada_ce_stream_observe(&model,lease,8192u,10240u,50u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_request(&model,22u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK && model.stop_needed);
    CHECK(kui_armada_ce_stream_stop_complete(&model,model.epoch,true)==KUI_ARMADA_CE_STREAM_OK && model.audible==8192u);
    CHECK(kui_armada_ce_stream_request(&model,23u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK && model.origin==8192u);
    staged=0;
    CHECK(kui_armada_ce_stream_read_plan(&model,4u,&rd)==KUI_ARMADA_CE_STREAM_OK && rd.lba==1003u && rd.skip==1136u);
    CHECK(kui_armada_ce_stream_read_complete(&model,&rd,true,rd.bytes)==KUI_ARMADA_CE_STREAM_OK);
    struct kui_armada_ce_stream_refill rf,bad_rf;
    CHECK(kui_armada_ce_stream_refill_plan(&model,4096u,&rf)==KUI_ARMADA_CE_STREAM_OK);
    for(unsigned field=0;field<7u;field++) {
        bad_rf=rf;((uint32_t *)(void *)&bad_rf)[field]^=1u;
        CHECK(kui_armada_ce_stream_refill_validate(&model,&bad_rf)==KUI_ARMADA_CE_STREAM_STALE);
    }
    bad_rf=rf;bad_rf.ring_offset+=64u;
    CHECK(kui_armada_ce_stream_refill_complete(&model,&bad_rf,true)==KUI_ARMADA_CE_STREAM_STALE);
    CHECK(kui_armada_ce_stream_request(&model,20u,1u,1u,0u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_refill_complete(&model,&rf,true)==KUI_ARMADA_CE_STREAM_STALE);
    staged=0;prime();lease=model.ring_epoch;
    CHECK(kui_armada_ce_stream_request(&model,20u,1u,1u,15u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_request(&model,22u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_stop_complete(&model,model.epoch,true)==KUI_ARMADA_CE_STREAM_OK &&
        model.state==KUI_ARMADA_CE_STREAM_PAUSED && model.repeats==15u && !model.audible);
    CHECK(kui_armada_ce_stream_request(&model,23u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK);staged=0;prime();lease=model.ring_epoch;
    CHECK(kui_armada_ce_stream_observe(&model,lease,4u,8u,1000u)==KUI_ARMADA_CE_STREAM_AGE && model.stop_needed);
    CHECK(kui_armada_ce_stream_stop_complete(&model,model.epoch,true)==KUI_ARMADA_CE_STREAM_OK &&
        model.state==KUI_ARMADA_CE_STREAM_PREFILL && !model.audible);
    staged=0;prime();lease=model.ring_epoch;
    uint32_t unchanged=model.epoch;uint64_t retained_fetch=model.fetch;
    CHECK(kui_armada_ce_stream_request(&model,23u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK &&
        model.epoch==unchanged && model.fetch==retained_fetch && model.native_running && !model.stop_needed);
    /* The native cursor is authoritative: a large catch-up inside fresh data
     * is consumption, not a fault, and never forces Stop/re-prime. */
    CHECK(kui_armada_ce_stream_observe(&model,lease,65536u,67584u,0u)==KUI_ARMADA_CE_STREAM_OK &&
        model.consumed==65536u && !model.stop_needed && model.native_running);
    CHECK(kui_armada_ce_stream_observe(&model,lease,R,0u,1u)==KUI_ARMADA_CE_STREAM_CURSOR);
    CHECK(kui_armada_ce_stream_stop_complete(&model,model.epoch,true)==KUI_ARMADA_CE_STREAM_OK);staged=0;prime();lease=model.ring_epoch;
    model.produced=model.consumed+4096u;
    uint64_t audible_before=model.audible;
    CHECK(kui_armada_ce_stream_observe(&model,lease,8192u,10240u,50u)==KUI_ARMADA_CE_STREAM_UNDERRUN &&
        model.audible==audible_before);
    CHECK(kui_armada_ce_stream_stop_complete(&model,model.epoch,true)==KUI_ARMADA_CE_STREAM_OK);
    staged=0;prime();lease=model.ring_epoch;
    uint32_t old_first=model.first_lba,old_end=model.end_lba;
    CHECK(kui_armada_ce_stream_request(&model,33u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK &&
        !model.selection && model.native_running && model.stop_needed);
    CHECK(model.first_lba==old_first && model.end_lba==old_end && model.ring_epoch==lease);
    CHECK(kui_armada_ce_stream_request(&model,23u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OK &&
        model.after_stop==KUI_ARMADA_CE_STREAM_STOPPED && !model.selection);
    CHECK(kui_armada_ce_stream_stop_complete(&model,model.epoch,true)==KUI_ARMADA_CE_STREAM_OK &&
        model.state==KUI_ARMADA_CE_STREAM_STOPPED && !model.native_running);
    CHECK(kui_armada_ce_stream_request(&model,20u,1u,1u,15u)==KUI_ARMADA_CE_STREAM_OK &&
        model.state==KUI_ARMADA_CE_STREAM_PREFILL && model.selection);
    model.epoch=UINT32_MAX;
    CHECK(kui_armada_ce_stream_request(&model,33u,0u,0u,0u)==KUI_ARMADA_CE_STREAM_OVERFLOW);
    reset(&t,1u);CHECK(kui_armada_ce_stream_request(&model,20u,1u,1u,0u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_read_plan(&model,4u,&rd)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_read_complete(&model,&rd,false,0u)==KUI_ARMADA_CE_STREAM_IO && model.state==KUI_ARMADA_CE_STREAM_FAULT);
    reset(&t,1u);CHECK(kui_armada_ce_stream_request(&model,20u,1u,1u,15u)==KUI_ARMADA_CE_STREAM_OK);
    prime();lease=model.ring_epoch;
    /* A cached native cursor catches up after several unchanged polls.
     * The pre-Play clock crosses32-bit wrap here; no elapsed time is lost. */
    uint32_t before_clock_recoveries=model.recoveries;
    model.last_ms=UINT32_MAX-23u;
    CHECK(kui_armada_ce_stream_observe(&model,lease,0u,11008u,0u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_observe(&model,lease,0u,11008u,16u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_observe(&model,lease,0u,11008u,32u)==KUI_ARMADA_CE_STREAM_OK);
    CHECK(kui_armada_ce_stream_observe(&model,lease,0x3c80u,0x6780u,64u)==KUI_ARMADA_CE_STREAM_OK &&
        model.consumed==0x3c80u && model.elapsed_ms==88u && model.recoveries==before_clock_recoveries);
    CHECK(kui_armada_ce_stream_observe(&model,lease,0xbc80u,0xe780u,84u)==KUI_ARMADA_CE_STREAM_OK &&
        model.consumed==0xbc80u && model.recoveries==before_clock_recoveries && !model.stop_needed);
    /* A jump past the fresh bytes is still an UNDERRUN recovery. */
    model.produced=model.consumed+4096u;
    CHECK(kui_armada_ce_stream_observe(&model,lease,0xdc80u,0xe780u,100u)==KUI_ARMADA_CE_STREAM_UNDERRUN &&
        model.stop_needed);
    CHECK(kui_armada_ce_stream_stop_complete(&model,model.epoch,true)==KUI_ARMADA_CE_STREAM_OK);staged=0;prime();lease=model.ring_epoch;
    printf("Armada CE full stream model: %u checks passed; exact byte sequences, finite/infinite repeats, raw carry, cursor/epoch/control guards.\n",checks);
    return 0;
}
