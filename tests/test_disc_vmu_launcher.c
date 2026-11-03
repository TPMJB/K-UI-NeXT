/* SPDX-License-Identifier: GPL-3.0-only */
/* Test the actual disc-VMU frontdoor with bounded optical/card stubs. Card and
 * packet admission have their own real-code tests; this exercises mode dispatch,
 * call order, exact GUI selection forwarding and every mounted-session exit. */
#include "kui/disc_launcher.h"
#include "kui/disc_launch.h"
#include "kui/disc_vmu_card.h"
#include "kui/disc_vmu_probe.h"
#include "kui/disc_region.h"
#include "kui/vmu_emulation_settings.h"
#include "kui/vmu_native_cache_seed.h"
#include "kui/vmu_native_resolution_cache.h"
#include "kui/vmu_native_program.h"
#include "platform.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
struct scenario {
    bool connected,mounted,enabled,stop,connect_fail,mount_fail,preference_fail,unmount_fail;
    bool identity_fail,info_fail,baseline_fail,module_fail,callback_skipped;
    bool metadata_fail,metadata_cancel,cancel_after_metadata;
    bool cancel_after_preference,cancel_after_info,cancel_after_card;
    bool cancel_after_probe;
    bool extent_fail,proof_fail,proof_arena,proof_slot_missing,cancel_after_module;
    bool seed_missing,seed_duplicate,seed_malformed,seed_outside_module,seed_retained,packet_bad;
    bool region_free,region_fail,cancel_after_region;
    char console;
    unsigned region_loads,console_reads;
    unsigned vmu_slot;
    char module_package[96];
    struct kui_disc_launcher_region region_forwarded;
    unsigned connect_fail_at,mount_fail_at,unmount_fail_at;
    unsigned connections,mounts,unmounts,disconnects,preferences,identities,infos,baselines,modules,cards,probes,progress,logs;
    unsigned extents,proofs,frees,metadatas;
    uint32_t probe_stage;
    int card_result;
    const struct kui_vmu_game_selection *expected_selection;
    struct kui_app_status last;
    char actual_product[11];
    char log_text[4096];size_t log_bytes;
    char trace[256];size_t trace_count;
} mock;
static struct kui_vmu_game_info info;
static struct kui_app_status status;
static struct kui_runtime_image image;
static struct kui_vmu_game_selection selected;
static struct kui_game_metadata preview_metadata;
static uint8_t baseline_payload[4],dummy_payload[0xe100],proof_wire[1024];
static uint8_t previous_proof[1024];
static bool previous_proof_valid;
enum {SEED_OFFSET=0x2400,MODULE_BYTES=0x5000};
static const uint8_t dummy_executable[256]={0x22,0x4f};
static const uint8_t *launch_executable;
static uint8_t *launch_payload;
static uint32_t launch_executable_bytes,launch_program_bytes,launch_payload_bytes;
static void event(char code) {CHECK(mock.trace_count+1u<sizeof(mock.trace));mock.trace[mock.trace_count++]=code;mock.trace[mock.trace_count]=0;}
static bool cancel(void) {return mock.stop;}
static void log_line(const char *format,...) {
    ++mock.logs;CHECK(mock.log_bytes<sizeof(mock.log_text));
    va_list args;va_start(args,format);
    int n=vsnprintf(mock.log_text+mock.log_bytes,sizeof(mock.log_text)-mock.log_bytes,format,args);
    va_end(args);CHECK(n>=0 && (size_t)n<sizeof(mock.log_text)-mock.log_bytes);
    mock.log_bytes+=(size_t)n;
    CHECK(mock.log_bytes+1u<sizeof(mock.log_text));mock.log_text[mock.log_bytes++]='\n';
    mock.log_text[mock.log_bytes]=0;
}
static void progress(const struct kui_app_status *view) {mock.last=*view;++mock.progress;}
static bool zero(const void *data,size_t bytes) {const uint8_t *p=data;for(size_t i=0;i<bytes;i++)if(p[i])return false;return true;}
static void setup(void) {
    memset(&mock,0,sizeof(mock));mock.enabled=true;mock.card_result=KUI_DISC_VMU_CARD_READY;
    mock.region_free=true;mock.console='?';
    mock.probe_stage=KUI_DISC_VMU_PROBE_READY;
    strcpy(mock.actual_product,"ACTUAL-01");
    memset(&info,0xa5,sizeof(info));memset(&status,0xa5,sizeof(status));memset(&image,0xa5,sizeof(image));
    memset(&selected,0,sizeof(selected));strcpy(selected.product,"ACTUAL-01");selected.selected=true;selected.pair=23u;
    memset(selected.identity,0x23,16u);selected.storage.cid[0]=0x42;selected.storage.card_sectors=2000000u;
    selected.storage.partition_first=2048u;selected.storage.partition_end=1002048u;selected.candidates_sequence=9u;
    mock.expected_selection=&selected;
    preview_metadata=(struct kui_game_metadata){.ip_valid=true,.boot_valid=true,.native_gd=true,.boot_bytes=256u};
    strcpy(preview_metadata.product,mock.actual_product);
    memset(dummy_payload,0,sizeof(dummy_payload));memset(proof_wire,0x6b,sizeof(proof_wire));
    previous_proof_valid=false;launch_executable=dummy_executable;launch_payload=dummy_payload;
    launch_executable_bytes=sizeof(dummy_executable);launch_program_bytes=192u;launch_payload_bytes=sizeof(dummy_payload);
}
void kui_runtime_free(struct kui_runtime_image *out) {
    CHECK(out && (!out->data || out->data==launch_payload));++mock.frees;
    *out=(struct kui_runtime_image){0};
}
bool kui_sd_connect(void) {
    CHECK(!mock.connected && !mock.mounted);event('C');++mock.connections;
    mock.connected=!(mock.connect_fail || mock.connections==mock.connect_fail_at);return mock.connected;
}
void kui_sd_disconnect(void) {
    CHECK(mock.connected);event('D');++mock.disconnects;mock.connected=mock.mounted=false;
}
bool kui_mount(FATFS *fs,kui_log_fn log) {
    CHECK(mock.connected && !mock.mounted && fs && log==log_line);event('M');++mock.mounts;
    if(mock.mount_fail || mock.mounts==mock.mount_fail_at) return false;
    memset(fs,0,sizeof(*fs));fs->fs_type=FS_FAT32;mock.mounted=true;return true;
}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE opt) {
    CHECK(mock.connected && !fs && !strcmp(path,"0:") && !opt);event('U');++mock.unmounts;
    bool fail=mock.unmount_fail || mock.unmounts==mock.unmount_fail_at;
    mock.mounted=false;return fail?FR_DISK_ERR:FR_OK;
}
bool kui_vmu_emulation_settings_load_full(bool *enabled,unsigned *slot,kui_log_fn log) {
    CHECK(mock.connected && mock.mounted && enabled && slot && log==log_line);event('P');++mock.preferences;
    if(mock.preference_fail) return false;
    *enabled=mock.enabled;*slot=mock.vmu_slot;if(mock.cancel_after_preference)mock.stop=true;return true;
}
bool kui_disc_region_load(bool *enabled,kui_log_fn log,kui_cancel_fn stop) {
    CHECK(!mock.connected && !mock.mounted && enabled && log==log_line && stop==cancel);
    event('R');++mock.region_loads;
    if(mock.region_fail || stop())return false;
    *enabled=mock.region_free;if(mock.cancel_after_region)mock.stop=true;return !stop();
}
char kui_disc_region_console(void) {
    CHECK(!mock.connected && !mock.mounted && mock.region_loads==1u && !mock.region_free);
    event('O');++mock.console_reads;return mock.console;
}
bool kui_disc_launcher_identity(struct kui_disc_boot_plan *plan,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn stop,kui_app_progress_fn notify) {
    CHECK(!mock.connected && !mock.mounted && plan && out && log==log_line && stop==cancel && notify==progress);
    event('I');++mock.identities;memset(plan,0,sizeof(*plan));*out=(struct kui_app_status){.complete=true};
    if(mock.identity_fail) {out->errors=1u;strcpy(out->message,"identity refused");return false;}
    strcpy(plan->metadata.product,mock.actual_product);out->passed=true;return true;
}
bool kui_disc_launcher_metadata(struct kui_game_metadata *metadata,uint8_t signature[32],
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn stop,kui_app_progress_fn notify) {
    CHECK(!mock.connected && !mock.mounted && metadata && signature && out);
    CHECK(log==log_line && stop==cancel && notify==progress);event('T');++mock.metadatas;
    memset(metadata,0,sizeof(*metadata));memset(signature,0,32u);*out=(struct kui_app_status){.complete=true};
    if(mock.metadata_fail || mock.metadata_cancel) {
        if(mock.metadata_cancel)mock.stop=true;
        out->stopped=mock.metadata_cancel;out->errors=mock.metadata_cancel?0u:1u;
        strcpy(out->message,mock.metadata_cancel?"metadata stopped":"metadata refused");return false;
    }
    *metadata=preview_metadata;memset(signature,(int)(mock.metadatas&255u),32u);out->passed=true;
    if(mock.cancel_after_metadata)mock.stop=true;
    return true;
}
void kui_vmu_game_info_run(const char *product,bool native,struct kui_vmu_game_info *out,
    kui_log_fn log,kui_cancel_fn stop) {
    CHECK(!mock.connected && !mock.mounted && !strcmp(product,mock.actual_product));
    CHECK(native && out && log==log_line && stop==cancel);event('N');++mock.infos;
    memset(out,0,sizeof(*out));strcpy(out->product,product);out->enabled=mock.enabled;
    out->known=!mock.info_fail;strcpy(out->notice,mock.info_fail?"info refused":"Choose a card");
    if(mock.cancel_after_info)mock.stop=true;
}
bool kui_disc_vmu_probe_run(const void *executable,size_t bytes,uint32_t load,
    struct kui_disc_vmu_probe *out) {
    CHECK(!mock.connected && !mock.mounted && executable==launch_executable &&
        bytes==launch_executable_bytes && load==KUI_DISC_LAUNCH_EXEC_ADDRESS && out);
    event('Q');++mock.probes;
    *out=(struct kui_disc_vmu_probe){.version=KUI_DISC_VMU_PROBE_VERSION,.bytes=sizeof(*out),
        .stage=mock.probe_stage,.file_bytes=launch_executable_bytes,.program_bytes=launch_program_bytes,
        .file_crc32=0x11223344u,.driver_result=KUI_VMU_NATIVE_NOT_FOUND,
        .heap_result=KUI_VMU_NATIVE_HEAP_INVALID};
    if(mock.probe_stage>KUI_DISC_VMU_PROBE_MAPLE_DRIVER) {
        out->driver_result=KUI_VMU_NATIVE_RESOLVED;out->driver.submit=0x8c012340u;
        out->driver.initializer=0x8c012380u;
    }
    if(mock.probe_stage>KUI_DISC_VMU_PROBE_MAPLE_LAYOUT) {
        out->layout.initializer=0x8c012400u;out->layout.tx_base=0x8c200000u;out->layout.rx_base=0x8c210000u;
    }
    if(mock.probe_stage>KUI_DISC_VMU_PROBE_HEAP_DISCOVERY) {
        out->heap_result=KUI_VMU_NATIVE_HEAP_FOUND;out->heap.initializer=0x8c012500u;
        out->heap.initializer_slot=0x8c012600u;out->heap.allocate=0x8c012700u;out->heap.release=0x8c012800u;
    }
    if(mock.probe_stage==KUI_DISC_VMU_PROBE_READY) {
        out->call.load=0x8c012900u;out->call.call=0x8c012902u;out->call.return_pr=0x8c012906u;
    }
    if(mock.cancel_after_probe)mock.stop=true;
    return mock.probe_stage==KUI_DISC_VMU_PROBE_READY;
}
bool kui_vmu_native_program_extent(const void *executable,size_t bytes,uint32_t load,size_t *out) {
    CHECK(!mock.connected && !mock.mounted && executable==launch_executable &&
        bytes==launch_executable_bytes && load==KUI_DISC_LAUNCH_EXEC_ADDRESS && out);
    event('E');++mock.extents;
    if(mock.extent_fail || mock.probe_stage<=KUI_DISC_VMU_PROBE_EXTENT) return false;
    *out=launch_program_bytes;return true;
}
bool kui_vmu_native_resolution_cache_build(const void *executable,size_t bytes,uint32_t load,
    void *wire,size_t wire_bytes,struct kui_vmu_native_resolution *out) {
    CHECK(!mock.connected && !mock.mounted && executable==launch_executable && bytes==launch_program_bytes &&
        load==KUI_DISC_LAUNCH_EXEC_ADDRESS && wire && wire_bytes==sizeof(proof_wire) && out);
    event('H');++mock.proofs;
    if(mock.proof_fail || mock.probe_stage!=KUI_DISC_VMU_PROBE_READY) return false;
    *out=(struct kui_vmu_native_resolution){0};
    out->driver.submit=0x8c012340u;out->driver.initializer=0x8c012380u;
    out->layout.initializer=0x8c012400u;out->layout.tx_base=0x8c200000u;out->layout.rx_base=0x8c210000u;
    out->heap.initializer=0x8c012500u;out->heap.initializer_slot=0x8c012600u;
    out->heap.family=mock.proof_arena?KUI_VMU_NATIVE_HEAP_ARENA:KUI_VMU_NATIVE_HEAP_TAIL32;
    if(mock.proof_slot_missing)out->heap.initializer_slot=0u;
    out->heap.allocate=0x8c012700u;out->heap.release=0x8c012800u;
    out->call.load=0x8c012900u;out->call.call=0x8c012902u;out->call.return_pr=0x8c012906u;
    memcpy(wire,proof_wire,sizeof(proof_wire));
    if(mock.cancel_after_probe)mock.stop=true;
    return true;
}
/* The complete proof builder/digest codec has independent real-code and
 * owner-executable tests. Here the producer is a bounded stub, while the
 * actual seed finder/fill/read and disc packet decoder are linked below. */
bool kui_vmu_native_resolution_cache_record_digest(const void *wire,size_t bytes,uint8_t digest[32]) {
    if(!wire || bytes!=sizeof(proof_wire) ||
       (memcmp(wire,proof_wire,bytes) && (!previous_proof_valid || memcmp(wire,previous_proof,bytes)))) return false;
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,wire,bytes);kui_sha256_digest(&h,digest);
    return true;
}
const char *kui_disc_vmu_probe_stage_text(uint32_t stage) {
    static const char *const text[]={"argument","executable extent","Maple driver","Maple buffer layout",
        "allocator discovery","allocator initializer","allocator startup call","ready"};
    return stage<sizeof(text)/sizeof(text[0])?text[stage]:"unknown";
}
bool kui_disc_launcher_prepare(struct kui_runtime_image *out,struct kui_app_status *view,
    kui_log_fn log,kui_cancel_fn stop,kui_app_progress_fn notify) {
    CHECK(!mock.connected && !mock.mounted && out && zero(out,sizeof(*out)));
    CHECK(view && log==log_line && stop==cancel && notify==progress);event('B');++mock.baselines;
    *view=(struct kui_app_status){.complete=true,.passed=!mock.baseline_fail,.errors=mock.baseline_fail?1u:0u};
    if(mock.baseline_fail)return false;
    out->data=baseline_payload;out->info.payload_bytes=sizeof(baseline_payload);return true;
}
int kui_disc_vmu_card_prepare(const char *product,bool enabled,FATFS *fs,
    const struct kui_vmu_game_selection *selection,struct kui_disc_vmu_card *card,
    kui_log_fn log,kui_cancel_fn stop) {
    CHECK(mock.connected && mock.mounted && enabled && fs && fs->fs_type==FS_FAT32);
    CHECK(product && !strcmp(product,mock.actual_product) && selection==mock.expected_selection);
    CHECK(card && zero(card,sizeof(*card)) && log==log_line && stop==cancel);event('S');++mock.cards;
    if(selection && selection->selected) {
        CHECK(selection->pair==23u && selection->identity[0]==0x23 &&
            selection->storage.cid[0]==0x42 && selection->candidates_sequence==9u);
        if(strcmp(selection->product,product)) {
            strcpy(card->choice.problem,"choice belongs to another game");return KUI_DISC_VMU_CARD_FAILED;
        }
    }
    memset(card->profile_wire,0x5a,sizeof(card->profile_wire));
    if(mock.card_result!=KUI_DISC_VMU_CARD_READY)strcpy(card->choice.problem,"card refused");
    if(mock.cancel_after_card)mock.stop=true;
    return mock.card_result;
}
bool kui_disc_launcher_prepare_module(const char *package,kui_disc_launcher_card_fn callback,
    void *context,const struct kui_disc_launcher_region *region,
    struct kui_runtime_image *out,struct kui_app_status *view,
    kui_log_fn log,kui_cancel_fn stop,kui_app_progress_fn notify) {
    CHECK(!mock.connected && !mock.mounted && package && region && region->region_free==mock.region_free);
    CHECK(region->console==(mock.region_free?0:mock.console));mock.region_forwarded=*region;
    if(!strcmp(package,KUI_DISC_LAUNCH_PACKAGE)) {
        CHECK(!callback && !context);
        return kui_disc_launcher_prepare(out,view,log,stop,notify);
    }
    CHECK(!strcmp(package,mock.vmu_slot==KUI_GAME_VMU_SLOT_A1?
        KUI_DISC_LAUNCH_VMU_A1_PACKAGE:KUI_DISC_LAUNCH_VMU_PACKAGE));
    snprintf(mock.module_package,sizeof(mock.module_package),"%s",package);
    CHECK(callback && context && out && zero(out,sizeof(*out)));
    CHECK(view && log==log_line && stop==cancel && notify==progress);event('V');++mock.modules;
    if(mock.module_fail) {*view=(struct kui_app_status){.complete=true,.errors=1u};return false;}
    uint8_t profile[4096];memset(profile,0xa5,sizeof(profile));
    char refusal[KUI_DISC_LAUNCHER_REFUSAL_BYTES]={0};
    bool ok=mock.callback_skipped || callback(mock.actual_product,launch_executable,launch_executable_bytes,profile,
        refusal,context,log,stop);
    CHECK(!mock.connected && !mock.mounted);
    if(ok && !mock.callback_skipped) {for(size_t i=0;i<sizeof(profile);i++)CHECK(profile[i]==0x5au);}
    else if(mock.callback_skipped) CHECK(mock.proofs==0u && mock.probes==0u && mock.cards==0u);
    else CHECK(zero(profile,sizeof(profile)));
    *view=(struct kui_app_status){.complete=true,.passed=ok,.errors=ok?0u:1u};
    snprintf(view->message,sizeof(view->message),"%s",refusal);
    if(ok) {
        struct kui_disc_launch_plan plan={.flags=KUI_DISC_LAUNCH_VMU_FLAGS,.module_bytes=MODULE_BYTES,
            .ip_offset=MODULE_BYTES,.exec_offset=0xd000u,.exec_bytes=launch_executable_bytes,
            .vmu_profile_offset=(0xd000u+launch_executable_bytes+31u)&~31u,
            .vmu_profile_bytes=4096u,.vmu_transport=1u,.payload_bytes=launch_payload_bytes};
        CHECK(kui_disc_launch_encode(launch_payload+KUI_DISC_LAUNCH_CONFIG_OFFSET,&plan));
        memcpy(launch_payload+plan.exec_offset,launch_executable,launch_executable_bytes);
        if(mock.packet_bad) launch_payload[KUI_DISC_LAUNCH_CONFIG_OFFSET]^=1u;
        if(!mock.seed_missing && !mock.seed_retained) {
            uint8_t seed[KUI_VMU_NATIVE_CACHE_SEED_BYTES];kui_vmu_native_cache_seed_template(seed);
            memcpy(launch_payload+(mock.seed_outside_module?MODULE_BYTES:SEED_OFFSET),seed,sizeof(seed));
            if(mock.seed_duplicate) memcpy(launch_payload+SEED_OFFSET+sizeof(seed),seed,sizeof(seed));
            if(mock.seed_malformed) launch_payload[SEED_OFFSET+56u]^=1u;
        }
        out->data=launch_payload;out->info.payload_bytes=out->info.memory_bytes=launch_payload_bytes;
        out->info.crc32=0xdeadbeefu;
        if(mock.cancel_after_module)mock.stop=true;
    }
    return ok;
}
static bool prepare(const struct kui_vmu_game_selection *selection) {
    mock.expected_selection=selection;
    return kui_disc_launcher_prepare_vmu(selection,&image,&status,log_line,cancel,progress);
}
static void info_run(void) {kui_disc_launcher_vmu_info(&info,&status,log_line,cancel,progress);}
static void released(void) {
    CHECK(!mock.connected && !mock.mounted && mock.connections==mock.disconnects && mock.mounts==mock.unmounts);
}
static void preview_work(unsigned metadatas,unsigned lookups) {
    released();CHECK(mock.metadatas==metadatas && mock.infos==lookups);
    CHECK(!mock.identities && !mock.baselines && !mock.modules && !mock.cards);
    CHECK(!mock.probes && !mock.extents && !mock.proofs && !mock.frees);
    CHECK(status.complete && mock.enabled);
}
static void preview_paths(void) {
    const uint32_t accepted[]={128u,5460080u,KUI_DISC_LAUNCH_EXEC_MAX_BYTES};
    for(unsigned i=0;i<sizeof(accepted)/sizeof(accepted[0]);i++) {
        setup();preview_metadata.boot_bytes=accepted[i];mock.identity_fail=true;
        /* No full executable exists in this preview fixture. Any accidental
         * preflight or fingerprint access encounters a poisoned address. */
        launch_executable=(const void *)(uintptr_t)1;launch_executable_bytes=UINT32_MAX;
        info_run();preview_work(1u,1u);
        CHECK(status.passed && !status.stopped && !status.errors && info.known);
        CHECK(!strcmp(info.product,mock.actual_product) && !strcmp(mock.trace,"CMPUDTN"));
    }
    for(unsigned invalid=0u;invalid<15u;invalid++) {
        setup();
        switch(invalid) {
        case 0u:preview_metadata.windows_ce=true;break;
        case 1u:preview_metadata.native_cd=true;break;
        case 2u:preview_metadata.native_gd=false;break;
        case 3u:preview_metadata.ip_valid=false;break;
        case 4u:preview_metadata.boot_valid=false;break;
        case 5u:preview_metadata.boot_bytes=0u;break;
        case 6u:preview_metadata.boot_bytes=127u;break;
        case 7u:preview_metadata.boot_bytes=KUI_DISC_LAUNCH_EXEC_MAX_BYTES+1u;break;
        case 8u:preview_metadata.product[0]=0;break;
        case 9u:strcpy(preview_metadata.product,"bad/id");break;
        case 10u:memset(preview_metadata.product,'X',sizeof(preview_metadata.product));break;
        case 11u:strcpy(preview_metadata.product,"   ");break;
        case 12u:strcpy(preview_metadata.product,"000-000");break;
        case 13u:preview_metadata.boot_bytes=UINT32_MAX;break;
        case 14u:preview_metadata.boot_bytes=1u;break;
        }
        info_run();preview_work(1u,0u);
        CHECK(!info.known && !status.passed && !status.stopped && status.errors==1u);
        CHECK(!info.product[0] && info.notice[0] && !strcmp(mock.trace,"CMPUDT"));
    }
    for(unsigned fault=0u;fault<3u;fault++) {
        setup();mock.metadata_fail=fault==0u;mock.metadata_cancel=fault==1u;mock.cancel_after_metadata=fault==2u;
        info_run();preview_work(1u,0u);
        CHECK(!info.known && !status.passed && status.stopped==(fault!=0u));
        CHECK(status.errors==(fault?0u:1u) && !info.product[0]);
    }
    /* Every preview checks the currently inserted release again. A failure
     * between valid releases must not expose the preceding card details. */
    setup();info_run();preview_work(1u,1u);CHECK(!strcmp(info.product,"ACTUAL-01"));
    strcpy(mock.actual_product,"OTHER-02");strcpy(preview_metadata.product,mock.actual_product);
    preview_metadata.boot_bytes=5460080u;info_run();preview_work(2u,2u);
    CHECK(status.passed && !strcmp(info.product,"OTHER-02"));
    mock.metadata_fail=true;info_run();preview_work(3u,2u);
    CHECK(!status.passed && !info.known && !info.product[0]);
    mock.metadata_fail=false;strcpy(mock.actual_product,"MK-51006");strcpy(preview_metadata.product,mock.actual_product);
    info_run();preview_work(4u,3u);CHECK(status.passed && !strcmp(info.product,"MK-51006"));
    setup();mock.enabled=false;mock.metadata_fail=mock.metadata_cancel=true;info_run();released();
    CHECK(!mock.enabled && info.known && !info.enabled && status.passed && !status.stopped);
    CHECK(!mock.metadatas && !mock.identities && !mock.infos && !mock.extents && !mock.proofs && !mock.probes && !mock.cards);
    CHECK(!strcmp(mock.trace,"CMPUD"));
}
static void retain_for_next_launch(uint8_t proof_byte) {
    CHECK(image.data==dummy_payload && !mock.connected && !mock.mounted);
    uint8_t seed[KUI_VMU_NATIVE_CACHE_SEED_BYTES],prior[sizeof(proof_wire)];
    memcpy(seed,dummy_payload+SEED_OFFSET,sizeof(seed));memcpy(prior,proof_wire,sizeof(prior));
    setup();memcpy(dummy_payload+SEED_OFFSET,seed,sizeof(seed));
    memcpy(previous_proof,prior,sizeof(prior));previous_proof_valid=true;
    memset(proof_wire,proof_byte,sizeof(proof_wire));mock.seed_retained=true;
}
static void consecutive_launches(void) {
    for(unsigned failure=0u;failure<4u;failure++) {
        setup();CHECK(prepare(&selected));released();
        uint8_t first[KUI_VMU_NATIVE_CACHE_SEED_BYTES];memcpy(first,dummy_payload+SEED_OFFSET,sizeof(first));
        retain_for_next_launch(0x27u);CHECK(prepare(&selected));released();
        CHECK(mock.proofs==1u && mock.extents==1u && mock.cards==1u && !mock.probes && !mock.frees);
        const uint8_t *wire,*pin;
        CHECK(kui_vmu_native_cache_seed_read(dummy_payload+SEED_OFFSET,KUI_VMU_NATIVE_CACHE_SEED_BYTES,&wire,&pin));
        CHECK(!memcmp(wire,proof_wire,sizeof(proof_wire)) && memcmp(wire,previous_proof,sizeof(previous_proof)));
        CHECK(memcmp(first,dummy_payload+SEED_OFFSET,sizeof(first)));
        CHECK(image.info.crc32==kui_crc32(0u,image.data,image.info.payload_bytes));
        uint8_t second[KUI_VMU_NATIVE_CACHE_SEED_BYTES];memcpy(second,dummy_payload+SEED_OFFSET,sizeof(second));
        retain_for_next_launch(0xd3u);
        if(failure==0u)mock.proof_fail=true;
        else if(failure==1u)mock.cancel_after_module=true;
        else if(failure==2u)mock.callback_skipped=true;
        else mock.enabled=false;
        bool ok=prepare(failure==3u?(const struct kui_vmu_game_selection *)(uintptr_t)1:&selected);released();
        CHECK(ok==(failure==3u) && status.complete && status.passed==ok);
        CHECK(!memcmp(second,dummy_payload+SEED_OFFSET,sizeof(second)));
        if(failure==3u) {
            CHECK(!mock.enabled && mock.baselines==1u && image.data==baseline_payload);
            CHECK(!mock.modules && !mock.extents && !mock.proofs && !mock.probes && !mock.cards && !mock.frees);
        } else {
            CHECK(!image.data && !status.passed && status.stopped==(failure==1u));
            CHECK(status.errors==(failure==1u?0u:1u) && mock.modules==1u);
            CHECK(mock.frees==(failure==0u?0u:1u));
            if(failure==0u)CHECK(mock.extents==1u && mock.proofs==1u && mock.probes==1u && !mock.cards);
            else if(failure==1u)CHECK(mock.extents==1u && mock.proofs==1u && !mock.probes && mock.cards==1u);
            else CHECK(!mock.extents && !mock.proofs && !mock.probes && !mock.cards);
        }
    }
}
static void large_synthetic_launch(void) {
    setup();launch_executable_bytes=5460080u;launch_program_bytes=launch_executable_bytes;
    uint8_t *generated=malloc(launch_executable_bytes);CHECK(generated);
    for(uint32_t i=0;i<launch_executable_bytes;i++)generated[i]=(uint8_t)(i*29u+(i>>16u)+11u);
    launch_executable=generated;
    launch_payload_bytes=((0xd000u+launch_executable_bytes+31u)&~31u)+4096u;
    launch_payload=calloc(1u,launch_payload_bytes);CHECK(launch_payload);
    CHECK(prepare(&selected));released();
    CHECK(image.data==launch_payload && status.complete && status.passed);
    CHECK(mock.extents==1u && mock.proofs==1u && mock.cards==1u && !mock.probes && !mock.frees);
    struct kui_disc_launch_plan plan;
    CHECK(kui_disc_launch_decode(launch_payload+KUI_DISC_LAUNCH_CONFIG_OFFSET,&plan));
    CHECK(plan.exec_bytes==5460080u && plan.payload_bytes==launch_payload_bytes);
    CHECK(!memcmp(launch_payload+plan.exec_offset,generated,launch_executable_bytes));
    const uint8_t *wire,*pin;
    CHECK(kui_vmu_native_cache_seed_read(launch_payload+SEED_OFFSET,KUI_VMU_NATIVE_CACHE_SEED_BYTES,&wire,&pin));
    CHECK(!memcmp(wire,proof_wire,sizeof(proof_wire)));
    CHECK(strstr(mock.log_text,"full bytes 5460080; program bytes 5460080"));
    CHECK(image.info.crc32==kui_crc32(0u,image.data,image.info.payload_bytes));
    kui_runtime_free(&image);CHECK(!image.data && mock.frees==1u);
    free(launch_payload);free(generated);
}
int main(void) {
    setup();mock.enabled=false;info_run();released();
    CHECK(info.known && !info.enabled && !info.product[0] && !strcmp(info.label,"Physical VMUs"));
    CHECK(status.complete && status.passed && !status.errors && !mock.metadatas && !mock.identities && !mock.infos && !mock.cards);
    CHECK(!strcmp(mock.trace,"CMPUD"));
    setup();mock.enabled=false;CHECK(prepare((const struct kui_vmu_game_selection *)(uintptr_t)1));released();
    CHECK(mock.baselines==1u && !mock.modules && !mock.identities && !mock.infos && !mock.cards && !mock.probes && !mock.proofs && !mock.extents);
    CHECK(!strcmp(mock.trace,"RCMPUDB") && mock.region_loads==1u && !mock.console_reads && mock.region_forwarded.region_free);
    setup();mock.enabled=false;mock.baseline_fail=true;CHECK(!prepare((const struct kui_vmu_game_selection *)(uintptr_t)1));released();
    CHECK(!image.data && status.errors==1u && mock.baselines==1u && !mock.modules && !mock.cards);
    setup();info_run();released();CHECK(info.known && info.enabled && !strcmp(info.product,"ACTUAL-01"));
    CHECK(status.passed && !mock.identities && mock.metadatas==1u && mock.infos==1u && !strcmp(mock.trace,"CMPUDTN"));
    setup();mock.metadata_fail=true;info_run();released();CHECK(!status.passed && status.errors==1u && !mock.infos && strstr(info.notice,"metadata refused"));
    setup();mock.info_fail=true;info_run();released();CHECK(!status.passed && status.errors==1u && strstr(status.message,"info refused"));
    setup();mock.cancel_after_info=true;info_run();released();CHECK(status.stopped && !status.errors && !status.passed);
    setup();CHECK(prepare(&selected));released();
    CHECK(mock.modules==1u && !mock.baselines && mock.cards==1u && !mock.probes && mock.proofs==1u && mock.extents==1u && image.data && status.passed);
    CHECK(!strcmp(mock.trace,"RCMPUDVEHCMSUD"));
    char fingerprint[128];snprintf(fingerprint,sizeof(fingerprint),
        "product ACTUAL-01; exact file CRC32 %08x; full bytes 256; program bytes 192",
        kui_crc32(0u,dummy_executable,sizeof(dummy_executable)));
    CHECK(strstr(mock.log_text,fingerprint));
    CHECK(strstr(mock.log_text,"ready; driver result 1; heap result 1") &&
        strstr(mock.log_text,"Maple submit 8c012340") && strstr(mock.log_text,"initializer 8c012380") &&
        strstr(mock.log_text,"layout initializer 8c012400; TX 8c200000; RX 8c210000") &&
        strstr(mock.log_text,"heap initializer 8c012500; slot 8c012600; allocate 8c012700; release 8c012800") &&
        strstr(mock.log_text,"heap load 8c012900; call 8c012902; return 8c012906"));
    CHECK(!strcmp(mock.module_package,KUI_DISC_LAUNCH_VMU_PACKAGE));
    const uint8_t *seed_wire,*seed_digest;
    CHECK(kui_vmu_native_cache_seed_read(dummy_payload+SEED_OFFSET,KUI_VMU_NATIVE_CACHE_SEED_BYTES,&seed_wire,&seed_digest));
    CHECK(!memcmp(seed_wire,proof_wire,sizeof(proof_wire)) && seed_digest==dummy_payload+SEED_OFFSET+24u);
    CHECK(image.info.crc32==kui_crc32(0u,image.data,image.info.payload_bytes));
    setup();mock.vmu_slot=KUI_GAME_VMU_SLOT_A1;CHECK(prepare(&selected));released();
    CHECK(mock.modules==1u && !mock.baselines && mock.cards==1u && !mock.probes && mock.proofs==1u && status.passed);
    CHECK(!strcmp(mock.module_package,KUI_DISC_LAUNCH_VMU_A1_PACKAGE));
    CHECK(kui_vmu_native_cache_seed_read(dummy_payload+SEED_OFFSET,KUI_VMU_NATIVE_CACHE_SEED_BYTES,&seed_wire,&seed_digest));
    CHECK(image.info.crc32==kui_crc32(0u,image.data,image.info.payload_bytes));
    for(unsigned mode=0u;mode<4u;mode++) {
        setup();mock.seed_missing=mode==0u;mock.seed_duplicate=mode==1u;
        mock.seed_malformed=mode==2u;mock.seed_outside_module=mode==3u;
        CHECK(prepare(&selected));released();
        CHECK(mock.proofs==1u && mock.probes==0u && mock.cards==1u && !mock.frees && status.passed);
        CHECK(strstr(mock.log_text,"no unique valid mapping seed; retaining complete cold discovery"));
        CHECK(image.info.crc32==0xdeadbeefu);
        if(mode==3u)CHECK(!kui_vmu_native_cache_seed_read(dummy_payload+MODULE_BYTES,KUI_VMU_NATIVE_CACHE_SEED_BYTES,&seed_wire,&seed_digest));
    }
    setup();mock.packet_bad=true;CHECK(!prepare(&selected));released();
    CHECK(!image.data && mock.frees==1u && mock.proofs==1u && mock.cards==1u && status.errors==1u && !status.passed);
    setup();mock.cancel_after_module=true;CHECK(!prepare(&selected));released();
    CHECK(!image.data && mock.frees==1u && status.stopped && !status.errors && !status.passed);
    setup();mock.callback_skipped=true;CHECK(!prepare(&selected));released();
    CHECK(!image.data && mock.frees==1u && !mock.proofs && !mock.probes && !mock.cards && status.errors==1u);
    for(unsigned mode=0u;mode<4u;mode++) {
        setup();mock.extent_fail=mode==0u;mock.proof_fail=mode==1u;
        mock.proof_arena=mode==2u;mock.proof_slot_missing=mode==3u;
        CHECK(!prepare(&selected));released();
        CHECK(!image.data && !mock.cards && mock.probes==1u && mock.extents==1u && status.errors==1u);
        CHECK(mock.proofs==(mode?1u:0u) && strstr(status.message,"complete lifetime proof"));
    }
    setup();mock.vmu_slot=KUI_GAME_VMU_SLOT_A1;mock.enabled=false;
    CHECK(prepare((const struct kui_vmu_game_selection *)(uintptr_t)1));released();
    CHECK(mock.baselines==1u && !mock.modules && !mock.cards && !mock.probes && !mock.identities);
    setup();mock.vmu_slot=KUI_GAME_VMU_SLOT_COUNT;CHECK(!prepare(&selected));released();
    CHECK(!mock.modules && !mock.baselines && !mock.cards && !mock.probes && !image.data && status.errors==1u);
    for(uint32_t stage=KUI_DISC_VMU_PROBE_ARGUMENT;stage<KUI_DISC_VMU_PROBE_READY;stage++) {
        setup();mock.probe_stage=stage;CHECK(!prepare(&selected));released();
        CHECK(mock.probes==1u && !mock.cards && mock.connections==1u && !image.data && status.errors==1u);
        CHECK(!strcmp(mock.trace,stage<=KUI_DISC_VMU_PROBE_EXTENT?"RCMPUDVEQ":"RCMPUDVEHQ") && strstr(status.message,"Virtual VMU unsupported:") &&
            strstr(status.message,"R: Save Log") && strstr(mock.log_text,kui_disc_vmu_probe_stage_text(stage)));
        if(stage==KUI_DISC_VMU_PROBE_MAPLE_DRIVER)
            CHECK(strstr(status.message,"Maple driver") && strstr(mock.log_text,"driver result 0; heap result -1"));
        if(stage==KUI_DISC_VMU_PROBE_MAPLE_LAYOUT)
            CHECK(strstr(status.message,"Maple buffer layout") && strstr(mock.log_text,"driver result 1; heap result -1"));
    }
    setup();mock.cancel_after_probe=true;CHECK(!prepare(&selected));released();
    CHECK(!mock.probes && mock.proofs==1u && !mock.cards && mock.connections==1u && !image.data);
    setup();CHECK(prepare(NULL));released();CHECK(mock.cards==1u && status.passed);
    setup();strcpy(mock.actual_product,"OTHER-02");CHECK(!prepare(&selected));released();
    CHECK(!mock.cards && !image.data && !status.passed && status.errors==1u && mock.logs==1u && mock.connections==1u);
    setup();memset(&selected,0,sizeof(selected));strcpy(selected.product,"ACTUAL-01");
    CHECK(prepare(&selected));released();CHECK(mock.cards==1u && status.passed);
    setup();memset(&selected,0,sizeof(selected));strcpy(selected.product,"ACTUAL-01");strcpy(mock.actual_product,"OTHER-02");
    CHECK(!prepare(&selected));released();CHECK(!mock.cards && !image.data && mock.connections==1u && mock.logs==1u);
    setup();memset(selected.product,'X',sizeof(selected.product));CHECK(!prepare(&selected));released();
    CHECK(!mock.cards && !image.data && mock.connections==1u);
    for(unsigned mode=0;mode<4u;mode++) {
        setup();mock.card_result=mode==0u?KUI_DISC_VMU_CARD_CHOICE_REQUIRED:
            mode==1u?KUI_DISC_VMU_CARD_FAILED:mode==2u?KUI_DISC_VMU_CARD_CANCELLED:KUI_DISC_VMU_CARD_PHYSICAL_ONLY;
        CHECK(!prepare(&selected));released();CHECK(mock.cards==1u && !image.data && !status.passed);
    }
    setup();mock.cancel_after_card=true;CHECK(!prepare(&selected));released();CHECK(!image.data && mock.cards==1u);
    setup();mock.unmount_fail_at=2u;CHECK(!prepare(&selected));released();CHECK(!image.data && mock.cards==1u);
    setup();mock.mount_fail_at=2u;CHECK(!prepare(&selected));released();CHECK(!image.data && !mock.cards);
    setup();mock.connect_fail_at=2u;CHECK(!prepare(&selected));
    CHECK(!mock.connected && !mock.mounted && mock.connections==2u && mock.disconnects==1u && !mock.cards && !image.data);
    for(unsigned mode=0;mode<4u;mode++) {
        setup();mock.preference_fail=mode==0u;mock.mount_fail=mode==1u;mock.unmount_fail=mode==2u;mock.cancel_after_preference=mode==3u;
        CHECK(!prepare(&selected));released();CHECK(!image.data && !mock.baselines && !mock.modules && !mock.cards && !mock.identities);
        CHECK(status.complete && !status.passed && status.errors==(mode==3u?0u:1u));
        setup();mock.preference_fail=mode==0u;mock.mount_fail=mode==1u;mock.unmount_fail=mode==2u;mock.cancel_after_preference=mode==3u;
        info_run();released();CHECK(!info.known && !mock.metadatas && !mock.identities && !mock.infos && status.complete && !status.passed);
    }
    setup();mock.stop=true;CHECK(!prepare(&selected));CHECK(!mock.connections && status.stopped && !status.errors && !image.data);
    setup();mock.connect_fail=true;CHECK(!prepare(&selected));CHECK(mock.connections==1u && !mock.disconnects && !mock.modules);
    setup();mock.module_fail=true;CHECK(!prepare(&selected));released();CHECK(mock.modules==1u && !mock.cards && !image.data);
    setup();mock.region_fail=true;CHECK(!prepare((const struct kui_vmu_game_selection *)(uintptr_t)1));
    CHECK(mock.region_loads==1u && !mock.console_reads && !mock.preferences && !mock.connections && !image.data);
    CHECK(status.errors==1u && !status.passed && strstr(status.message,"Region-free preference"));
    setup();mock.cancel_after_region=true;CHECK(!prepare(&selected));
    CHECK(!mock.preferences && !mock.connections && status.stopped && !status.errors && !image.data);
    setup();mock.region_free=false;CHECK(!prepare((const struct kui_vmu_game_selection *)(uintptr_t)1));
    CHECK(mock.region_loads==1u && mock.console_reads==1u && !mock.preferences && !mock.connections && !image.data);
    CHECK(status.errors==1u && strstr(status.message,"Console region"));
    setup();mock.region_free=false;mock.console='U';mock.enabled=false;
    CHECK(prepare((const struct kui_vmu_game_selection *)(uintptr_t)1));released();
    CHECK(mock.baselines==1u && !mock.cards && !mock.modules && mock.region_forwarded.console=='U');
    CHECK(!mock.region_forwarded.region_free && !strcmp(mock.trace,"ROCMPUDB"));
    for(unsigned i=0;i<3u;i++) {
        setup();mock.region_free=false;mock.console="JUE"[i];CHECK(prepare(&selected));released();
        CHECK(mock.region_loads==1u && mock.console_reads==1u && !mock.region_forwarded.region_free &&
            mock.region_forwarded.console==mock.console && status.passed && mock.cards==1u);
    }
    setup();mock.region_fail=true;info_run();released();CHECK(info.known && status.passed && !mock.region_loads && !mock.console_reads);
    setup();kui_disc_launcher_vmu_info(NULL,&status,log_line,cancel,progress);CHECK(!mock.connections);
    CHECK(!kui_disc_launcher_prepare_vmu(&selected,NULL,&status,log_line,cancel,progress) && !mock.connections);
    preview_paths();consecutive_launches();large_synthetic_launch();
    printf("PASS physical-disc VMU frontdoor: %u checks; metadata-only fresh previews, launch-local proofs, synthetic large executable, preference-first Off and released storage\n",checks);
    return 0;
}
