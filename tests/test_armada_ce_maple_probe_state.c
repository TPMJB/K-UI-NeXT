/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the independent closed RAM/cache fixture. The actual production
 * wrapper is compiled separately with proof, state and host boundaries. */
#define KUI_ARMADA_CE_MAPLE_STATE_TEST_MAIN kui_armada_ce_maple_probe_state_fixture_main
#include "test_armada_ce_maple_state.c"
#include "kui/armada_ce_maple_probe.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"

#if !KUI_ARMADA_CE_MAPLE_PROOF || !KUI_ARMADA_CE_MAPLE_STATE || \
    !defined(KUI_ARMADA_CE_MAPLE_PROBE_HOST_TEST)
#error "Integrated Maple state fixture requires production proof/state and host boundaries"
#endif
static unsigned wrapper_ram_calls,wrapper_cache_calls;
static const char *retained_labels[18];
static const char *const expected_labels[18]={
    "DISCOVERY: SAMPLE OK REFUSED READS STATUS",
    "TTB PTEH CCR PROCESS HEAD",
    "MAPLE NODE BASE SPAN SECTIONS ATTACH",
    "MAPLEDEV NODE BASE SPAN SECTIONS ATTACH",
    "MDSTAR MDTSEL MDEN MDST ISTNRM",
    "FAULT WHY AT VALUE EXPECT DETAIL",
    "MAPPING: STATUS READS EXPECT CRC MASK",
    "CODE VA SLOT BLOCK PAGE PHYSICAL",
    "OWNER PROCESS SLOT HANDLE ATTACH ROOT VA",
    "ROOT OWNER P1 PAGE VALUE CANON P1 VALUE",
    "MAP STATUS CODE OWNER CANON WORD CANON VA",
    "FAULT WHY AT VALUE EXPECT DETAIL",
    "DRIVER: STATUS READS MASK RAW EXT VA",
    "EXT PHYSICAL PAGE SHUTDOWN MMIO SIZE",
    "TX BUS RAW VA LAST END PHYSICAL",
    "RX BUS HIGH RAW VA BIAS PHYSICAL",
    "TX PAGE RX PAGE0 PAGE1 SLOT0 OWNER",
    "FAULT WHY AT VALUE EXPECT DETAIL"
};
bool kui_armada_ce_maple_probe_test_read(void *context,uint32_t address,uint32_t *out) {
    assert(context==NULL);++wrapper_ram_calls;
    return read_ram(&f,address,out);
}
bool kui_armada_ce_maple_probe_test_cache(void *context,uint32_t address,uint32_t *out) {
    assert(context==NULL);++wrapper_cache_calls;
    return read_cache(&f,address,out);
}
static void memory_reset(void) {
    prepare(0x30a60u);wrapper_ram_calls=wrapper_cache_calls=0;
}
static const struct kui_armada_ce_maple_probe_report *report_at(
    const struct kui_armada_ce_maple_snapshot *discovery) {
    const struct kui_armada_ce_maple_probe_report *r=
        (const struct kui_armada_ce_maple_probe_report *)(const void *)discovery;
    assert((const void *)&r->discovery==(const void *)r && &r->discovery==discovery);
    assert((const unsigned char *)&r->proof==(const unsigned char *)discovery+120u);
    assert((const unsigned char *)&r->state==(const unsigned char *)discovery+240u);
    for(unsigned i=0;i<18u;i++) {
        assert(r->labels[i] && !strcmp(r->labels[i],expected_labels[i]));
        if(retained_labels[i]) assert(r->labels[i]==retained_labels[i]);
        else retained_labels[i]=r->labels[i];
    }
    return r;
}
static const struct kui_armada_ce_maple_probe_report *entry(uint32_t function,
    uint32_t command,int32_t result,uint32_t sample) {
    return report_at(kui_armada_ce_bind_probe(function,command,result,sample));
}
static void assert_no_reads(void) {
    assert(!wrapper_ram_calls && !wrapper_cache_calls && !f.calls);
}
static void assert_state_zero(const struct kui_armada_ce_maple_probe_report *r) {
    struct kui_armada_ce_maple_state_snapshot zero={0};
    assert(!memcmp(&r->state,&zero,sizeof(zero)));
}
static void assert_proof_zero(const struct kui_armada_ce_maple_probe_report *r) {
    struct kui_armada_ce_maple_proof_snapshot zero={0};
    assert(!memcmp(&r->proof,&zero,sizeof(zero)));
}
static void assert_no_extension_reads(void) {
    for(unsigned i=0;i<10u;i++) assert(!visits((ext_p1+fields[i])|0x20000000u));
    assert(!visits(owner_table+12u) && !visits(ext_block+12u));
}
static void rejected_and_report_only(void) {
    static const struct {uint32_t function,command;int32_t result;uint32_t sample;} calls[]={
        {KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,0},
        {KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,1,0},
        {UINT32_MAX,0,0,0},
        {KUI_GD_EXEC,KUI_RETAIL_GD_PLAY,1,1},
        {KUI_GD_CHECK,KUI_RETAIL_GD_PLAY2,1,1},
        {KUI_GD_REQUEST,KUI_RETAIL_GD_SEEK,1,1},
        {KUI_GD_REQUEST,KUI_RETAIL_GD_PAUSE,1,1},
        {KUI_GD_REQUEST,KUI_GD_STOP,1,1},
        {KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,0,1},
        {KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,-1,1},
        {KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,INT32_MIN,1},
        {KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,2},
        {KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,1,UINT32_MAX}
    };
    const struct kui_armada_ce_maple_probe_report *r=entry(0,0,0,0);
    struct kui_armada_ce_maple_probe_report before=*r;
    for(unsigned i=0;i<sizeof(calls)/sizeof(*calls);i++) {
        memory_reset();f.fail_call=1u;
        assert(entry(calls[i].function,calls[i].command,calls[i].result,calls[i].sample)==r);
        assert_no_reads();assert(!memcmp(r,&before,sizeof(before)));
    }
}
static const struct kui_armada_ce_maple_probe_report *valid(uint32_t command,
    uint32_t attempts,uint32_t committed,uint32_t refused) {
    memory_reset();
    const struct kui_armada_ce_maple_probe_report *r=entry(KUI_GD_REQUEST,command,1,1);
    assert(r->discovery.counts[0]==attempts && r->discovery.counts[1]==committed && r->discovery.counts[2]==refused);
    assert(r->discovery.counts[3]==339u && r->discovery.counts[4]==KUI_ARMADA_CE_MAPLE_MISSING);
    assert(r->discovery.maple[0]==module && !r->discovery.mapledev[0]);
    assert(r->proof.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_OK && r->proof.counts[1]==662u);
    assert(r->proof.counts[2]==KUI_ARMADA_CE_MAPLE_PROOF_CODE_CRC && r->proof.counts[3]==r->proof.counts[2]);
    assert(r->proof.counts[4]==31u && r->proof.root[2]==0x30a60u && !r->proof.root[4]);
    assert(r->state.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_OK && r->state.counts[1]==1029u);
    assert(r->state.counts[2]==31u && r->state.counts[3]==0x30a60u && r->state.counts[4]==ext_va);
    assert(r->state.tx[4]==0x8cfd3000u && r->state.rx[4]==0x8cfd4000u);
    assert(r->state.maps[3]==owner_table && r->state.maps[4]==owner_table);
    assert(f.calls==r->discovery.counts[3]+r->proof.counts[1]+r->state.counts[1] && f.calls==2030u);
    assert(wrapper_ram_calls+wrapper_cache_calls==f.calls && wrapper_cache_calls>0u);
    assert(!find(0x8cfd3000u) && !find(0x8cfd4000u) && !find(0x8cfd5000u) && !find(0x1e400000u));
    return r;
}
static void refusal_revokes_latest_state(void) {
    memory_reset();put(KUI_ARMADA_CE_MAPLE_TTB,0xac1458a0u);
    const struct kui_armada_ce_maple_probe_report *r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,1);
    assert(r->discovery.counts[0]==3u && r->discovery.counts[1]==2u && r->discovery.counts[2]==1u);
    assert(r->discovery.counts[4]==KUI_ARMADA_CE_MAPLE_CONTEXT);
    assert_proof_zero(r);assert_state_zero(r);assert_no_extension_reads();
    assert(wrapper_ram_calls==7u && !wrapper_cache_calls && f.calls==7u);
    rejected_and_report_only();valid(KUI_RETAIL_GD_PLAY2,4u,3u,1u);

    memory_reset();f.fail_call=340u;
    r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,1,1);
    assert(r->discovery.counts[0]==5u && r->discovery.counts[4]==KUI_ARMADA_CE_MAPLE_MISSING);
    assert(r->proof.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_READ && r->proof.counts[1]==1u);
    assert_state_zero(r);assert_no_extension_reads();assert(f.calls==340u && !wrapper_cache_calls);
    rejected_and_report_only();valid(KUI_RETAIL_GD_PLAY,6u,5u,1u);

    memory_reset();put(f.cached[0].p1,synthetic[0]^1u);
    r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,1);
    assert(r->discovery.counts[0]==7u && r->proof.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_CODE);
    assert(r->proof.counts[2]!=r->proof.counts[3]);assert_state_zero(r);assert_no_extension_reads();
    assert(f.calls==339u+r->proof.counts[1]);rejected_and_report_only();
}
static void state_refusal_preserves_proof(void) {
    const struct kui_armada_ce_maple_probe_report *r=valid(KUI_RETAIL_GD_PLAY2,8u,7u,1u);
    struct kui_armada_ce_maple_proof_snapshot before=r->proof;
    memory_reset();extension_word(0,1u);
    r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,1);
    assert(r->discovery.counts[0]==9u && !memcmp(&r->proof,&before,sizeof(before)));
    assert(r->state.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_METADATA && r->state.extension[2]==1u);
    assert(r->state.counts[1]>0u && r->state.counts[2]!=31u);
    assert(f.calls==339u+662u+r->state.counts[1]);rejected_and_report_only();

    valid(KUI_RETAIL_GD_PLAY2,10u,9u,1u);
    memory_reset();f.fail_call=1002u;
    r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,1,1);
    assert(r->discovery.counts[0]==11u && !memcmp(&r->proof,&before,sizeof(before)));
    assert(r->state.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_READ && r->state.counts[1]==1u);
    assert(r->state.counts[2]!=31u && f.calls==1002u);assert_no_extension_reads();
    rejected_and_report_only();
}
static void cap_and_reset(void) {
    kui_armada_ce_maple_probe_test_reset();
    for(unsigned i=0;i<KUI_ARMADA_CE_MAPLE_SAMPLE_LIMIT;i++)
        valid(i&1u?KUI_RETAIL_GD_PLAY:KUI_RETAIL_GD_PLAY2,i+1u,i+1u,0u);
    const struct kui_armada_ce_maple_probe_report *r=entry(0,0,0,0);
    struct kui_armada_ce_maple_probe_report before=*r;
    for(unsigned i=0;i<3u;i++) {
        memory_reset();f.fail_call=1u;
        assert(entry(KUI_GD_REQUEST,i&1u?KUI_RETAIL_GD_PLAY:KUI_RETAIL_GD_PLAY2,1,1)==r);
        assert_no_reads();assert(!memcmp(r,&before,sizeof(before)));
    }
    rejected_and_report_only();
    memory_reset();kui_armada_ce_maple_probe_test_reset();
    r=entry(0,0,0,0);assert_no_reads();assert_proof_zero(r);assert_state_zero(r);
    struct kui_armada_ce_maple_snapshot zero={0};
    assert(!memcmp(&r->discovery,&zero,sizeof(zero)));
}
int main(void) {
    memory_reset();kui_armada_ce_maple_probe_test_reset();
    const struct kui_armada_ce_maple_probe_report *r=entry(0,0,0,0);
    assert_no_reads();assert_proof_zero(r);assert_state_zero(r);
    assert(KUI_ARMADA_CE_MAPLE_PROBE_LABELS==18u && kui_armada_ce_maple_probe_contract==UINT32_C(0x33504d43));
    rejected_and_report_only();valid(KUI_RETAIL_GD_PLAY,1u,1u,0u);
    rejected_and_report_only();valid(KUI_RETAIL_GD_PLAY2,2u,2u,0u);
    refusal_revokes_latest_state();state_refusal_preserves_proof();cap_and_reset();
    puts("Integrated CE Maple state entry: CMP3 and 18 stable labels; dormant/rejected/report/cap paths preserve reports without reads; PLAY/PLAY2 run 339+662+1029 callbacks; metadata/proof refusal revokes state and state refusal preserves proof.");
    return 0;
}
