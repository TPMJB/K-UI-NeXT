/* SPDX-License-Identifier: GPL-3.0-only */
/* Used only by test_games_retail_perf_route.py. Generated executables have
 * no retail SDK code. Model only the complete-proof boundary; the actual
 * launcher, mounted settings, module reads/capabilities and output stay real. */
#include "kui/vmu_native_program.h"
#include "kui/vmu_native_resolution_cache.h"
#include "kui/vmu_native_cache_seed.h"
#include "kui/vmu_game_card_service.h"
#include "kui/vmu_history.h"
#include "kui/native_vmu_standard.h"
#include "kui/clock.h"
struct kui_vmu_sd_identity;

bool __wrap_kui_vmu_native_program_extent(const void *image,size_t bytes,uint32_t load,size_t *out) {
    assert(perf_route_case() && image && bytes==3001u && load==0x8c010000u && out);
    ++test.perf_extent_calls;*out=bytes;return true;
}
bool __wrap_kui_vmu_native_resolution_cache_build(const void *image,size_t bytes,uint32_t load,
    void *wire,size_t wire_bytes,struct kui_vmu_native_resolution *out) {
    assert(perf_route_case() && image && bytes==3001u && load==0x8c010000u &&
        wire && wire_bytes==1024u && out);
    ++test.perf_resolution_calls;memset(wire,0,wire_bytes);memset(out,0,sizeof(*out));
    out->heap.initializer_slot=0x8c010040u;return true;
}
bool __wrap_kui_vmu_native_cache_seed_fill(void *slot,size_t bytes,const void *wire,size_t wire_bytes) {
    assert(perf_route_case() && slot && bytes==1120u && wire && wire_bytes==1024u &&
        test.perf_resolution_calls==1u);
    ++test.perf_seed_calls;return true;
}

/* Card On reaches the genuine post-selection card-profile guard. The small
 * synthetic combined module intentionally has no prepared virtual card.
 * These unrelated platform services must never be used by this route test. */
bool kui_sd_raw_read_ops(struct kui_media_ops *out) {(void)out;assert(false);return false;}
bool kui_sd_vmu_profile_identity(struct kui_vmu_sd_identity *out) {
    (void)out;assert(perf_route_case() && strstr(test.fault,"-ce"));return false;
}
int kui_vmu_history_allocation_mounted(kui_cancel_fn cancel) {(void)cancel;assert(false);return -1;}
int kui_vmu_history_launch_mounted(const struct kui_vmu_game_selection *card,
    struct kui_vmu_history_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    (void)card;(void)out;(void)log;(void)cancel;assert(false);return -1;
}
int kui_vmu_game_candidates_list(const char *product,const struct kui_vmu_game_candidates_storage *storage,
    struct kui_vmu_game_candidates_view *out,kui_log_fn log,kui_cancel_fn cancel) {
    (void)product;(void)storage;(void)out;(void)log;(void)cancel;assert(false);return -1;
}
int kui_vmu_game_card_assignment(const char *product,struct kui_vmu_game_card_work *work,
    struct kui_vmu_game_card_choice *choice,kui_cancel_fn cancel) {
    (void)product;(void)work;(void)choice;(void)cancel;assert(false);return -1;
}
int kui_vmu_game_card_select_boundary(const char *product,const struct kui_vmu_game_candidate_target *target,
    const struct kui_vmu_game_candidates_storage *storage,uint64_t first,uint64_t end,
    const struct kui_vmu_fs_range *forbidden,size_t count,const struct kui_vmu_game_card_platform *platform,
    struct kui_vmu_game_card_work *work,uint8_t wire[KUI_VMU_PERSIST_PROFILE_WIRE_BYTES],
    struct kui_vmu_game_card_choice *choice,kui_vmu_game_card_boundary boundary,void *context,
    kui_log_fn log,kui_cancel_fn cancel) {
    (void)product;(void)target;(void)storage;(void)first;(void)end;(void)forbidden;(void)count;
    (void)platform;(void)work;(void)wire;(void)choice;(void)boundary;(void)context;(void)log;(void)cancel;
    assert(false);return -1;
}

static void check_perf_route(const char *directory) {
    bool on=strstr(test.fault,"-on")!=NULL,legacy=strstr(test.fault,"-legacy")!=NULL;
    bool standard=strstr(test.fault,"-standard")!=NULL,ce=strstr(test.fault,"-ce")!=NULL;
    bool cdda=strstr(test.fault,"-cdda")!=NULL;
    bool missing=strstr(test.fault,"-absent")!=NULL;
    bool bad=strstr(test.fault,"-badcap") || strstr(test.fault,"-corrupt") || strstr(test.fault,"-directory");
    uint32_t reader=standard || cdda?KUI_RETAIL_READER_STANDARD:
        legacy?KUI_RETAIL_READER_ASYNC_EAGER:KUI_RETAIL_READER_ASYNC;
    uint32_t request=reader|(ce?KUI_GAMES_RETAIL_CE_PROBE:0u)|(cdda?KUI_GAMES_RETAIL_CDDA:0u);
    struct kui_runtime_image image={0};struct kui_game_ram_patches patches={0};
    bool result=kui_games_retail_prepare_launch(selected,request,&patches,&image,log_line,cancel);
    bool requested=KUI_PERF_NATIVE_STANDALONE && !on && !legacy && !standard && !ce && !cdda;
    bool selected_standalone=requested && !missing && !bad;
    /* On deliberately stops at its missing prepared-card profile, and CDDA
     * stops at its exact-title admission. Neither may inspect standalone. */
    assert(result==(selected_standalone || (!requested && !on && !cdda && !ce)));
    assert(test.standalone_package_opens==(requested && !missing && !strstr(test.fault,"-directory")?1u:0u));
    if(!standard && !ce && !cdda) {
        assert(test.perf_extent_calls==1u && test.perf_resolution_calls==1u && test.perf_seed_calls==1u);
        assert(!test.family_calls); /* combined preflight succeeded */
    } else assert(!test.perf_extent_calls && !test.perf_resolution_calls && !test.perf_seed_calls);
    if(result) {
        assert((kui_native_standalone_descriptor_find(image.data,image.info.payload_bytes,NULL)==1)==selected_standalone);
        check_mapping(directory,&image);
        host_write(directory,"perf-payload.bin",image.data,image.info.payload_bytes);
    } else assert(!image.data && !image.info.payload_bytes && !test.writes);
    kui_runtime_free(&image);
    /* Compare logical source bytes after report preparation; only the report
     * can be written. Directory/missing modules intentionally have no file. */
    test.active=false;kui_media_set(&media);assert(kui_mount(&fs,log_line));
    export_file(directory,dual_package,"after-retail-native-dual.kui",false);
    export_file(directory,KUI_GAMES_RETAIL_PHYSICAL_PACKAGE,"after-retail-physical.kui",false);
    export_file(directory,ce_package,"after-ce-probe.kui",false);
    if(!missing && !strstr(test.fault,"-directory"))
        export_file(directory,standalone_package,"after-retail-native-standalone.kui",false);
    export_file(directory,"0:/Games/Reader Test/disc.gdi","after-disc.gdi",false);
    export_file(directory,"0:/Games/Reader Test/track03.bin","after-track03.bin",false);
    export_file(directory,KUI_GAME_LAUNCH_REPORT_PATH_A,"after-last-launch-a.txt",true);
    assert(f_mount(NULL,"0:",0)==FR_OK);kui_media_set(NULL);test.active=true;
    printf("Performance route PASS: flag=%u requested=%u selected=%u prepared=%u complete_proof=%u\n",
        KUI_PERF_NATIVE_STANDALONE,requested,selected_standalone,result,test.perf_seed_calls);
}
