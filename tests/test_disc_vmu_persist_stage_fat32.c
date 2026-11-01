/* SPDX-License-Identifier: GPL-3.0-only */
/* This FAT32 oracle permits writes only to the selected256+2 useful sectors.
 * It is independent of the exFAT stage fixture and the production parser. */
#define main disc_vmu_original_fat32_session_suite
#include "test_vmu_persist_session_adversarial.c"
#undef main
#include "kui/vmu_persist_stage.h"
#include "kui/retail_image.h"

static struct kui_vmu_persist_stage_workspace disc_work;
static struct kui_vmu_durable_handoff disc_handoff;
static int disc_prepare(uint32_t transport) {
    struct kui_vmu_persist_session_ops ops=operations();
    disk.active_session=&disc_work.session;
    return kui_vmu_persist_disc_stage_prepare(&disk.profile,transport,&ops,&disc_work,&disc_handoff);
}
static void disc_fresh(void) {
    fresh();memset(&disc_work,0,sizeof(disc_work));memset(&disc_handoff,0xa5,sizeof(disc_handoff));
}
static void handoff_zero(void) {
    const uint8_t *bytes=(const uint8_t *)&disc_handoff;
    for(size_t i=0u;i<sizeof(disc_handoff);i++) assert(!bytes[i]);
}
int main(void) {
    unsigned checks=0u;
    for(uint32_t transport=KUI_STORAGE_SCIF;transport<=KUI_STORAGE_SCI;transport++) {
        disc_fresh();assert(!kui_vmu_persist_disc_stage_admit(&disk.profile,transport));++checks;
        assert(!disc_prepare(transport));++checks;
        assert(disc_work.phase==KUI_VMU_PERSIST_STAGE_SEAL && disk.writes==1u && disk.syncs==2u);
        assert(!kui_vmu_durable_handoff_validate(&disc_handoff));++checks;
        assert(!kui_vmu_persist_stage_handoff_matches(&disk.profile,&disc_handoff));++checks;
        assert(disc_handoff.base[0].lba==cluster_lba(BASE_FIRST));
        assert(disc_handoff.journal[0].lba==cluster_lba(JOURNAL));
        assert(!memcmp(before,disk.durable,sizeof(before)));++checks;

        disc_fresh();seed_pending(0u,0u,1u);
        assert(!disc_prepare(transport));++checks;
        assert(disk.writes==2u && disk.durable[255][0]==0x55u && disk.durable[255][0x30u]==0x21u);
        assert(disc_handoff.block_crc[255]==kui_retail_crc32(0u,disk.durable[255],512u));++checks;
        for(unsigned i=0u;i<512u;i++) assert(!disk.durable[257][i]);

        for(unsigned failure=0u;failure<4u;failure++) {
            disc_fresh();
            if(failure==0u) disk.actual_identity.cid[0]^=1u;
            if(failure==1u) disk.identity[0]^=1u;
            if(failure==2u) disk.fat[JOURNAL]=0u;
            if(failure==3u) disk.dirs[3][160u+26u]=(uint8_t)BASE_FIRST;
            assert(disc_prepare(transport));++checks;handoff_zero();
            assert(!disk.writes && !disk.syncs && !memcmp(before,disk.durable,sizeof(before)));
        }
        disc_fresh();assert(disc_prepare(KUI_STORAGE_IDE)==KUI_VMU_PERSIST_STAGE_MAPPING);++checks;
        assert(!disk.calls && !disk.reads && !disk.writes && !disk.syncs);handoff_zero();
    }
    printf("Physical-disc FAT32 preparation: %u SCI/SCIF seal, physical-map recovery, byte preservation and pre-write ownership refusals passed\n",checks);
    return 0;
}
