/* SPDX-License-Identifier: GPL-3.0-only */
/* Same real-core cases are compiled through the ordinary and VC2 stage.
 * Compare their sealed outputs while counting the actual callback traffic. */
#define KUI_VMU_PERSIST_STAGE_MAIN vc2_snapshot_reused_fixture_main
#include "test_vmu_persist_stage.c"
#ifndef KUI_VC2_VMU_PERSIST
#define KUI_VC2_VMU_PERSIST 0
#endif

static void snapshot_check(FILE *out,unsigned recovery,unsigned imported) {
    if(imported)stage_imported_pair_fresh();else stage_fresh();
    if(recovery)stage_seed_real_marker();
    else card.base[123][7]^=0x71u;
    uint8_t old_before[32],old_after[32];
    if(imported)stage_old_pair_digest(old_before);
    assert(!stage_prepare());
    assert(card.work.phase==KUI_VMU_PERSIST_STAGE_SEAL && card.work.session.snapshot_valid);
    assert(card.writes==(recovery?2u:1u) && card.syncs==(recovery?4u:2u));
    assert(card.reads-card.work.filesystem.stats.reads==
        (KUI_VC2_VMU_PERSIST?529u:1055u)+recovery);
    assert(card.identities==(KUI_VC2_VMU_PERSIST?11u:13u)+(recovery?3u:0u));
    assert(!kui_vmu_durable_handoff_validate(&card.handoff));
    assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));
    struct kui_sha256 hash;uint8_t sha[32];kui_sha256_init(&hash);
    kui_sha256_update(&hash,card.base,sizeof(card.base));kui_sha256_digest(&hash,sha);
    assert(!memcmp(sha,card.handoff.base_sha256,sizeof(sha)));
    for(unsigned block=0u;block<256u;block++)
        assert(card.handoff.block_crc[block]==kui_retail_crc32(0u,card.base[block],512u));
    for(unsigned i=0u;i<512u;i++)assert(!card.journal[1][i]);
    if(recovery)for(unsigned i=0u;i<512u;i++)assert(card.base[123][i]==0x79u);
    if(imported) {
        assert(card.work.session.summary.files==13u && card.work.session.summary.used_blocks==196u);
        stage_old_pair_digest(old_after);assert(!memcmp(old_before,old_after,sizeof(old_before)));
    }
    assert(fwrite(&card.handoff,1u,sizeof(card.handoff),out)==sizeof(card.handoff));
    printf("VC2=%u imported=%u recovery=%u fs_reads=%u other_reads=%u writes=%u syncs=%u identities=%u\n",
        KUI_VC2_VMU_PERSIST,imported,recovery,card.work.filesystem.stats.reads,
        card.reads-card.work.filesystem.stats.reads,card.writes,card.syncs,card.identities);
}
static void snapshot_failures(void) {
    stage_fresh();card.base[255][0]=0u;
    assert(stage_prepare()==KUI_VMU_PERSIST_STAGE_SESSION && !card.writes && !card.syncs);
    stage_handoff_zero();
    stage_recovery_and_fresh_admission_first();
    for(unsigned which=0u;which<7u;which++) {
        stage_fresh();
        if(which==0u)card.fail_write=1u;
        if(which==1u)card.fail_sync=1u;
        if(which==2u)card.fail_sync=2u;
        if(which==3u)card.nonzero_final_marker=1u;
        if(which==4u)card.fail_final_read=1u;
        if(which==5u)card.fail_phase_identity=KUI_VMU_PERSIST_STAGE_ZERO_MARKER;
        if(which==6u)card.actual.cid[0]^=1u;
        assert(stage_prepare());stage_handoff_zero();assert(card.work.session.faulted);
    }
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *out=fopen(argv[1],"wb");assert(out);
    for(unsigned imported=0u;imported<2u;imported++)
        for(unsigned recovery=0u;recovery<2u;recovery++)snapshot_check(out,recovery,imported);
    assert(!fclose(out));snapshot_failures();
    puts("PASS real-core stage snapshot equivalence, current CRC/SHA, pending recovery, imported saves, and final fault guards");
    return 0;
}
