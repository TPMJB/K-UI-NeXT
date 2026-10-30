/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the filesystem image fixture while exercising the production stage
 * with both admitted SD transports. Its session I/O is modeled separately
 * from the actual GPIO/protocol loan in test_vmu_only_scif_transport.c. */
#define KUI_VMU_PERSIST_STAGE_MAIN vmu_only_original_stage_suite
#include "test_vmu_persist_stage.c"

int main(void) {
    unsigned checks=0u;
    for(uint32_t transport=KUI_STORAGE_SCIF;transport<=KUI_STORAGE_SCI;transport++) {
        stage_fresh();card.game.storage_transport=transport;
        assert(!kui_vmu_persist_stage_admit(&card.expected,&card.game));++checks;
        assert(!stage_prepare());++checks;
        assert(card.work.phase==KUI_VMU_PERSIST_STAGE_SEAL && card.writes==1u && card.syncs==2u);
        assert(!kui_vmu_durable_handoff_validate(&card.handoff));++checks;
        assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));++checks;

        stage_fresh();card.game.storage_transport=transport;stage_seed_real_marker();
        assert(!stage_prepare());++checks;
        for(unsigned i=0u;i<512u;i++) assert(card.base[123][i]==0x79u);
        assert(card.handoff.block_crc[123]==kui_retail_crc32(0u,card.base[123],512u));++checks;
        for(unsigned i=0u;i<512u;i++) assert(!card.journal[1][i]);

        for(unsigned failure=0u;failure<5u;failure++) {
            stage_fresh();card.game.storage_transport=transport;
            if(failure==0u) card.fail_write=1u;
            if(failure==1u) card.fail_sync=1u;
            if(failure==2u) card.fail_final_read=1u;
            if(failure==3u) card.nonzero_final_marker=1u;
            if(failure==4u) card.fail_phase_identity=KUI_VMU_PERSIST_STAGE_SEAL;
            /* SEAL itself performs no extra identity I/O; use the final
             * ZERO_MARKER identity phase to model a swapped card instead. */
            if(failure==4u) card.fail_phase_identity=KUI_VMU_PERSIST_STAGE_ZERO_MARKER;
            assert(stage_prepare()!=0);stage_handoff_zero();++checks;
        }
        for(unsigned refusal=0u;refusal<5u;refusal++) {
            stage_fresh();card.game.storage_transport=transport;
            if(refusal==0u) card.game.storage_transport=KUI_STORAGE_IDE;
            if(refusal==1u) card.game.reader=KUI_RETAIL_READER_ASYNC;
            if(refusal==2u) card.game.flags|=KUI_RETAIL_IMAGE_CD;
            if(refusal==3u) card.game.flags|=KUI_RETAIL_IMAGE_SCRAMBLED;
            if(refusal==4u) card.game.slots[1].extent.card_lba=lba(20u)+200u;
            assert(stage_prepare()==KUI_VMU_PERSIST_STAGE_MAPPING);++checks;
            assert(!card.reads && !card.writes && !card.syncs);stage_handoff_zero();
        }
    }
    printf("VMU-only prepare: %u SCI/SCIF checkpoint, journal recovery, seal, swapped-card/fault and no-I/O refusal cases passed\n",checks);
    return 0;
}
