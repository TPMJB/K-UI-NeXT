/* SPDX-License-Identifier: GPL-3.0-only */
/* Bounded receipt-only device cuts; retain the accepted wider fixture. */
#define KUI_VMU_HISTORY_TEST_MAIN history_reference_main
#include "test_vmu_history.c"

static void receipt_trial(BYTE kind,DWORD cluster) {
    format(kind,cluster);uint8_t wire[4096];unsigned pair=create_game("MK-51000",wire);
    struct kui_vmu_persist_profile profile;
    CHECK(!kui_vmu_persist_profile_decode(&profile,wire,sizeof(wire)));
    struct kui_vmu_game_selection card={.pair=pair,.selected=true};
    CHECK(kui_vmu_game_card_key("MK-51000",card.product));
    memcpy(card.identity,profile.image_id,16u);CHECK(history_storage(&volume,&card.storage));
    struct kui_vmu_history_view view;
    CHECK(!session_begin(&card,&view));CHECK(view.count==1u);
    uint64_t sequence=view.entries[0].sequence;
    CHECK(!session_begin(&card,&view)); /* Both alternating files now exist. */
    CHECK(!protected_count);protect_pair(pair,true);
    char path[192];history_path(path,sequence,"card.vmd");protect(path,KUI_VMU_IMAGE_BYTES);
    history_path(path,sequence,"record.bin");protect(path,512u);
    uint8_t *seed=malloc(disk_bytes);CHECK(seed);
    unsigned cuts=0u;
    for(unsigned clear=0u;clear<2u;clear++) {
        unmount();memcpy(seed,disk,disk_bytes);mount();writes=0;
        CHECK(!(clear?session_resume(&view):session_begin(&card,&view)));
        unsigned complete=writes;CHECK(complete>0u && complete<=8u);unmount();
        for(unsigned cut=0u;cut<=complete;cut++) {
            memcpy(disk,seed,disk_bytes);fail_after=-1;mount();writes=0;fail_after=(int)cut;
            (void)(clear?session_resume(&view):session_begin(&card,&view));
            fail_after=-1;unmount();mount();protected_unchanged();
            CHECK(session_resume(&view)>=0);protected_unchanged();writes=0;
            CHECK(session_resume(&view)==KUI_VMU_HISTORY_UNCHANGED && !writes);
            ++cuts;unmount();
        }
        memcpy(disk,seed,disk_bytes);mount();
    }
    protected_unchanged();unmount();
    for(unsigned i=0u;i<protected_count;i++)free(protected_files[i].data);
    protected_count=0u;free(seed);free(disk);disk=NULL;
    printf("Receipt-only %s cluster%u: %u publication/clear cuts; original card, journal, identity, profile and history retained\n",
        kind==FM_FAT32?"FAT32":"exFAT",(unsigned)cluster,cuts);
}
int main(void) {
    receipt_trial(FM_FAT32,512u);receipt_trial(FM_EXFAT,4096u);receipt_trial(FM_EXFAT,131072u);
    return 0;
}
