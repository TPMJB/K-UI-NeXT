/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the closed, independent RAM/cache fixture, not a replacement entry.
 * The actual wrapper is compiled as a separate translation unit. */
#define main kui_armada_ce_maple_standalone_fixture_main
#include "test_armada_ce_maple_proof.c"
#undef main
#include "kui/armada_ce_maple_probe.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"

#if !KUI_ARMADA_CE_MAPLE_PROOF || !defined(KUI_ARMADA_CE_MAPLE_PROBE_HOST_TEST)
#error "Integrated Maple proof fixture requires production proof and host boundaries"
#endif
static unsigned wrapper_ram_calls,wrapper_cache_calls;
static const char *retained_labels[12];
bool kui_armada_ce_maple_probe_test_read(void *context,uint32_t address,uint32_t *out) {
    assert(context==NULL);++wrapper_ram_calls;
    return read_ram(&f,address,out);
}
bool kui_armada_ce_maple_probe_test_cache(void *context,uint32_t address,uint32_t *out) {
    assert(context==NULL);++wrapper_cache_calls;
    return read_cache(&f,address,out);
}
static void memory_reset(void) {
    reset();wrapper_ram_calls=wrapper_cache_calls=0;
}
static const struct kui_armada_ce_maple_probe_report *report_at(
    const struct kui_armada_ce_maple_snapshot *discovery) {
    const struct kui_armada_ce_maple_probe_report *r=
        (const struct kui_armada_ce_maple_probe_report *)(const void *)discovery;
    assert((const void *)&r->discovery==(const void *)r && &r->discovery==discovery);
    assert((const unsigned char *)&r->proof==(const unsigned char *)discovery+sizeof(*discovery));
    for(unsigned i=0;i<12u;i++) {
        assert(r->labels[i] && strlen(r->labels[i])>0u);
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
static void assert_proof_zero(const struct kui_armada_ce_maple_probe_report *r) {
    struct kui_armada_ce_maple_proof_snapshot zero={0};
    assert(!memcmp(&r->proof,&zero,sizeof(zero)));
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
static void valid(uint32_t command,uint32_t attempts,uint32_t committed,uint32_t refused) {
    memory_reset();
    const struct kui_armada_ce_maple_probe_report *r=entry(KUI_GD_REQUEST,command,1,1);
    assert(r->discovery.counts[0]==attempts && r->discovery.counts[1]==committed && r->discovery.counts[2]==refused);
    assert(r->discovery.counts[3]==0x153u && r->discovery.counts[4]==KUI_ARMADA_CE_MAPLE_MISSING);
    assert(r->discovery.maple[0]==module && !r->discovery.mapledev[0]);
    assert(r->proof.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_OK && r->proof.counts[1]==662u);
    assert(r->proof.counts[2]==KUI_ARMADA_CE_MAPLE_PROOF_CODE_CRC && r->proof.counts[3]==r->proof.counts[2]);
    assert(r->proof.counts[4]==31u && wrapper_cache_calls>0u);
    assert(f.calls==r->discovery.counts[3]+r->proof.counts[1]);
    assert(wrapper_ram_calls+wrapper_cache_calls==f.calls && f.calls==1001u);
    assert(r->proof.owner[4]==0x09dd72b8u && r->proof.root[2]==0x8cfd4000u);
}
static void refusal_revokes_latest_report(void) {
    /* An accepted sample with a refused collector must clear the prior
     * successful proof; no proof-only table or cache read may occur. */
    memory_reset();put(KUI_ARMADA_CE_MAPLE_TTB,0xac1458a0u);
    const struct kui_armada_ce_maple_probe_report *r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,1);
    assert(r->discovery.counts[0]==3u && r->discovery.counts[1]==2u && r->discovery.counts[2]==1u);
    assert(r->discovery.counts[4]==KUI_ARMADA_CE_MAPLE_CONTEXT);
    assert_proof_zero(r);assert(wrapper_ram_calls==7u && !wrapper_cache_calls && f.calls==7u);
    rejected_and_report_only();
    valid(KUI_RETAIL_GD_PLAY2,4u,3u,1u);
    memory_reset();f.fail_call=1u;
    r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,1,1);
    assert(r->discovery.counts[0]==5u && r->discovery.counts[4]==KUI_ARMADA_CE_MAPLE_READ);
    assert_proof_zero(r);assert(wrapper_ram_calls==1u && !wrapper_cache_calls && f.calls==1u);
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
    r=entry(0,0,0,0);assert_no_reads();assert_proof_zero(r);
    struct kui_armada_ce_maple_snapshot zero={0};
    assert(!memcmp(&r->discovery,&zero,sizeof(zero)));
}
int main(void) {
    memory_reset();kui_armada_ce_maple_probe_test_reset();
    const struct kui_armada_ce_maple_probe_report *r=entry(0,0,0,0);
    assert_no_reads();assert_proof_zero(r);
    assert(kui_armada_ce_maple_probe_contract==UINT32_C(0x32504d43));
    rejected_and_report_only();valid(KUI_RETAIL_GD_PLAY,1u,1u,0u);
    rejected_and_report_only();valid(KUI_RETAIL_GD_PLAY2,2u,2u,0u);
    refusal_revokes_latest_report();cap_and_reset();
    puts("Integrated CE Maple proof entry: dormant/rejected/report/cap paths read nothing; valid PLAY/PLAY2 run both models; collector refusal clears proof; labels and initial-member ABI retained.");
    return 0;
}
