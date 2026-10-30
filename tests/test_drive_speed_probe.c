/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/drive_speed_probe.h"
#include "kui/recovery_checks.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Independent polynomial-division encoder. The production checker instead
 * uses a syndrome recurrence and inverse tables. No game bytes are used. */
static uint8_t multiply(uint8_t a,uint8_t b) {
    unsigned product=0,x=a;
    while(b) {if(b&1u)product^=x;b>>=1;x<<=1;if(x&256u)x^=0x11du;}
    return (uint8_t)product;
}
static void parity(uint8_t *raw,unsigned major,unsigned minor,unsigned multiplier,
    unsigned increment,unsigned offset) {
    for(unsigned word=0;word<major;++word) {
        unsigned at=(word/2u)*multiplier+(word&1u);uint8_t left=0,right=0;
        for(unsigned n=0;n<minor;++n) {
            uint8_t factor=raw[12u+at]^left;
            left=right^multiply(factor,3);right=multiply(factor,2);
            at=(at+increment)%(major*minor);
        }
        raw[offset+word]=left;raw[offset+word+major]=right;
    }
}
static uint8_t bcd(unsigned value) {return (uint8_t)((value/10u)*16u+value%10u);}
static void encode(uint8_t *raw,uint32_t fad,bool changed) {
    memset(raw,0,KUI_RAW_BYTES);memset(raw+1,255,10);
    raw[12]=bcd(fad/4500u);raw[13]=bcd(fad/75u%60u);raw[14]=bcd(fad%75u);raw[15]=1;
    for(unsigned n=0;n<2048u;++n)raw[16+n]=(uint8_t)(n*37u+fad+(changed?11u:0u));
    uint32_t edc=0;
    for(unsigned n=0;n<2064u;++n) {
        edc^=raw[n];for(unsigned bit=0;bit<8;++bit)edc=(edc>>1)^((edc&1u)?UINT32_C(0xd8018001):0);
    }
    for(unsigned n=0;n<4;++n)raw[2064+n]=(uint8_t)(edc>>(8*n));
    parity(raw,86,24,2,86,2076);parity(raw,52,43,86,88,2248);
    assert(!kui_recovery_sector_check(raw,KUI_RAW_BYTES,fad));
}
static uint8_t sector_cache[1024][KUI_RAW_BYTES];
static uint32_t cached_fad[1024];

static bool recording_allocation,allocation_fail;
static void *owned;
static size_t allocation_size;
static unsigned allocations,frees;
void *__real_malloc(size_t);
void __real_free(void *);
void *__wrap_malloc(size_t bytes) {
    if(!recording_allocation)return __real_malloc(bytes);
    assert(!owned && bytes<=75u*1024u);++allocations;allocation_size=bytes;
    if(allocation_fail)return NULL;
    owned=__real_malloc(bytes);assert(owned);return owned;
}
void __wrap_free(void *pointer) {
    if(recording_allocation && pointer) {assert(pointer==owned);owned=NULL;++frees;}
    __real_free(pointer);
}

struct backend {
    uint32_t first,end;
    uint64_t time,read_time,select_time;
    unsigned selection,selects,reads,progress,logs;
    unsigned rejected,fail_select,stop_select,stop_read,fatal_read;
    unsigned unreadable_selection,invalid_selection,changed_selection;
    unsigned unreadable_references;
    bool stop,stop_before,cancel_progress,partial;
    unsigned selected[KUI_SPEED_PROBE_PROFILES+1u];
    unsigned read_selections[KUI_SPEED_PROBE_PROFILES];
};
static struct kui_capture_plan plan(void) {
    struct kui_capture_plan p={.count=2};
    p.tracks[0]=(struct kui_capture_track){1,4,0,150,190,190};
    p.tracks[1]=(struct kui_capture_track){2,4,1,45150,45662,45662};
    p.bytes=(40u+512u)*KUI_RAW_BYTES;return p;
}
static void log_line(const char *format,...) {(void)format;}
static bool cancelled(void *ctx) {struct backend *b=ctx;return b->stop || b->stop_before;}
static uint64_t now(void *ctx) {return ((struct backend *)ctx)->time;}
static int select_mode(void *ctx,unsigned selection) {
    struct backend *b=ctx;
    assert(!b->stop && !b->stop_before && selection<KUI_SPEED_PROBE_PROFILES);
    assert(b->selects<KUI_SPEED_PROBE_PROFILES+1u);b->selected[b->selects++]=selection;
    b->time+=b->select_time;
    if(b->selects==b->stop_select)b->stop=true;
    if(b->selects==b->fail_select)return -1;
    if((b->rejected&(1u<<selection)) || (b->selects==2 && (b->rejected&512u)))return 0;
    b->selection=selection;return 1;
}
static enum kui_read_result read_data(void *ctx,uint32_t fad,unsigned count,uint8_t *raw) {
    struct backend *b=ctx;
    assert(!b->stop && !b->stop_before && count==32u && !((uintptr_t)raw&31u));
    assert(fad>=b->first && (uint64_t)fad+count<=b->end);
    ++b->reads;++b->read_selections[b->selection];b->time+=b->read_time;
    if(b->reads==b->stop_read)b->stop=true;
    if(b->reads==b->fatal_read)return KUI_READ_FATAL;
    if((b->selects==1 && b->unreadable_references && fad==b->first) ||
       (b->selects==1 && b->unreadable_references==6u) ||
       (b->selects>1 && b->selection==b->unreadable_selection))return KUI_READ_RETRY;
    bool changed=b->selects>1 && b->selection==b->changed_selection;
    for(unsigned n=0;n<count;++n) {
        unsigned slot=(fad+n)%1024u;
        if(changed)encode(raw+n*KUI_RAW_BYTES,fad+n,true);
        else {
            if(cached_fad[slot]!=fad+n) {encode(sector_cache[slot],fad+n,false);cached_fad[slot]=fad+n;}
            memcpy(raw+n*KUI_RAW_BYTES,sector_cache[slot],KUI_RAW_BYTES);
        }
    }
    if(b->selects>1 && b->selection==b->invalid_selection)raw[12]^=1;
    return b->partial?KUI_READ_PARTIAL:KUI_READ_OK;
}
static void progress(void *ctx,const struct kui_drive_speed_probe_status *s) {
    struct backend *b=ctx;++b->progress;
    assert(s->selection<KUI_SPEED_PROBE_PROFILES && s->references<=3 && s->done<=s->total && s->total==9);
    if(b->cancel_progress && !b->reads)b->stop=true;
}
static struct backend backend(void) {
    return (struct backend){.first=45150,.end=45662,.read_time=5,.select_time=2,
        .unreadable_selection=99,.invalid_selection=99,.changed_selection=99};
}
static bool run(struct kui_capture_plan *p,struct backend *b,struct kui_drive_speed_probe_status *s) {
    struct kui_capture_ops ops={.ctx=b,.read=read_data,.cancelled=cancelled,.now_ms=now,.log=log_line};
    allocations=frees=0;allocation_size=0;recording_allocation=true;
    bool result=kui_drive_speed_probe_run(p,&ops,select_mode,progress,s);
    recording_allocation=false;assert(!owned);
    assert(allocations<=1 && (!allocations || allocation_fail || frees==1));
    return result;
}
static void complete_cases(void) {
    struct kui_capture_plan p=plan();struct kui_drive_speed_probe_status s;
    struct backend b=backend();assert(run(&p,&b,&s));
    assert(s.complete && !s.failed && !s.stopped && !s.budget_expired && s.done==9 && s.references==3);
    assert(b.selects==10 && b.reads==60 && allocations==1 && allocation_size<=75u*1024u);
    for(unsigned i=0;i<9;++i) {
        assert(s.tried[i] && s.verified[i] && s.matched[i]==3 && s.attempted[i]==3 && s.read_ms[i]==30);
        assert(b.selected[i+1]==i && b.read_selections[i]==(i?6u:12u));
    }
    b=backend();b.partial=true;assert(run(&p,&b,&s) && s.complete);
    b=backend();b.rejected=(1u<<2)|(1u<<4)|(1u<<8);assert(run(&p,&b,&s));
    for(unsigned i=0;i<9;++i)if(b.rejected&(1u<<i))
        assert(s.tried[i] && !s.verified[i] && !s.attempted[i] && !b.read_selections[i]);
    b=backend();b.unreadable_selection=3;assert(run(&p,&b,&s));
    assert(s.verified[3] && s.attempted[3]==3 && s.matched[3]==0 && s.matched[4]==3);
    b=backend();b.invalid_selection=3;assert(run(&p,&b,&s) && s.matched[3]==0);
    b=backend();b.unreadable_references=1;assert(run(&p,&b,&s) && s.references==3 && b.reads==61);
}
static void failures(void) {
    struct kui_capture_plan p=plan();struct kui_drive_speed_probe_status s;struct backend b;
    b=backend();b.rejected=1;assert(!run(&p,&b,&s) && s.failed && !b.reads && b.selects==1);
    b=backend();b.rejected=512;assert(!run(&p,&b,&s) && s.failed && b.reads==6 && b.selects==2);
    b=backend();b.fail_select=4;assert(!run(&p,&b,&s) && s.failed && s.done==2 && !s.attempted[2]);
    b=backend();b.fatal_read=1;assert(!run(&p,&b,&s) && s.failed && b.reads==1 && b.selects==1);
    b=backend();b.fatal_read=13;assert(!run(&p,&b,&s) && s.failed && b.reads==13 && b.selects==3);
    b=backend();b.unreadable_references=6;assert(!run(&p,&b,&s) && s.failed && !s.references && b.reads==6 && b.selects==1);
    b=backend();b.unreadable_selection=0;assert(!run(&p,&b,&s) && s.failed && b.selects==2 && !s.done);
    b=backend();b.changed_selection=2;assert(!run(&p,&b,&s) && s.failed && s.done==2 && strstr(s.message,"bytes changed"));
    allocation_fail=true;b=backend();assert(!run(&p,&b,&s) && s.failed && !b.selects && !b.reads);allocation_fail=false;
}
static void stopping(void) {
    struct kui_capture_plan p=plan();struct kui_drive_speed_probe_status s;struct backend b;
    b=backend();b.stop_before=true;assert(!run(&p,&b,&s) && s.stopped && !s.failed && !b.selects && !b.reads);
    b=backend();b.cancel_progress=true;assert(!run(&p,&b,&s) && s.stopped && !b.selects && !b.reads);
    for(unsigned which=1;which<=4;++which) {
        b=backend();b.stop_select=which;assert(!run(&p,&b,&s) && s.stopped && !s.complete && b.selects==which);
    }
    for(unsigned which=1;which<=14;++which) {
        b=backend();b.stop_read=which;assert(!run(&p,&b,&s) && s.stopped && !s.complete && b.reads==which);
    }
    b=backend();b.select_time=KUI_SPEED_PROBE_BUDGET_MS;
    assert(!run(&p,&b,&s) && s.budget_expired && !s.failed && !b.reads && b.selects==1);
    b=backend();b.read_time=KUI_SPEED_PROBE_BUDGET_MS;
    assert(!run(&p,&b,&s) && s.budget_expired && !s.failed && b.reads==1);
    b=backend();b.read_time=10000;
    assert(!run(&p,&b,&s) && s.budget_expired && !s.failed && b.reads==30 && s.done<9);
}
static void bounds(void) {
    struct kui_capture_plan p=plan();struct kui_drive_speed_probe_status s;struct backend b=backend();
    p.tracks[1].end=p.tracks[1].toc_end=p.tracks[1].start+192;p.bytes=(40u+192u)*KUI_RAW_BYTES;b.end=p.tracks[1].end;
    assert(run(&p,&b,&s) && s.complete);
    p=plan();p.count=3;p.tracks[1].end=p.tracks[1].toc_end=45342;
    p.tracks[2]=(struct kui_capture_track){3,4,1,45342,45854,45854};p.bytes=(40u+192u+512u)*KUI_RAW_BYTES;
    b=backend();b.first=45342;b.end=45854;assert(run(&p,&b,&s));
    p=plan();p.tracks[1].start=719488;p.tracks[1].end=p.tracks[1].toc_end=720000;
    b=backend();b.first=719488;b.end=720000;assert(run(&p,&b,&s));
    for(unsigned which=0;which<8;++which) {
        p=plan();b=backend();
        switch(which) {
        case 0:p.profile=KUI_CAPTURE_PROFILE_CD_TOC;break;
        case 1:p.count=100;break;
        case 2:p.tracks[1].session=0;break;
        case 3:p.tracks[1].control=0;break;
        case 4:p.tracks[1].end=p.tracks[1].start+191;break;
        case 5:p.tracks[1].toc_end=720001;break;
        case 6:p.tracks[1].number=3;break;
        default:--p.bytes;break;
        }
        assert(!run(&p,&b,&s) && s.failed && !b.reads && !b.selects && !allocations);
    }
    struct kui_capture_ops ops={.ctx=&b,.read=read_data,.cancelled=cancelled,.now_ms=now,.log=log_line};
    p=plan();assert(!kui_drive_speed_probe_run(&p,&ops,select_mode,progress,NULL));
    ops.read=NULL;assert(!kui_drive_speed_probe_run(&p,&ops,select_mode,progress,&s) && s.failed);
}
int main(void) {
    complete_cases();failures();stopping();bounds();
    puts("PASS read-only drive speed probe: all profiles, strict references, bounds, failures, Stop and budget");
    return 0;
}
