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
#include "kui/vmu_match_memory.h"
#include "kui/games_storage.h"
#include "kui/vmu_backing.h"
#include "kui/clock.h"
#include "kui/hash.h"

static uint8_t physical_sources[2][KUI_VMU_IMAGE_BYTES];
static uint8_t physical_hashes[2][32];
static bool host_mounted,fail_catalog_once,cancel_at_catalog;
static unsigned physical_reads,catalog_faults;
static bool future_legacy_visible;
enum memory_catalog_mode { MEMORY_CATALOG_NORMAL, MEMORY_CATALOG_MISSING,
    MEMORY_CATALOG_INELIGIBLE, MEMORY_CATALOG_TITLE_CHANGED, MEMORY_CATALOG_AMBIGUOUS,
    MEMORY_CATALOG_EMPTY, MEMORY_CATALOG_NO_FOLDER };
static enum memory_catalog_mode memory_catalog;
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
    if(memory_catalog==MEMORY_CATALOG_EMPTY) return true;
    if(memory_catalog==MEMORY_CATALOG_NO_FOLDER) return false;
    static const char *const names[]={"alpha.gdi","beta.gdi","blank.gdi","pso.gdi","shared.gdi","legacy.gdi"};
    for(unsigned i=0;i<(future_legacy_visible?6u:5u);i++) {
        if(cancel && cancel()) return false;
        if(memory_catalog==MEMORY_CATALOG_MISSING && !i) continue;
        if(!visit(context,names[i],false)) return false;
    }
    if(memory_catalog==MEMORY_CATALOG_AMBIGUOUS && !visit(context,"pso2.gdi",false)) return false;
    return true;
}
bool kui_games_inspect(const char *path,struct kui_games_detail *detail,kui_log_fn log,kui_cancel_fn cancel) {
    (void)log;if(cancel && cancel()) return false;memset(detail,0,sizeof(*detail));
    const char *key=strstr(path,"alpha")?"ALPHA-1":strstr(path,"beta")?"BETA-1":
        strstr(path,"pso2")?"PSO-2":
        strstr(path,"blank")?"BLANK-1":strstr(path,"shared")?"SHARED-1":strstr(path,"legacy")?"LEGACY001":"PSO-1";
    const char *title=strstr(path,"alpha")?"Alpha Adventure":strstr(path,"beta")?"Beta Adventure":
        strstr(path,"blank")?"Blank":strstr(path,"shared")?"Shared":strstr(path,"legacy")?"Legacy Adventure":"Phantasy Star Online";
    if(memory_catalog==MEMORY_CATALOG_TITLE_CHANGED && strstr(path,"alpha")) title="Alpha Adventure Revised";
    strcpy(detail->product,key);strcpy(detail->title,title);detail->valid=detail->native_gd=true;
    if(memory_catalog==MEMORY_CATALOG_INELIGIBLE && strstr(path,"alpha")) {
        detail->native_gd=false;detail->windows_ce=true;
    }
    return true;
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
    (void)slot;(void)image;(void)bytes;(void)summary;(void)out;
    (void)log;(void)cancel;(void)progress;
    CHECK(!"Wizard must use whole-save recovery, never strict raw/legacy capture");return false;
}
bool kui_vmu_capture_recover_run(unsigned slot,void *image,size_t bytes,struct kui_vmu_image_summary *summary,
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
/* Thirty-three authored VMS saves reproduce the two source-card occupancies
 * in the hardware report. Each save has an actual VMS text header, ordinary
 * data-file directory entry and distinct opaque payload bytes. */
static void source_images(void) {
    for(unsigned source=0;source<2u;source++) {
        struct kui_vmu_image_file files[20]={0};unsigned n=source?20u:13u;
        for(unsigned i=0;i<n;i++) {
            char name[13],short_text[17],long_text[33];unsigned size;
            if(i<2u) {
                strcpy(name,i?"PSO______GCD":"PSO______SYS");size=i?30u:15u;
                strcpy(short_text,"PSO");strcpy(long_text,i?"GUILD_CARD":"MAIN_DATA");
            } else if(!source && i<4u) {
                strcpy(name,i==2u?"ALPHA_SYS":"ALPHA_SAVE");size=i==2u?1u:2u;
                strcpy(short_text,"Alpha Adventure");strcpy(long_text,"Alpha Adventure");
            } else if(source && i==2u) {
                strcpy(name,"BETA_SAVE");size=3u;
                strcpy(short_text,"Beta Adventure");strcpy(long_text,"Beta Adventure");
            } else if(i<(source?5u:6u)) {
                unsigned at=i-(source?3u:4u);
                strcpy(name,at?"MYSTERY_.002":"MYSTERY_.001");size=1u;
                strcpy(short_text,"MYSTERY SET");strcpy(long_text,"Unidentified adventure saves");
            } else {
                snprintf(name,sizeof(name),"ARCHIVE.%03u",i-(source?5u:6u)+1u);
                size=source?10u:(i==12u?26u:20u);
                strcpy(short_text,"ARCHIVE DATA");strcpy(long_text,"Archived unrecognized saves");
            }
            files[i].entry[0]=0x33;memcpy(files[i].entry+4u,name,strlen(name));
            put16(files[i].entry+24u,size);files[i].bytes=size*512u;
            uint8_t *payload=malloc(files[i].bytes);CHECK(payload);
            memset(payload,(int)(0x11u+source*0x40u+i),files[i].bytes);
            memset(payload,0,64u);memcpy(payload,short_text,strlen(short_text));
            memcpy(payload+16u,long_text,strlen(long_text));
            /* Publisher IDs are deliberately generic: they must not merge
             * unrelated games or resolve the unidentified sets by themselves. */
            memcpy(payload+48u,"SEGA",4u);files[i].payload=payload;
            /* One ordinary file has its VMS header in logical block 1. The
             * details view must follow metadata rather than assume block 0. */
            if(!source && i==6u) {
                put16(files[i].entry+26u,1u);memcpy(payload+512u,payload,64u);
                memset(payload,0,64u);
            }
        }
        struct kui_vmu_image_workspace scratch;struct kui_vmu_image_summary summary;
        CHECK(kui_vmu_image_pack(files,n,physical_sources[source],KUI_VMU_IMAGE_BYTES,&scratch,&summary)==KUI_VMU_IMAGE_OK);
        CHECK(summary.files==n && summary.used_blocks==(source?200u:196u));
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
static unsigned details16(const uint8_t *bytes) {
    return (unsigned)bytes[0]|(unsigned)bytes[1]<<8;
}
/* The oracle reads the independently authored physical image, rather than
 * asking the importer to inspect its own output a second time. */
static void details_row(const struct kui_vmu_wizard_save *row,unsigned source) {
    CHECK(source<2u && row->index<(source?20u:13u));
    const uint8_t *image=physical_sources[source];
    const uint8_t *entry=image+(253u-row->index/16u)*512u+(row->index%16u)*32u;
    CHECK(row->slot==source && row->type==entry[0] && row->blocks==details16(entry+24u));
    CHECK(!strncmp(row->name,(const char *)entry+4u,12u));
    CHECK(row->header_offset==details16(entry+26u));
    unsigned block=details16(entry+2u);
    for(unsigned i=0;i<row->header_offset;i++) {
        CHECK(block<200u);block=details16(image+254u*512u+block*2u);
    }
    CHECK(block<200u);const uint8_t *header=image+block*512u;
    CHECK(!strncmp(row->short_description,(const char *)header,16u));
    CHECK(!strncmp(row->long_description,(const char *)header+16u,32u));
    CHECK(!strncmp(row->application,(const char *)header+48u,16u));
}
static void details_unchanged(const struct kui_vmu_wizard_view *before) {
    CHECK(wizard_view.stage==before->stage && wizard_view.card==before->card &&
          wizard_view.page==before->page && wizard_view.groups_page==before->groups_page);
    CHECK(wizard_view.group_index==before->group_index && wizard_view.selected_games==before->selected_games &&
          wizard_view.variants==before->variants && wizard_view.unresolved_groups==before->unresolved_groups &&
          wizard_view.skipped_groups==before->skipped_groups && !wizard_view.publication_started);
    CHECK(!memcmp(wizard_view.games,before->games,sizeof(before->games)));
    if(before->stage==KUI_VMU_WIZARD_SAVES) {
        CHECK(wizard_view.game_index==before->game_index && wizard_view.saves_page==before->saves_page &&
              wizard_view.saves_selected_files==before->saves_selected_files &&
              wizard_view.saves_selected_blocks==before->saves_selected_blocks);
        CHECK(!memcmp(wizard_view.saves,before->saves,sizeof(before->saves)));
    }
}
/* Inspect every filtered companion and prove read-only requests cannot be
 * promoted into capture, selection, assignment or publication operations. */
static bool group_details(const struct kui_vmu_wizard_group *group) {
    struct kui_vmu_wizard_request saved=wizard_request;
    struct kui_vmu_wizard_view before=wizard_view;
    unsigned previous_reads=physical_reads,previous_writes=writes,seen=0u;
    wizard_request.index=group->index;wizard_request.group_index=group->index;
    wizard_request.page=before.stage==KUI_VMU_WIZARD_GROUPS?before.groups_page:before.page;
    unsigned page=wizard_request.page;
    ++wizard_request.page;wizard_call(KUI_VMU_WIZARD_OPEN_DETAILS);
    CHECK(wizard_view.status.errors && wizard_view.stage==before.stage);
    wizard_request.page=page;wizard_ok(KUI_VMU_WIZARD_OPEN_DETAILS);
    CHECK(wizard_view.stage==KUI_VMU_WIZARD_DETAILS && wizard_view.details_return_stage==before.stage);
    CHECK(wizard_view.saves_total==group->files && !wizard_view.saves_page);
    bool mystery=wizard_view.saves_count && !strncmp(wizard_view.saves[0].name,"MYSTERY_",8u);
    unsigned last_index=0u;
    for(unsigned p=0u;p*8u<group->files;p++) {
        if(p) {wizard_request.page=p;wizard_ok(KUI_VMU_WIZARD_DETAILS_PAGE);}
        CHECK(wizard_view.saves_page==p && wizard_view.saves_total==group->files);
        unsigned rows=group->files-p*8u;if(rows>8u) rows=8u;
        CHECK(wizard_view.saves_count==rows);
        for(unsigned i=0u;i<rows;i++) {
            const struct kui_vmu_wizard_save *row=wizard_view.saves+i;
            details_row(row,group->source);
            if(seen) CHECK(row->index>last_index);
            CHECK(mystery?!strncmp(row->name,"MYSTERY_",8u):!strncmp(row->name,"ARCHIVE.",8u));
            last_index=row->index;++seen;
        }
    }
    CHECK(seen==group->files);
    unsigned last_page=wizard_view.saves_page;
    wizard_request.page=(group->files-1u)/8u+1u;wizard_call(KUI_VMU_WIZARD_DETAILS_PAGE);
    CHECK(wizard_view.status.errors && wizard_view.stage==KUI_VMU_WIZARD_DETAILS && wizard_view.saves_page==last_page);
    static const enum kui_vmu_wizard_op refused[]={KUI_VMU_WIZARD_RESET,KUI_VMU_WIZARD_CAPTURE,
        KUI_VMU_WIZARD_TOGGLE_SAVE,KUI_VMU_WIZARD_RESOLVE_GROUP,KUI_VMU_WIZARD_SKIP_GROUP,
        KUI_VMU_WIZARD_COMMIT,KUI_VMU_WIZARD_SHOW_SOURCES,KUI_VMU_WIZARD_OPEN_SAVES};
    for(unsigned i=0u;i<sizeof(refused)/sizeof(refused[0]);i++) {
        wizard_call(refused[i]);CHECK(wizard_view.status.errors && wizard_view.stage==KUI_VMU_WIZARD_DETAILS);
        CHECK(wizard_view.saves_total==group->files && wizard_view.saves_page==last_page);
    }
    wizard_ok(KUI_VMU_WIZARD_CLOSE_DETAILS);details_unchanged(&before);
    CHECK(physical_reads==previous_reads && writes==previous_writes);physical_unchanged();
    wizard_request=saved;return mystery;
}
static void save_details(void) {
    unsigned previous_reads=physical_reads,previous_writes=writes;
    wizard_request.card=wizard_request.page=0u;wizard_ok(KUI_VMU_WIZARD_SHOW_GAMES);
    wizard_request.index=0u;wizard_ok(KUI_VMU_WIZARD_OPEN_SAVES);
    CHECK(wizard_view.stage==KUI_VMU_WIZARD_SAVES && wizard_view.saves_total==13u);
    struct kui_vmu_wizard_view before=wizard_view;
    wizard_request.index=6u;wizard_request.page=0u;wizard_ok(KUI_VMU_WIZARD_OPEN_DETAILS);
    CHECK(wizard_view.stage==KUI_VMU_WIZARD_DETAILS && wizard_view.details_return_stage==KUI_VMU_WIZARD_SAVES);
    CHECK(wizard_view.saves_total==13u && wizard_view.saves_count==8u);
    for(unsigned i=0u;i<wizard_view.saves_count;i++) details_row(wizard_view.saves+i,0u);
    CHECK(wizard_view.saves[6].header_offset==1u && !strcmp(wizard_view.saves[6].application,"SEGA"));
    wizard_request.page=1u;wizard_ok(KUI_VMU_WIZARD_DETAILS_PAGE);
    CHECK(wizard_view.saves_page==1u && wizard_view.saves_count==5u && wizard_view.saves[0].index==8u);
    for(unsigned i=0u;i<wizard_view.saves_count;i++) details_row(wizard_view.saves+i,0u);
    wizard_ok(KUI_VMU_WIZARD_CLOSE_DETAILS);details_unchanged(&before);
    /* The original second source/page must survive viewing a different
     * details page, as must every game's existing accepted save choice. */
    wizard_request.card=1u;wizard_request.page=1u;wizard_request.index=0u;wizard_ok(KUI_VMU_WIZARD_OPEN_SAVES);
    CHECK(wizard_view.card==1u && wizard_view.saves_page==1u && wizard_view.saves_total==20u);
    before=wizard_view;wizard_request.index=9u;wizard_ok(KUI_VMU_WIZARD_OPEN_DETAILS);
    CHECK(wizard_view.saves_page==1u && wizard_view.saves[1].index==9u);
    wizard_request.page=0u;wizard_ok(KUI_VMU_WIZARD_DETAILS_PAGE);
    for(unsigned i=0u;i<wizard_view.saves_count;i++) details_row(wizard_view.saves+i,1u);
    wizard_ok(KUI_VMU_WIZARD_CLOSE_DETAILS);details_unchanged(&before);
    CHECK(physical_reads==previous_reads && writes==previous_writes);physical_unchanged();
    wizard_request.card=wizard_request.page=0u;wizard_ok(KUI_VMU_WIZARD_SHOW_GAMES);
    wizard_ok(KUI_VMU_WIZARD_SHOW_REVIEW);
}
static struct kui_vmu_import_plan *memory_group(const char *prefix,
    struct kui_vmu_import_match_group *group) {
    struct kui_vmu_import_plan *plan=malloc(sizeof(*plan));
    struct kui_vmu_image_workspace *scratch=malloc(sizeof(*scratch));CHECK(plan && scratch);
    CHECK(kui_vmu_import_plan_inspect(physical_sources[0],KUI_VMU_IMAGE_BYTES,NULL,NULL,scratch,plan)==KUI_VMU_IMAGE_OK);
    free(scratch);memset(group,0,sizeof(*group));
    for(unsigned i=0u;i<plan->entry_count;i++) if(!strncmp(plan->entries[i].name,prefix,strlen(prefix))) {
        group->members[i/8u]|=(uint8_t)(1u<<(i%8u));++group->files;group->blocks+=plan->entries[i].blocks;
    }
    CHECK(group->files);return plan;
}
static int memory_target(const char *prefix,struct kui_vmu_match_memory_target *target) {
    struct kui_vmu_import_match_group group;struct kui_vmu_import_plan *plan=memory_group(prefix,&group);
    bool already_mounted=host_mounted;wizard_mount();
    int result=kui_vmu_match_memory_load(plan,&group,target);
    if(!already_mounted) wizard_unmount();
    free(plan);return result;
}
static void memory_seed(const char *prefix,const char *product,const char *title) {
    struct kui_vmu_import_match_group group;struct kui_vmu_import_plan *plan=memory_group(prefix,&group);
    struct kui_vmu_match_memory_target target={0};strcpy(target.product,product);strcpy(target.title,title);
    bool already_mounted=host_mounted;wizard_mount();
    CHECK(kui_vmu_match_memory_remember(plan,&group,&target)==KUI_VMU_MATCH_MEMORY_OK);
    if(!already_mounted) wizard_unmount();
    free(plan);
}
static void memory_forgotten(void) {
    struct kui_vmu_match_memory_target target;
    CHECK(memory_target("MYSTERY_",&target)==KUI_VMU_MATCH_MEMORY_MISSING && !target.product[0]);
    struct kui_vmu_import_match_group group;struct kui_vmu_import_plan *plan=memory_group("MYSTERY_",&group);
    uint8_t key[32],record[208];char hex[65],path[128];
    CHECK(!kui_vmu_match_memory_key(plan,&group,key));kui_hex(key,sizeof(key),hex);free(plan);
    snprintf(path,sizeof(path),KUI_VMU_MATCH_MEMORY_ROOT "/%s-b.bin",hex);
    bool already_mounted=host_mounted;wizard_mount();
    CHECK(file_read(path,record,sizeof(record))==sizeof(record));
    CHECK(!memcmp(record,"KUIVMM01",8u) && record[185u]==1u);
    CHECK((uint32_t)details16(record+8u)==2u && kui_crc32(0u,record,204u)==
        ((uint32_t)record[204u]|(uint32_t)record[205u]<<8|(uint32_t)record[206u]<<16|(uint32_t)record[207u]<<24));
    if(!already_mounted) wizard_unmount();
}
static void plans(void) {
    memset(&wizard_request,0,sizeof(wizard_request));strcpy(wizard_request.root,"/Games");
    wizard_ok(KUI_VMU_WIZARD_RESET);
    wizard_ok(KUI_VMU_WIZARD_CAPTURE);wizard_request.slot=1;wizard_ok(KUI_VMU_WIZARD_CAPTURE);
    CHECK(wizard_view.cards==2u && wizard_view.sources==2u && physical_reads==2u);
    CHECK(wizard_view.summary[0].files==13u && wizard_view.summary[0].blocks==196u &&
          wizard_view.summary[1].files==20u && wizard_view.summary[1].blocks==200u);
    wizard_request.card=0;wizard_request.page=0;wizard_ok(KUI_VMU_WIZARD_LIST_GAMES);
    CHECK(wizard_view.automatic && wizard_view.stage==KUI_VMU_WIZARD_GROUPS);
    CHECK(wizard_view.total==5u && wizard_view.selected_games==3u && wizard_view.variants==4u);
    unsigned unresolved=wizard_view.unresolved_groups,resolved=0,skipped=0;
    CHECK(unresolved && !wizard_view.publication_started);
    /* Commit cannot silently discard any unrecognized saves. */
    wizard_call(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.status.errors && !wizard_view.publication_started);
    wizard_request.page=0;wizard_ok(KUI_VMU_WIZARD_SHOW_GROUPS);
    while(wizard_view.unresolved_groups) {
        CHECK(wizard_view.stage==KUI_VMU_WIZARD_GROUPS && wizard_view.groups_count);
        struct kui_vmu_wizard_group group=wizard_view.groups[0];
        wizard_request.index=group.index;wizard_request.page=wizard_view.groups_page;
        if(group_details(&group)) {
            wizard_ok(KUI_VMU_WIZARD_OPEN_GROUP);
            CHECK(wizard_view.stage==KUI_VMU_WIZARD_GROUP_GAME &&
                  wizard_view.group_index==group.index && wizard_view.group_source==group.source);
            CHECK(group_details(&group));
            const char *key=group.source?"BETA-1":"ALPHA-1";unsigned at=0;
            while(at<wizard_view.count && strcmp(wizard_view.games[at].product,key)) ++at;
            CHECK(at<wizard_view.count);
            wizard_request.index=wizard_view.games[at].index;wizard_request.page=wizard_view.page;
            wizard_request.group_index=wizard_view.group_index;
            if(!resolved) {
                unsigned selected=wizard_view.selected_games,variants=wizard_view.variants;
                ++wizard_request.group_index;wizard_call(KUI_VMU_WIZARD_RESOLVE_GROUP);
                CHECK(wizard_view.status.errors && wizard_view.stage==KUI_VMU_WIZARD_GROUP_GAME &&
                      wizard_view.selected_games==selected && wizard_view.variants==variants);
                wizard_request.group_index=group.index;
            }
            wizard_ok(KUI_VMU_WIZARD_RESOLVE_GROUP);resolved+=group.files;
        } else {
            wizard_ok(KUI_VMU_WIZARD_SKIP_GROUP);skipped+=group.files;
        }
        if(wizard_view.unresolved_groups) {wizard_request.page=0;wizard_ok(KUI_VMU_WIZARD_SHOW_GROUPS);}
    }
    CHECK(resolved==4u && skipped==22u && resolved+skipped+7u==33u);
    CHECK(wizard_view.selected_games==3u && wizard_view.variants==4u && !wizard_view.publication_started);
    if(wizard_view.stage!=KUI_VMU_WIZARD_REVIEW) wizard_ok(KUI_VMU_WIZARD_SHOW_REVIEW);
    CHECK(wizard_view.stage==KUI_VMU_WIZARD_REVIEW && wizard_view.count==3u && wizard_view.total==3u);
    CHECK(wizard_view.games[0].index==0u && wizard_view.games[1].index==1u && wizard_view.games[2].index==3u);
    save_details();
    unsigned files=0,blocks=0;
    for(unsigned i=0;i<wizard_view.count;i++) {
        CHECK(wizard_view.games[i].selected && !wizard_view.games[i].blank);
        files+=wizard_view.games[i].saves;blocks+=wizard_view.games[i].blocks;
    }
    CHECK(files==11u && blocks==100u && !writes); /* No file toggles, blank-card command or storage write. */
    printf("Automatic import: 33 saves, two immutable sources (196/200 blocks); three matched games/four variants, four unresolved files explicitly resolved, 22 retained only in full archives\n");
}
static void legacy_records(uint8_t record[KUI_VMU_GAME_ASSIGN_WIRE_BYTES]) {
    struct kui_vmu_game_assign_table table={.sequence=1u,.count=34u};
    for(unsigned i=0;i<34u;i++) {
        if(!i) strcpy(table.entries[i].key,"ALPHA-1");
        else snprintf(table.entries[i].key,sizeof(table.entries[i].key),"LEGACY%03u",i);
        table.entries[i].pair=5u;memset(table.entries[i].identity,0x35,16u);
    }
    CHECK(kui_vmu_game_assign_encode(&table,record));
}
/* A legacy assignment count is not a candidate capacity measurement. An
 * existing card without its import proof is refused before publication with
 * a stale-card diagnostic. A wholly deleted card is handled separately below. */
static void legacy_missing_preflight(void) {
    wizard_mount();wizard_unmount();uint8_t *seed=malloc(disk_bytes);CHECK(seed);memcpy(seed,disk,disk_bytes);
    wizard_mount();FRESULT made=f_mkdir("0:/KUI");CHECK(made==FR_OK || made==FR_EXIST);
    CHECK(f_mkdir("0:/KUI/vmu-test")==FR_OK);
    uint8_t record[KUI_VMU_GAME_ASSIGN_WIRE_BYTES],after[KUI_VMU_GAME_ASSIGN_WIRE_BYTES];legacy_records(record);
    file_write("0:/KUI/vmu-test/assign-a.bin",record,sizeof(record));
    CHECK(f_mkdir("0:/KUI/vmu-test/p0005")==FR_OK);
    file_write("0:/KUI/vmu-test/p0005/card.vmd",physical_sources[0],KUI_VMU_IMAGE_BYTES);
    uint8_t journal[1024]={0},identity_bytes[16];memset(identity_bytes,0x35,16u);
    file_write("0:/KUI/vmu-test/p0005/card.journal",journal,sizeof(journal));
    file_write("0:/KUI/vmu-test/p0005/identity.bin",identity_bytes,16u);
    wizard_unmount();writes=0;wizard_call(KUI_VMU_WIZARD_COMMIT);
    if(!wizard_view.status.errors || strstr(wizard_view.status.message,"full"))
        fprintf(stderr,"Missing legacy import proof diagnostic: %s\n",wizard_view.status.message);
    CHECK(wizard_view.status.errors && !wizard_view.status.stopped && !wizard_view.publication_started && !writes);
    CHECK(!strstr(wizard_view.status.message,"full") && !strstr(wizard_view.status.message,"Full"));
    CHECK(strstr(wizard_view.status.message,"missing") || strstr(wizard_view.status.message,"changed") ||
          strstr(wizard_view.status.message,"stale") || strstr(wizard_view.status.message,"Stale"));
    wizard_mount();CHECK(pair_count()==1u);
    CHECK(file_read("0:/KUI/vmu-test/assign-a.bin",after,sizeof(after))==sizeof(after) && !memcmp(record,after,sizeof(record)));
    FILINFO info;FRESULT r=f_stat(KUI_VMU_GAME_CANDIDATES_PATH_A,&info);CHECK(r==FR_NO_FILE || r==FR_NO_PATH);
    r=f_stat(KUI_VMU_GAME_CANDIDATES_PATH_B,&info);CHECK(r==FR_NO_FILE || r==FR_NO_PATH);
    wizard_unmount();physical_unchanged();memcpy(disk,seed,disk_bytes);free(seed);
    /* The user explicitly deleted the old p0005 folder and requested a fresh
     * import. Recovery of this selected key must preserve every v1 byte and
     * every unrelated old key; launch of those missing targets still refuses. */
    wizard_mount();made=f_mkdir("0:/KUI");CHECK(made==FR_OK || made==FR_EXIST);
    CHECK(f_mkdir("0:/KUI/vmu-test")==FR_OK);
    file_write("0:/KUI/vmu-test/assign-a.bin",record,sizeof(record));
    protect("0:/KUI/vmu-test/assign-a.bin",sizeof(record));CHECK(!pair_count());
    struct kui_vmu_game_candidates_storage storage=storage_snapshot();
    struct kui_vmu_game_candidates_plan plan[3]={0};
    strcpy(plan[0].key,"ALPHA-1");plan[0].additional=1u;
    strcpy(plan[1].key,"BETA-1");plan[1].additional=1u;
    strcpy(plan[2].key,"PSO-1");plan[2].additional=2u;
    struct kui_vmu_game_candidates_repair repair,changed;writes=0;
    CHECK(kui_vmu_game_candidates_preflight(plan,3u,&storage,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_MISSING);
    CHECK(kui_vmu_game_candidates_preflight_repair(plan,3u,&storage,&repair,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_MISSING);
    CHECK(repair.count==1u && !strcmp(repair.entries[0].key,"ALPHA-1") && repair.entries[0].pair==5u &&
          repair.legacy_sequence==1u && !repair.catalogue_sequence && !writes);
    CHECK(!kui_vmu_game_candidates_preflight_confirmed(plan,3u,&storage,&repair,log_line,cancelled));
    changed=repair;++changed.entries[0].pair;
    CHECK(kui_vmu_game_candidates_preflight_confirmed(plan,3u,&storage,&changed,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_STALE);
    struct kui_vmu_game_candidates_storage foreign=storage;foreign.cid[0]^=1u;
    CHECK(kui_vmu_game_candidates_preflight_confirmed(plan,3u,&foreign,&repair,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_STALE);
    CHECK(!writes);protected_unchanged();wizard_unmount();
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
    struct kui_vmu_game_candidates_view alpha=candidates("ALPHA-1"),beta=candidates("BETA-1"),pso=candidates("PSO-1");
    CHECK(alpha.count==1u && beta.count==1u && pso.count==2u);
    struct kui_vmu_game_candidates_storage storage=storage_snapshot();struct kui_vmu_game_candidates_view blank;
    CHECK(kui_vmu_game_candidates_list("BLANK-1",&storage,&blank,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_ABSENT);
    CHECK(pso.default_index==KUI_VMU_GAME_CANDIDATES_NO_DEFAULT);
    static const char *const alpha_names[]={"ALPHA_SYS","ALPHA_SAVE","MYSTERY_.001","MYSTERY_.002"},
        *const beta_names[]={"BETA_SAVE","MYSTERY_.001","MYSTERY_.002"};
    static const char *const pso_names[]={"PSO______SYS","PSO______GCD"};
    check_card(alpha.targets,physical_sources[0],alpha_names,4u,5u);
    check_card(beta.targets,physical_sources[1],beta_names,3u,5u);
    bool seen_sources[2]={false,false};
    for(unsigned n=0;n<2u;n++) {
        char path[96];uint8_t image[KUI_VMU_IMAGE_BYTES];pair_path(path,pso.targets[n].pair,"card.vmd");
        CHECK(file_read(path,image,sizeof(image))==sizeof(image));
        const uint8_t *entry=image+253u*512u;unsigned first=(unsigned)entry[2]|((unsigned)entry[3]<<8);
        unsigned source=image[first*512u+100u]==physical_sources[0][199u*512u+100u]?0u:1u;
        CHECK(!seen_sources[source]);seen_sources[source]=true;
        check_card(pso.targets+n,physical_sources[source],pso_names,2u,45u);
    }
    CHECK(seen_sources[0] && seen_sources[1]);
    unsigned pairs[]={alpha.targets[0].pair,beta.targets[0].pair,pso.targets[0].pair,pso.targets[1].pair};
    for(unsigned i=0;i<4u;i++) {
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
    CHECK(kui_vmu_game_card_select("LEGACY001",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
    require_zero_wire(wire);CHECK(!writes && pair_count()==count);
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
    FILINFO info;CHECK(f_stat("0:/KUI/vmu-test/assign-a.bin",&info)==FR_OK && info.fsize==KUI_VMU_GAME_ASSIGN_WIRE_BYTES &&
        f_stat("0:/KUI/vmu-test/assign-b.bin",&info)==FR_NO_FILE);
    protected_unchanged();
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
    CHECK(candidate_visits==4u && !writes);candidate_visits=0;
    CHECK(!kui_vmu_game_candidates_visit(NULL,candidate_visitor,&candidate_visits,log_line,cancelled));
    CHECK(candidate_visits==4u && !writes);candidate_visits=0;
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
    CHECK(candidate_visits==4u);
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

static void automatic_reimport_defaults(void) {
    struct kui_vmu_game_candidates_storage storage=storage_snapshot();
    struct kui_vmu_game_candidates_view old_pso=candidates("PSO-1"),old_alpha=candidates("ALPHA-1");
    CHECK(old_pso.count==2u && old_alpha.count==1u);
    CHECK(!kui_vmu_game_candidates_set_default("PSO-1",old_pso.targets+1u,old_pso.sequence,&storage,log_line,cancelled));
    old_pso=candidates("PSO-1");CHECK(old_pso.default_index<2u);
    unsigned original_pairs=pair_count();uint8_t before[32],after[32];catalogue_hash(before);
    wizard_unmount();memset(&wizard_request,0,sizeof(wizard_request));strcpy(wizard_request.root,"/Games");
    wizard_ok(KUI_VMU_WIZARD_RESET);wizard_ok(KUI_VMU_WIZARD_CAPTURE);wizard_ok(KUI_VMU_WIZARD_LIST_GAMES);
    CHECK(wizard_view.automatic && wizard_view.selected_games==2u && wizard_view.variants==2u);
    while(wizard_view.unresolved_groups) {
        wizard_request.page=0;wizard_ok(KUI_VMU_WIZARD_SHOW_GROUPS);CHECK(wizard_view.groups_count);
        wizard_request.index=wizard_view.groups[0].index;wizard_request.page=wizard_view.groups_page;
        wizard_ok(KUI_VMU_WIZARD_SKIP_GROUP);
    }
    if(wizard_view.stage!=KUI_VMU_WIZARD_REVIEW) wizard_ok(KUI_VMU_WIZARD_SHOW_REVIEW);
    CHECK(wizard_view.selected_games==2u && wizard_view.variants==2u);
    fail_catalog_once=true;wizard_call(KUI_VMU_WIZARD_COMMIT);
    CHECK(wizard_view.status.errors && !fail_catalog_once && catalog_faults==3u);
    wizard_mount();unsigned prepared_pairs=pair_count();CHECK(prepared_pairs==original_pairs+2u);
    catalogue_hash(after);CHECK(!memcmp(before,after,32u));protected_unchanged();wizard_unmount();
    wizard_ok(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.stage==KUI_VMU_WIZARD_DONE);
    wizard_mount();CHECK(pair_count()==prepared_pairs);
    struct kui_vmu_game_candidates_view pso=candidates("PSO-1"),alpha=candidates("ALPHA-1");
    CHECK(pso.count==3u && pso.default_index<3u &&
          pso.targets[pso.default_index].pair==old_pso.targets[old_pso.default_index].pair);
    CHECK(alpha.count==2u && alpha.default_index<2u && old_alpha.default_index<1u &&
          alpha.targets[alpha.default_index].pair==old_alpha.targets[old_alpha.default_index].pair);
    static const char *const pso_names[]={"PSO______SYS","PSO______GCD"},
        *const alpha_names[]={"ALPHA_SYS","ALPHA_SAVE"};
    unsigned new_pso=0,new_alpha=0;
    for(unsigned i=0;i<pso.count;i++) if(pso.targets[i].pair!=old_pso.targets[0].pair &&
        pso.targets[i].pair!=old_pso.targets[1].pair) {
        check_card(pso.targets+i,physical_sources[0],pso_names,2u,45u);++new_pso;
    }
    for(unsigned i=0;i<alpha.count;i++) if(alpha.targets[i].pair!=old_alpha.targets[0].pair) {
        check_card(alpha.targets+i,physical_sources[0],alpha_names,2u,3u);++new_alpha;
    }
    CHECK(new_pso==1u && new_alpha==1u);protected_unchanged();physical_unchanged();
    uint8_t wire[4096];struct kui_vmu_game_card_choice choice;writes=0;memset(wire,0xa5,sizeof(wire));
    CHECK(kui_vmu_game_card_select("LEGACY001",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
    require_zero_wire(wire);CHECK(!writes && pair_count()==prepared_pairs);
    FILINFO absent;FRESULT stat=f_stat("0:/KUI/vmu-test/p0005",&absent);CHECK(stat==FR_NO_FILE || stat==FR_NO_PATH);
    printf("Automatic reimport: existing defaults and original cards preserved; confirmed new pairs reused after failed catalogue publication\n");
}

/* An unrelated ordinary import and an automatic blank must not take a deleted
 * legacy number after the first repair. Keeping it absent lets the next old
 * game independently confirm its own fresh import, with all old pins intact. */
static void future_missing_legacy_workflow(void) {
    struct kui_vmu_game_candidates_view old_pso=candidates("PSO-1"),old_alpha=candidates("ALPHA-1");
    CHECK(old_pso.default_index<old_pso.count && old_alpha.default_index<old_alpha.count);
    uint8_t catalogue_before[32],catalogue_after[32];catalogue_hash(catalogue_before);
    unsigned initial_pairs=pair_count();uint8_t import_salt[16],import_identity[16];memset(import_salt,0xc9,sizeof(import_salt));
    struct kui_vmu_game_card_choice imported,blank;char imported_path[96];uint8_t imported_image[KUI_VMU_IMAGE_BYTES];
    CHECK(kui_vmu_game_card_import_publish(physical_sources[0],KUI_VMU_IMAGE_BYTES,import_salt,&work,&imported,
        import_identity,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    CHECK(imported.created && imported.pair && imported.pair!=5u && pair_count()==initial_pairs+1u);
    pair_path(imported_path,imported.pair,"card.vmd");
    CHECK(file_read(imported_path,imported_image,sizeof(imported_image))==sizeof(imported_image) &&
          !memcmp(imported_image,physical_sources[0],sizeof(imported_image)));
    uint8_t wire[4096];
    CHECK(kui_vmu_game_card_select("FRESH-1",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&blank,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    CHECK(blank.created && blank.pair && blank.pair!=5u && blank.pair!=imported.pair && pair_count()==initial_pairs+2u);
    char blank_path[96];uint8_t blank_image[KUI_VMU_IMAGE_BYTES];pair_path(blank_path,blank.pair,"card.vmd");
    CHECK(file_read(blank_path,blank_image,sizeof(blank_image))==sizeof(blank_image));
    struct kui_vmu_image_summary summary=wizard_summary(blank_image);
    CHECK(!summary.files && !summary.used_blocks && summary.free_blocks==200u);
    protect_pair(blank.pair,true);pair_path(blank_path,blank.pair,KUI_VMU_GAME_CARD_BINDING_NAME);
    protect(blank_path,KUI_VMU_GAME_CARD_BINDING_BYTES);
    FILINFO absent;FRESULT stat=f_stat("0:/KUI/vmu-test/p0005",&absent);CHECK(stat==FR_NO_FILE || stat==FR_NO_PATH);
    catalogue_hash(catalogue_after);CHECK(!memcmp(catalogue_before,catalogue_after,32u));protected_unchanged();
    unsigned before_repair=pair_count();future_legacy_visible=true;wizard_unmount();
    memset(&wizard_request,0,sizeof(wizard_request));strcpy(wizard_request.root,"/Games");
    wizard_ok(KUI_VMU_WIZARD_RESET);wizard_ok(KUI_VMU_WIZARD_CAPTURE);wizard_ok(KUI_VMU_WIZARD_LIST_GAMES);
    CHECK(wizard_view.automatic && wizard_view.selected_games==2u && wizard_view.variants==2u);
    unsigned resolved=0;
    while(wizard_view.unresolved_groups) {
        wizard_request.page=0;wizard_ok(KUI_VMU_WIZARD_SHOW_GROUPS);CHECK(wizard_view.groups_count);
        struct kui_vmu_wizard_group group=wizard_view.groups[0];wizard_request.index=group.index;
        wizard_request.page=wizard_view.groups_page;
        if(group_details(&group)) {
            wizard_ok(KUI_VMU_WIZARD_OPEN_GROUP);unsigned i=0;
            while(i<wizard_view.count && strcmp(wizard_view.games[i].product,"LEGACY001")) ++i;
            CHECK(i<wizard_view.count);wizard_request.index=wizard_view.games[i].index;
            wizard_request.page=wizard_view.page;wizard_request.group_index=wizard_view.group_index;
            wizard_ok(KUI_VMU_WIZARD_RESOLVE_GROUP);resolved+=group.files;
        } else wizard_ok(KUI_VMU_WIZARD_SKIP_GROUP);
    }
    if(wizard_view.stage!=KUI_VMU_WIZARD_REVIEW) wizard_ok(KUI_VMU_WIZARD_SHOW_REVIEW);
    CHECK(resolved==2u && wizard_view.selected_games==3u && wizard_view.variants==3u);
    writes=0;wizard_ok(KUI_VMU_WIZARD_COMMIT);
    CHECK(wizard_view.missing_card_confirmation && wizard_view.missing_cards==1u && wizard_view.missing_pair==5u &&
          !strcmp(wizard_view.missing_product,"LEGACY001") && !wizard_view.publication_started && !writes);
    wizard_ok(KUI_VMU_WIZARD_CONFIRM_MISSING);CHECK(wizard_view.stage==KUI_VMU_WIZARD_DONE);
    wizard_mount();CHECK(pair_count()==before_repair+3u);
    struct kui_vmu_game_candidates_view pso=candidates("PSO-1"),alpha=candidates("ALPHA-1"),legacy=candidates("LEGACY001");
    CHECK(pso.count==old_pso.count+1u && pso.default_index<pso.count &&
        pso.targets[pso.default_index].pair==old_pso.targets[old_pso.default_index].pair);
    CHECK(alpha.count==old_alpha.count+1u && alpha.default_index<alpha.count &&
        alpha.targets[alpha.default_index].pair==old_alpha.targets[old_alpha.default_index].pair);
    CHECK(legacy.count==1u && legacy.targets[0].pair!=5u && legacy.default_index==KUI_VMU_GAME_CANDIDATES_NO_DEFAULT);
    static const char *const names[]={"MYSTERY_.001","MYSTERY_.002"};
    check_card(legacy.targets,physical_sources[0],names,2u,2u);
    struct kui_vmu_game_card_choice chosen;
    CHECK(kui_vmu_game_card_select("LEGACY001",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&chosen,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    CHECK(chosen.pair==legacy.targets[0].pair && !chosen.created);
    writes=0;memset(wire,0xa5,sizeof(wire));
    CHECK(kui_vmu_game_card_select("LEGACY002",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,wire,&chosen,log_line,cancelled)==KUI_VMU_GAME_CARD_FAILED);
    require_zero_wire(wire);CHECK(!writes);
    stat=f_stat("0:/KUI/vmu-test/p0005",&absent);CHECK(stat==FR_NO_FILE || stat==FR_NO_PATH);
    CHECK(file_read(imported_path,imported_image,sizeof(imported_image))==sizeof(imported_image) &&
          !memcmp(imported_image,physical_sources[0],sizeof(imported_image)));
    protected_unchanged();physical_unchanged();
    printf("Future repair: ordinary import and automatic blank skip old p0005; a different legacy game can confirm its own fresh import, preserving every v1 byte and healthy default\n");
}

static void memory_import(void) {
    memset(&wizard_request,0,sizeof(wizard_request));strcpy(wizard_request.root,"/Games");
    wizard_ok(KUI_VMU_WIZARD_RESET);wizard_ok(KUI_VMU_WIZARD_CAPTURE);
    wizard_ok(KUI_VMU_WIZARD_LIST_GAMES);CHECK(wizard_view.automatic);
}
static bool pending_named(const char *prefix,struct kui_vmu_wizard_group *found) {
    unsigned pages=wizard_view.unresolved_groups?(wizard_view.unresolved_groups-1u)/8u+1u:0u;
    for(unsigned page=0u;page<pages;page++) {
        wizard_request.page=page;wizard_ok(KUI_VMU_WIZARD_SHOW_GROUPS);
        unsigned count=wizard_view.groups_count;
        for(unsigned i=0u;i<count;i++) {
            struct kui_vmu_wizard_group group=wizard_view.groups[i];
            wizard_request.index=group.index;wizard_request.page=page;
            wizard_ok(KUI_VMU_WIZARD_OPEN_DETAILS);bool match=false;
            for(unsigned j=0u;j<wizard_view.saves_count;j++)
                if(!strncmp(wizard_view.saves[j].name,prefix,strlen(prefix))) match=true;
            wizard_ok(KUI_VMU_WIZARD_CLOSE_DETAILS);
            if(match) {if(found) *found=group;return true;}
        }
    }
    return false;
}
static void memory_review(const char *manual_product) {
    while(wizard_view.unresolved_groups) {
        wizard_request.page=0u;wizard_ok(KUI_VMU_WIZARD_SHOW_GROUPS);CHECK(wizard_view.groups_count);
        struct kui_vmu_wizard_group group=wizard_view.groups[0];
        wizard_request.index=group.index;wizard_request.page=wizard_view.groups_page;
        wizard_ok(KUI_VMU_WIZARD_OPEN_DETAILS);
        bool mystery=wizard_view.saves_count && !strncmp(wizard_view.saves[0].name,"MYSTERY_",8u);
        wizard_ok(KUI_VMU_WIZARD_CLOSE_DETAILS);
        if(mystery && manual_product) {
            wizard_ok(KUI_VMU_WIZARD_OPEN_GROUP);unsigned at=0u;
            while(at<wizard_view.count && strcmp(wizard_view.games[at].product,manual_product)) ++at;
            CHECK(at<wizard_view.count);wizard_request.index=wizard_view.games[at].index;
            wizard_request.page=wizard_view.page;wizard_request.group_index=wizard_view.group_index;
            wizard_ok(KUI_VMU_WIZARD_RESOLVE_GROUP);
        } else wizard_ok(KUI_VMU_WIZARD_SKIP_GROUP);
    }
    if(wizard_view.stage!=KUI_VMU_WIZARD_REVIEW) wizard_ok(KUI_VMU_WIZARD_SHOW_REVIEW);
}
static uint8_t *source_header(unsigned index) {
    uint8_t *image=physical_sources[0];
    uint8_t *entry=image+(253u-index/16u)*512u+(index%16u)*32u;
    unsigned block=details16(entry+2u);
    for(unsigned i=0u;i<details16(entry+26u);i++) block=details16(image+254u*512u+block*2u);
    CHECK(block<200u);return image+block*512u;
}
/* These sessions run through the actual wizard and preserve the preceding
 * recovery/candidate tests' complete disk. Only authored VMU metadata changes;
 * none of the fixtures contains a retail save or payload. */
static void remembered_workflows(void) {
    wizard_unmount();uint8_t *disk_seed=malloc(disk_bytes),*sources_seed=malloc(sizeof(physical_sources));
    CHECK(disk_seed && sources_seed);memcpy(disk_seed,disk,disk_bytes);
    memcpy(sources_seed,physical_sources,sizeof(physical_sources));uint8_t hashes_seed[2][32];
    memcpy(hashes_seed,physical_hashes,sizeof(hashes_seed));
    for(unsigned i=4u;i<6u;i++) {
        uint8_t *header=source_header(i);memset(header,0,64u);
        memcpy(header,"BONUS PROFILE",13u);memcpy(header+16u,"Unidentified bonus profile",26u);memcpy(header+48u,"SEGA",4u);
    }
    wizard_hash(physical_sources[0],KUI_VMU_IMAGE_BYTES,physical_hashes[0]);
    struct kui_vmu_match_memory_target target;
    CHECK(memory_target("MYSTERY_",&target)==KUI_VMU_MATCH_MEMORY_MISSING);

    /* An unaccepted Beta draft containing one inferred PSO file must not
     * make the inferred complete PSO assignment a remembered manual choice. */
    memory_import();wizard_request.page=0u;wizard_ok(KUI_VMU_WIZARD_SHOW_GAMES);
    unsigned beta=0u;while(beta<wizard_view.count && strcmp(wizard_view.games[beta].product,"BETA-1")) ++beta;
    CHECK(beta<wizard_view.count);wizard_request.index=wizard_view.games[beta].index;
    wizard_ok(KUI_VMU_WIZARD_OPEN_SAVES);wizard_request.index=0u;wizard_request.page=0u;
    wizard_ok(KUI_VMU_WIZARD_TOGGLE_SAVE);CHECK(wizard_view.saves[0].picked);
    wizard_request.page=0u;wizard_ok(KUI_VMU_WIZARD_SHOW_GROUPS);memory_review(NULL);
    wizard_ok(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.stage==KUI_VMU_WIZARD_DONE);
    CHECK(memory_target("PSO_",&target)==KUI_VMU_MATCH_MEMORY_MISSING);

    memory_import();CHECK(pending_named("MYSTERY_",NULL));memory_review("ALPHA-1");
    unsigned before=writes;cancelled_flag=true;wizard_call(KUI_VMU_WIZARD_COMMIT);cancelled_flag=false;
    CHECK(wizard_view.status.stopped && writes==before && memory_target("MYSTERY_",&target)==KUI_VMU_MATCH_MEMORY_MISSING);
    memory_import();CHECK(pending_named("MYSTERY_",NULL));memory_review("ALPHA-1");
    wizard_ok(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.stage==KUI_VMU_WIZARD_DONE);
    CHECK(memory_target("MYSTERY_",&target)==KUI_VMU_MATCH_MEMORY_OK &&
          !strcmp(target.product,"ALPHA-1") && !strcmp(target.title,"Alpha Adventure"));
    memory_import();CHECK(strstr(wizard_view.status.message,"remembered matches"));
    CHECK(!pending_named("MYSTERY_",NULL));
    wizard_ok(KUI_VMU_WIZARD_SHOW_REVIEW);unsigned alpha=0u;
    while(alpha<wizard_view.count && strcmp(wizard_view.games[alpha].product,"ALPHA-1")) ++alpha;
    CHECK(alpha<wizard_view.count && wizard_view.games[alpha].saves==4u && wizard_view.games[alpha].blocks==5u);
    /* A prior user choice is applied only to the same still-eligible release. */
    const enum memory_catalog_mode refused[]={MEMORY_CATALOG_MISSING,MEMORY_CATALOG_INELIGIBLE,MEMORY_CATALOG_TITLE_CHANGED};
    for(unsigned i=0u;i<sizeof(refused)/sizeof(refused[0]);i++) {
        memory_catalog=refused[i];before=writes;memory_import();
        CHECK(pending_named("MYSTERY_",NULL) && writes==before);
        CHECK(memory_target("MYSTERY_",&target)==KUI_VMU_MATCH_MEMORY_OK && !strcmp(target.product,"ALPHA-1"));
    }
    memory_catalog=MEMORY_CATALOG_NORMAL;
    /* Even a matching stored key cannot override contradictory current text. */
    uint8_t *header=source_header(4u);memset(header,0,64u);
    memcpy(header,"SONIC ADVENTURE",15u);memcpy(header+16u,"SONIC ADVENTURE 2",17u);memcpy(header+48u,"SEGA",4u);
    wizard_hash(physical_sources[0],KUI_VMU_IMAGE_BYTES,physical_hashes[0]);
    memory_seed("MYSTERY_","ALPHA-1","Alpha Adventure");before=writes;memory_import();
    struct kui_vmu_wizard_group conflict;CHECK(pending_named("MYSTERY_",&conflict) && writes==before);
    CHECK(strstr(conflict.reason,"different games"));
    /* Two plausible PSO releases narrow the picker to those games. A stored
     * unrelated target may not bypass that ambiguity or select either one. */
    memory_seed("PSO_","ALPHA-1","Alpha Adventure");memory_catalog=MEMORY_CATALOG_AMBIGUOUS;
    before=writes;memory_import();struct kui_vmu_wizard_group ambiguous;
    CHECK(pending_named("PSO_",&ambiguous) && writes==before);
    CHECK(strstr(ambiguous.reason,"More than one game"));
    wizard_request.index=ambiguous.index;wizard_request.page=wizard_view.groups_page;
    wizard_ok(KUI_VMU_WIZARD_OPEN_GROUP);CHECK(wizard_view.count==2u);
    for(unsigned i=0u;i<wizard_view.count;i++) CHECK(!strncmp(wizard_view.games[i].product,"PSO-",4u));
    physical_unchanged();
    const enum memory_catalog_mode no_games[]={MEMORY_CATALOG_EMPTY,MEMORY_CATALOG_NO_FOLDER};
    for(unsigned i=0u;i<sizeof(no_games)/sizeof(no_games[0]);i++) {
        memory_catalog=no_games[i];wizard_mount();unsigned pairs_before=pair_count();
        uint8_t before_catalogue[32],after_catalogue[32];catalogue_hash(before_catalogue);wizard_unmount();
        memory_import();CHECK(wizard_view.total==0u && !wizard_view.selected_games && !wizard_view.variants);
        struct kui_vmu_wizard_group known;CHECK(pending_named("PSO_",&known));
        CHECK(!strcmp(known.label,"Phantasy Star Online") && strstr(known.reason,"not found in library"));
        CHECK(!wizard_view.selected_games && !wizard_view.variants);
        memory_review(NULL);wizard_ok(KUI_VMU_WIZARD_COMMIT);
        CHECK(wizard_view.stage==KUI_VMU_WIZARD_DONE && !wizard_view.selected_games && !wizard_view.variants &&
              wizard_view.sources==1u && wizard_view.summary[0].pair);
        char archive[128];snprintf(archive,sizeof(archive),"0:/KUI/vmu/cards/v%04u/card.vmd",wizard_view.summary[0].pair);
        wizard_mount();struct kui_vmu_collection_card verified;
        CHECK(!kui_vmu_collection_verify(archive,&verified,log_line,cancelled));
        CHECK(verified.summary.files==13u && !memcmp(verified.sha256,physical_hashes[0],32u));
        CHECK(pair_count()==pairs_before);catalogue_hash(after_catalogue);
        CHECK(!memcmp(before_catalogue,after_catalogue,sizeof(before_catalogue)));wizard_unmount();
    }
    wizard_ok(KUI_VMU_WIZARD_RESET);memory_catalog=MEMORY_CATALOG_NORMAL;
    memcpy(disk,disk_seed,disk_bytes);memcpy(physical_sources,sources_seed,sizeof(physical_sources));
    memcpy(physical_hashes,hashes_seed,sizeof(hashes_seed));free(sources_seed);free(disk_seed);
    printf("Remembered wizard matches: successful manual commit/reimport, cancelled import, missing/renamed/ineligible games, conflicting text, restricted ambiguous releases, abandoned manual drafts and no-library backup-only imports\n");
}

int main(int argc,char **argv) {
    CHECK(argc==2);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));
    long bytes=ftell(input);CHECK(bytes>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)bytes;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));source_images();
    wizard_mount();bool exfat=fs.fs_type==FS_EXFAT;wizard_unmount();plans();
    /* Identical metadata was manually assigned to different games on these
     * two sources. A previously remembered choice must become a tombstone
     * only after their conflicting final choices successfully commit. */
    memory_seed("MYSTERY_","ALPHA-1","Alpha Adventure");legacy_missing_preflight();
    cancelled_flag=true;wizard_call(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.status.stopped);
    struct kui_vmu_match_memory_target remembered;
    CHECK(memory_target("MYSTERY_",&remembered)==KUI_VMU_MATCH_MEMORY_OK && !strcmp(remembered.product,"ALPHA-1"));
    cancelled_flag=false;writes=0;wizard_ok(KUI_VMU_WIZARD_COMMIT);
    CHECK(wizard_view.missing_card_confirmation && wizard_view.missing_cards==1u && wizard_view.missing_pair==5u &&
          !strcmp(wizard_view.missing_product,"ALPHA-1") && !wizard_view.publication_started && !writes);
    wizard_ok(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.missing_card_confirmation && !writes);
    wizard_ok(KUI_VMU_WIZARD_CANCEL_MISSING);CHECK(!wizard_view.missing_card_confirmation && !writes);
    wizard_call(KUI_VMU_WIZARD_CONFIRM_MISSING);CHECK(wizard_view.status.errors && !writes);
    wizard_ok(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.missing_card_confirmation && !writes);
    fail_catalog_once=true;wizard_call(KUI_VMU_WIZARD_CONFIRM_MISSING);
    if(!(wizard_view.status.errors && catalog_faults==1u && !fail_catalog_once))
        fprintf(stderr,"Confirmed import: errors=%u faults=%u armed=%u last=%s\n",wizard_view.status.errors,catalog_faults,fail_catalog_once,last_log);
    CHECK(wizard_view.status.errors && catalog_faults==1u && !fail_catalog_once);
    wizard_mount();unsigned retained=pair_count();CHECK(retained==4u);
    FILINFO reserved;FRESULT reserved_status=f_stat("0:/KUI/vmu-test/p0005",&reserved);
    CHECK(reserved_status==FR_NO_FILE || reserved_status==FR_NO_PATH);archive_check(true);wizard_unmount();
    cancel_at_catalog=true;wizard_call(KUI_VMU_WIZARD_COMMIT);
    CHECK(wizard_view.status.stopped && !cancel_at_catalog);cancelled_flag=false;
    wizard_mount();CHECK(pair_count()==retained);archive_check(false);protected_unchanged();
    struct kui_vmu_game_candidates_storage empty_storage=storage_snapshot();struct kui_vmu_game_candidates_view empty;
    CHECK(kui_vmu_game_candidates_list("PSO-1",&empty_storage,&empty,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_ABSENT);
    wizard_unmount();
    wizard_ok(KUI_VMU_WIZARD_COMMIT);CHECK(wizard_view.stage==KUI_VMU_WIZARD_DONE);
    memory_forgotten();
    wizard_mount();CHECK(pair_count()==retained);
    reserved_status=f_stat("0:/KUI/vmu-test/p0005",&reserved);CHECK(reserved_status==FR_NO_FILE || reserved_status==FR_NO_PATH);
    result_cards();archive_check(false);protected_unchanged();
    selector_and_defaults();visitor_refusals();catalogue_power_cuts();shared_manual_path();archive_check(false);automatic_reimport_defaults();future_missing_legacy_workflow();protected_unchanged();wizard_unmount();physical_unchanged();
    remembered_workflows();
    printf("VMU wizard real filesystems (%s): %u checks; 33-save automatic plan, unresolved-group picker, exact source archives, separate PSO versions, 200-block cards, interrupted catalogue retry retained pairs, real selector/defaults and manual shared path\n",exfat?"exFAT":"FAT32",checks);
    for(unsigned i=0;i<protected_count;i++) free(protected_files[i].data);
    free(disk);return 0;
}
