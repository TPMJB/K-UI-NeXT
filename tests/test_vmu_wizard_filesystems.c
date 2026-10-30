/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual wizard, subset builder, immutable collection, playable-card service,
 * candidate catalogue and FatFs. Only physical capture, game discovery and
 * exclusive host storage ownership replace console devices. */
#define main retained_library_regression_main
#include "test_vmu_library.c"
#undef main
#include "kui/vmu_wizard.h"
#include "kui/vmu_image_pack.h"
#include "kui/vmu_game_candidates.h"
#include "kui/games_storage.h"
#include "kui/vmu_backing.h"
#include "kui/clock.h"
#include "kui/hash.h"

static uint8_t physical_sources[2][KUI_VMU_IMAGE_BYTES];
static uint8_t physical_hashes[2][32];
static bool host_mounted,fail_catalog_once,cancel_at_catalog;
static unsigned physical_reads,catalog_faults;
static const char *wizard_test_phase="start";
static unsigned wizard_test_cut;
static struct kui_vmu_wizard_request wizard_request;
static struct kui_vmu_wizard_view wizard_view;

static void wizard_hash(const void *data,size_t bytes,uint8_t out[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,data,bytes);kui_sha256_digest(&hash,out);
}

static int wizard_media_write(void *context,uint32_t lba,size_t count,const uint8_t *data) {
    if(fail_catalog_once) for(size_t at=0;at<count*512u;at+=512u)
        if(!memcmp(data+at,"KUIVMUC2",8u)) {
            fail_catalog_once=false;++catalog_faults;return -1;
        }
    return write_media(context,lba,count,data);
}
static const struct kui_media_ops wizard_media={NULL,blocks,read_media,wizard_media_write,sync_media};
static void wizard_mount(void) {
    if(host_mounted) return;
    kui_media_set(&wizard_media);CHECK(f_mount(&fs,"0:",1)==FR_OK);
    volume=*kui_media_volume();CHECK(volume.partitioned && volume.start==2048u);host_mounted=true;
}
static void wizard_unmount(void) {
    if(host_mounted) {CHECK(f_mount(NULL,"0:",0)==FR_OK);host_mounted=false;}
}
enum kui_games_storage_result kui_games_storage_open(kui_log_fn log) {
    (void)log;wizard_mount();return KUI_GAMES_STORAGE_OK;
}
bool kui_games_storage_release(void) {wizard_unmount();return true;}
void kui_games_storage_hold(bool hold) {(void)hold;}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    struct kui_vmu_game_card_raw raw;CHECK(host_mounted && identity(&volume,&raw));
    memcpy(out->cid,raw.cid,16u);out->sectors=raw.card_sectors;out->high_capacity=raw.high_capacity;return true;
}
bool kui_games_visit_images(const char *root,kui_games_image_visit_fn visit,void *context,kui_cancel_fn cancel) {
    CHECK(!strcmp(root,"/Games") && host_mounted);
    static const char *const names[]={"alpha.gdi","beta.gdi","blank.gdi","pso.gdi","shared.gdi"};
    for(unsigned i=0;i<5u;i++) {
        if(cancel && cancel()) return false;
        if(!visit(context,names[i],false)) return false;
    }
    return true;
}
bool kui_games_inspect(const char *path,struct kui_games_detail *detail,kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;if(cancel && cancel()) return false;memset(detail,0,sizeof(*detail));
    const char *key=strstr(path,"alpha")?"ALPHA-1":strstr(path,"beta")?"BETA-1":
        strstr(path,"blank")?"BLANK-1":strstr(path,"shared")?"SHARED-1":"PSO-1";
    const char *title=strstr(path,"alpha")?"Alpha":strstr(path,"beta")?"Beta":
        strstr(path,"blank")?"Blank":strstr(path,"shared")?"Shared":"Phantasy Star Online";
    strcpy(detail->product,key);strcpy(detail->title,title);detail->valid=detail->native_gd=true;return true;
}
static int wizard_image_read(void *context,uint16_t block,void *out) {
    CHECK(block<KUI_VMU_IMAGE_BLOCKS);memcpy(out,(uint8_t *)context+512u*block,512u);return 0;
}
static struct kui_vmu_image_summary wizard_summary(const uint8_t image[KUI_VMU_IMAGE_BYTES]) {
    struct kui_vmu_image_workspace scratch;struct kui_vmu_image_summary summary;
    const struct kui_vmu_image_io io={(void *)image,wizard_image_read,NULL,NULL};
    CHECK(kui_vmu_image_validate(&io,KUI_VMU_IMAGE_BYTES,&scratch,&summary)==KUI_VMU_IMAGE_OK);return summary;
}
bool kui_vmu_capture_run(unsigned slot,void *image,size_t bytes,struct kui_vmu_image_summary *summary,
    struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    (void)log;(void)progress;CHECK(slot<2u && bytes==KUI_VMU_IMAGE_BYTES);
    if(cancel && cancel()) return false;
    memset(out,0,sizeof(*out));memcpy(image,physical_sources[slot],bytes);
    *summary=wizard_summary(image);++physical_reads;return true;
}
static void wizard_progress(const struct kui_app_status *status) {
    if(cancel_at_catalog && strstr(status->message,"Saving")) {
        cancel_at_catalog=false;cancelled_flag=true;
    }
}
static void wizard_call(enum kui_vmu_wizard_op op) {
    wizard_request.op=op;++wizard_request.generation;
    kui_vmu_wizard_run(&wizard_request,&wizard_view,log_line,cancelled,wizard_progress);
    CHECK(!host_mounted && !raw_open);
}
static void wizard_ok(enum kui_vmu_wizard_op op) {
    wizard_call(op);
    if(wizard_view.status.errors || wizard_view.status.stopped)
        fprintf(stderr,"Wizard op%u: %s\n",(unsigned)op,wizard_view.status.message);
    CHECK(!wizard_view.status.errors && !wizard_view.status.stopped);
}
static void source_images(void) {
    static const char *const names[2][5]={
        {"PSO______SYS","PSO______GCD","ALPHA_SYS","ALPHA_SAVE","FILLER_ONE"},
        {"PSO______SYS","PSO______GCD","BETA_SAVE","FILLER_TWO",NULL}};
    static const unsigned sizes[2][5]={{15u,30u,1u,2u,148u},{15u,30u,3u,152u,0u}};
    for(unsigned source=0;source<2u;source++) {
        struct kui_vmu_image_file files[5]={0};unsigned n=source?4u:5u;
        for(unsigned i=0;i<n;i++) {
            files[i].entry[0]=0x33;memcpy(files[i].entry+4u,names[source][i],strlen(names[source][i]));
            put16(files[i].entry+24u,sizes[source][i]);files[i].bytes=sizes[source][i]*512u;
            uint8_t *payload=malloc(files[i].bytes);CHECK(payload);
            memset(payload,(int)(0x11u+source*0x40u+i),files[i].bytes);
            memcpy(payload,names[source][i],strlen(names[source][i]));
            files[i].payload=payload;
        }
        struct kui_vmu_image_workspace scratch;struct kui_vmu_image_summary summary;
        CHECK(kui_vmu_image_pack(files,n,physical_sources[source],KUI_VMU_IMAGE_BYTES,&scratch,&summary)==KUI_VMU_IMAGE_OK);
        CHECK(summary.used_blocks==(source?200u:196u));
        wizard_hash(physical_sources[source],KUI_VMU_IMAGE_BYTES,physical_hashes[source]);
        for(unsigned i=0;i<n;i++) free((void *)files[i].payload);
    }
}
static void physical_unchanged(void) {
    uint8_t digest[32];
    for(unsigned i=0;i<2u;i++) {
        wizard_hash(physical_sources[i],KUI_VMU_IMAGE_BYTES,digest);CHECK(!memcmp(digest,physical_hashes[i],32u));
    }
}
static unsigned pair_count(void) {
    DIR directory;FILINFO info;unsigned count=0;
    CHECK(f_opendir(&directory,"0:/KUI/vmu-test")==FR_OK);
    for(;;) {
        CHECK(f_readdir(&directory,&info)==FR_OK);if(!info.fname[0]) break;
        if((info.fattrib&AM_DIR) && kui_vmu_game_card_pair_number(info.fname)) ++count;
    }
    CHECK(f_closedir(&directory)==FR_OK);return count;
}
static struct kui_vmu_game_candidates_storage storage_snapshot(void) {
    struct kui_vmu_game_card_raw raw;CHECK(host_mounted && identity(&volume,&raw));
    struct kui_vmu_game_candidates_storage storage={.card_sectors=raw.card_sectors,
        .partition_first=volume.start,.partition_end=(uint64_t)volume.start+volume.count,
        .high_capacity=raw.high_capacity};memcpy(storage.cid,raw.cid,16u);return storage;
}
static struct kui_vmu_game_candidates_view candidates(const char *product) {
    struct kui_vmu_game_candidates_view out;struct kui_vmu_game_candidates_storage storage=storage_snapshot();
    int result=kui_vmu_game_candidates_list(product,&storage,&out,log_line,cancelled);
    if(result) fprintf(stderr,"Candidate list %s refused %d phase=%s cut=%u; last=%s\n",product,result,wizard_test_phase,wizard_test_cut,last_log);
    CHECK(result==KUI_VMU_GAME_CANDIDATES_OK);return out;
}
static void choose_save(const char *name) {
    unsigned i=0;while(i<wizard_view.saves_count && strcmp(wizard_view.saves[i].name,name)) ++i;
    CHECK(i<wizard_view.saves_count && !wizard_view.saves[i].picked);
    wizard_request.index=wizard_view.saves[i].index;wizard_ok(KUI_VMU_WIZARD_TOGGLE_SAVE);
}
static void choose_game(unsigned index,const char *const *names,unsigned count) {
    wizard_request.index=index;wizard_request.card=0;wizard_request.page=0;
    wizard_ok(KUI_VMU_WIZARD_OPEN_SAVES);
    for(unsigned i=0;i<wizard_view.saves_count;i++) CHECK(!wizard_view.saves[i].picked);
    for(unsigned i=0;i<count;i++) choose_save(names[i]);
    wizard_ok(KUI_VMU_WIZARD_ACCEPT_SAVES);
}
static void plans(void) {
    memset(&wizard_request,0,sizeof(wizard_request));strcpy(wizard_request.root,"/Games");
    wizard_ok(KUI_VMU_WIZARD_RESET);
    wizard_ok(KUI_VMU_WIZARD_CAPTURE);wizard_request.slot=1;wizard_ok(KUI_VMU_WIZARD_CAPTURE);
    CHECK(wizard_view.cards==2u && wizard_view.sources==2u && physical_reads==2u);
    wizard_request.card=0;wizard_request.page=0;wizard_ok(KUI_VMU_WIZARD_LIST_GAMES);
    CHECK(wizard_view.total==5u && !wizard_view.selected_games);
    for(unsigned i=0;i<5u;i++) CHECK(!wizard_view.games[i].selected);
    static const char *const alpha[]={"ALPHA_SYS","ALPHA_SAVE"};choose_game(0u,alpha,2u);
    wizard_request.index=3u;wizard_request.card=0;wizard_request.page=0;wizard_ok(KUI_VMU_WIZARD_OPEN_SAVES);
    choose_save("PSO______SYS");choose_save("PSO______GCD");
    wizard_request.card=1;wizard_ok(KUI_VMU_WIZARD_OPEN_SAVES);
    choose_save("PSO______SYS");choose_save("PSO______GCD");wizard_ok(KUI_VMU_WIZARD_ACCEPT_SAVES);
    wizard_request.index=1u;wizard_request.card=1;wizard_request.page=0;wizard_ok(KUI_VMU_WIZARD_OPEN_SAVES);
    choose_save("BETA_SAVE");wizard_ok(KUI_VMU_WIZARD_ACCEPT_SAVES);
    choose_game(2u,NULL,0u);
    CHECK(wizard_view.selected_games==4u && wizard_view.variants==5u);
    wizard_ok(KUI_VMU_WIZARD_SHOW_REVIEW);
}
static void archive_check(bool remember) {
    struct kui_vmu_collection_page page;
    CHECK(!kui_vmu_collection_list(0u,&page,log_line,cancelled) && page.total==2u && page.count==2u);
    uint8_t image[KUI_VMU_IMAGE_BYTES];bool found[2]={false,false};
    for(unsigned i=0;i<2u;i++) {
        const struct kui_vmu_collection_card *card=page.entries+i;
        CHECK(file_read(card->path,image,sizeof(image))==sizeof(image));
        unsigned source=!memcmp(card->sha256,physical_hashes[0],32u)?0u:1u;
        CHECK(!found[source] && !memcmp(card->sha256,physical_hashes[source],32u) &&
              !memcmp(image,physical_sources[source],sizeof(image)));found[source]=true;
        if(remember) {
            const char *const leaves[]={"card.vmd","card.journal","identity.bin","card.vmd.crc","ready.bin"};
            const size_t sizes[]={131072u,1024u,16u,32u,512u};
            char path[96];const char *slash=strrchr(card->path,'/');CHECK(slash);
            for(unsigned n=0;n<5u;n++) {
                snprintf(path,sizeof(path),"%.*s/%s",(int)(slash-card->path),card->path,leaves[n]);protect(path,sizes[n]);
            }
        }
    }
}
static void check_card(const struct kui_vmu_game_candidate_target *target,const uint8_t *source,
    const char *const *names,unsigned count,unsigned used_blocks) {
    char path[96];uint8_t image[KUI_VMU_IMAGE_BYTES];pair_path(path,target->pair,"card.vmd");
    CHECK(file_read(path,image,sizeof(image))==sizeof(image));
    struct kui_vmu_image_summary summary=wizard_summary(image);
    CHECK(summary.files==count && summary.used_blocks==used_blocks && summary.free_blocks==200u-used_blocks);
    for(unsigned slot=0;slot<208u;slot++) {
        const uint8_t *entry=image+(253u-slot/16u)*512u+(slot%16u)*32u;if(!entry[0]) continue;
        unsigned which=0;while(which<count && strncmp((const char *)entry+4u,names[which],12u)) ++which;
        CHECK(which<count && source);
        const uint8_t *original=NULL;
        for(unsigned old=0;old<208u;old++) {
            const uint8_t *candidate=source+(253u-old/16u)*512u+(old%16u)*32u;
            if(candidate[0] && !memcmp(candidate+4u,entry+4u,12u)) {original=candidate;break;}
        }
        CHECK(original && !memcmp(original,entry,2u) && !memcmp(original+4u,entry+4u,28u));
        unsigned here=(unsigned)entry[2]|((unsigned)entry[3]<<8),there=(unsigned)original[2]|((unsigned)original[3]<<8);
        unsigned blocks=(unsigned)entry[24]|((unsigned)entry[25]<<8);
        for(unsigned n=0;n<blocks;n++) {
            CHECK(here<200u && there<200u && !memcmp(image+here*512u,source+there*512u,512u));
            here=(unsigned)image[254u*512u+here*2u]|((unsigned)image[254u*512u+here*2u+1u]<<8);
            there=(unsigned)source[254u*512u+there*2u]|((unsigned)source[254u*512u+there*2u+1u]<<8);
        }
        CHECK(here==0xfffau && there==0xfffau);
    }
}
static void result_cards(void) {
    struct kui_vmu_game_candidates_view alpha=candidates("ALPHA-1"),beta=candidates("BETA-1"),blank=candidates("BLANK-1"),pso=candidates("PSO-1");
    CHECK(alpha.count==1u && beta.count==1u && blank.count==1u && pso.count==2u);
    CHECK(pso.default_index==KUI_VMU_GAME_CANDIDATES_NO_DEFAULT);
    static const char *const alpha_names[]={"ALPHA_SYS","ALPHA_SAVE"},*const beta_names[]={"BETA_SAVE"};
    static const char *const pso_names[]={"PSO______SYS","PSO______GCD"};
    check_card(alpha.targets,physical_sources[0],alpha_names,2u,3u);
    check_card(beta.targets,physical_sources[1],beta_names,1u,3u);check_card(blank.targets,NULL,NULL,0u,0u);
    for(unsigned n=0;n<2u;n++) {
        char path[96];uint8_t image[KUI_VMU_IMAGE_BYTES];pair_path(path,pso.targets[n].pair,"card.vmd");
        CHECK(file_read(path,image,sizeof(image))==sizeof(image));
        const uint8_t *entry=image+253u*512u;unsigned first=(unsigned)entry[2]|((unsigned)entry[3]<<8);
        unsigned source=image[first*512u+100u]==physical_sources[0][199u*512u+100u]?0u:1u;
        check_card(pso.targets+n,physical_sources[source],pso_names,2u,45u);
    }
    unsigned pairs[]={alpha.targets[0].pair,beta.targets[0].pair,blank.targets[0].pair,pso.targets[0].pair,pso.targets[1].pair};
    for(unsigned i=0;i<5u;i++) {
        for(unsigned j=0;j<i;j++) CHECK(pairs[i]!=pairs[j]);
        protect_pair(pairs[i],false);char marker[96];pair_path(marker,pairs[i],"import.bin");protect(marker,64u);
    }
}

static void catalogue_hash(uint8_t out[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);
    uint8_t *data=malloc(KUI_VMU_GAME_CANDIDATES_WIRE_BYTES);CHECK(data);
    const char *const paths[]={KUI_VMU_GAME_CANDIDATES_PATH_A,KUI_VMU_GAME_CANDIDATES_PATH_B};
    for(unsigned i=0;i<2u;i++) {
        size_t bytes=file_read(paths[i],data,KUI_VMU_GAME_CANDIDATES_WIRE_BYTES);
        kui_sha256_update(&hash,&bytes,sizeof(bytes));
        if(bytes!=SIZE_MAX) kui_sha256_update(&hash,data,bytes);
    }
    kui_sha256_digest(&hash,out);free(data);
}
static void require_zero_wire(const uint8_t wire[4096]) {
    for(unsigned i=0;i<4096u;i++) CHECK(!wire[i]);
}
static void selector_and_defaults(void) {
    struct kui_vmu_game_candidates_storage storage=storage_snapshot();
    struct kui_vmu_game_candidates_view pso=candidates("PSO-1"),alpha=candidates("ALPHA-1"),beta=candidates("BETA-1");
    uint8_t wire[4096],before[32],after[32];struct kui_vmu_game_card_choice choice;
    unsigned count=pair_count();catalogue_hash(before);writes=0;
    memset(wire,0xa5,sizeof(wire));
    CHECK(kui_vmu_game_card_select("PSO-1",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_CHOICE_REQUIRED);
    require_zero_wire(wire);CHECK(!writes && !choice.pair && pair_count()==count);
    CHECK(kui_vmu_game_card_select_exact("PSO-1",pso.targets,&storage,volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    CHECK(choice.pair==pso.targets[0].pair && !choice.created && pair_count()==count);
    catalogue_hash(after);CHECK(!memcmp(before,after,32));
    wizard_test_phase="after one-time selection";
    pso=candidates("PSO-1");CHECK(pso.count==2u && pso.default_index==KUI_VMU_GAME_CANDIDATES_NO_DEFAULT);
    writes=0;
    CHECK(kui_vmu_game_card_select_exact("PSO-1",beta.targets,&storage,volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
    require_zero_wire(wire);CHECK(!writes && pair_count()==count);
    CHECK(kui_vmu_game_card_select("ALPHA-1",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    CHECK(choice.pair==alpha.targets[0].pair && !choice.created && pair_count()==count);
    catalogue_hash(after);CHECK(!memcmp(before,after,32));
    CHECK(kui_vmu_game_candidates_set_default("PSO-1",pso.targets+1,pso.sequence,&storage,log_line,cancelled)==0);
    wizard_unmount();wizard_mount();
    wizard_test_phase="after default remount";
    struct kui_vmu_game_candidates_view chosen=candidates("PSO-1");
    CHECK(chosen.count==2u && chosen.default_index<2u &&
        chosen.targets[chosen.default_index].pair==pso.targets[1].pair && chosen.sequence>pso.sequence);
    catalogue_hash(before);writes=0;
    CHECK(kui_vmu_game_candidates_set_default("PSO-1",pso.targets, pso.sequence,&storage,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_STALE);
    CHECK(!writes);cancelled_flag=true;
    CHECK(kui_vmu_game_candidates_set_default("PSO-1",NULL,chosen.sequence,&storage,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_CANCELLED);
    cancelled_flag=false;CHECK(!writes);catalogue_hash(after);CHECK(!memcmp(before,after,32));
    CHECK(kui_vmu_game_card_select("PSO-1",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    CHECK(choice.pair==pso.targets[1].pair && !choice.created);
    catalogue_hash(after);CHECK(!memcmp(before,after,32));
    for(unsigned field=0;field<5u;field++) {
        struct kui_vmu_game_candidates_storage foreign=storage;
        if(!field) foreign.cid[0]^=1u;
        else if(field==1u) ++foreign.card_sectors;
        else if(field==2u) ++foreign.partition_first;
        else if(field==3u) --foreign.partition_end;
        else foreign.high_capacity^=1u;
        struct kui_vmu_game_candidates_view out;writes=0;
        CHECK(kui_vmu_game_candidates_list("PSO-1",&foreign,&out,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_STALE);
        CHECK(!out.count && !writes);
    }
    /* A later torn catalogue write preserves the preceding complete default;
     * retry can publish the exact intended next generation. */
    fail_catalog_once=true;
    CHECK(kui_vmu_game_candidates_set_default("PSO-1",NULL,chosen.sequence,&storage,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_IO);
    CHECK(!fail_catalog_once && catalog_faults==2u);
    wizard_unmount();wizard_mount();wizard_test_phase="after torn default remount";
    chosen=candidates("PSO-1");CHECK(chosen.default_index<2u &&
        chosen.targets[chosen.default_index].pair==pso.targets[1].pair);
    CHECK(kui_vmu_game_candidates_set_default("PSO-1",NULL,chosen.sequence,&storage,log_line,cancelled)==0);
    pso=candidates("PSO-1");CHECK(pso.default_index==KUI_VMU_GAME_CANDIDATES_NO_DEFAULT);
}
static void shared_manual_path(void) {
    struct kui_vmu_game_candidates_view alpha=candidates("ALPHA-1");
    uint8_t image[KUI_VMU_IMAGE_BYTES];char path[96];pair_path(path,alpha.targets[0].pair,"card.vmd");
    CHECK(file_read(path,image,sizeof(image))==sizeof(image));unsigned count=pair_count();
    strcpy(wizard_request.card_path,path);wizard_unmount();wizard_ok(KUI_VMU_WIZARD_USE_CARD);
    CHECK(wizard_view.existing_card && !wizard_view.copy_on_commit);
    wizard_request.card=wizard_request.page=0;wizard_request.index=0;wizard_ok(KUI_VMU_WIZARD_TOGGLE);
    wizard_request.index=4;wizard_ok(KUI_VMU_WIZARD_TOGGLE);wizard_ok(KUI_VMU_WIZARD_SHOW_REVIEW);
    wizard_ok(KUI_VMU_WIZARD_COMMIT);wizard_mount();
    CHECK(pair_count()==count);
    struct kui_vmu_game_candidates_view shared=candidates("SHARED-1");alpha=candidates("ALPHA-1");
    CHECK(shared.count==1u && shared.default_index==0u && alpha.default_index<alpha.count);
    CHECK(shared.targets[0].pair==alpha.targets[alpha.default_index].pair);
    uint8_t checked[KUI_VMU_IMAGE_BYTES];CHECK(file_read(path,checked,sizeof(checked))==sizeof(checked) && !memcmp(image,checked,sizeof(image)));
    FILINFO info;CHECK(f_stat("0:/KUI/vmu-test/assign-a.bin",&info)==FR_NO_FILE &&
        f_stat("0:/KUI/vmu-test/assign-b.bin",&info)==FR_NO_FILE);
}
static unsigned candidate_visits;
static bool abort_candidate_visit;
static bool candidate_visitor(void *context,const char key[12],
    const struct kui_vmu_game_candidate_target *target,bool is_default) {
    CHECK(context==&candidate_visits && key[0] && target->pair);(void)is_default;
    ++candidate_visits;return !abort_candidate_visit;
}
static void visitor_refusals(void) {
    struct kui_vmu_game_candidates_storage storage=storage_snapshot();
    candidate_visits=0;writes=0;
    CHECK(!kui_vmu_game_candidates_visit(&storage,candidate_visitor,&candidate_visits,log_line,cancelled));
    CHECK(candidate_visits==5u && !writes);candidate_visits=0;
    CHECK(!kui_vmu_game_candidates_visit(NULL,candidate_visitor,&candidate_visits,log_line,cancelled));
    CHECK(candidate_visits==5u && !writes);candidate_visits=0;
    struct kui_vmu_game_candidates_storage foreign=storage;foreign.cid[0]^=1u;
    CHECK(kui_vmu_game_candidates_visit(&foreign,candidate_visitor,&candidate_visits,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_STALE);
    CHECK(!candidate_visits && !writes);cancelled_flag=true;
    CHECK(kui_vmu_game_candidates_visit(&storage,candidate_visitor,&candidate_visits,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_CANCELLED);
    cancelled_flag=false;CHECK(!candidate_visits && !writes);abort_candidate_visit=true;
    CHECK(kui_vmu_game_candidates_visit(&storage,candidate_visitor,&candidate_visits,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_CANCELLED);
    abort_candidate_visit=false;CHECK(candidate_visits==1u && !writes);
    struct kui_vmu_game_candidates_view pso=candidates("PSO-1");char path[96];uint8_t id[16],changed[16];
    pair_path(path,pso.targets[1].pair,"identity.bin");CHECK(file_read(path,id,16u)==16u);memcpy(changed,id,16u);changed[0]^=1u;
    file_write(path,changed,16u);candidate_visits=0;writes=0;
    CHECK(kui_vmu_game_candidates_visit(&storage,candidate_visitor,&candidate_visits,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_STALE);
    CHECK(!candidate_visits && !writes);file_write(path,id,16u);
    CHECK(kui_vmu_game_candidates_identity_unpinned(pso.targets[0].pair,pso.targets[0].identity,cancelled)==KUI_VMU_GAME_CANDIDATES_STALE);
    memset(changed,0x42,16u);CHECK(!kui_vmu_game_candidates_identity_unpinned(9999u,changed,cancelled));
    uint8_t *records[2];const char *const paths[]={KUI_VMU_GAME_CANDIDATES_PATH_A,KUI_VMU_GAME_CANDIDATES_PATH_B};
    for(unsigned i=0;i<2u;i++) {
        records[i]=malloc(KUI_VMU_GAME_CANDIDATES_WIRE_BYTES);CHECK(records[i]);
        CHECK(file_read(paths[i],records[i],KUI_VMU_GAME_CANDIDATES_WIRE_BYTES)==KUI_VMU_GAME_CANDIDATES_WIRE_BYTES);
        records[i][100]^=1u;file_write(paths[i],records[i],KUI_VMU_GAME_CANDIDATES_WIRE_BYTES);records[i][100]^=1u;
    }
    candidate_visits=0;writes=0;
    CHECK(kui_vmu_game_candidates_visit(&storage,candidate_visitor,&candidate_visits,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_INVALID);
    CHECK(!candidate_visits && !writes);
    for(unsigned i=0;i<2u;i++) {file_write(paths[i],records[i],KUI_VMU_GAME_CANDIDATES_WIRE_BYTES);free(records[i]);}
    candidate_visits=0;CHECK(!kui_vmu_game_candidates_visit(&storage,candidate_visitor,&candidate_visits,log_line,cancelled));
    CHECK(candidate_visits==5u);
}
static void catalogue_power_cuts(void) {
    struct kui_vmu_game_candidates_view original=candidates("PSO-1");
    CHECK(original.default_index==KUI_VMU_GAME_CANDIDATES_NO_DEFAULT);
    struct kui_vmu_game_candidates_storage storage=storage_snapshot();
    wizard_unmount();uint8_t *seed=malloc(disk_bytes);CHECK(seed);memcpy(seed,disk,disk_bytes);wizard_mount();
    writes=0;
    CHECK(!kui_vmu_game_candidates_set_default("PSO-1",original.targets+1,original.sequence,&storage,log_line,cancelled));
    unsigned complete_writes=writes;CHECK(complete_writes && complete_writes<256u);wizard_unmount();
    for(unsigned cut=0;cut<=complete_writes;cut++) {
        wizard_test_cut=cut;
        memcpy(disk,seed,disk_bytes);fail_after=-1;wizard_mount();writes=0;fail_after=(int)cut;
        int result=kui_vmu_game_candidates_set_default("PSO-1",original.targets+1,original.sequence,&storage,log_line,cancelled);
        fail_after=-1;wizard_unmount();wizard_mount();wizard_test_phase="power cut remount read";
        struct kui_vmu_game_candidates_view now=candidates("PSO-1");
        CHECK(now.count==2u && !memcmp(now.targets,original.targets,2u*sizeof(now.targets[0])));
        CHECK(now.default_index==KUI_VMU_GAME_CANDIDATES_NO_DEFAULT ||
            (now.default_index<2u && now.targets[now.default_index].pair==original.targets[1].pair));
        if(!result) CHECK(now.default_index<2u && now.targets[now.default_index].pair==original.targets[1].pair);
        CHECK(!kui_vmu_game_candidates_set_default("PSO-1",original.targets+1,now.sequence,&storage,log_line,cancelled));
        wizard_test_phase="power cut retry read";
        struct kui_vmu_game_candidates_view retried=candidates("PSO-1");
        CHECK(retried.default_index<2u && retried.targets[retried.default_index].pair==original.targets[1].pair);
        protected_unchanged();wizard_unmount();
    }
    memcpy(disk,seed,disk_bytes);free(seed);wizard_mount();
    printf("Candidate catalogue real filesystem: %u complete-write boundaries preserve exact old or new default\n",complete_writes+1u);
}

int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));
    long bytes=ftell(input);CHECK(bytes>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)bytes;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));source_images();
    wizard_mount();bool exfat=fs.fs_type==FS_EXFAT;wizard_unmount();plans();
    cancelled_flag=true;wizard_call(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.status.stopped);
    cancelled_flag=false;fail_catalog_once=true;wizard_call(KUI_VMU_WIZARD_COMMIT);
    CHECK(wizard_view.status.errors && catalog_faults==1u && !fail_catalog_once);
    wizard_mount();unsigned retained=pair_count();CHECK(retained==5u);archive_check(true);wizard_unmount();
    cancel_at_catalog=true;wizard_call(KUI_VMU_WIZARD_COMMIT);
    CHECK(wizard_view.status.stopped && !cancel_at_catalog);cancelled_flag=false;
    wizard_mount();CHECK(pair_count()==retained);archive_check(false);protected_unchanged();
    struct kui_vmu_game_candidates_storage empty_storage=storage_snapshot();struct kui_vmu_game_candidates_view empty;
    CHECK(kui_vmu_game_candidates_list("PSO-1",&empty_storage,&empty,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_ABSENT);
    wizard_unmount();
    wizard_ok(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.stage==KUI_VMU_WIZARD_DONE);
    wizard_mount();CHECK(pair_count()==retained);result_cards();archive_check(false);protected_unchanged();
    selector_and_defaults();visitor_refusals();catalogue_power_cuts();shared_manual_path();archive_check(false);protected_unchanged();wizard_unmount();physical_unchanged();
    printf("VMU wizard real filesystems (%s): %u checks; exact source archives, explicit per-game files, separate PSO versions, blank200, interrupted catalogue retry retained pairs, real selector/defaults and manual shared path\n",exfat?"exFAT":"FAT32",checks);
    for(unsigned i=0;i<protected_count;i++) free(protected_files[i].data);
    free(disk);return 0;
}
