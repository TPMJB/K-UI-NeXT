/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_launch_timing.h"
#include "kui/core.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static uint64_t now;
static unsigned connects,disconnects,mounts,unmounts,opens,writes,syncs,closes,logs;
static bool cancelled_now,cancel_after_write,fail_open,fail_sync,short_write,corrupt_read,connect_ok=true,active;
static uint8_t file_bytes[8192];static size_t file_size;
static char text[8192];
static const char *expected_path=KUI_SMASH_LAUNCH_TIMING_PATH;
static uint64_t clock_ms(void *context) {assert(context==&now);return now;}
uint16_t kui_smash_phase_clock_test_read16(uint32_t address) {(void)address;return 0u;}
uint32_t kui_smash_phase_clock_test_read32(uint32_t address) {(void)address;return 0u;}
uint32_t kui_smash_phase_clock_test_sr(void) {return 0u;}
static void log_line(const char *format,...) {(void)format;++logs;}
static bool cancel(void) {return cancelled_now || (cancel_after_write && writes);}
bool kui_sd_connect(void) {++connects;return connect_ok;}
void kui_sd_disconnect(void) {++disconnects;}
bool kui_mount(FATFS *fs,kui_log_fn log) {(void)fs;assert(log);++mounts;return true;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE opt) {
    assert(!fs && !strcmp(path,"0:") && opt==0u);++unmounts;return FR_OK;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(!active && !strcmp(path,expected_path));++opens;
    if(fail_open)return FR_DISK_ERR;
    if(mode&FA_WRITE)file_size=0u;
    else assert(mode==FA_READ);
    memset(file,0,sizeof(*file));file->obj.objsize=file_size;active=true;return FR_OK;
}
FRESULT f_write(FIL *file,const void *data,UINT bytes,UINT *put) {
    assert(active && !file->fptr && bytes<=sizeof(file_bytes));++writes;
    *put=short_write?bytes-1u:bytes;memcpy(file_bytes,data,*put);file_size=*put;
    file->obj.objsize=file_size;file->fptr=*put;now+=500u;return FR_OK;
}
FRESULT f_read(FIL *file,void *data,UINT bytes,UINT *got) {
    assert(active && file->fptr<=file_size && bytes<=file_size-file->fptr);
    memcpy(data,file_bytes+file->fptr,bytes);*got=bytes;
    if(corrupt_read && bytes)((uint8_t *)data)[0]^=1u;
    file->fptr+=bytes;now+=10u;return FR_OK;
}
FRESULT f_sync(FIL *file) {(void)file;assert(active);++syncs;return fail_sync?FR_DISK_ERR:FR_OK;}
FRESULT f_close(FIL *file) {(void)file;assert(active);active=false;++closes;return FR_OK;}
static void reset(void) {
    assert(!active);connects=disconnects=mounts=unmounts=opens=writes=syncs=closes=logs=0u;
    cancelled_now=cancel_after_write=fail_open=fail_sync=short_write=corrupt_read=false;connect_ok=true;now=1000u;
    kui_smash_launch_timing_clock(clock_ms,&now);kui_smash_launch_timing_request(false,0u);
    expected_path=KUI_SMASH_LAUNCH_TIMING_PATH;
}
static void measured(bool selected) {
    kui_smash_launch_timing_request(true,990u);
    now=1020u;kui_smash_launch_timing_phase(KUI_SMASH_UI_METADATA);
    now=1030u;KUI_SMASH_TIME_SPAN(history,KUI_SMASH_UI_HISTORY_TOTAL);
    now=1050u;kui_smash_launch_timing_phase(KUI_SMASH_UI_MENU_PROOF);
    now=1060u;KUI_SMASH_TIME_END(history);
    now=1070u;kui_smash_launch_timing_phase(KUI_SMASH_UI_TRACK_MAP);
    if(selected)kui_smash_launch_timing_activate("/Games/Smash/disc.gdi","2c53ceafadf5");
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CACHE_HIT,1u);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_MODULE_CRC32,0x12345678u);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_MODULE_CRC32,0x9abcdef0u);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_CAPSULE_CRC32,0x10203040u);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_CAPSULE_CRC32,0xaabbccddu);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_PAYLOAD_BYTES,59400u);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_PAYLOAD_BYTES,247976u);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_OUTER_CAPSULE_BYTES,59904u);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_CHILD_CAPSULE_BYTES,248320u);
    kui_smash_launch_timing_metric(KUI_SMASH_UI_NATIVE_PROFILE_FLAGS,7u);
    now=1100u;
}
static void verify_crc(void) {
    assert(file_size && file_size<sizeof(text));memcpy(text,file_bytes,file_size);text[file_size]=0;
    char *footer=strstr(text,"\ncrc32: ");assert(footer);++footer;unsigned stored;
    assert(sscanf(footer,"crc32: %x",&stored)==1);
    assert(kui_crc32(0u,text,(size_t)(footer-text))==stored);
}
int main(void) {
    reset();kui_smash_launch_timing_phase(KUI_SMASH_UI_METADATA);
    kui_smash_launch_timing_activate("/Games/x.gdi","aaaaaaaaaaaa");
    kui_smash_launch_timing_freeze(true,log_line);assert(!kui_smash_launch_timing_format(text,sizeof(text)));
    assert(!kui_smash_launch_timing_save_mounted(log_line,cancel) && !opens && !logs);
    reset();measured(false);kui_smash_launch_timing_finish(true,log_line,cancel);
    assert(!kui_smash_launch_timing_format(text,sizeof(text)) && !connects && !opens && !logs);
    reset();measured(true);
    kui_smash_launch_timing_component(KUI_SMASH_UI_COMPONENT_MENU,"ab1b145b33b4",59400u,0x12345678u,0u);
    kui_smash_launch_timing_component(KUI_SMASH_UI_COMPONENT_VC2,"10ef97cc87b7",200000u,0x87654321u,0xabcdef01u);
    kui_smash_launch_timing_component(KUI_SMASH_UI_COMPONENT_SWIRL,"invalid",1u,1u,1u);
    kui_smash_launch_timing_freeze(true,log_line);
    size_t size=kui_smash_launch_timing_format(text,sizeof(text));assert(size && size<sizeof(text));
    assert(strstr(text,"total.action_to_ready: calls=1 ms=110 "));
    assert(strstr(text,"component.retail-smash-menu.kui: build=ab1b145b33b4 payload_bytes=59400 crc32=12345678 helper_crc32=00000000"));
    assert(strstr(text,"component.retail-smash-vc2.kui: build=10ef97cc87b7 payload_bytes=200000 crc32=87654321 helper_crc32=ABCDEF01"));
    assert(!strstr(text,"component.retail-smash-swirl.kui:"));
    assert(strstr(text,"phase.request_setup: calls=1 ms=30 ") && strstr(text,"phase.metadata: calls=1 ms=30 "));
    assert(strstr(text,"phase.menu_proof: calls=1 ms=20 ") && strstr(text,"phase.track_map: calls=1 ms=30 "));
    assert(strstr(text,"detail.history_total: calls=1 ms=30 "));
    assert(strstr(text,"nested intervals; do not add to flat phases") && strstr(text,"cycles=unavailable"));
    assert(strstr(text,"metric.sdk_cache_hit: 1") && strstr(text,"report_io: excluded"));
    assert(strstr(text,"metric.outer_module_crc32: 0x12345678") &&
        strstr(text,"metric.child_module_crc32: 0x9ABCDEF0") &&
        strstr(text,"metric.outer_capsule_crc32: 0x10203040") &&
        strstr(text,"metric.child_capsule_crc32: 0xAABBCCDD") &&
        strstr(text,"metric.outer_payload_bytes: 59400") && strstr(text,"metric.child_payload_bytes: 247976") &&
        strstr(text,"metric.outer_capsule_bytes: 59904") && strstr(text,"metric.child_capsule_bytes: 248320") &&
        strstr(text,"metric.native_profile_flags: 7"));
    assert(!kui_smash_launch_timing_format(text,10u));
    assert(kui_smash_launch_timing_save_mounted(log_line,cancel));verify_crc();
    assert(!connects && !disconnects && !mounts && !unmounts && writes==1u && syncs==1u && closes==2u);
    assert(strstr(text,"total.action_to_ready: calls=1 ms=110 ")); /* 500ms file writes excluded */
    reset();measured(true);kui_smash_launch_timing_finish(true,log_line,cancel);verify_crc();
    assert(connects==1u && disconnects==1u && mounts==1u && unmounts==1u && writes==1u);
    assert(strstr(text,"ms=110 "));
    reset();measured(true);kui_smash_launch_timing_finish(false,log_line,cancel);
    assert(!connects && !opens && kui_smash_launch_timing_format(text,sizeof(text)) && strstr(text,"preparation failed"));
    reset();measured(true);kui_smash_launch_timing_report_allowed(false);
    kui_smash_launch_timing_finish(true,log_line,cancel);assert(!connects && !opens);
    reset();measured(true);cancelled_now=true;kui_smash_launch_timing_finish(true,log_line,cancel);
    assert(!connects && !opens);
    reset();measured(true);kui_smash_launch_timing_freeze(true,log_line);fail_open=true;
    assert(!kui_smash_launch_timing_save_mounted(log_line,cancel) && !active && !writes);
    reset();measured(true);kui_smash_launch_timing_freeze(true,log_line);fail_sync=true;
    assert(!kui_smash_launch_timing_save_mounted(log_line,cancel) && !active && closes==1u);
    reset();measured(true);kui_smash_launch_timing_freeze(true,log_line);short_write=true;
    assert(!kui_smash_launch_timing_save_mounted(log_line,cancel) && !active && closes==1u);
    reset();measured(true);kui_smash_launch_timing_freeze(true,log_line);corrupt_read=true;
    assert(!kui_smash_launch_timing_save_mounted(log_line,cancel) && !active && closes==2u);
    reset();measured(true);kui_smash_launch_timing_freeze(true,log_line);cancel_after_write=true;
    assert(!kui_smash_launch_timing_save_mounted(log_line,cancel) && writes==1u && closes==1u && !active);
    reset();measured(true);connect_ok=false;kui_smash_launch_timing_finish(true,log_line,cancel);
    assert(connects==1u && !mounts && !writes);
    reset();kui_smash_launch_timing_request(true,1000u);now=900u;
    kui_smash_launch_timing_phase(KUI_SMASH_UI_METADATA);now=1100u;
    kui_smash_launch_timing_activate("/Games/Smash/disc.gdi","2c53ceafadf5");
    kui_smash_launch_timing_freeze(true,log_line);
    assert(kui_smash_launch_timing_format(text,sizeof(text)) && strstr(text,"phase.request_setup: calls=1 ms=unavailable"));
    const char *const modes[]={"Standard","Background New (X)","Background Legacy (Y)"};
    for(unsigned mode=0;mode<3u;mode++)for(unsigned vmu=0;vmu<2u;vmu++) {
        reset();measured(false);expected_path=KUI_NATIVE_LAUNCH_TIMING_PATH;
        kui_smash_launch_timing_native("/Games/Sonic/disc.gdi","0123456789ab","MK-51000",
            modes[mode],vmu!=0u,0x76543210u,263676u);
        kui_smash_launch_timing_add(KUI_SMASH_UI_SOURCE_READ_CALLBACKS,2u);
        kui_smash_launch_timing_add(KUI_SMASH_UI_SOURCE_READ_CALLBACKS,3u);
        kui_smash_launch_timing_add(KUI_SMASH_UI_HISTORY_READ_BYTES,UINT64_MAX);
        kui_smash_launch_timing_add(KUI_SMASH_UI_HISTORY_READ_BYTES,1u);
        now=1110u;kui_smash_launch_timing_phase(KUI_SMASH_UI_MODULE_LOAD);
        now=1120u;kui_smash_launch_timing_freeze(true,log_line);
        assert(kui_smash_launch_timing_save_mounted(log_line,cancel));verify_crc();
        assert(strstr(text,"KUI native UI launch timing v1") && strstr(text,"product: MK-51000"));
        assert(strstr(text,modes[mode]) && strstr(text,vmu?"virtual_vmu: On":"virtual_vmu: Off"));
        assert(strstr(text,"metric.module_crc32: 0x76543210") && strstr(text,"metric.module_bytes: 263676"));
        assert(strstr(text,"metric.source_read_callbacks: 5") && strstr(text,"phase.module_load: calls=1 ms=10 "));
        assert(strstr(text,"metric.history_read_bytes: 18446744073709551615"));
        assert(strstr(text,"total.action_to_ready: calls=1 ms=130 "));
        assert(writes==1u && syncs==1u && closes==2u && !connects);
    }
    reset();measured(false);expected_path=KUI_NATIVE_LAUNCH_TIMING_PATH;
    kui_smash_launch_timing_native("/Games/Sonic/disc.gdi","0123456789ab","MK-51000",
        "Background New (X)",true,0u,0u);
    kui_smash_launch_timing_finish(false,log_line,cancel);verify_crc();
    assert(writes==1u && syncs==1u && strstr(text,"preparation failed"));
    reset();measured(false);expected_path=KUI_NATIVE_LAUNCH_TIMING_PATH;
    kui_smash_launch_timing_native("/Games/Sonic/disc.gdi","0123456789ab","MK-51000",
        "Background New (X)",true,0u,0u);
    kui_smash_launch_timing_report_allowed(false);kui_smash_launch_timing_finish(false,log_line,cancel);
    assert(!connects && !opens);
    puts("UI timing accounting/sidecar: OK");return 0;
}
