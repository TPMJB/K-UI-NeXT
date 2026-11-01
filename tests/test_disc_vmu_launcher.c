/* SPDX-License-Identifier: GPL-3.0-only */
/* Test the actual disc-VMU frontdoor with bounded optical/card stubs. Card and
 * packet admission have their own real-code tests; this exercises mode dispatch,
 * call order, exact GUI selection forwarding and every mounted-session exit. */
#include "kui/disc_launcher.h"
#include "kui/disc_launch.h"
#include "kui/disc_vmu_card.h"
#include "kui/disc_region.h"
#include "kui/vmu_emulation_settings.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)) {fprintf(stderr,"%s:%u: %s\n",__FILE__,__LINE__,#x);exit(1);}} while(0)
struct scenario {
    bool connected,mounted,enabled,stop,connect_fail,mount_fail,preference_fail,unmount_fail;
    bool identity_fail,info_fail,baseline_fail,module_fail,callback_skipped;
    bool cancel_after_preference,cancel_after_info,cancel_after_card;
    bool region_free,region_fail,cancel_after_region;
    char console;
    unsigned region_loads,console_reads;
    struct kui_disc_launcher_region region_forwarded;
    unsigned connect_fail_at,mount_fail_at,unmount_fail_at;
    unsigned connections,mounts,unmounts,disconnects,preferences,identities,infos,baselines,modules,cards,progress,logs;
    int card_result;
    const struct kui_vmu_game_selection *expected_selection;
    struct kui_app_status last;
    char actual_product[11];
    char trace[256];size_t trace_count;
} mock;
static struct kui_vmu_game_info info;
static struct kui_app_status status;
static struct kui_runtime_image image;
static struct kui_vmu_game_selection selected;
static uint8_t dummy_payload[4];
static void event(char code) {CHECK(mock.trace_count+1u<sizeof(mock.trace));mock.trace[mock.trace_count++]=code;mock.trace[mock.trace_count]=0;}
static bool cancel(void) {return mock.stop;}
static void log_line(const char *format,...) {(void)format;++mock.logs;}
static void progress(const struct kui_app_status *view) {mock.last=*view;++mock.progress;}
static bool zero(const void *data,size_t bytes) {const uint8_t *p=data;for(size_t i=0;i<bytes;i++)if(p[i])return false;return true;}
static void setup(void) {
    memset(&mock,0,sizeof(mock));mock.enabled=true;mock.card_result=KUI_DISC_VMU_CARD_READY;
    mock.region_free=true;mock.console='?';
    strcpy(mock.actual_product,"ACTUAL-01");
    memset(&info,0xa5,sizeof(info));memset(&status,0xa5,sizeof(status));memset(&image,0xa5,sizeof(image));
    memset(&selected,0,sizeof(selected));strcpy(selected.product,"ACTUAL-01");selected.selected=true;selected.pair=23u;
    memset(selected.identity,0x23,16u);selected.storage.cid[0]=0x42;selected.storage.card_sectors=2000000u;
    selected.storage.partition_first=2048u;selected.storage.partition_end=1002048u;selected.candidates_sequence=9u;
    mock.expected_selection=&selected;
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
bool kui_vmu_emulation_settings_load(bool *enabled,kui_log_fn log) {
    CHECK(mock.connected && mock.mounted && enabled && log==log_line);event('P');++mock.preferences;
    if(mock.preference_fail) return false;
    *enabled=mock.enabled;if(mock.cancel_after_preference)mock.stop=true;return true;
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
void kui_vmu_game_info_run(const char *product,bool native,struct kui_vmu_game_info *out,
    kui_log_fn log,kui_cancel_fn stop) {
    CHECK(!mock.connected && !mock.mounted && !strcmp(product,mock.actual_product));
    CHECK(native && out && log==log_line && stop==cancel);event('N');++mock.infos;
    memset(out,0,sizeof(*out));strcpy(out->product,product);out->enabled=mock.enabled;
    out->known=!mock.info_fail;strcpy(out->notice,mock.info_fail?"info refused":"Choose a card");
    if(mock.cancel_after_info)mock.stop=true;
}
bool kui_disc_launcher_prepare(struct kui_runtime_image *out,struct kui_app_status *view,
    kui_log_fn log,kui_cancel_fn stop,kui_app_progress_fn notify) {
    CHECK(!mock.connected && !mock.mounted && out && zero(out,sizeof(*out)));
    CHECK(view && log==log_line && stop==cancel && notify==progress);event('B');++mock.baselines;
    *view=(struct kui_app_status){.complete=true,.passed=!mock.baseline_fail,.errors=mock.baseline_fail?1u:0u};
    if(mock.baseline_fail)return false;
    out->data=dummy_payload;out->info.payload_bytes=sizeof(dummy_payload);return true;
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
    CHECK(!strcmp(package,KUI_DISC_LAUNCH_VMU_PACKAGE));
    CHECK(callback && context==mock.expected_selection && out && zero(out,sizeof(*out)));
    CHECK(view && log==log_line && stop==cancel && notify==progress);event('V');++mock.modules;
    if(mock.module_fail) {*view=(struct kui_app_status){.complete=true,.errors=1u};return false;}
    uint8_t profile[4096];memset(profile,0xa5,sizeof(profile));
    bool ok=callback(mock.actual_product,profile,context,log,stop);
    CHECK(!mock.connected && !mock.mounted);
    if(ok) {for(size_t i=0;i<sizeof(profile);i++)CHECK(profile[i]==0x5au);}
    else CHECK(zero(profile,sizeof(profile)));
    *view=(struct kui_app_status){.complete=true,.passed=ok,.errors=ok?0u:1u};
    if(ok) {out->data=dummy_payload;out->info.payload_bytes=sizeof(dummy_payload);}
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
int main(void) {
    setup();mock.enabled=false;info_run();released();
    CHECK(info.known && !info.enabled && !info.product[0] && !strcmp(info.label,"Physical VMUs"));
    CHECK(status.complete && status.passed && !status.errors && !mock.identities && !mock.infos && !mock.cards);
    CHECK(!strcmp(mock.trace,"CMPUD"));
    setup();mock.enabled=false;CHECK(prepare((const struct kui_vmu_game_selection *)(uintptr_t)1));released();
    CHECK(mock.baselines==1u && !mock.modules && !mock.identities && !mock.infos && !mock.cards);
    CHECK(!strcmp(mock.trace,"RCMPUDB") && mock.region_loads==1u && !mock.console_reads && mock.region_forwarded.region_free);
    setup();mock.enabled=false;mock.baseline_fail=true;CHECK(!prepare((const struct kui_vmu_game_selection *)(uintptr_t)1));released();
    CHECK(!image.data && status.errors==1u && mock.baselines==1u && !mock.modules && !mock.cards);
    setup();info_run();released();CHECK(info.known && info.enabled && !strcmp(info.product,"ACTUAL-01"));
    CHECK(status.passed && mock.identities==1u && mock.infos==1u && !strcmp(mock.trace,"CMPUDIN"));
    setup();mock.identity_fail=true;info_run();released();CHECK(!status.passed && status.errors==1u && !mock.infos && strstr(info.notice,"identity refused"));
    setup();mock.info_fail=true;info_run();released();CHECK(!status.passed && status.errors==1u && strstr(status.message,"info refused"));
    setup();mock.cancel_after_info=true;info_run();released();CHECK(status.stopped && !status.errors && !status.passed);
    setup();CHECK(prepare(&selected));released();
    CHECK(mock.modules==1u && !mock.baselines && mock.cards==1u && image.data && status.passed);
    CHECK(!strcmp(mock.trace,"RCMPUDVCMSUD"));
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
        info_run();released();CHECK(!info.known && !mock.identities && !mock.infos && status.complete && !status.passed);
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
    printf("PASS physical-disc VMU frontdoor: %u checks; region policy, preference-first Off, actual disc identity, exact selection, storage release and cleared failures\n",checks);
    return 0;
}
