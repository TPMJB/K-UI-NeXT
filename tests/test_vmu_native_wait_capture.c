/* SPDX-License-Identifier: GPL-3.0-only */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/loader/vmu_native_phase.inc"
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
struct cell {uint32_t at,value;};
struct bus {struct cell cell[32];unsigned cells,reads,fail;uint32_t frame;};
static void put(struct bus *b,uint32_t at,uint32_t value) {
    for(unsigned i=0;i<b->cells;i++) if(b->cell[i].at==at) {b->cell[i].value=value;return;}
    CHECK(b->cells<COUNT(b->cell));b->cell[b->cells++]=(struct cell){at,value};
}
static bool read_word(void *context,uint32_t at,unsigned width,uint32_t *value) {
    struct bus *b=context;CHECK(width==4u);++b->reads;
    if(b->reads==b->fail) return false;
    for(unsigned i=0;i<b->cells;i++) if(b->cell[i].at==at) {*value=b->cell[i].value;return true;}
    return false;
}
static void fixture(struct bus *b,uint32_t frame,bool shared) {
    memset(b,0,sizeof(*b));b->frame=frame;
    const uint32_t words[][2]={
        {88u,0x8c1b1500u},{120u,0x8c1b09f6u},{140u,0x8c1b0ad2u},
        {148u,shared?0x8c1da348u:0x8c1da1bcu},
        {308u,0x700000f0u},{312u,0x3ffu},{328u,0x10cu},
        {360u,0x8c1f1f36u},{364u,0x3ffu},{368u,0x10cu},{380u,0x8c00f400u},
        {388u,0x8c1f362cu},{392u,0x8c1f362eu},{396u,0x60000000u}};
    for(unsigned i=0;i<COUNT(words);i++) put(b,frame+words[i][0],words[i][1]);
    put(b,0xa05f810cu,523u);put(b,0xffd8000cu,123456u);put(b,0x8c358d18u,77u);
}
static bool add(struct native_wait_capture *c,struct bus *b) {
    b->reads=0u;return native_wait_capture_add(c,b->frame,0xac700000u,0x20000u,read_word,b);
}
static void all_aliases_and_layout(void) {
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    CHECK(sizeof(struct native_wait_record)==68u && sizeof(struct native_phase_state)==908u);
    CHECK(sizeof(struct native_wait_capture)<=884u);
    for(unsigned a=0;a<COUNT(aliases);a++) for(unsigned shared=0;shared<2u;shared++) {
        struct {uint32_t before;struct native_phase_state phase;uint32_t after;} guarded;
        memset(&guarded,0xa7,sizeof(guarded));
        struct native_wait_capture *c=&guarded.phase.wait;native_wait_capture_reset(c);
        for(unsigned i=0;i<COUNT(c->snapshot);i++) c->snapshot[i]=0x12340000u+i;
        struct bus b;fixture(&b,0x0c2ff800u|aliases[a],shared!=0u);
        for(unsigned i=0;i<8u;i++) {CHECK(add(c,&b)==(i==7u));CHECK(b.reads==19u);}
        CHECK(c->ready==1u && c->valid==8u && c->attempts==8u && c->waits==8u);
        CHECK(c->operand_match==8u && !c->operand_bad && !c->bank_bad);
        CHECK(c->t_checked==8u && c->t_match==8u && !c->t_bad);
        CHECK(c->first_wait==0u && c->first_bad==UINT32_MAX);
        CHECK(c->record[0].frame==b.frame && c->record[7].vbr==0x8c00f400u);
        CHECK(c->record[7].flags==127u && c->record[0].r0==0x3ffu);
        CHECK(c->record[0].r4==0x10cu && c->record[0].r12==0x8c1f1f36u &&
            c->record[0].r13==0x3ffu && c->record[0].r14==0x10cu);
        CHECK(c->record[0].scan_begin==523u && c->record[0].scan_end==523u &&
            c->record[0].tick_begin==123456u && c->record[0].tick_end==123456u &&
            c->record[0].video_counter==77u);
        b.reads=0u;CHECK(native_wait_capture_add(c,b.frame,0u,0u,NULL,NULL));CHECK(!b.reads);
        CHECK(c->attempts==8u);
        for(unsigned i=0;i<COUNT(c->snapshot);i++) CHECK(c->snapshot[i]==0x12340000u+i);
        CHECK(guarded.before==0xa7a7a7a7u && guarded.after==0xa7a7a7a7u);
    }
}
static void spans_and_refusals(void) {
    const uint32_t rejected[]={0u,1u,0x4c2ff800u,0x8c00fffcu,0x8c2ff801u,0x8d000000u,0xfffffffcu};
    for(unsigned i=0;i<COUNT(rejected);i++) {
        struct native_wait_capture c;struct bus b;fixture(&b,rejected[i],false);
        native_wait_capture_reset(&c);CHECK(!add(&c,&b));
        CHECK(c.unwind_fail==1u && !c.valid && !b.reads);
    }
    for(unsigned shared=0;shared<2u;shared++) for(unsigned i=0;i<4u;i++) {
        const unsigned offsets[]={88u,120u,140u,148u};
        for(unsigned bit=0;bit<32u;bit++) {
            struct native_wait_capture c;struct bus b;fixture(&b,0x8c2ff800u,shared!=0u);
            for(unsigned j=0;j<b.cells;j++) if(b.cell[j].at==b.frame+offsets[i]) b.cell[j].value^=1u<<bit;
            native_wait_capture_reset(&c);CHECK(!add(&c,&b));CHECK(c.unwind_fail==1u && !c.valid);
        }
    }
    for(unsigned fail=1;fail<=19u;fail++) {
        struct native_wait_capture c;struct bus b;fixture(&b,0x8c2ff800u,false);b.fail=fail;
        native_wait_capture_reset(&c);CHECK(!add(&c,&b));
        CHECK(!c.valid && c.first_bad==UINT32_MAX && c.first_wait==UINT32_MAX);
        CHECK((fail<=7u?c.unwind_fail:c.read_fail)==1u);
    }
    for(unsigned edge=0;edge<2u;edge++) {
        struct native_wait_capture c;struct bus b;fixture(&b,0x8c2ff800u,false);native_wait_capture_reset(&c);
        CHECK(!native_wait_capture_add(&c,b.frame,(b.frame&0x1fffffffu)+(edge?399u:0u),1u,read_word,&b));
        CHECK(c.unwind_fail==1u && !b.reads);
    }
    struct native_wait_capture c;native_wait_capture_reset(&c);
    for(unsigned i=0;i<120u;i++) CHECK(native_wait_capture_add(&c,0u,0u,0u,NULL,NULL)==(i==119u));
    CHECK(c.ready==2u && c.attempts==120u && c.read_fail==120u && !c.valid);
    native_wait_capture_reset(&c);struct bus b;fixture(&b,0u,false);
    for(unsigned i=0;i<120u;i++) CHECK(add(&c,&b)==(i==119u));
    CHECK(c.ready==2u && c.unwind_fail==120u && !c.valid);
}
static void banks_and_operands(void) {
    const unsigned offsets[]={396u,308u};
    const uint32_t banks[]={0u,0x20000000u,0x40000000u};
    for(unsigned i=0;i<COUNT(offsets);i++) for(unsigned j=0;j<COUNT(banks);j++) {
        struct native_wait_capture c;struct bus b;fixture(&b,0x8c2ff800u,false);
        put(&b,b.frame+offsets[i],banks[j]);native_wait_capture_reset(&c);CHECK(!add(&c,&b));
        CHECK(c.valid==1u && c.bank_bad==1u && c.first_bad==0u && c.waits==1u);
        CHECK(!(c.record[0].flags&(NATIVE_WAIT_BANK_VALID|NATIVE_WAIT_OPERANDS_CHECKED|NATIVE_WAIT_OPERANDS_MATCH)));
        CHECK(!c.operand_match && !c.operand_bad);
        CHECK(!c.t_checked && !c.t_match && !c.t_bad &&
            !(c.record[0].flags&(NATIVE_WAIT_T_CHECKED|NATIVE_WAIT_T_MATCH)));
    }
    const unsigned operands[]={328u,360u,364u,368u};
    for(unsigned i=0;i<COUNT(operands);i++) {
        struct native_wait_capture c;struct bus b;fixture(&b,0x8c2ff800u,false);
        put(&b,b.frame+operands[i],UINT32_MAX);native_wait_capture_reset(&c);CHECK(!add(&c,&b));
        CHECK(c.valid==1u && c.operand_bad==1u && !c.operand_match && c.first_bad==0u);
        CHECK(c.record[0].flags&NATIVE_WAIT_OPERANDS_CHECKED);
    }
}
static void pc_and_t_rules(void) {
    const uint32_t pcs[]={0x8c1f3628u,0x8c1f362au,0x8c1f362cu,0x8c1f362eu,
        0x8c1f1f36u,0x8c1f1f38u,0x8c1f1f3au,0x8c1f3630u,0x8c1f362du,0x8c123456u};
    for(unsigned i=0;i<COUNT(pcs);i++) for(unsigned wrong_pr=0;wrong_pr<2u;wrong_pr++) {
        struct native_wait_capture c;struct bus b;fixture(&b,0x8c2ff800u,false);
        put(&b,b.frame+392u,pcs[i]);if(wrong_pr) put(&b,b.frame+388u,0x8c1f3624u);
        native_wait_capture_reset(&c);CHECK(!add(&c,&b));CHECK(c.valid==1u);
        bool wait=i<4u || (i<7u && !wrong_pr);
        CHECK(!!(c.record[0].flags&NATIVE_WAIT_PC)==wait);
        CHECK(!!(c.record[0].flags&NATIVE_WAIT_OPERANDS_CHECKED)==wait);
        CHECK(!!(c.record[0].flags&NATIVE_WAIT_T_CHECKED)==(i==3u));
        CHECK(c.first_wait==(wait?0u:UINT32_MAX));
    }
    for(unsigned zero=0;zero<2u;zero++) for(unsigned correct=0;correct<2u;correct++) {
        struct native_wait_capture c;struct bus b;fixture(&b,0x8c2ff800u,false);
        put(&b,b.frame+312u,zero?0x400u:0x3ffu);
        put(&b,b.frame+396u,0x60000000u|(zero^!correct));
        native_wait_capture_reset(&c);CHECK(!add(&c,&b));
        CHECK(c.t_checked==1u && c.t_match==correct && c.t_bad==!correct);
        CHECK(!!(c.record[0].flags&NATIVE_WAIT_T_MATCH)==!!correct);
    }
    struct native_wait_capture c;struct bus b;native_wait_capture_reset(&c);
    fixture(&b,0x8c2ff800u,false);put(&b,b.frame+392u,0x8c123456u);CHECK(!add(&c,&b));
    fixture(&b,0x8c2ff800u,false);CHECK(!add(&c,&b));
    put(&b,b.frame+360u,0xffffffffu);CHECK(!add(&c,&b));
    CHECK(c.valid==3u && c.first_wait==1u && c.first_bad==2u);
    CHECK(c.record[0].pc==0x8c123456u && c.record[2].r12==UINT32_MAX);
    CHECK(!native_wait_capture_ready(NULL));
    CHECK(!native_wait_capture_add(NULL,0u,0u,0u,NULL,NULL));
}
int main(void) {
    all_aliases_and_layout();spans_and_refusals();banks_and_operands();pc_and_t_rules();
    printf("native wait capture: %u checks PASS; record=%zu capture=%zu phase=%zu\n",
        checks,sizeof(struct native_wait_record),sizeof(struct native_wait_capture),sizeof(struct native_phase_state));
    return 0;
}
