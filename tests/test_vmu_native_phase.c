/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production optional phase collector through a read-only bus.
 * Fake stack words retain their original virtual aliases; no target address is
 * dereferenced by this host test. */
#include "kui/vmu_native_profile.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/loader/vmu_native_phase.inc"

static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
#define COUNT(x) (sizeof(x)/sizeof((x)[0]))

static void image_identity(void) {
    /* SHA256 of the independently retained original executable, as recorded
     * by the external SDK replay fixture, rather than a hash computed from
     * an authored instruction transcript. */
    const uint8_t original[32]={
        0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
        0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
    CHECK(native_phase_image(2143652u,0x8c010000u,original));
    for(unsigned byte=0u;byte<sizeof(original);byte++) for(unsigned bit=0u;bit<8u;bit++) {
        uint8_t changed[32];memcpy(changed,original,sizeof(changed));
        changed[byte]^=(uint8_t)(1u<<bit);
        CHECK(!native_phase_image(2143652u,0x8c010000u,changed));
    }
    const uint32_t wrong_bytes[]={0u,2143651u,2143653u,UINT32_MAX};
    for(unsigned i=0u;i<COUNT(wrong_bytes);i++)
        CHECK(!native_phase_image(wrong_bytes[i],0x8c010000u,original));
    const uint32_t wrong_load[]={0u,0x0c010000u,0xac010000u,0x8c00fffeu,0x8c010002u,UINT32_MAX};
    for(unsigned i=0u;i<COUNT(wrong_load);i++)
        CHECK(!native_phase_image(2143652u,wrong_load[i],original));
    CHECK(!native_phase_image(2143652u,0x8c010000u,NULL));
    CHECK(!native_phase_image(0u,0u,NULL));
}

struct word {uint32_t address,value;};
struct bus {
    struct word words[32];
    unsigned count,reads,stack_reads;
    uint32_t addresses[64],frame;
    unsigned fail_read;
};
static void put(struct bus *b,uint32_t address,uint32_t value) {
    for(unsigned i=0;i<b->count;i++) if(b->words[i].address==address) {
        b->words[i].value=value;return;
    }
    CHECK(b->count<COUNT(b->words));
    b->words[b->count++]=(struct word){address,value};
}
static bool read_word(void *context,uint32_t address,unsigned width,uint32_t *value) {
    struct bus *b=context;
    CHECK(width==4u && value && b->reads<COUNT(b->addresses));
    b->addresses[b->reads++]=address;
    if((address&UINT32_C(0x1fffffff))>=UINT32_C(0x0c010000) &&
       (address&UINT32_C(0x1fffffff))<UINT32_C(0x0d000000)) ++b->stack_reads;
    if(b->fail_read && b->reads==b->fail_read) return false;
    for(unsigned i=0;i<b->count;i++) if(b->words[i].address==address) {
        *value=b->words[i].value;return true;
    }
    return false;
}
static struct kui_vmu_native_profile_sample clock_at(uint32_t counter) {
    return (struct kui_vmu_native_profile_sample){0xe0au,UINT32_MAX,2u,1u,counter};
}
static void video(struct bus *b,uint32_t scan,uint32_t counter) {
    put(b,UINT32_C(0xa05f80d8),UINT32_C(0x01060359));
    put(b,UINT32_C(0xa05f80cc),UINT32_C(0x00150104));
    put(b,UINT32_C(0xa05f80d0),UINT32_C(0x100));
    put(b,UINT32_C(0xa05f810c),scan);
    put(b,UINT32_C(0xff000028),UINT32_C(0x320));
    put(b,UINT32_C(0x8c358d18),counter);
}
static void fixture(struct bus *b,unsigned kind,uint32_t frame,bool shared) {
    memset(b,0,sizeof(*b));b->frame=frame;
    video(b,260u,17u);
    const uint32_t submit[][2]={{76u,0x8c1b1944u},{96u,0x8c1b0a1cu},
        {120u,0x8c1f1bacu},{128u,0x8c1f1ad8u},{200u,0u},
        {448u,0x8c1f362cu},{452u,0x8c1f362cu},{456u,0x60000000u}};
    const uint32_t complete[][2]={{88u,0x8c1b1500u},{120u,0x8c1b09f6u},
        {140u,0x8c1b0ad2u},{148u,0u},{388u,0x8c1f362cu},
        {392u,0x8c1f362cu},{396u,0x60000000u}};
    const uint32_t (*words)[2]=kind?complete:submit;
    unsigned count=kind?(unsigned)COUNT(complete):(unsigned)COUNT(submit);
    for(unsigned i=0;i<count;i++) put(b,frame+words[i][0],
        words[i][1]?words[i][1]:(shared?0x8c1da348u:0x8c1da1bcu));
}
static void begin(struct native_phase_state *s,struct bus *b,unsigned kind,
    uint32_t lease,uint32_t bytes,const struct kui_vmu_native_profile_sample *sample) {
    native_phase_begin(s,kind,b->frame,lease,bytes,sample,read_word,b);
}
static void finish(struct native_phase_state *s,struct bus *b,
    const struct kui_vmu_native_profile_sample *sample) {
    put(b,UINT32_C(0xa05f810c),7u);
    native_phase_end(s,sample,read_word,b);
}
static const struct native_phase_record *last(const struct native_phase_state *s) {
    CHECK(s->sequence && s->count);
    return &s->ring[(s->sequence-1u)&15u];
}

static void successful_unwinds(void) {
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    for(unsigned kind=0;kind<2u;kind++) for(unsigned alias=0;alias<COUNT(aliases);alias++)
        for(unsigned shared=0;shared<2u;shared++) {
            struct native_phase_state state;struct bus b;
            fixture(&b,kind,UINT32_C(0x0c2ff800)|aliases[alias],shared!=0u);
            struct kui_vmu_native_profile_sample first=clock_at(10000u),end=clock_at(9700u);
            native_phase_reset(&state,true);
            begin(&state,&b,kind,UINT32_C(0xac700000),UINT32_C(0x20000),&first);
            finish(&state,&b,&end);
            const struct native_phase_record *r=last(&state);
            CHECK(r->kind==kind && r->pc==UINT32_C(0x8c1f362c) &&
                r->pr==UINT32_C(0x8c1f362c) && r->sr==UINT32_C(0x60000000));
            CHECK(r->flags==(NATIVE_PHASE_CLOCK|NATIVE_PHASE_READ|NATIVE_PHASE_VIDEO|
                NATIVE_PHASE_UNWIND|NATIVE_PHASE_WAIT|NATIVE_PHASE_WRAP));
            CHECK(r->scan_begin==260u && r->scan_end==7u &&
                r->tick_begin==10000u && r->tick_end==9700u);
            CHECK(r->load==UINT32_C(0x01060359) && r->vblank==UINT32_C(0x00150104) &&
                r->control==UINT32_C(0x100) && r->event==UINT32_C(0x320) && r->video_counter==17u);
            CHECK(state.sequence==1u && state.count==1u && state.valid==1u &&
                state.waits==1u && state.wraps==1u);
            CHECK(b.stack_reads==(kind?7u:8u)+1u); /* Include the SDK video counter. */
            for(unsigned i=0;i<b.reads;i++) if((b.addresses[i]&UINT32_C(0x1fffffff))>=
                UINT32_C(0x0c2ff800) && (b.addresses[i]&UINT32_C(0x1fffffff))<
                UINT32_C(0x0c2ff800)+464u)
                CHECK((b.addresses[i]&UINT32_C(0xe0000000))==aliases[alias]);
        }
}

static unsigned owner_reads(const struct bus *b) {
    unsigned count=0u;
    for(unsigned i=0;i<b->reads;i++) {
        uint32_t at=b->addresses[i]&UINT32_C(0x1fffffff);
        if(at>=UINT32_C(0x0c010000) && at<UINT32_C(0x0d000000) &&
           b->addresses[i]!=UINT32_C(0x8c358d18)) ++count;
    }
    return count;
}
static void span_admission(void) {
    struct kui_vmu_native_profile_sample clock=clock_at(10000u);
    const uint32_t rejected[]={0u,1u,0x4c2ff800u,0x8c000000u,0x8c00fffcu,
        0x8c2ff801u,0x8d000000u,0xffffffffu};
    for(unsigned kind=0u;kind<2u;kind++) {
        unsigned span=kind?400u:460u;
        for(unsigned i=0u;i<COUNT(rejected);i++) {
            struct native_phase_state s;struct bus b;
            fixture(&b,kind,rejected[i],false);native_phase_reset(&s,true);
            begin(&s,&b,kind,0xac700000u,0x20000u,&clock);
            CHECK(!(s.pending.flags&NATIVE_PHASE_UNWIND));CHECK(!owner_reads(&b));
        }
        const uint32_t boundary[]={0x8c010000u,0x8d000000u-span};
        for(unsigned i=0u;i<COUNT(boundary);i++) {
            struct native_phase_state s;struct bus b;
            fixture(&b,kind,boundary[i],false);native_phase_reset(&s,true);
            begin(&s,&b,kind,0xac700000u,0x20000u,&clock);
            CHECK(s.pending.flags&NATIVE_PHASE_UNWIND);
        }
        struct native_phase_state s;struct bus b;
        fixture(&b,kind,0x8d000000u-span+4u,false);native_phase_reset(&s,true);
        begin(&s,&b,kind,0xac700000u,0x20000u,&clock);
        CHECK(!(s.pending.flags&NATIVE_PHASE_UNWIND));CHECK(!owner_reads(&b));
        /* Ownership exclusion compares physical spans across every admitted
         * alias, including either edge of the complete requested stack span. */
        const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
        for(unsigned a=0u;a<COUNT(aliases);a++) for(unsigned edge=0u;edge<2u;edge++) {
            uint32_t overlap=0x0c2ff800u+(edge?span-1u:0u);
            fixture(&b,kind,0x8c2ff800u,false);native_phase_reset(&s,true);
            begin(&s,&b,kind,overlap|aliases[a],1u,&clock);
            CHECK(!(s.pending.flags&NATIVE_PHASE_UNWIND));CHECK(!owner_reads(&b));
        }
        for(unsigned edge=0u;edge<2u;edge++) {
            uint32_t adjacent=edge?0x0c2ff800u+span:0x0c2ff7ffu;
            fixture(&b,kind,0x8c2ff800u,false);native_phase_reset(&s,true);
            begin(&s,&b,kind,adjacent,1u,&clock);
            CHECK(s.pending.flags&NATIVE_PHASE_UNWIND);
        }
        const struct {uint32_t at,bytes;} bad_lease[]={
            {0u,0x20000u},{0x0d000000u,1u},{0xac700000u,0u},{0x0cffffffu,2u}};
        for(unsigned i=0u;i<COUNT(bad_lease);i++) {
            fixture(&b,kind,0x8c2ff800u,false);native_phase_reset(&s,true);
            begin(&s,&b,kind,bad_lease[i].at,bad_lease[i].bytes,&clock);
            CHECK(!(s.pending.flags&NATIVE_PHASE_UNWIND));CHECK(!owner_reads(&b));
        }
    }
}

static void marker_and_read_faults(void) {
    struct kui_vmu_native_profile_sample first=clock_at(10000u),end=clock_at(9700u);
    const unsigned submit[]={76u,96u,120u,128u,200u};
    const unsigned complete[]={88u,120u,140u,148u};
    for(unsigned kind=0u;kind<2u;kind++) {
        const unsigned *markers=kind?complete:submit;
        unsigned count=kind?(unsigned)COUNT(complete):(unsigned)COUNT(submit);
        for(unsigned shared=0u;shared<2u;shared++) for(unsigned i=0u;i<count;i++)
            for(unsigned bit=0u;bit<32u;bit++) {
                struct native_phase_state s;struct bus b;
                fixture(&b,kind,0x8c2ff800u,shared!=0u);
                for(unsigned at=0u;at<b.count;at++) if(b.words[at].address==b.frame+markers[i])
                    b.words[at].value^=UINT32_C(1)<<bit;
                native_phase_reset(&s,true);begin(&s,&b,kind,0xac700000u,0x20000u,&first);
                finish(&s,&b,&end);
                const struct native_phase_record *r=last(&s);
                CHECK(!(r->flags&(NATIVE_PHASE_UNWIND|NATIVE_PHASE_WAIT)));
                CHECK(s.valid==1u && !s.waits && s.wraps==1u);
            }
        /* The two submit callers are explicitly admitted, while a completion
         * reached from another SDK caller must remain an unclassified sample. */
        struct native_phase_state s;struct bus b;
        fixture(&b,kind,0x8c2ff800u,false);
        put(&b,b.frame+(kind?88u:76u),kind?0x8c1b193eu:0x8c1b19b0u);
        native_phase_reset(&s,true);begin(&s,&b,kind,0xac700000u,0x20000u,&first);
        CHECK(!!(s.pending.flags&NATIVE_PHASE_UNWIND)==!kind);

        /* Fail each actual read independently. Control reads and the fixed
         * stack walk fail softly; no failure becomes a VMU rejection. */
        fixture(&b,kind,0x8c2ff800u,false);native_phase_reset(&s,true);
        begin(&s,&b,kind,0xac700000u,0x20000u,&first);
        unsigned entry_reads=b.reads;
        CHECK(entry_reads==6u+(kind?7u:8u));
        for(unsigned fail=1u;fail<=entry_reads+4u;fail++) {
            fixture(&b,kind,0x8c2ff800u,false);b.fail_read=fail;native_phase_reset(&s,true);
            begin(&s,&b,kind,0xac700000u,0x20000u,&first);finish(&s,&b,&end);
            const struct native_phase_record *r=last(&s);
            CHECK(s.sequence==1u && s.count==1u);
            if(fail<=6u || fail>entry_reads) {
                CHECK(!(r->flags&NATIVE_PHASE_READ));CHECK(!s.valid && !s.waits && !s.wraps);
            } else {
                CHECK(!(r->flags&(NATIVE_PHASE_UNWIND|NATIVE_PHASE_WAIT)));
                CHECK(s.valid==1u && !s.waits);
            }
        }
    }
}

static void wait_classification(void) {
    struct kui_vmu_native_profile_sample clock=clock_at(10000u);
    for(unsigned kind=0u;kind<2u;kind++) {
        unsigned pc_offset=kind?392u:452u,pr_offset=kind?388u:448u;
        for(uint32_t pc=0x8c1f3626u;pc<=0x8c1f3630u;pc++) {
            struct native_phase_state s;struct bus b;
            fixture(&b,kind,0x8c2ff800u,false);put(&b,b.frame+pc_offset,pc);
            native_phase_reset(&s,true);begin(&s,&b,kind,0xac700000u,0x20000u,&clock);
            bool expected=!(pc&1u) && pc>=0x8c1f3628u && pc<=0x8c1f362eu;
            CHECK(s.pending.flags&NATIVE_PHASE_UNWIND);
            CHECK(!!(s.pending.flags&NATIVE_PHASE_WAIT)==expected);
        }
        for(uint32_t pc=0x8c1f1f34u;pc<=0x8c1f1f3cu;pc++) for(unsigned correct=0u;correct<2u;correct++) {
            struct native_phase_state s;struct bus b;
            fixture(&b,kind,0x8c2ff800u,false);put(&b,b.frame+pc_offset,pc);
            put(&b,b.frame+pr_offset,correct?0x8c1f362cu:0x8c1f3624u);
            native_phase_reset(&s,true);begin(&s,&b,kind,0xac700000u,0x20000u,&clock);
            bool expected=correct && !(pc&1u) && pc>=0x8c1f1f36u && pc<=0x8c1f1f3au;
            CHECK(s.pending.flags&NATIVE_PHASE_UNWIND);
            CHECK(!!(s.pending.flags&NATIVE_PHASE_WAIT)==expected);
        }
    }
}

static void clocks_and_video(void) {
    struct kui_vmu_native_profile_sample first=clock_at(10000u),end=clock_at(9700u);
    for(unsigned endpoint=0u;endpoint<2u;endpoint++) for(unsigned field=0u;field<4u;field++) {
        struct native_phase_state s;struct bus b;
        struct kui_vmu_native_profile_sample start=first,stop=end;
        uint32_t words[5];memcpy(words,endpoint?&stop:&start,sizeof(words));
        words[field]=0u;memcpy(endpoint?&stop:&start,words,sizeof(words));
        CHECK(!kui_vmu_native_profile_sample_valid(endpoint?&stop:&start));
        fixture(&b,0u,0x8c2ff800u,false);native_phase_reset(&s,true);
        begin(&s,&b,0u,0xac700000u,0x20000u,&start);finish(&s,&b,&stop);
        const struct native_phase_record *r=last(&s);
        CHECK(!(r->flags&(NATIVE_PHASE_CLOCK|NATIVE_PHASE_WRAP)));
        CHECK(r->flags&NATIVE_PHASE_WAIT);CHECK(!s.valid && !s.waits && !s.wraps);
    }
    const uint32_t registers[]={0xa05f80d8u,0xa05f80ccu,0xa05f80d0u};
    for(unsigned i=0u;i<COUNT(registers);i++) {
        struct native_phase_state s;struct bus b;
        fixture(&b,0u,0x8c2ff800u,false);native_phase_reset(&s,true);
        begin(&s,&b,0u,0xac700000u,0x20000u,&first);
        for(unsigned at=0u;at<b.count;at++) if(b.words[at].address==registers[i]) b.words[at].value^=1u;
        finish(&s,&b,&end);const struct native_phase_record *r=last(&s);
        CHECK(!(r->flags&(NATIVE_PHASE_VIDEO|NATIVE_PHASE_WRAP)));
        CHECK(r->flags&NATIVE_PHASE_READ);CHECK(!s.valid && !s.waits && !s.wraps);
    }
    /* Endpoint subtraction admits natural down-counter wrap but rejects a
     * backwards/reset-sized interval. Zero end-line is deliberately excluded
     * from the stronger whole-line-zero coverage classification. */
    const struct {uint32_t before,after,scan;bool clock,wrap;} cases[]={
        {3u,UINT32_MAX-8u,7u,true,true},{10000u,10001u,7u,false,false},
        {10000u,9700u,0u,true,false},{10000u,9700u,260u,true,false},
        {10000u,9700u,261u,true,false},{10000u,9700u,0x407u,true,true}};
    for(unsigned i=0u;i<COUNT(cases);i++) {
        struct native_phase_state s;struct bus b;
        first=clock_at(cases[i].before);end=clock_at(cases[i].after);
        fixture(&b,0u,0x8c2ff800u,false);native_phase_reset(&s,true);
        begin(&s,&b,0u,0xac700000u,0x20000u,&first);
        put(&b,0xa05f810cu,cases[i].scan);native_phase_end(&s,&end,read_word,&b);
        const struct native_phase_record *r=last(&s);
        CHECK(!!(r->flags&NATIVE_PHASE_CLOCK)==cases[i].clock);
        CHECK(!!(r->flags&NATIVE_PHASE_WRAP)==cases[i].wrap);
    }
    struct native_phase_state s;struct bus b;
    fixture(&b,0u,0x8c2ff800u,false);native_phase_reset(&s,true);
    begin(&s,&b,0u,0xac700000u,0x20000u,NULL);finish(&s,&b,NULL);
    CHECK(!(last(&s)->flags&NATIVE_PHASE_CLOCK) && !s.valid);
}

static void disabled_and_ring(void) {
    struct {uint32_t before;struct native_phase_state phase;uint32_t after;} guarded;
    struct bus b;struct kui_vmu_native_profile_sample first=clock_at(10000u),end=clock_at(9700u);
    guarded.before=0x12345678u;guarded.after=0xabcdef01u;
    native_phase_reset(&guarded.phase,false);fixture(&b,0u,0x8c2ff800u,false);
    begin(&guarded.phase,&b,0u,0xac700000u,0x20000u,&first);finish(&guarded.phase,&b,&end);
    CHECK(!b.reads && !guarded.phase.sequence && !guarded.phase.count);
    CHECK(guarded.before==0x12345678u && guarded.after==0xabcdef01u);
    native_phase_reset(&guarded.phase,true);
    for(unsigned i=0u;i<65u;i++) {
        fixture(&b,i&1u,0x8c2ff800u,(i&2u)!=0u);put(&b,0x8c358d18u,100u+i);
        begin(&guarded.phase,&b,i&1u,0xac700000u,0x20000u,&first);finish(&guarded.phase,&b,&end);
        CHECK(guarded.phase.sequence==i+1u && guarded.phase.count==(i<16u?i+1u:16u));
        CHECK(last(&guarded.phase)->video_counter==100u+i);
        CHECK(guarded.before==0x12345678u && guarded.after==0xabcdef01u);
    }
    CHECK(guarded.phase.valid==65u && guarded.phase.waits==65u && guarded.phase.wraps==65u);
    for(unsigned i=49u;i<65u;i++) CHECK(guarded.phase.ring[i&15u].video_counter==100u+i);
    /* Sequence arithmetic and diagnostic counters cannot escape the ring or
     * wrap cumulative counts back to misleadingly small values. */
    guarded.phase.sequence=UINT32_MAX;
    guarded.phase.valid=guarded.phase.waits=guarded.phase.wraps=UINT32_MAX;
    fixture(&b,0u,0x8c2ff800u,false);begin(&guarded.phase,&b,0u,0xac700000u,0x20000u,&first);
    finish(&guarded.phase,&b,&end);
    CHECK(!guarded.phase.sequence && guarded.phase.count==16u);
    CHECK(guarded.phase.ring[15].flags&NATIVE_PHASE_WRAP);
    CHECK(guarded.phase.valid==UINT32_MAX && guarded.phase.waits==UINT32_MAX &&
        guarded.phase.wraps==UINT32_MAX);
    CHECK(guarded.before==0x12345678u && guarded.after==0xabcdef01u);
}

int main(void) {
    image_identity();successful_unwinds();span_admission();marker_and_read_faults();wait_classification();
    clocks_and_video();disabled_and_ring();
    printf("Native optional phase capture: %u checks; original-alias unwind, bounds/refusals, clock/video identity and bounded history passed\n",checks);
    return 0;
}
