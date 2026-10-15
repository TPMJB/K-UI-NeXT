/* SPDX-License-Identifier: GPL-3.0-only */
/* ROMHDR metadata scalars and generated samples only; no retail game bytes. */
#include "kui/armada_ce_excerpt_pool.h"
#include "kui/core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define HEADER_WORDS 21u
#define SEED_WORDS 16u
#define PCM_WORDS 88200u
#define TOTAL_READS (HEADER_WORDS+SEED_WORDS+PCM_WORDS)
static const uint32_t metadata[HEADER_WORDS]={
    0x01d20000u,0x02000000u,0x8c010000u,0x8c141778u,21u,
    0x8c142000u,0x8c150000u,0x8cfd0000u,1u,0x8c141768u,
    0u,0u,3u,1u,0u,0x8cfd0000u,0x00030000u,0u,0u,0u,0u
};
static const struct kui_armada_ce_excerpt_pool_limits valid_limits={
    0x8ce22eecu,0x8ce2e8a4u,true
};
static unsigned checks;
static uint32_t pcm_crc;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void put32(uint8_t *out,uint32_t word) {
    for(unsigned byte=0;byte<4u;byte++) out[byte]=(uint8_t)(word>>(8u*byte));
}
static void header_make(uint8_t out[84]) {
    for(unsigned i=0;i<HEADER_WORDS;i++) put32(out+4u*i,metadata[i]);
}
static uint32_t generated_word(unsigned index) {
    return (index*UINT32_C(0x9e3779b9))^(index>>7u)^UINT32_C(0xa55a5a5a);
}
static void sentinel(struct kui_armada_ce_excerpt_pool_plan *plan) {
    memset(plan,0xa5,sizeof(*plan));
}
static void expected_plan(const struct kui_armada_ce_excerpt_pool_plan *plan,uint32_t crc) {
    CHECK(plan->patch_address==0x8c14143cu);
    CHECK(plan->original_ram_end==0x8cfd0000u);
    CHECK(plan->reserved_ram_end==0x8cf50000u);
    CHECK(plan->pool_begin==0x8cf50000u && plan->pool_end==0x8cfd0000u);
    CHECK(plan->pcm_begin==0x8cf50040u && plan->pcm_bytes==352800u);
    CHECK(plan->pcm_crc32==crc && plan->sectors==150u && plan->start_lba==149992u);
}
static struct kui_armada_ce_excerpt_pool_inputs input_make(uint8_t header[84],uint32_t seed[16]) {
    header_make(header);CHECK(kui_armada_ce_excerpt_seed_make(seed,pcm_crc));
    return (struct kui_armada_ce_excerpt_pool_inputs){
        header,84u,seed,64u,352800u,pcm_crc,valid_limits,false
    };
}
static void rejected(const struct kui_armada_ce_excerpt_pool_inputs *in,
    enum kui_armada_ce_excerpt_pool_status expected) {
    struct kui_armada_ce_excerpt_pool_plan out,before;
    sentinel(&out);before=out;
    CHECK(kui_armada_ce_excerpt_pool_prepare(in,&out)==expected);
    CHECK(!memcmp(&before,&out,sizeof(out)));
}
static void crc_and_seed_contract(void) {
    static const uint8_t vector[]="123456789";
    CHECK(kui_crc32(0,vector,9u)==UINT32_C(0xcbf43926));
    CHECK(kui_armada_ce_excerpt_crc32(vector,9u)==UINT32_C(0xcbf43926));
    CHECK(kui_armada_ce_excerpt_crc32(NULL,0u)==0u);
    uint32_t crc=kui_armada_ce_excerpt_crc32_word(0u,UINT32_C(0x34333231));
    crc=kui_armada_ce_excerpt_crc32_word(crc,UINT32_C(0x38373635));
    CHECK(crc==kui_crc32(0,vector,8u));
    uint8_t header[84];header_make(header);
    CHECK(kui_crc32(0,header,sizeof(header))==UINT32_C(0x7ddd77a1));
    CHECK(kui_armada_ce_excerpt_crc32(header,sizeof(header))==UINT32_C(0x7ddd77a1));
    uint32_t seed[16];memset(seed,0xff,sizeof(seed));
    CHECK(!kui_armada_ce_excerpt_seed_make(NULL,0u));
    CHECK(kui_armada_ce_excerpt_seed_make(seed,0u));
    CHECK(seed[0]==UINT32_C(0x31584543) && seed[1]==1u && seed[2]==352800u);
    CHECK(seed[3]==0u && seed[4]==150u && seed[5]==149992u);
    for(unsigned i=6u;i<16u;i++) CHECK(seed[i]==0u);
    CHECK(kui_armada_ce_excerpt_seed_valid(seed));
    CHECK(kui_armada_ce_excerpt_seed_matches(seed,352800u,0u));
    CHECK(!kui_armada_ce_excerpt_seed_matches(seed,352799u,0u));
    CHECK(!kui_armada_ce_excerpt_seed_matches(seed,352800u,1u));
    CHECK(!kui_armada_ce_excerpt_seed_valid(NULL));
    seed[5]=149842u;CHECK(!kui_armada_ce_excerpt_seed_valid(seed));
    seed[5]=149992u;CHECK(kui_armada_ce_excerpt_seed_valid(seed));
    pcm_crc=0u;
    for(unsigned i=0;i<PCM_WORDS;i++) {
        uint8_t bytes[4];put32(bytes,generated_word(i));
        pcm_crc=kui_crc32(pcm_crc,bytes,sizeof(bytes));
    }
}
static void header_and_input_rejections(void) {
    uint8_t header[84],original[84];uint32_t seed[16],old_seed[16];
    struct kui_armada_ce_excerpt_pool_inputs in=input_make(header,seed);
    memcpy(original,header,sizeof(original));memcpy(old_seed,seed,sizeof(seed));
    struct kui_armada_ce_excerpt_pool_plan out;
    CHECK(kui_armada_ce_excerpt_pool_prepare(&in,&out)==KUI_ARMADA_CE_EXCERPT_POOL_OK);
    expected_plan(&out,pcm_crc);
    CHECK(!memcmp(original,header,sizeof(header)) && !memcmp(old_seed,seed,sizeof(seed)));
    for(unsigned byte=0;byte<84u;byte++) for(unsigned bit=0;bit<8u;bit++) {
        header[byte]^=(uint8_t)(1u<<bit);
        rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_HEADER);
        header[byte]^=(uint8_t)(1u<<bit);
    }
    CHECK(!memcmp(original,header,sizeof(header)) && !memcmp(old_seed,seed,sizeof(seed)));
    in.original_header=NULL;rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_HEADER);in.original_header=header;
    const size_t bad_header_sizes[]={0u,83u,85u,SIZE_MAX};
    for(unsigned i=0;i<sizeof(bad_header_sizes)/sizeof(bad_header_sizes[0]);i++) {
        in.header_bytes=bad_header_sizes[i];rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_HEADER);
    }
    in.header_bytes=84u;
    for(unsigned word=0;word<16u;word++) for(unsigned bit=0;bit<32u;bit++) {
        seed[word]^=UINT32_C(1)<<bit;
        rejected(&in,word==3u?KUI_ARMADA_CE_EXCERPT_POOL_PCM_CRC:KUI_ARMADA_CE_EXCERPT_POOL_SEED);
        seed[word]^=UINT32_C(1)<<bit;
    }
    in.seed=NULL;rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_SEED);in.seed=seed;
    const size_t bad_seed_sizes[]={0u,63u,65u,SIZE_MAX};
    for(unsigned i=0;i<sizeof(bad_seed_sizes)/sizeof(bad_seed_sizes[0]);i++) {
        in.seed_bytes=bad_seed_sizes[i];rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_SEED);
    }
    in.seed_bytes=64u;in.pcm_bytes=352799u;rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_PCM_BYTES);
    in.pcm_bytes=352804u;rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_PCM_BYTES);
    in.pcm_bytes=352800u;in.pcm_crc32^=1u;rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_PCM_CRC);
    in.pcm_crc32=pcm_crc;in.cancelled=true;in.original_header=NULL;
    rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_CANCELLED);
    rejected(NULL,KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT);
    CHECK(kui_armada_ce_excerpt_pool_prepare(&in,NULL)==KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT);
    /* The interface deliberately admits an independently measured zero CRC. */
    in=input_make(header,seed);seed[3]=0u;in.pcm_crc32=0u;
    CHECK(kui_armada_ce_excerpt_pool_prepare(&in,&out)==KUI_ARMADA_CE_EXCERPT_POOL_OK);
    expected_plan(&out,0u);
}
static void layout_boundaries(void) {
    struct kui_armada_ce_excerpt_pool_limits limits=valid_limits;
    CHECK(kui_armada_ce_excerpt_pool_limits_valid(NULL)==KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT);
    const uint32_t stage_bad[]={0u,0x8ce0ffffu,0x8cf50001u,0xacf00000u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(stage_bad)/sizeof(stage_bad[0]);i++) {
        limits=valid_limits;limits.stage_end=stage_bad[i];
        CHECK(kui_armada_ce_excerpt_pool_limits_valid(&limits)==KUI_ARMADA_CE_EXCERPT_POOL_STAGE);
    }
    const uint32_t bss_bad[]={0u,0x8ce22eebu,0x8cf50000u,0x8cf50001u,0xacf00000u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(bss_bad)/sizeof(bss_bad[0]);i++) {
        limits=valid_limits;limits.bss_end=bss_bad[i];
        CHECK(kui_armada_ce_excerpt_pool_limits_valid(&limits)==KUI_ARMADA_CE_EXCERPT_POOL_STAGE);
    }
    limits=valid_limits;limits.bss_end=limits.stage_end;
    CHECK(kui_armada_ce_excerpt_pool_limits_valid(&limits)==KUI_ARMADA_CE_EXCERPT_POOL_OK);
    limits.bss_end=0x8cf4ffffu;
    CHECK(kui_armada_ce_excerpt_pool_limits_valid(&limits)==KUI_ARMADA_CE_EXCERPT_POOL_OK);
    limits.stage_end=0x8cf50000u;limits.bss_end=limits.stage_end;
    CHECK(kui_armada_ce_excerpt_pool_limits_valid(&limits)==KUI_ARMADA_CE_EXCERPT_POOL_STAGE);
    limits=valid_limits;limits.gd_destination_excluded=false;
    CHECK(kui_armada_ce_excerpt_pool_limits_valid(&limits)==KUI_ARMADA_CE_EXCERPT_POOL_DESTINATION);
    uint8_t header[84];uint32_t seed[16];struct kui_armada_ce_excerpt_pool_inputs in=input_make(header,seed);
    in.limits=limits;rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_DESTINATION);
    in.limits.bss_end=UINT32_MAX;rejected(&in,KUI_ARMADA_CE_EXCERPT_POOL_STAGE);
}
static bool independent_safe(uint32_t physical,uint32_t bytes) {
    uint64_t end=(uint64_t)physical+bytes;
    return bytes && physical>=0x0c000000u && end<=UINT64_C(0x0d000000) &&
        (end<=UINT64_C(0x0cf50000) || physical>=0x0cfd0000u);
}
static void destination_boundaries(void) {
    const uint32_t aliases[]={0u,0x80000000u,0xa0000000u};
    const uint32_t edges[]={0x0cf50000u,0x0cfd0000u};
    for(unsigned alias=0;alias<3u;alias++) for(unsigned edge=0;edge<2u;edge++)
        for(int offset=-16;offset<=16;offset++) for(uint32_t bytes=1u;bytes<=64u;bytes++) {
            uint32_t physical=edges[edge]+(uint32_t)offset;
            CHECK(kui_armada_ce_excerpt_pool_destination_safe(physical|aliases[alias],bytes)==
                  independent_safe(physical,bytes));
        }
    const uint32_t starts[]={0x0bffffffu,0x0c000000u,0x0cf4ffffu,0x0cf50000u,
        0x0cfcffffu,0x0cfd0000u,0x0cffffffu,0x0d000000u};
    const uint32_t sizes[]={0u,1u,4u,0x80000u,0x1000000u,UINT32_MAX};
    for(unsigned alias=0;alias<3u;alias++)
        for(unsigned start=0;start<sizeof(starts)/sizeof(starts[0]);start++)
            for(unsigned size=0;size<sizeof(sizes)/sizeof(sizes[0]);size++)
                CHECK(kui_armada_ce_excerpt_pool_destination_safe(starts[start]|aliases[alias],sizes[size])==
                      independent_safe(starts[start],sizes[size]));
    const uint32_t unknown[]={0u,UINT32_MAX,0x2cfd0000u,0x4cfd0000u,0x6cfd0000u,
        0xccfd0000u,0xecfd0000u,0xa0702800u,0xa0800000u,0xff00001cu};
    for(unsigned i=0;i<sizeof(unknown)/sizeof(unknown[0]);i++)
        CHECK(!kui_armada_ce_excerpt_pool_destination_safe(unknown[i],4u));
    CHECK(kui_armada_ce_excerpt_pool_destination_safe(0x8cfd0000u,0x30000u));
    CHECK(!kui_armada_ce_excerpt_pool_destination_safe(0x8cf50000u,0x80000u));
}
struct capture {
    uint32_t header[21],seed[16];
    unsigned attempts,reads,cancel_calls,fail_at,cancel_after;
};
static void capture_init(struct capture *c) {
    memset(c,0,sizeof(*c));memcpy(c->header,metadata,sizeof(metadata));
    CHECK(kui_armada_ce_excerpt_seed_make(c->seed,pcm_crc));
    c->fail_at=c->cancel_after=UINT32_MAX;
}
static bool capture_read(void *context,uint32_t address,uint32_t *word) {
    struct capture *c=context;unsigned index=c->attempts++;
    assert(index<TOTAL_READS && word);
    uint32_t expected=index<21u?0x8c141420u+index*4u:
        index<37u?0x8cf50000u+(index-21u)*4u:0x8cf50040u+(index-37u)*4u;
    assert(address==expected);
    if(index==c->fail_at) return false;
    *word=index<21u?c->header[index]:index<37u?c->seed[index-21u]:generated_word(index-37u);
    ++c->reads;return true;
}
static bool capture_cancel(void *context) {
    struct capture *c=context;++c->cancel_calls;
    return c->reads>=c->cancel_after;
}
static void capture_unchanged(const struct capture *c,const struct capture *before,
                              const struct kui_armada_ce_excerpt_pool_plan *out,
                              const struct kui_armada_ce_excerpt_pool_plan *old) {
    CHECK(!memcmp(c->header,before->header,sizeof(c->header)));
    CHECK(!memcmp(c->seed,before->seed,sizeof(c->seed)));
    CHECK(!memcmp(out,old,sizeof(*out)));
}
static void callback_capture(void) {
    struct capture c,before;struct kui_armada_ce_excerpt_pool_plan out,old;
    capture_init(&c);before=c;sentinel(&out);
    CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,capture_cancel,&c,&valid_limits,&out)==
          KUI_ARMADA_CE_EXCERPT_POOL_OK);
    CHECK(c.attempts==88237u && c.reads==88237u && c.cancel_calls==88239u);
    CHECK(!memcmp(c.header,before.header,sizeof(c.header)) && !memcmp(c.seed,before.seed,sizeof(c.seed)));
    expected_plan(&out,pcm_crc);
    capture_init(&c);
    CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,NULL,&c,&valid_limits,&out)==
          KUI_ARMADA_CE_EXCERPT_POOL_OK);
    CHECK(c.reads==88237u && !c.cancel_calls);expected_plan(&out,pcm_crc);

    const unsigned fail_at[]={0u,20u,21u,36u,37u,44137u,88236u};
    for(unsigned i=0;i<sizeof(fail_at)/sizeof(fail_at[0]);i++) {
        capture_init(&c);c.fail_at=fail_at[i];before=c;sentinel(&out);old=out;
        CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,capture_cancel,&c,&valid_limits,&out)==
              KUI_ARMADA_CE_EXCERPT_POOL_READ);
        CHECK(c.attempts==fail_at[i]+1u && c.reads==fail_at[i]);
        capture_unchanged(&c,&before,&out,&old);
    }
    const unsigned cancel_after[]={0u,1u,21u,37u,44137u,88236u,88237u};
    for(unsigned i=0;i<sizeof(cancel_after)/sizeof(cancel_after[0]);i++) {
        capture_init(&c);c.cancel_after=cancel_after[i];before=c;sentinel(&out);old=out;
        CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,capture_cancel,&c,&valid_limits,&out)==
              KUI_ARMADA_CE_EXCERPT_POOL_CANCELLED);
        CHECK(c.attempts==cancel_after[i] && c.reads==cancel_after[i]);
        capture_unchanged(&c,&before,&out,&old);
    }
    capture_init(&c);c.header[0]^=1u;before=c;sentinel(&out);old=out;
    CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,capture_cancel,&c,&valid_limits,&out)==
          KUI_ARMADA_CE_EXCERPT_POOL_HEADER);
    CHECK(c.reads==21u);capture_unchanged(&c,&before,&out,&old);
    capture_init(&c);c.seed[15]=1u;before=c;sentinel(&out);old=out;
    CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,capture_cancel,&c,&valid_limits,&out)==
          KUI_ARMADA_CE_EXCERPT_POOL_SEED);
    CHECK(c.reads==37u);capture_unchanged(&c,&before,&out,&old);
    capture_init(&c);c.seed[3]^=1u;before=c;sentinel(&out);old=out;
    CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,capture_cancel,&c,&valid_limits,&out)==
          KUI_ARMADA_CE_EXCERPT_POOL_PCM_CRC);
    CHECK(c.reads==88237u);capture_unchanged(&c,&before,&out,&old);

    capture_init(&c);before=c;sentinel(&out);old=out;
    CHECK(kui_armada_ce_excerpt_pool_capture(NULL,capture_cancel,&c,&valid_limits,&out)==
          KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT);
    CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,capture_cancel,&c,NULL,&out)==
          KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT);
    CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,capture_cancel,&c,&valid_limits,NULL)==
          KUI_ARMADA_CE_EXCERPT_POOL_ARGUMENT);
    struct kui_armada_ce_excerpt_pool_limits limits=valid_limits;
    limits.gd_destination_excluded=false;
    CHECK(kui_armada_ce_excerpt_pool_capture(capture_read,capture_cancel,&c,&limits,&out)==
          KUI_ARMADA_CE_EXCERPT_POOL_DESTINATION);
    CHECK(c.reads==0u);capture_unchanged(&c,&before,&out,&old);
}
int main(void) {
    crc_and_seed_contract();header_and_input_rejections();layout_boundaries();
    destination_boundaries();callback_capture();
    printf("Armada CE fixed excerpt pool: %u checks passed\n",checks);
    return 0;
}
