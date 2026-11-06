/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the real-volume fixture, including raw device/journal helpers. */
#define main retained_library_main
#include "test_vmu_library.c"
#undef main

static void deletion_image(uint8_t image[131072]) {
    for(unsigned i=0;i<256u;i++) CHECK(!kui_vmu_image_blank_block((uint16_t)i,image+i*512u));
    uint8_t *a=image+253u*512u,*b=a+32u,*fat=image+254u*512u;
    a[0]=0x33u;put16(a+2u,5u);memcpy(a+4u,"DELETE_ME",9u);put16(a+24u,2u);
    put16(fat+5u*2u,9u);put16(fat+9u*2u,0xfffau);
    b[0]=0x33u;put16(b+2u,17u);memcpy(b+4u,"KEEP_ME",7u);put16(b+24u,1u);
    put16(fat+17u*2u,0xfffau);memset(image+5u*512u,0x51,512u);
    memset(image+9u*512u,0x91,512u);memset(image+17u*512u,0x71,512u);
}
static void storage_pin(struct kui_vmu_game_candidates_storage *out) {
    struct kui_vmu_game_card_raw raw={0};CHECK(identity(&volume,&raw));
    memset(out,0,sizeof(*out));memcpy(out->cid,raw.cid,16u);out->card_sectors=raw.card_sectors;
    out->partition_first=volume.start;out->partition_end=(uint64_t)volume.start+volume.count;
    out->high_capacity=raw.high_capacity;
}
static void stop_after_reference_publication(const char *format,...) {
    if(!strcmp(format,"Virtual VMU choices saved (generation %lu)")) cancelled_flag=true;
}
static char cancel_stage_path[96];
static unsigned cancel_stage_poison;
static bool cancel_when_stage_exists(void) {
    FILINFO info;if(f_stat(cancel_stage_path,&info)!=FR_OK) return false;
    if(cancel_stage_poison) {
        char target[120];unsigned mode=cancel_stage_poison;cancel_stage_poison=0;
        snprintf(target,sizeof(target),"%s/%s",cancel_stage_path,mode==1u?"unexpected.bin":"identity.bin");
        uint8_t wrong_identity[16]={0};file_write(target,wrong_identity,mode==1u?4u:16u);
    }
    return true;
}
int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));
    long size=ftell(input);CHECK(size>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)size;
    disk=malloc(disk_bytes);CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));
    mount();paths();bool exfat=fs.fs_type==FS_EXFAT;
    uint8_t *image=malloc(131072u),*expected=malloc(131072u),*result=malloc(131072u);
    CHECK(image && expected && result);deletion_image(image);
    uint8_t profile[4096];unsigned unrelated=create_game("T-OTHER",profile);protect_pair(unrelated,true);
    struct kui_vmu_collection_card snapshot;
    CHECK(!kui_vmu_collection_publish(image,131072u,"Keep backup",&snapshot,log_line,cancelled));
    protect(snapshot.path,131072u);char snap_ready[96];
    snprintf(snap_ready,sizeof(snap_ready),"0:/KUI/vmu/cards/v%04u/ready.bin",snapshot.id);protect(snap_ready,512u);
    struct kui_vmu_game_card_choice choice;uint8_t id[16],nonce[16]={0x81};
    CHECK(!kui_vmu_game_card_import_publish(image,131072u,nonce,&work,&choice,id,log_line,cancelled));
    unsigned selected=choice.pair;char path[96];pair_path(path,selected,"card.vmd");
    struct kui_vmu_game_assignment assigns[2]={0};
    for(unsigned i=0;i<2u;i++) {
        CHECK(kui_vmu_game_card_key(i?"T-SHARE2":"T-SHARE1",assigns[i].key));
        assigns[i].pair=selected;memcpy(assigns[i].identity,id,16u);
    }
    CHECK(!kui_vmu_game_card_assignments_apply(assigns,2u,&work,&choice,log_line,cancelled));
    CHECK(!kui_vmu_game_card_select("T-SHARE1",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0u,&platform,&work,profile,&choice,log_line,cancelled));
    CHECK(!kui_vmu_library_rename(path,"Shared deletion test",id,log_line,cancelled));
    struct kui_vmu_persist_profile p;CHECK(!kui_vmu_persist_profile_decode(&p,profile,sizeof(profile)));
    uint8_t journal[1024];journal_make(journal,&p,17u,0xabu);char journal_path[96];
    pair_path(journal_path,selected,"card.journal");file_write(journal_path,journal,sizeof(journal));
    memcpy(expected,image,131072u);memset(expected+17u*512u,0xab,512u);
    memset(expected+253u*512u,0,32u);put16(expected+254u*512u+5u*2u,0xfffcu);
    put16(expected+254u*512u+9u*2u,0xfffcu);
    struct kui_vmu_game_candidates_storage storage;storage_pin(&storage);
    struct kui_vmu_delete_preview token;writes=0;
    CHECK(!kui_vmu_library_delete_preview(path,id,"DELETE_ME",&token,log_line,cancelled));
    CHECK(!writes && token.ready && token.users==2u && token.blocks==2u && !token.whole_card);
    struct kui_vmu_delete_preview bad=token;bad.identity[0]^=1u;
    CHECK(kui_vmu_library_delete_commit(&bad,&storage,log_line,cancelled)<0 && !writes);
    bad=token;bad.entry[4]^=1u;
    CHECK(kui_vmu_library_delete_commit(&bad,&storage,log_line,cancelled)<0 && !writes);
    struct kui_vmu_game_candidates_storage wrong=storage;wrong.cid[0]^=1u;
    /* A present catalogue is storage-pinned. Bootstrap it before testing a
     * copied-device refusal, retaining both shared legacy assignments. */
    CHECK(!kui_vmu_game_candidates_apply(assigns,2u,&storage,log_line,cancelled));
    CHECK(!kui_vmu_library_delete_preview(path,id,"DELETE_ME",&token,log_line,cancelled));writes=0;
    CHECK(kui_vmu_library_delete_commit(&token,&wrong,log_line,cancelled)<0 && !writes);
    /* A payload edit after confirmation invalidates the whole-image pin even
     * when the chosen directory entry and identity still match exactly. */
    FIL changed;UINT done=0;uint8_t changed_byte=0x61,original_byte=image[100u*512u];
    CHECK(f_open(&changed,path,FA_WRITE)==FR_OK && f_lseek(&changed,100u*512u)==FR_OK &&
        f_write(&changed,&changed_byte,1u,&done)==FR_OK && done==1u && f_sync(&changed)==FR_OK && f_close(&changed)==FR_OK);
    writes=0;CHECK(kui_vmu_library_delete_commit(&token,&storage,log_line,cancelled)<0 && !writes);
    CHECK(f_open(&changed,path,FA_WRITE)==FR_OK && f_lseek(&changed,100u*512u)==FR_OK &&
        f_write(&changed,&original_byte,1u,&done)==FR_OK && done==1u && f_sync(&changed)==FR_OK && f_close(&changed)==FR_OK);
    char foreign[96];pair_path(foreign,selected,"unrelated.bin");file_write(foreign,"keep",4u);writes=0;
    CHECK(kui_vmu_library_delete_preview(path,id,NULL,&bad,log_line,cancelled)<0 && !writes);
    CHECK(f_unlink(foreign)==FR_OK);writes=0;
    cancelled_flag=true;CHECK(kui_vmu_library_delete_commit(&token,&storage,log_line,cancelled)<0 && !writes);
    cancelled_flag=false;
    /* Stop immediately after our stage directory is created. Cleanup may
     * remove only that identity-pinned stage, leaving the original image and
     * journal byte-exact and permitting a freshly confirmed retry. */
    snprintf(cancel_stage_path,sizeof(cancel_stage_path),"0:/KUI/vmu-test/staged-p%04u",selected);
    CHECK(kui_vmu_library_delete_commit(&token,&storage,log_line,cancel_when_stage_exists)==KUI_VMU_LIBRARY_CANCELLED);
    FILINFO cancelled_stage;CHECK(f_stat(cancel_stage_path,&cancelled_stage)==FR_NO_FILE);
    CHECK(file_read(path,result,131072u)==131072u && !memcmp(result,image,131072u));
    uint8_t saved_journal[1024];CHECK(file_read(journal_path,saved_journal,sizeof(saved_journal))==sizeof(saved_journal) &&
        !memcmp(saved_journal,journal,sizeof(journal)));
    protected_unchanged();
    CHECK(!kui_vmu_library_delete_preview(path,id,"DELETE_ME",&token,log_line,cancelled));
    /* Unknown stage contents or a changed identity forbid cancellation
     * cleanup. Neither refusal may touch the original card or silently claim
     * cancellation succeeded. A preexisting stage is never ours to remove. */
    for(unsigned mode=1u;mode<=2u;mode++) {
        cancel_stage_poison=mode;
        CHECK(kui_vmu_library_delete_commit(&token,&storage,log_line,cancel_when_stage_exists)==KUI_VMU_LIBRARY_UNCERTAIN);
        CHECK(f_stat(cancel_stage_path,&cancelled_stage)==FR_OK);
        CHECK(file_read(path,result,131072u)==131072u && !memcmp(result,image,131072u));
        protected_unchanged();char target[120];
        if(mode==1u) {snprintf(target,sizeof(target),"%s/unexpected.bin",cancel_stage_path);CHECK(f_unlink(target)==FR_OK);}
        snprintf(target,sizeof(target),"%s/identity.bin",cancel_stage_path);CHECK(f_unlink(target)==FR_OK);
        CHECK(f_unlink(cancel_stage_path)==FR_OK);
    }
    CHECK(f_mkdir(cancel_stage_path)==FR_OK);char stage_identity_path[120];
    snprintf(stage_identity_path,sizeof(stage_identity_path),"%s/identity.bin",cancel_stage_path);
    file_write(stage_identity_path,id,16u);writes=0;
    CHECK(kui_vmu_library_delete_commit(&token,&storage,log_line,cancelled)==KUI_VMU_LIBRARY_INVALID && !writes);
    CHECK(file_read(stage_identity_path,saved_journal,sizeof(saved_journal))==16u && !memcmp(saved_journal,id,16u));
    CHECK(f_unlink(stage_identity_path)==FR_OK && f_unlink(cancel_stage_path)==FR_OK);
    unmount();uint8_t *seed=malloc(disk_bytes);CHECK(seed);memcpy(seed,disk,disk_bytes);mount();
    writes=0;CHECK(!kui_vmu_library_delete_commit(&token,&storage,log_line,cancelled));unsigned complete_writes=writes;
    CHECK(!kui_vmu_library_read(path,result,log_line,cancelled) && !memcmp(result,expected,131072u));
    CHECK(file_read(journal_path,journal,sizeof(journal))==sizeof(journal));
    for(unsigned i=0;i<sizeof(journal);i++) CHECK(!journal[i]);
    char label[49];CHECK(!kui_vmu_library_label(path,id,label,cancelled) && !strcmp(label,"Shared deletion test"));
    struct kui_vmu_game_candidates_view candidates;
    CHECK(!kui_vmu_game_candidates_list("T-SHARE1",&storage,&candidates,log_line,cancelled) && candidates.count==1u);
    protected_unchanged();unmount();
    /* Every durable write cut of save replacement: the active card is either
     * exact old data (with its original redo), exact new data, or unavailable
     * with the complete old folder still quarantined. Other cards/backups
     * remain byte-identical throughout. No partial image can become active. */
    for(unsigned cut=0;cut<=complete_writes;cut++) {
        memcpy(disk,seed,disk_bytes);fail_after=-1;mount();writes=0;fail_after=(int)cut;
        int deleted=kui_vmu_library_delete_commit(&token,&storage,log_line,cancelled);
        fail_after=-1;unmount();mount();
        int admitted=kui_vmu_library_read(path,result,log_line,cancelled);
        if(!admitted) {
            memcpy(image+17u*512u,expected+17u*512u,512u);
            CHECK(!memcmp(result,image,131072u) || !memcmp(result,expected,131072u));
            if(!deleted) CHECK(!memcmp(result,expected,131072u));
        } else {
            char quarantined[96];snprintf(quarantined,sizeof(quarantined),"0:/KUI/vmu-test/deleted-p%04u/card.vmd",selected);
            CHECK(file_read(quarantined,result,131072u)==131072u);
            deletion_image(image);CHECK(!memcmp(result,image,131072u));
        }
        protected_unchanged();unmount();
    }
    memcpy(disk,seed,disk_bytes);mount();
    /* A selected shared card is detached from both products in one catalogue
     * generation. Empty coverage suppresses the preserved v1 predecessor. */
    CHECK(!kui_vmu_library_delete_preview(path,id,NULL,&token,log_line,cancelled) && token.users==2u);
    writes=0;CHECK(!kui_vmu_library_delete_commit(&token,&storage,log_line,cancelled));
    unsigned card_writes=writes;unmount();
    for(unsigned cut=0;cut<=card_writes;cut++) {
        memcpy(disk,seed,disk_bytes);fail_after=-1;mount();writes=0;fail_after=(int)cut;
        (void)kui_vmu_library_delete_commit(&token,&storage,log_line,cancelled);
        fail_after=-1;unmount();mount();FILINFO present;
        bool exists=f_stat(path,&present)==FR_OK;
        for(unsigned i=0;i<2u;i++) {
            CHECK(!kui_vmu_game_candidates_list(assigns[i].key,&storage,&candidates,log_line,cancelled));
            CHECK(candidates.count<=1u);
            if(!exists) CHECK(candidates.present && !candidates.count);
            if(candidates.count) CHECK(candidates.targets[0].pair==selected && !memcmp(candidates.targets[0].identity,id,16u));
        }
        if(exists) CHECK(!kui_vmu_library_read(path,result,log_line,cancelled));
        protected_unchanged();unmount();
    }
    memcpy(disk,seed,disk_bytes);mount();
    CHECK(!kui_vmu_library_delete_commit(&token,&storage,stop_after_reference_publication,cancelled));
    CHECK(cancelled_flag);cancelled_flag=false;
    FILINFO info;CHECK(f_stat(path,&info)==FR_NO_PATH || f_stat(path,&info)==FR_NO_FILE);
    for(unsigned i=0;i<2u;i++) CHECK(!kui_vmu_game_candidates_list(assigns[i].key,&storage,&candidates,log_line,cancelled) &&
        candidates.present && !candidates.count);
    /* A valid covered-empty product is an intentional deletion. Refuse with
     * an actionable message before old v1 fallback, creation or raw mapping. */
    writes=0;
    for(unsigned i=0;i<2u;i++) {
        memset(profile,0x81,sizeof(profile));
        CHECK(kui_vmu_game_card_select(assigns[i].key,volume.start,(uint64_t)volume.start+volume.count,
            NULL,0u,&platform,&work,profile,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
        CHECK(strstr(choice.problem,"No virtual VMU remains; assign/import a card in VMU tools") && !choice.created && !writes);
        for(unsigned j=0;j<sizeof(profile);j++) CHECK(!profile[j]);
    }
    CHECK(kui_vmu_library_delete_commit(&token,&storage,log_line,cancelled)<0);
    protected_unchanged();
    /* Snapshot save deletion is refused; explicit whole-snapshot deletion is
     * supported independently of active references or physical storage pins. */
    char identity_path[96];snprintf(identity_path,sizeof(identity_path),"0:/KUI/vmu/cards/v%04u/identity.bin",snapshot.id);
    uint8_t snapshot_id[16];CHECK(file_read(identity_path,snapshot_id,16u)==16u);writes=0;
    CHECK(kui_vmu_library_delete_preview(snapshot.path,snapshot_id,"DELETE_ME",&token,log_line,cancelled)<0 && !writes);
    CHECK(!kui_vmu_library_delete_preview(snapshot.path,snapshot_id,NULL,&token,log_line,cancelled) && !writes);
    CHECK(!kui_vmu_library_delete_commit(&token,NULL,log_line,cancelled));
    CHECK(f_stat(snapshot.path,&info)==FR_NO_PATH || f_stat(snapshot.path,&info)==FR_NO_FILE);
    unmount();printf("VMU library deletion (%s): %u checks, %u write boundaries passed\n",exfat?"exFAT":"FAT32",checks,complete_writes+card_writes+2u);
    for(unsigned i=0;i<protected_count;i++) free(protected_files[i].data);
    free(seed);free(image);free(expected);free(result);free(disk);return 0;
}
