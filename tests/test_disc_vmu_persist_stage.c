/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent sparse exFAT card model from the image-stage suite. Physical
 * disc preparation supplies no retail manifest or SD game extents. */
#define KUI_VMU_PERSIST_STAGE_MAIN disc_vmu_original_image_stage_suite
#include "test_vmu_persist_stage.c"

static int disc_prepare(uint32_t transport) {
    struct kui_vmu_persist_session_ops ops=stage_ops();
    return kui_vmu_persist_disc_stage_prepare(&card.expected,transport,&ops,
        &card.work,&card.handoff);
}
int main(void) {
    unsigned checks=0u;
    for(uint32_t transport=KUI_STORAGE_SCIF;transport<=KUI_STORAGE_SCI;transport++) {
        stage_fresh();memset(&card.game,0xa5,sizeof(card.game));
        card.base[123][7]=0x71u;
        assert(!kui_vmu_persist_disc_stage_admit(&card.expected,transport));++checks;
        assert(!disc_prepare(transport));++checks;
        assert(card.work.phase==KUI_VMU_PERSIST_STAGE_SEAL && card.writes==1u && card.syncs==2u);
        assert(!kui_vmu_durable_handoff_validate(&card.handoff));++checks;
        assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));++checks;
        assert(card.handoff.block_crc[123]==kui_retail_crc32(0u,card.base[123],512u));++checks;
        for(unsigned i=0u;i<512u;i++) assert(!card.journal[1][i]);

        stage_fresh();stage_seed_real_marker();
        assert(!disc_prepare(transport));++checks;
        assert(card.writes==2u && card.base[123][0]==0x79u);
        assert(card.handoff.block_crc[123]==kui_retail_crc32(0u,card.base[123],512u));++checks;
        for(unsigned i=0u;i<512u;i++) assert(!card.journal[1][i]);

        stage_imported_pair_fresh();uint8_t before[32],after[32];
        stage_old_pair_digest(before);assert(!disc_prepare(transport));++checks;
        stage_old_pair_digest(after);assert(!memcmp(before,after,sizeof(before)));++checks;
        assert(card.work.session.summary.files==13u && card.work.session.summary.used_blocks==196u);

        for(unsigned failure=0u;failure<9u;failure++) {
            stage_fresh();memset(&card.handoff,0xa5,sizeof(card.handoff));
            if(failure==0u) card.fail_write=1u;
            if(failure==1u) card.fail_sync=1u;
            if(failure==2u) card.fail_sync=2u;
            if(failure==3u) card.fail_final_read=1u;
            if(failure==4u) card.nonzero_final_marker=1u;
            if(failure==5u) card.fail_phase_identity=KUI_VMU_PERSIST_STAGE_ZERO_MARKER;
            if(failure==6u) card.actual.cid[0]^=1u;
            if(failure==7u) {stage_seed_real_marker();file_entry(disk.dirs[3]+96u,"card.journal",32u,62u,1024u,3u);allocated(62u,1);}
            if(failure==8u) {allocated(60u,0);stage_seed_real_marker();}
            assert(disc_prepare(transport));stage_handoff_zero();++checks;
            if(failure>=6u) assert(!card.writes && !card.syncs);
        }
        for(unsigned refusal=0u;refusal<4u;refusal++) {
            stage_fresh();uint32_t selected=transport;
            if(refusal==0u) selected=KUI_STORAGE_IDE;
            if(refusal==1u) selected=UINT32_MAX;
            if(refusal==2u) ++card.expected.partition_end;
            if(refusal==3u) card.expected.files[0].useful[0].blocks=0u;
            assert(disc_prepare(selected)==KUI_VMU_PERSIST_STAGE_MAPPING);++checks;
            assert(!card.reads && !card.writes && !card.syncs && !card.identities);stage_handoff_zero();
        }
        stage_fresh();assert(!disc_prepare(transport));
        unsigned calls=card.reads+card.writes+card.syncs+card.identities;
        assert(disc_prepare(transport)==KUI_VMU_PERSIST_STAGE_ARGUMENT);++checks;
        assert(card.reads+card.writes+card.syncs+card.identities==calls);stage_handoff_zero();
    }
    printf("Physical-disc exFAT preparation: %u SCI/SCIF seal, exact maps, current contents, recovery, sibling preservation and refusal cases passed\n",checks);
    return 0;
}
