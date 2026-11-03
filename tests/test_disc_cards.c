/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual mounted FatFs, card service, journal reader and candidate catalogue.
 * Only the optical drive, physical SD identity and exclusive storage owner are
 * simulated. Every optical lookup asserts that SD storage is released. */
#define main inherited_vmu_library_main
#include "test_vmu_library.c"
#undef main
#include "kui/disc_cards.h"
#include "kui/disc_launcher.h"
#include "kui/games_storage.h"
#include "kui/vmu_backing.h"
#include "kui/vmu_image_pack.h"
#include "kui/clock.h"
#include "kui/hash.h"

static bool host_mounted,disc_present=true,fail_catalog_once,disc_ce;
static unsigned optical_reads,catalog_faults,cid_delta;
static uint8_t disc_stamp=0x41;
static const char *disc_product="T-10001N";
static struct kui_disc_cards_request request;
static struct kui_disc_cards_view view;
static unsigned active_pair;
static uint8_t active_identity[16],save_image[KUI_VMU_IMAGE_BYTES];
static struct kui_vmu_collection_card archive;
static uint8_t active_profile[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES];

static int disc_test_write(void *context,uint32_t lba,size_t count,const uint8_t *data) {
    if(fail_catalog_once) for(size_t i=0;i<count*512u;i+=512u)
        if(!memcmp(data+i,"KUIVMUC2",8u)) {
            fail_catalog_once=false;++catalog_faults;return -1;
        }
    return write_media(context,lba,count,data);
}
static const struct kui_media_ops disc_media={NULL,blocks,read_media,disc_test_write,sync_media};
static void storage_mount(void) {
    CHECK(!host_mounted && !raw_open);
    kui_media_set(&disc_media);CHECK(f_mount(&fs,"0:",1)==FR_OK);
    volume=*kui_media_volume();CHECK(volume.partitioned && volume.start==2048u);host_mounted=true;
}
static void storage_unmount(void) {
    CHECK(host_mounted && !raw_open);CHECK(f_mount(NULL,"0:",0)==FR_OK);host_mounted=false;
}
enum kui_games_storage_result kui_games_storage_open(kui_log_fn log) {
    (void)log;storage_mount();return KUI_GAMES_STORAGE_OK;
}
bool kui_games_storage_release(void) {
    if(host_mounted) storage_unmount();
    return true;
}
void kui_games_storage_hold(bool hold) {(void)hold;}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    struct kui_vmu_game_card_raw raw;CHECK(host_mounted && identity(&volume,&raw));
    memcpy(out->cid,raw.cid,16u);out->cid[0]^=(uint8_t)cid_delta;
    out->sectors=raw.card_sectors;out->high_capacity=raw.high_capacity;return true;
}
bool kui_disc_launcher_metadata(struct kui_game_metadata *metadata,uint8_t signature[32],
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    (void)log;CHECK(!host_mounted && !raw_open);++optical_reads;
    memset(out,0,sizeof(*out));memset(metadata,0,sizeof(*metadata));memset(signature,0,32u);
    if(!disc_present || (cancel && cancel())) {
        out->stopped=cancel && cancel();out->errors=out->stopped?0u:1u;out->complete=true;
        strcpy(out->message,out->stopped?"Disc check stopped":"No readable disc");return false;
    }
    metadata->ip_valid=metadata->boot_valid=true;metadata->native_gd=!disc_ce;metadata->windows_ce=disc_ce;
    strcpy(metadata->title,"Synthetic Sonic Disc");strcpy(metadata->product,disc_product);
    strcpy(metadata->region,"U");strcpy(metadata->version,"V1.000");strcpy(metadata->bootfile,"1ST_READ.BIN");
    metadata->session_lba=45000u;metadata->boot_lba=45100u;metadata->boot_bytes=4096u;
    memset(signature,disc_stamp,32u);out->complete=out->passed=true;
    if(progress) progress(out);
    return true;
}
static void call(enum kui_disc_cards_op op,bool success) {
    request.op=op;++request.generation;
    kui_disc_cards_run(&request,&view,log_line,cancelled,NULL);
    CHECK(!host_mounted && !raw_open && view.generation==request.generation && view.status.complete);
    if(view.status.passed!=success) fprintf(stderr,"Disc cards op%u: %s\n",(unsigned)op,view.status.message);
    CHECK(view.status.passed==success);
    if(!success) CHECK(view.status.errors || view.status.stopped);
}
static void review(void) {
    request.token=view.token;request.candidates_sequence=view.candidates_sequence;
    memcpy(request.product,view.product,sizeof(request.product));
    memcpy(request.signature,view.signature,sizeof(request.signature));request.storage=view.storage;
    request.page=view.page;request.path[0]=0;request.pair=0;memset(request.identity,0,sizeof(request.identity));
}
static void choose(const struct kui_disc_cards_row *row) {
    strcpy(request.path,row->path);request.pair=row->pair;memcpy(request.identity,row->identity,16u);
}
static struct kui_vmu_game_candidates_storage storage_pin(void) {
    struct kui_vmu_sd_identity identity_bytes;CHECK(kui_sd_vmu_profile_identity(&identity_bytes));
    struct kui_vmu_game_candidates_storage result={.card_sectors=identity_bytes.sectors,
        .partition_first=volume.start,.partition_end=(uint64_t)volume.start+volume.count,
        .high_capacity=identity_bytes.high_capacity};
    memcpy(result.cid,identity_bytes.cid,16u);return result;
}
static unsigned pair_directories(void) {
    DIR dir;FILINFO info;unsigned count=0;
    FRESULT r=f_opendir(&dir,KUI_VMU_GAME_CARD_ROOT);
    if(r==FR_NO_PATH || r==FR_NO_FILE)return 0;
    CHECK(r==FR_OK);
    for(;;) {
        CHECK(f_readdir(&dir,&info)==FR_OK);if(!info.fname[0])break;
        if((info.fattrib&AM_DIR) && info.fname[0]=='p' && strlen(info.fname)==5u) ++count;
    }
    CHECK(f_closedir(&dir)==FR_OK);return count;
}
static void image_fixture(void) {
    uint8_t payload[1024];memset(payload,0x6c,sizeof(payload));
    memset(payload,0,128u);memcpy(payload,"Sonic Adventure",15u);memcpy(payload+16u,"SONIC ADVENTURE / Main File",26u);
    memcpy(payload+48u,"SYNTHETIC",9u);
    struct kui_vmu_image_file file={.payload=payload,.bytes=sizeof(payload)};
    file.entry[0]=0x33u;memcpy(file.entry+4u,"SONICADV_INT",12u);put16(file.entry+24u,2u);
    struct kui_vmu_image_summary summary;
    CHECK(kui_vmu_image_pack(&file,1u,save_image,sizeof(save_image),&work.image,&summary)==KUI_VMU_IMAGE_OK);
    CHECK(summary.files==1u && summary.used_blocks==2u);
}
static void seed_cards(void) {
    image_fixture();storage_mount();
    uint8_t fresh[16];memset(fresh,0x91,sizeof(fresh));struct kui_vmu_game_card_choice choice;
    CHECK(kui_vmu_game_card_import_publish(save_image,sizeof(save_image),fresh,&work,&choice,
        active_identity,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);active_pair=choice.pair;
    char path[96];pair_path(path,active_pair,"card.vmd");
    CHECK(kui_vmu_library_rename(path,"Existing active saves",active_identity,log_line,cancelled)==KUI_VMU_LIBRARY_OK);
    struct kui_vmu_game_assignment assignment={.pair=active_pair};
    CHECK(kui_vmu_game_card_key("T-OTHER01",assignment.key));memcpy(assignment.identity,active_identity,16u);
    struct kui_vmu_game_candidates_storage pin=storage_pin();
    CHECK(kui_vmu_game_candidates_apply_selected(&assignment,1u,&pin,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_OK);
    CHECK(kui_vmu_game_card_select("T-OTHER01",volume.start,(uint64_t)volume.start+volume.count,
        NULL,0,&platform,&work,active_profile,&choice,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    struct kui_vmu_persist_profile profile;
    CHECK(!kui_vmu_persist_profile_decode(&profile,active_profile,sizeof(active_profile)));
    uint8_t journal[1024];journal_make(journal,&profile,100u,0x5au);
    pair_path(path,active_pair,"card.journal");file_write(path,journal,sizeof(journal));
    protect_pair(active_pair,true);
    CHECK(!kui_vmu_collection_publish(save_image,sizeof(save_image),"Imported source",&archive,log_line,cancelled));
    protect(archive.path,KUI_VMU_IMAGE_BYTES);
    char archive_journal[192];snprintf(archive_journal,sizeof(archive_journal),"%.*s/card.journal",
        (int)(strlen(archive.path)-9u),archive.path);protect(archive_journal,1024u);
    FILINFO info;FRESULT games=f_stat("0:/Games",&info);CHECK(games==FR_NO_FILE || games==FR_NO_PATH);
    storage_unmount();writes=0;
}
static struct kui_disc_cards_row import_row(bool archived) {
    review();request.page=0u;call(KUI_DISC_CARDS_IMPORTS,true);
    CHECK(view.stage==KUI_DISC_CARDS_IMPORTS_STAGE && view.total==2u && view.count==2u);
    for(unsigned i=0;i<view.count;i++) if(view.rows[i].active!=archived) return view.rows[i];
    CHECK(!"Selected imported card missing");return view.rows[0];
}
static struct kui_vmu_game_candidates_view candidates_for(const char *key) {
    struct kui_vmu_game_candidates_storage pin=storage_pin();struct kui_vmu_game_candidates_view result;
    CHECK(kui_vmu_game_candidates_list(key,&pin,&result,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_OK);
    return result;
}
static void unchanged(void) {
    storage_mount();protected_unchanged();
    struct kui_vmu_game_candidates_view other=candidates_for("T-OTHER01");
    CHECK(other.count==1u && other.default_index==0u && other.targets[0].pair==active_pair);
    storage_unmount();
}
static void test_create(void) {
    review();call(KUI_DISC_CARDS_CREATE,true);
    CHECK(view.stage==KUI_DISC_CARDS_LINKED_STAGE && view.created && view.count==1u && view.linked_count==1u);
    CHECK(view.rows[0].files==0u && view.rows[0].free_blocks==200u && view.rows[0].pair!=active_pair);
    storage_mount();struct kui_vmu_game_candidates_view added=candidates_for(disc_product);
    CHECK(added.count==1u && added.targets[0].pair==view.rows[0].pair);
    uint8_t image_bytes[KUI_VMU_IMAGE_BYTES];CHECK(!kui_vmu_library_read(view.rows[0].path,image_bytes,log_line,cancelled));
    for(unsigned b=0;b<200u;b++) for(unsigned i=0;i<512u;i++) CHECK(!image_bytes[b*512u+i]);
    unsigned before=pair_directories();struct kui_vmu_game_card_choice chosen;uint8_t profile[4096];
    CHECK(kui_vmu_game_card_select(disc_product,volume.start,(uint64_t)volume.start+volume.count,
        NULL,0u,&platform,&work,profile,&chosen,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);
    CHECK(!chosen.created && chosen.pair==view.rows[0].pair && pair_directories()==before);
    storage_unmount();unchanged();
}
static void test_link(bool archived) {
    struct kui_disc_cards_row row=import_row(archived);review();choose(&row);
    call(KUI_DISC_CARDS_LINK,true);CHECK(view.linked_count==1u && view.count==1u);
    if(archived)CHECK(view.rows[0].pair!=active_pair);
    else CHECK(view.rows[0].pair==active_pair);
    storage_mount();uint8_t image_bytes[KUI_VMU_IMAGE_BYTES];
    CHECK(!kui_vmu_library_read(view.rows[0].path,image_bytes,log_line,cancelled));
    if(archived) CHECK(!memcmp(image_bytes,save_image,sizeof(save_image)));
    else for(unsigned i=0;i<512u;i++)CHECK(image_bytes[100u*512u+i]==0x5au);
    storage_unmount();unchanged();
}
static void test_defaults(void) {
    struct kui_disc_cards_row row=import_row(false);review();choose(&row);call(KUI_DISC_CARDS_LINK,true);
    review();choose(view.rows);call(KUI_DISC_CARDS_DEFAULT,true);
    CHECK(view.default_pair==active_pair && view.rows[0].is_default);
    review();call(KUI_DISC_CARDS_CREATE,true);CHECK(view.linked_count==2u && view.default_pair==active_pair);
    review();call(KUI_DISC_CARDS_ASK,true);CHECK(!view.default_pair);
    unchanged();
}
static void test_pagination(void) {
    storage_mount();struct kui_vmu_game_assignment entries[9]={0};
    for(unsigned i=0;i<9u;i++) {
        CHECK(kui_vmu_game_card_key(disc_product,entries[i].key));
        if(!i) {entries[i].pair=active_pair;memcpy(entries[i].identity,active_identity,16u);}
        else {
            struct kui_vmu_game_card_choice choice;uint8_t nonce[16];memset(nonce,(int)(0x20u+i),sizeof(nonce));
            CHECK(kui_vmu_game_card_import_publish(save_image,sizeof(save_image),nonce,&work,&choice,
                entries[i].identity,log_line,cancelled)==KUI_VMU_GAME_CARD_READY);entries[i].pair=choice.pair;
        }
    }
    struct kui_vmu_game_candidates_storage pin=storage_pin();
    CHECK(!kui_vmu_game_candidates_apply(entries,9u,&pin,log_line,cancelled));storage_unmount();
    call(KUI_DISC_CARDS_IDENTIFY,true);CHECK(view.count==8u && view.total==9u && view.linked_count==9u);
    review();request.page=1u;call(KUI_DISC_CARDS_LINKED,true);
    CHECK(view.count==1u && view.page==1u && view.rows[0].pair==entries[8].pair);
    review();choose(view.rows);call(KUI_DISC_CARDS_DEFAULT,true);CHECK(view.default_pair==entries[8].pair);
    unchanged();
}
static void test_legacy_copy(void) {
    storage_mount();uint8_t profile_wire[4096];unsigned legacy=create_game("T-LEGACY1",profile_wire);
    struct kui_vmu_persist_profile profile;CHECK(!kui_vmu_persist_profile_decode(&profile,profile_wire,sizeof(profile_wire)));
    uint8_t journal[1024];journal_make(journal,&profile,100u,0x77u);
    char path[96];pair_path(path,legacy,"card.journal");file_write(path,journal,sizeof(journal));protect_pair(legacy,true);
    storage_unmount();review();call(KUI_DISC_CARDS_IMPORTS,true);CHECK(view.count==3u);
    unsigned selected=0u;while(selected<view.count && view.rows[selected].pair!=legacy) ++selected;CHECK(selected<view.count);
    review();choose(view.rows+selected);call(KUI_DISC_CARDS_LINK,true);
    CHECK(view.linked_count==1u && view.rows[0].pair!=legacy && view.rows[0].pair!=active_pair);
    storage_mount();uint8_t copied[KUI_VMU_IMAGE_BYTES];
    CHECK(!kui_vmu_library_read(view.rows[0].path,copied,log_line,cancelled));
    for(unsigned i=0;i<512u;i++)CHECK(copied[100u*512u+i]==0x77u);
    storage_unmount();unchanged();
}
static struct kui_disc_cards_row listed_pair(unsigned pair) {
    review();request.page=0u;call(KUI_DISC_CARDS_IMPORTS,true);
    for(unsigned i=0;i<view.count;i++)if(view.rows[i].pair==pair)return view.rows[i];
    CHECK(!"Requested source card was not listed");return view.rows[0];
}
static void test_legacy_default(bool create) {
    storage_mount();uint8_t profile[4096];unsigned legacy=create_game(disc_product,profile);
    protect_pair(legacy,true);storage_unmount();
    call(KUI_DISC_CARDS_IDENTIFY,true);
    CHECK(view.linked_count==1u && view.default_pair==legacy && view.rows[0].is_default);
    if(create) {review();call(KUI_DISC_CARDS_CREATE,true);}
    else {
        struct kui_disc_cards_row source=listed_pair(active_pair);review();choose(&source);
        call(KUI_DISC_CARDS_LINK,true);
    }
    CHECK(view.linked_count==2u && view.default_pair==legacy);
    storage_mount();struct kui_vmu_game_candidates_view linked=candidates_for(disc_product);
    CHECK(linked.count==2u && linked.default_index!=KUI_VMU_GAME_CANDIDATES_NO_DEFAULT);
    CHECK(linked.targets[linked.default_index].pair==legacy);
    bool original=false,added=false;
    for(unsigned i=0;i<linked.count;i++) {
        original|=linked.targets[i].pair==legacy;
        added|=linked.targets[i].pair!=legacy && (create || linked.targets[i].pair==active_pair);
    }
    CHECK(original && added);storage_unmount();unchanged();
}
static void test_sharing_changed(bool removed) {
    storage_mount();uint8_t profile[4096];unsigned legacy=create_game("T-LEGACY1",profile);
    protect_pair(legacy,true);char marker_path[96],identity_path[96];uint8_t marker[64]={0};
    pair_path(marker_path,legacy,"import.bin");pair_path(identity_path,legacy,"identity.bin");
    memcpy(marker,"KUIVMUI1",8u);CHECK(file_read(identity_path,marker+16u,16u)==16u);
    put32(marker+60u,kui_crc32(0,marker,60u));
    if(removed)file_write(marker_path,marker,sizeof(marker));
    storage_unmount();struct kui_disc_cards_row source=listed_pair(legacy);
    CHECK(source.readable && source.link_copies!=removed);review();choose(&source);
    storage_mount();
    if(removed)CHECK(f_unlink(marker_path)==FR_OK);
    else file_write(marker_path,marker,sizeof(marker));
    uint8_t readable[KUI_VMU_IMAGE_BYTES];CHECK(!kui_vmu_library_read(source.path,readable,log_line,cancelled));
    storage_unmount();writes=0;call(KUI_DISC_CARDS_LINK,false);
    CHECK(!writes && strstr(view.status.message,"sharing state changed"));
    if(!removed) {storage_mount();CHECK(f_unlink(marker_path)==FR_OK);storage_unmount();}
    unchanged();
}
static void test_legacy_identity_changed(void) {
    storage_mount();uint8_t profile_wire[4096],changed_wire[4096],original_id[16],changed_id[16];
    unsigned legacy=create_game(disc_product,profile_wire);protect_pair(legacy,true);
    char identity_path[96],profile_path[96],leaf[48],image_path[96];
    pair_path(identity_path,legacy,"identity.bin");pair_path(image_path,legacy,"card.vmd");
    snprintf(leaf,sizeof(leaf),"retail-persistent-profile-%04u.bin",legacy);pair_path(profile_path,legacy,leaf);
    CHECK(file_read(identity_path,original_id,sizeof(original_id))==sizeof(original_id));storage_unmount();
    call(KUI_DISC_CARDS_IDENTIFY,true);CHECK(view.linked_count==1u && view.default_pair==legacy);
    uint64_t sequence=view.candidates_sequence;review();
    storage_mount();memcpy(changed_id,original_id,sizeof(changed_id));changed_id[0]^=1u;
    struct kui_vmu_persist_profile profile;CHECK(!kui_vmu_persist_profile_decode(&profile,profile_wire,sizeof(profile_wire)));
    memcpy(profile.image_id,changed_id,sizeof(changed_id));
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,changed_id,sizeof(changed_id));
    kui_sha256_digest(&hash,profile.identity_sha256);
    CHECK(!kui_vmu_persist_profile_encode(&profile,changed_wire));
    file_write(identity_path,changed_id,sizeof(changed_id));file_write(profile_path,changed_wire,sizeof(changed_wire));
    uint8_t readable[KUI_VMU_IMAGE_BYTES];CHECK(!kui_vmu_library_read(image_path,readable,log_line,cancelled));
    storage_unmount();writes=0;call(KUI_DISC_CARDS_CREATE,false);
    CHECK(!writes && view.candidates_sequence==sequence && strstr(view.status.message,"links changed"));
    storage_mount();file_write(identity_path,original_id,sizeof(original_id));file_write(profile_path,profile_wire,sizeof(profile_wire));
    storage_unmount();unchanged();
}
static void test_refusal(const char *mode) {
    struct kui_disc_cards_row row=import_row(false);review();choose(&row);
    if(!strcmp(mode,"disc-swap")) ++disc_stamp;
    else if(!strcmp(mode,"no-disc")) disc_present=false;
    else if(!strcmp(mode,"storage-swap")) cid_delta=1u;
    else if(!strcmp(mode,"cancel")) cancelled_flag=true;
    else if(!strcmp(mode,"stale-token")) ++request.token;
    else if(!strcmp(mode,"wrong-row")) request.identity[0]^=1u;
    else if(!strcmp(mode,"invalid-path")) strcpy(request.path,"0:/KUI/vmu-test/p0001/../card.vmd");
    else if(!strcmp(mode,"source-changed")) {
        storage_mount();char path[96];pair_path(path,active_pair,"identity.bin");
        uint8_t wrong[16];memcpy(wrong,active_identity,16u);wrong[0]^=1u;
        file_write(path,wrong,sizeof(wrong));storage_unmount();
    }
    else if(!strcmp(mode,"catalogue-stale")) {
        storage_mount();struct kui_vmu_game_assignment add={.pair=active_pair};
        CHECK(kui_vmu_game_card_key("T-OTHER02",add.key));memcpy(add.identity,active_identity,16u);
        struct kui_vmu_game_candidates_storage pin=storage_pin();
        CHECK(!kui_vmu_game_candidates_apply(&add,1u,&pin,log_line,cancelled));storage_unmount();
    } else CHECK(!"Unknown refusal mode");
    writes=0;call(KUI_DISC_CARDS_LINK,false);CHECK(!writes);
    if(!strcmp(mode,"source-changed")) {
        storage_mount();char path[96];pair_path(path,active_pair,"identity.bin");
        file_write(path,active_identity,sizeof(active_identity));storage_unmount();
    }
    cid_delta=0u;cancelled_flag=false;disc_present=true;unchanged();
}
static void test_retry(void) {
    review();
    storage_mount();unsigned before=pair_directories();storage_unmount();
    fail_catalog_once=true;call(KUI_DISC_CARDS_CREATE,false);CHECK(catalog_faults==1u && !fail_catalog_once);
    storage_mount();unsigned after=pair_directories();CHECK(after==before+1u);storage_unmount();
    call(KUI_DISC_CARDS_IDENTIFY,true);review();call(KUI_DISC_CARDS_CREATE,true);CHECK(view.linked_count==1u);
    storage_mount();CHECK(pair_directories()==after);storage_unmount();unchanged();
}
static void test_pending_changed(bool identity_changed) {
    struct kui_disc_cards_row source=import_row(true);review();choose(&source);
    storage_mount();unsigned before=pair_directories();storage_unmount();
    fail_catalog_once=true;call(KUI_DISC_CARDS_LINK,false);CHECK(catalog_faults==1u && !fail_catalog_once);
    storage_mount();CHECK(pair_directories()==before+1u);
    DIR dir;FILINFO info;unsigned retained=0u;CHECK(f_opendir(&dir,KUI_VMU_GAME_CARD_ROOT)==FR_OK);
    for(;;) {
        CHECK(f_readdir(&dir,&info)==FR_OK);if(!info.fname[0])break;
        unsigned pair=kui_vmu_game_card_pair_number(info.fname);
        if((info.fattrib&AM_DIR) && pair && pair!=active_pair) {CHECK(!retained);retained=pair;}
    }
    CHECK(f_closedir(&dir)==FR_OK && retained);
    char path[96];
    if(identity_changed) {
        uint8_t id[16];pair_path(path,retained,"identity.bin");CHECK(file_read(path,id,sizeof(id))==sizeof(id));
        id[0]^=1u;file_write(path,id,sizeof(id));
    } else {
        uint8_t payload[KUI_VMU_IMAGE_BYTES];pair_path(path,retained,"card.vmd");
        CHECK(file_read(path,payload,sizeof(payload))==sizeof(payload));payload[100u*512u]^=1u;
        file_write(path,payload,sizeof(payload));
        CHECK(!kui_vmu_library_read(path,payload,log_line,cancelled));
    }
    storage_unmount();call(KUI_DISC_CARDS_IDENTIFY,true);CHECK(!view.linked_count);
    review();call(KUI_DISC_CARDS_IMPORTS,true);bool found=false;
    for(unsigned i=0;i<view.count;i++)if(!strcmp(view.rows[i].path,source.path)) {
        CHECK(view.rows[i].readable && !memcmp(view.rows[i].identity,source.identity,16u));found=true;
    }
    CHECK(found);review();choose(&source);writes=0;call(KUI_DISC_CARDS_LINK,false);CHECK(!writes);
    if(!identity_changed)CHECK(strstr(view.status.message,"retained new card changed"));
    storage_mount();CHECK(pair_directories()==before+1u);
    struct kui_vmu_game_candidates_storage pin=storage_pin();struct kui_vmu_game_candidates_view absent;
    CHECK(kui_vmu_game_candidates_list(disc_product,&pin,&absent,log_line,cancelled)==KUI_VMU_GAME_CANDIDATES_ABSENT);
    storage_unmount();unchanged();
}
int main(int argc,char **argv) {
    CHECK(argc==3);FILE *input=fopen(argv[1],"rb");CHECK(input && !fseek(input,0,SEEK_END));
    long length=ftell(input);CHECK(length>0 && !fseek(input,0,SEEK_SET));disk_bytes=(size_t)length;disk=malloc(disk_bytes);
    CHECK(disk && fread(disk,1,disk_bytes,input)==disk_bytes && !fclose(input));seed_cards();
    if(!strcmp(argv[2],"wince-identify")) disc_ce=true;
    call(KUI_DISC_CARDS_IDENTIFY,true);CHECK(view.identified && view.token && view.linked_count==0u && !view.count && !writes);
    CHECK(!strcmp(view.product,disc_product) && !strcmp(view.metadata.title,"Synthetic Sonic Disc"));
    if(!strcmp(argv[2],"identify") || !strcmp(argv[2],"wince-identify")) {
        CHECK(view.metadata.windows_ce==disc_ce);unchanged();
    }
    else if(!strcmp(argv[2],"create"))test_create();
    else if(!strcmp(argv[2],"link-active"))test_link(false);
    else if(!strcmp(argv[2],"link-archive"))test_link(true);
    else if(!strcmp(argv[2],"defaults"))test_defaults();
    else if(!strcmp(argv[2],"pagination"))test_pagination();
    else if(!strcmp(argv[2],"legacy-copy"))test_legacy_copy();
    else if(!strcmp(argv[2],"legacy-default-create"))test_legacy_default(true);
    else if(!strcmp(argv[2],"legacy-default-link"))test_legacy_default(false);
    else if(!strcmp(argv[2],"sharing-removed"))test_sharing_changed(true);
    else if(!strcmp(argv[2],"sharing-added"))test_sharing_changed(false);
    else if(!strcmp(argv[2],"legacy-identity-changed"))test_legacy_identity_changed();
    else if(!strcmp(argv[2],"pending-copy-identity"))test_pending_changed(true);
    else if(!strcmp(argv[2],"pending-copy-payload"))test_pending_changed(false);
    else if(!strcmp(argv[2],"retry"))test_retry();
    else test_refusal(argv[2]);
    CHECK(!host_mounted && optical_reads && !raw_open);
    for(unsigned i=0;i<protected_count;i++)free(protected_files[i].data);
    free(disk);printf("PASS disc cards %s: %u checks\n",argv[2],checks);return 0;
}
