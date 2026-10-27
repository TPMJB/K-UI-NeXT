/* SPDX-License-Identifier: GPL-3.0-only */
#define KUI_VMU_PERSIST_STAGE_MAIN armada_ce_vmu_persist_fixture_main
#include "test_vmu_persist_stage.c"
#include "kui/armada_ce_vmu_persist_stage.h"
#include "kui/armada_ce_vmu_sd_loan.h"

static struct kui_armada_ce_vmu_persist_stage_workspace ce_work;
static void ce_fresh(void) {
    stage_fresh();memset(&ce_work,0,sizeof(ce_work));
    card.game.reader=KUI_RETAIL_READER_ASYNC;
    /* The reused memory-card fixture's fault injector follows its native
     * workspace. CE uses a separate workspace and no injection by default. */
    card.work.phase=UINT32_MAX;
}
static int ce_prepare(void) {
    struct kui_vmu_persist_session_ops ops=stage_ops();
    return kui_armada_ce_vmu_persist_stage_prepare(&card.expected,&card.game,&ops,
        &ce_work,&card.handoff);
}
static void ce_serialized_preparation(void) {
    ce_fresh();struct kui_retail_manifest before=card.game;
    card.base[123][7]=0x71u;
    assert(!ce_prepare());
    assert(!memcmp(&card.game,&before,sizeof(before)));
    before.reader=KUI_RETAIL_READER_STANDARD;
    assert(!memcmp(&ce_work.serialized,&before,sizeof(before)));
    assert(ce_work.started && ce_work.core.phase==KUI_VMU_PERSIST_STAGE_SEAL);
    assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));
    assert(card.handoff.block_crc[123]==kui_retail_crc32(0,card.base[123],512u));
    assert(card.writes==1u && card.syncs==2u);
    unsigned io=card.reads+card.writes+card.identities+card.syncs;
    assert(ce_prepare()==KUI_VMU_PERSIST_STAGE_ARGUMENT);
    assert(io==card.reads+card.writes+card.identities+card.syncs);stage_handoff_zero();
}
static void ce_recovery_and_refusal(void) {
    ce_fresh();stage_seed_real_marker();assert(!ce_prepare());
    assert(card.writes==2u && card.base[123][0]==0x79u);
    assert(!kui_vmu_persist_stage_handoff_matches(&card.expected,&card.handoff));
    for(unsigned mode=0u;mode<5u;mode++) {
        ce_fresh();
        if(mode==0u) card.game.reader=KUI_RETAIL_READER_STANDARD;
        if(mode==1u) card.game.reader=KUI_RETAIL_READER_ASYNC_EAGER;
        if(mode==2u) card.game.storage_transport=KUI_STORAGE_SCIF;
        if(mode==3u) card.game.slots[1].extent.card_lba=lba(20u)+200u;
        if(mode==4u) card.game.slots[1].extent.card_lba=lba(60u)+2u;
        assert(ce_prepare()==KUI_VMU_PERSIST_STAGE_MAPPING);
        assert(!card.reads && !card.writes && !card.identities && !card.syncs);stage_handoff_zero();
    }
    ce_fresh();file_entry(disk.dirs[3]+96u,"card.journal",32u,62u,1024u,3u);allocated(62u,1);
    assert(ce_prepare()==KUI_VMU_PERSIST_STAGE_SESSION);
    assert(!card.writes && !card.syncs);stage_handoff_zero();
    ce_fresh();card.fail_write=1u;assert(ce_prepare());stage_handoff_zero();
    ce_fresh();card.actual.cid[0]^=1u;assert(ce_prepare());
    assert(!card.writes && !card.syncs);stage_handoff_zero();
}
static void ce_service_descriptor(void) {
    uint8_t resident[256]={0};struct kui_armada_ce_vmu_runtime_services services;
    uint32_t descriptor[5]={KUI_ARMADA_CE_VMU_LOAN_DESCRIPTOR_MAGIC,1u,20u,
        0x8c008380u,0x8c0083a0u};
    for(unsigned stream=0u;stream<2u;stream++) {
        memset(resident,0,sizeof(resident));size_t offset=52u+4u*stream;
        memcpy(resident+offset,descriptor,sizeof(descriptor));
        assert(!kui_armada_ce_vmu_persist_services_prepare(resident,sizeof(resident),stream,&services));
        assert(!kui_armada_ce_vmu_runtime_services_validate(&services));
        assert(services.loan_begin==descriptor[3] && services.loan_end==descriptor[4]);
        assert(kui_armada_ce_vmu_persist_services_prepare(resident,offset+19u,stream,&services));
        for(unsigned word=0u;word<5u;word++) {
            uint32_t bad[5];memcpy(bad,descriptor,sizeof(bad));
            bad[word]^=word<3u?1u:0x20000000u;
            memcpy(resident+offset,bad,sizeof(bad));
            assert(kui_armada_ce_vmu_persist_services_prepare(resident,sizeof(resident),stream,&services));
            for(size_t i=0u;i<sizeof(services);i++) assert(!((uint8_t *)&services)[i]);
        }
        const uint32_t invalid[]={0x8c008333u,0x8c008334u,0x8c0083a0u,0x8c008400u,
            0x8c00d800u,0xac008380u,0x01dd6200u};
        for(unsigned i=0u;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
            uint32_t bad[5];memcpy(bad,descriptor,sizeof(bad));bad[3]=invalid[i];
            memcpy(resident+offset,bad,sizeof(bad));
            assert(kui_armada_ce_vmu_persist_services_prepare(resident,sizeof(resident),stream,&services));
        }
    }
}
int main(int argc,char **argv) {
    ce_serialized_preparation();ce_recovery_and_refusal();ce_service_descriptor();
    if(argc==2) {
        /* Explicit synthetic fixture output, solely for native layout builds.
         * It has no user card CID or allocation and cannot be used on hardware. */
        ce_fresh();uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES];
        assert(!kui_vmu_persist_profile_encode(&card.expected,wire));
        FILE *out=fopen(argv[1],"wb");assert(out);
        assert(fwrite(wire,1u,sizeof(wire),out)==sizeof(wire));assert(!fclose(out));
    } else assert(argc==1);
    puts("PASS CE pre-NK persistent preparation: serialized ASYNC admission, real recovery/checkpoint, immutable extents and bounded sealed services");
    return 0;
}
