/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the actual CMP4 wrapper with the closed mapping/cache fixture.
 * Only the eight authored request/response header words are readable inside
 * the transfer ranges; deliberately absent payload cells catch overreads. */
#define KUI_ARMADA_CE_MAPLE_STATE_TEST_MAIN kui_armada_ce_maple_probe_headers_fixture_main
#include "test_armada_ce_maple_state.c"
#include "kui/armada_ce_maple_probe.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"

#if !KUI_ARMADA_CE_MAPLE_PROOF || !KUI_ARMADA_CE_MAPLE_STATE || \
    !KUI_ARMADA_CE_MAPLE_HEADERS || !defined(KUI_ARMADA_CE_MAPLE_PROBE_HOST_TEST)
#error "Integrated Maple header fixture requires CMP4 proof/state/headers and host boundaries"
#endif
static unsigned wrapper_ram_calls,wrapper_cache_calls,header_calls,fail_header_call;
enum after_capture {CAPTURE_NONE,CAPTURE_METADATA,CAPTURE_MAPPING,CAPTURE_READ};
static enum after_capture change_after_capture;
static const char *retained_labels[21];
static const char *const expected_labels[21]={
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
    "FAULT WHY AT VALUE EXPECT DETAIL",
    "HEADERS: STATUS READS COUNT BYTES MASK",
    "OFFSET CONTROL RX BUS REQUEST RESPONSE",
    "HEADER FAULT WHY AT VALUE EXPECT DETAIL"
};
static const uint32_t header_bus[8]={
    0x0cfd3000u,0x0cfd3004u,0x0cfd3008u,0x0cfd4020u,
    0x0cfd3010u,0x0cfd3014u,0x0cfd3018u,0x0cfd4100u
};
static const uint32_t header_value[8]={
    1u,0x0cfd4020u,0x0100020cu,0x82010008u,
    0x80010002u,0x0cfd4100u,0x02406001u,0x10004005u
};
static bool transfer_address(uint32_t a) {
    return (a>=0xacfd3000u && a<0xacfd4000u) ||
        (a>=0xacfd4000u && a<0xacfd6000u);
}
bool kui_armada_ce_maple_probe_test_read(void *context,uint32_t address,uint32_t *out) {
    assert(context==NULL);++wrapper_ram_calls;
    assert(!transfer_address(address)); /* Headers must use the P2 callback. */
    return read_ram(&f,address,out);
}
bool kui_armada_ce_maple_probe_test_cache(void *context,uint32_t address,uint32_t *out) {
    assert(context==NULL);++wrapper_cache_calls;
    bool header=transfer_address(address);
    if(header) {
        bool admitted=false;
        for(unsigned i=0;i<8u;i++) admitted|=address==(header_bus[i]|0xa0000000u);
        assert(admitted); /* No request or response payload read is admitted. */
        ++header_calls;
        if(header_calls==fail_header_call) f.fail_call=f.calls+1u;
    }
    bool ok=read_cache(&f,address,out);
    if(header && header_calls==16u) {
        if(change_after_capture==CAPTURE_METADATA) extension_word(0,1u);
        if(change_after_capture==CAPTURE_MAPPING) put(tx_block+12u,0x0cfd6071u);
        if(change_after_capture==CAPTURE_READ) f.fail_call=f.calls+1u;
    }
    return ok;
}
static void memory_reset(void) {
    prepare(0x30a60u);
    extension_word(3,0x00050010u);extension_word(4,0x00050024u);
    for(unsigned i=0;i<8u;i++) put(header_bus[i]|0x80000000u,header_value[i]);
    wrapper_ram_calls=wrapper_cache_calls=header_calls=fail_header_call=0;
    change_after_capture=CAPTURE_NONE;
}
static const struct kui_armada_ce_maple_probe_report *report_at(
    const struct kui_armada_ce_maple_snapshot *discovery) {
    const struct kui_armada_ce_maple_probe_report *r=(const void *)discovery;
    assert((const void *)&r->discovery==(const void *)r && &r->discovery==discovery);
    assert((const unsigned char *)&r->proof==(const unsigned char *)discovery+120u);
    assert((const unsigned char *)&r->state==(const unsigned char *)discovery+240u);
    assert(r->headers);
    for(unsigned i=0;i<21u;i++) {
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
static void assert_headers_zero(const struct kui_armada_ce_maple_probe_report *r) {
    struct kui_armada_ce_maple_headers_snapshot zero={0};
    assert(!memcmp(r->headers,&zero,sizeof(zero)));
}
static void assert_no_reads(void) {
    assert(!wrapper_ram_calls && !wrapper_cache_calls && !header_calls && !f.calls);
}
static void assert_counts_agree(const struct kui_armada_ce_maple_probe_report *r) {
    assert(f.calls==r->discovery.counts[3]+r->proof.counts[1]+r->state.counts[1]);
    assert(wrapper_ram_calls+wrapper_cache_calls==f.calls);
    assert(header_calls==r->headers->counts[1]);
}
static void assert_scalar_state(const struct kui_armada_ce_maple_probe_report *r) {
    assert(r->state.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_OK && r->state.counts[2]==31u);
    assert(r->state.counts[3]==0x30a60u && r->state.counts[4]==ext_va);
    assert(r->state.tx[0]==tx_bus && r->state.tx[1]==0x50000u && r->state.tx[2]==0x50010u &&
        r->state.tx[3]==0x50024u && r->state.tx[4]==0x8cfd3000u);
    assert(r->state.rx[0]==rx_bus && !r->state.rx[1] && r->state.rx[2]==0x60000u &&
        r->state.rx[3]==rx_bus-0x60000u && r->state.rx[4]==0x8cfd4000u);
    assert(r->state.maps[0]==0x0cfd3071u && r->state.maps[1]==0x0cfd4071u &&
        r->state.maps[2]==0x0cfd5071u && r->state.maps[3]==owner_table && r->state.maps[4]==owner_table);
}
static const struct kui_armada_ce_maple_probe_report *valid(uint32_t command) {
    memory_reset();
    const struct kui_armada_ce_maple_probe_report *r=entry(KUI_GD_REQUEST,command,1,1);
    assert(r->discovery.counts[3]==339u && r->discovery.counts[4]==KUI_ARMADA_CE_MAPLE_MISSING);
    assert(r->proof.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_OK && r->proof.counts[1]==662u && r->proof.counts[4]==31u);
    assert(r->state.counts[1]==1045u && f.calls==2046u);assert_scalar_state(r);assert_counts_agree(r);
    assert(r->headers->counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_OK && r->headers->counts[1]==16u);
    assert(r->headers->counts[2]==2u && r->headers->counts[3]==36u && r->headers->counts[4]==3u);
    for(unsigned n=0;n<2u;n++) {
        assert(r->headers->rows[n][0]==n*16u);
        for(unsigned j=0;j<4u;j++) assert(r->headers->rows[n][j+1u]==header_value[n*4u+j]);
    }
    for(unsigned i=0;i<5u;i++) assert(!r->headers->fault[i] && !r->state.fault[i]);
    for(unsigned i=0;i<8u;i++) assert(visits(header_bus[i]|0xa0000000u)==2u);
    assert(!find(0x8cfd300cu) && !find(0x8cfd301cu) && !find(0x8cfd3020u));
    assert(!find(0x8cfd4024u) && !find(0x8cfd4104u));
    return r;
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
    struct kui_armada_ce_maple_headers_snapshot saved=*r->headers;
    for(unsigned i=0;i<sizeof(calls)/sizeof(*calls);i++) {
        memory_reset();f.fail_call=1u;
        assert(entry(calls[i].function,calls[i].command,calls[i].result,calls[i].sample)==r);
        assert_no_reads();assert(!memcmp(r,&before,sizeof(before)) && !memcmp(r->headers,&saved,sizeof(saved)));
    }
}
static void header_refusal_preserves_state(void) {
    const struct kui_armada_ce_maple_probe_report *r=valid(KUI_RETAIL_GD_PLAY);
    struct kui_armada_ce_maple_proof_snapshot saved=r->proof;
    for(unsigned i=0;i<2u;i++) {
        memory_reset();
        put((tx_bus|(uint32_t)0x80000000u)+i*4u,i?rx_bus+0x2000u:0x80000001u);
        r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,1,1);
        assert(!memcmp(&r->proof,&saved,sizeof(saved)));assert_scalar_state(r);assert_counts_agree(r);
        assert(r->headers->counts[0]==(i?KUI_ARMADA_CE_MAPLE_PROOF_MAPPING:KUI_ARMADA_CE_MAPLE_PROOF_METADATA));
        assert(r->headers->counts[1]==3u && r->state.counts[1]==1032u && !r->headers->counts[4]);
        rejected_and_report_only();
    }
    memory_reset();mutate((rx_bus+0x20u)|0xa0000000u,2u,header_value[3]^1u);
    r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,1);
    assert(r->headers->counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED && r->headers->counts[4]==1u);
    assert(r->headers->fault[1]==((rx_bus+0x20u)|0xa0000000u));
    assert(!memcmp(&r->proof,&saved,sizeof(saved)));assert_scalar_state(r);assert_counts_agree(r);
}
static void final_witnesses_revoke_headers(void) {
    const struct kui_armada_ce_maple_probe_report *r=valid(KUI_RETAIL_GD_PLAY2);
    struct kui_armada_ce_maple_proof_snapshot saved=r->proof;
    for(unsigned i=CAPTURE_METADATA;i<=CAPTURE_READ;i++) {
        memory_reset();change_after_capture=(enum after_capture)i;
        r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,1);
        assert(header_calls==16u && r->headers->counts[4]==3u && r->headers->counts[2]==2u);
        assert(r->state.counts[0]==(i==CAPTURE_READ?KUI_ARMADA_CE_MAPLE_PROOF_READ:KUI_ARMADA_CE_MAPLE_PROOF_CHANGED));
        assert(r->headers->counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_CHANGED);
        assert(!memcmp(r->headers->fault,r->state.fault,sizeof(r->state.fault)));
        assert(!memcmp(&r->proof,&saved,sizeof(saved)));assert_counts_agree(r);
        rejected_and_report_only();
    }
}
static void header_read_failures(void) {
    for(unsigned i=1;i<=16u;i++) {
        memory_reset();kui_armada_ce_maple_probe_test_reset();fail_header_call=i;
        const struct kui_armada_ce_maple_probe_report *r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,1,1);
        assert(r->proof.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_OK);
        assert(r->state.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_READ);
        assert(r->headers->counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_READ && header_calls==i);
        assert_counts_agree(r);assert(f.calls==f.fail_call);
    }
}
static void earlier_failures_clear_headers(void) {
    valid(KUI_RETAIL_GD_PLAY);
    memory_reset();put(KUI_ARMADA_CE_MAPLE_TTB,0xac1458a0u);
    const struct kui_armada_ce_maple_probe_report *r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,1,1);
    assert(r->discovery.counts[4]==KUI_ARMADA_CE_MAPLE_CONTEXT && !header_calls);assert_headers_zero(r);
    valid(KUI_RETAIL_GD_PLAY);
    memory_reset();f.fail_call=340u;
    r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,1,1);
    assert(r->proof.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_READ && !header_calls);assert_headers_zero(r);
    valid(KUI_RETAIL_GD_PLAY);
    memory_reset();extension_word(0,1u);
    r=entry(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY2,1,1);
    assert(r->state.counts[0]==KUI_ARMADA_CE_MAPLE_PROOF_METADATA && !header_calls);assert_headers_zero(r);
}
static void immutable_inputs(void) {
    /* The direct state call checks the original discovery, module list,
     * proof snapshot and full proof workspace byte-for-byte on return. */
    memory_reset();
    assert(run_state()==KUI_ARMADA_CE_MAPLE_PROOF_OK);
    struct kui_armada_ce_maple_state_snapshot saved=state;
    struct kui_armada_ce_maple_headers_snapshot header;
    reset_reads();
    assert(kui_armada_ce_maple_headers(state.tx,state.rx,state.maps,read_cache,&f,&header)==KUI_ARMADA_CE_MAPLE_PROOF_OK);
    assert(!memcmp(&state,&saved,sizeof(saved)) && f.calls==16u);
}
static void cap_and_reset(void) {
    kui_armada_ce_maple_probe_test_reset();
    for(unsigned i=0;i<KUI_ARMADA_CE_MAPLE_SAMPLE_LIMIT;i++) {
        const struct kui_armada_ce_maple_probe_report *r=valid(i&1u?KUI_RETAIL_GD_PLAY:KUI_RETAIL_GD_PLAY2);
        assert(r->discovery.counts[0]==i+1u && r->discovery.counts[1]==i+1u && !r->discovery.counts[2]);
    }
    const struct kui_armada_ce_maple_probe_report *r=entry(0,0,0,0);
    struct kui_armada_ce_maple_probe_report before=*r;
    struct kui_armada_ce_maple_headers_snapshot saved=*r->headers;
    for(unsigned i=0;i<3u;i++) {
        memory_reset();f.fail_call=1u;
        assert(entry(KUI_GD_REQUEST,i&1u?KUI_RETAIL_GD_PLAY:KUI_RETAIL_GD_PLAY2,1,1)==r);
        assert_no_reads();assert(!memcmp(r,&before,sizeof(before)) && !memcmp(r->headers,&saved,sizeof(saved)));
    }
    rejected_and_report_only();
    memory_reset();kui_armada_ce_maple_probe_test_reset();
    r=entry(0,0,0,0);assert_no_reads();assert_headers_zero(r);
    struct kui_armada_ce_maple_snapshot zero={0};
    assert(!memcmp(&r->discovery,&zero,sizeof(zero)));
}
int main(void) {
    memory_reset();kui_armada_ce_maple_probe_test_reset();
    const struct kui_armada_ce_maple_probe_report *r=entry(0,0,0,0);
    assert_no_reads();assert_headers_zero(r);
    assert(KUI_ARMADA_CE_MAPLE_PROBE_LABELS==21u && kui_armada_ce_maple_probe_contract==UINT32_C(0x34504d43));
    rejected_and_report_only();valid(KUI_RETAIL_GD_PLAY);rejected_and_report_only();valid(KUI_RETAIL_GD_PLAY2);
    header_refusal_preserves_state();final_witnesses_revoke_headers();earlier_failures_clear_headers();
    header_read_failures();immutable_inputs();cap_and_reset();
    puts("Integrated CE Maple headers: CMP4 and 21 stable labels; exact header-only P2 reads; 339+662+1045 callbacks; immutable inputs; header refusals preserve state; final witnesses revoke header success; dormant/report/cap read nothing.");
    return 0;
}
