/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the established real FatFs/card/store fixture, including its actual
 * raw ownership admission and write-cut device. No VMU backend is mocked. */
#define main vmu_library_reference_main
#include "test_vmu_library.c"
#undef main
#include "kui/vmu_history.h"
#include <inttypes.h>

static bool changed_storage;
static bool history_storage(void *context,struct kui_vmu_game_candidates_storage *out) {
    struct kui_vmu_game_card_raw raw={0};CHECK(identity(context,&raw));
    memset(out,0,sizeof(*out));memcpy(out->cid,raw.cid,16u);
    out->card_sectors=raw.card_sectors;out->high_capacity=raw.high_capacity?1u:0u;
    out->partition_first=volume.start;out->partition_end=(uint64_t)volume.start+volume.count;
    if(changed_storage)out->cid[0]^=1u;
    return true;
}
static const struct kui_vmu_history_platform history_platform={&volume,history_storage,salt};
static uint64_t history_time=1793700000u;
static int operation(const struct kui_vmu_history_request *request,struct kui_vmu_history_view *view) {
    return kui_vmu_history_apply(request,KUI_VMU_HISTORY_MANUAL,history_time++,&history_platform,view,log_line,cancelled);
}
static void history_path(char out[192],uint64_t sequence,const char *leaf) {
    snprintf(out,192,KUI_VMU_HISTORY_ROOT "/MK-51000/h%016" PRIx64 "/%s",sequence,leaf);
}
static void format(BYTE kind,DWORD cluster) {
    disk_bytes=(2048u+131072u+2048u)*512u;disk=calloc(1,disk_bytes);CHECK(disk);
    disk[446u+4u]=kind==FM_FAT32?0x0cu:7u;
    put32(disk+446u+8u,2048u);put32(disk+446u+12u,131072u);
    disk[510]=0x55;disk[511]=0xaa;kui_media_set(&media);
    MKFS_PARM parameters={.fmt=(BYTE)(kind|FM_SFD),.n_fat=1u,.align=1u,.n_root=512u,.au_size=cluster};
    uint8_t scratch[8192];CHECK(f_mkfs("0:",&parameters,scratch,sizeof(scratch))==FR_OK);
    /* The production disk adapter presents a partition-relative device to
     * FatFs's private formatter. Bind FAT32's hidden-sector field back to
     * the real enclosing MBR in both boot copies before raw admission. */
    if(kind==FM_FAT32) {
        uint8_t *boot=disk+2048u*512u;unsigned backup=boot[50u]|(unsigned)boot[51u]<<8;
        put32(boot+28u,2048u);CHECK(backup && backup<32u);put32(boot+(size_t)backup*512u+28u,2048u);
    }
    mount();
}
static void snapshot_cuts(struct kui_vmu_history_request *request,const char *source,
    const uint8_t expected[KUI_VMU_IMAGE_BYTES]) {
    struct kui_vmu_history_view view;uint8_t *seed=malloc(disk_bytes);CHECK(seed);
    unmount();memcpy(seed,disk,disk_bytes);mount();writes=0;
    CHECK(!operation(request,&view));unsigned complete=writes;CHECK(complete>10u);
    unmount();
    unsigned points[]={0u,1u,2u,5u,10u,complete/2u,complete-2u,complete-1u,complete};
    uint8_t *readback=malloc(KUI_VMU_IMAGE_BYTES);CHECK(readback);
    for(unsigned cut=0;cut<sizeof(points)/sizeof(points[0]);cut++) {
        memcpy(disk,seed,disk_bytes);fail_after=-1;mount();writes=0;fail_after=(int)points[cut];
        int result=operation(request,&view);fail_after=-1;unmount();mount();
        CHECK(file_read(source,readback,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES && !memcmp(readback,expected,KUI_VMU_IMAGE_BYTES));
        struct kui_vmu_history_request list=*request;list.action=KUI_VMU_HISTORY_LIST;
        int listed=operation(&list,&view);
        CHECK(listed==0 && view.known && (view.count==1u || view.count==2u));
        if(result>=0)CHECK(view.count==2u);
        protected_unchanged();
        unmount();
    }
    memcpy(disk,seed,disk_bytes);mount();free(seed);free(readback);
}
static void restore_cuts(const struct kui_vmu_history_request *request,const char *source,
    const uint8_t original[KUI_VMU_IMAGE_BYTES]) {
    struct kui_vmu_history_view view;uint8_t *seed=malloc(disk_bytes),*readback=malloc(KUI_VMU_IMAGE_BYTES),
        *restored=malloc(KUI_VMU_IMAGE_BYTES);CHECK(seed && readback && restored);
    char path[192];history_path(path,request->sequence,"card.vmd");
    CHECK(file_read(path,restored,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES);
    CHECK(!kui_vmu_history_session_begin(&request->card,history_time++,&history_platform,&view,log_line,cancelled));
    unmount();memcpy(seed,disk,disk_bytes);mount();writes=0;
    CHECK(!operation(request,&view) && view.restored && view.card.pair!=request->card.pair);
    unsigned complete=writes;CHECK(complete>10u);
    /* The readiness alternative is exact game.bin, never a way around a
     * damaged import marker or another product's binding. */
    uint8_t binding[64],marker[64]={0};char legacy[96];
    pair_path(legacy,request->card.pair,"game.bin");CHECK(file_read(legacy,binding,sizeof(binding))==sizeof(binding));
    struct kui_vmu_game_candidates_view inspected;
    pair_path(legacy,request->card.pair,"import.bin");file_write(legacy,marker,sizeof(marker));writes=0;
    CHECK(kui_vmu_game_candidates_list(request->card.product,&request->card.storage,&inspected,log_line,cancelled)==
        KUI_VMU_GAME_CANDIDATES_STALE && !writes);
    CHECK(f_unlink(legacy)==FR_OK);
    char foreign[12];CHECK(kui_vmu_game_card_key("OTHER-0",foreign));CHECK(kui_vmu_game_card_binding_encode(foreign,marker));
    pair_path(legacy,request->card.pair,"game.bin");file_write(legacy,marker,sizeof(marker));writes=0;
    CHECK(kui_vmu_game_candidates_list(request->card.product,&request->card.storage,&inspected,log_line,cancelled)==
        KUI_VMU_GAME_CANDIDATES_STALE && !writes);
    file_write(legacy,binding,sizeof(binding));
    /* A historical restore can rescue a damaged current image; it does not
     * need to read/copy that image just to keep the old identity selectable. */
    memcpy(readback,original,KUI_VMU_IMAGE_BYTES);readback[255u*512u]^=1u;file_write(source,readback,KUI_VMU_IMAGE_BYTES);
    CHECK(!operation(request,&view) && view.restored);
    file_write(source,original,KUI_VMU_IMAGE_BYTES);
    struct kui_vmu_game_assignment entry={.pair=request->card.pair},absent={.pair=9999u};
    memcpy(entry.key,request->card.product,12u);memcpy(entry.identity,request->card.identity,16u);
    memcpy(absent.key,entry.key,12u);memcpy(absent.identity,entry.identity,16u);writes=0;
    CHECK(kui_vmu_game_candidates_apply_choose(&entry,1u,&absent,&request->card.storage,log_line,cancelled)==
        KUI_VMU_GAME_CANDIDATES_ARGUMENT && !writes);
    unmount();
    unsigned points[]={0u,1u,5u,10u,complete/3u,complete/2u,complete-2u,complete-1u,complete};
    for(unsigned cut=0;cut<sizeof(points)/sizeof(points[0]);cut++) {
        memcpy(disk,seed,disk_bytes);fail_after=-1;mount();writes=0;fail_after=(int)points[cut];
        int result=operation(request,&view);fail_after=-1;unmount();mount();
        CHECK(file_read(source,readback,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES && !memcmp(readback,original,KUI_VMU_IMAGE_BYTES));
        struct kui_vmu_game_candidates_view candidates;
        int r=kui_vmu_game_candidates_list(request->card.product,&request->card.storage,&candidates,log_line,cancelled);
        CHECK(r==KUI_VMU_GAME_CANDIDATES_OK || r==KUI_VMU_GAME_CANDIDATES_ABSENT ||
              r==KUI_VMU_GAME_CANDIDATES_INVALID);
        if(r==KUI_VMU_GAME_CANDIDATES_OK) {
            CHECK(candidates.count==2u && candidates.default_index<2u);
            bool source_found=false;
            for(unsigned i=0;i<candidates.count;i++) {
                const struct kui_vmu_game_candidate_target *target=candidates.targets+i;
                char current[96];pair_path(current,target->pair,"card.vmd");
                CHECK(!kui_vmu_library_read(current,readback,log_line,cancelled));
                CHECK(!memcmp(readback,i==candidates.default_index?restored:original,KUI_VMU_IMAGE_BYTES));
                if(target->pair==request->card.pair) {
                    CHECK(i!=candidates.default_index && !memcmp(target->identity,request->card.identity,16u));
                    struct kui_vmu_game_card_choice choice;uint8_t wire[4096];
                    CHECK(kui_vmu_game_card_select_exact(request->card.product,target,&request->card.storage,
                        volume.start,(uint64_t)volume.start+volume.count,NULL,0u,&platform,&work,wire,&choice,
                        log_line,cancelled)==KUI_VMU_GAME_CARD_READY && choice.pair==request->card.pair);
                    source_found=true;
                }
            }
            CHECK(source_found);
            /* The old pair remains a member, so an outstanding receipt still
             * recovers and clears after a manual restore selected another. */
            CHECK(kui_vmu_history_session_resume(history_time++,&history_platform,&view,log_line,cancelled)>=0);
        }
        if(result>=0)CHECK(r==KUI_VMU_GAME_CANDIDATES_OK);
        protected_unchanged();
        unmount();
    }
    memcpy(disk,seed,disk_bytes);mount();free(seed);free(readback);free(restored);
}
static int session_begin(const struct kui_vmu_game_selection *card,struct kui_vmu_history_view *view) {
    return kui_vmu_history_session_begin(card,history_time++,&history_platform,view,log_line,cancelled);
}
static int session_resume(struct kui_vmu_history_view *view) {
    return kui_vmu_history_session_resume(history_time++,&history_platform,view,log_line,cancelled);
}
static void receipt_cuts(const struct kui_vmu_game_selection *card,const char *source,bool clear) {
    struct kui_vmu_history_view view;uint8_t *seed=malloc(disk_bytes),*before=malloc(KUI_VMU_IMAGE_BYTES),*after=malloc(KUI_VMU_IMAGE_BYTES);
    CHECK(seed && before && after && file_read(source,before,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES);
    CHECK(!protected_count);protect_pair(card->pair,true);
    if(clear)CHECK(!session_begin(card,&view));
    unmount();memcpy(seed,disk,disk_bytes);mount();writes=0;
    CHECK(!(clear?session_resume(&view):session_begin(card,&view)));unsigned complete=writes;CHECK(complete>0u);unmount();
    unsigned points[]={0u,1u,complete/2u,complete-1u,complete};
    for(unsigned cut=0;cut<sizeof(points)/sizeof(points[0]);cut++) {
        memcpy(disk,seed,disk_bytes);fail_after=-1;mount();writes=0;fail_after=(int)points[cut];
        (void)(clear?session_resume(&view):session_begin(card,&view));
        fail_after=-1;unmount();mount();
        CHECK(file_read(source,after,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES && !memcmp(before,after,KUI_VMU_IMAGE_BYTES));
        CHECK(session_resume(&view)>=0);writes=0;
        CHECK(session_resume(&view)==KUI_VMU_HISTORY_UNCHANGED && !writes);
        protected_unchanged();
        unmount();
    }
    memcpy(disk,seed,disk_bytes);mount();if(clear)CHECK(session_resume(&view)>=0);
    for(unsigned i=0;i<protected_count;i++)free(protected_files[i].data);
    protected_count=0u;
    free(seed);free(before);free(after);
}
static void automatic_history(const struct kui_vmu_history_request *request,const char *source,
    const char *journal_path,const struct kui_vmu_persist_profile *profile) {
    struct kui_vmu_history_view view;uint8_t journal[1024],checked[1024],receipt[2][512],after[512];
    CHECK(!session_begin(&request->card,&view));
    journal_make(journal,profile,100u,0x77);file_write(journal_path,journal,sizeof(journal));
    uint8_t foreign[1024];memcpy(foreign,journal,sizeof(foreign));foreign[512u+16u]^=1u;
    put32(foreign+1020u,kui_crc32(0,foreign+512u,508u));file_write(journal_path,foreign,sizeof(foreign));writes=0;
    CHECK(session_resume(&view)<0 && !writes && strstr(view.notice,"receipt retained"));
    file_write(journal_path,journal,sizeof(journal));
    CHECK(!session_resume(&view) && view.known && view.entries[0].reason==KUI_VMU_HISTORY_AFTER_RETURN);
    CHECK(strstr(view.entries[0].label,"Previous session"));
    CHECK(file_read(journal_path,checked,sizeof(checked))==sizeof(checked) && !memcmp(journal,checked,sizeof(journal)));
    CHECK(!session_begin(&request->card,&view));
    static const char *const receipts[]={KUI_VMU_HISTORY_ROOT "/session-a.bin",KUI_VMU_HISTORY_ROOT "/session-b.bin"};
    for(unsigned i=0;i<2u;i++)CHECK(file_read(receipts[i],receipt[i],512u)==512u);
    changed_storage=true;writes=0;CHECK(session_resume(&view)==KUI_VMU_HISTORY_STALE && !writes);changed_storage=false;
    for(unsigned i=0;i<2u;i++)CHECK(file_read(receipts[i],after,512u)==512u && !memcmp(receipt[i],after,512u));
    char id_path[96];pair_path(id_path,request->card.pair,"identity.bin");uint8_t id[16];
    memcpy(id,request->card.identity,16u);id[0]^=1u;file_write(id_path,id,sizeof(id));writes=0;
    CHECK(session_resume(&view)==KUI_VMU_HISTORY_STALE && !writes);
    file_write(id_path,request->card.identity,16u);
    for(unsigned i=0;i<2u;i++){memcpy(after,receipt[i],512u);after[508]^=1u;file_write(receipts[i],after,512u);}
    writes=0;CHECK(session_resume(&view)==KUI_VMU_HISTORY_INVALID && !writes);
    for(unsigned i=0;i<2u;i++)file_write(receipts[i],receipt[i],512u);
    CHECK(!session_resume(&view));writes=0;CHECK(session_resume(&view)==KUI_VMU_HISTORY_UNCHANGED && !writes);
    memset(journal,0,sizeof(journal));file_write(journal_path,journal,sizeof(journal));
    receipt_cuts(&request->card,source,false);receipt_cuts(&request->card,source,true);
}
static bool boundary_seen,boundary_fail;
static unsigned boundary_pair,boundary_raw_calls,boundary_precreates;
static int safe_boundary(void *context,enum kui_vmu_game_card_boundary_phase phase,
    const char *product,unsigned pair,const uint8_t id[16],
    const struct kui_vmu_game_candidates_storage *storage_pin,kui_log_fn log,kui_cancel_fn cancel) {
    CHECK(context==&boundary_seen && !raw_open && log==log_line && cancel==cancelled);
    if(phase==KUI_VMU_GAME_CARD_BEFORE_CREATE) {
        CHECK(!pair && !id);++boundary_precreates;
        return kui_vmu_history_allocation_mounted(cancel)>=0?KUI_VMU_GAME_CARD_READY:KUI_VMU_GAME_CARD_FAILED;
    }
    CHECK(phase==KUI_VMU_GAME_CARD_RESOLVED && pair && id);
    boundary_seen=true;boundary_pair=pair;
    if(boundary_fail)return KUI_VMU_GAME_CARD_FAILED;
    struct kui_vmu_game_selection card={.pair=pair,.storage=*storage_pin,.selected=true};
    CHECK(kui_vmu_game_card_key(product,card.product));memcpy(card.identity,id,16u);
    struct kui_vmu_history_view view;return session_begin(&card,&view)>=0?KUI_VMU_GAME_CARD_READY:KUI_VMU_GAME_CARD_FAILED;
}
static bool boundary_raw_begin(void *context,struct kui_vmu_game_card_raw *out) {
    CHECK(boundary_seen);++boundary_raw_calls;return raw_begin(context,out);
}
static void actual_boundary(void) {
    struct kui_vmu_game_card_platform guarded=platform;guarded.raw_begin=boundary_raw_begin;
    struct kui_vmu_game_card_choice choice;uint8_t wire[4096];boundary_seen=boundary_fail=false;boundary_raw_calls=0u;
    CHECK(kui_vmu_game_card_select_boundary("HIST-NEW",NULL,NULL,volume.start,(uint64_t)volume.start+volume.count,
        NULL,0u,&guarded,&work,wire,&choice,safe_boundary,&boundary_seen,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    CHECK(boundary_seen && boundary_pair==choice.pair && boundary_raw_calls==1u && choice.created);
    struct kui_vmu_persist_profile profile;CHECK(!kui_vmu_persist_profile_decode(&profile,wire,sizeof(wire)));
    struct kui_vmu_game_candidate_target target={.pair=choice.pair};memcpy(target.identity,profile.image_id,16u);
    struct kui_vmu_game_candidates_storage storage_pin;CHECK(history_storage(&volume,&storage_pin));
    boundary_fail=true;boundary_seen=false;unsigned calls=boundary_raw_calls;writes=0;
    CHECK(kui_vmu_game_card_select_boundary("HIST-NEW",&target,&storage_pin,volume.start,(uint64_t)volume.start+volume.count,
        NULL,0u,&guarded,&work,wire,&choice,safe_boundary,&boundary_seen,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(boundary_seen && boundary_raw_calls==calls && !writes);
    struct kui_vmu_history_view view;CHECK(!session_resume(&view));boundary_fail=false;
}
static void allocation_before_create(void) {
    format(FM_EXFAT,4096u);uint8_t wire[4096];unsigned pair=create_game("ALLOC-0",wire);
    struct kui_vmu_persist_profile profile;CHECK(!kui_vmu_persist_profile_decode(&profile,wire,sizeof(wire)));
    struct kui_vmu_history_request request={.action=KUI_VMU_HISTORY_CHECKPOINT};
    CHECK(kui_vmu_game_card_key("ALLOC-0",request.card.product));request.card.pair=pair;request.card.selected=true;
    memcpy(request.card.identity,profile.image_id,16u);CHECK(history_storage(&volume,&request.card.storage));
    struct kui_vmu_history_view view;CHECK(!operation(&request,&view) && view.count==1u);
    char path[192];snprintf(path,sizeof(path),KUI_VMU_HISTORY_ROOT "/ALLOC-0/h%016" PRIx64 "/card.vmd",view.entries[0].sequence);
    FIL file;CHECK(f_open(&file,path,FA_READ)==FR_OK);uint32_t cluster=file.obj.sclust;
    CHECK(cluster>=2u && f_close(&file)==FR_OK);
    uint32_t bit=cluster-2u;size_t bitmap=((size_t)volume.start+fs.bitbase)*512u+bit/8u;
    size_t image_sector=((size_t)volume.start+fs.database+(size_t)(cluster-2u)*fs.csize)*512u;
    uint8_t before[512];memcpy(before,disk+image_sector,sizeof(before));
    /* Force the next allocation toward an existing history cluster whose
     * bitmap was damaged. Admission must refuse before creating any file. */
    disk[bitmap]&=(uint8_t)~(1u<<(bit%8u));unmount();mount();fs.last_clst=cluster-1u;
    writes=0;boundary_seen=false;unsigned precreates=boundary_precreates;
    struct kui_vmu_game_card_choice choice;
    CHECK(kui_vmu_game_card_select_boundary("ALLOC-NEW",NULL,NULL,volume.start,(uint64_t)volume.start+volume.count,
        NULL,0u,&platform,&work,wire,&choice,safe_boundary,&boundary_seen,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
    CHECK(!writes && !boundary_seen && boundary_precreates==precreates+1u &&
        !memcmp(before,disk+image_sector,sizeof(before)));
    unmount();free(disk);disk=NULL;
}
static void trial(BYTE kind,DWORD cluster) {
    format(kind,cluster);uint8_t profile_wire[4096];unsigned pair=create_game("MK-51000",profile_wire);
    struct kui_vmu_persist_profile profile;CHECK(!kui_vmu_persist_profile_decode(&profile,profile_wire,sizeof(profile_wire)));
    struct kui_vmu_history_request request={.action=KUI_VMU_HISTORY_LIST};
    CHECK(kui_vmu_game_card_key("MK-51000",request.card.product));request.card.pair=pair;
    memcpy(request.card.identity,profile.image_id,16u);request.card.selected=true;
    CHECK(history_storage(&volume,&request.card.storage));
    struct kui_vmu_history_view view;writes=0;CHECK(!operation(&request,&view) && view.known && !view.count && !writes);
    char source[96],journal_path[96],path[192];pair_path(source,pair,"card.vmd");pair_path(journal_path,pair,"card.journal");
    uint8_t *original=malloc(KUI_VMU_IMAGE_BYTES),*image=malloc(KUI_VMU_IMAGE_BYTES);CHECK(original && image);
    CHECK(file_read(source,original,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES);
    actual_store_protocol(source,&profile,original,image);
    /* A committed redo block not yet in VMD belongs in the history image.
     * The old VMD and its journal remain byte-identical after checkpointing. */
    uint8_t journal[1024],checked[1024];journal_make(journal,&profile,100u,0x5au);file_write(journal_path,journal,sizeof(journal));
    request.action=KUI_VMU_HISTORY_CHECKPOINT;CHECK(!operation(&request,&view) && view.changed && view.count==1u);
    uint64_t first=view.entries[0].sequence;history_path(path,first,"card.vmd");
    CHECK(file_read(path,image,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES);
    CHECK(!memcmp(image,original,100u*512u));for(unsigned i=0;i<512u;i++)CHECK(image[100u*512u+i]==0x5au);
    CHECK(file_read(source,image,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES && !memcmp(image,original,KUI_VMU_IMAGE_BYTES));
    CHECK(file_read(journal_path,checked,sizeof(checked))==sizeof(checked) && !memcmp(checked,journal,sizeof(journal)));
    writes=0;CHECK(operation(&request,&view)==KUI_VMU_HISTORY_UNCHANGED && !writes && view.count==1u);
    uint8_t damaged[1024];memcpy(damaged,journal,sizeof(damaged));damaged[512u+16u]^=1u;
    put32(damaged+1020u,kui_crc32(0,damaged+512u,508u));file_write(journal_path,damaged,sizeof(damaged));writes=0;
    CHECK(operation(&request,&view)<0 && !writes);
    memset(journal,0,sizeof(journal));file_write(journal_path,journal,sizeof(journal));
    /* Snapshot publication cuts preserve both the original and the preceding
     * complete version. A pending short file never becomes a listed version. */
    protect_pair(pair,true);history_path(path,first,"card.vmd");protect(path,KUI_VMU_IMAGE_BYTES);
    history_path(path,first,"record.bin");protect(path,512u);
    snapshot_cuts(&request,source,original);
    CHECK(!operation(&request,&view) && view.count==2u);
    struct kui_vmu_history_request restore_request;
    CHECK(kui_vmu_history_choose(&view,1u,KUI_VMU_HISTORY_RESTORE,&restore_request));
    restore_cuts(&restore_request,source,original);
    for(unsigned i=0;i<protected_count;i++)free(protected_files[i].data);
    protected_count=0u;
    /* Damage a snapshot payload, not the active card: omit it and refuse an
     * already-selected restore. Do not silently fall through to another row. */
    history_path(path,restore_request.sequence,"card.vmd");CHECK(file_read(path,image,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES);
    image[100u*512u]^=1u;file_write(path,image,KUI_VMU_IMAGE_BYTES);writes=0;
    request.action=KUI_VMU_HISTORY_LIST;CHECK(!operation(&request,&view) && view.damaged==1u && view.count==1u && !writes);
    CHECK(operation(&restore_request,&view)<0 && !writes);image[100u*512u]^=1u;file_write(path,image,KUI_VMU_IMAGE_BYTES);
    request.card.identity[0]^=1u;writes=0;CHECK(operation(&request,&view)==KUI_VMU_HISTORY_STALE && !writes);request.card.identity[0]^=1u;
    changed_storage=true;CHECK(operation(&request,&view)==KUI_VMU_HISTORY_STALE && !writes);changed_storage=false;
    /* Verified explicit export uses a plain VMD and readable metadata under
     * the HTTP export root. No active pair/journal is exposed or rewritten. */
    request.action=KUI_VMU_HISTORY_EXPORT;CHECK(!operation(&request,&view) && view.exported);
    CHECK(!strncmp(view.export_path,KUI_VMU_HISTORY_EXPORT_ROOT "/",strlen(KUI_VMU_HISTORY_EXPORT_ROOT)+1u));
    CHECK(file_read(view.export_path,image,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES && !memcmp(image,original,KUI_VMU_IMAGE_BYTES));
    /* Retention removes only completed history-owned directories after a new
     * verified version exists. The active source remains the latest data. */
    request.action=KUI_VMU_HISTORY_CHECKPOINT;
    for(unsigned version=1;version<=10u;version++) {
        memcpy(image,original,KUI_VMU_IMAGE_BYTES);image[100u*512u]=(uint8_t)version;file_write(source,image,KUI_VMU_IMAGE_BYTES);
        CHECK(!operation(&request,&view) && view.count<=KUI_VMU_HISTORY_KEEP);
        for(unsigned i=1;i<view.count;i++)CHECK(view.entries[i-1u].sequence>view.entries[i].sequence);
    }
    CHECK(view.count==KUI_VMU_HISTORY_KEEP);
    CHECK(file_read(source,image,KUI_VMU_IMAGE_BYTES)==KUI_VMU_IMAGE_BYTES && image[100u*512u]==10u);
    cancelled_flag=true;writes=0;CHECK(operation(&request,&view)==KUI_VMU_HISTORY_CANCELLED && !writes);cancelled_flag=false;
    automatic_history(&request,source,journal_path,&profile);actual_boundary();
    unmount();free(original);free(image);free(disk);disk=NULL;
    printf("VMU history %s cluster%u: redo, dedup, snapshot/restore cuts, identity/corrupt receipts, receipt publication/clear cuts, pre-raw resolved-card boundary, export and retention passed\n",kind==FM_FAT32?"FAT32":"exFAT",(unsigned)cluster);
}
#ifndef KUI_VMU_HISTORY_TEST_MAIN
#define KUI_VMU_HISTORY_TEST_MAIN main
#endif
int KUI_VMU_HISTORY_TEST_MAIN(void) {
    trial(FM_FAT32,512u);trial(FM_EXFAT,4096u);trial(FM_EXFAT,131072u);
    allocation_before_create();
    printf("VMU history: %u checks passed on real FatFs images; private formatter, no independent fsck\n",checks);
    return 0;
}
