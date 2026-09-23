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
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_MSTATS && s.confirm_new);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NEW_DUMP && !s.confirm_new);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.home_selected==5);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DIAGNOSTICS);
    press(KUI_SHELL_B,false);
    press(KUI_SHELL_UP,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_LOAD_SYSTEM);
    assert(s.page==KUI_SHELL_SETTINGS);
}
static void operation_lock_and_stop(void) {
    const unsigned launch=KUI_SHELL_A|KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_R;
    for(unsigned page=0;page<=KUI_SHELL_NETWORK;page++) {
        reset((enum kui_shell_page)page);
        assert(press(launch,true)==KUI_SHELL_NONE && s.page==page);
        assert(press(launch|KUI_SHELL_L|KUI_SHELL_B,true)==KUI_SHELL_STOP);
        assert(press(KUI_SHELL_L,true)==KUI_SHELL_MSTATS);
        assert(s.page==page);
    }
    reset(KUI_SHELL_RIPPER); s.confirm_new=true;
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP && !s.confirm_new);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_RESUME);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VERIFY);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_DEST_LIST);
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
    press(KUI_SHELL_DOWN,false);assert(s.system_selected==0);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE);
    assert(s.saved.crc_only==defaults.crc_only && s.saved.end_readback==defaults.end_readback);
}
static void app_navigation_and_vmu(void) {
    static const enum kui_shell_page pages[]={KUI_SHELL_RIPPER,KUI_SHELL_VMU,
        KUI_SHELL_MEMORY,KUI_SHELL_NETWORK,KUI_SHELL_SETTINGS,KUI_SHELL_DIAGNOSTICS};
    for(unsigned i=0;i<6;i++) {
        reset(KUI_SHELL_HOME);s.home_selected=i;
        enum kui_shell_action expected=i==1?KUI_SHELL_VMU_LIST:
            i==4?KUI_SHELL_LOAD_SYSTEM:KUI_SHELL_NONE;
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
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_DEST_LIST);
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
    press(KUI_SHELL_R,false);
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
    reset(KUI_SHELL_RIPPER); press(KUI_SHELL_R,false);
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
    press(KUI_SHELL_R,false); press(KUI_SHELL_X,false);
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
    for(unsigned page=0;page<=KUI_SHELL_NETWORK;page++) {
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
    assert(!strstr(drawn,"RAM 1 /")); /* Legacy ripper field no longer controls RAM. */
    v.outcome=KUI_SHELL_OUTCOME_FAILED;v.phase=2;v.drive_reset_required=true;
    v.message="Capture DMA timed out; abort failed. Restart required.";
    render(&v);
    assert(strstr(drawn,"Drive stopped - restart required") && strstr(drawn,"Capture DMA timed out"));
    assert(!strstr(drawn,"Verifying") && !strstr(drawn,"FULL TRACK MATCH"));
    v.drive_reset_required=false;v.busy=true;v.total=10*1024;v.done=1024;
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
    assert(strstr(drawn,"Network Test") && strstr(drawn,"Inspect network adapter"));
    reset(KUI_SHELL_VMU);s.vmu.present=true;s.vmu.total=8;s.vmu.count=8;
    for(unsigned i=0;i<8;i++) {
        snprintf(s.vmu.entries[i].name,16,"SAVE_%02u",i);s.vmu.entries[i].bytes=32768;
    }
    s.vmu_slot=7;s.vmu_selected=7;render(&v);
    assert(strstr(drawn,"VMU D2") && strstr(drawn,"SAVE_07") && strstr(drawn,"32768 bytes"));
    assert(strstr(drawn,"LEFT/RIGHT VMU") && strstr(drawn,"Backups go to SD"));
    reset(KUI_SHELL_HOME);v.app_status=NULL;
    render(&v);
    assert(strstr(drawn,"Inserted: Sword of the Berserk"));
    v.inserted_title=NULL;render(&v);
    assert(strstr(drawn,"Inserted: No disc detected"));
    v.inserted_title="A very long inserted disc title which exceeds the launcher subtitle width";
    render(&v);assert(strstr(drawn,"Inserted:") && strstr(drawn,"..."));
    for(unsigned i=0;i<6;i++) {
        s.home_selected=i;render(&v);
        assert(strstr(drawn,"6 applications") && strstr(drawn,"Memory Test"));
    }
}
int main(void) {
    launcher_and_confirmation(); operation_lock_and_stop(); settings_transaction();
    system_transaction_and_video(); app_navigation_and_vmu(); phase_eta();
    diagnostics(); destination_transaction(); keyboard_transaction(); advanced_navigation();
    rendering_semantics(); reference_and_destination_rendering(); new_pages_rendering();
    puts("PASS shell: Stop lock, system/ripper preferences, reversible video actions, VMU paging, phase ETA, destination keyboard, reference grades, safe rendering");
    return 0;
}
