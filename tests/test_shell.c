/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell.h"
#include "kui/shell_font.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static struct kui_shell s;
static const struct kui_settings defaults={true,false,false};
static void reset(enum kui_shell_page page) {
    kui_shell_init(&s,&defaults); s.page=page;
}
static enum kui_shell_action press(unsigned buttons, bool busy) {
    return kui_shell_input(&s,buttons,busy);
}
static void open_destination(void) {
    assert(s.page==KUI_SHELL_RIPPER);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
    s.advanced_selected=4;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST);
    assert(s.page==KUI_SHELL_DESTINATION);
}
static void launcher_and_confirmation(void) {
    reset(KUI_SHELL_HOME);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE);
    assert(s.page==KUI_SHELL_HOME);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(s.page==KUI_SHELL_RIPPER && !s.confirm_new);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_R,false)==KUI_SHELL_NONE);
    assert(s.confirm_new);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE);
    assert(!s.confirm_new && s.page==KUI_SHELL_RIPPER);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NEW_DUMP && !s.confirm_new);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.home_selected==8);
    press(KUI_SHELL_UP,false); press(KUI_SHELL_UP,false); press(KUI_SHELL_UP,false);
    assert(s.home_selected==5);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DIAGNOSTICS);
    press(KUI_SHELL_B,false);
    press(KUI_SHELL_UP,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_LOAD_SYSTEM);
    assert(s.page==KUI_SHELL_SETTINGS);
}
static void operation_lock_and_stop(void) {
    const unsigned launch=KUI_SHELL_A|KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_R;
    for(unsigned page=0;page<=KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM;page++) {
        reset((enum kui_shell_page)page);
        assert(press(launch,true)==KUI_SHELL_NONE && s.page==page);
        assert(press(launch|KUI_SHELL_L|KUI_SHELL_B,true)==KUI_SHELL_STOP);
        assert(press(KUI_SHELL_L,true)==(page==KUI_SHELL_HOME||page==KUI_SHELL_RIPPER?
            KUI_SHELL_MUSIC_PREVIOUS:(page==KUI_SHELL_GAMES_PROBE_CONFIRM ||
            page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM)?
            KUI_SHELL_NONE:KUI_SHELL_MSTATS));
        assert(s.page==page);
    }
    reset(KUI_SHELL_RIPPER); s.confirm_new=true;
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP && !s.confirm_new);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_RESUME);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VERIFY);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_MUSIC_NEXT);
}
static void settings_transaction(void) {
    reset(KUI_SHELL_RIPPER_SETTINGS);
    press(KUI_SHELL_RIGHT,false);
    assert(!s.draft.crc_only && s.saved.crc_only && kui_shell_settings_dirty(&s));
    press(KUI_SHELL_DOWN,false); press(KUI_SHELL_LEFT,false);
    /* SHA's existing schema requires readback; no misleading OFF choice. */
    assert(s.draft.end_readback && !s.saved.end_readback);
    assert(!s.draft.show_memory); /* System memory preference is independent. */
    struct kui_settings changed=s.draft;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_SETTINGS);
    assert(kui_shell_settings_dirty(&s)); /* Failure leaves draft for retry. */
    press(KUI_SHELL_LEFT|KUI_SHELL_UP,true);
    assert(s.setting_selected==1 && s.draft.end_readback);
    kui_shell_set_preferences(&s,&changed);
    assert(!kui_shell_settings_dirty(&s));
    press(KUI_SHELL_UP,false); press(KUI_SHELL_LEFT,false);
    assert(s.draft.crc_only);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,false)==KUI_SHELL_DISCARD_SETTINGS);
    assert(s.page==KUI_SHELL_HOME && !s.draft.crc_only && !kui_shell_settings_dirty(&s));
    reset(KUI_SHELL_RIPPER_SETTINGS);
    press(KUI_SHELL_LEFT|KUI_SHELL_RIGHT,false);
    assert(!kui_shell_settings_dirty(&s));
    press(KUI_SHELL_DOWN,false); press(KUI_SHELL_RIGHT,false);
    assert(s.draft.end_readback);
    press(KUI_SHELL_LEFT,false); assert(!s.draft.end_readback);
    press(KUI_SHELL_DOWN,false); assert(s.setting_selected==0);
    s.draft.show_memory=!s.saved.show_memory;
    assert(!kui_shell_settings_dirty(&s));
    const struct kui_settings inconsistent={false,false,true};
    kui_shell_set_preferences(&s,&inconsistent);
    assert(s.saved.end_readback && s.draft.end_readback);
    kui_shell_init(&s,NULL); assert(s.saved.show_memory);
}
static void system_transaction_and_video(void) {
    reset(KUI_SHELL_SETTINGS);
    assert(s.system_saved.video_mode==KUI_VIDEO_AUTO && s.system_saved.show_memory);
    press(KUI_SHELL_LEFT,false); assert(s.system_draft.video_mode==KUI_VIDEO_PAL50);
    assert(s.system_saved.video_mode==KUI_VIDEO_AUTO && kui_shell_system_dirty(&s));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_PREVIEW_VIDEO && !s.video_trial);
    /* Main owns the actual reversible preview and its deadline. */
    s.video_trial=true;
    struct kui_system_settings draft=s.system_draft;
    assert(press(KUI_SHELL_DOWN|KUI_SHELL_RIGHT|KUI_SHELL_X|KUI_SHELL_L,false)==KUI_SHELL_NONE);
    assert(s.system_selected==0 && s.system_draft.video_mode==draft.video_mode);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_CANCEL_VIDEO);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_CONFIRM_VIDEO);
    assert(s.system_saved.video_mode==KUI_VIDEO_AUTO); /* Never implicit commit. */
    s.video_trial=false;
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_DISCARD_SYSTEM);
    assert(s.page==KUI_SHELL_HOME && !kui_shell_system_dirty(&s));
    s.page=KUI_SHELL_SETTINGS;
    press(KUI_SHELL_DOWN,false);press(KUI_SHELL_RIGHT,false);
    assert(!s.system_draft.show_memory && s.system_saved.show_memory);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_SYSTEM);
    assert(kui_shell_system_dirty(&s)); /* Failed save leaves a retryable draft. */
    draft=s.system_draft;kui_shell_set_system_preferences(&s,&draft);
    assert(!kui_shell_system_dirty(&s) && !s.system_saved.show_memory);
    press(KUI_SHELL_DOWN,false);press(KUI_SHELL_RIGHT,false);
    assert(s.system_draft.music_enabled && !s.system_saved.music_enabled);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_MUSIC_NEXT);
    press(KUI_SHELL_DOWN,false);
    for(unsigned i=0;i<30;i++) press(KUI_SHELL_RIGHT,false);
    assert(s.system_draft.music_volume==100);
    for(unsigned i=0;i<30;i++) press(KUI_SHELL_LEFT,false);
    assert(s.system_draft.music_volume==0);
    draft=s.system_saved;draft.music_volume=101;
    kui_shell_set_system_preferences(&s,&draft);
    assert(s.system_saved.music_volume==75);
    press(KUI_SHELL_DOWN,false);assert(s.system_selected==4);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE);
    assert(s.saved.crc_only==defaults.crc_only && s.saved.end_readback==defaults.end_readback);
}
static void app_navigation_and_vmu(void) {
    static const enum kui_shell_page pages[]={KUI_SHELL_RIPPER,KUI_SHELL_VMU,
        KUI_SHELL_MEMORY,KUI_SHELL_NETWORK,KUI_SHELL_SETTINGS,KUI_SHELL_DIAGNOSTICS,
        KUI_SHELL_GD_PLAY,KUI_SHELL_MUSIC};
    for(unsigned i=0;i<8;i++) {
        reset(KUI_SHELL_HOME);s.home_selected=i;
        enum kui_shell_action expected=i==1?KUI_SHELL_VMU_LIST:
            i==4?KUI_SHELL_LOAD_SYSTEM:i==7?KUI_SHELL_MUSIC_LIST:KUI_SHELL_NONE;
        assert(press(KUI_SHELL_A,false)==expected && s.page==pages[i]);
    }
    reset(KUI_SHELL_MEMORY);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_MEMORY_TEST);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_HOME);
    reset(KUI_SHELL_NETWORK);assert(press(KUI_SHELL_A,false)==KUI_SHELL_NETWORK_TEST);
    reset(KUI_SHELL_VMU);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_VMU_LIST && s.vmu_slot==7);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_VMU_LIST && s.vmu_slot==0);
    struct kui_vmu_view view={.slot=0,.page=0,.count=8,.total=10,.present=true};
    strcpy(view.entries[0].name,"SONIC2__S01");
    kui_shell_set_vmu(&s,&view);assert(s.vmu.count==8);
    press(KUI_SHELL_UP,false);assert(s.vmu_selected==7);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_VMU_BACKUP);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VMU_BACKUP_ALL);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_VMU_LIST && s.vmu_page==1 && !s.vmu.count);
    kui_shell_set_vmu(&s,&view);assert(!s.vmu.count); /* Stale previous page. */
    view.page=1;view.count=2;kui_shell_set_vmu(&s,&view);
    assert(s.vmu.count==2 && s.vmu_selected==0);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_VMU_LIST && s.vmu_page==0);
    view.page=0;view.count=UINT_MAX;
    memset(view.entries[0].name,'x',sizeof(view.entries[0].name));
    kui_shell_set_vmu(&s,&view);
    assert(s.vmu.count==8 && !s.vmu.entries[0].name[15]);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_VMU_LIST && s.vmu_slot==1);
    kui_shell_set_vmu(&s,&view);assert(!s.vmu.count); /* Stale previous VMU. */
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_LIST);
}
static void clock_and_defaults(void) {
    reset(KUI_SHELL_SETTINGS);s.system_selected=4;
    assert(s.system_saved.startup_chime);
    press(KUI_SHELL_RIGHT,false);assert(!s.system_draft.startup_chime && kui_shell_system_dirty(&s));
    press(KUI_SHELL_DOWN,false);press(KUI_SHELL_LEFT,false);
    assert(s.system_draft.startup_app==KUI_STARTUP_DIAGNOSTICS);
    press(KUI_SHELL_RIGHT,false);assert(s.system_draft.startup_app==KUI_STARTUP_HOME);
    struct kui_system_settings previous=s.system_draft;
    kui_shell_set_system_preferences(&s,&previous);assert(!s.system_saved.startup_chime);
    s.system_selected=7;assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_defaults);
    assert(press(KUI_SHELL_DOWN|KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.system_selected==7);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_defaults);
    assert(!s.system_draft.startup_chime);
    press(KUI_SHELL_A,false);press(KUI_SHELL_A,false);
    assert(s.system_draft.startup_chime && s.system_selected==0);
    /* Resetting a draft does not commit it or modify independently saved ripper settings. */
    assert(!s.system_saved.startup_chime && s.saved.crc_only && kui_shell_system_dirty(&s));
    s.system_selected=6;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_CLOCK_READ && s.page==KUI_SHELL_CLOCK);
    assert(!s.clock_valid && press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.confirm_clock);
    kui_shell_set_clock(&s,NULL,NULL);
    assert(s.clock_valid && s.clock_draft.year==1980 && strstr(s.clock_notice,"fallback"));
    const struct kui_datetime leap={2024,2,29,23,59,59};kui_shell_set_clock(&s,&leap,NULL);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE);
    assert(s.clock_draft.year==2025 && s.clock_draft.day==28); /* Clamp leap-day on year edit. */
    s.clock_selected=2;press(KUI_SHELL_RIGHT,false);assert(s.clock_draft.day==1);
    press(KUI_SHELL_LEFT,false);assert(s.clock_draft.day==28); /* Feb wraps at its real end. */
    s.clock_selected=1;press(KUI_SHELL_RIGHT,false);assert(s.clock_draft.month==3);
    s.clock_selected=2;press(KUI_SHELL_LEFT,false);assert(s.clock_draft.day==27);
    s.clock_selected=5;press(KUI_SHELL_RIGHT,false);assert(s.clock_draft.second==0);
    press(KUI_SHELL_LEFT,false);assert(s.clock_draft.second==59);
    struct kui_datetime draft=s.clock_draft;
    kui_shell_set_clock(&s,NULL,"Clock write failed. Edit or retry.");
    assert(s.clock_valid && !memcmp(&draft,&s.clock_draft,sizeof(draft)));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_clock);
    assert(press(KUI_SHELL_RIGHT|KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.clock_selected==5);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_clock);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.confirm_clock);
    assert(press(KUI_SHELL_B,true)==KUI_SHELL_STOP && !s.confirm_clock);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_CLOCK_WRITE && !s.confirm_clock);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SETTINGS);
}
static void restore_and_scan_controls(void) {
    reset(KUI_SHELL_VMU);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_VMU_BACKUPS_LIST && s.page==KUI_SHELL_VMU_RESTORE);
    struct kui_vmu_backup_view backups={.page=0,.count=2,.total=10};
    strcpy(backups.entries[0].name,"MDK2_SAVE");strcpy(backups.entries[0].folder,"v0001");
    strcpy(backups.entries[0].path,"/KUI/backups/vmu/v0001/MDK2_SAVE.vms");backups.entries[0].bytes=4096;
    kui_shell_set_vmu_backups(&s,&backups);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.vmu_slot==7);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_RESTORE_PREVIEW);
    assert(!strcmp(s.restore_path,backups.entries[0].path) && !s.confirm_vmu_restore);
    struct kui_vmu_view preview={.slot=0,.restore_ready=true,.count=1};
    strcpy(preview.entries[0].name,"CHECKED_SAVE");preview.entries[0].bytes=8192;
    kui_shell_set_vmu_restore_preview(&s,&preview);assert(!s.confirm_vmu_restore); /* Wrong card. */
    preview.slot=7;kui_shell_set_vmu_restore_preview(&s,&preview);assert(s.confirm_vmu_restore);
    assert(!strcmp(s.restore_name,"CHECKED_SAVE") && s.restore_bytes==8192);
    assert(press(KUI_SHELL_RIGHT|KUI_SHELL_START,false)==KUI_SHELL_NONE && s.vmu_slot==7);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_vmu_restore);
    kui_shell_set_vmu_restore_preview(&s,&preview);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_RESTORE_COMMIT && !s.confirm_vmu_restore);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_VMU_BACKUPS_LIST && s.backup_page==1);
    kui_shell_set_vmu_backups(&s,&backups);assert(!s.backups.count); /* Stale page. */
    backups.page=1;backups.count=1;memset(backups.entries[0].path,'x',sizeof(backups.entries[0].path));
    kui_shell_set_vmu_backups(&s,&backups);
    assert(!s.backups.entries[0].path[0] && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU);
    reset(KUI_SHELL_ADVANCED);s.advanced_selected=5;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST && s.page==KUI_SHELL_DESTINATION && s.browse_for_scan);
    strcpy(s.browse_path,"/Games/MDK2");
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_ADVANCED_CRC && s.page==KUI_SHELL_CRC_SCAN);
    assert(!strcmp(s.destination,"/Games") && !strcmp(s.browse_path,"/Games/MDK2"));
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_ADVANCED_CRC);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED);
    press(KUI_SHELL_A,false);assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED);
    s.advanced_selected=4;press(KUI_SHELL_A,false);
    assert(!s.browse_for_scan && press(KUI_SHELL_Y,false)==KUI_SHELL_DEST_SAVE);
}
static void phase_eta(void) {
    struct kui_shell_view v={.busy=true,.phase=2,.phase_elapsed_ms=2000,
        .progress_age_ms=0,.total=2050,.done=1,.rate_kib=1};
    uint64_t seconds=999;
    assert(kui_shell_phase_eta(&v,&seconds) && seconds==3); /* Round up. */
    v.phase_elapsed_ms=1999;assert(!kui_shell_phase_eta(&v,&seconds));
    v.phase_elapsed_ms=2000;v.progress_age_ms=3000;
    assert(kui_shell_phase_eta(&v,&seconds));
    v.progress_age_ms=3001;assert(!kui_shell_phase_eta(&v,&seconds));
    v.progress_age_ms=0;
    for(unsigned phase=0;phase<6;phase++) {
        v.phase=phase;assert(kui_shell_phase_eta(&v,&seconds)==(phase>=1&&phase<=3));
    }
    v.phase=2;v.rate_kib=0;assert(!kui_shell_phase_eta(&v,&seconds));
    v.rate_kib=UINT_MAX;v.total=UINT64_MAX;v.done=0;
    assert(kui_shell_phase_eta(&v,&seconds));
    assert(seconds==UINT64_MAX/((uint64_t)UINT_MAX*1024)+1);
    v.done=UINT64_MAX;assert(kui_shell_phase_eta(&v,&seconds) && !seconds);
    v.saving=true;assert(!kui_shell_phase_eta(&v,&seconds));
    v.saving=false;v.cancel_requested=true;assert(!kui_shell_phase_eta(&v,&seconds));
    v.cancel_requested=false;v.busy=false;assert(!kui_shell_phase_eta(&v,&seconds));
    assert(!kui_shell_phase_eta(NULL,&seconds));assert(!kui_shell_phase_eta(&v,NULL));
}
static void diagnostics(void) {
    reset(KUI_SHELL_DIAGNOSTICS);
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_MSTATS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DISC_PROBE);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_STORAGE_PROBE);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_SAVE_LOG);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_BENCH);
    press(KUI_SHELL_UP,true); assert(s.scroll==3);
    press(KUI_SHELL_UP,false); assert(s.scroll==6);
    press(KUI_SHELL_DOWN,false); assert(s.scroll==3);
    press(KUI_SHELL_START,true); assert(s.scroll==0);
    press(KUI_SHELL_DOWN,false); assert(s.scroll==0);
    s.scroll=UINT_MAX; press(KUI_SHELL_UP,false); assert(s.scroll==UINT_MAX);
    for(unsigned page=0;page<3;page++) {
        reset((enum kui_shell_page)page); press(KUI_SHELL_UP,false);
        assert(s.scroll==0);
    }
}
static void destination_transaction(void) {
    reset(KUI_SHELL_RIPPER);
    assert(!strcmp(s.destination,"/Games"));
    open_destination();
    assert(s.page==KUI_SHELL_DESTINATION && !strcmp(s.browse_path,"/Games"));
    struct kui_destination_page page={.count=3,.has_more=true};
    strcpy(page.entries[0].name,"Adventure");
    strcpy(page.entries[1].name,"../bad");
    strcpy(page.entries[2].name,"[Name too long]"); page.entries[2].disabled=true;
    kui_shell_set_listing(&s,&page);
    press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.destination_notice[0]);
    assert(!strcmp(s.browse_path,"/Games"));
    press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.destination_notice[0]);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_DEST_LIST && s.browser_page==1);
    assert(s.listing.count==0 && !s.listing.has_more);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.browser_page==1);
    kui_shell_set_listing(&s,&page);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_DEST_LIST && s.browser_page==0);
    kui_shell_set_listing(&s,&page);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST);
    assert(!strcmp(s.browse_path,"/Games/Adventure") && !strcmp(s.destination,"/Games"));
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_DEST_SAVE);
    kui_shell_destination_error(&s,"Save failed. Card unchanged.");
    assert(!strcmp(s.destination,"/Games") && s.page==KUI_SHELL_DESTINATION);
    assert(press(KUI_SHELL_B|KUI_SHELL_Y,false)==KUI_SHELL_DEST_LIST);
    assert(!strcmp(s.browse_path,"/Games") && !strcmp(s.destination,"/Games"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_DEST_LIST && !strcmp(s.browse_path,"/"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_RIPPER);
    assert(!strcmp(s.browse_path,"/Games"));
    open_destination();
    strcpy(s.browse_path,"/Other");
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_DEST_SAVE);
    kui_shell_set_destination(&s,s.browse_path);
    assert(s.page==KUI_SHELL_RIPPER && !strcmp(s.destination,"/Other"));
    s.page=KUI_SHELL_HOME; kui_shell_set_destination(&s,"/Loaded");
    assert(s.page==KUI_SHELL_HOME && !strcmp(s.destination,"/Loaded"));
    kui_shell_set_destination(&s,"invalid:root");
    assert(!strcmp(s.destination,"/Loaded"));
    memset(&page,0,sizeof(page)); page.count=UINT_MAX;
    memset(page.entries[0].name,'x',sizeof(page.entries[0].name));
    kui_shell_set_listing(&s,&page);
    assert(s.listing.count==KUI_DEST_PAGE_SIZE && s.listing.entries[0].disabled);
    assert(!strcmp(s.listing.entries[0].name,"[Name too long]"));
}
static void keyboard_transaction(void) {
    reset(KUI_SHELL_RIPPER); open_destination();
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD);
    assert(!strcmp(s.keyboard,"/Games"));
    press(KUI_SHELL_A,false); assert(!strcmp(s.keyboard,"/Gamesq"));
    press(0,false); assert(!strcmp(s.keyboard,"/Gamesq")); /* No repeated action. */
    press(KUI_SHELL_X,false); assert(!strcmp(s.keyboard,"/Games"));
    press(KUI_SHELL_Y,false); press(KUI_SHELL_A,false);
    assert(!strcmp(s.keyboard,"/GamesQ"));
    s.keyboard_selected=40; press(KUI_SHELL_A,false);
    assert(!strcmp(s.keyboard,"/GamesQ "));
    s.keyboard_selected=41; press(KUI_SHELL_A,false);
    assert(!strcmp(s.keyboard,"/GamesQ"));
    s.keyboard_selected=42;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_SAVE);
    assert(!strcmp(s.browse_path,"/GamesQ") && !strcmp(s.destination,"/Games"));
    kui_shell_destination_error(&s,"Save failed.");
    press(KUI_SHELL_B,false);
    assert(s.page==KUI_SHELL_DESTINATION && !strcmp(s.browse_path,"/Games"));
    press(KUI_SHELL_X,false);
    strcpy(s.keyboard,"/Games/../unsafe");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.destination_notice[0]);
    assert(!strcmp(s.browse_path,"/Games"));
    strcpy(s.keyboard,"/Games/\xc0\xaf");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && !strcmp(s.browse_path,"/Games"));
    strcpy(s.keyboard,"/Games/\xc3\xa9");
    press(KUI_SHELL_X,false); assert(!strcmp(s.keyboard,"/Games/"));
    memset(s.keyboard,'a',sizeof(s.keyboard)-1); s.keyboard[sizeof(s.keyboard)-1]=0;
    s.keyboard_selected=0; press(KUI_SHELL_A,false);
    assert(strlen(s.keyboard)==sizeof(s.keyboard)-1 && s.destination_notice[0]);
    strcpy(s.keyboard,"/Games/New Folder");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_DEST_SAVE);
    assert(!strcmp(s.destination,"/Games"));
    kui_shell_set_destination(&s,s.browse_path);
    assert(!strcmp(s.destination,"/Games/New Folder") && s.page==KUI_SHELL_RIPPER);
    /* Repeated directions remain inside the grid, including the three-key row. */
    open_destination(); press(KUI_SHELL_X,false);
    for(unsigned key=0;key<KUI_SHELL_KEY_COUNT;key++) {
        const unsigned directions[]={KUI_SHELL_UP,KUI_SHELL_DOWN,KUI_SHELL_LEFT,KUI_SHELL_RIGHT};
        for(unsigned d=0;d<4;d++) {
            s.keyboard_selected=key;
            for(unsigned n=0;n<30;n++) {
                press(directions[d],false); assert(s.keyboard_selected<KUI_SHELL_KEY_COUNT);
            }
        }
    }
    assert(!strcmp(kui_shell_key_label(40,false),"SPACE"));
    assert(!strcmp(kui_shell_key_label(41,false),"BACK"));
    assert(!strcmp(kui_shell_key_label(42,false),"DONE"));
    assert(!strcmp(kui_shell_key_label(UINT_MAX,false),""));
}
static void advanced_navigation(void) {
    reset(KUI_SHELL_RIPPER); press(KUI_SHELL_START,false);
    assert(s.page==KUI_SHELL_ADVANCED);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VERIFY && s.page==KUI_SHELL_RIPPER);
    press(KUI_SHELL_START,false); press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_RESUME && s.page==KUI_SHELL_RIPPER);
    press(KUI_SHELL_START,false); press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_LOAD_SETTINGS && s.page==KUI_SHELL_RIPPER_SETTINGS);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_DISCARD_SETTINGS && s.page==KUI_SHELL_ADVANCED);
    press(KUI_SHELL_B,false); assert(s.page==KUI_SHELL_RIPPER);
    press(KUI_SHELL_START,false);s.advanced_selected=0;
    press(KUI_SHELL_UP,false);assert(s.advanced_selected==6);
    press(KUI_SHELL_UP,false);assert(s.advanced_selected==5);
    press(KUI_SHELL_UP,false);assert(s.advanced_selected==4);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST && s.page==KUI_SHELL_DESTINATION);
}
static void music_and_boot_controls(void) {
    const enum kui_shell_page song_pages[]={KUI_SHELL_HOME,KUI_SHELL_RIPPER};
    for(unsigned i=0;i<2;i++) for(unsigned busy=0;busy<2;busy++) {
        reset(song_pages[i]);
        assert(press(KUI_SHELL_L,busy)==KUI_SHELL_MUSIC_PREVIOUS);
        assert(press(KUI_SHELL_R,busy)==KUI_SHELL_MUSIC_NEXT);
        assert(press(KUI_SHELL_L|KUI_SHELL_R,busy)==KUI_SHELL_NONE);
        assert(s.page==song_pages[i] && !s.confirm_new);
        assert(press(KUI_SHELL_B|KUI_SHELL_R,busy)==(busy?KUI_SHELL_STOP:KUI_SHELL_NONE));
    }
    reset(KUI_SHELL_HOME);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_MUSIC_CYCLE && s.page==KUI_SHELL_HOME);
    assert(press(KUI_SHELL_Y|KUI_SHELL_A,false)==KUI_SHELL_MUSIC_CYCLE);
    assert(press(KUI_SHELL_Y,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_Y|KUI_SHELL_B,false)==KUI_SHELL_NONE);
    reset(KUI_SHELL_RIPPER);assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VERIFY);
    reset(KUI_SHELL_DIAGNOSTICS);assert(press(KUI_SHELL_Y,false)==KUI_SHELL_SAVE_LOG);
    reset(KUI_SHELL_SETTINGS);assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE);
    reset(KUI_SHELL_GD_PLAY);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_gd_boot);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.confirm_gd_boot);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_gd_boot);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.confirm_gd_boot);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,true)==KUI_SHELL_STOP && !s.confirm_gd_boot);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GD_BOOT && !s.confirm_gd_boot);
    reset(KUI_SHELL_ADVANCED);s.advanced_selected=3;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_quick_resume);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_DOWN,false)==KUI_SHELL_NONE);
    assert(s.confirm_quick_resume && s.advanced_selected==3);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.confirm_quick_resume);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_RESUME_QUICK && s.page==KUI_SHELL_RIPPER);
    assert(!s.confirm_quick_resume);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_RESUME); /* Default remains thorough. */
    reset(KUI_SHELL_MUSIC);
    assert(!strcmp(s.music_path,"/Music"));
    struct kui_music_player_page page={.count=2,.has_more=true};
    strcpy(page.root,"/Other");strcpy(page.entries[0].name,"ambient");page.entries[0].directory=true;
    strcpy(page.entries[1].name,"test.wav");
    kui_shell_set_music_listing(&s,&page);assert(!s.music_listing.count);
    strcpy(page.root,"/Music");kui_shell_set_music_listing(&s,&page);
    assert(s.music_listing.count==2);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_MUSIC_LIST && !strcmp(s.music_path,"/Music/ambient"));
    assert(!s.music_listing.count && !s.music_page);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_MUSIC_LIST && !strcmp(s.music_path,"/Music"));
    kui_shell_set_music_listing(&s,&page);s.music_selected=1;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_MUSIC_PLAY);
    assert(!strcmp(s.music_selected_path,"/Music/test.wav"));
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    s.page=KUI_SHELL_MUSIC;
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_MUSIC_STOP && s.page==KUI_SHELL_MUSIC);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_MUSIC_LIST && s.music_page==1);
    assert(!s.music_listing.count);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_MUSIC_LIST && !s.music_page);
    kui_shell_set_music_listing(&s,&page);s.music_listing.entries[0].disabled=true;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.music_listing.message[0]);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_MUSIC_LIST);
    page.count=UINT_MAX;memset(page.entries[0].name,'A',sizeof(page.entries[0].name));
    kui_shell_set_music_listing(&s,&page);
    assert(s.music_listing.count==KUI_MUSIC_PLAYER_ROWS && s.music_listing.entries[0].disabled);
    assert(!strcmp(s.music_listing.entries[0].name,"[Name too long]"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_MUSIC_LIST && !strcmp(s.music_path,"/"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
}
static void games_controls(void) {
    reset(KUI_SHELL_HOME);s.home_selected=8;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST);
    assert(s.page==KUI_SHELL_GAMES && !strcmp(s.games_path,"/Games"));
    struct kui_games_page page={.count=2,.has_more=true};
    strcpy(page.root,"/Other");strcpy(page.entries[0].name,"Fighting");
    strcpy(page.entries[0].path,"/Games/Fighting");page.entries[0].directory=true;
    strcpy(page.entries[1].name,"Dead or Alive 2");
    strcpy(page.entries[1].path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
    kui_shell_set_games_listing(&s,&page);assert(!s.games_listing.count);
    strcpy(page.root,"/Games");kui_shell_set_games_listing(&s,&page);
    assert(s.games_listing.count==2 && !s.games_listing.entries[0].disabled);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST);
    assert(!strcmp(s.games_path,"/Games/Fighting") && !s.games_listing.count);
    kui_shell_set_games_listing(&s,&page);assert(!s.games_listing.count);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_GAMES_LIST && !strcmp(s.games_path,"/Games"));
    kui_shell_set_games_listing(&s,&page);press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_INSPECT && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(!strcmp(s.games_selected_path,page.entries[1].path));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE); /* Uninspected images cannot start a test. */
    assert(press(KUI_SHELL_B|KUI_SHELL_A,true)==KUI_SHELL_STOP && s.page==KUI_SHELL_GAMES_DETAIL);
    struct kui_games_detail detail={.valid=true,.tracks=3};
    strcpy(detail.path,"/Games/Other.gdi");strcpy(detail.title,"Other image");
    kui_shell_set_games_detail(&s,&detail);assert(!s.games_detail.valid);
    strcpy(detail.path,s.games_selected_path);strcpy(detail.title,"Dead or Alive 2");
    kui_shell_set_games_detail(&s,&detail);assert(s.games_detail.valid && s.games_detail.tracks==3);
    assert(kui_shell_games_image_ready(&s));
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_L|KUI_SHELL_R|KUI_SHELL_START,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(kui_shell_games_image_ready(&s));
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP && s.page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_IMAGE_PROBE);
    /* The confirmation must not hand off stale, malformed, or missing details. */
    strcpy(s.games_detail.path,"/Games/Other.gdi");
    assert(!kui_shell_games_image_ready(&s) && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    memset(s.games_detail.path,'x',sizeof(s.games_detail.path));
    assert(!kui_shell_games_image_ready(&s) && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    strcpy(s.games_detail.path,s.games_selected_path);s.games_detail.valid=false;
    assert(!kui_shell_games_image_ready(&s) && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    s.games_detail.valid=true;strcpy(s.games_selected_path,"/Games/../invalid.gdi");
    strcpy(s.games_detail.path,s.games_selected_path);
    assert(!kui_shell_games_image_ready(&s) && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    strcpy(s.games_selected_path,detail.path);strcpy(s.games_detail.path,detail.path);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_DETAIL);
    assert(press(KUI_SHELL_A|KUI_SHELL_X,false)==KUI_SHELL_GAMES_INSPECT && !s.games_detail.valid);
    kui_shell_set_games_detail(&s,&detail);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_GAMES_INSPECT && !s.games_detail.valid);
    strcpy(detail.message,"Track file missing");detail.valid=false;
    kui_shell_set_games_detail(&s,&detail);assert(!strcmp(s.games_detail.message,"Track file missing"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES);
    assert(s.games_selected==1 && s.games_listing.count==2);
    detail.valid=true;kui_shell_set_games_detail(&s,&detail);assert(!s.games_detail.valid);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_GAMES_LIST && s.games_page==1);
    assert(!s.games_listing.count && !s.games_selected);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_GAMES_LIST && !s.games_page);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_GAMES_LIST);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_ADVANCED);
    kui_shell_set_games_listing(&s,&page);assert(!s.games_listing.count);
    press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST && !strcmp(s.games_path,"/"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    press(KUI_SHELL_A,false);assert(!strcmp(s.games_path,"/Games"));
    press(KUI_SHELL_START,false);press(KUI_SHELL_B,false);
    assert(s.page==KUI_SHELL_GAMES && !strcmp(s.games_path,"/Games"));
    press(KUI_SHELL_START,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST && !strcmp(s.games_path,"/Games"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);

    /* The accepted synthetic probe remains available independently of a game. */
    reset(KUI_SHELL_GAMES_ADVANCED);
    press(KUI_SHELL_UP,false);assert(s.games_advanced_selected==2);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_ADVANCED);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_PROBE_CONFIRM);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_L|KUI_SHELL_R|KUI_SHELL_START,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_ADVANCED);
    assert(s.games_advanced_selected==2);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_PROBE_CONFIRM);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_PROBE);
    assert(s.page==KUI_SHELL_GAMES_PROBE_CONFIRM);
    press(KUI_SHELL_B,false);press(KUI_SHELL_DOWN,false);
    assert(s.games_advanced_selected==0);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST && !strcmp(s.games_path,"/Games"));

    /* A long complete file path is not truncated to the folder path capacity. */
    reset(KUI_SHELL_GAMES);page.count=1;page.entries[0].directory=false;
    strcpy(page.entries[0].name,"A long game title.gdi");
    memset(page.entries[0].path,0,sizeof(page.entries[0].path));
    strcpy(page.entries[0].path,"/Games/");memset(page.entries[0].path+7,'A',100);
    page.entries[0].path[107]='/';memset(page.entries[0].path+108,'B',80);
    strcpy(page.entries[0].path+188,".gdi");
    kui_shell_set_games_listing(&s,&page);
    assert(!s.games_listing.entries[0].disabled);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_INSPECT && strlen(s.games_selected_path)==192);
    assert(!strcmp(s.games_selected_path,page.entries[0].path));
    press(KUI_SHELL_B,false);
    const char *bad[]={"/Games/../outside.gdi","/Games2/title.gdi","/Games//title.gdi",
        "0:/Games/title.gdi","/Games/./title.gdi","/Games/title\\file.gdi"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        snprintf(page.entries[0].path,sizeof(page.entries[0].path),"%s",bad[i]);
        kui_shell_set_games_listing(&s,&page);
        assert(s.games_listing.entries[0].disabled && !s.games_listing.entries[0].path[0]);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.games_listing.message[0]);
    }
    memset(page.entries[0].path,'x',sizeof(page.entries[0].path));
    memset(page.entries[0].name,'x',sizeof(page.entries[0].name));page.count=UINT_MAX;
    memset(page.message,'x',sizeof(page.message));kui_shell_set_games_listing(&s,&page);
    assert(s.games_listing.count==KUI_GAMES_ROWS && s.games_listing.entries[0].disabled);
    assert(!strcmp(s.games_listing.entries[0].name,"[Name too long]"));
    assert(!s.games_listing.message[sizeof(s.games_listing.message)-1]);
    s.games_page=UINT_MAX/KUI_GAMES_ROWS;s.games_listing.has_more=true;
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE);
}
static uint16_t pixels[640*480+2], prepared[640*480+2];
static char drawn[8192];
static size_t drawn_size;
static uint16_t badge_color;
static void observe(void *ctx,unsigned x,unsigned y,uint16_t color,const char *value,bool large) {
    (void)ctx;
    if(!strncmp(value,"STREAM CRC:",11)) badge_color=color;
    assert(x>=32 && x+kui_shell_font_width(value,large)<=608 && y>=24 &&
        y+(large?KUI_SHELL_FONT_LARGE_HEIGHT:KUI_SHELL_FONT_SMALL_HEIGHT)<=448);
    for(size_t i=0;value[i];i++) assert(value[i]>=32 && value[i]<=126);
    size_t n=strlen(value);
    assert(drawn_size+n+2<sizeof(drawn));
    memcpy(drawn+drawn_size,value,n); drawn_size+=n;
    drawn[drawn_size++]='\n'; drawn[drawn_size]=0;
}
static void render(struct kui_shell_view *v) {
    pixels[0]=0x1234; pixels[640*480+1]=0xabcd;
    drawn_size=0; drawn[0]=0; badge_color=0;
    kui_shell_draw(pixels+1,&s,v,observe,NULL);
    assert(pixels[0]==0x1234 && pixels[640*480+1]==0xabcd);
    prepared[0]=0x1234; prepared[640*480+1]=0xabcd;
    for(unsigned i=1;i<=640*480;i++) prepared[i]=0x0864;
    kui_shell_draw_content(prepared+1,&s,v,NULL,NULL);
    assert(memcmp(pixels,prepared,sizeof(pixels))==0);

}
static void rendering_semantics(void) {
    const char *logs[]={"A very long diagnostic line deliberately exceeding safe frame margins 0123456789012345678901234567890"};
    struct kui_shell_view v={.build="0123456789abcdef",.log_lines=logs,.log_count=1,
        .total_log_lines=1,.done=UINT64_MAX-1,.total=UINT64_MAX};
    for(unsigned page=0;page<=KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM;page++) {
        reset((enum kui_shell_page)page); render(&v);
    }
    reset(KUI_SHELL_DIAGNOSTICS); render(&v);
    assert(strstr(drawn,"...")); /* Clipped log text is visibly incomplete. */
    reset(KUI_SHELL_RIPPER);
    v.phase=4; v.outcome=KUI_SHELL_OUTCOME_COMPLETE;
    render(&v);
    assert(strstr(drawn,"Completed"));
    assert(strstr(drawn,"Saved bytes not fully reread"));
    assert(!strstr(drawn,"reread and verified"));
    v.saved_verified=true; render(&v);
    assert(strstr(drawn,"Saved bytes reread and verified"));
    s.confirm_new=true; render(&v);
    assert(strstr(drawn,"START A NEW DUMP?") && strstr(drawn,"B Cancel"));
    s.confirm_new=false; v.busy=true; v.saving=true; v.cancel_requested=true;
    render(&v); assert(strstr(drawn,"CANCELLING SAVE"));
    v.busy=false; v.saving=false; v.cancel_requested=false;
    v.outcome=KUI_SHELL_OUTCOME_STOPPED; v.job_dir=NULL;
    render(&v); assert(strstr(drawn,"Stopped") && !strstr(drawn,"partial dump kept"));
    v.job_dir="/KUI/dumps/example"; render(&v);
    assert(strstr(drawn,"partial dump kept"));
    reset(KUI_SHELL_RIPPER_SETTINGS); press(KUI_SHELL_RIGHT,false);
    v.settings_notice="Save failed. Review the diagnostic log.";
    render(&v);
    assert(strstr(drawn,"Unsaved changes") && strstr(drawn,"Save failed"));
    assert(strstr(drawn,"ON (SHA required)"));
}
static void reference_and_destination_rendering(void) {
    reset(KUI_SHELL_RIPPER);
    struct kui_shell_view v={.outcome=KUI_SHELL_OUTCOME_COMPLETE,
        .disc_title="Long disc title \xc3\xa9",.gdi_name="MDK2.gdi"};
    render(&v);
    assert(strstr(drawn,"Destination: /Games"));
    assert(strstr(drawn,"Long disc title ?"));
    assert(strstr(drawn,"Reference not checked") && badge_color!=0x8ef6);
    v.reference_checked=true;
    for(int result=KUI_KNOWN_ERROR;result<=KUI_KNOWN_FULL_MATCH;result++) {
        v.reference.result=(enum kui_known_result)result;
        render(&v);
        assert((badge_color==0x8ef6)==(result==KUI_KNOWN_FULL_MATCH));
        if(result>=KUI_KNOWN_PARTIAL && result<KUI_KNOWN_FULL_MATCH) assert(badge_color==0xfda9);
    }
    assert(strstr(drawn,"STREAM CRC: FULL TRACK MATCH"));
    assert(strstr(drawn,"Saved bytes not fully reread"));
    v.saved_verified=true; render(&v);
    assert(strstr(drawn,"Saved bytes reread and verified"));
    v.busy=true; v.phase=2; render(&v);
    assert(!strstr(drawn,"STREAM CRC:") && strstr(drawn,"Capturing disc"));
    v.busy=false; s.page=KUI_SHELL_DESTINATION;
    s.listing.count=UINT_MAX; s.listing.has_more=true;
    for(unsigned i=0;i<KUI_DEST_PAGE_SIZE;i++) {
        memset(s.listing.entries[i].name,'M',KUI_DEST_NAME_CAP-1);
        s.listing.entries[i].name[KUI_DEST_NAME_CAP-1]=0;
    }
    render(&v); assert(strstr(drawn,"PAGE 1 +") && strstr(drawn,"..."));
    s.page=KUI_SHELL_KEYBOARD;
    memset(s.keyboard,'M',KUI_DEST_ROOT_CAP-1); s.keyboard[KUI_DEST_ROOT_CAP-1]=0;
    s.keyboard_selected=42; render(&v);
    assert(strstr(drawn,"SPACE") && strstr(drawn,"BACK") && strstr(drawn,"DONE"));
    assert(strstr(drawn,"X Backspace") && strstr(drawn,"Y Shift"));
}
static void new_pages_rendering(void) {
    struct kui_shell_view v={.inserted_title="Sword of the Berserk",.disc_title="MDK2",
        .phase=4,.outcome=KUI_SHELL_OUTCOME_COMPLETE,.reference_checked=true,
        .reference={.result=KUI_KNOWN_FULL_MATCH},.memory_valid=true,
        .memory_used=1024,.memory_physical=16384*1024};
    reset(KUI_SHELL_RIPPER);render(&v);
    assert(strstr(drawn,"Inserted: Sword of the Berserk"));
    assert(strstr(drawn,"Completed: MDK2") && strstr(drawn,"FULL TRACK MATCH"));
    assert(strstr(drawn,"RAM 1 / 16384 KiB"));
    s.system_saved.show_memory=false;s.saved.show_memory=true;render(&v);
    assert(strstr(drawn,"RAM 1 /")); /* Ripper RAM is always shown. */
    v.outcome=KUI_SHELL_OUTCOME_FAILED;v.phase=2;v.drive_reset_required=true;
    v.message="Capture DMA timed out; abort failed. Restart required.";
    render(&v);
    assert(strstr(drawn,"Drive stopped - restart required") && strstr(drawn,"Capture DMA timed out"));
    assert(!strstr(drawn,"Verifying") && !strstr(drawn,"FULL TRACK MATCH"));
    v.drive_reset_required=false;v.busy=true;v.total=10*1024;v.done=1024;
    v.dma_degraded=true;render(&v);
    assert(strstr(drawn,"Drive errors: PIO active. Reboot to restore DMA."));
    assert(strstr(drawn,"RAM 1 /") && strstr(drawn,"L/R Songs"));
    v.dma_degraded=false;
    v.rate_kib=1;v.phase_elapsed_ms=2000;render(&v);
    assert(strstr(drawn,"Disc: MDK2") && strstr(drawn,"PHASE ETA 0:09"));
    v.progress_age_ms=3001;render(&v);assert(strstr(drawn,"PHASE ETA waiting"));
    v.progress_age_ms=0;v.phase_elapsed_ms=0;render(&v);
    assert(strstr(drawn,"PHASE ETA calculating"));
    reset(KUI_SHELL_SETTINGS);v.busy=false;v.video_trial=true;v.video_seconds=7;
    render(&v);
    assert(strstr(drawn,"System settings") && strstr(drawn,"640x480") && strstr(drawn,"VGA follows"));
    assert(strstr(drawn,"Keep this video mode?") && strstr(drawn,"in 7 seconds"));
    assert(strstr(drawn,"A Keep mode") && strstr(drawn,"B Revert"));
    v.video_trial=false;s.system_selected=2;v.music_title="Menu song 01";render(&v);
    assert(strstr(drawn,"X Next song") && strstr(drawn,"Menu song 01"));
    assert(!strstr(drawn,"Capture hashes"));
    struct kui_app_status status={.line_count=UINT_MAX,.done=UINT64_MAX,
        .total=UINT64_MAX,.complete=true,.passed=true};
    strcpy(status.message,"Allocated region passed");
    for(unsigned i=0;i<KUI_APP_LINES;i++) snprintf(status.lines[i],KUI_APP_LINE_CAP,
        "Line %u: 0123456789012345678901234567890123456789012345678901234567890123456789",i);
    v.app_status=&status;reset(KUI_SHELL_MEMORY);render(&v);
    assert(strstr(drawn,"Memory Test") && strstr(drawn,"Allocated region passed"));
    assert(!strstr(drawn,"Line 3:") && strstr(drawn,"Line 4:") && strstr(drawn,"Line 11:"));
    s.page=KUI_SHELL_NETWORK;render(&v);
    assert(strstr(drawn,"Network Test") && strstr(drawn,"Inspect adapter"));
    reset(KUI_SHELL_VMU);s.vmu.present=true;s.vmu.total=8;s.vmu.count=8;
    for(unsigned i=0;i<8;i++) {
        snprintf(s.vmu.entries[i].name,16,"SAVE_%02u",i);s.vmu.entries[i].bytes=32768;
    }
    s.vmu_slot=7;s.vmu_selected=7;render(&v);
    assert(strstr(drawn,"VMU D2") && strstr(drawn,"SAVE_07") && strstr(drawn,"32768 bytes"));
    assert(strstr(drawn,"LEFT/RIGHT VMU") && strstr(drawn,"L Copy / delete selected"));
    reset(KUI_SHELL_HOME);v.app_status=NULL;
    render(&v);
    assert(strstr(drawn,"Inserted: Sword of the Berserk"));
    v.inserted_title=NULL;render(&v);
    assert(strstr(drawn,"Inserted: No disc detected"));
    v.inserted_title="A very long inserted disc title which exceeds the launcher subtitle width";
    render(&v);assert(strstr(drawn,"Inserted:") && strstr(drawn,"..."));
    for(unsigned i=0;i<8;i++) {
        s.home_selected=i;render(&v);
        assert(strstr(drawn,"RAM 1 / 16384 KiB") && strstr(drawn,"Memory Test"));
    }
    s.system_saved.show_memory=false;render(&v);
    assert(strstr(drawn,"9 applications") && !strstr(drawn,"RAM 1 /"));
}
static void music_and_boot_rendering(void) {
    struct kui_shell_view v={.music_enabled=true,.music_playing=true,.music_volume=75,
        .music_title="Neon Circuit",.inserted_title="MDK2"};
    reset(KUI_SHELL_HOME);render(&v);
    assert(strstr(drawn,"Y Music 75%") && strstr(drawn,"Neon Circuit"));
    assert(strstr(drawn,"Y Volume") && strstr(drawn,"L/R Songs") && strstr(drawn,"GD Play"));
    v.music_enabled=false;v.music_playing=false;render(&v);
    assert(strstr(drawn,"Y Music off"));
    v.music_enabled=true;v.music_paused=true;v.busy=true;render(&v);
    assert(strstr(drawn,"Music paused") && !strstr(drawn,"Y Music"));
    v.busy=false;v.music_paused=false;reset(KUI_SHELL_RIPPER);render(&v);
    assert(!strstr(drawn,"Y Music") && strstr(drawn,"Music 75%"));
    assert(strstr(drawn,"START Advanced") && !strstr(drawn,"L Memory"));
    v.music_change_pending=true;render(&v);
    assert(strstr(drawn,"Music change queued") && strstr(drawn,"Neon Circuit"));
    v.music_change_pending=false;
    reset(KUI_SHELL_ADVANCED);s.advanced_selected=3;render(&v);
    assert(strstr(drawn,"Quick resume (sizes only)") && strstr(drawn,"Same-size corruption"));
    s.advanced_selected=4;render(&v);
    assert(strstr(drawn,"Destination folder") && strstr(drawn,"enter a destination path"));
    s.advanced_selected=3;
    s.confirm_quick_resume=true;render(&v);
    assert(strstr(drawn,"QUICK RESUME WITHOUT REREADING?"));
    assert(strstr(drawn,"Previously saved bytes will not be reread."));
    assert(strstr(drawn,"Same-size damage is not detected"));
    reset(KUI_SHELL_GD_PLAY);render(&v);
    assert(strstr(drawn,"Inserted: MDK2") && strstr(drawn,"A Boot via console BIOS"));
    s.confirm_gd_boot=true;render(&v);
    assert(strstr(drawn,"EXIT K-UI AND BOOT VIA CONSOLE BIOS?"));
    assert(strstr(drawn,"region and autostart"));
    reset(KUI_SHELL_MUSIC);
    strcpy(s.music_listing.entries[0].name,"test.wav");s.music_listing.count=1;
    strcpy(s.music_listing.entries[1].name,"Albums");s.music_listing.entries[1].directory=true;
    s.music_listing.count=2;render(&v);
    assert(strstr(drawn,"Music Player") && strstr(drawn,"SD: /Music"));
    assert(strstr(drawn,"test.wav") && strstr(drawn,"Albums") && strstr(drawn,"WAV"));
    assert(strstr(drawn,"B Parent") && strstr(drawn,"START Home") && !strstr(drawn,"CD playback"));
    assert(strstr(drawn,"Y Stop") && strstr(drawn,"Cached 0.0 MiB / 8 MiB"));
}
static void round_four_rendering(void) {
    struct kui_shell_view v={.music_cache_bytes=4674286};
    reset(KUI_SHELL_SETTINGS);render(&v);
    assert(strstr(drawn,"Startup chime") && strstr(drawn,"Start in") && strstr(drawn,"Console clock"));
    s.confirm_defaults=true;render(&v);assert(strstr(drawn,"USE DEFAULT SYSTEM") && strstr(drawn,"draft"));
    reset(KUI_SHELL_CLOCK);const struct kui_datetime d={2026,9,23,20,15,31};
    kui_shell_set_clock(&s,&d,NULL);render(&v);
    assert(strstr(drawn,"2026") && strstr(drawn,"Existing dates stay intact"));
    s.confirm_clock=true;render(&v);assert(strstr(drawn,"2026-09-23  20:15:31") && strstr(drawn,"SET THE CONSOLE CLOCK?"));
    reset(KUI_SHELL_VMU_RESTORE);s.vmu_slot=5;s.backups.count=1;
    strcpy(s.backups.entries[0].name,"MDK2_SAVE");strcpy(s.backups.entries[0].folder,"v0001");render(&v);
    assert(strstr(drawn,"Target VMU C2") && strstr(drawn,"Existing names are refused"));
    s.confirm_vmu_restore=true;strcpy(s.restore_name,"MDK2_SAVE");s.restore_bytes=4096;render(&v);
    assert(strstr(drawn,"WRITE THIS SAVE TO THE VMU?") && strstr(drawn,"4096 bytes / 8 blocks"));
    assert(strstr(drawn,"never overwritten"));
    reset(KUI_SHELL_DESTINATION);s.browse_for_scan=true;render(&v);
    assert(strstr(drawn,"Choose dump to scan") && strstr(drawn,"Y Scan this folder") && !strstr(drawn,"X Type path"));
    reset(KUI_SHELL_CRC_SCAN);strcpy(s.browse_path,"/Games/MDK2");render(&v);
    assert(strstr(drawn,"Mode1 EDC/parity") && strstr(drawn,"no disc reads") && strstr(drawn,"/Games/MDK2"));
    reset(KUI_SHELL_MUSIC);render(&v);assert(strstr(drawn,"Cached 4.4 MiB / 8 MiB"));
}
static void round_five_controls(void) {
    reset(KUI_SHELL_VMU);
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU);
    s.vmu.present=true;s.vmu.count=1;s.vmu.total=1;
    strcpy(s.vmu.entries[0].name,"TEST_SAVE");s.vmu.entries[0].bytes=4096;
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_ACTIONS);
    assert(s.vmu_copy_slot==1 && !s.confirm_vmu_delete);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_DELETE_PREVIEW);
    struct kui_vmu_view preview={.slot=1,.delete_ready=true,.count=1};
    strcpy(preview.entries[0].name,"ACTUAL_SAVE");preview.entries[0].bytes=8192;
    kui_shell_set_vmu_delete_preview(&s,&preview);assert(!s.confirm_vmu_delete);
    preview.slot=0;kui_shell_set_vmu_delete_preview(&s,&preview);assert(s.confirm_vmu_delete);
    assert(!strcmp(s.vmu.entries[0].name,"ACTUAL_SAVE") && s.vmu.entries[0].bytes==8192);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_vmu_delete);
    kui_shell_set_vmu_delete_preview(&s,&preview);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_DELETE_COMMIT && !s.confirm_vmu_delete);
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.vmu_action_selected==1);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==7);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==1);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COPY_PREVIEW);
    preview.copy_ready=true;preview.slot=0;kui_shell_set_vmu_copy_preview(&s,&preview);assert(!s.confirm_vmu_copy);
    strcpy(preview.entries[0].name,"LATEST_SAVE");
    preview.slot=1;kui_shell_set_vmu_copy_preview(&s,&preview);assert(s.confirm_vmu_copy);
    assert(!strcmp(s.vmu.entries[0].name,"LATEST_SAVE"));
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==1);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COPY_COMMIT && !s.confirm_vmu_copy);
    preview.copy_ready=false;preview.status.complete=true;
    kui_shell_set_vmu_copy_preview(&s,&preview);assert(!s.vmu.count && !s.confirm_vmu_copy);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_LIST && s.page==KUI_SHELL_VMU);
    reset(KUI_SHELL_MUSIC);
    assert(press(KUI_SHELL_X,true)==KUI_SHELL_NONE && !s.confirm_music_clear);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.confirm_music_clear);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_music_clear);
    press(KUI_SHELL_X,false);assert(press(KUI_SHELL_A,false)==KUI_SHELL_MUSIC_CLEAR_CACHE);
    assert(!s.confirm_music_clear && press(KUI_SHELL_R,false)==KUI_SHELL_MUSIC_LIST);
    reset(KUI_SHELL_SETTINGS);s.system_selected=8;
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.system_draft.screen_inset==2);
    assert(kui_shell_system_dirty(&s) && s.system_saved.screen_inset==0);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_SYSTEM);
    press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SYSTEM_INSPECT && s.page==KUI_SHELL_SYSTEM_TOOLS);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_FLASH_BACKUP);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_BIOS_BACKUP);
    press(KUI_SHELL_UP,false);assert(s.tools_selected==3);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_restart);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_restart);
    press(KUI_SHELL_A,false);assert(press(KUI_SHELL_A,false)==KUI_SHELL_RESTART);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SETTINGS);
    reset(KUI_SHELL_SETTINGS);s.system_selected=10;
    assert(!s.system_saved.menu_sounds && !s.system_draft.menu_sounds);
    press(KUI_SHELL_RIGHT,false);assert(s.system_draft.menu_sounds && kui_shell_system_dirty(&s));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_SYSTEM);
    reset(KUI_SHELL_MUSIC);assert(press(KUI_SHELL_L,false)==KUI_SHELL_CD_LIST && s.page==KUI_SHELL_CD_AUDIO);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    struct kui_cd_audio_status cd={.loaded=true,.count=2};cd.tracks[0].number=2;cd.tracks[1].number=4;
    kui_shell_set_cd_audio(&s,&cd);press(KUI_SHELL_DOWN,false);assert(s.cd_selected==1);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_CD_PLAY);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE);
    cd.playing=true;kui_shell_set_cd_audio(&s,&cd);assert(press(KUI_SHELL_Y,false)==KUI_SHELL_CD_PAUSE);
    cd.playing=false;cd.paused=true;kui_shell_set_cd_audio(&s,&cd);assert(press(KUI_SHELL_Y,false)==KUI_SHELL_CD_RESUME);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_CD_STOP);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_CD_LIST);
    cd.count=0;kui_shell_set_cd_audio(&s,&cd);assert(s.cd_selected==0);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_MUSIC);
    reset(KUI_SHELL_NETWORK);assert(press(KUI_SHELL_X,false)==KUI_SHELL_NETWORK_CONNECT);
    reset(KUI_SHELL_ADVANCED);s.advanced_selected=6;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SALVAGE);
    assert(!s.salvage_zero_fill && s.salvage_passes==1);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_salvage);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_salvage);
    s.salvage_selected=3;press(KUI_SHELL_RIGHT,false);assert(s.salvage_zero_fill);
    press(KUI_SHELL_DOWN,false);press(KUI_SHELL_LEFT,false);assert(s.salvage_passes==50);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_passes==1);
    s.salvage_selected=0;press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_NEW && !s.confirm_salvage);
    s.salvage_selected=1;assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RESUME);
    s.salvage_selected=2;assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RECOVER);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED);
    assert(kui_shell_progress_tenths(1,0)==0);
    assert(kui_shell_progress_tenths(1,3)==333);
    assert(kui_shell_progress_tenths(99,100)==990);
    assert(kui_shell_progress_tenths(UINT64_MAX-1,UINT64_MAX)==999);
    assert(kui_shell_progress_tenths(UINT64_MAX,UINT64_MAX)==1000);
    assert(kui_shell_progress_tenths(100,99)==1000);
}
static void round_five_rendering(void) {
    struct kui_shell_view v={.busy=true,.inserted_title="Dead or Alive 2",.done=7,.total=10,
        .retries=11,.retry_attempt=3,.retry_limit=10,.retry_fad=45150};
    reset(KUI_SHELL_RIPPER);render(&v);
    assert(strstr(drawn,"Disc: Dead or Alive 2") && !strstr(drawn,"Identifying..."));
    assert(strstr(drawn,"70.0%") && strstr(drawn,"TOTAL RETRIES 11"));
    assert(strstr(drawn,"Read retry 3/10 at FAD 45150"));
    v.busy=false;reset(KUI_SHELL_VMU_ACTIONS);s.vmu.present=true;s.vmu.count=1;s.vmu_copy_slot=1;
    strcpy(s.vmu.entries[0].name,"TEST_SAVE");render(&v);
    assert(strstr(drawn,"Source VMU A1: TEST_SAVE") && strstr(drawn,"Copy to VMU A2"));
    s.confirm_vmu_delete=true;render(&v);assert(strstr(drawn,"DELETE THIS SAVE") && strstr(drawn,"verified SD backup"));
    s.confirm_vmu_delete=false;s.confirm_vmu_copy=true;render(&v);assert(strstr(drawn,"COPY THIS SAVE") && strstr(drawn,"Destination VMU A2"));
    reset(KUI_SHELL_MUSIC);s.confirm_music_clear=true;render(&v);
    assert(strstr(drawn,"CLEAR CACHED MUSIC?") && strstr(drawn,"files remain"));
    reset(KUI_SHELL_SETTINGS);s.system_selected=8;render(&v);
    assert(strstr(drawn,"Safe area") && strstr(drawn,"System tools"));
    reset(KUI_SHELL_SYSTEM_TOOLS);render(&v);assert(strstr(drawn,"Inspect system hardware") && strstr(drawn,"Back up visible BIOS bank"));
    s.confirm_restart=true;render(&v);assert(strstr(drawn,"RESTART THE CONSOLE?"));
    reset(KUI_SHELL_CD_AUDIO);s.cd_audio.loaded=true;s.cd_audio.count=2;
    s.cd_audio.tracks[0].number=2;s.cd_audio.tracks[0].seconds=185;
    s.cd_audio.tracks[1].number=4;s.cd_audio.tracks[1].seconds=61;
    s.cd_audio.playing=true;s.cd_audio.current=4;render(&v);
    assert(strstr(drawn,"Audio CD player") && strstr(drawn,"Track 04  <") && strstr(drawn,"3:05"));
    reset(KUI_SHELL_SALVAGE);render(&v);assert(strstr(drawn,"Zero-fill unreadable sectors") && strstr(drawn,"OFF"));
    s.confirm_salvage=true;render(&v);assert(strstr(drawn,"Zero-fill OFF") && strstr(drawn,"Placeholders are not repaired"));
    s.salvage_zero_fill=true;render(&v);assert(strstr(drawn,"Zero-fill ON"));
    s.confirm_salvage=false;v.busy=true;
    struct kui_app_status state={.line_count=3,.total=10,.done=5};
    strcpy(state.message,"Retrying unresolved sectors");
    strcpy(state.lines[0],"Track 3/31  FAD 45150  attempts 3");
    strcpy(state.lines[1],"Targets 12  recovered 5  remaining 7");
    strcpy(state.lines[2],"Recovery pass 2/5  First pass: complete");
    v.app_status=&state;render(&v);
    assert(strstr(drawn,"remaining 7") && strstr(drawn,"pass 2/5") && !strstr(drawn,"New salvage job"));
}
static void games_rendering(void) {
    struct kui_shell_view view={0};
    reset(KUI_SHELL_HOME);s.home_selected=8;render(&view);
    assert(strstr(drawn,"Games") && strstr(drawn,"9 applications") && strstr(drawn,"Game launching is not ready yet"));
    reset(KUI_SHELL_GAMES);s.games_listing.count=8;s.games_listing.has_more=true;s.games_selected=7;
    for(unsigned i=0;i<8;i++) {
        snprintf(s.games_listing.entries[i].name,sizeof(s.games_listing.entries[i].name),"Game %u with a long but bounded name",i+1);
        s.games_listing.entries[i].directory=i==0;
    }
    strcpy(s.games_listing.message,"Choose a GDI image to inspect.");render(&view);
    assert(strstr(drawn,"Game 8") && strstr(drawn,"GDI") && strstr(drawn,"DIR"));
    assert(strstr(drawn,"PAGE 1 +") && strstr(drawn,"START Advanced") && !strstr(drawn,"A Launch"));
    view.busy=true;render(&view);assert(strstr(drawn,"B Stop safely"));view.busy=false;
    reset(KUI_SHELL_GAMES_DETAIL);strcpy(s.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
    strcpy(s.games_detail.path,s.games_selected_path);
    s.games_detail.valid=true;s.games_detail.tracks=3;s.games_detail.data_tracks=2;s.games_detail.audio_tracks=1;
    s.games_detail.bytes=1185765648;s.games_detail.boot_bytes=123456;s.games_detail.boot_lba=45166;
    strcpy(s.games_detail.title,"Dead or Alive 2");strcpy(s.games_detail.product,"T-3601N");
    strcpy(s.games_detail.region,"JUE");strcpy(s.games_detail.boot_file,"1ST_READ.BIN");render(&view);
    assert(strstr(drawn,"Dead or Alive 2") && strstr(drawn,"T-3601N") && strstr(drawn,"1ST_READ.BIN"));
    assert(strstr(drawn,"Tracks: 3") && strstr(drawn,"1185765648 bytes"));
    assert(strstr(drawn,"A Test image reads") && strstr(drawn,"X Inspect") && !strstr(drawn,"A Launch"));
    s.games_detail.valid=false;strcpy(s.games_detail.message,"Track file missing");render(&view);
    assert(strstr(drawn,"Could not inspect image") && strstr(drawn,"Track file missing"));
    assert(!strstr(drawn,"A Test image reads"));
    s.games_detail.stopped=true;render(&view);assert(strstr(drawn,"Inspection stopped"));
    reset(KUI_SHELL_GAMES_ADVANCED);render(&view);
    assert(strstr(drawn,"Game library") && strstr(drawn,"Browse SD folders") && strstr(drawn,"Resident loader probe"));
    assert(strstr(drawn,"IDE / CF sources are not available") && strstr(drawn,"Retail game launching is not available"));
    reset(KUI_SHELL_GAMES_PROBE_CONFIRM);render(&view);
    assert(strstr(drawn,"A Start probe") && strstr(drawn,"B Advanced"));
    assert(strstr(drawn,"Exits this menu") && strstr(drawn,"test data after shutdown"));
    assert(strstr(drawn,"does not launch a retail game") && strstr(drawn,"Photograph the final result"));
    assert(strstr(drawn,"Power cycle to return") && !strstr(drawn,"L Memory"));
    struct kui_app_status status={0};view.app_status=&status;view.busy=true;
    strcpy(status.message,"Validating the probe package");render(&view);
    assert(strstr(drawn,"Preparing the handoff") && strstr(drawn,status.message) && strstr(drawn,"B Stop safely"));
    assert(!strstr(drawn,"A Start probe"));
    view.busy=false;reset(KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM);
    strcpy(s.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
    strcpy(s.games_detail.path,s.games_selected_path);s.games_detail.valid=true;
    render(&view);
    assert(strstr(drawn,s.games_selected_path) && strstr(drawn,"A Start test") && strstr(drawn,"B Image details"));
    assert(strstr(drawn,"reads samples from this GDI") && strstr(drawn,"GD requests used by retail games"));
    assert(strstr(drawn,"game itself will not start") && strstr(drawn,"not a full image verification"));
    assert(strstr(drawn,"Photograph the final result, then power cycle") && !strstr(drawn,"L Memory"));
    view.busy=true;strcpy(status.message,"Mapping selected image files");render(&view);
    assert(strstr(drawn,"Preparing the handoff") && strstr(drawn,status.message) && strstr(drawn,"B Stop safely"));
    assert(!strstr(drawn,"A Start test"));view.busy=false;
    strcpy(s.games_detail.path,"/Games/Other.gdi");render(&view);
    assert(strstr(drawn,"Image details changed") && !strstr(drawn,"A Start test"));
}
int main(void) {
    games_controls(); games_rendering();
    launcher_and_confirmation(); operation_lock_and_stop(); settings_transaction();
    system_transaction_and_video(); app_navigation_and_vmu(); clock_and_defaults(); restore_and_scan_controls(); phase_eta();
    diagnostics(); destination_transaction(); keyboard_transaction(); advanced_navigation();
    rendering_semantics(); reference_and_destination_rendering(); new_pages_rendering();
    music_and_boot_controls(); music_and_boot_rendering(); round_four_rendering(); round_five_controls(); round_five_rendering();
    puts("PASS shell: Games browsing/inspection, stale result guards, Stop lock, system/ripper preferences, reversible video actions, VMU paging, phase ETA, destination keyboard, reference grades, safe rendering");
    return 0;
}
