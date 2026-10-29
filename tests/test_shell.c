/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell.h"
#include "kui/salvage_display.h"
#include "kui/version.h"
#include "kui/games_retail.h"
#include "kui/retail_image.h"
#include "kui/shell_font.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static struct kui_shell s;
static const struct kui_settings defaults={true,false,false,KUI_CAPTURE_FORMAT_GDI,0,false};
static void reset(enum kui_shell_page page) {
    kui_shell_init(&s,&defaults); s.page=page;
}
static enum kui_shell_action press(unsigned buttons, bool busy) {
    return kui_shell_input(&s,buttons,busy);
}
static enum kui_shell_action vmu_backup_selected(void) {
    assert(s.page==KUI_SHELL_VMU);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_ACTIONS);
    assert(s.vmu_action_selected==5);
    enum kui_shell_action action=press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_LIST && s.page==KUI_SHELL_VMU);
    return action;
}
static void open_extra_options(void) {
    assert(s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    s.games_launch_selected=KUI_SHELL_GAME_OPTION_ADVANCED;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
}
static void open_developer_tests(void) {
    open_extra_options();s.game_tools_selected=3;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_TESTS);
}
/* Background loading is a saved toggle; its reader choice is deeper
 * in Advanced. Neither gesture itself dispatches a game launch. */
static enum kui_shell_action launch_reader(unsigned button) {
    assert(s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    s.games_launch_selected=KUI_SHELL_GAME_OPTION_READER;
    unsigned reader=button==KUI_SHELL_A?KUI_RETAIL_READER_STANDARD:
        button==KUI_SHELL_X?KUI_RETAIL_READER_ASYNC:KUI_RETAIL_READER_ASYNC_EAGER;
    if((s.games_launch_reader!=KUI_RETAIL_READER_STANDARD)!=(reader!=KUI_RETAIL_READER_STANDARD))
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH);
    if(reader!=KUI_RETAIL_READER_STANDARD) {
        open_extra_options();s.game_tools_selected=1;
        if(s.games_launch_reader!=reader)
            assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH);
        assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    }
    assert(s.games_launch_reader==reader && !s.games_launch_cdda);
    enum kui_shell_action action=press(KUI_SHELL_START,false);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
    return action;
}
static void open_image_test(void) {
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    open_developer_tests();s.game_tests_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM);
    assert(press(KUI_SHELL_A|KUI_SHELL_START,false)==KUI_SHELL_NONE);
}
static unsigned home_index(enum kui_shell_page page) {
    for(unsigned i=0;i<KUI_SHELL_HOME_APPS;i++) if(kui_shell_home_pages[i]==page) return i;
    assert(!"app missing from Home");
    return 0;
}
static void open_destination(void) {
    assert(s.page==KUI_SHELL_RIPPER);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
    s.advanced_selected=4;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST);
    assert(s.page==KUI_SHELL_DESTINATION);
}
static void launcher_and_confirmation(void) {
    /* Games first, Disc Ripper and VMU tools next, Settings last; each
     * app appears once. */
    static const enum kui_shell_page order[KUI_SHELL_HOME_APPS]={KUI_SHELL_GAMES,KUI_SHELL_DISC_TOOLS,KUI_SHELL_VMU_TOOLS,
        KUI_SHELL_FILES,KUI_SHELL_MUSIC,KUI_SHELL_NETWORK,
        KUI_SHELL_DIAGNOSTICS,KUI_SHELL_SETTINGS};
    assert(!memcmp(order,kui_shell_home_pages,sizeof(order)));
    reset(KUI_SHELL_HOME);
    assert(s.home_selected==0);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE);
    assert(s.page==KUI_SHELL_HOME);
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.home_selected==1);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(s.page==KUI_SHELL_DISC_TOOLS && !s.confirm_new);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_RIPPER);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_R,false)==KUI_SHELL_NONE);
    assert(s.confirm_new);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE);
    assert(!s.confirm_new && s.page==KUI_SHELL_RIPPER);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NEW_DUMP && !s.confirm_new);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DISC_TOOLS);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME && s.home_selected==1);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.home_selected==0);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.home_selected==KUI_SHELL_HOME_APPS-1);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_LOAD_SYSTEM);
    assert(s.page==KUI_SHELL_SETTINGS);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_DISCARD_SYSTEM && s.page==KUI_SHELL_HOME);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.home_selected==KUI_SHELL_HOME_APPS-2);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DIAGNOSTICS);
}
static void operation_lock_and_stop(void) {
    const unsigned launch=KUI_SHELL_A|KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_R;
    for(unsigned page=0;page<=KUI_SHELL_GAMES_INFO;page++) {
        reset((enum kui_shell_page)page);
        assert(press(launch,true)==KUI_SHELL_NONE && s.page==page);
        assert(press(launch|KUI_SHELL_L|KUI_SHELL_B,true)==KUI_SHELL_STOP);
        assert(press(KUI_SHELL_L,true)==(page==KUI_SHELL_HOME||page==KUI_SHELL_RIPPER?
            KUI_SHELL_MUSIC_PREVIOUS:(page==KUI_SHELL_GAMES_PROBE_CONFIRM ||
            page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM || page==KUI_SHELL_GAMES_RETAIL_CONFIRM ||
            page==KUI_SHELL_GAMES_LAUNCH_OPTIONS ||
            page==KUI_SHELL_GAMES_VARIANTS || page==KUI_SHELL_FILES_CONFIRM)?KUI_SHELL_NONE:KUI_SHELL_MSTATS));
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
    press(KUI_SHELL_DOWN,false); assert(s.setting_selected==2);
    assert(s.draft.capture_format==KUI_CAPTURE_FORMAT_GDI);
    press(KUI_SHELL_LEFT|KUI_SHELL_RIGHT,false);assert(s.draft.capture_format==KUI_CAPTURE_FORMAT_GDI);
    press(KUI_SHELL_LEFT,false);assert(s.draft.capture_format==KUI_CAPTURE_FORMAT_COUNT-1 && kui_shell_settings_dirty(&s));
    press(KUI_SHELL_RIGHT,false);assert(s.draft.capture_format==KUI_CAPTURE_FORMAT_GDI && !kui_shell_settings_dirty(&s));
    for(unsigned i=1;i<=KUI_CAPTURE_FORMAT_COUNT;i++) {
        press(KUI_SHELL_RIGHT,false);assert((unsigned)s.draft.capture_format==i%KUI_CAPTURE_FORMAT_COUNT);
    }
    press(KUI_SHELL_RIGHT,false);changed=s.draft;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_SETTINGS && s.saved.capture_format==KUI_CAPTURE_FORMAT_GDI);
    kui_shell_set_preferences(&s,&changed);assert(s.saved.capture_format==KUI_CAPTURE_FORMAT_BIN_CUE && !kui_shell_settings_dirty(&s));
    press(KUI_SHELL_RIGHT,false);assert(kui_shell_settings_dirty(&s));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_DISCARD_SETTINGS && s.draft.capture_format==KUI_CAPTURE_FORMAT_BIN_CUE);
    s.draft.show_memory=!s.saved.show_memory;
    assert(!kui_shell_settings_dirty(&s));
    const struct kui_settings inconsistent={false,false,true,(enum kui_capture_format)KUI_CAPTURE_FORMAT_COUNT,0,false};
    kui_shell_set_preferences(&s,&inconsistent);
    assert(s.saved.end_readback && s.draft.end_readback);
    assert(s.saved.capture_format==KUI_CAPTURE_FORMAT_GDI && s.draft.capture_format==KUI_CAPTURE_FORMAT_GDI);
    /* Compressed formats require effective readback without overwriting the
     * stored GDI/CUE choice. Returning to GDI restores that exact preference. */
    reset(KUI_SHELL_RIPPER_SETTINGS);s.setting_selected=2;
    press(KUI_SHELL_RIGHT,false);press(KUI_SHELL_RIGHT,false);
    assert(s.draft.capture_format==KUI_CAPTURE_FORMAT_CSO && !s.draft.end_readback);
    for(unsigned format=KUI_CAPTURE_FORMAT_CSO;format<KUI_CAPTURE_FORMAT_COUNT;format++) {
        assert((unsigned)s.draft.capture_format==format);
        press(KUI_SHELL_UP,false);press(KUI_SHELL_RIGHT,false);press(KUI_SHELL_LEFT,false);
        assert(!s.draft.end_readback);
        press(KUI_SHELL_DOWN,false);press(KUI_SHELL_RIGHT,false);
    }
    assert(s.draft.capture_format==KUI_CAPTURE_FORMAT_GDI && !s.draft.end_readback);
    changed=s.draft;changed.capture_format=KUI_CAPTURE_FORMAT_CSO;
    kui_shell_set_preferences(&s,&changed);
    assert(s.saved.capture_format==KUI_CAPTURE_FORMAT_CSO && !s.saved.end_readback && !s.draft.end_readback);
    press(KUI_SHELL_LEFT,false);press(KUI_SHELL_LEFT,false);
    assert(s.draft.capture_format==KUI_CAPTURE_FORMAT_GDI && !s.draft.end_readback);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_DISCARD_SETTINGS && s.draft.capture_format==KUI_CAPTURE_FORMAT_CSO && !s.draft.end_readback);
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
    for(unsigned i=0;i<KUI_SHELL_HOME_APPS;i++) {
        enum kui_shell_page page=kui_shell_home_pages[i];
        reset(KUI_SHELL_HOME);s.home_selected=i;
        enum kui_shell_action expected=page==KUI_SHELL_VMU?KUI_SHELL_VMU_LIST:
            page==KUI_SHELL_VMU_TOOLS?KUI_SHELL_VMU_EMULATION_LOAD:
            page==KUI_SHELL_SETTINGS?KUI_SHELL_LOAD_SYSTEM:page==KUI_SHELL_MUSIC?KUI_SHELL_MUSIC_LIST:
            page==KUI_SHELL_GAMES?KUI_SHELL_GAMES_LIST:page==KUI_SHELL_FILES?KUI_SHELL_FILES_LIST:KUI_SHELL_NONE;
        assert(press(KUI_SHELL_A,false)==expected && s.page==page);
    }
    reset(KUI_SHELL_MEMORY);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_MEMORY_TEST);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_DIAGNOSTICS);
    reset(KUI_SHELL_NETWORK);assert(press(KUI_SHELL_A,false)==KUI_SHELL_NETWORK_TEST);
    reset(KUI_SHELL_VMU);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_VMU_LIST && s.vmu_slot==7);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_VMU_LIST && s.vmu_slot==0);
    struct kui_vmu_view view={.slot=0,.page=0,.count=8,.total=10,.present=true};
    strcpy(view.entries[0].name,"SONIC2__S01");
    kui_shell_set_vmu(&s,&view);assert(s.vmu.count==8);
    press(KUI_SHELL_UP,false);assert(s.vmu_selected==0);
    for(unsigned i=0;i<7;i++) press(KUI_SHELL_DOWN,false);
    assert(s.vmu_selected==7);
    assert(vmu_backup_selected()==KUI_SHELL_VMU_BACKUP);
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
static struct kui_vmu_view scroll_vmu_page(unsigned page,unsigned total) {
    struct kui_vmu_view view={.page=page,.count=KUI_VMU_ROWS,.total=total,.present=true};
    for(unsigned i=0;i<KUI_VMU_ROWS;i++) {
        unsigned index=page*KUI_VMU_ROWS+i;
        snprintf(view.entries[i].name,sizeof(view.entries[i].name),"RAW_%02u",index);
        snprintf(view.entries[i].description,sizeof(view.entries[i].description),"Game %02u save",index);
        strcpy(view.entries[i].app_id,"EXAMPLE_GAME");view.entries[i].bytes=(index+1)*512u;
    }
    return view;
}
static void vmu_continuous_scrolling(void) {
    /* Visit every save in both directions, including an incomplete final
     * page. Every dispatched row retains the on-card filename. */
    for(unsigned image=0;image<2;image++) {
        reset(image?KUI_SHELL_VMU_IMAGE:KUI_SHELL_VMU);
        strcpy(s.vmu_image_path,"0:/KUI/vmu/cards/v0001/card.vmd");
        struct kui_vmu_view view=scroll_vmu_page(0,19);
        if(image) kui_shell_set_vmu_image(&s,s.vmu_image_path,0,&view);
        else kui_shell_set_vmu(&s,&view);
        unsigned *page=image?&s.vmu_image_page:&s.vmu_page;
        unsigned *selected=image?&s.vmu_image_selected:&s.vmu_selected;
        struct kui_vmu_view *listing=image?&s.vmu_image:&s.vmu;
        bool *last=image?&s.vmu_image_select_last:&s.vmu_select_last;
        enum kui_shell_action action=image?KUI_SHELL_VMU_IMAGE_LIST:KUI_SHELL_VMU_LIST;
        assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && !*page && !*selected);
        for(unsigned index=0;index<19;index++) {
            char name[16];snprintf(name,sizeof(name),"RAW_%02u",index);
            assert(*page*KUI_VMU_ROWS+*selected==index);
            assert(!strcmp(listing->entries[*selected].name,name));
            assert((image?press(KUI_SHELL_X,false):vmu_backup_selected())==(image?KUI_SHELL_VMU_IMAGE_EXTRACT:KUI_SHELL_VMU_BACKUP));
            if(index==18) {
                assert(listing->count==3);
                assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && *selected==2);break;
            }
            bool boundary=index%KUI_VMU_ROWS==KUI_VMU_ROWS-1;
            assert(press(KUI_SHELL_DOWN|KUI_SHELL_X,false)==(boundary?action:
                image?KUI_SHELL_VMU_IMAGE_EXTRACT:KUI_SHELL_NONE));
            if(boundary) {
                assert(!listing->count && listing->total==19 && !*selected);
                assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE);
                assert(press(KUI_SHELL_UP,true)==KUI_SHELL_NONE);
                if(image) kui_shell_set_vmu_image(&s,s.vmu_image_path,view.page,&view);
                else kui_shell_set_vmu(&s,&view);
                assert(!listing->count); /* An old page cannot republish old rows. */
                view=scroll_vmu_page(*page,19);
                if(image) kui_shell_set_vmu_image(&s,s.vmu_image_path,*page,&view);
                else kui_shell_set_vmu(&s,&view);
            }
        }
        for(unsigned index=18;index;index--) {
            bool boundary=index%KUI_VMU_ROWS==0;
            assert(press(KUI_SHELL_UP,false)==(boundary?action:KUI_SHELL_NONE));
            if(boundary) {
                assert(*last && !listing->count && !*selected);
                if(image) kui_shell_set_vmu_image(&s,s.vmu_image_path,view.page,&view);
                else kui_shell_set_vmu(&s,&view);
                assert(*last && !listing->count);
                view=scroll_vmu_page(*page,19);
                if(image) kui_shell_set_vmu_image(&s,s.vmu_image_path,*page,&view);
                else kui_shell_set_vmu(&s,&view);
                assert(!*last && *selected==7);
            }
            assert(*page*KUI_VMU_ROWS+*selected==index-1);
        }
        assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && !*selected);
        /* START still cycles a page at a time and always lands on row one. */
        for(unsigned i=0;i<3;i++) {
            assert(press(KUI_SHELL_START,false)==action && *page==(i+1)%3 && !*selected && !*last);
            view=scroll_vmu_page(*page,19);
            if(image) kui_shell_set_vmu_image(&s,s.vmu_image_path,*page,&view);
            else kui_shell_set_vmu(&s,&view);
        }
        if(!image) {
            s.vmu_selected=7;
            assert(press(KUI_SHELL_DOWN,false)==action);
            view=scroll_vmu_page(1,19);kui_shell_set_vmu(&s,&view);
            assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE);
            assert(!strcmp(s.vmu.entries[s.vmu_selected].name,"RAW_08"));
            assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_DELETE_PREVIEW);
            s.vmu_action_selected=1;
            assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COPY_PREVIEW);
        }
    }
    puts("PASS continuous VMU scrolling: every save in both directions, endpoint clamp, final-page bounds, exact raw-name actions, stale results, busy guards and START shortcut");
}
static void vmu_catalog_scrolling(void) {
    for(unsigned collection=0;collection<2;collection++) {
        reset(collection?KUI_SHELL_VMU_COLLECTION:KUI_SHELL_VMU_RESTORE);
        s.vmu_collection_generation=1;
        struct kui_vmu_backup_view catalog={.page=0,.count=KUI_VMU_ROWS,.total=10};
        for(unsigned i=0;i<KUI_VMU_ROWS;i++) {
            snprintf(catalog.entries[i].name,sizeof(catalog.entries[i].name),"Card %u",i);
            snprintf(catalog.entries[i].path,sizeof(catalog.entries[i].path),
                "0:/KUI/vmu/cards/v%04u/card.vmd",i+1);
        }
        if(collection) kui_shell_set_vmu_collection(&s,1,&catalog);
        else kui_shell_set_vmu_backups(&s,&catalog);
        unsigned *page=collection?&s.vmu_collection_page:&s.backup_page;
        unsigned *selected=collection?&s.vmu_collection_selected:&s.backup_selected;
        bool *last=collection?&s.vmu_collection_select_last:&s.backup_select_last;
        struct kui_vmu_backup_view *listing=collection?&s.vmu_collection:&s.backups;
        enum kui_shell_action action=collection?KUI_SHELL_VMU_COLLECTION_LIST:KUI_SHELL_VMU_BACKUPS_LIST;
        assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && !*selected);
        *selected=7;
        assert(press(KUI_SHELL_DOWN,false)==action && *page==1 && !listing->count);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
        if(collection) kui_shell_set_vmu_collection(&s,1,&catalog);
        else kui_shell_set_vmu_backups(&s,&catalog);
        assert(!listing->count);
        catalog.page=1;
        if(collection) kui_shell_set_vmu_collection(&s,s.vmu_collection_generation,&catalog);
        else kui_shell_set_vmu_backups(&s,&catalog);
        assert(listing->count==2 && !*selected);
        assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && *selected==1);
        assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && *selected==1);
        assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && !*selected);
        assert(press(KUI_SHELL_UP,false)==action && !*page && *last && !listing->count);
        if(collection) kui_shell_set_vmu_collection(&s,s.vmu_collection_generation,&catalog);
        else kui_shell_set_vmu_backups(&s,&catalog);
        assert(*last && !listing->count);
        catalog.page=0;
        if(collection) kui_shell_set_vmu_collection(&s,s.vmu_collection_generation,&catalog);
        else kui_shell_set_vmu_backups(&s,&catalog);
        assert(listing->count==8 && *selected==7 && !*last);
        assert(press(collection?KUI_SHELL_Y:KUI_SHELL_X,false)==action && !*selected && !*last && !listing->count);
    }
    puts("PASS VMU catalogs scroll continuously with exact page/generation guards and no selectable rows while loading");
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
        v.phase=phase;assert(kui_shell_phase_eta(&v,&seconds)==(phase>=1&&phase<=4));
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
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_MEMORY);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DIAGNOSTICS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DISC_PROBE);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_STORAGE_PROBE);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_SAVE_LOG);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_TEST_HISTORY && s.page==KUI_SHELL_STORAGE_TESTS);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DIAGNOSTICS);
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
    press(KUI_SHELL_UP,false);assert(s.advanced_selected==KUI_SHELL_RIPPER_TOOL_COUNT-1u);
    s.advanced_selected=8;
    press(KUI_SHELL_UP,false);assert(s.advanced_selected==7);
    press(KUI_SHELL_UP,false);assert(s.advanced_selected==6);
    press(KUI_SHELL_UP,false);assert(s.advanced_selected==5);
    press(KUI_SHELL_UP,false);assert(s.advanced_selected==4);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST && s.page==KUI_SHELL_DESTINATION);
}
static void ripper_parity_controls(void) {
    const enum kui_shell_ripper_tool tools[]={KUI_SHELL_RIPPER_TOOL_REPAIR,KUI_SHELL_RIPPER_TOOL_FORENSIC};
    const enum kui_shell_action actions[]={KUI_SHELL_REPAIR_COPY,KUI_SHELL_FORENSIC};
    for(unsigned i=0;i<2u;i++) {
        reset(KUI_SHELL_ADVANCED);s.advanced_selected=tools[i];
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST && s.browse_for_scan && !s.browse_for_capture_job);
        strcpy(s.browse_path,"/Games/Saved dump");
        assert(press(KUI_SHELL_Y,false)==actions[i] && s.page==KUI_SHELL_CRC_SCAN);
        assert(press(KUI_SHELL_A,false)==actions[i]);
        assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    }
    reset(KUI_SHELL_ADVANCED);s.advanced_selected=KUI_SHELL_RIPPER_TOOL_REPAIR_IN_PLACE;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST && s.recovery_operation==3u);
    strcpy(s.browse_path,"/Games/Saved dump");
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.confirm_repair_in_place && s.page==KUI_SHELL_CRC_SCAN);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.confirm_repair_in_place);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_repair_in_place);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_REPAIR_IN_PLACE && !s.confirm_repair_in_place);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.salvage_passes==1);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.salvage_passes==0);
    assert(press(KUI_SHELL_RIGHT,true)==KUI_SHELL_NONE && s.salvage_passes==0);
    assert(!s.repair_saved_identity);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && !s.repair_saved_identity);
    s.recovery_operation=1u;
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.repair_saved_identity);
    assert(press(KUI_SHELL_X,true)==KUI_SHELL_NONE && s.repair_saved_identity);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && !s.repair_saved_identity);
    reset(KUI_SHELL_ADVANCED);s.advanced_selected=KUI_SHELL_RIPPER_TOOL_REPAIR_NEW_COPY;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST && s.recovery_operation==4u && s.browse_for_scan);
    strcpy(s.browse_path,"/Games/Changed source");
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_REPAIR_NEW_COPY && s.page==KUI_SHELL_CRC_SCAN);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_REPAIR_NEW_COPY);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.salvage_passes==1u);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.repair_saved_identity);
    /* Native dispatch consumes New once; the retained page resumes that job. */
    s.recovery_operation=1u;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_REPAIR_COPY);
    reset(KUI_SHELL_ADVANCED);strcpy(s.capture_job,"/Games/Local job");
    s.advanced_selected=KUI_SHELL_RIPPER_TOOL_NETWORK;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.capture_network && !s.capture_job[0]);
    assert(!strcmp(s.destination,"/Games"));
    press(KUI_SHELL_A,false);assert(!s.capture_network);
    s.advanced_selected=KUI_SHELL_RIPPER_TOOL_CD_SOURCE;
    press(KUI_SHELL_A,false);assert(s.capture_cd_toc);press(KUI_SHELL_A,false);assert(!s.capture_cd_toc);
    reset(KUI_SHELL_RIPPER_SETTINGS);s.setting_selected=3;
    press(KUI_SHELL_RIGHT,false);assert(s.draft.retry_limit==20u && kui_shell_settings_dirty(&s));
    press(KUI_SHELL_LEFT,false);assert(s.draft.retry_limit==10u);
    s.setting_selected=4;press(KUI_SHELL_RIGHT,false);assert(s.draft.strict_sectors);
}
static void open_capture_job(void) {
    s.page=KUI_SHELL_ADVANCED;s.advanced_selected=7;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST);
    assert(s.page==KUI_SHELL_DESTINATION && s.browse_for_capture_job && !s.browse_for_scan);
}
static void open_capture_label(void) {
    s.page=KUI_SHELL_ADVANCED;s.advanced_selected=8;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(s.page==KUI_SHELL_KEYBOARD && s.capture_label_keyboard);
    assert(!s.files_keyboard && !s.wifi_keyboard && !s.storage_test_keyboard);
}
static void ripper_saved_job_controls(void) {
    reset(KUI_SHELL_RIPPER);
    assert(KUI_SHELL_RIPPER_TOOL_JOB==7 && KUI_SHELL_RIPPER_TOOL_LABEL==8 && KUI_SHELL_RIPPER_TOOL_COUNT==15);
    assert(!s.capture_job[0] && !s.capture_folder_label[0] && !s.browse_for_capture_job && !s.capture_label_keyboard);
    open_capture_job();
    assert(!strcmp(s.browse_path,"/Games") && !s.capture_job[0]);
    struct kui_destination_page page={.count=3,.has_more=true};
    strcpy(page.entries[0].name,"Older dump");
    page.entries[0].job_selectable=true;
    strcpy(page.entries[1].name,"../bad");
    strcpy(page.entries[2].name,"Unavailable");page.entries[2].disabled=true;
    kui_shell_set_listing(&s,&page);
    assert(press(KUI_SHELL_A|KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION && !s.capture_job[0]);
    assert(press(KUI_SHELL_X|KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION && !s.capture_job[0]);
    assert(press(KUI_SHELL_Y,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION && !s.capture_job[0]);
    s.listing.entries[0].job_selectable=false;
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION && !s.capture_job[0]);
    s.listing.entries[0].job_selectable=true;
    s.browser_selected=1;
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.destination_notice[0] && !s.capture_job[0]);
    assert(s.page==KUI_SHELL_DESTINATION && !strcmp(s.browse_path,"/Games"));
    s.browser_selected=2;
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.destination_notice[0] && !s.capture_job[0]);
    s.browser_selected=0;
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE);
    assert(!strcmp(s.capture_job,"/Games/Older dump") && !strcmp(s.destination,"/Games"));
    assert(s.page==KUI_SHELL_ADVANCED && !s.browse_for_capture_job);
    /* Selection chooses a job without dispatching a read or changing New's
     * destination. All three existing resume/verify controls keep the path. */
    s.page=KUI_SHELL_RIPPER;
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_RESUME && !strcmp(s.capture_job,"/Games/Older dump"));
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VERIFY && !strcmp(s.capture_job,"/Games/Older dump"));
    s.page=KUI_SHELL_ADVANCED;s.advanced_selected=3;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_quick_resume);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_RESUME_QUICK && s.page==KUI_SHELL_RIPPER);
    assert(!strcmp(s.capture_job,"/Games/Older dump"));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_new);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NEW_DUMP && !strcmp(s.capture_job,"/Games/Older dump"));
    /* Busy input cannot replace or clear the job. B retains Stop priority. */
    assert(press(KUI_SHELL_LEFT,true)==KUI_SHELL_NONE && s.capture_job[0]);
    assert(press(KUI_SHELL_START,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_RIPPER);
    assert(press(KUI_SHELL_B|KUI_SHELL_LEFT,true)==KUI_SHELL_STOP && s.capture_job[0]);
    assert(press(KUI_SHELL_LEFT|KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.capture_job[0]);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && !s.capture_job[0]);
    assert(!strcmp(s.destination,"/Games"));
    strcpy(s.capture_job,"/Games/Prior");
    open_capture_job();kui_shell_set_listing(&s,&page);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_DEST_LIST && s.browser_page==1 && !s.listing.count);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.browser_page==1);
    kui_shell_set_listing(&s,&page);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_DEST_LIST && !s.browser_page);
    kui_shell_set_listing(&s,&page);
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.browser_selected==1);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && !s.browser_selected);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST && !strcmp(s.browse_path,"/Games/Older dump"));
    assert(press(KUI_SHELL_B|KUI_SHELL_Y,false)==KUI_SHELL_DEST_LIST);
    assert(!strcmp(s.browse_path,"/Games") && !strcmp(s.capture_job,"/Games/Prior"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_DEST_LIST && !strcmp(s.browse_path,"/"));
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.destination_notice[0]);
    assert(!strcmp(s.capture_job,"/Games/Prior") && s.page==KUI_SHELL_DESTINATION);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED && !s.browse_for_capture_job);
    open_capture_job();strcpy(s.browse_path,"/Games/Current");
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED);
    assert(!strcmp(s.capture_job,"/Games/Current") && !s.browse_for_capture_job);
    open_capture_job();strcpy(s.browse_path,"/Games/Discarded");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED);
    assert(!strcmp(s.capture_job,"/Games/Current") && !s.browse_for_capture_job);
    /* A parent must fit the existing 128-byte browser. Y can select a child
     * whose full 256-byte job path cannot be opened in that browser. */
    open_capture_job();
    s.browse_path[0]='/';memset(s.browse_path+1,'a',110);s.browse_path[111]=0;
    memset(&page,0,sizeof(page));page.count=1;
    memset(page.entries[0].name,'b',100);page.entries[0].name[100]=0;
    page.entries[0].disabled=true;page.entries[0].job_selectable=true;
    kui_shell_set_listing(&s,&page);
    char expected[KUI_DEST_JOB_CAP];
    assert(strlen(s.browse_path)+1+strlen(page.entries[0].name)<sizeof(expected));
    strcpy(expected,s.browse_path);strcat(expected,"/");strcat(expected,page.entries[0].name);
    assert(strlen(expected)>=KUI_DEST_ROOT_CAP);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION && s.destination_notice[0]);
    assert(!strcmp(s.capture_job,"/Games/Current") && strlen(s.browse_path)==111);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED);
    assert(!strcmp(s.capture_job,expected) && !strcmp(s.destination,"/Games"));
    /* Current-folder selection rejects invalid paths as well as the card
     * root; highlighted names never accept reserved DOS components. */
    open_capture_job();strcpy(s.browse_path,"/Games/../Bad");
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION && s.destination_notice[0]);
    assert(!strcmp(s.capture_job,expected));
    strcpy(s.browse_path,"/Games");strcpy(page.entries[0].name,"CON");
    page.entries[0].disabled=false;
    kui_shell_set_listing(&s,&page);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION && s.destination_notice[0]);
    memset(page.entries[0].name,'x',sizeof(page.entries[0].name));page.entries[0].job_selectable=true;
    kui_shell_set_listing(&s,&page);
    assert(s.listing.entries[0].disabled && !s.listing.entries[0].job_selectable);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION && !strcmp(s.capture_job,expected));
    memset(&page,0,sizeof(page));kui_shell_set_listing(&s,&page);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION && !strcmp(s.capture_job,expected));
    press(KUI_SHELL_START,false);
    /* Changing browser purposes resets job mode. Existing output-folder
     * saves and Advanced CRC's current-folder action retain their semantics. */
    s.advanced_selected=4;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST && !s.browse_for_capture_job && !s.browse_for_scan);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_DEST_SAVE && !strcmp(s.capture_job,expected));
    press(KUI_SHELL_START,false);s.page=KUI_SHELL_ADVANCED;s.advanced_selected=5;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST && s.browse_for_scan && !s.browse_for_capture_job);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DESTINATION);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_ADVANCED_CRC && s.page==KUI_SHELL_CRC_SCAN);
    assert(!strcmp(s.capture_job,expected) && !strcmp(s.destination,"/Games"));
    puts("PASS shell saved dump picker: exact paths, long children, paging, cancel, guards, separate browser purposes");
}
static void ripper_folder_label_controls(void) {
    reset(KUI_SHELL_RIPPER);strcpy(s.capture_job,"/Games/Existing");
    s.files_keyboard=s.wifi_keyboard=s.storage_test_keyboard=true;
    open_capture_label();assert(!s.keyboard[0]);
    strcpy(s.keyboard,"  ..My:\tGame*/ Name...  ");
    assert(press(KUI_SHELL_A|KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && !s.capture_folder_label[0]);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED && !s.capture_label_keyboard);
    assert(!strcmp(s.capture_folder_label,"My_ Game__ Name") && !strcmp(s.capture_job,"/Games/Existing"));
    assert(!strcmp(s.destination,"/Games"));
    open_capture_label();assert(!strcmp(s.keyboard,"My_ Game__ Name"));
    strcpy(s.keyboard,"Cancelled");
    assert(press(KUI_SHELL_B|KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED && !s.capture_label_keyboard);
    assert(!strcmp(s.capture_folder_label,"My_ Game__ Name"));
    open_capture_label();strcpy(s.keyboard,"CON");s.keyboard_selected=42;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED);
    assert(!strcmp(s.capture_folder_label,"DreamcastDisc") && !s.capture_label_keyboard);
    open_capture_label();strcpy(s.keyboard,"Caf\xc3\xa9");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && !strcmp(s.capture_folder_label,"Caf\xc3\xa9"));
    open_capture_label();memset(s.keyboard,'n',KUI_DEST_TITLE_CAP-2);s.keyboard[KUI_DEST_TITLE_CAP-2]=0;
    s.keyboard_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && strlen(s.keyboard)==KUI_DEST_TITLE_CAP-1);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && strlen(s.keyboard)==KUI_DEST_TITLE_CAP-1 && s.destination_notice[0]);
    assert(press(KUI_SHELL_START,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && s.capture_label_keyboard);
    assert(!strcmp(s.capture_folder_label,"Caf\xc3\xa9"));
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && strlen(s.capture_folder_label)==KUI_DEST_TITLE_CAP-1);
    open_capture_label();s.keyboard[0]=0;s.keyboard_selected=42;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED && !s.capture_folder_label[0]);
    assert(!s.capture_label_keyboard && !strcmp(s.capture_job,"/Games/Existing"));
    open_capture_label();strcpy(s.keyboard," \t  ");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED && !s.capture_folder_label[0]);
    /* Accepting the next normal destination keyboard still returns a save
     * request rather than accidentally rewriting the capture-only label. */
    s.page=KUI_SHELL_RIPPER;open_destination();
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && !s.capture_label_keyboard);
    strcpy(s.keyboard,"/Other");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_DEST_SAVE && !s.capture_folder_label[0]);
    assert(!strcmp(s.capture_job,"/Games/Existing"));
    /* This is a session-only choice: fresh shell state restores Auto and
     * Automatic latest without touching the separate stored destination. */
    reset(KUI_SHELL_RIPPER);assert(!s.capture_job[0] && !s.capture_folder_label[0]);
    puts("PASS shell new dump folder label: sanitized names, UTF-8, Auto, cancel, cap, keyboard purpose, session reset");
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
    reset(KUI_SHELL_HOME);s.home_selected=home_index(KUI_SHELL_GAMES);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST);
    assert(s.page==KUI_SHELL_GAMES && !strcmp(s.games_path,"/Games"));
    assert(!s.games_refresh && s.games_loading);
    struct kui_games_page page={.count=2,.has_more=true};
    strcpy(page.root,"/Other");strcpy(page.entries[0].name,"Fighting");
    strcpy(page.entries[0].path,"/Games/Fighting");page.entries[0].directory=true;
    strcpy(page.entries[1].name,"Dead or Alive 2");
    strcpy(page.entries[1].path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
    kui_shell_set_games_listing(&s,&page);assert(!s.games_listing.count);
    strcpy(page.root,"/Games");kui_shell_set_games_listing(&s,&page);
    assert(s.games_listing.count==2 && !s.games_listing.entries[0].disabled);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST);
    assert(!strcmp(s.games_path,"/Games/Fighting") && s.games_listing.count==2 && s.games_loading);
    assert(!s.games_refresh);
    kui_shell_set_games_listing(&s,&page);assert(s.games_listing.count==2 && s.games_loading);
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
    assert(press(KUI_SHELL_Y,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    open_image_test();
    assert(press(KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_L|KUI_SHELL_R|KUI_SHELL_START,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_TESTS);
    assert(kui_shell_games_image_ready(&s));
    s.game_tests_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM);
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
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_TESTS);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_DETAIL);
    assert(press(KUI_SHELL_A|KUI_SHELL_X,false)==KUI_SHELL_GAMES_INSPECT && !s.games_detail.valid);
    kui_shell_set_games_detail(&s,&detail);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_GAMES_INSPECT && !s.games_detail.valid);
    strcpy(detail.message,"Track file missing");detail.valid=false;
    kui_shell_set_games_detail(&s,&detail);assert(!strcmp(s.games_detail.message,"Track file missing"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES);
    assert(s.games_selected==1 && s.games_listing.count==2);
    detail.valid=true;kui_shell_set_games_detail(&s,&detail);assert(!s.games_detail.valid);
    char shown_message[sizeof(s.games_listing.message)];
    strcpy(shown_message,s.games_listing.message);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_GAMES_LIST && s.games_page==1);
    assert(!s.games_refresh);
    assert(s.games_listing.count==2 && !s.games_selected && s.games_loading);
    /* The page being left stays drawn, message included, until rows arrive. */
    assert(s.games_turning && !s.games_shown_page && s.games_shown_selected==1);
    assert(!strcmp(s.games_listing.message,shown_message));
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_GAMES_LIST && !s.games_page);
    assert(s.games_turning && !s.games_shown_page && s.games_shown_selected==1);
    assert(!s.games_refresh);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_GAMES_LIST);
    assert(s.games_refresh);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_ADVANCED);
    kui_shell_set_games_listing(&s,&page);assert(s.games_listing.count==2 && s.games_loading);
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
    press(KUI_SHELL_UP,false);assert(s.games_advanced_selected==3);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_ADVANCED);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_TESTS);
    assert(s.game_tests_selected==1 && !s.games_tests_from_options);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_PROBE_CONFIRM);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_L|KUI_SHELL_R|KUI_SHELL_START,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_TESTS);
    assert(s.games_advanced_selected==3);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_PROBE_CONFIRM);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_PROBE);
    assert(s.page==KUI_SHELL_GAMES_PROBE_CONFIRM);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_TESTS);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_ADVANCED);
    press(KUI_SHELL_DOWN,false);assert(s.games_advanced_selected==0);
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
static struct kui_games_page paired_games(void) {
    struct kui_games_page page={.count=2,.total=2,.view=KUI_GAMES_VIEW_LIST};
    strcpy(page.root,"/Games");
    strcpy(page.entries[0].name,"Other");strcpy(page.entries[0].path,"/Games/Other/disc.gdi");
    strcpy(page.entries[1].name,"DOA2");strcpy(page.entries[1].title,"DEAD OR ALIVE 2");
    strcpy(page.entries[1].path,"/Games/DOA2/DOA2.gdi");
    strcpy(page.entries[1].variant_2048_path,"/Games/DOA2-2048/DOA2.gdi");
    return page;
}
static void open_paired_games(void) {
    reset(KUI_SHELL_GAMES);
    struct kui_games_page page=paired_games();kui_shell_set_games_listing(&s,&page);
    press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_VARIANTS);
    assert(s.games_selected==1 && !s.games_variant_selected && !s.games_selected_path[0]);
    assert(!s.games_detail.valid && !strcmp(kui_shell_games_variant_label(&s),"Original"));
}
static void inspect_paired_choice(unsigned choice,bool ce) {
    if(s.games_variant_selected!=choice) press(KUI_SHELL_DOWN,false);
    const char *path=choice?s.games_2048_path:s.games_original_path;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_INSPECT && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(!strcmp(s.games_selected_path,path) && !s.games_detail.valid);
    struct kui_games_detail detail={.valid=true,.native_gd=!ce,.windows_ce=ce,.tracks=3,
        .boot_bytes=ce?1253376:123456,.boot_lba=45166};
    strcpy(detail.path,path);strcpy(detail.title,ce?"ARMADA":"DEAD OR ALIVE 2");
    strcpy(detail.boot_file,ce?"0WINCEOS.BIN":"1ST_READ.BIN");
    kui_shell_set_games_detail(&s,&detail);assert(kui_shell_games_image_ready(&s));
}
static void games_variant_controls(void) {
    const char *paths[]={"/Games/DOA2/DOA2.gdi","/Games/DOA2-2048/DOA2.gdi"};
    const char *labels[]={"Original","2048-byte copy"};
    const unsigned buttons[]={KUI_SHELL_A,KUI_SHELL_X,KUI_SHELL_Y};
    const unsigned readers[]={KUI_RETAIL_READER_STANDARD,KUI_RETAIL_READER_ASYNC,KUI_RETAIL_READER_ASYNC_EAGER};
    for(unsigned choice=0;choice<2;choice++) {
        open_paired_games();
        assert(press(KUI_SHELL_L|KUI_SHELL_START|KUI_SHELL_X|KUI_SHELL_Y,false)==KUI_SHELL_NONE);
        assert(s.page==KUI_SHELL_GAMES_VARIANTS && !s.games_selected_path[0]);
        assert(press(KUI_SHELL_A|KUI_SHELL_DOWN,true)==KUI_SHELL_NONE && !s.games_variant_selected);
        inspect_paired_choice(choice,false);
        assert(!strcmp(s.games_selected_path,paths[choice]));
        assert(!strcmp(kui_shell_games_variant_label(&s),labels[choice]));
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
        for(unsigned reader=0;reader<3;reader++) {
            assert(launch_reader(buttons[reader])==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==readers[reader]);
            assert(!strcmp(s.games_selected_path,paths[choice]));
        }
        assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
        assert(kui_shell_games_image_ready(&s) && !strcmp(kui_shell_games_variant_label(&s),labels[choice]));
        open_image_test();
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_IMAGE_PROBE && !strcmp(s.games_selected_path,paths[choice]));
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_TESTS);
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_DETAIL);
        struct kui_games_detail old=s.games_detail;
        assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_VARIANTS);
        assert(s.games_variant_selected==choice && !s.games_selected_path[0] && !s.games_detail.valid);
        kui_shell_set_games_detail(&s,&old);assert(!s.games_detail.valid);
        /* Changing versions must inspect anew, rejecting the old version's result. */
        press(KUI_SHELL_DOWN,false);
        assert(s.games_variant_selected==1-choice && !s.games_selected_path[0]);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_INSPECT && !s.games_detail.valid);
        assert(!strcmp(s.games_selected_path,paths[1-choice]));
        kui_shell_set_games_detail(&s,&old);assert(!s.games_detail.valid);
        assert(!kui_shell_games_image_ready(&s) && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_VARIANTS);
        assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES);
        assert(s.games_selected==1 && s.games_listing.count==2 && !s.games_selected_path[0]);
        assert(!s.games_original_path[0] && !s.games_2048_path[0] && !s.games_detail.path[0]);
        assert(!kui_shell_games_variant_label(&s));
    }
    /* Both CE versions default to the working background reader. */
    for(unsigned choice=0;choice<2;choice++) {
        open_paired_games();inspect_paired_choice(choice,true);
        s.games_detail.audio=(struct kui_game_audio_info){KUI_GAME_AUDIO_GD_HIGH_DENSITY,2,1,1};
        assert(kui_shell_games_ce_probe_ready(&s) && kui_shell_games_cdda_ready(&s));
        press(KUI_SHELL_A,false);assert(s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
        s.games_retail_reader=0;
        assert(press(KUI_SHELL_X|KUI_SHELL_Y,false)==KUI_SHELL_NONE && !s.games_retail_reader);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL &&
            s.games_retail_reader==(KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC));
        assert(!strcmp(s.games_selected_path,paths[choice]));
        press(KUI_SHELL_Y,false);s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_cdda);
        assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL &&
            s.games_retail_reader==(KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC|KUI_GAMES_RETAIL_CE_EXCERPT));
        assert(!strcmp(s.games_selected_path,paths[choice]));
    }

    const char *bad[]={"/Other/DOA2.gdi","/Games2/DOA2.gdi","/Games/../DOA2.gdi",
        "/Games//DOA2.gdi","/Games/DOA2/DOA2.gdi"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0])+1;i++) {
        reset(KUI_SHELL_GAMES);struct kui_games_page page=paired_games();
        if(i<sizeof(bad)/sizeof(bad[0])) strcpy(page.entries[1].variant_2048_path,bad[i]);
        else memset(page.entries[1].variant_2048_path,'x',sizeof(page.entries[1].variant_2048_path));
        kui_shell_set_games_listing(&s,&page);
        assert(!s.games_listing.entries[1].disabled && !s.games_listing.entries[1].variant_2048_path[0]);
        press(KUI_SHELL_DOWN,false);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_INSPECT && s.page==KUI_SHELL_GAMES_DETAIL);
        assert(!strcmp(s.games_selected_path,paths[0]) && !kui_shell_games_variant_label(&s));
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES);
    }
    /* Recheck both paths before leaving a selector or confirming a launch. */
    open_paired_games();strcpy(s.games_2048_path,"/Elsewhere/disc.gdi");
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES && s.games_listing.message[0]);
    assert(!s.games_original_path[0] && !s.games_selected_path[0]);
    open_paired_games();inspect_paired_choice(1,false);press(KUI_SHELL_A,false);
    strcpy(s.games_original_path,"/Games2/DOA2.gdi");
    assert(!kui_shell_games_image_ready(&s) && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    press(KUI_SHELL_B,false);press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES);
}
static uint16_t pixels[640*480+2], prepared[640*480+2];
static char drawn[8192];
static size_t drawn_size;
static uint16_t badge_color;
static bool header_link_seen;
static uint16_t header_link_color;
static uint16_t image_test_row_color;
static uint16_t option_row_colors[KUI_SHELL_GAME_OPTION_COUNT];
static const char *const ripper_tool_names[]={"Verify saved files","Resume interrupted dump","Capture settings",
    "Quick resume (sizes only)","Destination folder","Advanced CRC scan","Damaged disc salvage",
    "Saved job for Resume/Verify","New dump folder label","Repair / resume a separate copy",
    "Suspect-sector reread evidence","Rip destination: local / PC","Single-session CD source",
    "Repair / resume in place","Start a new repair copy"};
static uint16_t ripper_tool_colors[KUI_SHELL_RIPPER_TOOL_COUNT];
static void observe(void *ctx,unsigned x,unsigned y,uint16_t color,const char *value,bool large) {
    (void)ctx;
    if(!strncmp(value,"STREAM CRC:",11)) badge_color=color;
    if(x==48 && y==194 && !strcmp(value,"Image read test")) image_test_row_color=color;
    if(s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS && x==48) {
        static const char *const names[]={"Background loading","CD music","Advanced","ZSO codec"};
        for(unsigned i=0;i<KUI_SHELL_GAME_OPTION_COUNT;i++)
            if(!strcmp(value,names[i])) option_row_colors[i]=color;
    }
    if(s.page==KUI_SHELL_ADVANCED && x==48)
        for(unsigned i=0;i<KUI_SHELL_RIPPER_TOOL_COUNT;i++)
            if(!strcmp(value,ripper_tool_names[i])) ripper_tool_colors[i]=color;
    if(x==32 && y==78) {
        assert(!large && !strcmp(value,"github.com/TPMJB/K-UI-NeXT"));
        assert(kui_shell_font_width(value,false)<=396u-x);
        header_link_seen=true;header_link_color=color;
    }
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
    drawn_size=0; drawn[0]=0; badge_color=0;header_link_seen=false;image_test_row_color=0;
    memset(option_row_colors,0,sizeof(option_row_colors));
    memset(ripper_tool_colors,0,sizeof(ripper_tool_colors));
    kui_shell_draw(pixels+1,&s,v,observe,NULL);
    assert(header_link_seen);
    unsigned link_ink=0;
    for(unsigned y=78;y<78+KUI_SHELL_FONT_SMALL_HEIGHT;y++)
        for(unsigned x=32;x<32+kui_shell_font_width("github.com/TPMJB/K-UI-NeXT",false);x++)
            link_ink+=pixels[1+y*640+x]==header_link_color;
    assert(link_ink>0); /* The unclipped link stays below the brand on every page. */
    assert(pixels[0]==0x1234 && pixels[640*480+1]==0xabcd);
    prepared[0]=0x1234; prepared[640*480+1]=0xabcd;
    for(unsigned i=1;i<=640*480;i++) prepared[i]=0x0864;
    kui_shell_draw_content(prepared+1,&s,v,NULL,NULL);
    assert(memcmp(pixels,prepared,sizeof(pixels))==0);

}
static void vmu_scroll_rendering(void) {
    struct kui_shell_view view={0};
    reset(KUI_SHELL_VMU);
    struct kui_vmu_view listing=scroll_vmu_page(0,19);
    listing.free_blocks=81;
    listing.entries[1].description[0]=0;
    listing.entries[2].description[0]=listing.entries[2].app_id[0]=0;
    strcpy(listing.entries[3].description,"12345678901234567890123456789012");
    kui_shell_set_vmu(&s,&listing);render(&view);
    assert(strstr(drawn,"Saves 1-8 of 19") && strstr(drawn,"81 free blocks"));
    assert(strstr(drawn,"Game 00 save") && strstr(drawn,"Game 07 save"));
    assert(strstr(drawn,"EXAMPLE_GAME") && strstr(drawn,"RAW_02"));
    assert(strstr(drawn,"12345678901234567890123456789012"));
    assert(strstr(drawn,"File: RAW_00") && strstr(drawn,"512 bytes") && strstr(drawn,"8 blocks"));
    assert(!strstr(drawn,"RAW_01") && !strstr(drawn,"RAW_07"));
    assert(strstr(drawn,"UP/DOWN Scroll") && strstr(drawn,"START Page") && strstr(drawn,"B Tools"));
    assert(pixels[1+196*640+593]==0x675e && pixels[1+363*640+593]==0x318a);
    s.vmu_selected=7;render(&view);
    assert(pixels[1+196*640+593]==0x318a && pixels[1+234*640+593]==0x675e);
    listing=scroll_vmu_page(2,19);s.vmu_page=2;s.vmu_selected=2;
    kui_shell_set_vmu(&s,&listing);render(&view);
    assert(strstr(drawn,"Saves 17-19 of 19") && strstr(drawn,"File: RAW_18"));
    assert(!strstr(drawn,"Game 19 save"));
    assert(pixels[1+196*640+593]==0x318a && pixels[1+363*640+593]==0x675e);
    s.page=KUI_SHELL_VMU_IMAGE;strcpy(s.vmu_image_path,"0:/KUI/vmu/cards/v0001/card.vmd");
    s.vmu_image_page=2;s.vmu_image_selected=2;
    kui_shell_set_vmu_image(&s,s.vmu_image_path,2,&listing);render(&view);
    assert(strstr(drawn,"Saves 17-19 of 19") && strstr(drawn,"Game 18 save"));
    assert(strstr(drawn,"File: RAW_18") && strstr(drawn,"verified SD backups only"));
    assert(pixels[1+363*640+593]==0x675e);
    strcpy(s.vmu_image.status.message,"Source read failed.");s.vmu_image.status.complete=true;
    render(&view);assert(strstr(drawn,"Source read failed.") && strstr(drawn,"File: RAW_18"));
    listing=scroll_vmu_page(0,1);s.page=KUI_SHELL_VMU;s.vmu_page=0;
    memset(listing.entries[0].description,'d',sizeof(listing.entries[0].description));
    memset(listing.entries[0].app_id,'a',sizeof(listing.entries[0].app_id));
    kui_shell_set_vmu(&s,&listing);
    assert(!s.vmu.entries[0].description[32] && !s.vmu.entries[0].app_id[16]);
    render(&view);
    assert(pixels[1+196*640+593]==0x675e && pixels[1+363*640+593]==0x675e);
    puts("PASS VMU list rendering: descriptions, app-id/filename fallback, block counts, selected exact filename, ranges, scrollbar position/length, errors and safe text bounds");
}
static void rendering_semantics(void) {
    const char *logs[]={"A very long diagnostic line deliberately exceeding safe frame margins 0123456789012345678901234567890"};
    struct kui_shell_view v={.build="0123456789abcdef",.log_lines=logs,.log_count=1,
        .total_log_lines=1,.done=UINT64_MAX-1,.total=UINT64_MAX};
    for(unsigned page=0;page<=KUI_SHELL_GAMES_INFO;page++) {
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
static void ripper_saved_job_rendering(void) {
    reset(KUI_SHELL_RIPPER);struct kui_shell_view v={0};render(&v);
    assert(strstr(drawn,"Resume/Verify: Automatic latest matching job."));
    assert(strstr(drawn,"New folder label: Auto (disc title)"));
    strcpy(s.capture_job,"/Games/Older dump");strcpy(s.capture_folder_label,"Selected label");
    render(&v);
    assert(strstr(drawn,"Resume/Verify job:") && strstr(drawn,"/Games/Older dump"));
    assert(strstr(drawn,"X Resume selected") && strstr(drawn,"Y Verify selected"));
    assert(strstr(drawn,"LEFT restores Automatic latest") && strstr(drawn,"LEFT Automatic job"));
    assert(strstr(drawn,"New folder label: Selected label"));
    s.confirm_new=true;render(&v);
    assert(strstr(drawn,"START A NEW DUMP?") && strstr(drawn,"New folder label: Selected label"));
    s.confirm_new=false;s.confirm_quick_resume=true;s.page=KUI_SHELL_ADVANCED;render(&v);
    assert(strstr(drawn,"USE QUICK RESUME?") && strstr(drawn,"/Games/Older dump"));
    s.confirm_quick_resume=false;
    for(unsigned row=0;row<KUI_SHELL_RIPPER_TOOL_COUNT;row++) {
        s.advanced_selected=row;render(&v);const char *at=drawn;
        unsigned visible=0;
        for(unsigned i=0;i<KUI_SHELL_RIPPER_TOOL_COUNT;i++) {
            const char *found=strstr(at,ripper_tool_names[i]);
            if(!found) {assert(!ripper_tool_colors[i]);continue;}
            at=found+strlen(ripper_tool_names[i]);++visible;
            assert(ripper_tool_colors[i]==(i==row?0xf7bf:0xadd9));
        }
        assert(visible==8u && ripper_tool_colors[row]==0xf7bf);
        if(row==KUI_SHELL_RIPPER_TOOL_JOB) assert(strstr(drawn,"/Games/Older dump"));
        if(row==KUI_SHELL_RIPPER_TOOL_LABEL) assert(strstr(drawn,"Selected label"));
    }
    s.advanced_selected=KUI_SHELL_RIPPER_TOOL_JOB;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_DEST_LIST);render(&v);
    assert(strstr(drawn,"Choose saved job") && strstr(drawn,"Y Select highlighted") && strstr(drawn,"X Select this folder"));
    assert(strstr(drawn,"Resume/Verify only") && !strstr(drawn,"selected destination is saved"));
    v.busy=true;render(&v);assert(strstr(drawn,"Loading folders"));v.busy=false;
    s.listing.count=1;s.listing.entries[0].job_selectable=true;s.listing.entries[0].disabled=true;
    memset(s.listing.entries[0].name,'a',100);strcpy(s.listing.entries[0].name+100," (9)");render(&v);
    assert(strstr(drawn,"...") && strstr(drawn,"(9)")); /* Collision ordinal stays visible on long selected names. */
    press(KUI_SHELL_START,false);open_capture_label();render(&v);
    assert(strstr(drawn,"New dump folder label") && strstr(drawn,"Empty uses Auto (disc title)"));
    assert(strstr(drawn,"New folders only") && !strstr(drawn,"Type destination") && !strstr(drawn,"saves your choice"));
    memset(s.keyboard,'m',KUI_DEST_TITLE_CAP-1);s.keyboard[KUI_DEST_TITLE_CAP-1]=0;s.keyboard_selected=42;
    render(&v);assert(strstr(drawn,"...") && strstr(drawn,"SPACE") && strstr(drawn,"BACK") && strstr(drawn,"DONE"));
    puts("PASS shell saved dump rendering: nine Advanced rows, selected job, Auto, label, confirmations, picker and keyboard bounds");
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
    assert(strstr(drawn,"Network") && strstr(drawn,"Inspect adapter") && strstr(drawn,"Y FTP server"));
    assert(strstr(drawn,"START Wi-Fi") && strstr(drawn,"W5500 or the Wi-Fi board on the SCI port"));
    reset(KUI_SHELL_VMU);s.vmu.present=true;s.vmu.total=8;s.vmu.count=8;
    for(unsigned i=0;i<8;i++) {
        snprintf(s.vmu.entries[i].name,16,"SAVE_%02u",i);s.vmu.entries[i].bytes=32768;
    }
    s.vmu_slot=7;s.vmu_selected=7;render(&v);
    assert(strstr(drawn,"VMU D2") && strstr(drawn,"SAVE_07") && strstr(drawn,"32768 bytes"));
    assert(strstr(drawn,"LEFT/RIGHT VMU") && strstr(drawn,"X Actions"));
    reset(KUI_SHELL_HOME);v.app_status=NULL;
    render(&v);
    assert(strstr(drawn,"SD game library") && !strstr(drawn,"Inserted:"));
    s.home_selected=home_index(KUI_SHELL_DISC_TOOLS);render(&v);
    assert(strstr(drawn,"Inserted: Sword of the Berserk"));
    v.inserted_title=NULL;render(&v);
    assert(strstr(drawn,"Inserted: No disc detected"));
    v.inserted_title="A very long inserted disc title which exceeds the launcher subtitle width";
    render(&v);assert(strstr(drawn,"Inserted:") && strstr(drawn,"..."));
    static const char *const names[]={"Games","Disc tools","VMU tools","File Manager","Music Player",
        "Network","Diagnostics","Settings"};
    for(unsigned i=0;i<KUI_SHELL_HOME_APPS;i++) {
        s.home_selected=i;render(&v);
        assert(strstr(drawn,"RAM 1 / 16384 KiB") && strstr(drawn,"Diagnostics"));
        /* Rows are drawn top to bottom in Home order. */
        const char *at=drawn;
        for(unsigned j=0;j<KUI_SHELL_HOME_APPS;j++) {at=strstr(at,names[j]);assert(at);at+=strlen(names[j]);}
        struct kui_shell_view disc={.inserted_title="MDK2"};
        render(&disc);
        enum kui_shell_page page=kui_shell_home_pages[i];
        assert(!strstr(drawn,"Inserted: MDK2")==(page!=KUI_SHELL_DISC_TOOLS));
    }
    s.system_saved.show_memory=false;render(&v);
    assert(strstr(drawn,"8 applications") && !strstr(drawn,"RAM 1 /"));
}
static void music_and_boot_rendering(void) {
    struct kui_shell_view v={.music_enabled=true,.music_playing=true,.music_volume=75,
        .music_title="Neon Circuit",.inserted_title="MDK2"};
    reset(KUI_SHELL_HOME);render(&v);
    assert(strstr(drawn,"Y Music 75%") && strstr(drawn,"Neon Circuit"));
    assert(strstr(drawn,"Y Volume") && strstr(drawn,"L/R Songs") && strstr(drawn,"Disc tools"));
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
    assert(strstr(drawn,"Quick resume (sizes only)") && strstr(drawn,"Direct 2048 and repaired jobs retain full verification."));
    s.advanced_selected=4;render(&v);
    assert(strstr(drawn,"Destination folder") && strstr(drawn,"enter a destination path"));
    s.advanced_selected=3;
    s.confirm_quick_resume=true;render(&v);
    assert(strstr(drawn,"USE QUICK RESUME?"));
    assert(strstr(drawn,"Raw jobs can skip saved-prefix hash checks."));
    assert(strstr(drawn,"Direct 2048 and repaired jobs verify saved bytes."));
    assert(strstr(drawn,"Use Verify to detect same-size raw-job damage."));
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
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_ACTIONS);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_LIST && s.page==KUI_SHELL_VMU);
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
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==8);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==7);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==8);
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
    press(KUI_SHELL_UP,false);assert(s.tools_selected==5);
    press(KUI_SHELL_UP,false);press(KUI_SHELL_UP,false);assert(s.tools_selected==3);
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
    assert(s.salvage_passes==0);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_salvage);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_salvage);
    s.salvage_selected=3;press(KUI_SHELL_RIGHT,false);assert(s.salvage_passes==1);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_passes==5);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_passes==10);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_passes==20);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_passes==50);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_passes==0);
    press(KUI_SHELL_LEFT,false);assert(s.salvage_passes==50);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_passes==0);
    assert(!s.salvage_verify_files);
    press(KUI_SHELL_DOWN,false);assert(s.salvage_selected==4);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_verify_files);
    press(KUI_SHELL_LEFT,false);assert(!s.salvage_verify_files);
    press(KUI_SHELL_A,false);assert(s.salvage_verify_files);
    press(KUI_SHELL_DOWN,false);assert(s.salvage_selected==5);
    press(KUI_SHELL_DOWN,false);assert(s.salvage_selected==0);
    press(KUI_SHELL_LEFT,false);assert(s.salvage_passes==0);
    press(KUI_SHELL_UP,false);assert(s.salvage_selected==5);
    s.salvage_selected=0;press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_NEW && !s.confirm_salvage);
    s.salvage_selected=1;assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RESUME);
    assert(press(KUI_SHELL_Y,true)==KUI_SHELL_NONE && !s.confirm_salvage && !s.confirm_salvage_defer);
    assert(press(KUI_SHELL_A|KUI_SHELL_Y,false)==KUI_SHELL_NONE && !s.confirm_salvage);
    assert(press(KUI_SHELL_Y|KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.salvage_selected==1);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.confirm_salvage && s.confirm_salvage_defer);
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.salvage_selected==1 && s.confirm_salvage_defer);
    assert(press(KUI_SHELL_A|KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.confirm_salvage_defer);
    assert(press(KUI_SHELL_A|KUI_SHELL_START,false)==KUI_SHELL_NONE && s.confirm_salvage_defer);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RESUME_DEFER && !s.confirm_salvage && !s.confirm_salvage_defer);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RESUME);
    press(KUI_SHELL_Y,false);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_salvage && !s.confirm_salvage_defer && s.page==KUI_SHELL_SALVAGE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RESUME);
    press(KUI_SHELL_Y,false);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_salvage && !s.confirm_salvage_defer);
    press(KUI_SHELL_Y,false);
    assert(press(KUI_SHELL_B,true)==KUI_SHELL_STOP && !s.confirm_salvage && !s.confirm_salvage_defer);
    for(unsigned row=0;row<6;++row)if(row!=1) {
        s.salvage_selected=row;
        assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && !s.confirm_salvage && !s.confirm_salvage_defer);
    }
    s.salvage_selected=1;press(KUI_SHELL_Y,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RESUME_DEFER);
    s.salvage_selected=0;press(KUI_SHELL_A,false);
    assert(s.confirm_salvage && !s.confirm_salvage_defer);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_NEW);
    s.salvage_selected=2;assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RECOVER);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED);
    assert(kui_shell_progress_tenths(1,0)==0);
    assert(kui_shell_progress_tenths(1,3)==333);
    assert(kui_shell_progress_tenths(99,100)==990);
    assert(kui_shell_progress_tenths(UINT64_MAX-1,UINT64_MAX)==999);
    assert(kui_shell_progress_tenths(UINT64_MAX,UINT64_MAX)==1000);
    assert(kui_shell_progress_tenths(100,99)==1000);
}
static void salvage_drive_controls(void) {
    reset(KUI_SHELL_SALVAGE);
    assert(s.salvage_drive_options.speed==0 && s.salvage_drive_options.retries==0 && !s.salvage_trial_requested);
    s.salvage_selected=5;
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_SALVAGE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SALVAGE_DRIVE);
    assert(!s.confirm_salvage && s.salvage_drive_selected==0);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.salvage_drive_options.speed==8);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.salvage_drive_options.speed==0);
    for(unsigned i=1;i<KUI_DRIVE_SPEED_CHOICES;++i) {
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.salvage_drive_options.speed==i);
    }
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_drive_options.speed==0);
    press(KUI_SHELL_LEFT|KUI_SHELL_RIGHT,false);assert(s.salvage_drive_options.speed==0);
    press(KUI_SHELL_RIGHT,true);assert(s.salvage_drive_options.speed==0);
    press(KUI_SHELL_DOWN,false);assert(s.salvage_drive_selected==1);
    press(KUI_SHELL_LEFT,false);assert(s.salvage_drive_options.retries==5);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_drive_options.retries==0);
    for(unsigned i=1;i<KUI_DRIVE_RETRY_CHOICES;++i) {
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.salvage_drive_options.retries==i);
    }
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_drive_options.retries==0);
    press(KUI_SHELL_RIGHT,false);assert(s.salvage_drive_options.retries==1);
    press(KUI_SHELL_UP,false);press(KUI_SHELL_RIGHT,false);
    assert(s.salvage_drive_options.speed==1 && s.salvage_drive_options.retries==1);
    press(KUI_SHELL_UP,false);assert(s.salvage_drive_selected==3);
    press(KUI_SHELL_RIGHT,false);assert(s.page==KUI_SHELL_SALVAGE_DRIVE);
    press(KUI_SHELL_A,false);assert(s.page==KUI_SHELL_SALVAGE && s.salvage_selected==5);
    press(KUI_SHELL_A,false);assert(s.page==KUI_SHELL_SALVAGE_DRIVE);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SALVAGE);
    assert(s.salvage_drive_options.speed==1 && s.salvage_drive_options.retries==1);
    press(KUI_SHELL_A,false);assert(press(KUI_SHELL_B,true)==KUI_SHELL_STOP);
    assert(s.page==KUI_SHELL_SALVAGE_DRIVE);
    s.salvage_drive_selected=2;
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && !s.salvage_trial_requested);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && !s.salvage_trial_requested);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && !s.salvage_trial_requested);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP && !s.salvage_trial_requested);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SALVAGE);
    assert(!s.salvage_trial_requested);
    press(KUI_SHELL_A,false);assert(s.page==KUI_SHELL_SALVAGE_DRIVE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RECOVER);
    assert(s.page==KUI_SHELL_SALVAGE && s.salvage_trial_requested && s.salvage_trial_result);
    assert(s.salvage_drive_options.speed==1 && s.salvage_drive_options.retries==1);
    s.salvage_trial_requested=false; /* Main consumes the one-shot request. */
    unsigned selected=s.salvage_selected;
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.salvage_selected==selected);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && !s.confirm_salvage);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SALVAGE_DRIVE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SALVAGE && s.salvage_trial_result);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_ADVANCED && !s.salvage_trial_result);
    s.page=KUI_SHELL_SALVAGE;s.salvage_trial_requested=true;
    /* An ordinary action must never inherit a previous trial request. */
    s.salvage_selected=2;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RECOVER && !s.salvage_trial_requested);
    s.salvage_trial_requested=true;s.salvage_selected=1;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_RESUME && !s.salvage_trial_requested);
    s.salvage_trial_requested=true;s.salvage_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_salvage && !s.salvage_trial_requested);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SALVAGE_NEW);
    s.salvage_trial_requested=true;s.salvage_selected=1;
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.confirm_salvage_defer && !s.salvage_trial_requested);
    press(KUI_SHELL_B,false);s.salvage_selected=5;press(KUI_SHELL_A,false);
    s.salvage_drive_selected=3;press(KUI_SHELL_DOWN,false);assert(s.salvage_drive_selected==0);
    press(KUI_SHELL_UP,false);assert(s.salvage_drive_selected==3);
    reset(KUI_SHELL_SALVAGE);
    assert(s.salvage_drive_options.speed==0 && s.salvage_drive_options.retries==0 && !s.salvage_trial_requested);
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
    reset(KUI_SHELL_SALVAGE);render(&v);
    assert(strstr(drawn,"Recovery pass limit") && strstr(drawn,"Until stopped") && strstr(drawn,"retry holes automatically") && !strstr(drawn,"Zero-fill"));
    assert(strstr(drawn,"Read back recovered files") && strstr(drawn,"Catalogue check follows every resolved job"));
    assert(strstr(drawn,"Damaged disc controls"));
    s.salvage_selected=5;render(&v);
    assert(strstr(drawn,"read speed and drive retry settings") && strstr(drawn,"choices reset on restart"));
    press(KUI_SHELL_A,false);render(&v);
    assert(strstr(drawn,"Read speed") && strstr(drawn,"Drive retries") && strstr(drawn,"Return to salvage") && strstr(drawn,"Run bounded control trial"));
    assert(strstr(drawn,kui_drive_speed_name(0)) && strstr(drawn,kui_drive_retry_name(0)));
    assert(strstr(drawn,"Speed settings are optional") && strstr(drawn,"Actual speed needs measurement"));
    s.salvage_drive_options.speed=8;s.salvage_drive_options.retries=1;
    s.salvage_drive_selected=1;render(&v);
    assert(strstr(drawn,kui_drive_speed_name(8)) && strstr(drawn,kui_drive_retry_name(1)));
    assert(strstr(drawn,"Firmware decoding retries per command") && strstr(drawn,"Recovery passes and application attempts are separate"));
    assert(strstr(drawn,"Choices reset when the console restarts"));
    s.salvage_drive_selected=2;render(&v);
    assert(strstr(drawn,"Existing collected job; unresolved data sectors only"));
    assert(strstr(drawn,"Up to 8 sectors, 4 profiles, 32 comparison reads"));
    assert(strstr(drawn,"5 min soft limit; active command finishes first"));
    assert(strstr(drawn,"Fixed profiles ignore the manual choices above"));
    assert(strstr(drawn,"All validated repairs are saved immediately"));
    assert(strstr(drawn,"A Run trial   B Salvage"));
    s.salvage_drive_selected=3;render(&v);
    assert(strstr(drawn,"does not start disc reads"));
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_SALVAGE);
    s.salvage_selected=1;render(&v);
    assert(strstr(drawn,"Y Resume past next range") && strstr(drawn,"32 next sectors as unresolved zeros"));
    assert(strstr(drawn,"Restart first if drive reset is required"));
    press(KUI_SHELL_Y,false);render(&v);
    assert(strstr(drawn,"RESUME PAST THE NEXT RANGE?") && strstr(drawn,"committed prefix of the latest matching job"));
    assert(strstr(drawn,"32 uncommitted sectors") && strstr(drawn,"as unresolved zeros"));
    assert(strstr(drawn,"later optical repair") && strstr(drawn,"restart the console first"));
    assert(strstr(drawn,"A Resume past range") && !strstr(drawn,"START A DAMAGED-DISC"));
    press(KUI_SHELL_B,false);s.salvage_selected=0;
    s.confirm_salvage=true;render(&v);
    assert(strstr(drawn,"Skips bad sectors") && strstr(drawn,"until resolved or you press B") && strstr(drawn,"Unresolved holes stay incomplete"));
    s.salvage_passes=50;render(&v);assert(strstr(drawn,"up to 50 recovery passes"));
    s.confirm_salvage=false;v.busy=true;
    struct kui_app_status state;
    struct kui_salvage_status progress={.track=3,.tracks=31,.fad=45150,.attempts=291,
        .read_fad=45150,.read_count=1,.read_attempt=1,.read_limit=1,
        .read_state=KUI_SALVAGE_READ_READING,.last_problem_fad=45149,.last_problem_count=1,
        .last_problem_attempt=2,.last_problem_limit=2,.last_problem_state=KUI_SALVAGE_READ_DEFERRED,
        .targets=12,.recovered=5,.remaining=7,.done=5,.total=10,.first_pass_complete=true,.pass=2,.pass_limit=5};
    strcpy(progress.message,"Retrying unresolved sectors");
    kui_salvage_display_status(&state,&progress);
    s.salvage_drive_options=(struct kui_drive_options){0,0};
    v.app_status=&state;render(&v);
    assert(strstr(drawn,"Separate recovery jobs; normal captures remain untouched.") && !strstr(drawn,"Requested speed:"));
    assert(strstr(drawn,"left 7") && strstr(drawn,"pass 2/5") && !strstr(drawn,"New salvage job"));
    assert(strstr(drawn,"Sector attempt 1/1") && strstr(drawn,"Last issue FAD 45149: 2/2; deferred") && !strstr(drawn,"291"));
    s.salvage_drive_options=(struct kui_drive_options){8,0};render(&v);
    assert(strstr(drawn,"Requested speed: 12x requested; drive retries: Unchanged"));
    assert(!strstr(drawn,"Separate recovery jobs;") && strstr(drawn,"pass 2/5"));
    s.salvage_drive_options=(struct kui_drive_options){0,5};render(&v);
    assert(strstr(drawn,"Requested speed: Unchanged; drive retries: 8"));
    v.salvage_control_trial=true;render(&v);
    assert(strstr(drawn,"Bounded trial: up to 8 sectors; fixed comparison profiles") && !strstr(drawn,"Requested speed:"));
    v.salvage_control_trial=false;
    for(unsigned i=0;i<KUI_DRIVE_SPEED_CHOICES;++i) {
        s.salvage_drive_options.speed=i;
        for(unsigned j=0;j<KUI_DRIVE_RETRY_CHOICES;++j) {
            s.salvage_drive_options.retries=j;render(&v); /* observe() checks exact rendered text width. */
        }
    }
    struct kui_app_status trial_state=state;
    trial_state.complete=true;trial_state.line_count=7;
    strcpy(trial_state.message,"Bounded drive-control trial finished");
    for(unsigned i=0;i<7;++i)snprintf(trial_state.lines[i],KUI_APP_LINE_CAP,"Trial result line %u",i+1);
    v.salvage_control_trial=true;s.salvage_trial_result=true;v.busy=false;v.app_status=&trial_state;
    render(&v);
    assert(strstr(drawn,"Bounded trial: up to 8 sectors; fixed comparison profiles"));
    assert(strstr(drawn,"A Drive controls   B Advanced") && !strstr(drawn,"New salvage job"));
    for(unsigned i=0;i<7;++i) {
        char line[32];snprintf(line,sizeof(line),"Trial result line %u",i+1);assert(strstr(drawn,line));
    }
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SALVAGE_DRIVE);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_SALVAGE && s.salvage_trial_result);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_ADVANCED && !s.salvage_trial_result);
    s.page=KUI_SHELL_SALVAGE;v.app_status=&state;v.salvage_control_trial=false;
    state.line_count=7;state.complete=true;state.passed=true;state.done=state.total;v.busy=false;
    strcpy(state.lines[1],"Targets 12  recovered 12  remaining 0");
    strcpy(state.message,"All targets resolved; every saved track byte verified");
    strcpy(state.lines[3],"Saved bytes: every track byte read back and verified.");
    strcpy(state.lines[4],"TOSEC: FULL TRACK MATCH");
    strcpy(state.lines[5],"SWORD OF THE BERSERK");
    strcpy(state.lines[6],"/KUI/salvage/job-0001");
    render(&v);assert(strstr(drawn,"remaining 0") && strstr(drawn,"Saved bytes: every track") && strstr(drawn,"FULL TRACK MATCH"));
}
static struct kui_games_detail launch_detail(bool ce, bool cd) {
    struct kui_games_detail detail={.valid=true,.format=cd?KUI_GAME_IMAGE_CUE:KUI_GAME_IMAGE_GDI,
        .native_gd=!ce&&!cd,.native_cd=!ce&&cd,.cd_image=cd,.windows_ce=ce,
        .tracks=15,.boot_bytes=ce?1253376:123456,.boot_lba=45166,
        .audio={KUI_GAME_AUDIO_GD_HIGH_DENSITY,12,12,0}};
    strcpy(detail.path,cd?"/Games/CD sample/disc.cue":"/Games/Toy Commander/disc.gdi");
    strcpy(detail.title,ce?"ARMADA":"TOY COMMANDER");
    strcpy(detail.boot_file,ce?"0WINCEOS.BIN":"1ST_READ.BIN");
    return detail;
}
static void install_launch_detail(const struct kui_games_detail *detail) {
    reset(KUI_SHELL_GAMES_DETAIL);
    strcpy(s.games_selected_path,detail->path);
    kui_shell_set_games_detail(&s,detail);
}
static void games_retail_controls(void) {
    struct kui_games_detail detail=launch_detail(false,false);
    install_launch_detail(&detail);
    assert(kui_shell_games_retail_ready(&s) && kui_shell_games_launch_ready(&s));
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && !s.games_retail_reader);
    assert(press(KUI_SHELL_A|KUI_SHELL_X,false)==KUI_SHELL_NONE && !s.games_retail_reader);
    assert(press(KUI_SHELL_A|KUI_SHELL_Y,false)==KUI_SHELL_NONE && !s.games_retail_reader);
    assert(press(KUI_SHELL_START|KUI_SHELL_L,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_STANDARD);
    assert(launch_reader(KUI_SHELL_X)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_ASYNC);
    assert(launch_reader(KUI_SHELL_Y)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_ASYNC_EAGER);
    /* Leaving and returning retains this image's choices; a fresh inspection resets them. */
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_ASYNC_EAGER);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_GAMES_INSPECT && !s.games_detail.valid);
    assert(s.games_launch_reader==KUI_RETAIL_READER_STANDARD && !s.games_launch_cdda);
    kui_shell_set_games_detail(&s,&detail);press(KUI_SHELL_A,false);
    /* Launch admission is rechecked at handoff. */
    strcpy(s.games_detail.path,"/Games/Other.gdi");
    assert(!kui_shell_games_retail_ready(&s) && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    s.games_detail=detail;memset(s.games_detail.title,'X',sizeof(s.games_detail.title));
    assert(!kui_shell_games_retail_ready(&s));
    s.games_detail=detail;memset(s.games_detail.boot_file,'X',sizeof(s.games_detail.boot_file));
    assert(!kui_shell_games_retail_ready(&s));
    s.games_detail=detail;s.games_detail.windows_ce=true;assert(!kui_shell_games_retail_ready(&s));
    s.games_detail=detail;s.games_detail.native_gd=false;assert(!kui_shell_games_retail_ready(&s));
    s.games_detail=detail;s.games_detail.boot_bytes=0;assert(!kui_shell_games_retail_ready(&s));
    s.games_detail=detail;s.games_detail.boot_bytes=KUI_RETAIL_IMAGE_BOOT_MAX+1;assert(!kui_shell_games_retail_ready(&s));
    s.games_detail=detail;s.games_detail.boot_lba=44999;assert(!kui_shell_games_retail_ready(&s));
    s.games_detail=detail;s.games_detail.tracks=99;assert(kui_shell_games_retail_ready(&s));
    s.games_detail=detail;s.games_detail.tracks=100;assert(!kui_shell_games_retail_ready(&s));
    s.games_detail=detail;s.games_detail.tracks=0;assert(!kui_shell_games_retail_ready(&s));
    assert(!kui_shell_games_retail_ready(NULL) && !kui_shell_games_launch_ready(NULL));
    detail=launch_detail(true,false);install_launch_detail(&detail);
    assert(kui_shell_games_ce_probe_ready(&s) && !kui_shell_games_retail_ready(&s));
    assert(s.games_launch_reader==KUI_RETAIL_READER_ASYNC);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    assert(!kui_shell_games_option_ready(&s,KUI_SHELL_GAME_OPTION_READER));
    assert(kui_shell_games_option_ready(&s,KUI_SHELL_GAME_OPTION_CDDA));
    assert(!kui_shell_games_encoding_ready(&s));
    assert(!strcmp(kui_shell_games_reader_label(&s),"Background"));
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.games_launch_reader==KUI_RETAIL_READER_ASYNC);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_retail_reader);
    assert(press(KUI_SHELL_START|KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_retail_reader);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL &&
        s.games_retail_reader==(KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
    s.games_launch_cdda=true;s.games_launch_reader=KUI_RETAIL_READER_ASYNC_EAGER;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL &&
        s.games_retail_reader==(KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC|KUI_GAMES_RETAIL_CE_EXCERPT));
    s.games_launch_cdda=false;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL &&
        s.games_retail_reader==(KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC));
    s.games_detail.boot_bytes=2048;assert(!kui_shell_games_ce_probe_ready(&s));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    s.games_detail=detail;s.games_detail.boot_bytes=KUI_RETAIL_IMAGE_BOOT_MAX+1;
    assert(!kui_shell_games_ce_probe_ready(&s));
    s.games_detail=detail;s.games_detail.native_gd=true;assert(!kui_shell_games_ce_probe_ready(&s));
    s.games_detail=detail;s.games_detail.tracks=100;assert(!kui_shell_games_ce_probe_ready(&s));
    s.games_detail=detail;s.games_detail.boot_lba=44999;assert(!kui_shell_games_ce_probe_ready(&s));
    s.games_detail=detail;strcpy(s.games_detail.path,"/Games/Stale.gdi");assert(!kui_shell_games_ce_probe_ready(&s));
}
static void games_armada_ce_music_controls(void) {
    struct kui_shell_view view={0};
    struct kui_games_detail detail=launch_detail(true,false);
    detail.tracks=5;detail.audio=(struct kui_game_audio_info){KUI_GAME_AUDIO_GD_HIGH_DENSITY,2,1,1};
    strcpy(detail.product,"T40301N 00");strcpy(detail.path,"/Games/ARMADA/ARMADA.gdi");
    install_launch_detail(&detail);
    const unsigned ordinary=KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC;
    const unsigned excerpt=ordinary|KUI_GAMES_RETAIL_CE_EXCERPT;
    assert(kui_shell_games_cdda_ready(&s) && !s.games_launch_cdda);
    assert(s.games_launch_reader==KUI_RETAIL_READER_ASYNC);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==ordinary);
    press(KUI_SHELL_Y,false);s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
    assert(kui_shell_games_option_ready(&s,KUI_SHELL_GAME_OPTION_CDDA));
    render(&view);
    assert(strstr(drawn,"Play music from this game's audio tracks") &&
        !strstr(drawn,"CD music requires background loading off"));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_cdda);
    assert(s.games_launch_reader==KUI_RETAIL_READER_ASYNC && s.games_retail_reader==ordinary);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==excerpt);
    s.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;render(&view);
    assert(strstr(drawn,"CD music: On") && strstr(drawn,"Background loading: On"));
    s.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;
    /* Returning to Off retains the old CE background package route. */
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_launch_cdda);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==ordinary);
    /* The saved boolean hydrates to the CE reader, never native CDDA or an
     * eager reader. Persisting the setting remains a separate launch gesture. */
    const struct kui_game_launch_settings saved={.reader=KUI_GAME_READER_EAGER,.cdda=true,.encoding=KUI_GAME_ENCODING_SCRAMBLED};
    install_launch_detail(&detail);kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
    assert(s.games_launch_cdda && s.games_launch_reader==KUI_RETAIL_READER_ASYNC &&
        s.games_launch_encoding==KUI_GAME_ENCODING_AUTO && !s.games_retail_scrambled);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==excerpt);
    /* Inspection only advertises an exact metadata candidate. Physical raw
     * geometry is checked again by preparation before an excerpt is read. */
    for(unsigned fault=0;fault<7u;fault++) {
        struct kui_games_detail other=detail;
        if(fault==0u) strcpy(other.product,"T00000N 00");
        if(fault==1u) other.tracks=6;
        if(fault==2u) strcpy(other.boot_file,"OTHER.BIN");
        if(fault==3u) other.boot_bytes--;
        if(fault==4u) other.format=KUI_GAME_IMAGE_ISO;
        if(fault==5u) other.cd_image=true;
        if(fault==6u) other.scrambled=true;
        install_launch_detail(&other);
        /* The UI offers the opt-in by media capability; exact runtime
         * admission happens in the loader, before native code executes. */
        const bool candidate=fault<4u;
        assert(kui_shell_games_cdda_ready(&s)==candidate);
        kui_shell_set_game_launch_settings(&s,other.path,&saved,true);
        assert(s.games_launch_cdda==candidate);
        press(KUI_SHELL_Y,false);s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
        if(!candidate) {
            assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_launch_cdda);
            assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==ordinary);
        } else {
            assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==excerpt);
        }
    }
    install_launch_detail(&detail);memset(s.games_detail.product,'X',sizeof(s.games_detail.product));
    assert(!kui_shell_games_cdda_ready(&s));
    install_launch_detail(&detail);s.games_launch_cdda=true;
    strcpy(s.games_detail.path,"/Games/Stale/ARMADA.gdi");
    assert(!kui_shell_games_cdda_ready(&s));s.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    puts("PASS Armada CE music: saved On selects separate excerpt route, Off preserves CE route, unsupported metadata and stale paths refuse");
}
static void games_bam4_ce_music_controls(void) {
    struct kui_games_detail detail=launch_detail(true,false);
    detail.tracks=26u;detail.audio=(struct kui_game_audio_info){KUI_GAME_AUDIO_GD_HIGH_DENSITY,23u,22u,1u};
    detail.boot_bytes=1830912u;detail.boot_lba=548106u;
    strcpy(detail.product,"T-8117N");strcpy(detail.boot_file,"0WINCEOS.BIN");
    strcpy(detail.path,"/Games/BAM4/BAM4.gdi");
    install_launch_detail(&detail);
    assert(kui_shell_games_cdda_ready(&s) && !s.games_launch_cdda);
    const unsigned ordinary=KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC;
    const unsigned music=ordinary|KUI_GAMES_RETAIL_CE_EXCERPT;
    press(KUI_SHELL_Y,false);s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_cdda);
    assert(s.games_launch_reader==KUI_RETAIL_READER_ASYNC && !s.games_retail_reader);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==music);
    const struct kui_game_launch_settings saved={.reader=KUI_GAME_READER_EAGER,.cdda=true,.encoding=KUI_GAME_ENCODING_SCRAMBLED};
    install_launch_detail(&detail);kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
    assert(s.games_launch_cdda && s.games_launch_reader==KUI_RETAIL_READER_ASYNC &&
        s.games_launch_encoding==KUI_GAME_ENCODING_AUTO);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==music);
    s.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_launch_cdda);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==ordinary);
    for(unsigned fault=0;fault<3u;fault++) {
        struct kui_games_detail other=detail;
        if(fault==0u) other.tracks=25u;
        if(fault==1u) other.boot_bytes--;
        if(fault==2u) strcpy(other.product,"T40301N 00");
        install_launch_detail(&other);assert(kui_shell_games_cdda_ready(&s));
        kui_shell_set_game_launch_settings(&s,other.path,&saved,true);assert(s.games_launch_cdda);
    }
    detail.tracks=8u;detail.boot_bytes=1110016u;detail.boot_lba=548458u;
    detail.audio=(struct kui_game_audio_info){KUI_GAME_AUDIO_GD_HIGH_DENSITY,5u,4u,1u};
    strcpy(detail.product,"T40601N");strcpy(detail.path,"/Games/WORMS/WORMS.gdi");
    install_launch_detail(&detail);assert(kui_shell_games_cdda_ready(&s) && !s.games_launch_cdda);
    kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
    assert(s.games_launch_cdda && s.games_launch_reader==KUI_RETAIL_READER_ASYNC);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==music);
    puts("PASS WinCE CD music: BAM4/Worms and other CDDA candidates, saved opt-in On route and Off ordinary route");
}
static void games_cdda_compatibility_status(void) {
    struct kui_shell_view view={0};
    const char *experimental="CD audio: Experimental / untested";
    const char *tested="CD audio: Tested (known version)";
    const char *limited="CD audio: Tested; FMV issues remain";
    assert(!strcmp(kui_shell_games_cdda_status(NULL),"CD audio: Unavailable"));
    for(unsigned ce=0;ce<2u;ce++) {
        struct kui_games_detail detail=launch_detail(ce!=0,false);
        /* Neither a familiar title/product nor an enabled saved option is
         * evidence that this particular image has been tested. */
        strcpy(detail.title,ce?"WORMS ARMAGEDDON":"MDK2");
        strcpy(detail.product,ce?"T40601N":"T-12501N");
        install_launch_detail(&detail);
        assert(kui_shell_games_cdda_ready(&s) && !s.games_launch_cdda);
        assert(!strcmp(kui_shell_games_cdda_status(&s),experimental));
        render(&view);assert(strstr(drawn,experimental));
        const struct kui_game_launch_settings saved={.reader=KUI_GAME_READER_STANDARD,.cdda=true,.encoding=KUI_GAME_ENCODING_AUTO};
        kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
        assert(s.games_launch_cdda && !strcmp(kui_shell_games_cdda_status(&s),experimental));
        s.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
        render(&view);assert(strstr(drawn,experimental) && strstr(drawn,"START Launch"));
        assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL);
        s.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;render(&view);
        assert(strstr(drawn,experimental) && strstr(drawn,"CD music: On"));
        /* The worker owns content matching. Only its explicit status may
         * display a tested claim; the claim concerns audio, not the game. */
        s.games_detail.cdda_compatibility=KUI_GAME_CDDA_TESTED;
        s.games_launch_cdda=false;render(&view);
        assert(strstr(drawn,tested) && strstr(drawn,"CD music: Off") && !strstr(drawn,experimental));
        assert(strstr(drawn,"RAM patches: Off"));
        if(!ce) {
            s.games_launch_patches.entries[0]=(struct kui_game_ram_patch){UINT32_C(0x8c010000),0u,true};
            render(&view);
            assert(strstr(drawn,tested) && strstr(drawn,"RAM patches: On (one-time boot writes)"));
            memset(&s.games_launch_patches,0,sizeof(s.games_launch_patches));
        }
        s.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;render(&view);
        assert(strstr(drawn,tested));
        s.games_detail.cdda_compatibility=KUI_GAME_CDDA_TESTED_LIMITED;
        render(&view);assert(strstr(drawn,limited) && !strstr(drawn,tested));
        s.page=KUI_SHELL_GAMES_DETAIL;render(&view);assert(strstr(drawn,limited));
        s.games_detail.cdda_compatibility=(enum kui_game_cdda_compatibility)99;
        assert(!strcmp(kui_shell_games_cdda_status(&s),experimental));
        s.games_detail.cdda_compatibility=KUI_GAME_CDDA_TESTED;
        strcpy(s.games_detail.path,"/Games/Stale/disc.gdi");
        assert(!strcmp(kui_shell_games_cdda_status(&s),"CD audio: Unavailable"));
        s.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;render(&view);
        assert(!strstr(drawn,tested) && !strstr(drawn,experimental) && !strstr(drawn,"START Launch"));
        install_launch_detail(&detail);
        s.games_detail.audio=(struct kui_game_audio_info){KUI_GAME_AUDIO_GD_LOW_DENSITY_ONLY,1u,0u,1u};
        s.games_detail.cdda_compatibility=KUI_GAME_CDDA_TESTED;
        assert(!strcmp(kui_shell_games_cdda_status(&s),"CD audio: Unavailable"));
    }
    puts("PASS CD audio status: unknown versions remain experimental, tested audio comes from inspection, stale evidence is hidden");
}
static void games_launch_options_controls(void) {
    struct kui_games_detail detail=launch_detail(false,false);
    install_launch_detail(&detail);
    assert(kui_shell_games_cdda_ready(&s) && !kui_shell_games_cdda_ready(NULL));
    assert(!kui_shell_games_option_ready(NULL,KUI_SHELL_GAME_OPTION_READER));
    assert(!kui_shell_games_option_ready(&s,KUI_SHELL_GAME_OPTION_COUNT));
    assert(KUI_SHELL_GAME_OPTION_COUNT==4);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    assert(!s.games_launch_selected && !s.games_launch_cdda && !s.games_retail_reader);
    assert(press(KUI_SHELL_RIGHT,true)==KUI_SHELL_NONE && !s.games_launch_reader);
    assert(press(KUI_SHELL_A|KUI_SHELL_START,false)==KUI_SHELL_NONE && !s.games_launch_reader && !s.games_retail_reader);
    assert(press(KUI_SHELL_RIGHT|KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && !s.games_launch_selected);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_reader==KUI_RETAIL_READER_ASYNC);
    assert(!s.games_retail_reader); /* Saving a toggle never starts a game. */
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_launch_reader);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_reader==KUI_RETAIL_READER_ASYNC);
    /* The basic toggle has only two states. Legacy (Y) belongs to Advanced. */
    open_extra_options();s.game_tools_selected=1;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_reader==KUI_RETAIL_READER_ASYNC_EAGER);
    assert(!s.games_retail_reader);
    assert(press(KUI_SHELL_START|KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
    s.games_retail_scrambled=true;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_cdda && !s.games_launch_reader && !s.games_retail_scrambled);
    assert(!s.games_retail_reader);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL &&
        s.games_retail_reader==(KUI_RETAIL_READER_STANDARD|KUI_GAMES_RETAIL_CDDA));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
    assert(s.games_launch_cdda && press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL &&
        s.games_retail_reader==(KUI_RETAIL_READER_STANDARD|KUI_GAMES_RETAIL_CDDA));
    press(KUI_SHELL_Y,false);s.games_launch_selected=KUI_SHELL_GAME_OPTION_READER;
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_launch_cdda && s.games_launch_reader==KUI_RETAIL_READER_ASYNC);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_ASYNC);
    /* Diagnostic actions are nested and cancel to Tests, then Advanced. */
    open_developer_tests();s.game_tests_selected=0;
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_TESTS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_IMAGE_PROBE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_TESTS);
    assert(s.games_launch_reader==KUI_RETAIL_READER_ASYNC);
    press(KUI_SHELL_DOWN,false);assert(s.game_tests_selected==1);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_PROBE_CONFIRM && s.games_tests_from_options);
    assert(press(KUI_SHELL_A|KUI_SHELL_START,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_PROBE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_TESTS);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
    s.game_tools_selected=2;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_INFO);
    assert(press(KUI_SHELL_A|KUI_SHELL_START,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    /* Metadata eligibility remains separate from exact worker profile admission. */
    strcpy(s.games_detail.title,"ANOTHER NATIVE GAME");
    s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
    assert(kui_shell_games_cdda_ready(&s));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_cdda);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL);
    const struct kui_game_audio_info unavailable[]={
        {KUI_GAME_AUDIO_NONE,0,0,0},{KUI_GAME_AUDIO_GD_LOW_DENSITY_ONLY,1,0,1},
        {KUI_GAME_AUDIO_UNKNOWN,12,12,0},{KUI_GAME_AUDIO_GD_HIGH_DENSITY,12,0,0},
        {KUI_GAME_AUDIO_GD_HIGH_DENSITY,0,1,0},{KUI_GAME_AUDIO_GD_HIGH_DENSITY,12,13,0},
        {KUI_GAME_AUDIO_GD_HIGH_DENSITY,16,12,0},{KUI_GAME_AUDIO_GD_HIGH_DENSITY,UINT_MAX,UINT_MAX,0}
    };
    for(unsigned i=0;i<sizeof(unavailable)/sizeof(*unavailable);i++) {
        s.games_detail=detail;s.games_detail.audio=unavailable[i];s.games_launch_cdda=false;
        assert(!kui_shell_games_cdda_ready(&s));
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_launch_cdda);
        s.games_launch_cdda=true;s.games_retail_reader=0;
        assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && !s.games_retail_reader);
        s.games_launch_cdda=false;
        assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_STANDARD);
    }
    s.games_detail=detail;strcpy(s.games_detail.path,"/Games/Stale/disc.gdi");
    for(unsigned row=0;row<KUI_SHELL_GAME_OPTION_COUNT;row++) {
        s.games_launch_selected=row;assert(!kui_shell_games_option_ready(&s,row));
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    }
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
    kui_shell_set_games_detail(&s,&detail);assert(!kui_shell_games_image_ready(&s));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    /* An unknown inspected GDI still has read tests, without a launch route. */
    s.games_detail=detail;s.games_detail.native_gd=false;
    assert(!kui_shell_games_launch_ready(&s));
    open_image_test();assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_IMAGE_PROBE);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_TESTS);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
    press(KUI_SHELL_B,false);press(KUI_SHELL_B,false);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    /* Inspection clears the previous in-memory snapshot; matching worker
     * publication restores persisted settings separately. */
    s.games_launch_cdda=true;s.games_retail_scrambled=true;
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_GAMES_INSPECT && !s.games_launch_cdda && !s.games_retail_scrambled);
    kui_shell_set_games_detail(&s,&detail);
    assert(!s.games_launch_cdda && !s.games_launch_reader);
    puts("PASS Games settings: save is not launch, reader/CDDA isolation, nested diagnostic returns, stale identity");
}
static void games_cd_controls(void) {
    const struct {const char *path;enum kui_game_image_format kind;uint32_t boot_lba;bool scrambled;} cases[]={
        {"/Games/CD sample/disc.iso",KUI_GAME_IMAGE_ISO,0,false},
        {"/Games/CD sample/disc.cue",KUI_GAME_IMAGE_CUE,11700,false},
        {"/Games/CD sample/marked.cue",KUI_GAME_IMAGE_CUE,11700,true},
        {"/Games/CD sample/disc.cdi",KUI_GAME_IMAGE_CDI,11700,false}
    };
    const unsigned buttons[]={KUI_SHELL_A,KUI_SHELL_X,KUI_SHELL_Y};
    const unsigned readers[]={KUI_RETAIL_READER_STANDARD,KUI_RETAIL_READER_ASYNC,KUI_RETAIL_READER_ASYNC_EAGER};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++) {
        struct kui_games_detail detail=launch_detail(false,true);
        strcpy(detail.path,cases[i].path);detail.format=cases[i].kind;
        detail.boot_lba=cases[i].boot_lba;detail.scrambled=cases[i].scrambled;
        if(detail.format==KUI_GAME_IMAGE_CDI) {detail.native_gd=true;detail.native_cd=false;}
        install_launch_detail(&detail);
        assert(kui_shell_games_encoding_ready(&s) && s.games_retail_scrambled==cases[i].scrambled);
        assert(s.games_launch_encoding==KUI_GAME_ENCODING_AUTO);
        assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
        s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
        assert(!kui_shell_games_cdda_ready(&s) && press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_launch_cdda);
        open_developer_tests();s.game_tests_selected=0;
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_TESTS);
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
        s.game_tools_selected=KUI_SHELL_GAME_TOOL_ENCODING;
        assert(press(KUI_SHELL_RIGHT,true)==KUI_SHELL_NONE && s.games_retail_scrambled==cases[i].scrambled);
        assert(press(KUI_SHELL_LEFT|KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.games_launch_encoding==KUI_GAME_ENCODING_AUTO);
        unsigned override=cases[i].scrambled?KUI_GAME_ENCODING_PLAIN:KUI_GAME_ENCODING_SCRAMBLED;
        for(unsigned n=0;s.games_launch_encoding!=override && n<2;n++)
            assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH);
        assert(s.games_launch_encoding==override && s.games_retail_scrambled!=cases[i].scrambled);
        assert(!s.games_retail_reader);
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
        press(KUI_SHELL_B,false);
        assert(s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM && s.games_retail_scrambled!=cases[i].scrambled);
        assert(press(KUI_SHELL_R,false)==KUI_SHELL_NONE && s.games_retail_scrambled!=cases[i].scrambled);
        for(unsigned reader=0;reader<3;reader++) {
            unsigned encoding=cases[i].scrambled?KUI_GAMES_RETAIL_BOOT_PLAIN:KUI_GAMES_RETAIL_DESCRAMBLE;
            assert(launch_reader(buttons[reader])==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==(readers[reader]|encoding));
            assert(!strcmp(s.games_selected_path,cases[i].path));
        }
        press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES_DETAIL);
        press(KUI_SHELL_A,false);assert(s.games_retail_scrambled!=cases[i].scrambled);
        press(KUI_SHELL_B,false);press(KUI_SHELL_X,false);
        assert(!s.games_retail_scrambled && !s.games_launch_cdda && !s.games_launch_reader);
        kui_shell_set_games_detail(&s,&detail);
        assert(s.games_launch_encoding==KUI_GAME_ENCODING_AUTO && s.games_retail_scrambled==cases[i].scrambled);
        s.page=KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM;
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
        s.page=KUI_SHELL_GAMES_DETAIL;
        detail.windows_ce=true;detail.native_gd=detail.native_cd=false;
        strcpy(detail.boot_file,"0WINCEOS.BIN");detail.boot_bytes=1253376;
        kui_shell_set_games_detail(&s,&detail);
        assert(kui_shell_games_ce_probe_ready(&s) && !kui_shell_games_encoding_ready(&s) && !s.games_retail_scrambled);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL &&
            s.games_retail_reader==(KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC));
        strcpy(s.games_detail.path,"/Games/Stale.iso");
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    }
}
static void games_saved_launch_controls(void) {
    static struct kui_game_launch_table table,decoded;
    static uint8_t record[KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX];
    struct kui_games_detail detail=launch_detail(false,false);
    struct kui_game_launch_settings value={.reader=99,.cdda=true,.encoding=99},saved;
    install_launch_detail(&detail);
    assert(!kui_shell_game_launch_settings(NULL,&value));
    assert(!kui_shell_game_launch_settings(&s,NULL));
    assert(kui_shell_game_launch_settings(&s,&value));
    assert(value.reader==KUI_GAME_READER_STANDARD && !value.cdda && value.encoding==KUI_GAME_ENCODING_AUTO);
    press(KUI_SHELL_Y,false);s.games_launch_selected=KUI_SHELL_GAME_OPTION_READER;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_retail_reader);
    assert(kui_shell_game_launch_settings(&s,&saved) && saved.reader==KUI_GAME_READER_BACKGROUND);
    kui_shell_game_launch_saved(&s,detail.path,false);
    assert(strstr(s.games_launch_notice,"Not saved") && s.games_launch_reader==KUI_RETAIL_READER_ASYNC);
    kui_shell_game_launch_saved(&s,"/Games/Stale/disc.gdi",true);
    assert(strstr(s.games_launch_notice,"Not saved"));
    kui_shell_game_launch_saved(&s,detail.path,true);assert(strstr(s.games_launch_notice,"Saved for this image"));
    /* Persist two exact variants, then recreate the shell as on a cold boot. */
    kui_game_launch_table_clear(&table);
    assert(kui_game_launch_table_upsert(&table,detail.path,&saved));
    struct kui_game_launch_settings copy={.reader=KUI_GAME_READER_STANDARD,.cdda=true,.encoding=KUI_GAME_ENCODING_AUTO};
    const char *copy_path="/Games/Toy Commander/disc-2048.gdi";
    assert(kui_game_launch_table_upsert(&table,copy_path,&copy));
    size_t bytes=0;uint64_t sequence=0;
    assert(kui_game_launch_settings_encode(record,sizeof(record),&bytes,&table,7));
    assert(kui_game_launch_settings_decode(&decoded,&sequence,record,bytes) && sequence==7);
    install_launch_detail(&detail);
    assert(kui_game_launch_table_find(&decoded,detail.path,&value));
    kui_shell_set_game_launch_settings(&s,detail.path,&value,true);
    assert(s.games_launch_reader==KUI_RETAIL_READER_ASYNC && !s.games_launch_cdda);
    press(KUI_SHELL_A,false);assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_ASYNC);
    /* A stale publication cannot hydrate either settings or save feedback. */
    unsigned reader=s.games_launch_reader;char notice[sizeof(s.games_launch_notice)];
    strcpy(notice,s.games_launch_notice);
    kui_shell_set_game_launch_settings(&s,copy_path,&copy,true);
    kui_shell_game_launch_saved(&s,copy_path,false);
    assert(s.games_launch_reader==reader && !s.games_launch_cdda && !strcmp(notice,s.games_launch_notice));
    s.page=KUI_SHELL_GAMES_DETAIL;strcpy(detail.path,copy_path);strcpy(s.games_selected_path,copy_path);
    kui_shell_set_games_detail(&s,&detail);
    assert(kui_game_launch_table_find(&decoded,copy_path,&value));
    kui_shell_set_game_launch_settings(&s,copy_path,&value,true);
    assert(s.games_launch_cdda && s.games_launch_reader==KUI_RETAIL_READER_STANDARD);
    /* CE and unavailable profiles constrain a stored choice before launch. */
    detail=launch_detail(true,false);install_launch_detail(&detail);
    value=(struct kui_game_launch_settings){.reader=KUI_GAME_READER_EAGER,.cdda=true,.encoding=KUI_GAME_ENCODING_SCRAMBLED};
    kui_shell_set_game_launch_settings(&s,detail.path,&value,true);
    assert(s.games_launch_reader==KUI_RETAIL_READER_ASYNC && s.games_launch_cdda &&
        s.games_launch_encoding==KUI_GAME_ENCODING_AUTO && !s.games_retail_scrambled);
    detail=launch_detail(false,false);detail.audio=(struct kui_game_audio_info){KUI_GAME_AUDIO_NONE,0,0,0};
    install_launch_detail(&detail);kui_shell_set_game_launch_settings(&s,detail.path,&value,true);
    assert(s.games_launch_reader==KUI_RETAIL_READER_ASYNC_EAGER && !s.games_launch_cdda && s.games_launch_encoding==KUI_GAME_ENCODING_AUTO);
    value.reader=KUI_GAME_READER_COUNT;
    kui_shell_set_game_launch_settings(&s,detail.path,&value,true);
    assert(strstr(s.games_launch_notice,"could not be read"));
    strcpy(s.games_detail.path,"/Games/Stale/disc.gdi");
    saved=(struct kui_game_launch_settings){.reader=99,.cdda=true,.encoding=99};
    assert(!kui_shell_game_launch_settings(&s,&saved) && saved.reader==99 && saved.encoding==99);
    kui_shell_set_game_launch_settings(&s,detail.path,&copy,true);
    assert(!s.games_launch_cdda && s.games_launch_reader==KUI_RETAIL_READER_ASYNC_EAGER);
    /* Automatic follows current inspected encoding; explicit override survives restart. */
    detail=launch_detail(false,true);detail.scrambled=true;install_launch_detail(&detail);
    value=(struct kui_game_launch_settings){.reader=KUI_GAME_READER_BACKGROUND,.cdda=false,.encoding=KUI_GAME_ENCODING_AUTO};
    kui_shell_set_game_launch_settings(&s,detail.path,&value,true);assert(s.games_retail_scrambled);
    value.encoding=KUI_GAME_ENCODING_PLAIN;
    kui_shell_set_game_launch_settings(&s,detail.path,&value,true);assert(!s.games_retail_scrambled);
    press(KUI_SHELL_A,false);assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL &&
        s.games_retail_reader==(KUI_RETAIL_READER_ASYNC|KUI_GAMES_RETAIL_BOOT_PLAIN));
    puts("PASS Games saved settings: codec cold-boot hydration, separate variants, capabilities, stale publication and save feedback");
}
static void assert_patch(unsigned slot,uint32_t address,uint32_t value,bool enabled) {
    const struct kui_game_ram_patch *patch=&s.games_launch_patches.entries[slot];
    assert(patch->address==address && patch->value==value && patch->enabled==enabled);
}
static void same_launch_settings(const struct kui_game_launch_settings *a,const struct kui_game_launch_settings *b) {
    assert(a->reader==b->reader && a->cdda==b->cdda && a->encoding==b->encoding);
    for(unsigned i=0;i<KUI_GAME_RAM_PATCHES_COUNT;i++) {
        assert(a->patches.entries[i].address==b->patches.entries[i].address);
        assert(a->patches.entries[i].value==b->patches.entries[i].value);
        assert(a->patches.entries[i].enabled==b->patches.entries[i].enabled);
    }
}
static void open_patch_options(void) {
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_PATCHES;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_PATCHES);
}
/* Enter a complete word with controller gestures; the helper does not bypass
 * the editor by assigning its draft. The caller decides whether A must save. */
static void edit_patch_word(unsigned row,uint32_t value) {
    assert(s.page==KUI_SHELL_GAMES_PATCHES && !s.games_patch_editing && row%3u);
    while(s.games_patch_selected!=row) assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.games_patch_editing && !s.games_patch_digit);
    for(unsigned digit=0;digit<8;digit++) {
        unsigned shift=(7u-digit)*4u,wanted=(value>>shift)&15u;
        unsigned current=(s.games_patch_draft>>shift)&15u,up=(wanted+16u-current)%16u;
        for(unsigned n=0;n<(up<=8?up:16u-up);n++)
            assert(press(up<=8?KUI_SHELL_UP:KUI_SHELL_DOWN,false)==KUI_SHELL_NONE);
        assert(((s.games_patch_draft>>shift)&15u)==wanted && s.games_patch_digit==digit);
        if(digit<7) assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE);
    }
    assert(s.games_patch_draft==value);
}
static void games_patch_controls(void) {
    static struct kui_game_launch_table table,decoded;
    static uint8_t record[KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX];
    struct kui_games_detail detail=launch_detail(false,false);detail.boot_bytes=131;
    install_launch_detail(&detail);assert(kui_shell_games_patches_ready(&s) && !kui_shell_games_patches_ready(NULL));
    open_patch_options();
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.games_patch_selected==5);
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && !s.games_patch_selected);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_launch_patches.entries[0].enabled);
    assert(strstr(s.games_launch_notice,"valid boot-file address"));
    const unsigned ignored[]={KUI_SHELL_START,KUI_SHELL_START|KUI_SHELL_A,KUI_SHELL_UP|KUI_SHELL_DOWN,
        KUI_SHELL_A|KUI_SHELL_RIGHT,KUI_SHELL_LEFT|KUI_SHELL_RIGHT};
    for(unsigned i=0;i<sizeof(ignored)/sizeof(*ignored);i++) {
        assert(press(ignored[i],false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_PATCHES && !s.games_patch_editing);
        assert_patch(0,0,0,false);
    }
    edit_patch_word(1,UINT32_C(0x8c01007c));
    uint32_t draft=s.games_patch_draft;
    for(unsigned i=0;i<sizeof(ignored)/sizeof(*ignored);i++)
        assert(press(ignored[i],false)==KUI_SHELL_NONE && s.games_patch_editing && s.games_patch_draft==draft);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.games_patch_digit==6);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_patch_editing && s.page==KUI_SHELL_GAMES_PATCHES);
    assert_patch(0,0,0,false);
    edit_patch_word(1,UINT32_C(0x8c01007c));
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.games_patch_editing);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_patch_editing);
    assert_patch(0,UINT32_C(0x8c01007c),0,false);
    s.games_patch_selected=0;
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_SAVE_GAME_LAUNCH);
    assert_patch(0,UINT32_C(0x8c01007c),0,true); /* Zero is a valid word. */
    edit_patch_word(2,UINT32_C(0xcafebabe));assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH);
    edit_patch_word(4,UINT32_C(0x0c010000));assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH);
    edit_patch_word(5,UINT32_C(0x89abcdef));assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH);
    s.games_patch_selected=3;assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH);
    assert_patch(0,UINT32_C(0x8c01007c),UINT32_C(0xcafebabe),true);
    assert_patch(1,UINT32_C(0x0c010000),UINT32_C(0x89abcdef),true);
    /* The final 3 bytes of this boot file cannot accept a full word, even
     * though the address is aligned and lies inside its final SD sector. */
    const uint32_t invalid[]={UINT32_C(0x8c010080),UINT32_C(0x8c010001),UINT32_C(0xa05f8000)};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        edit_patch_word(1,invalid[i]);assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.games_patch_editing);
        assert(strstr(s.games_launch_notice,"aligned address inside this boot file"));
        assert_patch(0,UINT32_C(0x8c01007c),UINT32_C(0xcafebabe),true);
        assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.games_patch_editing);
    }
    edit_patch_word(4,UINT32_C(0xac01007c));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.games_patch_editing);
    assert_patch(1,UINT32_C(0x0c010000),UINT32_C(0x89abcdef),true);
    press(KUI_SHELL_B,false);
    struct kui_game_launch_settings saved,other={.reader=KUI_GAME_READER_EAGER,.encoding=KUI_GAME_ENCODING_PLAIN};
    other.patches.entries[0]=(struct kui_game_ram_patch){.address=UINT32_C(0x8c010004),.value=17,.enabled=true};
    assert(kui_shell_game_launch_settings(&s,&saved));kui_game_launch_table_clear(&table);
    const char *other_path="/Games/Other/disc.gdi";
    assert(kui_game_launch_table_upsert(&table,detail.path,&saved));assert(kui_game_launch_table_upsert(&table,other_path,&other));
    size_t bytes=0;uint64_t sequence=0;
    assert(kui_game_launch_settings_encode(record,sizeof(record),&bytes,&table,9));
    assert(kui_game_launch_settings_decode(&decoded,&sequence,record,bytes) && sequence==9);
    install_launch_detail(&detail);struct kui_game_launch_settings hydrated;
    assert(kui_game_launch_table_find(&decoded,detail.path,&hydrated));
    kui_shell_set_game_launch_settings(&s,detail.path,&hydrated,true);
    same_launch_settings(&saved,&hydrated);assert_patch(0,UINT32_C(0x8c01007c),UINT32_C(0xcafebabe),true);
    assert_patch(1,UINT32_C(0x0c010000),UINT32_C(0x89abcdef),true);
    kui_shell_set_game_launch_settings(&s,other_path,&other,true);
    assert_patch(0,UINT32_C(0x8c01007c),UINT32_C(0xcafebabe),true);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE);s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_launch_cdda);
    assert(strstr(s.games_launch_notice,"Disable RAM patches"));
    s.games_launch_selected=KUI_SHELL_GAME_OPTION_READER;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && kui_shell_games_patches_ready(&s));
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_ASYNC);
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_SPEED;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_reader==KUI_RETAIL_READER_ASYNC_EAGER);
    press(KUI_SHELL_B,false);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_ASYNC_EAGER);
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_PATCHES;press(KUI_SHELL_A,false);
    /* A stale/out-of-range peer must never prevent switching a patch Off. */
    s.games_launch_patches.entries[1].address=UINT32_C(0x8c010080);s.games_patch_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_launch_patches.entries[0].enabled);
    press(KUI_SHELL_B,false);press(KUI_SHELL_B,false);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    assert(strstr(s.games_launch_notice,"do not fit this native executable"));
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_PATCHES;press(KUI_SHELL_A,false);s.games_patch_selected=3;
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_launch_patches.entries[1].enabled);
    press(KUI_SHELL_B,false);press(KUI_SHELL_B,false);s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_cdda && !kui_shell_games_patches_ready(&s));
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_PATCHES;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
    detail=launch_detail(true,false);install_launch_detail(&detail);saved.cdda=false;
    kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
    assert(!kui_shell_games_patches_ready(&s) && !kui_game_ram_patches_enabled(&s.games_launch_patches));
    assert_patch(0,UINT32_C(0x8c01007c),UINT32_C(0xcafebabe),false);
    press(KUI_SHELL_Y,false);open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_PATCHES;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
    puts("PASS Games RAM patches: two-slot controller editor, guards, exact file bounds, aliases, saved reload and native/CE/CD music isolation");
}
static void games_image_defaults_controls(void) {
    static struct kui_game_launch_table table;
    const char *other_path="/Games/Unrelated/disc.gdi";
    struct kui_game_launch_settings other={.reader=KUI_GAME_READER_EAGER,.cdda=true,.encoding=KUI_GAME_ENCODING_SCRAMBLED};
    other.patches.entries[1]=(struct kui_game_ram_patch){.address=UINT32_MAX,.value=9};
    kui_game_launch_table_clear(&table);assert(kui_game_launch_table_upsert(&table,other_path,&other));
    for(unsigned kind=0;kind<4;kind++) {
        struct kui_games_detail detail=launch_detail(kind==1,kind>=2);detail.scrambled=kind==3;
        struct kui_game_launch_settings custom={.reader=KUI_GAME_READER_EAGER,.cdda=kind==1,.encoding=KUI_GAME_ENCODING_PLAIN};
        custom.patches.entries[0]=(struct kui_game_ram_patch){.address=UINT32_C(0x8c010004),.value=UINT32_MAX,.enabled=kind!=1};
        install_launch_detail(&detail);kui_shell_set_game_launch_settings(&s,detail.path,&custom,true);
        assert(s.games_launch_patches.entries[0].enabled==(kind!=1));
        press(KUI_SHELL_Y,false);open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_DEFAULTS;
        struct kui_game_launch_settings before,after,unrelated;
        assert(kui_shell_game_launch_settings(&s,&before));
        const unsigned guards[]={KUI_SHELL_START,KUI_SHELL_LEFT,KUI_SHELL_RIGHT,KUI_SHELL_A|KUI_SHELL_START};
        for(unsigned i=0;i<sizeof(guards)/sizeof(*guards);i++) {
            assert(press(guards[i],false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
            assert(kui_shell_game_launch_settings(&s,&after));same_launch_settings(&before,&after);
        }
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
        assert(s.games_launch_reader==(kind==1?KUI_GAME_READER_BACKGROUND:KUI_GAME_READER_STANDARD));
        assert(!s.games_launch_cdda && s.games_launch_encoding==KUI_GAME_ENCODING_AUTO && s.games_retail_scrambled==(kind==3));
        assert_patch(0,0,0,false);assert_patch(1,0,0,false);
        assert(kui_shell_game_launch_settings(&s,&after));assert(kui_game_launch_table_upsert(&table,detail.path,&after));
        assert(kui_game_launch_table_find(&table,other_path,&unrelated));same_launch_settings(&other,&unrelated);
        assert(s.saved.crc_only==defaults.crc_only && s.saved.end_readback==defaults.end_readback &&
            s.saved.show_memory==defaults.show_memory && s.saved.capture_format==defaults.capture_format);
        install_launch_detail(&detail);assert(kui_game_launch_table_find(&table,detail.path,&after));
        kui_shell_set_game_launch_settings(&s,detail.path,&after,true);
        assert(s.games_launch_reader==(kind==1?KUI_GAME_READER_BACKGROUND:KUI_GAME_READER_STANDARD));
        assert(!s.games_launch_cdda && s.games_launch_encoding==KUI_GAME_ENCODING_AUTO && s.games_retail_scrambled==(kind==3));
        assert_patch(0,0,0,false);assert_patch(1,0,0,false);
    }
    puts("PASS Games defaults: current-image save only, detected native/CE/scrambled defaults, cleared patches and preserved unrelated presets");
}
static struct kui_games_detail zso_detail(void) {
    struct kui_games_detail detail=launch_detail(false,true);
    strcpy(detail.path,"/Games/Generated ZSO/disc.zso");strcpy(detail.title,"GENERATED ZSO SAMPLE");
    detail.format=KUI_GAME_IMAGE_ZSO;detail.tracks=detail.data_tracks=1;detail.audio_tracks=0;
    detail.audio=(struct kui_game_audio_info){KUI_GAME_AUDIO_UNKNOWN,0,0,0};
    return detail;
}
static void games_zso_controls(void) {
    struct kui_games_detail detail=zso_detail();
    struct kui_game_launch_settings saved={.reader=KUI_GAME_READER_EAGER,.cdda=true,.encoding=KUI_GAME_ENCODING_PLAIN};
    saved.patches.entries[0]=(struct kui_game_ram_patch){.address=UINT32_C(0x8c010004),.value=UINT32_C(0x12345678),.enabled=true};
    install_launch_detail(&detail);
    /* Metadata from SCIF/another source has no explicit native SCI capability.
     * It is inspectable without accidentally inheriting the ordinary ISO path. */
    assert(kui_shell_games_image_ready(&s) && !kui_shell_games_launch_ready(&s));
    assert(!kui_shell_games_retail_ready(&s) && !kui_shell_games_ce_probe_ready(&s));
    assert(!kui_shell_games_background_ready(&s) && !kui_shell_games_background_ready(NULL));
    assert(!strcmp(kui_shell_games_reader_label(&s),"Standard"));
    kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
    assert(!s.games_launch_reader && !s.games_launch_cdda && !kui_game_ram_patches_enabled(&s.games_launch_patches));
    assert(strstr(s.games_launch_notice,"Saved RAM patches are unavailable"));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_INFO;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_INFO);
    /* CE remains inspection-only even if a stale or malformed publication
     * supplies zso_launch; it cannot fall through to the CE probe route. */
    detail=launch_detail(true,false);detail.format=KUI_GAME_IMAGE_ZSO;detail.zso_launch=true;
    strcpy(detail.path,"/Games/Generated CE/disc.zso");install_launch_detail(&detail);
    assert(kui_shell_games_image_ready(&s) && !kui_shell_games_launch_ready(&s) && !kui_shell_games_ce_probe_ready(&s));
    assert(s.games_launch_reader==KUI_GAME_READER_STANDARD && !kui_shell_games_cdda_ready(&s));
    kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
    assert(!s.games_launch_reader && !s.games_launch_cdda && !kui_game_ram_patches_enabled(&s.games_launch_patches));
    press(KUI_SHELL_Y,false);assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_DEFAULTS;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_reader==KUI_GAME_READER_STANDARD);
    detail=zso_detail();detail.zso_launch=true;install_launch_detail(&detail);
    assert(kui_shell_games_retail_ready(&s) && kui_shell_games_launch_ready(&s) && kui_shell_games_encoding_ready(&s));
    assert(!kui_shell_games_background_ready(&s) && !kui_shell_games_patches_ready(&s) && !kui_shell_games_cdda_ready(&s));
    kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
    assert(s.games_launch_reader==KUI_GAME_READER_STANDARD && !s.games_launch_cdda && !s.games_retail_scrambled);
    assert(s.games_launch_encoding==KUI_GAME_ENCODING_PLAIN && strstr(s.games_launch_notice,"Saved RAM patches are unavailable"));
    assert_patch(0,UINT32_C(0x8c010004),UINT32_C(0x12345678),false);
    struct kui_game_launch_settings sanitized;
    assert(kui_shell_game_launch_settings(&s,&sanitized) && sanitized.reader==KUI_GAME_READER_STANDARD && !sanitized.cdda);
    assert(!sanitized.patches.entries[0].enabled);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_RETAIL_CONFIRM);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_GAMES_RETAIL_BOOT_PLAIN);
    press(KUI_SHELL_Y,false);
    const unsigned unavailable[]={KUI_SHELL_GAME_OPTION_READER,KUI_SHELL_GAME_OPTION_CDDA};
    for(unsigned i=0;i<sizeof(unavailable)/sizeof(*unavailable);i++) {
        s.games_launch_selected=unavailable[i];assert(!kui_shell_games_option_ready(&s,unavailable[i]));
        const unsigned controls[]={KUI_SHELL_A,KUI_SHELL_LEFT,KUI_SHELL_RIGHT};
        for(unsigned j=0;j<sizeof(controls)/sizeof(*controls);j++)
            assert(press(controls[j],false)==KUI_SHELL_NONE && !s.games_launch_reader && !s.games_launch_cdda);
    }
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_SPEED;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_launch_reader);
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_PATCHES;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_ENCODING;
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_retail_scrambled);
    assert(s.games_launch_encoding==KUI_GAME_ENCODING_SCRAMBLED);
    press(KUI_SHELL_B,false);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_GAMES_RETAIL_DESCRAMBLE);
    /* Dispatch still forces Standard if a stale state somehow retained a
     * background reader; it never selects an unsupported ZSO runtime. */
    s.games_launch_reader=KUI_GAME_READER_EAGER;
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_GAMES_RETAIL_DESCRAMBLE);
    s.games_detail.scrambled=true;open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_DEFAULTS;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_launch_reader);
    assert(!s.games_launch_cdda && s.games_launch_encoding==KUI_GAME_ENCODING_AUTO && s.games_retail_scrambled);
    assert_patch(0,0,0,false);assert_patch(1,0,0,false);
    s.games_detail.native_cd=false;
    assert(!kui_shell_games_retail_ready(&s) && !kui_shell_games_launch_ready(&s));
    s.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
    puts("PASS Games ZSO: explicit native SCI capability, inspection-only CE/other sources, Standard-only dispatch and saved-option sanitization");
}
static void games_codec_controls(void) {
    struct kui_shell_view view={0};struct kui_games_detail detail=zso_detail();detail.zso_launch=true;
    install_launch_detail(&detail);
    assert(kui_shell_games_zso_selected(&s) && s.games_launch_codec==KUI_ZSO_LZ4);
    s.games_launch_codec=KUI_ZSO_LZO1X;assert(!kui_shell_games_launch_ready(&s));
    s.games_launch_codec=2;assert(!kui_shell_games_launch_ready(&s));s.games_launch_codec=KUI_ZSO_LZ4;
    press(KUI_SHELL_Y,false);s.games_launch_selected=KUI_SHELL_GAME_OPTION_CODEC;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_CODEC);
    press(KUI_SHELL_DOWN,false);
    assert(s.games_codec_selected==KUI_ZSO_LZO1X && s.games_launch_codec==KUI_ZSO_LZ4);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_INSPECT && s.games_codec_override &&
        s.games_launch_codec==KUI_ZSO_LZO1X && s.page==KUI_SHELL_GAMES_DETAIL && !s.games_detail.valid);
    detail.zso_codec=KUI_ZSO_LZO1X;kui_shell_set_games_detail(&s,&detail);
    struct kui_game_launch_settings saved={.codec=KUI_ZSO_LZO1X};
    kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
    press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==(KUI_GAMES_RETAIL_LZO|KUI_GAMES_RETAIL_BOOT_PLAIN));
    /* A decode failure must still offer the explicit setting, but never launch. */
    detail.valid=false;install_launch_detail(&detail);
    kui_shell_set_game_launch_settings(&s,detail.path,&saved,true);
    assert(s.games_launch_codec==KUI_ZSO_LZO1X && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    render(&view);assert(strstr(drawn,"Y Select ZSO codec"));
    press(KUI_SHELL_Y,false);assert(s.page==KUI_SHELL_GAMES_CODEC);render(&view);
    assert(strstr(drawn,"Standard LZ4") && strstr(drawn,"Historical LZO1X") && strstr(drawn,"never") && strstr(drawn,"retried"));
    press(KUI_SHELL_UP,false);press(KUI_SHELL_B,false);
    assert(s.games_launch_codec==KUI_ZSO_LZO1X && !s.games_codec_override);
    press(KUI_SHELL_X,false);assert(!s.games_codec_override); /* ordinary reinspection loads saved codec */
    strcpy(s.games_selected_path,"/Games/plain.gdi");s.games_detail.valid=false;
    assert(!kui_shell_games_zso_selected(&s));press(KUI_SHELL_Y,false);assert(s.page==KUI_SHELL_GAMES_DETAIL);
    puts("PASS explicit ZSO codecs: chooser before valid inspection, cancel/no launch, saved hydration and typed LZO dispatch");
}
static void games_check_baseline_controls(void) {
    struct kui_games_detail detail=launch_detail(false,true);
    install_launch_detail(&detail);
    struct kui_game_launch_settings previous={.reader=KUI_GAME_READER_EAGER,.encoding=KUI_GAME_ENCODING_PLAIN};
    previous.patches.entries[0]=(struct kui_game_ram_patch){0x8c010004u,0x12345678u,true};
    kui_shell_set_game_launch_settings(&s,detail.path,&previous,true);
    press(KUI_SHELL_Y,false);open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_BASELINE;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.games_baseline);
    assert(s.games_launch_reader==KUI_GAME_READER_STANDARD && !s.games_launch_cdda &&
        s.games_launch_encoding==KUI_GAME_ENCODING_AUTO && !kui_game_ram_patches_enabled(&s.games_launch_patches));
    struct kui_shell_view view={0};render(&view);
    assert(strstr(drawn,"Temporary baseline") && strstr(drawn,"not saved"));
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_DEFAULTS;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.games_baseline);
    press(KUI_SHELL_B,false);s.games_launch_selected=KUI_SHELL_GAME_OPTION_READER;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.games_launch_reader==KUI_GAME_READER_BACKGROUND);
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_BASELINE;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !s.games_baseline);
    assert(s.games_launch_reader==previous.reader && s.games_launch_encoding==previous.encoding);
    assert_patch(0,0x8c010004u,0x12345678u,true);
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_CHECK;
    assert(press(KUI_SHELL_A|KUI_SHELL_START,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_CHECK_EXECUTABLE && s.page==KUI_SHELL_GAMES_CHECK);
    assert(s.games_retail_reader==(KUI_GAME_READER_EAGER|KUI_GAMES_RETAIL_BOOT_PLAIN) && !s.games_baseline);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && press(KUI_SHELL_B,true)==KUI_SHELL_STOP);
    struct kui_app_status result={.complete=true,.passed=true};strcpy(result.message,"Executable preparation passed. The game was not started.");
    view.app_status=&result;render(&view);
    assert(strstr(drawn,"Games / Check executable") && strstr(drawn,"not started") && !strstr(drawn,"Power cycle"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_EXTRA_OPTIONS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_CHECK_EXECUTABLE);
    detail=zso_detail();detail.zso_launch=true;detail.zso_codec=KUI_ZSO_LZO1X;install_launch_detail(&detail);
    press(KUI_SHELL_Y,false);open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_BASELINE;press(KUI_SHELL_A,false);
    assert(s.games_baseline && s.games_launch_codec==KUI_ZSO_LZO1X);
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_CHECK;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_CHECK_EXECUTABLE && (s.games_retail_reader&KUI_GAMES_RETAIL_LZO));
    detail=launch_detail(true,false);install_launch_detail(&detail);press(KUI_SHELL_Y,false);open_extra_options();
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_BASELINE;press(KUI_SHELL_A,false);
    assert(s.games_baseline && s.games_launch_reader==KUI_GAME_READER_BACKGROUND && !s.games_launch_cdda);
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_CHECK;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_CHECK_EXECUTABLE &&
        s.games_retail_reader==(KUI_GAMES_RETAIL_CE_PROBE|KUI_RETAIL_READER_ASYNC));
    s.games_detail.valid=false;s.page=KUI_SHELL_GAMES_EXTRA_OPTIONS;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    puts("PASS Games Check/baseline: read-only preparation dispatch, Stop, unsaved edits/defaults, exact restore, CE and typed ZSO choices");
}
static void games_page(unsigned count,bool more,unsigned view) {
    struct kui_games_page page={.count=count,.has_more=more,.view=view,.total=count};
    strcpy(page.root,s.games_path);
    for(unsigned i=0;i<count;i++) {
        snprintf(page.entries[i].name,sizeof(page.entries[i].name),"Game %u",i);
        snprintf(page.entries[i].path,sizeof(page.entries[i].path),"%s/Game %u/disc.gdi",s.games_path,i);
        page.entries[i].cover=true;
    }
    kui_shell_set_games_listing(&s,&page);
}
/* Background titles reach only the rows on screen that still show the same
 * image; a turned page, another folder or a loading page takes none. */
static void games_background_titles(void) {
    reset(KUI_SHELL_HOME);
    s.home_selected=home_index(KUI_SHELL_GAMES);assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST);
    games_page(4,true,KUI_GAMES_VIEW_LIST);s.games_selected=2;
    struct kui_games_page found=s.games_listing;
    snprintf(found.entries[1].title,sizeof(found.entries[1].title),"GAME ONE");
    snprintf(found.entries[3].title,sizeof(found.entries[3].title),"GAME THREE");
    strcpy(found.entries[3].path,"/Games/Other/disc.gdi");          /* Not the image shown. */
    assert(!kui_shell_set_games_titles(&s,&found,KUI_GAMES_ROWS));    /* Another page. */
    char root[sizeof(found.root)];strcpy(root,found.root);strcpy(found.root,"/Games/Fighting");
    assert(!kui_shell_set_games_titles(&s,&found,0));strcpy(found.root,root);
    s.games_loading=true;assert(!kui_shell_set_games_titles(&s,&found,0));s.games_loading=false;
    assert(kui_shell_set_games_titles(&s,&found,0));
    assert(!strcmp(s.games_listing.entries[1].title,"GAME ONE") && !s.games_listing.entries[3].title[0]);
    assert(s.games_selected==2 && s.games_listing.count==4 && !s.games_loading);
    assert(!kui_shell_set_games_titles(&s,&found,0));                 /* Nothing new. */
}
static void games_views(void) {
    reset(KUI_SHELL_HOME);
    assert(s.games_view==KUI_GAMES_VIEW_SAVED && kui_shell_games_view(&s)==KUI_GAMES_VIEW_LIST);
    s.home_selected=home_index(KUI_SHELL_GAMES);assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST);
    /* The first listing reports the card's saved view; a failed one does not. */
    games_page(8,true,KUI_GAMES_VIEW_SAVED);assert(s.games_view==KUI_GAMES_VIEW_SAVED);
    games_page(8,true,KUI_GAMES_VIEW_GALLERY);assert(s.games_view==KUI_GAMES_VIEW_GALLERY);
    /* Y moves to the next view on the same page and keeps the selection. */
    s.games_selected=5;
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_GAMES_LIST && s.games_view==KUI_GAMES_VIEW_LIST);
    assert(s.games_selected==5 && s.games_listing.count==8 && !s.games_page && s.games_loading);
    assert(!s.games_refresh);
    assert(press(KUI_SHELL_Y,true)==KUI_SHELL_NONE && s.games_view==KUI_GAMES_VIEW_LIST);
    games_page(8,true,KUI_GAMES_VIEW_LIST);assert(s.games_selected==5);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_GAMES_LIST && s.games_view==KUI_GAMES_VIEW_COMPACT);
    games_page(8,true,KUI_GAMES_VIEW_COMPACT);
    /* Compact: columns of four; across is the other column, then a page. */
    s.games_selected=1;
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.games_selected==5);
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.games_selected==6);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.games_selected==2);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.games_selected==2); /* First page. */
    s.games_selected=6;
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_GAMES_LIST && s.games_page==1 && s.games_selected==2);
    assert(s.games_turning && !s.games_shown_page && s.games_shown_selected==6);
    games_page(3,false,KUI_GAMES_VIEW_COMPACT);assert(s.games_selected==2 && !s.games_turning);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.games_page==1); /* Last page; no cell. */
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_GAMES_LIST && !s.games_page && s.games_selected==6);
    games_page(8,true,KUI_GAMES_VIEW_COMPACT);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_GAMES_LIST && s.games_view==KUI_GAMES_VIEW_GALLERY);
    games_page(8,true,KUI_GAMES_VIEW_GALLERY);
    /* Gallery: rows of four; across moves along a row, then a page. */
    s.games_selected=5;
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.games_selected==4);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.games_selected==0);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.games_selected==4);
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.games_selected==0);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.games_selected==0);
    s.games_selected=7;
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_GAMES_LIST && s.games_page==1 && s.games_selected==4);
    games_page(6,true,KUI_GAMES_VIEW_GALLERY);assert(s.games_selected==4);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.games_selected==5);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_GAMES_LIST && s.games_page==2 && s.games_selected==4);
    games_page(2,false,KUI_GAMES_VIEW_GALLERY);assert(!s.games_selected); /* Short page clamps. */
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && !s.games_selected);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_GAMES_LIST && s.games_page==1 && s.games_selected==3);
    games_page(8,true,KUI_GAMES_VIEW_GALLERY);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_INSPECT && !strcmp(s.games_selected_path,"/Games/Game 3/disc.gdi"));
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_GAMES);
    /* Stale or unsafe rows never draw a cover; titles are always ended. */
    struct kui_games_page page={.count=2,.view=KUI_GAMES_VIEW_GALLERY};
    strcpy(page.root,"/Games");
    strcpy(page.entries[0].name,"One");strcpy(page.entries[0].path,"/Games/One/disc.gdi");
    memset(page.entries[0].title,'T',sizeof(page.entries[0].title));page.entries[0].cover=true;
    strcpy(page.entries[1].name,"Two");strcpy(page.entries[1].path,"/Elsewhere/disc.gdi");page.entries[1].cover=true;
    kui_shell_set_games_listing(&s,&page);
    assert(strlen(s.games_listing.entries[0].title)==KUI_COVER_TITLE_CAP-1 && s.games_listing.entries[0].cover);
    assert(s.games_listing.entries[1].disabled && !s.games_listing.entries[1].cover);
    /* Box art scan: from Games > Advanced; the next listing ends it. */
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_ADVANCED);
    press(KUI_SHELL_DOWN,false);press(KUI_SHELL_DOWN,false);assert(s.games_advanced_selected==2);
    strcpy(s.games_path,"/Games/Fighting");s.games_page=3;
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_SCAN && s.page==KUI_SHELL_GAMES && s.games_scanning);
    assert(!strcmp(s.games_path,"/Games") && !s.games_page && s.games_listing.count==2 && s.games_loading && s.games_listing.message[0]);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,true)==KUI_SHELL_STOP && s.games_scanning && s.page==KUI_SHELL_GAMES);
    games_page(8,true,KUI_GAMES_VIEW_GALLERY);assert(!s.games_scanning && s.games_listing.count==8);
}
static void games_async_publication(void) {
    reset(KUI_SHELL_HOME);s.home_selected=home_index(KUI_SHELL_GAMES);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST);
    unsigned generation=s.games_generation,view=s.games_view;
    struct kui_games_page page={.count=2,.has_more=true,.view=KUI_GAMES_VIEW_LIST};
    strcpy(page.root,"/Games");
    strcpy(page.entries[0].name,"One");strcpy(page.entries[0].path,"/Games/One/disc.gdi");
    strcpy(page.entries[1].name,"Two");strcpy(page.entries[1].path,"/Games/Two/disc.gdi");
    struct kui_shell_games_art_key first,current;
    assert(!kui_shell_games_art_key(&s,&first));
    assert(!kui_shell_set_games_rows(&s,&page,generation-1,0,view));
    assert(!kui_shell_set_games_rows(&s,&page,generation,8,view));
    assert(!kui_shell_set_games_rows(&s,&page,generation,0,KUI_GAMES_VIEW_GALLERY));
    assert(kui_shell_set_games_rows(&s,&page,generation,0,view) && !s.games_loading);
    assert(kui_shell_games_art_key(&s,&first));
    struct kui_games_cover_result art={.size=KUI_COVER_SIZE_LARGE,.cover=true};
    strcpy(art.path,page.entries[0].path);strcpy(art.name,page.entries[0].name);strcpy(art.title,"ONE TITLE");
    s.games_art_loading=true;
    assert(kui_shell_set_games_art(&s,&first,&art) && !s.games_art_loading && s.games_listing.entries[0].cover);
    assert(!strcmp(s.games_listing.entries[0].title,"ONE TITLE"));
    /* Idle cover work never locks navigation. An old result cannot repaint
     * the newly selected game, even after returning to the same path. */
    s.games_art_loading=true;
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.games_selected==1 && !s.games_art_loading);
    assert(!s.games_listing.entries[0].cover && !kui_shell_set_games_art(&s,&first,&art));
    assert(kui_shell_games_art_key(&s,&current) && !kui_shell_games_art_same(&first,&current));
    strcpy(art.path,page.entries[1].path);strcpy(art.name,page.entries[1].name);strcpy(art.title,"TWO TITLE");
    art.size=KUI_COVER_SIZE_SMALL;assert(!kui_shell_set_games_art(&s,&current,&art));
    art.size=KUI_COVER_SIZE_LARGE;strcpy(art.name,"One");assert(!kui_shell_set_games_art(&s,&current,&art));
    strcpy(art.name,"Two");art.stopped=true;assert(!kui_shell_set_games_art(&s,&current,&art));
    art.stopped=false;assert(kui_shell_set_games_art(&s,&current,&art));
    assert(!s.games_listing.entries[0].cover && s.games_listing.entries[1].cover);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.games_selected==0);
    strcpy(art.path,page.entries[0].path);strcpy(art.name,page.entries[0].name);
    assert(!kui_shell_set_games_art(&s,&first,&art));
    assert(kui_shell_games_art_key(&s,&current));
    struct kui_shell_games_art_key malformed=current;
    memset(malformed.path,'x',sizeof(malformed.path));assert(!kui_shell_set_games_art(&s,&malformed,&art));
    memset(art.title,'x',sizeof(art.title));assert(!kui_shell_set_games_art(&s,&current,&art));
    strcpy(art.title,"ONE TITLE");art.cover=false;
    assert(kui_shell_set_games_art(&s,&current,&art) && !s.games_listing.entries[0].cover);
    /* Keep visible rows during paging/refresh but never open or decorate
     * them as if they were the newly requested page. */
    generation=s.games_generation;view=s.games_view;
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_GAMES_LIST && s.games_loading && s.games_listing.count==2);
    assert(!kui_shell_games_art_key(&s,&current));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES);
    assert(!kui_shell_set_games_rows(&s,&page,generation,0,view));
    generation=s.games_generation;
    assert(!kui_shell_set_games_rows(&s,&page,generation,0,view));
    assert(kui_shell_set_games_rows(&s,&page,generation,8,view));
    generation=s.games_generation;
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_GAMES_LIST && s.games_view==KUI_GAMES_VIEW_COMPACT);
    assert(!kui_shell_set_games_rows(&s,&page,generation,8,view));
    page.view=KUI_GAMES_VIEW_COMPACT;
    assert(kui_shell_set_games_rows(&s,&page,s.games_generation,8,s.games_view));
    assert(kui_shell_games_art_key(&s,&current));art.size=KUI_COVER_SIZE_SMALL;art.cover=true;
    assert(kui_shell_set_games_art(&s,&current,&art));
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_GAMES_LIST);
    assert(!kui_shell_set_games_art(&s,&current,&art));
    page.view=KUI_GAMES_VIEW_GALLERY;
    assert(kui_shell_set_games_rows(&s,&page,s.games_generation,8,s.games_view));
    assert(kui_shell_games_art_key(&s,&current));art.size=KUI_COVER_SIZE_MEDIUM;
    assert(kui_shell_set_games_art(&s,&current,&art));
    generation=s.games_generation;view=s.games_view;
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_GAMES_LIST && s.games_refresh);
    assert(!kui_shell_set_games_rows(&s,&page,generation,8,view));
    assert(kui_shell_set_games_rows(&s,&page,s.games_generation,8,s.games_view));
    strcpy(page.root,"/Other");assert(!kui_shell_set_games_rows(&s,&page,s.games_generation,8,s.games_view));
    /* Launch/inspect the current row without waiting for its artwork. */
    s.games_art_loading=true;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_INSPECT && !s.games_art_loading);
    assert(!strcmp(s.games_selected_path,"/Games/One/disc.gdi"));
    assert(!kui_shell_set_games_art(&s,&current,&art));
}
static void games_rendering(void) {
    struct kui_shell_view view={0};
    reset(KUI_SHELL_HOME);s.home_selected=home_index(KUI_SHELL_GAMES);render(&view);
    assert(strstr(drawn,"Games") && strstr(drawn,"8 applications") && strstr(drawn,"V" KUI_VERSION ": compatibility varies"));
    reset(KUI_SHELL_GAMES);s.games_listing.count=8;s.games_listing.has_more=true;s.games_selected=7;
    for(unsigned i=0;i<8;i++) {
        snprintf(s.games_listing.entries[i].name,sizeof(s.games_listing.entries[i].name),"Game %u with a long but bounded name",i+1);
        s.games_listing.entries[i].directory=i==0;
    }
    strcpy(s.games_listing.message,"Choose a game image to inspect.");render(&view);
    assert(strstr(drawn,"Game 8") && strstr(drawn,"DIR"));
    assert(strstr(drawn,"Page 1 +") && strstr(drawn,"START More") && strstr(drawn,"Y View") && !strstr(drawn,"A Launch"));
    /* Every view names the entries; a GDI without box art gets a hint. */
    s.games_listing.total=19;render(&view);assert(strstr(drawn,"Page 1 of 3") && strstr(drawn,"19 items"));
    assert(strstr(drawn,"No box art yet"));
    s.games_listing.artwork=true;render(&view);assert(!strstr(drawn,"press START") && strstr(drawn,"Choose a game"));
    strcpy(s.games_listing.entries[7].title,"GAME EIGHT TITLE");render(&view);
    assert(strstr(drawn,"GAME EIGHT TITLE") && !strstr(drawn,"Game 8"));
    s.games_view=KUI_GAMES_VIEW_COMPACT;render(&view);
    assert(strstr(drawn,"GAME EIGHT TITLE") && strstr(drawn,"Game 8") && strstr(drawn,"Folder") && strstr(drawn,"D-pad Move"));
    s.games_view=KUI_GAMES_VIEW_GALLERY;render(&view);assert(strstr(drawn,"GAME EIGHT") && strstr(drawn,"D-pad Move"));
    s.games_scanning=true;view.busy=true;render(&view);
    assert(strstr(drawn,"Scanning for box art") && strstr(drawn,"B stops safely"));
    s.games_scanning=false;view.busy=false;s.games_view=KUI_GAMES_VIEW_SAVED;
    view.busy=true;render(&view);assert(strstr(drawn,"B Stop safely"));view.busy=false;
    reset(KUI_SHELL_GAMES_DETAIL);strcpy(s.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
    strcpy(s.games_detail.path,s.games_selected_path);
    s.games_detail.valid=true;s.games_detail.native_gd=true;s.games_detail.tracks=3;s.games_detail.data_tracks=2;s.games_detail.audio_tracks=1;
    s.games_detail.bytes=1185765648;s.games_detail.boot_bytes=123456;s.games_detail.boot_lba=45166;
    strcpy(s.games_detail.title,"Dead or Alive 2");strcpy(s.games_detail.product,"T-3601N");
    strcpy(s.games_detail.region,"JUE");strcpy(s.games_detail.boot_file,"1ST_READ.BIN");render(&view);
    assert(strstr(drawn,"Dead or Alive 2") && strstr(drawn,"A Launch"));
    assert(!strstr(drawn,"T-3601N") && !strstr(drawn,"1ST_READ.BIN") && !strstr(drawn,"Tracks: 3"));
    assert(!strstr(drawn,"1185765648 bytes") && !strstr(drawn,"LBA"));
    s.page=KUI_SHELL_GAMES_INFO;render(&view);
    assert(strstr(drawn,"T-3601N") && strstr(drawn,"1ST_READ.BIN"));
    assert(strstr(drawn,"Tracks: 3") && strstr(drawn,"1185765648 bytes") && strstr(drawn,"LBA"));
    s.page=KUI_SHELL_GAMES_DETAIL;
    strcpy(s.games_detail.title,"DEAD OR ALIVE 2");render(&view);
    assert(strstr(drawn,"A Launch"));
    strcpy(s.games_detail.title,"ARMADA");render(&view);assert(strstr(drawn,"A Launch"));
    const struct kui_game_audio_info notices[]={
        {KUI_GAME_AUDIO_GD_LOW_DENSITY_ONLY,1,0,1},
        {KUI_GAME_AUDIO_GD_HIGH_DENSITY,2,1,1},{KUI_GAME_AUDIO_CD,1,1,0},
        {KUI_GAME_AUDIO_UNKNOWN,0,0,0}
    };
    for(unsigned i=0;i<sizeof(notices)/sizeof(*notices);i++) {
        s.games_detail.audio=notices[i];render(&view);assert(strstr(drawn,"A Launch"));
        assert(!strstr(drawn,"Boot file") && !strstr(drawn,"LBA") && !strstr(drawn,"launch test"));
    }
    s.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;render(&view);
    assert(strstr(drawn,"CD music: Off") && strstr(drawn,"A Launch"));
    assert(strstr(drawn,"Background loading: Off") && !strstr(drawn,"Boot encoding"));
    s.page=KUI_SHELL_GAMES_DETAIL;
    s.games_detail.audio=(struct kui_game_audio_info){KUI_GAME_AUDIO_NONE,0,0,0};
    s.games_detail.windows_ce=true;render(&view);
    assert(!strstr(drawn,"A Launch") && !strstr(drawn,"CE test") && !strstr(drawn,"Windows CE SCI launch test"));
    s.games_detail.native_gd=false;strcpy(s.games_detail.boot_file,"0WINCEOS.BIN");
    s.games_detail.boot_bytes=1253376;render(&view);
    assert(strstr(drawn,"A Launch game") && !strstr(drawn,"0WINCEOS.BIN") && !strstr(drawn,"boot test"));
    s.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;render(&view);
    assert(strstr(drawn,"Games / Launch game") && strstr(drawn,"ARMADA") && strstr(drawn,"A Launch"));
    assert(strstr(drawn,"Background loading: On") && !strstr(drawn,"standard reader") && !strstr(drawn,"X/Y"));
    s.page=KUI_SHELL_GAMES_DETAIL;s.games_detail.native_gd=true;
    strcpy(s.games_detail.boot_file,"1ST_READ.BIN");s.games_detail.boot_bytes=123456;s.games_detail.windows_ce=false;
    s.games_detail.valid=false;strcpy(s.games_detail.message,"Track file missing");render(&view);
    assert(strstr(drawn,"Could not inspect image") && strstr(drawn,"Track file missing") && !strstr(drawn,"A Launch"));
    s.games_detail.stopped=true;render(&view);assert(strstr(drawn,"Inspection stopped"));
    reset(KUI_SHELL_GAMES_ADVANCED);render(&view);
    assert(strstr(drawn,"Game library") && strstr(drawn,"Browse SD folders") && strstr(drawn,"Developer tests"));
    assert(strstr(drawn,"Scan box art") && !strstr(drawn,"Loader test") && !strstr(drawn,"Resident loader probe"));
    reset(KUI_SHELL_GAMES_TESTS);render(&view);
    assert(strstr(drawn,"Image read test") && strstr(drawn,"Loader read test") && !strstr(drawn,"A Launch"));
    assert(image_test_row_color==0xadd9);
    struct kui_games_detail test_detail=launch_detail(false,true);
    install_launch_detail(&test_detail);s.page=KUI_SHELL_GAMES_TESTS;render(&view);
    assert(image_test_row_color==0xadd9 && strstr(drawn,"Requires an inspected GDI"));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_TESTS);
    test_detail=launch_detail(false,false);install_launch_detail(&test_detail);
    s.page=KUI_SHELL_GAMES_TESTS;render(&view);assert(image_test_row_color==0xf7bf);
    reset(KUI_SHELL_GAMES_PROBE_CONFIRM);render(&view);
    assert(strstr(drawn,"A Start test") && strstr(drawn,"B Tests"));
    assert(strstr(drawn,"Exits this menu") && strstr(drawn,"test data after shutdown"));
    assert(strstr(drawn,"does not launch a retail game") && strstr(drawn,"Photograph the final result"));
    assert(strstr(drawn,"Power cycle to return") && !strstr(drawn,"L Memory"));
    struct kui_app_status status={0};view.app_status=&status;view.busy=true;
    strcpy(status.message,"Validating the probe package");render(&view);
    assert(strstr(drawn,"Preparing the handoff") && strstr(drawn,status.message) && strstr(drawn,"B Stop safely"));
    assert(!strstr(drawn,"A Start test"));
    view.busy=false;reset(KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM);
    strcpy(s.games_selected_path,"/Games/Dead or Alive 2/Dead or Alive 2.gdi");
    strcpy(s.games_detail.path,s.games_selected_path);s.games_detail.valid=true;render(&view);
    assert(strstr(drawn,s.games_selected_path) && strstr(drawn,"A Start test") && strstr(drawn,"B Tests"));
    assert(strstr(drawn,"reads samples from this GDI") && strstr(drawn,"GD requests used by retail games"));
    assert(strstr(drawn,"game itself will not start") && strstr(drawn,"not a full image verification"));
    assert(strstr(drawn,"Photograph the final result, then power cycle") && !strstr(drawn,"L Memory"));
    view.busy=true;strcpy(status.message,"Mapping selected image files");render(&view);
    assert(strstr(drawn,"Preparing the handoff") && strstr(drawn,status.message) && strstr(drawn,"B Stop safely"));
    assert(!strstr(drawn,"A Start test"));view.busy=false;
    strcpy(s.games_detail.path,"/Games/Other.gdi");render(&view);
    assert(strstr(drawn,"Image details changed") && !strstr(drawn,"A Start test"));
    struct kui_games_detail detail=launch_detail(false,false);
    install_launch_detail(&detail);s.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;render(&view);
    assert(strstr(drawn,"Launch game") && strstr(drawn,"A Launch") && strstr(drawn,"B Details"));
    assert(!strstr(drawn,"X Background") && !strstr(drawn,"Boot encoding") && !strstr(drawn,"Loader test"));
    assert(strstr(drawn,"Background loading: Off") && strstr(drawn,"CD music: Off"));
    assert(strstr(drawn,"Power cycle to return") && !strstr(drawn,"L Memory"));
    view.busy=true;strcpy(status.message,"Preparing selected game launch...");render(&view);
    assert(strstr(drawn,"Preparing the launch") && strstr(drawn,"B Stop safely") && !strstr(drawn,"A Launch"));
    view.busy=false;s.games_detail.native_gd=false;render(&view);
    assert(strstr(drawn,"Image details changed") && !strstr(drawn,"A Launch"));
    s.games_detail.native_gd=true;s.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;
    s.games_launch_selected=KUI_SHELL_GAME_OPTION_CDDA;render(&view);
    assert(strstr(drawn,"Background loading") && strstr(drawn,"CD music") && strstr(drawn,"Advanced"));
    assert(!strstr(drawn,"Image read test") && !strstr(drawn,"Loader read test") && !strstr(drawn,"Boot encoding"));
    assert(strstr(drawn,"START Launch") && !strstr(drawn,"X/Y Background"));
    for(unsigned i=0;i<KUI_SHELL_GAME_OPTION_COUNT-1u;i++) assert(option_row_colors[i]==0xf7bf);
    strcpy(s.games_launch_notice,"Saved for this image.");render(&view);assert(strstr(drawn,s.games_launch_notice));
    strcpy(s.games_launch_notice,"Not saved. These choices apply until you leave this image.");
    render(&view);assert(strstr(drawn,"Not saved"));
    view.busy=true;strcpy(status.message,"Stale result from the previous loader probe");
    strcpy(s.games_launch_notice,"Saving options...");render(&view);
    assert(strstr(drawn,"Saving options...") && !strstr(drawn,status.message));
    assert(strstr(drawn,"B Stop safely") && !strstr(drawn,"START Launch"));
    view.busy=false;
    s.games_launch_cdda=true;s.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;render(&view);
    assert(strstr(drawn,"CD music: On") && strstr(drawn,"Background loading: Off"));
    s.page=KUI_SHELL_GAMES_EXTRA_OPTIONS;s.games_launch_cdda=false;
    s.games_launch_reader=KUI_RETAIL_READER_ASYNC_EAGER;s.game_tools_selected=KUI_SHELL_GAME_TOOL_SPEED;render(&view);
    assert(strstr(drawn,"Legacy (Y)") && strstr(drawn,"Background reader") &&
        !strstr(drawn,"Aggressive") && strstr(drawn,"Boot encoding") &&
        strstr(drawn,"Image information") && strstr(drawn,"Developer tests"));
    s.games_detail.native_gd=false;s.games_detail.windows_ce=true;s.games_detail.boot_bytes=1253376;
    s.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;s.games_launch_selected=KUI_SHELL_GAME_OPTION_READER;render(&view);
    assert(strstr(drawn,"On") && strstr(drawn,"required") && strstr(drawn,"CD music") && strstr(drawn,"Off"));
    assert(option_row_colors[KUI_SHELL_GAME_OPTION_READER]==0xadd9 &&
        option_row_colors[KUI_SHELL_GAME_OPTION_CDDA]==0xf7bf &&
        option_row_colors[KUI_SHELL_GAME_OPTION_ADVANCED]==0xf7bf);
    assert(!strstr(drawn,"0WINCEOS.BIN") && !strstr(drawn,"CE test") && !strstr(drawn,"Windows CE SCI launch test"));
    strcpy(s.games_detail.path,"/Games/Other.gdi");render(&view);
    assert(strstr(drawn,"Unavailable"));
    assert(!strstr(drawn,"START Launch"));
    for(unsigned i=0;i<KUI_SHELL_GAME_OPTION_COUNT-1u;i++) assert(option_row_colors[i]==0xadd9);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
}

static void games_native_reader_choices(void) {
    struct kui_shell_view view={0};
    struct kui_games_detail detail=launch_detail(false,false);
    install_launch_detail(&detail);
    assert(s.games_launch_reader==KUI_GAME_READER_STANDARD);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    s.games_launch_selected=KUI_SHELL_GAME_OPTION_READER;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH);
    assert(s.games_launch_reader==KUI_GAME_READER_BACKGROUND && !s.games_retail_reader);
    assert(!strcmp(kui_shell_games_reader_label(&s),"New (X)"));
    for(unsigned reader=KUI_GAME_READER_BACKGROUND;reader<=KUI_GAME_READER_EAGER;reader++) {
        install_launch_detail(&detail);s.games_launch_reader=reader;
        const char *choice=reader==KUI_GAME_READER_BACKGROUND?"New (X)":"Legacy (Y)";
        const char *summary=reader==KUI_GAME_READER_BACKGROUND?
            "Background loading: On - New (X)":"Background loading: On - Legacy (Y)";
        const enum kui_shell_page pages[]={KUI_SHELL_GAMES_DETAIL,KUI_SHELL_GAMES_RETAIL_CONFIRM,
            KUI_SHELL_GAMES_LAUNCH_OPTIONS};
        for(unsigned i=0;i<3u;i++) {
            s.page=pages[i];render(&view);assert(strstr(drawn,choice));
            if(i<2u)assert(strstr(drawn,summary));
            assert(!strstr(drawn,"X Launch") && !strstr(drawn,"Y Launch"));
        }
        s.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;
        assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.games_launch_reader==reader);
        assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
        open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_SPEED;
        render(&view);
        assert(strstr(drawn,"Background reader") && strstr(drawn,choice));
        assert(!strstr(drawn,"Background speed") && !strstr(drawn,"Aggressive"));
        /* Changing the native reader keeps every other saved field. */
        s.games_launch_codec=KUI_ZSO_LZO1X;
        s.games_launch_patches.entries[0]=(struct kui_game_ram_patch){UINT32_C(0x8c010004),17u,true};
        struct kui_game_launch_settings before,after;
        assert(kui_shell_game_launch_settings(&s,&before));
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && !s.games_retail_reader);
        assert(kui_shell_game_launch_settings(&s,&after));
        before.reader=(reader+1u)%KUI_GAME_READER_COUNT;
        same_launch_settings(&before,&after);
        assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_reader==reader);
        assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
        assert(press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==reader);
    }
    /* CE, CDDA and ZSO retain their existing labels and forced choices. */
    detail=launch_detail(true,false);install_launch_detail(&detail);
    s.page=KUI_SHELL_GAMES_EXTRA_OPTIONS;s.game_tools_selected=KUI_SHELL_GAME_TOOL_SPEED;
    render(&view);assert(strstr(drawn,"Background speed") && !strstr(drawn,"Background reader"));
    assert(!strstr(drawn,"New (X)") && !strstr(drawn,"Legacy (Y)"));
    assert(!strcmp(kui_shell_games_reader_label(&s),"Background"));
    detail=launch_detail(false,false);install_launch_detail(&detail);
    s.games_launch_cdda=true;s.page=KUI_SHELL_GAMES_EXTRA_OPTIONS;
    render(&view);assert(strstr(drawn,"Background speed") && !strstr(drawn,"Background reader"));
    assert(!strstr(drawn,"New (X)") && !strstr(drawn,"Legacy (Y)"));
    s.page=KUI_SHELL_GAMES_RETAIL_CONFIRM;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL &&
        s.games_retail_reader==(KUI_RETAIL_READER_STANDARD|KUI_GAMES_RETAIL_CDDA));
    detail.format=KUI_GAME_IMAGE_ZSO;detail.zso_launch=true;detail.zso_codec=KUI_ZSO_LZ4;
    install_launch_detail(&detail);s.page=KUI_SHELL_GAMES_EXTRA_OPTIONS;
    render(&view);assert(strstr(drawn,"Background speed") && !strstr(drawn,"Background reader"));
    assert(!strstr(drawn,"New (X)") && !strstr(drawn,"Legacy (Y)"));
    assert(!strcmp(kui_shell_games_reader_label(&s),"Standard"));
    puts("PASS native reader UI: New X/Legacy Y/Off choices, persisted fields and CE/CDDA/ZSO exclusions");
}
static void games_new_reader_defaults(void) {
    struct kui_shell_view view={0};
    struct kui_games_detail detail=launch_detail(false,false);
    detail.background_default=true;
    strcpy(detail.title,"UNCONFIGURED NATIVE GAME");
    install_launch_detail(&detail);
    assert(s.games_launch_reader==KUI_GAME_READER_BACKGROUND);
    /* A successful empty lookup must not turn the new default into Off.
     * Reading defaults does not request a preset write or launch. */
    kui_shell_set_game_launch_settings(&s,detail.path,NULL,true);
    assert(s.games_launch_reader==KUI_GAME_READER_BACKGROUND && !s.games_retail_reader);
    assert(strstr(s.games_launch_notice,"save automatically") && !strstr(s.games_launch_notice,"could not"));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_RETAIL && s.games_retail_reader==KUI_RETAIL_READER_ASYNC);
    s.page=KUI_SHELL_GAMES_LAUNCH_OPTIONS;
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_SPEED;
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_reader==KUI_GAME_READER_STANDARD);
    render(&view);assert(strstr(drawn,"Background reader") && strstr(drawn,"Off"));
    struct kui_game_launch_settings off;
    assert(kui_shell_game_launch_settings(&s,&off) && off.reader==KUI_GAME_READER_STANDARD);
    static struct kui_game_launch_table table,decoded;
    static uint8_t record[KUI_GAME_LAUNCH_SETTINGS_RECORD_MAX];
    kui_game_launch_table_clear(&table);assert(kui_game_launch_table_upsert(&table,detail.path,&off));
    size_t bytes=0;uint64_t sequence=0;
    assert(kui_game_launch_settings_encode(record,sizeof(record),&bytes,&table,17u));
    assert(kui_game_launch_settings_decode(&decoded,&sequence,record,bytes) && sequence==17u);
    install_launch_detail(&detail);assert(s.games_launch_reader==KUI_GAME_READER_BACKGROUND);
    assert(kui_game_launch_table_find(&decoded,detail.path,&off));
    kui_shell_set_game_launch_settings(&s,detail.path,&off,true);
    assert(s.games_launch_reader==KUI_GAME_READER_STANDARD);
    press(KUI_SHELL_Y,false);open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_SPEED;
    /* Off remains an enabled Advanced control and can be reenabled/saved. */
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_reader==KUI_GAME_READER_BACKGROUND);
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_DEFAULTS;
    s.games_launch_reader=KUI_GAME_READER_STANDARD;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SAVE_GAME_LAUNCH && s.games_launch_reader==KUI_GAME_READER_BACKGROUND);
    /* Explicit persisted Legacy is retained as well. */
    off.reader=KUI_GAME_READER_EAGER;
    kui_shell_set_game_launch_settings(&s,detail.path,&off,true);
    assert(s.games_launch_reader==KUI_GAME_READER_EAGER);
    /* Read failure retains the image default but gives accurate feedback. */
    install_launch_detail(&detail);kui_shell_set_game_launch_settings(&s,detail.path,NULL,false);
    assert(s.games_launch_reader==KUI_GAME_READER_BACKGROUND && strstr(s.games_launch_notice,"could not"));
    for(unsigned kind=0;kind<6u;kind++) {
        struct kui_games_detail other=detail;
        if(kind==0u) other.background_default=false; /* non-SCI inspection */
        if(kind==1u) {other.native_gd=false;other.native_cd=true;other.cd_image=true;other.format=KUI_GAME_IMAGE_CUE;}
        if(kind==2u) other.scrambled=true;
        if(kind==3u) other.format=KUI_GAME_IMAGE_ZSO;
        if(kind==4u) {other.windows_ce=true;other.native_gd=false;}
        if(kind==5u) other.native_gd=false;
        install_launch_detail(&other);kui_shell_set_game_launch_settings(&s,other.path,NULL,true);
        assert(s.games_launch_reader==(kind==4u?KUI_GAME_READER_BACKGROUND:KUI_GAME_READER_STANDARD));
        assert(!s.games_launch_cdda);
    }
    puts("PASS New reader defaults: unsaved SCI/native GDI selects New, Advanced Off persists, explicit presets survive, other routes retain defaults");
}

static void games_patch_rendering(void) {
    struct kui_shell_view view={0};
    struct kui_games_detail detail=launch_detail(false,false);detail.boot_bytes=131;
    install_launch_detail(&detail);press(KUI_SHELL_Y,false);open_extra_options();render(&view);
    const char *tools[]={"Boot encoding","Background reader","Image information","Developer tests","RAM patches","Restore defaults"};
    const char *at=drawn;
    for(unsigned i=0;i<sizeof(tools)/sizeof(*tools);i++) {at=strstr(at,tools[i]);assert(at);at+=strlen(tools[i]);}
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_DEFAULTS;render(&view);
    assert(strstr(drawn,"Save detected defaults for this image") && !strstr(drawn,"START Launch"));
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_PATCHES;press(KUI_SHELL_A,false);render(&view);
    assert(strstr(drawn,"Games / RAM patches") && strstr(drawn,"One-time 32-bit writes"));
    const char *rows[]={"Patch 1 / Enabled","Patch 1 / Address","Patch 1 / Value",
        "Patch 2 / Enabled","Patch 2 / Address","Patch 2 / Value"};
    at=drawn;
    for(unsigned i=0;i<sizeof(rows)/sizeof(*rows);i++) {at=strstr(at,rows[i]);assert(at);at+=strlen(rows[i]);}
    assert(strstr(drawn,"Boot range: 8C010000 - 8C01007C (4-byte aligned)"));
    assert(strstr(drawn,"A Toggle/edit") && !strstr(drawn,"START Launch"));
    edit_patch_word(1,UINT32_C(0x8c01007c));render(&view);
    assert(strstr(drawn,"8C01007C") && strstr(drawn,"LEFT/RIGHT Digit") && strstr(drawn,"A Save   B Cancel"));
    /* The selected nibble has an actual two-pixel amber underline, and
     * moving the cursor relocates it without publishing the draft. */
    unsigned y=186u+29u+21u,x=400u+kui_shell_font_width("8C01007",false);
    unsigned width=kui_shell_font_width("C",false);
    for(unsigned py=y;py<y+2;py++) for(unsigned px=x;px<x+width;px++) assert(pixels[1+py*640+px]==0xfda9);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && !s.games_patch_digit);render(&view);
    width=kui_shell_font_width("8",false);
    for(unsigned py=y;py<y+2;py++) for(unsigned px=400;px<400+width;px++) assert(pixels[1+py*640+px]==0xfda9);
    assert(pixels[1+y*640+x]!=0xfda9);assert_patch(0,0,0,false);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.games_patch_editing);render(&view);
    assert(strstr(drawn,"Edit discarded") && !strstr(drawn,"LEFT/RIGHT Digit"));
    s.page=KUI_SHELL_GAMES_EXTRA_OPTIONS;s.games_launch_cdda=true;render(&view);
    assert(strstr(drawn,"RAM patches") && strstr(drawn,"Unavailable"));
    detail=launch_detail(true,false);install_launch_detail(&detail);press(KUI_SHELL_Y,false);open_extra_options();
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_PATCHES;render(&view);
    assert(strstr(drawn,"RAM patches") && strstr(drawn,"Unavailable") && strstr(drawn,"Native games with CD music Off only"));
    puts("PASS Games patch rendering: six advanced rows, both patch slots, exact range, controller hints and moving nibble underline");
}
static void games_zso_rendering(void) {
    struct kui_shell_view view={0};struct kui_games_detail detail=zso_detail();
    strcpy(detail.message,"ZSO launching requires native SCI storage.");
    install_launch_detail(&detail);render(&view);
    assert(strstr(drawn,"Games / Selected image") && strstr(drawn,detail.message) && !strstr(drawn,"A Launch"));
    detail.zso_launch=true;install_launch_detail(&detail);render(&view);
    assert(strstr(drawn,"Ready to play") && strstr(drawn,"Background loading: Off") && strstr(drawn,"CD music: Off"));
    assert(strstr(drawn,"ZSO launch test; compatibility untested"));
    press(KUI_SHELL_Y,false);render(&view);
    assert(option_row_colors[KUI_SHELL_GAME_OPTION_READER]==0xadd9 && option_row_colors[KUI_SHELL_GAME_OPTION_CDDA]==0xadd9);
    assert(option_row_colors[KUI_SHELL_GAME_OPTION_ADVANCED]==0xf7bf && strstr(drawn,"ZSO uses the Standard reader"));
    open_extra_options();s.game_tools_selected=KUI_SHELL_GAME_TOOL_SPEED;render(&view);
    assert(strstr(drawn,"Unavailable") && strstr(drawn,"ZSO uses the Standard reader"));
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_PATCHES;render(&view);
    assert(strstr(drawn,"RAM patches are unavailable for ZSO images"));
    s.game_tools_selected=KUI_SHELL_GAME_TOOL_INFO;press(KUI_SHELL_A,false);render(&view);
    assert(strstr(drawn,"Format: ZSO (standard LZ4)") && strstr(drawn,"Audio: unknown"));
    assert(!strstr(drawn,"CSO") && !strstr(drawn,"LZO") && !strstr(drawn,"CHD"));
    puts("PASS Games ZSO rendering: inspect-only explanation, Standard reader constraints and accurate format/unknown-audio information");
}
static void games_selected_art_rendering(void) {
    static uint16_t selected[KUI_COVER_PIXELS],legacy[KUI_GAMES_ROWS][KUI_COVER_PIXELS];
    for(unsigned i=0;i<KUI_COVER_PIXELS;i++) selected[i]=0x1234;
    for(unsigned i=0;i<KUI_GAMES_ROWS;i++)
        for(unsigned j=0;j<KUI_COVER_PIXELS;j++) legacy[i][j]=0x5678;
    struct kui_shell_view v={.game_selected_cover=selected,
        .game_covers=(const uint16_t (*)[KUI_COVER_PIXELS])legacy};
    reset(KUI_SHELL_GAMES);
    for(unsigned view=0;view<KUI_GAMES_VIEW_COUNT;view++) {
        s.games_view=view;games_page(2,false,view);s.games_selected=1;
        render(&v);
        unsigned x=view==KUI_GAMES_VIEW_LIST?441:view==KUI_GAMES_VIEW_COMPACT?41:201;
        unsigned y=view==KUI_GAMES_VIEW_LIST?151:view==KUI_GAMES_VIEW_COMPACT?210:141;
        assert(pixels[1+y*640+x]==0x1234);
        if(view!=KUI_GAMES_VIEW_LIST) {
            unsigned other_x=view==KUI_GAMES_VIEW_COMPACT?41:57;
            unsigned other_y=view==KUI_GAMES_VIEW_COMPACT?148:141;
            assert(pixels[1+other_y*640+other_x]!=0x1234 && pixels[1+other_y*640+other_x]!=0x5678);
        }
    }
    s.games_view=s.games_listing.view=KUI_GAMES_VIEW_LIST;
    s.games_listing.entries[1].cover=false;s.games_art_loading=true;render(&v);
    assert(strstr(drawn,"Loading box art...") && !strstr(drawn,"B Stop safely"));
}
static void ripper_export_rendering(void) {
    struct kui_shell_view v={0};reset(KUI_SHELL_RIPPER_SETTINGS);s.setting_selected=2;
    for(unsigned format=0;format<KUI_CAPTURE_FORMAT_COUNT;format++) {
        s.draft.capture_format=(enum kui_capture_format)format;render(&v);
        assert(strstr(drawn,"Output format") && strstr(drawn,kui_capture_format_name(s.draft.capture_format)));
        if(format==KUI_CAPTURE_FORMAT_GDI_DIRECT_2048) {
            assert(strstr(drawn,"Save data as 2048-byte") && strstr(drawn,"no full raw data tracks"));
            assert(strstr(drawn,"ON (cooked required)"));
        } else if(format==KUI_CAPTURE_FORMAT_GDI_2048) {
            assert(strstr(drawn,"Create verified 2048-byte") && strstr(drawn,"Original raw data and audio"));
            assert(strstr(drawn,"ON (export required)"));
        } else if(format>=KUI_CAPTURE_FORMAT_CSO) {
            assert(strstr(drawn,"Verify raw rip, then create") && !strstr(drawn,"descriptor changes"));
            assert(strstr(drawn,format==KUI_CAPTURE_FORMAT_CHD?"includes captured data and audio":"raw audio tracks are kept"));
            assert(strstr(drawn,"ON (export required)"));
        } else assert(strstr(drawn,"descriptor changes"));
    }
    reset(KUI_SHELL_RIPPER);v.busy=true;v.phase=4;
    v.total=1048576;v.done=524288;v.rate_kib=256;v.phase_elapsed_ms=5000;
    render(&v);assert(strstr(drawn,"Creating export") && strstr(drawn,"PHASE ETA 0:02"));
    assert(!strstr(drawn,"Completed"));
}
static void games_variant_rendering(void) {
    struct kui_shell_view view={0};
    for(unsigned choice=0;choice<2;choice++) {
        open_paired_games();
        if(choice) press(KUI_SHELL_DOWN,false);
        render(&view);
        assert(strstr(drawn,"Games / Choose version") && strstr(drawn,"DEAD OR ALIVE 2"));
        assert(strstr(drawn,"Original") && strstr(drawn,"2048-byte copy"));
        assert(strstr(drawn,s.games_original_path) && strstr(drawn,s.games_2048_path));
        assert(strstr(drawn,"A Inspect") && strstr(drawn,"B Games") && !strstr(drawn,"L Memory"));
        inspect_paired_choice(choice,false);render(&view);
        const char *label=choice?"Version: 2048-byte copy":"Version: Original";
        assert(strstr(drawn,label) && strstr(drawn,"B Versions"));
        assert(!strstr(drawn,"1ST_READ.BIN") && !strstr(drawn,"LBA"));
        press(KUI_SHELL_A,false);render(&view);
        assert(strstr(drawn,label) && strstr(drawn,"A Launch") && strstr(drawn,"Y Options"));
        press(KUI_SHELL_B,false);press(KUI_SHELL_Y,false);render(&view);
        assert(strstr(drawn,label) && strstr(drawn,"Background loading") && strstr(drawn,"CD music"));
        assert(!strstr(drawn,"Image read test") && !strstr(drawn,"Loader read test"));
        open_developer_tests();s.game_tests_selected=0;
        press(KUI_SHELL_A,false);render(&view);
        assert(strstr(drawn,label) && strstr(drawn,"A Start test") && strstr(drawn,"B Tests"));
        for(unsigned back=0;s.page!=KUI_SHELL_GAMES && back<8;back++) press(KUI_SHELL_B,false);
        assert(s.page==KUI_SHELL_GAMES);
        render(&view);assert(!strstr(drawn,"Version:") && strstr(drawn,"A chooses Original or 2048-byte copy"));
        s.games_view=KUI_GAMES_VIEW_COMPACT;render(&view);
        assert(strstr(drawn,"Original / 2048-byte copy"));
    }
    open_paired_games();inspect_paired_choice(1,true);press(KUI_SHELL_A,false);render(&view);
    assert(strstr(drawn,"Version: 2048-byte copy") && strstr(drawn,"Launch game"));
    assert(strstr(drawn,"A Launch") && strstr(drawn,"Background loading: On"));
    assert(!strstr(drawn,"standard reader") && !strstr(drawn,"X Background reader") && !strstr(drawn,"X/Y"));
}
/* ---- File Manager ---- */
static struct kui_files_page files_result(const char *path,bool picker,unsigned count,unsigned before,unsigned total,
        const char *const *names,const bool *directories) {
    struct kui_files_page page;
    memset(&page,0,sizeof(page));
    snprintf(page.path,sizeof(page.path),"%s",path);
    page.folders_only=picker;page.count=count;page.before=before;page.total=total;page.ok=true;
    for(unsigned i=0;i<count;i++) {
        struct kui_files_entry *e=&page.entries[i];
        snprintf(e->name,sizeof(e->name),"%s",names[i]);
        e->directory=directories[i];e->bytes=directories[i]?0:2048u*(i+1u);
        e->date=(uint16_t)((46u<<9)|(9u<<5)|26u);e->time=(uint16_t)((14u<<11)|(3u<<5));
    }
    if(count) {
        snprintf(page.first,sizeof(page.first),"%s",names[0]);page.first_directory=directories[0];
        snprintf(page.last,sizeof(page.last),"%s",names[count-1]);page.last_directory=directories[count-1];
    }
    return page;
}
static const char *const root_names[]={"Games","KUI","Music","Pictures","cover.png","disc.gdi","readme.txt","song.ogg"};
static const bool root_directories[]={true,true,true,true,false,false,false,false};
static void files_root(void) {
    struct kui_files_page page=files_result("/",false,8,0,12,root_names,root_directories);
    kui_shell_set_files_listing(&s,&page);
    assert(s.files_listing.count==8 && !strcmp(s.files_listing.entries[0].name,"Games"));
}
static void files_select(const char *name) {
    for(unsigned i=0;i<s.files_listing.count;i++)
        if(!strcmp(s.files_listing.entries[i].name,name)) {s.files_selected=i;return;}
    assert(!"row missing");
}
static void files_controls(void) {
    reset(KUI_SHELL_HOME);s.home_selected=home_index(KUI_SHELL_FILES);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_LIST && s.page==KUI_SHELL_FILES);
    assert(!strcmp(s.files_request.path,"/") && s.files_request.seek==KUI_FILES_SEEK_FIRST && !s.files_request.folders_only);
    files_root();
    /* Results for another folder, or the picker's, do not replace the page. */
    struct kui_files_page other=files_result("/Other",false,1,0,1,root_names,root_directories);
    kui_shell_set_files_listing(&s,&other);
    struct kui_files_page picked=files_result("/",true,1,0,1,root_names+1,root_directories+1);
    kui_shell_set_files_listing(&s,&picked);
    assert(!strcmp(s.files_listing.entries[0].name,"Games") && s.files_listing.count==8);
    /* Pages turn from the neighbouring row. */
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_FILES_LIST && s.files_request.seek==KUI_FILES_SEEK_NEXT);
    assert(!strcmp(s.files_request.anchor,"song.ogg") && !s.files_request.anchor_directory && !s.files_listing.count);
    static const char *const more[]={"setup.log","video.bin","zeta.txt","~temp"};
    static const bool files_only[]={false,false,false,false};
    struct kui_files_page page=files_result("/",false,4,8,12,more,files_only);
    kui_shell_set_files_listing(&s,&page);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_FILES_LIST && s.files_request.seek==KUI_FILES_SEEK_FIRST);
    page=files_result("/",false,4,16,30,more,files_only);
    kui_shell_set_files_listing(&s,&page);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_FILES_LIST && s.files_request.seek==KUI_FILES_SEEK_PREVIOUS);
    assert(!strcmp(s.files_request.anchor,"setup.log"));
    files_root();
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_FILES_LIST && s.files_request.seek==KUI_FILES_SEEK_AT);
    assert(!strcmp(s.files_request.anchor,"Games") && s.files_request.anchor_directory);
    files_root();
    /* A opens folders; B goes up, listing from the folder just left. */
    files_select("Music");
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_LIST && !strcmp(s.files_path,"/Music"));
    assert(!strcmp(s.files_request.path,"/Music") && s.files_request.seek==KUI_FILES_SEEK_FIRST);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_FILES_LIST && !strcmp(s.files_path,"/"));
    assert(s.files_request.seek==KUI_FILES_SEEK_AT && !strcmp(s.files_request.anchor,"Music") && s.files_request.anchor_directory);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    s.page=KUI_SHELL_FILES;files_root();
    /* Games open their details, and B comes back here. */
    files_select("disc.gdi");
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_INSPECT && s.page==KUI_SHELL_GAMES_DETAIL);
    assert(!strcmp(s.games_selected_path,"/disc.gdi") && s.games_from_files);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES && !s.games_from_files);
    /* Songs play through the music player. */
    files_select("song.ogg");
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_MUSIC_PLAY && !strcmp(s.music_selected_path,"/song.ogg"));
    assert(s.page==KUI_SHELL_FILES && !strcmp(s.files_notice,"Loading music..."));
    /* Pictures open their view; only that picture's result is kept. */
    files_select("cover.png");
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_PICTURE && s.page==KUI_SHELL_FILES_VIEW);
    assert(!strcmp(s.files_picture.path,"/cover.png") && !s.files_picture.ok);
    struct kui_files_picture picture={.path="/other.png",.format="PNG",.width=64,.height=48,.ok=true};
    kui_shell_set_files_picture(&s,&picture);assert(!s.files_picture.ok);
    snprintf(picture.path,sizeof(picture.path),"/cover.png");
    kui_shell_set_files_picture(&s,&picture);assert(s.files_picture.ok && s.files_picture.width==64);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES);
    s.page=KUI_SHELL_FILES_ACTIONS;kui_shell_set_files_picture(&s,&picture);s.page=KUI_SHELL_FILES;
    /* Other files show details. */
    files_select("readme.txt");
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_CHECK && s.page==KUI_SHELL_FILES_INFO);
    assert(s.files_job.op==KUI_FILES_OP_DETAILS && !strcmp(s.files_job.source,"/readme.txt"));
    struct kui_files_preview preview;memset(&preview,0,sizeof(preview));
    preview.job=s.files_job;preview.status.complete=preview.status.passed=true;preview.bytes=2048;preview.files=1;
    snprintf(preview.job.source,sizeof(preview.job.source),"/other.txt");
    kui_shell_set_files_preview(&s,&preview);assert(!s.files_preview.status.complete);
    preview.job=s.files_job;kui_shell_set_files_preview(&s,&preview);assert(s.files_preview.bytes==2048);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES);
    /* The actions menu refuses to move, rename or delete what K-UI needs. */
    files_select("KUI");
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES_ACTIONS && s.files_action_selected==0);
    for(unsigned item=2;item<=4;item++) {
        s.files_action_selected=item;
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES_ACTIONS);
        assert(strstr(s.files_notice,"K-UI needs this") && s.files_notice_error);
    }
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.files_action_selected==3);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_FILES);
    /* Copy: choose a folder, check, confirm, run, then list the folder again. */
    files_select("Music");
    press(KUI_SHELL_X,false);s.files_action_selected=1;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_LIST && s.page==KUI_SHELL_FILES_PICK);
    assert(s.files_job.op==KUI_FILES_OP_COPY && !strcmp(s.files_job.source,"/Music"));
    assert(s.files_request.folders_only && !strcmp(s.files_request.path,"/") && !strcmp(s.files_pick_path,"/"));
    static const char *const folders[]={"Games","KUI","Music","Pictures"};
    static const bool all_folders[]={true,true,true,true};
    struct kui_files_page pick=files_result("/",true,4,0,4,folders,all_folders);
    kui_shell_set_files_listing(&s,&pick);
    assert(s.files_pick.count==4 && s.files_pick.entries[2].disabled && !s.files_pick.entries[0].disabled);
    s.files_pick_selected=2;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && strstr(s.files_pick.message,"being copied or moved"));
    s.files_pick_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_LIST && !strcmp(s.files_pick_path,"/Games"));
    assert(s.files_request.folders_only && !strcmp(s.files_request.path,"/Games"));
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_FILES_CHECK && s.page==KUI_SHELL_FILES_CONFIRM);
    assert(!strcmp(s.files_job.target,"/Games") && !kui_shell_files_ready(&s));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    memset(&preview,0,sizeof(preview));
    preview.job=s.files_job;preview.ready=true;preview.status.complete=preview.status.passed=true;
    preview.directory=true;preview.files=3;preview.bytes=4096;
    snprintf(preview.job.name,sizeof(preview.job.name),"Music");
    snprintf(preview.job.target,sizeof(preview.job.target),"/Pictures");
    kui_shell_set_files_preview(&s,&preview);assert(!kui_shell_files_ready(&s));
    snprintf(preview.job.target,sizeof(preview.job.target),"/Games");
    snprintf(preview.job.name,sizeof(preview.job.name),"bad/name");
    kui_shell_set_files_preview(&s,&preview);assert(!kui_shell_files_ready(&s));
    snprintf(preview.job.name,sizeof(preview.job.name),"Music");
    kui_shell_set_files_preview(&s,&preview);assert(kui_shell_files_ready(&s) && !strcmp(s.files_job.name,"Music"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES_PICK);
    s.page=KUI_SHELL_FILES_CONFIRM;
    assert(press(KUI_SHELL_A|KUI_SHELL_L,true)==KUI_SHELL_NONE && press(KUI_SHELL_B,true)==KUI_SHELL_STOP);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_RUN && s.page==KUI_SHELL_FILES && s.files_running);
    assert(s.files_request.seek==KUI_FILES_SEEK_AT && !strcmp(s.files_request.anchor,"Games") && !s.files_request.folders_only);
    assert(!strcmp(s.files_notice,"Copying..."));
    struct kui_app_status status={.complete=true,.passed=true,.line_count=1};
    snprintf(status.message,sizeof(status.message),"Copied to /Games");
    snprintf(status.lines[0],sizeof(status.lines[0]),"3 files, 4.0 KB, read back and checked.");
    kui_shell_set_files_status(&s,&status);
    assert(!s.files_running && !strcmp(s.files_notice,"Copied to /Games") && !s.files_notice_error);
    assert(!strcmp(s.files_notice_detail,"3 files, 4.0 KB, read back and checked."));
    status.passed=false;snprintf(status.message,sizeof(status.message),"The card is full");
    kui_shell_set_files_status(&s,&status);assert(s.files_notice_error);
    files_root();
    /* The picker: B climbs, then returns to the menu; START cancels. */
    files_select("Pictures");press(KUI_SHELL_X,false);s.files_action_selected=2;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_LIST && s.files_job.op==KUI_FILES_OP_MOVE);
    kui_shell_set_files_listing(&s,&pick);s.files_pick_selected=0;
    press(KUI_SHELL_A,false);assert(!strcmp(s.files_pick_path,"/Games"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_FILES_LIST && !strcmp(s.files_pick_path,"/"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES_ACTIONS);
    s.files_action_selected=2;press(KUI_SHELL_A,false);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES);
    /* Delete: nothing runs until a ready check is confirmed. */
    files_select("Pictures");press(KUI_SHELL_X,false);s.files_action_selected=4;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_CHECK && s.page==KUI_SHELL_FILES_CONFIRM);
    assert(s.files_job.op==KUI_FILES_OP_DELETE && !strcmp(s.files_job.source,"/Pictures"));
    memset(&preview,0,sizeof(preview));preview.job=s.files_job;preview.status.complete=true;
    snprintf(preview.status.message,sizeof(preview.status.message),"Cannot read a folder inside");
    kui_shell_set_files_preview(&s,&preview);
    assert(!kui_shell_files_ready(&s) && press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES);
    /* Rename and new folder use the keyboard; names are checked first. */
    files_select("readme.txt");press(KUI_SHELL_X,false);s.files_action_selected=3;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && s.files_keyboard);
    assert(!strcmp(s.keyboard,"readme.txt") && s.files_job.op==KUI_FILES_OP_RENAME);
    snprintf(s.keyboard,sizeof(s.keyboard),"bad/name");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.destination_notice[0] && s.page==KUI_SHELL_KEYBOARD);
    s.keyboard[0]=0;assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && strstr(s.destination_notice,"Type a name"));
    snprintf(s.keyboard,sizeof(s.keyboard),"notes.txt");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_FILES_RUN && s.page==KUI_SHELL_FILES && !s.files_keyboard);
    assert(!strcmp(s.files_job.name,"notes.txt") && !strcmp(s.files_job.source,"/readme.txt"));
    assert(s.files_request.seek==KUI_FILES_SEEK_AT && !strcmp(s.files_request.anchor,"notes.txt") && !s.files_request.anchor_directory);
    assert(s.files_running && !strcmp(s.files_notice,"Renaming..."));
    kui_shell_set_files_status(&s,&status);files_root();
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && !s.keyboard[0]);
    assert(s.files_job.op==KUI_FILES_OP_MKDIR && !strcmp(s.files_job.source,"/"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES && !s.files_keyboard);
    press(KUI_SHELL_Y,false);snprintf(s.keyboard,sizeof(s.keyboard),"Saves");
    s.keyboard_selected=42;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_RUN && !strcmp(s.files_job.name,"Saves"));
    assert(s.files_request.anchor_directory && !strcmp(s.files_request.anchor,"Saves"));
    /* Rows the worker could not name are shown but never used. */
    files_root();
    struct kui_files_page broken=files_result("/",false,2,0,2,root_names,root_directories);
    memset(broken.entries[1].name,'x',sizeof(broken.entries[1].name));
    kui_shell_set_files_listing(&s,&broken);
    assert(s.files_listing.entries[1].disabled && !strcmp(s.files_listing.entries[1].name,"[Name too long]"));
    s.files_selected=1;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && strstr(s.files_notice,"cannot use this name"));
    broken.last[sizeof(broken.last)-1]='x';memset(broken.last,'x',sizeof(broken.last));
    kui_shell_set_files_listing(&s,&broken);assert(!s.files_listing.count);
    puts("PASS File Manager: navigation, paging anchors, open-with, protected items, copy/move/delete/rename/new folder flows");
}
static void files_rendering(void) {
    struct kui_shell_view v={.build="a1b2c3d4e5f6"};
    reset(KUI_SHELL_HOME);s.home_selected=home_index(KUI_SHELL_FILES);render(&v);
    assert(strstr(drawn,"File Manager") && strstr(drawn,"SD card files") && strstr(drawn,"8 applications"));
    reset(KUI_SHELL_FILES);files_root();s.files_selected=6;render(&v);
    assert(strstr(drawn,"Games") && strstr(drawn,"readme.txt") && strstr(drawn,"DIR") && strstr(drawn,"TXT"));
    assert(strstr(drawn,"Items 1-8 of 12") && strstr(drawn,"Modified 2026-09-26 14:03") && strstr(drawn,"14.0 KB"));
    assert(strstr(drawn,"A Open   X Actions   Y New folder   R Refresh") && strstr(drawn,"B Up   START Home"));
    s.files_listing.entries[6].attributes=0x03;render(&v);assert(strstr(drawn,"Read-only   Hidden"));
    struct kui_app_status status={.complete=true,.line_count=1};
    snprintf(status.message,sizeof(status.message),"The card is full");
    snprintf(status.lines[0],sizeof(status.lines[0]),"The partial copy was removed.");
    kui_shell_set_files_status(&s,&status);render(&v);
    assert(strstr(drawn,"The card is full") && strstr(drawn,"The partial copy was removed.") && !strstr(drawn,"Items 1-8"));
    s.files_job.op=KUI_FILES_OP_COPY;s.files_running=true;v.busy=true;
    struct kui_app_status live={.done=500,.total=1000};
    snprintf(live.message,sizeof(live.message),"Copying 2 of 3: track02.raw");
    v.app_status=&live;render(&v);
    assert(strstr(drawn,"Copying 2 of 3: track02.raw") && strstr(drawn,"50.0%") && strstr(drawn,"B stops safely"));
    assert(!strstr(drawn,"readme.txt"));
    v.busy=false;v.app_status=NULL;s.files_running=false;
    reset(KUI_SHELL_FILES);files_root();s.files_selected=1;s.page=KUI_SHELL_FILES_ACTIONS;s.files_action_selected=4;
    render(&v);
    assert(strstr(drawn,"File actions") && strstr(drawn,"KUI") && strstr(drawn,"Copy to another folder"));
    assert(strstr(drawn,"K-UI needs this to start: move, rename and delete are off."));
    assert(strstr(drawn,"Review what will be deleted, then confirm."));
    s.files_selected=0;render(&v);assert(!strstr(drawn,"K-UI needs this"));
    /* Confirmations show what the check found. */
    s.page=KUI_SHELL_FILES_CONFIRM;s.files_job.op=KUI_FILES_OP_COPY;
    snprintf(s.files_job.source,sizeof(s.files_job.source),"/Music");
    snprintf(s.files_job.target,sizeof(s.files_job.target),"/Games");
    struct kui_files_preview *pv=&s.files_preview;
    memset(pv,0,sizeof(*pv));pv->job=s.files_job;pv->ready=true;pv->status.complete=pv->status.passed=true;
    pv->directory=true;pv->files=3;pv->folders=1;pv->bytes=4096;pv->free_known=true;pv->free_bytes=1048576;
    pv->renamed=true;snprintf(pv->job.name,sizeof(pv->job.name),"Music (2)");
    snprintf(s.files_job.name,sizeof(s.files_job.name),"Music (2)");
    render(&v);
    assert(strstr(drawn,"COPY THIS FOLDER?") && strstr(drawn,"Holds 3 files and 1 folder, 4.0 KB"));
    assert(strstr(drawn,"To: /Games") && strstr(drawn,"it will be Music (2)") && strstr(drawn,"1.0 MB free"));
    assert(strstr(drawn,"A Copy") && strstr(drawn,"B Cancel") && !strstr(drawn,"L Memory"));
    pv->ready=false;snprintf(pv->status.message,sizeof(pv->status.message),"A folder cannot be copied into itself");
    render(&v);
    assert(strstr(drawn,"CANNOT COPY THIS ITEM") && strstr(drawn,"copied into itself") && strstr(drawn,"B Back"));
    assert(!strstr(drawn,"A Copy"));
    v.busy=true;struct kui_app_status counting={0};
    snprintf(counting.message,sizeof(counting.message),"Counting: 12 files in 2 folders, 1.0 MB");
    v.app_status=&counting;render(&v);
    assert(strstr(drawn,"CHECKING...") && strstr(drawn,"Counting: 12 files") && strstr(drawn,"B stops the check"));
    v.busy=false;v.app_status=NULL;
    s.files_job.op=KUI_FILES_OP_DELETE;pv->job=s.files_job;pv->ready=true;pv->read_only=2;
    render(&v);
    assert(strstr(drawn,"DELETE THIS FOLDER?") && strstr(drawn,"This cannot be undone.") && strstr(drawn,"2 read-only items"));
    assert(strstr(drawn,"A Delete"));
    /* Details and pictures. */
    s.page=KUI_SHELL_FILES_INFO;s.files_job.op=KUI_FILES_OP_DETAILS;
    snprintf(s.files_job.source,sizeof(s.files_job.source),"/KUI/runtime.kui");
    memset(pv,0,sizeof(*pv));pv->job=s.files_job;pv->status.complete=pv->status.passed=true;
    pv->bytes=1834112;pv->files=1;pv->attributes=0x01;
    render(&v);
    assert(strstr(drawn,"runtime.kui") && strstr(drawn,"Size: 1.7 MB (1834112 bytes)") && strstr(drawn,"Read-only"));
    assert(strstr(drawn,"In: /KUI") && strstr(drawn,"K-UI needs this to start"));
    s.page=KUI_SHELL_FILES_VIEW;memset(&s.files_picture,0,sizeof(s.files_picture));
    snprintf(s.files_picture.path,sizeof(s.files_picture.path),"/Pictures/cover.png");
    snprintf(s.files_picture.message,sizeof(s.files_picture.message),"Image larger than 1.2 megapixels or 3 MB");
    render(&v);
    assert(strstr(drawn,"cover.png") && strstr(drawn,"Image larger than 1.2 megapixels") && strstr(drawn,"B Files"));
    static uint16_t picture[KUI_FILES_PICTURE_EDGE*KUI_FILES_PICTURE_EDGE];
    for(unsigned i=0;i<KUI_FILES_PICTURE_EDGE*KUI_FILES_PICTURE_EDGE;i++) picture[i]=0x07e0;
    v.files_picture=picture;s.files_picture.ok=true;s.files_picture.format="PNG";
    s.files_picture.width=64;s.files_picture.height=48;s.files_picture.bytes=2048;
    render(&v);
    assert(strstr(drawn,"PNG") && strstr(drawn,"64 x 48 pixels") && strstr(drawn,"File: 2.0 KB") && strstr(drawn,"/Pictures"));
    assert(pixels[1+(136+100)*640+40+100]==0x07e0);
    s.page=KUI_SHELL_KEYBOARD;s.files_keyboard=true;s.files_job.op=KUI_FILES_OP_RENAME;render(&v);
    assert(strstr(drawn,"Rename") && strstr(drawn,"A name cannot contain /") && !strstr(drawn,"Type destination"));
    s.files_keyboard=false;render(&v);assert(strstr(drawn,"Type destination"));
    /* Games details opened here return here. */
    reset(KUI_SHELL_GAMES_DETAIL);s.games_from_files=true;render(&v);
    assert(strstr(drawn,"B Files"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES);
    puts("PASS File Manager rendering: Home entry, rows, notices, progress, actions, confirmations, details, picture, keyboard");
}
/* FTP server: Y on the Network page opens its page and starts it; B stops
 * it while it runs, then returns to Network; A starts it again. */
static void ftp_controls(void) {
    reset(KUI_SHELL_NETWORK);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_FTP_START && s.page==KUI_SHELL_FTP);
    assert(press(KUI_SHELL_A|KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_START,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_FTP);
    assert(press(KUI_SHELL_B,true)==KUI_SHELL_STOP && s.page==KUI_SHELL_FTP);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FTP_START && s.page==KUI_SHELL_FTP);
    assert(press(KUI_SHELL_X|KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FTP);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_NETWORK);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    /* The busy Network page does not start it. */
    reset(KUI_SHELL_NETWORK);
    assert(press(KUI_SHELL_Y,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_NETWORK);
    puts("PASS shell FTP controls: start from Network, stop, restart, back");
}
static void ftp_rendering(void) {
    struct kui_shell_view v={.build="a1b2c3d4e5f6",.busy=true};
    reset(KUI_SHELL_FTP);render(&v);
    assert(strstr(drawn,"FTP Server") && strstr(drawn,"Starting") && strstr(drawn,"B Stop the server"));
    struct kui_ftp_status f;
    memset(&f,0,sizeof(f));
    snprintf(f.message,sizeof(f.message),"Asking the router for an address (DHCP)");
    v.ftp=&f;render(&v);
    assert(strstr(drawn,"Asking the router for an address") && strstr(drawn,"SD card stays on SCIF"));
    f.state=KUI_FTP_READY;f.link=true;f.port=KUI_FTP_PORT;
    f.ip[0]=192;f.ip[1]=168;f.ip[2]=1;f.ip[3]=50;
    snprintf(f.password,sizeof(f.password),"48217365");
    snprintf(f.adapter,sizeof(f.adapter),"W5500 on SCI at 12.5 MHz; 100 Mbit/s full duplex");
    render(&v);
    assert(strstr(drawn,"ftp://192.168.1.50") && !strstr(drawn,"ftp://192.168.1.50:"));
    assert(strstr(drawn,"User: kui   Password: 48217365") && strstr(drawn,"12.5 MHz; 100 Mbit/s"));
    assert(strstr(drawn,"No client connected yet") && strstr(drawn,"not encrypted"));
    assert(strstr(drawn,"Received 0 files (0 B), sent 0 (0 B)"));
    f.port=2121;render(&v);assert(strstr(drawn,"ftp://192.168.1.50:2121"));
    f.link=false;render(&v);assert(strstr(drawn,"No cable link"));
    f.link=true;
    struct kui_ftp_client *c=f.clients;
    c[0].active=c[0].logged_in=c[0].sending=true;c[0].ip[0]=192;c[0].ip[1]=168;c[0].ip[2]=1;c[0].ip[3]=20;
    snprintf(c[0].name,sizeof(c[0].name),"track03.bin");c[0].done=12u*1024u*1024u;c[0].total=48u*1024u*1024u;
    c[0].rate=480u*1024u;
    c[2].active=c[2].logged_in=c[2].receiving=true;c[2].ip[0]=10;c[2].ip[3]=7;
    snprintf(c[2].name,sizeof(c[2].name),"disc.gdi");c[2].done=2048;
    c[1].active=true;c[1].ip[0]=10;c[1].ip[3]=9;
    f.files_in=3;f.files_out=1;f.bytes_in=UINT64_C(1288490188);f.bytes_out=2u*1024u*1024u;
    f.event_count=2;
    snprintf(f.events[0],sizeof(f.events[0]),"Received /Games/Crazy Taxi/track02.raw (412.3 MB)");
    snprintf(f.events[1],sizeof(f.events[1]),"192.168.1.20 logged in");
    render(&v);
    assert(strstr(drawn,"192.168.1.20") && strstr(drawn,"Sending track03.bin") && strstr(drawn,"25%  12.0 MB of 48.0 MB"));
    assert(strstr(drawn,"480 KB/s") && strstr(drawn,"Receiving disc.gdi") && strstr(drawn,"2.0 KB received so far"));
    assert(strstr(drawn,"10.0.0.9") && strstr(drawn,"Connected, not logged in"));
    assert(strstr(drawn,"Received 3 files (1.2 GB), sent 1 (2.0 MB)") && strstr(drawn,"track02.raw (412.3 MB)"));
    assert(strstr(drawn,"192.168.1.20 logged in") && !strstr(drawn,"No client connected"));
    /* The last upload's and download's lines stay whole; the newest events
     * fill the rows left. */
    f.event_count=3;
    snprintf(f.events[2],sizeof(f.events[2]),"192.168.1.20 connected");
    render(&v);
    assert(strstr(drawn,"192.168.1.20 connected"));
    snprintf(f.last_in,sizeof(f.last_in),"Last up 1023 KiB/s: card 1041, net 1402, overlap 100%%, 12.5%% retried");
    render(&v);
    assert(strstr(drawn,f.last_in) && strstr(drawn,"track02.raw (412.3 MB)") && strstr(drawn,"192.168.1.20 logged in"));
    assert(!strstr(drawn,"192.168.1.20 connected"));
    snprintf(f.last_out,sizeof(f.last_out),"Last down 573 KiB/s: card 599, net 1402, overlap 100%%, 12.5%% retried");
    render(&v);
    assert(strstr(drawn,f.last_in) && strstr(drawn,f.last_out) && strstr(drawn,"track02.raw (412.3 MB)"));
    assert(!strstr(drawn,"192.168.1.20 logged in"));
    snprintf(f.last_out,sizeof(f.last_out),"Last down 1023 KiB/s: card 1041, net 1051, overlap 12%%, then no DMA");
    render(&v);
    assert(strstr(drawn,f.last_out));
    v.busy=false;f.state=KUI_FTP_STOPPED;memset(f.clients,0,sizeof(f.clients));
    snprintf(f.message,sizeof(f.message),"The FTP server was stopped on the Dreamcast");
    render(&v);
    assert(strstr(drawn,"The FTP server is off") && strstr(drawn,"stopped on the Dreamcast"));
    assert(strstr(drawn,"A Start again   B Network") && strstr(drawn,"No clients connected."));
    f.state=KUI_FTP_FAILED;snprintf(f.message,sizeof(f.message),"No W5500 answered on the SCI port (read FF)");
    render(&v);
    assert(strstr(drawn,"The FTP server stopped") && strstr(drawn,"No W5500 answered"));
    puts("PASS shell FTP rendering: starting, ready, clients and transfers, stopped, failed");
}
static struct kui_storage_test_history test_history;
static void storage_test_fixture(void) {
    memset(&test_history,0,sizeof(test_history));test_history.count=2;
    for(unsigned i=0;i<2;i++) {
        struct kui_storage_test_result *r=&test_history.rows[i];
        r->id=12-i;kui_storage_test_defaults(&r->request);r->request.repeats=3;
        r->outcome=i?KUI_STORAGE_TEST_STOPPED:KUI_STORAGE_TEST_PASSED;
        r->saved=true;r->sample_count=3;r->cycles=3;r->verified_bytes=12u*1048576;
        r->elapsed_us=40000000;
        snprintf(r->request.card_label,sizeof(r->request.card_label),"Samsung 128GB");
        snprintf(r->metadata.build,sizeof(r->metadata.build),"a1b2c3d4e5f6");
        snprintf(r->metadata.filesystem,sizeof(r->metadata.filesystem),"exFAT");
        r->metadata.transport=1;r->metadata.cluster_bytes=131072;r->metadata.ui_hz=2;
        snprintf(r->path,sizeof(r->path),"/KUI/tests/t0012/result.txt");
        snprintf(r->message,sizeof(r->message),"Every byte matched after remount.");
        r->write_latency.max_us=84201;r->write_latency.p95_upper_us=20000;
        r->read_latency.max_us=19425;r->read_latency.p95_upper_us=20000;
        for(unsigned j=0;j<3;j++) r->samples[j]=(struct kui_storage_test_sample){
            .chunk_bytes=65536,.repeat=j+1,.bytes=4u*1048576,
            .write_us=5000000+j*100000,.read_us=4000000+j*100000,.verified=true};
    }
    test_history.baseline=test_history.rows[0];test_history.baseline.id=9;
    test_history.baseline.metadata.transport=0;test_history.baseline_valid=true;
    snprintf(test_history.baseline.metadata.build,sizeof(test_history.baseline.metadata.build),"112233445566");
    for(unsigned i=0;i<3;i++) {test_history.baseline.samples[i].write_us*=2;test_history.baseline.samples[i].read_us*=2;}
}
static void storage_test_controls(void) {
    reset(KUI_SHELL_DIAGNOSTICS);storage_test_fixture();
    assert(press(KUI_SHELL_R,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_DIAGNOSTICS);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_TEST_HISTORY && s.page==KUI_SHELL_STORAGE_TESTS);
    kui_shell_set_storage_test_history(&s,&test_history);
    assert(s.storage_test_last_valid && s.storage_test_last_request.repeats==3);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.storage_test_request.preset==KUI_STORAGE_TEST_COMPARE);
    press(KUI_SHELL_RIGHT,false);assert(s.storage_test_request.preset==KUI_STORAGE_TEST_SOAK);
    s.storage_test_selected=2;press(KUI_SHELL_RIGHT,false);assert(s.storage_test_request.soak_minutes==30);
    press(KUI_SHELL_LEFT,false);assert(s.storage_test_request.soak_minutes==15);
    s.storage_test_selected=3;press(KUI_SHELL_A,false);assert(s.storage_test_keyboard && s.page==KUI_SHELL_KEYBOARD);
    strcpy(s.keyboard,"Kingston 64GB");press(KUI_SHELL_START,false);
    assert(s.page==KUI_SHELL_STORAGE_TESTS && !strcmp(s.storage_test_request.card_label,"Kingston 64GB"));
    press(KUI_SHELL_A,false);strcpy(s.keyboard,"discarded");press(KUI_SHELL_B,false);
    assert(!strcmp(s.storage_test_request.card_label,"Kingston 64GB"));
    s.storage_test_selected=4;assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.confirm_storage_test);
    assert(press(KUI_SHELL_L|KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_R,false)==KUI_SHELL_NONE && s.confirm_storage_test);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_storage_test);
    press(KUI_SHELL_A,false);assert(press(KUI_SHELL_A,false)==KUI_SHELL_TEST_RUN && !s.confirm_storage_test);
    assert(press(KUI_SHELL_A|KUI_SHELL_Y|KUI_SHELL_X,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    kui_shell_set_storage_test_result(&s,&test_history.rows[0]);assert(s.storage_test_show_result);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_TEST_BASELINE && s.storage_test_baseline_id==12);
    press(KUI_SHELL_B,false);s.storage_test_selected=5;
    s.storage_test_request.repeats=5;press(KUI_SHELL_A,false);
    assert(s.confirm_storage_test && s.storage_test_request.repeats==3);
    press(KUI_SHELL_B,false);s.storage_test_selected=6;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_TEST_HISTORY && s.page==KUI_SHELL_STORAGE_TEST_HISTORY);
    press(KUI_SHELL_DOWN,false);assert(s.storage_history_selected==1);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE); /* stopped is not a baseline */
    press(KUI_SHELL_A,false);assert(s.storage_test_show_result && s.storage_test_from_history);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE);
    press(KUI_SHELL_X,false);assert(s.storage_test_details);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_STORAGE_TEST_HISTORY);
    press(KUI_SHELL_UP,false);test_history.rows[0].saved=false;kui_shell_set_storage_test_history(&s,&test_history);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE); /* failed persistence is not a baseline */
    press(KUI_SHELL_B,false);s.storage_test_selected=7;assert(press(KUI_SHELL_A,false)==KUI_SHELL_BENCH);
    assert(s.page==KUI_SHELL_DIAGNOSTICS);
    press(KUI_SHELL_R,false);s.storage_test_selected=8;
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_STORAGE_TESTS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SCI_ASYNC_PROBE);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_SCI_ASYNC_RUN);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_SCI_ASYNC_STRESS);
    assert(press(KUI_SHELL_X,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_SCI_ASYNC_SCREEN && s.page==KUI_SHELL_SCI_ASYNC_PROBE);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_SCI_ASYNC_SPEED && s.page==KUI_SHELL_SCI_ASYNC_PROBE);
    assert(press(KUI_SHELL_Y,true)==KUI_SHELL_NONE && press(KUI_SHELL_R,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_X,false)==KUI_SHELL_SCI_ASYNC_RUN);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP && s.page==KUI_SHELL_SCI_ASYNC_PROBE);
    assert(press(KUI_SHELL_X|KUI_SHELL_B,true)==KUI_SHELL_STOP && s.page==KUI_SHELL_SCI_ASYNC_PROBE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_STORAGE_TESTS);
    assert(s.storage_test_selected==8);
    puts("PASS storage tests controls: Diagnostics entry, presets, labels, confirmation, Stop, repeat, history and baseline guards");
}
static void storage_test_rendering(void) {
    reset(KUI_SHELL_STORAGE_TESTS);storage_test_fixture();
    struct kui_shell_view v={.build="a1b2c3d4e5f6",.storage_test_target="SCI SD"};
    render(&v);assert(strstr(drawn,"Storage tests") && strstr(drawn,"Target: SCI SD") && strstr(drawn,"Advanced benchmarks"));
    v.busy=true;render(&v);assert(strstr(drawn,"Working...") && !strstr(drawn,"Preparing tests..."));v.busy=false;
    s.confirm_storage_test=true;render(&v);
    assert(strstr(drawn,"dedicated temporary test file") && strstr(drawn,"Music pauses") && strstr(drawn,"A Start test"));
    s.confirm_storage_test=false;
    struct kui_storage_test_progress progress={.preset=KUI_STORAGE_TEST_SOAK,.repeat=12,
        .done=2097152,.total=16777216,.elapsed_us=420000000,.target_us=900000000};
    strcpy(progress.phase,"Verifying");v.storage_test_progress=&progress;v.busy=true;render(&v);
    assert(strstr(drawn,"Verifying") && strstr(drawn,"Cycle 12") && strstr(drawn,"7:00") && strstr(drawn,"15 min target"));
    v.cancel_requested=true;render(&v);assert(strstr(drawn,"Stopping safely"));
    v.busy=v.cancel_requested=false;
    kui_shell_set_storage_test_history(&s,&test_history);kui_shell_set_storage_test_result(&s,&test_history.rows[0]);
    render(&v);assert(strstr(drawn,"Verification: PASSED") && strstr(drawn,"p95 upper bound") && strstr(drawn,"Baseline #9"));
    assert(strstr(drawn,"+100.") && strstr(drawn,"Range: write"));
    s.storage_test_history.baseline.metadata.cluster_bytes=65536;render(&v);
    assert(strstr(drawn,"No comparison:") && !strstr(drawn,"+100."));
    s.storage_test_result.saved=false;render(&v);assert(strstr(drawn,"Not saved: keep a photo"));
    s.storage_test_details=true;s.storage_test_result.errors.total=1;
    s.storage_test_result.errors.last_lba=12345;s.storage_test_result.errors.last_count=4;
    s.storage_test_result.errors.sd_detail_valid=true;s.storage_test_result.errors.sd_command=24;
    s.storage_test_result.errors.sd_response=0x0b;s.storage_test_result.cleanup_failed=true;
    render(&v);assert(strstr(drawn,"Sector 12345 +4") && strstr(drawn,"CMD24") && strstr(drawn,"cleanup failed"));
    s.page=KUI_SHELL_STORAGE_TEST_HISTORY;render(&v);
    assert(strstr(drawn,"Newest eight results") && strstr(drawn,"Samsung 128GB") && strstr(drawn,"Baseline #9"));
    s.page=KUI_SHELL_SCI_ASYNC_PROBE;v.app_status=NULL;v.busy=false;render(&v);
    assert(strstr(drawn,"SCI async probe") && strstr(drawn,"CPU can work") && strstr(drawn,"/KUI/tests"));
    assert(strstr(drawn,"16 sectors") && strstr(drawn,"X 60s stress") && strstr(drawn,"Read-only"));
    assert(strstr(drawn,"Y With screen") && strstr(drawn,"R Speed") && strstr(drawn,"screen updating"));
    struct kui_app_status probe={.complete=true,.line_count=2};
    snprintf(probe.message,sizeof(probe.message),"Read test finished; async proof is incomplete.");
    snprintf(probe.lines[0],KUI_APP_LINE_CAP,"CPU overlap batches: slow 0 / fast 0");
    snprintf(probe.lines[1],KUI_APP_LINE_CAP,"Normal read recovery: verified");
    v.app_status=&probe;render(&v);
    assert(strstr(drawn,"proof is incomplete") && strstr(drawn,"slow 0 / fast 0") && strstr(drawn,"recovery: verified"));
    probe.errors=1;probe.line_count=8;
    snprintf(probe.message,sizeof(probe.message),"SCI unsupported state; reinit init failed. Restart required.");
    snprintf(probe.lines[6],KUI_APP_LINE_CAP,"Recover init: card timeout; CMD0 R1FF");
    snprintf(probe.lines[7],KUI_APP_LINE_CAP,"GPIO: DMA0 SSR84 SPTR82 R100 TKFE");
    render(&v);
    assert(strstr(drawn,"unsupported state") && strstr(drawn,"CMD0 R1FF") && strstr(drawn,"SPTR82"));
    snprintf(probe.lines[3],KUI_APP_LINE_CAP,"DMA left 513 CHCR00004911 ERI1 RXI0");
    snprintf(probe.lines[7],KUI_APP_LINE_CAP,"DMA: DMA1 SSR20 SPTR84 R100 TKFE");
    render(&v);
    assert(strstr(drawn,"DMA left 513") && strstr(drawn,"CHCR00004911") && strstr(drawn,"ERI1 RXI0"));
    v.busy=true;probe.complete=false;v.cancel_requested=true;render(&v);
    assert(strstr(drawn,"Stopping safely") && strstr(drawn,"Keep the card connected"));
    v.cancel_requested=false;v.sci_video_quiet=true;render(&v);
    assert(strstr(drawn,"screen stays still") && strstr(drawn,"controller input stay active") && strstr(drawn,"Hold B"));
    puts("PASS storage tests rendering: progress, rates, safe baseline comparison, metadata, errors and persistence warnings");
}
/* Wi-Fi: START on the Network page opens its page, which looks for the
 * board and networks; rows are the bands, each network, "Other network". */
static struct kui_wifi_view three_networks(void) {
    struct kui_wifi_view view;
    memset(&view,0,sizeof(view));
    view.found=view.scanned=true;view.count=3;
    strcpy(view.board,"XIAO ESP32-C5, firmware 0.1.0 (SPI 12.5 MHz, select GPIO6)");
    strcpy(view.message,"3 networks in range");
    view.wifi.state=KWM_WIFI_IDLE;view.wifi.band_mode=KWM_BAND_BOTH;
    struct kui_wifi_network *n=view.networks;
    strcpy(n[0].ssid,"Home 5G");n[0].security=3;n[0].five=true;n[0].channel=36;n[0].rssi=-48;
    strcpy(n[1].ssid,"Home");n[1].security=4;n[1].channel=11;n[1].rssi=-66;
    strcpy(n[2].ssid,"Cafe");n[2].channel=1;n[2].rssi=-80;
    return view;
}
static void wifi_controls(void) {
    reset(KUI_SHELL_NETWORK);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_WIFI_REFRESH && s.page==KUI_SHELL_WIFI);
    /* No board yet: X looks again, B leaves; nothing else. */
    assert(kui_shell_wifi_rows(&s)==2);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && !s.confirm_wifi_forget);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && press(KUI_SHELL_X,false)==KUI_SHELL_WIFI_REFRESH);
    struct kui_wifi_view view=three_networks();
    kui_shell_set_wifi(&s,&view);
    assert(kui_shell_wifi_rows(&s)==5 && s.wifi_selected==0);
    /* Bands: RIGHT from both to 5 GHz only; LEFT from both to 2.4 GHz only. */
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_WIFI_BAND && s.wifi_request.action==KUI_WIFI_BAND);
    assert(s.wifi_request.band==KWM_BAND_5);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_WIFI_BAND && s.wifi_request.band==KWM_BAND_24);
    /* An open network joins at once. */
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.wifi_selected==4);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.wifi_selected==3);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_WIFI_JOIN && s.page==KUI_SHELL_WIFI);
    assert(!strcmp(s.wifi_request.ssid,"Cafe") && !s.wifi_request.password[0]);
    assert(s.wifi_request.action==KUI_WIFI_JOIN && s.wifi_request.band==KWM_BAND_KEEP);
    /* A secured one asks for its password: 8 to 63 characters. */
    s.wifi_selected=2;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && s.wifi_keyboard);
    assert(!s.wifi_typing_name && !strcmp(s.wifi_request.ssid,"Home") && !s.keyboard[0]);
    strcpy(s.keyboard,"short");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && strstr(s.destination_notice,"8 to 63"));
    /* Y cycles lowercase, uppercase, symbols; the symbols reach passwords. */
    strcpy(s.keyboard,"correct horse");s.keyboard_selected=0;
    assert(s.keyboard_layer==KUI_SHELL_KEYS_LOWER);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.keyboard_layer==KUI_SHELL_KEYS_UPPER);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !strcmp(s.keyboard,"correct horseQ"));
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.keyboard_layer==KUI_SHELL_KEYS_SYMBOLS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !strcmp(s.keyboard,"correct horseQ!"));
    s.keyboard_selected=20;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && !strcmp(s.keyboard,"correct horseQ!\\"));
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.keyboard_layer==KUI_SHELL_KEYS_LOWER);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_WIFI_JOIN && s.page==KUI_SHELL_WIFI && !s.wifi_keyboard);
    assert(!strcmp(s.wifi_request.password,"correct horseQ!\\") && !s.keyboard[0]);
    /* Passwords stop at 64 characters. */
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD);
    memset(s.keyboard,'k',64);s.keyboard[64]=0;s.keyboard_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && strlen(s.keyboard)==64 && strstr(s.destination_notice,"at most 64"));
    /* B leaves the keyboard without joining; what was typed is wiped. */
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_WIFI && !s.wifi_keyboard && !s.keyboard[0]);
    /* Another network by name, then its password; none: an open network. */
    s.wifi_selected=4;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && s.wifi_typing_name);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && strstr(s.destination_notice,"name first"));
    memset(s.keyboard,'n',32);s.keyboard[32]=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && strlen(s.keyboard)==32 && strstr(s.destination_notice,"at most 32"));
    strcpy(s.keyboard,"Hidden net");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && !s.wifi_typing_name);
    assert(!strcmp(s.wifi_request.ssid,"Hidden net") && !s.keyboard[0]);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_WIFI_JOIN && s.page==KUI_SHELL_WIFI && !s.wifi_request.password[0]);
    /* Forget asks first: B cancels, A confirms. */
    view.wifi.state=KWM_WIFI_ONLINE;strcpy(view.wifi.ssid,"Home");view.wifi.saved=1;
    kui_shell_set_wifi(&s,&view);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.confirm_wifi_forget);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.confirm_wifi_forget);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_wifi_forget && s.page==KUI_SHELL_WIFI);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.confirm_wifi_forget);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_WIFI_FORGET && !s.confirm_wifi_forget);
    assert(s.wifi_request.action==KUI_WIFI_FORGET);
    /* While a job runs, B stops it and the page stays. */
    assert(press(KUI_SHELL_A|KUI_SHELL_X|KUI_SHELL_Y|KUI_SHELL_RIGHT,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_B,true)==KUI_SHELL_STOP && s.page==KUI_SHELL_WIFI);
    /* A view without a scan keeps the list, as does one just starting;
     * a board that has gone clears it. */
    struct kui_wifi_view joined=view;
    joined.scanned=false;joined.count=0;strcpy(joined.message,"Online on Home");
    kui_shell_set_wifi(&s,&joined);
    assert(s.wifi.count==3 && !strcmp(s.wifi.networks[1].ssid,"Home") && !strcmp(s.wifi.message,"Online on Home"));
    struct kui_wifi_view starting;
    memset(&starting,0,sizeof(starting));
    starting.working=true;strcpy(starting.message,"Looking for the Wi-Fi board");
    kui_shell_set_wifi(&s,&starting);
    assert(s.wifi.found && s.wifi.count==3 && strstr(s.wifi.board,"ESP32-C5") && s.wifi.working);
    struct kui_wifi_view gone;
    memset(&gone,0,sizeof(gone));
    gone.failed=true;strcpy(gone.message,"No Wi-Fi board answered on the SCI port");
    s.wifi_selected=4;kui_shell_set_wifi(&s,&gone);
    assert(!s.wifi.found && !s.wifi.count && s.wifi_selected==0);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_NETWORK);
    puts("PASS shell Wi-Fi controls: open, bands, open and secured joins, keyboard symbols and limits, by name, forget");
}
static void wifi_rendering(void) {
    struct kui_shell_view v={.build="a1b2c3d4e5f6",.busy=true};
    reset(KUI_SHELL_WIFI);render(&v);
    assert(strstr(drawn,"Wi-Fi") && strstr(drawn,"Looking for the Wi-Fi board") && strstr(drawn,"B Stop safely"));
    struct kui_wifi_view gone;
    memset(&gone,0,sizeof(gone));
    gone.failed=true;strcpy(gone.message,"No Wi-Fi board answered on the SCI port");
    kui_shell_set_wifi(&s,&gone);v.busy=false;render(&v);
    assert(strstr(drawn,"No Wi-Fi board answered") && strstr(drawn,"READY to GPIO5") && strstr(drawn,"X Look again"));
    struct kui_wifi_view view=three_networks();
    kui_shell_set_wifi(&s,&view);render(&v);
    assert(strstr(drawn,"XIAO ESP32-C5, firmware 0.1.0") && strstr(drawn,"No network set up: choose one below"));
    assert(strstr(drawn,"Bands: 2.4 and 5 GHz") && strstr(drawn,"LEFT/RIGHT"));
    assert(strstr(drawn,"Home 5G") && strstr(drawn,"5 GHz ch 36") && strstr(drawn,"WPA2") && strstr(drawn,"WPA3"));
    assert(strstr(drawn,"2.4 GHz ch 1") && strstr(drawn,"Open") && strstr(drawn,"Other network (type its name)"));
    assert(strstr(drawn,"3 networks in range") && strstr(drawn,"A Join   X Scan   Y Forget   START Update   B Network"));
    view.wifi.state=KWM_WIFI_ONLINE;strcpy(view.wifi.ssid,"Home 5G");view.wifi.band=5;view.wifi.rssi=-48;
    view.wifi.ip[0]=192;view.wifi.ip[1]=168;view.wifi.ip[2]=1;view.wifi.ip[3]=23;
    kui_shell_set_wifi(&s,&view);render(&v);
    assert(strstr(drawn,"Online on Home 5G as 192.168.1.23 (5 GHz, -48 dBm)") && strstr(drawn,"Joined"));
    view.wifi.state=KWM_WIFI_BAD_PASSWORD;kui_shell_set_wifi(&s,&view);render(&v);
    assert(strstr(drawn,"Home 5G: wrong password"));
    /* A long list scrolls with the selection. */
    view.count=KUI_WIFI_NETWORKS;
    for(unsigned i=3;i<KUI_WIFI_NETWORKS;i++) {
        snprintf(view.networks[i].ssid,sizeof(view.networks[i].ssid),"Network %02u",i);
        view.networks[i].channel=6;view.networks[i].rssi=(int8_t)(-60-(int)i);view.networks[i].security=3;
    }
    kui_shell_set_wifi(&s,&view);
    s.wifi_selected=kui_shell_wifi_rows(&s)-1;render(&v);
    assert(strstr(drawn,"Other network") && strstr(drawn,"Network 23") && !strstr(drawn,"Bands:") && strstr(drawn,"24 networks"));
    /* The password keyboard, with its symbols. */
    s.wifi_selected=1;assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    s.keyboard_layer=KUI_SHELL_KEYS_SYMBOLS;strcpy(s.keyboard,"p@ss");render(&v);
    assert(strstr(drawn,"Password for Home 5G") && strstr(drawn,"Y Shift: symbols") && strstr(drawn,"p@ss_"));
    assert(strstr(drawn,"8 to 63 characters") && strstr(drawn,"#") && strstr(drawn,"DONE"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE);
    s.wifi_selected=kui_shell_wifi_rows(&s)-1;assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);render(&v);
    assert(strstr(drawn,"Wi-Fi network name") && strstr(drawn,"open network"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE);
    view.wifi.state=KWM_WIFI_ONLINE;view.wifi.saved=1;kui_shell_set_wifi(&s,&view);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE);render(&v);
    assert(strstr(drawn,"FORGET THIS WI-FI NETWORK?") && strstr(drawn,"A Forget") && strstr(drawn,"B Cancel"));
    /* The FTP page over Wi-Fi. */
    reset(KUI_SHELL_FTP);
    struct kui_ftp_status f;
    memset(&f,0,sizeof(f));
    f.state=KUI_FTP_READY;f.wifi=true;f.port=KUI_FTP_PORT;f.ip[0]=192;f.ip[1]=168;f.ip[2]=1;f.ip[3]=23;
    snprintf(f.adapter,sizeof(f.adapter),"Wi-Fi: Home 5G, 5 GHz channel 36, -48 dBm");
    v.ftp=&f;f.link=true;render(&v);
    assert(strstr(drawn,"ftp://192.168.1.23") && strstr(drawn,"Wi-Fi: Home 5G, 5 GHz channel 36"));
    f.link=false;render(&v);
    assert(strstr(drawn,"Wi-Fi connection lost; the board is reconnecting") && !strstr(drawn,"No cable link"));
    puts("PASS shell Wi-Fi rendering: looking, no board, networks, online, scrolling, password keyboard, forget, FTP over Wi-Fi");
}
static struct kui_wifi_view firmware_preview(void) {
    struct kui_wifi_view view=three_networks();
    view.firmware_ready=true;
    view.firmware.bytes=128u*1024u;
    view.firmware.chip=5;
    strcpy(view.firmware.version,"0.1.2-21d3b1c5");
    strcpy(view.firmware.path,"/KUI/firmware/kui-wifi-esp32c5-update.bin");
    for(unsigned i=0;i<sizeof(view.firmware.sha256);i++) view.firmware.sha256[i]=(uint8_t)(i*7u);
    for(unsigned i=0;i<sizeof(view.firmware.mac);i++) view.firmware.mac[i]=(uint8_t)(i+1u);
    strcpy(view.message,"Firmware image checked; review before updating");
    return view;
}
static void wifi_update_controls(void) {
    reset(KUI_SHELL_WIFI);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
    struct kui_wifi_view view=three_networks();
    kui_shell_set_wifi(&s,&view);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_WIFI_UPDATE_CHECK);
    assert(s.wifi_request.action==KUI_WIFI_UPDATE_CHECK && !s.confirm_wifi_update);
    /* Looking at an image is a separate action from writing it. Busy A,
     * START and join controls cannot commit before the preview arrives. */
    assert(press(KUI_SHELL_A|KUI_SHELL_START|KUI_SHELL_Y,true)==KUI_SHELL_NONE);
    view=firmware_preview();view.working=true;
    kui_shell_set_wifi(&s,&view);
    assert(!s.confirm_wifi_update);
    view.working=false;kui_shell_set_wifi(&s,&view);
    assert(s.confirm_wifi_update && kui_shell_wifi_rows(&s)==5);
    assert(press(KUI_SHELL_X|KUI_SHELL_START,false)==KUI_SHELL_NONE);
    /* Cancel and even A+B must not write or leave the Wi-Fi page. */
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE);
    assert(!s.confirm_wifi_update && s.page==KUI_SHELL_WIFI);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_WIFI_UPDATE_CHECK);
    kui_shell_set_wifi(&s,&view);
    assert(s.confirm_wifi_update);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_WIFI_UPDATE);
    assert(!s.confirm_wifi_update && s.wifi_request.action==KUI_WIFI_UPDATE);
    assert(!memcmp(&s.wifi_request.firmware,&view.firmware,sizeof(view.firmware)));
    view.firmware_ready=false;view.firmware_updating=true;view.working=true;
    kui_shell_set_wifi(&s,&view);
    assert(press(KUI_SHELL_B,true)==KUI_SHELL_STOP);
    view.firmware_committing=true;kui_shell_set_wifi(&s,&view);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(s.page==KUI_SHELL_WIFI && !s.confirm_wifi_update);
    /* Failed or stale image publications cannot offer a commit. */
    view=firmware_preview();view.failed=true;
    kui_shell_set_wifi(&s,&view);
    assert(!s.confirm_wifi_update);
    reset(KUI_SHELL_NETWORK);view.failed=false;kui_shell_set_wifi(&s,&view);
    assert(!s.confirm_wifi_update);
    puts("PASS shell Wi-Fi update: check first, explicit pinned confirmation, cancel priority, commit lock, failed and stale guards");
}
static void wifi_update_rendering(void) {
    struct kui_shell_view v={.build="a1b2c3d4e5f6"};
    reset(KUI_SHELL_WIFI);
    struct kui_wifi_view view=firmware_preview();
    kui_shell_set_wifi(&s,&view);render(&v);
    assert(strstr(drawn,"UPDATE WI-FI FIRMWARE?") && strstr(drawn,"ESP32-C5 firmware 0.1.2-21d3b1c5"));
    assert(strstr(drawn,view.firmware.path) && strstr(drawn,"128 KB"));
    assert(strstr(drawn,"Saved Wi-Fi settings are kept") && strstr(drawn,"A Update firmware   B Cancel"));
    view.firmware_ready=false;view.firmware_updating=true;view.working=true;
    view.firmware_total=view.firmware.bytes;view.firmware_done=view.firmware.bytes/2u;
    strcpy(view.message,"Sending firmware to the Wi-Fi board");
    kui_shell_set_wifi(&s,&view);v.busy=true;render(&v);
    assert(strstr(drawn,"Updating the Wi-Fi board") && strstr(drawn,"Sending firmware"));
    assert(strstr(drawn,"64.0 KB / 128 KB") && strstr(drawn,"B Stop safely"));
    view.firmware_committing=true;kui_shell_set_wifi(&s,&view);render(&v);
    assert(strstr(drawn,"Finishing and checking the restarted board") && strstr(drawn,"keep the console powered on"));
    assert(!strstr(drawn,"B Stop safely"));
    puts("PASS shell Wi-Fi update rendering: version, image, size, confirmation, transfer progress, finishing state");
}
static void ripper_label_keyboard_purpose_controls(void) {
    /* Each existing keyboard entry owns its purpose even if an old capture
     * label mode remained set. Its completion must keep the capture label. */
    reset(KUI_SHELL_FILES);files_root();
    strcpy(s.capture_folder_label,"Keep label");s.capture_label_keyboard=true;
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && s.files_keyboard && !s.capture_label_keyboard);
    strcpy(s.keyboard,"Created folder");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_FILES_RUN && s.page==KUI_SHELL_FILES && !s.files_keyboard);
    assert(!strcmp(s.files_job.name,"Created folder") && !strcmp(s.capture_folder_label,"Keep label"));
    reset(KUI_SHELL_STORAGE_TESTS);s.storage_test_selected=3;
    strcpy(s.capture_folder_label,"Keep label");s.capture_label_keyboard=true;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && s.storage_test_keyboard && !s.capture_label_keyboard);
    strcpy(s.keyboard,"Card nickname");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_STORAGE_TESTS && !s.storage_test_keyboard);
    assert(!strcmp(s.storage_test_request.card_label,"Card nickname") && !strcmp(s.capture_folder_label,"Keep label"));
    reset(KUI_SHELL_WIFI);struct kui_wifi_view view=three_networks();kui_shell_set_wifi(&s,&view);
    strcpy(s.capture_folder_label,"Keep label");s.capture_label_keyboard=true;s.wifi_selected=4;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && s.wifi_keyboard && !s.capture_label_keyboard);
    strcpy(s.keyboard,"Other network");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && !s.wifi_typing_name);
    assert(!strcmp(s.wifi_request.ssid,"Other network") && !strcmp(s.capture_folder_label,"Keep label"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_WIFI && !s.wifi_keyboard && !s.capture_label_keyboard);
    puts("PASS shell capture label keyboard isolation: File Manager, card nickname and Wi-Fi retain their own actions");
}
static void games_remembered_selection(void) {
    reset(KUI_SHELL_HOME);
    struct kui_games_selection remembered={"/Games/Arcade","Game10.gdi"};
    kui_shell_set_games_selection(&s,&remembered);
    while(kui_shell_home_pages[s.home_selected]!=KUI_SHELL_GAMES)++s.home_selected;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST && s.games_seek && !strcmp(s.games_path,remembered.root));
    assert(!strcmp(s.games_seek_name,remembered.name));
    struct kui_games_page page={.count=3,.total=11,.view=KUI_GAMES_VIEW_LIST};strcpy(page.root,remembered.root);
    for(unsigned i=0;i<3;i++) {
        snprintf(page.entries[i].name,sizeof(page.entries[i].name),"Game%02u.gdi",i+8u);
        snprintf(page.entries[i].path,sizeof(page.entries[i].path),"/Games/Arcade/Game%02u.gdi",i+8u);
    }
    unsigned epoch=s.games_generation;s.games_view=KUI_GAMES_VIEW_LIST;
    assert(!kui_shell_set_games_found_rows(&s,&page,epoch-1,8,KUI_GAMES_VIEW_LIST,2) && s.games_seek);
    assert(kui_shell_set_games_found_rows(&s,&page,epoch,8,KUI_GAMES_VIEW_LIST,2));
    assert(s.games_page==1 && s.games_selected==2 && !s.games_seek);
    struct kui_games_selection got;assert(kui_shell_games_selection(&s,&got) && kui_games_selection_equal(&got,&remembered));
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && !s.games_selected);
    press(0,false);assert(kui_shell_games_selection(&s,&got) && !strcmp(got.name,"Game08.gdi"));
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_GAMES_LIST && s.games_seek && !strcmp(s.games_seek_name,"Game08.gdi"));
    assert(kui_shell_set_games_found_rows(&s,&page,s.games_generation,8,KUI_GAMES_VIEW_LIST,0));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_GAMES_LIST && !strcmp(s.games_path,"/Games") && s.games_seek);
    assert(!strcmp(s.games_seek_name,"Arcade"));
    memset(&page,0,sizeof(page));page.count=page.total=1;page.view=KUI_GAMES_VIEW_LIST;strcpy(page.root,"/Games");
    strcpy(page.entries[0].name,"Arcade");strcpy(page.entries[0].path,"/Games/Arcade/disc.gdi");
    assert(kui_shell_set_games_found_rows(&s,&page,s.games_generation,0,KUI_GAMES_VIEW_LIST,0));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST && s.games_seek && !strcmp(s.games_seek_name,"Arcade"));
    strcpy(page.root,"/Unexpected");assert(!kui_shell_set_games_found_rows(&s,&page,s.games_generation,0,KUI_GAMES_VIEW_LIST,0));
    strcpy(page.root,"/Games");assert(kui_shell_set_games_found_rows(&s,&page,s.games_generation,0,KUI_GAMES_VIEW_LIST,0));
    remembered=(struct kui_games_selection){"/Games/Removed","Gone.gdi"};kui_shell_set_games_selection(&s,&remembered);
    s.page=KUI_SHELL_HOME;assert(press(KUI_SHELL_A,false)==KUI_SHELL_GAMES_LIST);
    assert(kui_shell_set_games_found_rows(&s,&page,s.games_generation,0,KUI_GAMES_VIEW_LIST,0) && !strcmp(s.games_path,"/Games"));
    struct kui_games_selection invalid=remembered;strcpy(invalid.root,"../escape");kui_shell_set_games_selection(&s,&invalid);
    assert(kui_shell_games_selection(&s,&got) && !strcmp(got.root,"/Games"));
    puts("PASS Games selection: cold folder/name restoration after sorting, exact page row, stale guards, parent anchor, refresh and missing-folder fallback");
}

static void small_tools_controls(void) {
    /* Full image backup includes an empty readable card; existing selected
     * and all-save actions retain their independent dispatch. */
    reset(KUI_SHELL_VMU);s.vmu.present=true;
    assert(press(KUI_SHELL_L,true)==KUI_SHELL_MSTATS && s.page==KUI_SHELL_VMU);
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_ACTIONS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    press(KUI_SHELL_DOWN,false);assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    press(KUI_SHELL_DOWN,false);assert(s.vmu_action_selected==2);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP && s.page==KUI_SHELL_VMU_ACTIONS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_BACKUP_CARD);
    s.vmu.present=false;assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    press(KUI_SHELL_B,false);assert(s.page==KUI_SHELL_VMU);
    s.vmu.present=true;s.vmu.count=s.vmu.total=1;
    assert(vmu_backup_selected()==KUI_SHELL_VMU_BACKUP);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VMU_BACKUP_ALL);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_VMU_BACKUPS_LIST);
    struct kui_vmu_backup_view backups={.page=0,.count=2,.total=2};
    strcpy(backups.entries[0].path,"/KUI/backups/vmu/A1-0001/card.VMD");
    strcpy(backups.entries[0].name,"card.VMD");
    strcpy(backups.entries[1].path,"/KUI/backups/vmu/A1-0002/TESTSAVE.vms");
    strcpy(backups.entries[1].name,"TESTSAVE");
    kui_shell_set_vmu_backups(&s,&backups);s.vmu_slot=5;
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_RESTORE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_IMAGE_LIST && s.page==KUI_SHELL_VMU_IMAGE);
    assert(!strcmp(s.vmu_image_path,backups.entries[0].path) && !s.restore_path[0] && !s.confirm_vmu_restore);
    assert(s.vmu_slot==5 && s.vmu.count==1 && !s.vmu_image.count);
    struct kui_vmu_view image={.page=0,.count=2,.total=10,.present=true};
    strcpy(image.entries[0].name,"FIRST_SAVE");strcpy(image.entries[1].name,"SECOND_SAVE");
    kui_shell_set_vmu_image(&s,"/other.vmd",0,&image);assert(!s.vmu_image.count);
    image.page=1;kui_shell_set_vmu_image(&s,s.vmu_image_path,image.page,&image);assert(!s.vmu_image.count);
    image.page=0;kui_shell_set_vmu_image(&s,s.vmu_image_path,image.page,&image);assert(s.vmu_image.count==2);
    press(KUI_SHELL_DOWN,false);assert(s.vmu_image_selected==1);
    assert(press(KUI_SHELL_X,true)==KUI_SHELL_NONE && s.vmu_image_selected==1);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_VMU_IMAGE_EXTRACT && s.vmu_image_selected==1);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VMU_IMAGE_EXTRACT_ALL);
    assert(s.vmu_slot==5 && s.vmu_selected==0 && !s.confirm_vmu_restore);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_VMU_IMAGE_LIST && s.vmu_image_page==1);
    assert(!s.vmu_image.count && s.vmu_image_selected==0);
    kui_shell_set_vmu_image(&s,s.vmu_image_path,image.page,&image);assert(!s.vmu_image.count);
    image.page=1;image.status.stopped=true;
    kui_shell_set_vmu_image(&s,s.vmu_image_path,image.page,&image);assert(s.vmu_image.present);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_VMU_IMAGE_EXTRACT); /* Retry after stop. */
    image.page=0;kui_shell_set_vmu_image(&s,s.vmu_image_path,1,&image);
    assert(s.vmu_image_page==0 && s.vmu_image.count==2); /* Refresh can return a shorter image. */
    assert(press(KUI_SHELL_B|KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_RESTORE);
    assert(s.backups.count==2 && s.backup_selected==0 && s.vmu_slot==5);
    kui_shell_set_vmu_image(&s,s.vmu_image_path,image.page,&image);assert(s.page==KUI_SHELL_VMU_RESTORE);
    s.backup_selected=1;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_RESTORE_PREVIEW);
    assert(!strcmp(s.restore_path,backups.entries[1].path)); /* Original physical restore. */
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU);
    for(unsigned bios=0;bios<2;bios++) {
        reset(KUI_SHELL_SYSTEM_TOOLS);s.tools_selected=4+bios;
        strcpy(s.files_path,"/Music");
        assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && !s.files_pick_compare);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_LIST && s.page==KUI_SHELL_FILES_PICK);
        assert(s.files_pick_compare && s.system_compare_bios==(bool)bios && !s.files_request.folders_only);
        assert(!strcmp(s.files_path,"/Music") && !strcmp(s.files_request.path,"/KUI/backups"));
        const char *names[]={"system","image.bin","bad.bin","bad/name"};
        const bool dirs[]={true,false,false,false};
        struct kui_files_page page=files_result("/KUI/backups",false,4,0,12,names,dirs);
        page.entries[1].bytes=bios?2097152u:131072u;
        page.entries[2].bytes=512;
        struct kui_files_page stale=page;stale.folders_only=true;
        kui_shell_set_files_listing(&s,&stale);assert(!s.files_pick.count);
        strcpy(stale.path,"/other");stale.folders_only=false;
        kui_shell_set_files_listing(&s,&stale);assert(!s.files_pick.count);
        kui_shell_set_files_listing(&s,&page);assert(s.files_pick.count==4 && s.files_pick.entries[3].disabled);
        assert(!s.files_listing.count); /* File choice stays outside the ordinary browser. */
        assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES_PICK);
        s.files_pick_selected=2;
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && strstr(s.files_pick.message,"exact"));
        assert(!s.system_compare_path[0]);
        s.files_pick_selected=3;assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
        s.files_pick_selected=0;
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_LIST && !strcmp(s.files_pick_path,"/KUI/backups/system"));
        assert(!s.files_request.folders_only);
        assert(press(KUI_SHELL_B,false)==KUI_SHELL_FILES_LIST && !strcmp(s.files_pick_path,"/KUI/backups"));
        kui_shell_set_files_listing(&s,&page);s.files_pick_selected=1;
        assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.files_pick_compare);
        assert(press(KUI_SHELL_A,false)==(bios?KUI_SHELL_BIOS_COMPARE:KUI_SHELL_FLASH_COMPARE));
        assert(s.page==KUI_SHELL_SYSTEM_TOOLS && !s.files_pick_compare);
        assert(!strcmp(s.system_compare_path,"/KUI/backups/image.bin") && !strcmp(s.files_path,"/Music"));
        assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP && s.page==KUI_SHELL_SYSTEM_TOOLS);
        assert(!strcmp(s.system_compare_path,"/KUI/backups/image.bin"));
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_LIST);
        assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SYSTEM_TOOLS && !s.files_pick_compare);
        press(KUI_SHELL_A,false);
        while(strcmp(s.files_pick_path,"/")) assert(press(KUI_SHELL_B,false)==KUI_SHELL_FILES_LIST);
        assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_SYSTEM_TOOLS && !s.files_pick_compare);
    }
    puts("PASS small tools: empty VMU card backup, VMD browse/extract separate from restore, safe file selection, exact size checks, stale guards and Stop ownership");
}
static void small_tools_rendering(void) {
    struct kui_shell_view view={0};
    reset(KUI_SHELL_VMU_ACTIONS);s.vmu_action_selected=2;render(&view);
    assert(strstr(drawn,"Back up card image") && strstr(drawn,"all 128 KiB") && strstr(drawn,"No selected save"));
    reset(KUI_SHELL_VMU_IMAGE);strcpy(s.vmu_image_path,"/KUI/backups/vmu/A1-0001/card.vmd");
    s.vmu_image.present=true;s.vmu_image.count=8;s.vmu_image.total=13;
    for(unsigned i=0;i<8;i++) {
        snprintf(s.vmu_image.entries[i].name,16,"SAVE_%02u",i);s.vmu_image.entries[i].bytes=32768;
    }
    s.vmu_image_selected=7;render(&view);
    assert(strstr(drawn,"VMU backup / Saves") && strstr(drawn,"SAVE_07") && strstr(drawn,"X Extract selected"));
    assert(strstr(drawn,"verified SD backups only") && !strstr(drawn,"WRITE THIS SAVE"));
    s.vmu_image.count=0;render(&view);assert(strstr(drawn,"No saves on this page"));
    view.busy=true;render(&view);assert(strstr(drawn,"Reading card image"));
    view.busy=false;reset(KUI_SHELL_SYSTEM_TOOLS);s.tools_selected=5;
    struct kui_app_status status={.complete=true,.line_count=12};
    strcpy(status.message,"Different: 42 bytes; first at 0x000020.");
    strcpy(status.lines[10],"Visible BIOS differs at 0x000020");
    strcpy(status.lines[11],"Console memory and SD image were not changed.");
    view.app_status=&status;render(&view);
    assert(strstr(drawn,"Compare settings flash with SD image") && strstr(drawn,"Compare visible BIOS with SD image"));
    assert(strstr(drawn,"Different: 42 bytes") && strstr(drawn,"not changed"));
    view.app_status=NULL;
    press(KUI_SHELL_A,false);
    static const char *names[]={"system","bios-0001.bin","short.bin"};
    static const bool dirs[]={true,false,false};
    struct kui_files_page files=files_result("/KUI/backups",false,3,0,3,names,dirs);
    files.entries[1].bytes=2097152u;kui_shell_set_files_listing(&s,&files);render(&view);
    assert(strstr(drawn,"Choose a BIOS image") && strstr(drawn,"bios-0001.bin") && strstr(drawn,"2.0 MB"));
    assert(strstr(drawn,"Comparison is read-only") && !strstr(drawn,"Y Move here") && !strstr(drawn,"Y Copy here"));
    s.system_compare_bios=false;render(&view);assert(strstr(drawn,"128 KiB") && strstr(drawn,"settings-flash image"));
    puts("PASS small tools rendering: card image actions, eight image rows, progress, compare file sizes and outcome details fit safe margins");
}


/* The bulk operation confirms a fresh snapshot of both cards. A stopped or
 * uncertain previous attempt may only be retried through another preview. */
static void vmu_copy_all_controls(void) {
    reset(KUI_SHELL_VMU);s.vmu_slot=7;
    assert(press(KUI_SHELL_L,true)==KUI_SHELL_MSTATS && s.page==KUI_SHELL_VMU);
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_ACTIONS);
    assert(s.vmu_copy_slot==0);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.vmu_action_selected==5);
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.vmu_action_selected==0);
    press(KUI_SHELL_UP,false);press(KUI_SHELL_UP,false);press(KUI_SHELL_UP,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE); /* No readable source. */
    s.vmu.present=true;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE); /* Empty source. */
    s.vmu.total=12;s.vmu.count=0;
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==8);
    assert(press(KUI_SHELL_LEFT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==6);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==8);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.vmu_copy_slot==0);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && !s.confirm_vmu_copy_all);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COPY_ALL_PREVIEW);
    assert(!s.confirm_vmu_copy_all && !s.vmu.count && s.vmu.total==12);
    struct kui_vmu_view ready={.slot=7,.copy_ready=true,.bulk_total=12,.bulk_remaining=12};
    ready.status.complete=ready.status.passed=true;
    kui_shell_set_vmu_copy_all(&s,6,0,false,&ready);assert(!s.confirm_vmu_copy_all);
    kui_shell_set_vmu_copy_all(&s,7,1,false,&ready);assert(!s.confirm_vmu_copy_all);
    s.vmu_action_selected=2;kui_shell_set_vmu_copy_all(&s,7,0,false,&ready);
    assert(!s.confirm_vmu_copy_all);s.vmu_action_selected=3;
    /* A result with partial or unverified work is never a new write permit. */
    struct kui_vmu_view refused=ready;refused.bulk_remaining=11;refused.bulk_completed=1;
    kui_shell_set_vmu_copy_all(&s,7,0,false,&refused);assert(!s.confirm_vmu_copy_all);
    refused=ready;refused.bulk_unverified=1;
    kui_shell_set_vmu_copy_all(&s,7,0,false,&refused);assert(!s.confirm_vmu_copy_all);
    refused=ready;refused.bulk_total=refused.bulk_remaining=0;
    kui_shell_set_vmu_copy_all(&s,7,0,false,&refused);assert(!s.confirm_vmu_copy_all);
    refused=ready;refused.status.passed=false;
    kui_shell_set_vmu_copy_all(&s,7,0,false,&refused);assert(!s.confirm_vmu_copy_all);
    refused=ready;refused.status.stopped=true;
    kui_shell_set_vmu_copy_all(&s,7,0,false,&refused);assert(!s.confirm_vmu_copy_all);
    refused=ready;refused.status.errors=1;
    kui_shell_set_vmu_copy_all(&s,7,0,false,&refused);assert(!s.confirm_vmu_copy_all);
    refused=ready;refused.status.complete=false;
    kui_shell_set_vmu_copy_all(&s,7,0,false,&refused);assert(!s.confirm_vmu_copy_all);
    refused=ready;refused.slot=0;
    kui_shell_set_vmu_copy_all(&s,7,0,false,&refused);assert(!s.confirm_vmu_copy_all);
    kui_shell_set_vmu_copy_all(&s,7,0,false,&ready);assert(s.confirm_vmu_copy_all);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.confirm_vmu_copy_all);
    assert(press(KUI_SHELL_LEFT|KUI_SHELL_START,false)==KUI_SHELL_NONE && s.vmu_copy_slot==0);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_vmu_copy_all);
    assert(s.page==KUI_SHELL_VMU_ACTIONS);
    kui_shell_set_vmu_copy_all(&s,7,0,false,&ready);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COPY_ALL_COMMIT && !s.confirm_vmu_copy_all);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    struct kui_vmu_view partial=ready;partial.copy_ready=false;
    partial.status.passed=false;partial.status.stopped=true;
    partial.bulk_completed=4;partial.bulk_unverified=1;partial.bulk_remaining=7;
    kui_shell_set_vmu_copy_all(&s,7,0,true,&partial);
    assert(!s.confirm_vmu_copy_all && s.vmu_copy_all.bulk_completed==4 && s.vmu_copy_all.bulk_unverified==1);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COPY_ALL_PREVIEW && !s.confirm_vmu_copy_all);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COPY_ALL_PREVIEW); /* Never recommit stale state. */
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_LIST && s.page==KUI_SHELL_VMU);
    kui_shell_set_vmu_copy_all(&s,7,0,false,&ready);assert(!s.confirm_vmu_copy_all);
    puts("PASS VMU bulk controls: empty cards, no selected row, both-slot stale guards, explicit confirmation and partial retry require fresh preview");
}
/* External saves are staged on SD, then appear in the existing restore list.
 * Neither picking a file nor returning from the import screen restores a VMU. */
static void vmu_import_controls(void) {
    static const char *const names[]={"Saves","MDK2.VMS","Grandia.vMi","Sonic.DcI","readme.txt","bad/name.vms","save.vms.txt"};
    static const bool dirs[]={true,false,false,false,false,false,false};
    reset(KUI_SHELL_VMU_RESTORE);strcpy(s.files_path,"/Music");
    assert(!s.vmu.present);
    assert(press(KUI_SHELL_Y,true)==KUI_SHELL_NONE && !s.files_pick_import);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_FILES_LIST && s.page==KUI_SHELL_FILES_PICK);
    assert(s.files_pick_import && !s.files_pick_compare && !s.files_request.folders_only);
    assert(!strcmp(s.files_pick_path,"/") && !strcmp(s.files_request.path,"/") && !strcmp(s.files_path,"/Music"));
    struct kui_files_page page=files_result("/",false,7,0,7,names,dirs);
    struct kui_files_page stale=page;stale.folders_only=true;
    kui_shell_set_files_listing(&s,&stale);assert(!s.files_pick.count);
    stale=page;strcpy(stale.path,"/Other");
    kui_shell_set_files_listing(&s,&stale);assert(!s.files_pick.count);
    kui_shell_set_files_listing(&s,&page);
    assert(s.files_pick.count==7 && !s.files_pick.entries[0].disabled);
    for(unsigned i=1;i<=3;i++) assert(!s.files_pick.entries[i].disabled);
    for(unsigned i=4;i<=6;i++) assert(s.files_pick.entries[i].disabled);
    assert(!s.files_listing.count);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_FILES_PICK);
    s.files_pick_selected=4;assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    s.files_pick_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_FILES_LIST && !strcmp(s.files_pick_path,"/Saves"));
    assert(!s.files_request.folders_only && s.files_pick_import);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_FILES_LIST && !strcmp(s.files_pick_path,"/"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_RESTORE && !s.files_pick_import);
    press(KUI_SHELL_Y,false);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_RESTORE && !s.files_pick_import);
    for(unsigned choice=1;choice<=3;choice++) {
        assert(press(KUI_SHELL_Y,false)==KUI_SHELL_FILES_LIST);
        kui_shell_set_files_listing(&s,&page);s.files_pick_selected=choice;
        unsigned generation=s.vmu_import_generation;
        assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.files_pick_import);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_IMPORT_PREVIEW && s.page==KUI_SHELL_VMU_IMPORT);
        assert(s.vmu_import_generation==generation+1 && !s.files_pick_import);
        char expected[KUI_FILES_PATH_CAP];snprintf(expected,sizeof(expected),"/%s",names[choice]);
        assert(!strcmp(s.vmu_import_path,expected) && !s.confirm_vmu_import && !s.vmu_import_staged);
        assert(!s.confirm_vmu_restore && !s.restore_path[0]);
        generation=s.vmu_import_generation;
        struct kui_vmu_view ready={.import_ready=true,.count=1,.total=1};
        ready.status.complete=ready.status.passed=true;
        strcpy(ready.entries[0].name,"IMPORTED_SAVE");ready.entries[0].bytes=4096;
        kui_shell_set_vmu_import(&s,"/Other.VMS",generation,false,&ready);
        assert(!s.confirm_vmu_import && !s.vmu_import.count);
        kui_shell_set_vmu_import(&s,expected,generation-1,false,&ready);
        assert(!s.confirm_vmu_import && !s.vmu_import.count);
        struct kui_vmu_view refused=ready;refused.count=0;
        kui_shell_set_vmu_import(&s,expected,generation,false,&refused);assert(!s.confirm_vmu_import);
        refused=ready;refused.entries[0].bytes=0;
        kui_shell_set_vmu_import(&s,expected,generation,false,&refused);assert(!s.confirm_vmu_import);
        refused=ready;refused.entries[0].name[0]=0;
        kui_shell_set_vmu_import(&s,expected,generation,false,&refused);assert(!s.confirm_vmu_import);
        refused=ready;refused.status.passed=false;
        kui_shell_set_vmu_import(&s,expected,generation,false,&refused);assert(!s.confirm_vmu_import);
        refused=ready;refused.status.complete=false;
        kui_shell_set_vmu_import(&s,expected,generation,false,&refused);assert(!s.confirm_vmu_import);
        refused=ready;refused.status.stopped=true;
        kui_shell_set_vmu_import(&s,expected,generation,false,&refused);assert(!s.confirm_vmu_import);
        refused=ready;refused.status.errors=1;
        kui_shell_set_vmu_import(&s,expected,generation,false,&refused);assert(!s.confirm_vmu_import);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_IMPORT_PREVIEW && s.vmu_import_generation==generation+1);
        kui_shell_set_vmu_import(&s,expected,generation,false,&ready);assert(!s.confirm_vmu_import);
        generation=s.vmu_import_generation;
        kui_shell_set_vmu_import(&s,expected,generation,false,&ready);assert(s.confirm_vmu_import);
        assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE && s.confirm_vmu_import);
        assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP && s.page==KUI_SHELL_VMU_IMPORT);
        assert(!s.confirm_vmu_import);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_IMPORT_PREVIEW && s.vmu_import_generation==generation+1);
        generation=s.vmu_import_generation;
        kui_shell_set_vmu_import(&s,expected,generation,false,&ready);assert(s.confirm_vmu_import);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_IMPORT_COMMIT && !s.confirm_vmu_import);
        ready.import_ready=false;
        strcpy(ready.backup_path,"/KUI/backups/vmu/import-0001/IMPORTED_SAVE.vms");
        kui_shell_set_vmu_import(&s,expected,generation,true,&ready);
        assert(s.vmu_import_staged && !s.confirm_vmu_import);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.vmu_import_generation==generation);
        assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_BACKUPS_LIST && s.page==KUI_SHELL_VMU_RESTORE);
        assert(!s.confirm_vmu_restore && !s.restore_path[0] && !s.vmu.present);
        kui_shell_set_vmu_import(&s,expected,generation,false,&ready);assert(!s.confirm_vmu_import);
    }
    /* Cancelling an uncommitted preview needs no list refresh or VMU write. */
    press(KUI_SHELL_Y,false);kui_shell_set_files_listing(&s,&page);s.files_pick_selected=1;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_IMPORT_PREVIEW);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_RESTORE);
    assert(!s.confirm_vmu_import && !s.confirm_vmu_restore && !strcmp(s.files_path,"/Music"));
    puts("PASS VMU import controls: mixed-case VMS/VMI/DCI selection, folders, unsupported names, busy lock, exact path/generation guards, staged-only completion and cancellation");
}


static void vmu_bulk_import_rendering(void) {
    struct kui_shell_view view={0};
    reset(KUI_SHELL_VMU_ACTIONS);s.vmu_slot=7;s.vmu_copy_slot=0;s.vmu_action_selected=3;
    s.vmu.present=true;s.vmu.total=12;
    s.vmu_copy_all.bulk_total=s.vmu_copy_all.bulk_remaining=12;render(&view);
    assert(strstr(drawn,"Source VMU D2: No selected save") && strstr(drawn,"Copy all saves to VMU A1"));
    assert(strstr(drawn,"Total 12   Verified 0   Remaining 12"));
    s.confirm_vmu_copy_all=true;render(&view);
    assert(strstr(drawn,"COPY ALL SAVES TO ANOTHER VMU?") && strstr(drawn,"12 saves from VMU D2 to VMU A1"));
    assert(strstr(drawn,"Existing destination names are refused.") && strstr(drawn,"A Copy all saves"));
    s.confirm_vmu_copy_all=false;s.vmu_copy_all.bulk_completed=4;s.vmu_copy_all.bulk_remaining=8;
    s.vmu_copy_all.bulk_unverified=true;render(&view);
    assert(strstr(drawn,"Total 12   Verified 4   Remaining 8") && strstr(drawn,"Destination save unverified"));
    assert(!strstr(drawn,"COPY ALL SAVES TO ANOTHER VMU?"));
    reset(KUI_SHELL_VMU_RESTORE);render(&view);assert(strstr(drawn,"Y Import save"));
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_FILES_LIST);
    static const char *const names[]={"Saves","GRANDIA2.VMS","SONICADV.vmi","TOYCOMMANDER.DCI","readme.txt"};
    static const bool dirs[]={true,false,false,false,false};
    struct kui_files_page files=files_result("/",false,5,0,5,names,dirs);
    kui_shell_set_files_listing(&s,&files);render(&view);
    assert(strstr(drawn,"Choose an external save") && strstr(drawn,"GRANDIA2.VMS") && strstr(drawn,"TOYCOMMANDER.DCI"));
    assert(strstr(drawn,"VMS, VMI or DCI") && strstr(drawn,"A Open folder / check save"));
    assert(!strstr(drawn,"Y Move here") && !strstr(drawn,"Y Copy here") && !strstr(drawn,"Comparison is read-only"));
    s.files_pick_selected=2;press(KUI_SHELL_A,false);
    struct kui_vmu_view imported={.count=1,.total=1,.import_ready=true};
    strcpy(imported.entries[0].name,"SONICADV_SYS");imported.entries[0].bytes=32768;
    imported.status.complete=imported.status.passed=true;imported.status.line_count=8;
    for(unsigned i=0;i<8;i++) snprintf(imported.status.lines[i],KUI_APP_LINE_CAP,"Import detail %u",i+1);
    kui_shell_set_vmu_import(&s,s.vmu_import_path,s.vmu_import_generation,false,&imported);render(&view);
    assert(strstr(drawn,"Import save to SD") && strstr(drawn,"/SONICADV.vmi"));
    assert(strstr(drawn,"SONICADV_SYS   32768 bytes / 64 blocks") && strstr(drawn,"Import detail 1") && strstr(drawn,"Import detail 8"));
    assert(strstr(drawn,"A Stage SD backup") && strstr(drawn,"Creates an SD backup only; restore separately from backups."));
    assert(!strstr(drawn,"WRITE THIS SAVE TO THE VMU?"));
    imported.import_ready=false;strcpy(imported.backup_path,"/KUI/backups/vmu/import-0001/SONICADV_SYS.vms");
    kui_shell_set_vmu_import(&s,s.vmu_import_path,s.vmu_import_generation,true,&imported);render(&view);
    assert(strstr(drawn,"Verified SD backup:") && strstr(drawn,imported.backup_path) && strstr(drawn,"No VMU was written."));
    assert(strstr(drawn,"Restore separately from the SD backups list.") && !strstr(drawn,"A Stage SD backup"));
    s.vmu_import_staged=false;s.vmu_import.count=0;s.vmu_import.status.line_count=0;view.busy=true;render(&view);
    assert(strstr(drawn,"Reading external save...") && !strstr(drawn,"A Stage SD backup"));
    puts("PASS VMU bulk/import rendering: both slots, total/verified/remaining counts, uncertain destination, external picker, eight metadata lines and staged SD-only result");
}

/* A virtual destination creates a new managed card. Preview publications must
 * belong to the same source selection and request before they can grant A. */
static const char *const invalid_vmu_collection_paths[]={
    "/KUI/vmu/cards/v0001/card.vmd",
    "0:/KUI/vmu/backups/v0001/card.vmd",
    "0:/KUI/vmu/cards/v0000/card.vmd",
    "0:/KUI/vmu/cards/v0001/../card.vmd",
    "0:/KUI/vmu/cards/v0001/card.VMD",
    "0:/KUI/vmu/cards/V0001/card.vmd",
    "1:/KUI/vmu/cards/v0001/card.vmd"
};
static void vmu_collection_copy_controls(void) {
    for(unsigned all=0;all<2;all++) {
        reset(KUI_SHELL_VMU_ACTIONS);s.vmu_slot=3;s.vmu_page=1;
        s.vmu_selected=1;s.vmu_action_selected=all?3:1;s.vmu_copy_slot=8;
        assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE); /* No source card. */
        s.vmu.present=true;
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE); /* No source saves. */
        s.vmu.count=2;s.vmu.total=3;
        strcpy(s.vmu.entries[0].name,"COMPANION");s.vmu.entries[0].bytes=512;
        strcpy(s.vmu.entries[1].name,"SELECTED");s.vmu.entries[1].bytes=4096;
        if(all) s.vmu.count=0; /* All saves does not require a selected row. */
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COLLECTION_COPY_PREVIEW);
        unsigned generation=s.vmu_collection_copy_generation;
        assert(generation && s.vmu_collection_copy_all==(bool)all);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all && !s.vmu_collection_copy_stored);
        struct kui_vmu_view ready={.slot=3,.page=1,.count=1,.total=all?3:1,
            .copy_ready=true,.bulk_total=all?3:1,.bulk_remaining=all?3:1};
        ready.status.complete=ready.status.passed=true;
        strcpy(ready.entries[0].name,"SELECTED");ready.entries[0].bytes=4096;
        struct kui_vmu_view physical=ready;physical.slot=8;
        kui_shell_set_vmu_copy_preview(&s,&physical);
        kui_shell_set_vmu_copy_all(&s,3,8,false,&ready);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all); /* No physical E1 destination. */
        kui_shell_set_vmu_collection_copy(&s,2,1,1,all,generation,false,&ready);
        kui_shell_set_vmu_collection_copy(&s,3,0,1,all,generation,false,&ready);
        kui_shell_set_vmu_collection_copy(&s,3,1,0,all,generation,false,&ready);
        kui_shell_set_vmu_collection_copy(&s,3,1,1,!all,generation,false,&ready);
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation+1,false,&ready);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all);
        s.vmu_copy_slot=0;
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&ready);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all);s.vmu_copy_slot=8;
        struct kui_vmu_view refused=ready;refused.status.stopped=true;
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&refused);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all);
        refused=ready;refused.status.passed=false;
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&refused);
        refused=ready;refused.status.complete=false;
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&refused);
        refused=ready;refused.status.errors=1;
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&refused);
        refused=ready;refused.bulk_remaining=0;
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&refused);
        refused=ready;refused.bulk_completed=1;
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&refused);
        refused=ready;refused.bulk_unverified=true;
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&refused);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all);
        if(!all) {
            refused=ready;memset(refused.entries[0].name,'x',sizeof(refused.entries[0].name));
            kui_shell_set_vmu_collection_copy(&s,3,1,1,false,generation,false,&refused);
            refused=ready;refused.entries[0].bytes=0;
            kui_shell_set_vmu_collection_copy(&s,3,1,1,false,generation,false,&refused);
            assert(!s.confirm_vmu_copy);
        }
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&ready);
        assert(s.confirm_vmu_copy==!all && s.confirm_vmu_copy_all==(bool)all);
        assert(press(KUI_SHELL_LEFT|KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.vmu_copy_slot==8);
        assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
        assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all);
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,generation,false,&ready);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all); /* Cancelled publication. */
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COLLECTION_COPY_PREVIEW);
        unsigned current=s.vmu_collection_copy_generation;assert(current!=generation);
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,current,false,&ready);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COLLECTION_COPY_COMMIT);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all);
        unsigned committing=s.vmu_collection_copy_generation;
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,current,false,&ready);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all); /* Consumed preview. */
        struct kui_vmu_view stored=ready;stored.copy_ready=false;
        for(unsigned i=0;i<sizeof(invalid_vmu_collection_paths)/sizeof(invalid_vmu_collection_paths[0]);i++) {
            strcpy(stored.backup_path,invalid_vmu_collection_paths[i]);
            kui_shell_set_vmu_collection_copy(&s,3,1,1,all,committing,true,&stored);
            assert(!s.vmu_collection_copy_stored && !s.confirm_vmu_copy && !s.confirm_vmu_copy_all);
        }
        strcpy(stored.backup_path,"0:/KUI/vmu/cards/v0001/card.vmd");
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,committing,true,&stored);
        assert(s.vmu_collection_copy_stored && !s.confirm_vmu_copy && !s.confirm_vmu_copy_all);
        assert(s.vmu_slot==3 && s.vmu_page==1 && s.vmu_selected==1 && s.vmu.total==3);
        assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_LIST && s.page==KUI_SHELL_VMU);
        kui_shell_set_vmu_collection_copy(&s,3,1,1,all,committing,false,&ready);
        assert(!s.confirm_vmu_copy && !s.confirm_vmu_copy_all);
    }
    puts("PASS virtual VMU copy: selected/all dispatch, unreadable and empty sources, exact async keys, failed previews, cancellation, one-use confirmation and stored-only completion");
}

static void vmu_collection_controls(void) {
    reset(KUI_SHELL_VMU);s.vmu_slot=5;s.backups.count=1;
    strcpy(s.backups.entries[0].path,"/KUI/backups/vmu/A1-0001/ORIGINAL.vms");
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.vmu_action_selected==5);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.vmu_action_selected==4);
    assert(press(KUI_SHELL_A,true)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COLLECTION_LIST && s.page==KUI_SHELL_VMU_COLLECTION);
    unsigned generation=s.vmu_collection_generation;
    struct kui_vmu_backup_view cards={.page=0,.count=3,.total=10};
    cards.entries[0].readable=true;
    strcpy(cards.entries[0].name,"Selected save");strcpy(cards.entries[0].folder,"v0001");
    strcpy(cards.entries[0].path,"0:/KUI/vmu/cards/v0001/card.vmd");
    strcpy(cards.entries[1].path,"0:/KUI/vmu/cards/v0002/save.vms");
    memset(cards.entries[2].path,'x',sizeof(cards.entries[2].path));
    kui_shell_set_vmu_collection(&s,generation+1,&cards);assert(!s.vmu_collection.count);
    cards.page=1;kui_shell_set_vmu_collection(&s,generation,&cards);assert(!s.vmu_collection.count);
    struct kui_vmu_backup_view invalid={.page=0,.count=1,.total=1};
    for(unsigned i=0;i<sizeof(invalid_vmu_collection_paths)/sizeof(invalid_vmu_collection_paths[0]);i++) {
        strcpy(invalid.entries[0].path,invalid_vmu_collection_paths[i]);
        kui_shell_set_vmu_collection(&s,generation,&invalid);
        assert(s.vmu_collection.count==1 && !s.vmu_collection.entries[0].path[0]);
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_COLLECTION);
    }
    cards.page=0;kui_shell_set_vmu_collection(&s,generation,&cards);
    assert(s.vmu_collection.count==3 && !s.vmu_collection.entries[1].path[0] && !s.vmu_collection.entries[2].path[0]);
    assert(s.backups.count==1 && strstr(s.backups.entries[0].path,"ORIGINAL.vms"));
    s.vmu_collection_selected=1;assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    s.vmu_collection_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_CARD);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_IMAGE_LIST && s.page==KUI_SHELL_VMU_IMAGE);
    assert(s.vmu_image_collection && !strcmp(s.vmu_image_path,cards.entries[0].path));
    assert(s.vmu_slot==5 && !s.confirm_vmu_restore);
    struct kui_vmu_view image={.count=1,.total=1,.present=true};
    strcpy(image.entries[0].name,"SELECTED");image.entries[0].bytes=4096;
    kui_shell_set_vmu_image(&s,s.vmu_image_path,0,&image);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_VMU_IMAGE_EXTRACT);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VMU_IMAGE_EXTRACT_ALL);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_CARD);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_COLLECTION);
    assert(s.vmu_collection.count==3 && s.vmu_collection_selected==0);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_VMU_COLLECTION_LIST && s.vmu_collection_page==1);
    assert(!s.vmu_collection.count && s.vmu_collection_generation!=generation);
    kui_shell_set_vmu_collection(&s,generation,&cards);assert(!s.vmu_collection.count);
    cards.page=1;kui_shell_set_vmu_collection(&s,s.vmu_collection_generation,&cards);
    assert(s.vmu_collection.count==2); /* The final page has only two cards. */
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VMU_COLLECTION_LIST && s.vmu_collection_page==1);
    assert(!s.vmu_collection.count);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_ACTIONS && s.vmu_action_selected==4);
    kui_shell_set_vmu_collection(&s,s.vmu_collection_generation,&cards);assert(!s.vmu_collection.count);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_LIST && s.page==KUI_SHELL_VMU);
    assert(s.vmu_slot==5 && s.backups.count==1 && !s.confirm_vmu_restore);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_VMU_BACKUPS_LIST); /* Original restore path stays distinct. */
    puts("PASS managed virtual card browser: no physical source required, exact listing generation/page, invalid paths, read-only image extraction, return context and separate SD restore");
}

static void vmu_collection_rendering(void) {
    struct kui_shell_view view={0};
    reset(KUI_SHELL_VMU_ACTIONS);s.vmu_slot=3;s.vmu_copy_slot=8;s.vmu_action_selected=1;
    s.vmu.present=true;s.vmu.count=s.vmu.total=1;
    strcpy(s.vmu.entries[0].name,"SELECTED");s.vmu.entries[0].bytes=4096;
    render(&view);assert(strstr(drawn,"Virtual VMU (new card)") && strstr(drawn,"Virtual VMUs"));
    assert(!strstr(drawn,"E1"));
    s.confirm_vmu_copy=true;render(&view);
    assert(strstr(drawn,"COPY THIS SAVE") && strstr(drawn,"Virtual VMU") && !strstr(drawn,"E1"));
    s.confirm_vmu_copy=false;s.vmu_action_selected=3;s.confirm_vmu_copy_all=true;
    s.vmu_collection_copy.bulk_total=3;render(&view);
    assert(strstr(drawn,"COPY ALL SAVES") && strstr(drawn,"Virtual VMU") && !strstr(drawn,"E1"));
    s.confirm_vmu_copy_all=false;s.vmu_collection_copy_stored=true;
    strcpy(s.vmu_collection_copy.status.message,"Stored; game activation pending");render(&view);
    assert(strstr(drawn,"Backup card saved; assign games"));
    reset(KUI_SHELL_VMU_COLLECTION);s.vmu_collection.count=KUI_VMU_ROWS;s.vmu_collection.total=13;
    for(unsigned i=0;i<KUI_VMU_ROWS;i++) {
        snprintf(s.vmu_collection.entries[i].name,16,"Card %02u",i);
        snprintf(s.vmu_collection.entries[i].folder,16,"v%04u",i+1);
    }
    s.vmu_collection_selected=7;render(&view);
    assert(strstr(drawn,"Virtual VMUs") && strstr(drawn,"Card 07"));
    assert(!strstr(drawn,"Target VMU") && !strstr(drawn,"WRITE THIS SAVE"));
    /* Card identity and game/save context are visible while selecting a row;
     * a custom name must not conceal which games actually use the card. */
    struct kui_vmu_backup_entry *entry=&s.vmu_collection.entries[7];
    entry->readable=entry->active=entry->custom_label=true;entry->users=2;
    strcpy(entry->label,"My white VMU");strcpy(entry->folder,"p0008");
    strcpy(entry->games,"T-00001, T-00002");
    strcpy(entry->titles,"Sonic Adventure, Crazy Taxi");
    strcpy(entry->contents,"Sonic save, Taxi save");render(&view);
    assert(strstr(drawn,"My white VMU") && strstr(drawn,"Sonic Adventure") && strstr(drawn,"Crazy Taxi"));
    assert(strstr(drawn,"Sonic save") && strstr(drawn,"Taxi save") && strstr(drawn,"p0008"));
    assert(!strstr(drawn,"T-00001") && !strstr(drawn,"T-00002"));
    entry->users=0;entry->titles[0]=0;render(&view);
    assert(strstr(drawn,"Sonic save") && strstr(drawn,"Taxi save"));
    assert(strstr(drawn,"No game assignments"));
    s.vmu_collection.count=0;view.busy=true;render(&view);
    view.busy=false;s.page=KUI_SHELL_VMU_IMAGE;s.vmu_image_collection=true;
    strcpy(s.vmu_image_path,"0:/KUI/vmu/cards/v0001/card.vmd");render(&view);
    assert(strstr(drawn,"B Card") && strstr(drawn,"verified SD backups only"));
    puts("PASS virtual VMU rendering: ninth destination label, selected/all confirmation, assignment guidance, eight virtual card rows and read-only image return context");
}

static void vmu_emulation_controls(void) {
    reset(KUI_SHELL_HOME);s.home_selected=home_index(KUI_SHELL_VMU_TOOLS);
    assert(s.vmu_emulation_enabled && !s.vmu_emulation_known);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_EMULATION_LOAD);
    assert(s.page==KUI_SHELL_VMU_TOOLS && s.vmu_emulation_loading);
    unsigned generation=s.vmu_emulation_generation;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_STOP);
    kui_shell_set_vmu_emulation(&s,generation+1,false,true,false,"Stale");
    assert(s.vmu_emulation_loading && s.vmu_emulation_enabled && !s.vmu_emulation_known);
    kui_shell_set_vmu_emulation(&s,generation,false,true,false,"Preference loaded");
    assert(!s.vmu_emulation_loading && s.vmu_emulation_known && !s.vmu_emulation_enabled);
    for(unsigned i=0;i<4;i++) press(KUI_SHELL_DOWN,false);
    assert(s.vmu_tools_selected==4);
    s.games_launch_reader=KUI_RETAIL_READER_ASYNC;
    s.vmu_wizard.cards=2;s.vmu_wizard.summary[0].pair=12;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_EMULATION_SAVE);
    assert(s.vmu_emulation_requested && !s.vmu_emulation_enabled);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    kui_shell_set_vmu_emulation(&s,s.vmu_emulation_generation,true,false,false,"Save uncertain; reload");
    assert(!s.vmu_emulation_enabled && !s.vmu_emulation_known);
    assert(strstr(s.vmu_emulation_notice,"uncertain"));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_EMULATION_LOAD);
    assert(!s.vmu_emulation_requested); /* Reload never inverses an uncertain value. */
    kui_shell_set_vmu_emulation(&s,s.vmu_emulation_generation,true,true,false,"Reloaded");
    assert(s.vmu_emulation_enabled && s.vmu_emulation_known);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_EMULATION_SAVE && !s.vmu_emulation_requested);
    generation=s.vmu_emulation_generation;
    kui_shell_set_vmu_emulation(&s,generation,false,true,false,NULL);
    assert(!s.vmu_emulation_enabled && s.vmu_emulation_known);
    kui_shell_set_vmu_emulation(&s,generation,true,true,false,"Duplicate");
    assert(!s.vmu_emulation_enabled);
    assert(s.games_launch_reader==KUI_RETAIL_READER_ASYNC && s.vmu_wizard.cards==2 &&
        s.vmu_wizard.summary[0].pair==12);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_VMU_EMULATION_LOAD);
    kui_shell_set_vmu_emulation(&s,s.vmu_emulation_generation,false,false,true,NULL);
    assert(!s.vmu_emulation_known && strstr(s.vmu_emulation_notice,"stopped"));
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.vmu_tools_selected==3);
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && s.vmu_tools_selected==2);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN);
    struct kui_shell_view view={0};
    s.vmu_wizard_loading=false;s.vmu_emulation_known=true;s.vmu_emulation_enabled=false;
    render(&view);assert(strstr(drawn,"Virtual VMU Off") && strstr(drawn,"enable it later in VMU tools"));
    s.vmu_wizard.stage=KUI_VMU_WIZARD_DONE;s.vmu_wizard.status.complete=true;s.vmu_wizard.status.passed=true;
    strcpy(s.vmu_wizard.status.message,"Ready: selected games will use their imported card in virtual A2");
    render(&view);
    assert(strstr(drawn,"virtual VMU remains Off") && strstr(drawn,"Enable virtual VMU in VMU tools"));
    assert(!strstr(drawn,"now choose their imported") && !strstr(drawn,"will use their imported"));
    s.vmu_emulation_known=false;render(&view);
    assert(strstr(drawn,"check virtual VMU status") && !strstr(drawn,"will use their imported"));
    s.vmu_wizard.stage=KUI_VMU_WIZARD_REVIEW;s.vmu_wizard.status.message[0]=0;render(&view);
    assert(strstr(drawn,"Check virtual VMU status") && !strstr(drawn,"will use their imported"));
    s.vmu_wizard.stage=KUI_VMU_WIZARD_DONE;s.vmu_wizard.status.passed=false;
    strcpy(s.vmu_wizard.status.message,"Could not confirm saved choices");render(&view);
    assert(strstr(drawn,"Could not confirm saved choices"));
    s.vmu_emulation_known=true;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_EMULATION_LOAD && s.page==KUI_SHELL_VMU_TOOLS);
    kui_shell_set_vmu_emulation(&s,s.vmu_emulation_generation,false,true,false,NULL);
    s.vmu_tools_selected=4;render(&view);
    assert(strstr(drawn,"Use virtual VMU: Off") && strstr(drawn,"supported native games") &&
        strstr(drawn,"next launch") && strstr(drawn,"X Reload") && strstr(drawn,"independent of reader"));
    s.vmu_emulation_notice[0]=0;render(&view);
    assert(strstr(drawn,"prefetch settings stay unchanged") && strstr(drawn,"cards are kept"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    puts("PASS VMU emulation preference: confirmed On/Off, stale and duplicate results, uncertain save reload, unchanged reader/cards, and wizard Off state");
}
static void vmu_wizard_controls(void) {
    reset(KUI_SHELL_HOME);s.home_selected=2;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_EMULATION_LOAD && s.page==KUI_SHELL_VMU_TOOLS);
    kui_shell_set_vmu_emulation(&s,s.vmu_emulation_generation,true,true,false,NULL);
    press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_LIST && s.page==KUI_SHELL_VMU);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_EMULATION_LOAD && s.page==KUI_SHELL_VMU_TOOLS);
    kui_shell_set_vmu_emulation(&s,s.vmu_emulation_generation,true,true,false,NULL);
    press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN && s.page==KUI_SHELL_VMU_WIZARD);
    assert(s.vmu_wizard_request.op==KUI_VMU_WIZARD_OPEN && s.vmu_wizard_loading);
    assert(!strcmp(s.vmu_wizard_request.root,"/Games"));
    struct kui_vmu_wizard_view w={.generation=s.vmu_wizard_generation,.stage=KUI_VMU_WIZARD_SOURCES};
    ++w.generation;kui_shell_set_vmu_wizard(&s,&w);assert(s.vmu_wizard_loading);
    --w.generation;kui_shell_set_vmu_wizard(&s,&w);assert(!s.vmu_wizard_loading);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_NONE); /* No card to assign yet. */
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.vmu_wizard_slot==7);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_NONE && s.vmu_wizard_slot==0);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN);
    assert(s.vmu_wizard_request.op==KUI_VMU_WIZARD_CAPTURE && s.vmu_wizard_request.slot==0);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE); /* Queued work stays locked. */
    assert(press(KUI_SHELL_A|KUI_SHELL_B,true)==KUI_SHELL_STOP);
    w.generation=s.vmu_wizard_generation;w.cards=2;w.sources=2;w.summary[0].files=12;w.summary[0].blocks=93;
    kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.card==1);
    w.generation=s.vmu_wizard_generation;w.card=1;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.confirm_vmu_wizard_reset);
    assert(press(KUI_SHELL_A|KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_vmu_wizard_reset);
    assert(s.vmu_wizard.cards==2);
    strcpy(s.games_path,"/Games/Keep browser context");s.games_page=4;s.games_selected=3;
    unsigned original_games_generation=s.games_generation;
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_VMU_WIZARD_RUN);
    assert(s.vmu_wizard_request.op==KUI_VMU_WIZARD_LIST_GAMES && s.vmu_wizard_request.card==1);
    w.generation=s.vmu_wizard_generation;w.stage=KUI_VMU_WIZARD_GAMES_STAGE;w.total=17;w.count=8;
    for(unsigned i=0;i<8;i++) {snprintf(w.games[i].title,sizeof(w.games[i].title),"Game %u",i);snprintf(w.games[i].product,sizeof(w.games[i].product),"T-%04u",i);w.games[i].supported=i!=0;}
    kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE); /* Unsupported launch cannot be selected. */
    press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN);
    assert(s.vmu_wizard_request.op==KUI_VMU_WIZARD_TOGGLE && s.vmu_wizard_request.index==1);
    w.generation=s.vmu_wizard_generation;w.selected_games=1;w.games[1].selected=true;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_RIGHT,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.page==1);
    unsigned page_generation=s.vmu_wizard_generation;
    kui_shell_set_vmu_wizard(&s,&w);assert(s.vmu_wizard_loading && s.vmu_wizard.page==0);
    w.generation=page_generation;w.page=1;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.card==0);
    w.generation=s.vmu_wizard_generation;w.page=0;w.card=0;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.op==KUI_VMU_WIZARD_SHOW_REVIEW);
    w.generation=s.vmu_wizard_generation;w.stage=KUI_VMU_WIZARD_REVIEW;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_B|KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.op==KUI_VMU_WIZARD_LIST_GAMES);
    w.generation=s.vmu_wizard_generation;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.op==KUI_VMU_WIZARD_COMMIT);
    w.generation=s.vmu_wizard_generation;w.stage=KUI_VMU_WIZARD_DONE;w.summary[0].pair=12;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_EMULATION_LOAD && s.page==KUI_SHELL_VMU_TOOLS);
    w.cards=8;kui_shell_set_vmu_wizard(&s,&w);assert(s.vmu_wizard.cards==2); /* Closed page publication ignored. */
    assert(!strcmp(s.games_path,"/Games/Keep browser context") && s.games_page==4 && s.games_selected==3 && s.games_generation==original_games_generation);
    s.page=KUI_SHELL_VMU_WIZARD;s.vmu_wizard_loading=false;
    press(KUI_SHELL_X,false);assert(s.confirm_vmu_wizard_reset);
    assert(press(KUI_SHELL_A|KUI_SHELL_X,false)==KUI_SHELL_NONE); /* Reset requires its own confirmation edge. */
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.op==KUI_VMU_WIZARD_RESET);
    puts("PASS VMU wizard: tools/explorer routes, read-only source capture request, multiple card selection, product toggles, pagination, guarded confirmation, Stop lock and stale publication rejection");
}
static void vmu_wizard_publication_lock(void) {
    reset(KUI_SHELL_VMU_WIZARD);
    struct kui_vmu_wizard_view w={.stage=KUI_VMU_WIZARD_GAMES_STAGE,.cards=1,.total=1,.count=1,
        .selected_games=1,.publication_started=true};
    strcpy(w.games[0].product,"T-12345");w.games[0].supported=true;w.games[0].selected=true;
    kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE); /* Retry must keep the exact published batch. */
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.op==KUI_VMU_WIZARD_SHOW_REVIEW);
    w.generation=s.vmu_wizard_generation;w.stage=KUI_VMU_WIZARD_REVIEW;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.op==KUI_VMU_WIZARD_COMMIT);
    w.generation=s.vmu_wizard_generation;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.op==KUI_VMU_WIZARD_LIST_GAMES);
    w.generation=s.vmu_wizard_generation;w.stage=KUI_VMU_WIZARD_GAMES_STAGE;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.op==KUI_VMU_WIZARD_SHOW_SOURCES);
    w.generation=s.vmu_wizard_generation;w.stage=KUI_VMU_WIZARD_SOURCES;kui_shell_set_vmu_wizard(&s,&w);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.confirm_vmu_wizard_reset);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.confirm_vmu_wizard_reset && s.vmu_wizard.publication_started);
    puts("PASS VMU wizard publication lock: frozen choices and sources after publication starts, read-only review and exact retry remain available");
}
static void vmu_wizard_library_scrolling(void) {
    reset(KUI_SHELL_VMU_WIZARD);
    struct kui_vmu_wizard_view w={.stage=KUI_VMU_WIZARD_GAMES_STAGE,.cards=1,.total=40,.count=8};
    for(unsigned i=0;i<8;i++) {strcpy(w.games[i].title,"Game");strcpy(w.games[i].product,"T-12345");w.games[i].supported=true;}
    kui_shell_set_vmu_wizard(&s,&w);
    for(unsigned i=1;i<40;i++) {
        enum kui_shell_action action=press(KUI_SHELL_DOWN,false);
        if(action==KUI_SHELL_VMU_WIZARD_RUN) {
            w.page=s.vmu_wizard_request.page;w.generation=s.vmu_wizard_generation;kui_shell_set_vmu_wizard(&s,&w);
        } else assert(action==KUI_SHELL_NONE);
        assert(s.vmu_wizard.page*8u+s.vmu_wizard_selected==i);
    }
    assert(press(KUI_SHELL_DOWN,false)==KUI_SHELL_NONE && s.vmu_wizard_selected==7 && s.vmu_wizard.page==4);
    for(unsigned i=39;i;i--) {
        enum kui_shell_action action=press(KUI_SHELL_UP,false);
        if(action==KUI_SHELL_VMU_WIZARD_RUN) {
            w.page=s.vmu_wizard_request.page;w.generation=s.vmu_wizard_generation;kui_shell_set_vmu_wizard(&s,&w);
        } else assert(action==KUI_SHELL_NONE);
        assert(s.vmu_wizard.page*8u+s.vmu_wizard_selected==i-1u);
    }
    assert(press(KUI_SHELL_UP,false)==KUI_SHELL_NONE && !s.vmu_wizard_selected && !s.vmu_wizard.page);
    puts("PASS VMU wizard library scrolling: all 40 game rows in both directions and stable endpoints");
}
static void vmu_wizard_rendering(void) {
    struct kui_shell_view view={0};reset(KUI_SHELL_VMU_TOOLS);render(&view);
    assert(strstr(drawn,"VMU tools") && strstr(drawn,"Physical VMUs") && strstr(drawn,"Import saves"));
    s.page=KUI_SHELL_VMU_WIZARD;s.vmu_wizard_slot=7;s.vmu_emulation_known=true;
    struct kui_vmu_wizard_view *w=&s.vmu_wizard;w->sources=4;w->cards=2;
    w->summary[0]=(struct kui_vmu_wizard_card){.files=20,.blocks=194,.games=3};
    w->summary[1]=(struct kui_vmu_wizard_card){.files=12,.blocks=84,.games=2};
    render(&view);assert(strstr(drawn,"Source VMU D2") && strstr(drawn,"194 / 200 blocks") && strstr(drawn,"2 virtual cards"));
    s.confirm_vmu_wizard_reset=true;render(&view);assert(strstr(drawn,"Start a new import?") && strstr(drawn,"A Start over"));s.confirm_vmu_wizard_reset=false;
    w->stage=KUI_VMU_WIZARD_GAMES_STAGE;w->total=40;w->count=8;w->selected_games=5;s.vmu_wizard_selected=7;
    for(unsigned i=0;i<8;i++) {snprintf(w->games[i].title,sizeof(w->games[i].title),"Game %u with a deliberately long title that must be clipped safely",i);snprintf(w->games[i].product,sizeof(w->games[i].product),"T-%04u",i);w->games[i].supported=i!=7;}
    w->games[0].selected=true;render(&view);
    assert(strstr(drawn,"[x]") && strstr(drawn,"Game 7") && strstr(drawn,"No VMU") && strstr(drawn,"T-0007"));
    w->stage=KUI_VMU_WIZARD_REVIEW;render(&view);
    assert(strstr(drawn,"Review VMU import") && strstr(drawn,"5 assigned games") && strstr(drawn,"A Create and assign cards"));
    w->stage=KUI_VMU_WIZARD_DONE;w->summary[0].pair=12;render(&view);
    assert(strstr(drawn,"VMU import complete") && strstr(drawn,"3 assigned games") && !strstr(drawn,"p0012") && strstr(drawn,"compatibility checked at game launch"));
    w->publication_started=true;w->stage=KUI_VMU_WIZARD_REVIEW;render(&view);
    assert(strstr(drawn,"A Retry saving these choices") && strstr(drawn,"same cards and game choices"));
    w->stage=KUI_VMU_WIZARD_GAMES_STAGE;render(&view);assert(strstr(drawn,"Choices locked") && !strstr(drawn,"A Select / remove"));
    w->stage=KUI_VMU_WIZARD_SOURCES;render(&view);assert(strstr(drawn,"source capture is locked"));
    puts("PASS VMU wizard rendering: source slots, card capacity, eight game rows, unavailable products, review, reset confirmation and published card IDs");
}

static void vmu_menu_cleanup_controls(void) {
    /* Home grouping leaves the persisted startup preference enum intact. */
    assert(kui_shell_startup_page(KUI_STARTUP_HOME)==KUI_SHELL_HOME);
    assert(kui_shell_startup_page(KUI_STARTUP_RIPPER)==KUI_SHELL_RIPPER);
    assert(kui_shell_startup_page(KUI_STARTUP_VMU)==KUI_SHELL_VMU_TOOLS);
    assert(kui_shell_startup_page(KUI_STARTUP_MUSIC)==KUI_SHELL_MUSIC);
    assert(kui_shell_startup_page(KUI_STARTUP_DIAGNOSTICS)==KUI_SHELL_DIAGNOSTICS);
    assert(kui_shell_startup_page(UINT_MAX)==KUI_SHELL_HOME);
    reset(KUI_SHELL_HOME);s.home_selected=home_index(KUI_SHELL_DISC_TOOLS);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DISC_TOOLS);
    press(KUI_SHELL_DOWN,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_GD_PLAY);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DISC_TOOLS);
    press(KUI_SHELL_UP,false);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_RIPPER);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DISC_TOOLS);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_HOME);
    reset(KUI_SHELL_DIAGNOSTICS);
    assert(press(KUI_SHELL_L,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_MEMORY);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_DIAGNOSTICS);

    reset(KUI_SHELL_VMU_TOOLS);s.vmu_emulation_known=true;s.vmu_emulation_enabled=false;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COLLECTION_LIST);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_TOOLS);
    s.vmu_tools_selected=3;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_BACKUPS_LIST);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_TOOLS);
    s.vmu_tools_selected=1;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_LIST);
    assert(press(KUI_SHELL_R,false)==KUI_SHELL_VMU_BACKUPS_LIST);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_VMU_EMULATION_LOAD && s.page==KUI_SHELL_VMU_TOOLS);
    kui_shell_set_vmu_emulation(&s,s.vmu_emulation_generation,false,true,false,NULL);
    s.vmu_tools_selected=0;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_COLLECTION_LIST);
    unsigned generation=s.vmu_collection_generation;
    struct kui_vmu_backup_view cards={.page=0,.count=2,.total=2};
    cards.status.complete=cards.status.passed=true;
    struct kui_vmu_backup_entry *active=&cards.entries[0],*snapshot=&cards.entries[1];
    strcpy(active->path,"0:/KUI/vmu-test/p0005/card.vmd");strcpy(active->folder,"p0005");
    strcpy(active->label,"Code Veronica saves");strcpy(active->games,"T-12301 T-12302");
    active->active=active->readable=true;active->users=2;active->free_blocks=84;active->identity[0]=7;
    strcpy(snapshot->path,"0:/KUI/vmu/cards/v0001/card.vmd");strcpy(snapshot->label,"Imported backup");
    snapshot->readable=true;snapshot->free_blocks=100;snapshot->identity[0]=8;
    kui_shell_set_vmu_collection(&s,generation,&cards);
    assert(press(KUI_SHELL_X,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_CARD);
    assert(!strcmp(s.vmu_card.path,active->path));
    struct kui_shell_view view={0};render(&view);
    assert(strstr(drawn,"Code Veronica saves") && strstr(drawn,"84 free blocks") && strstr(drawn,"Used by 2 games"));
    assert(!strstr(drawn,"p0005") && !strstr(drawn,active->path));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_IMAGE_LIST && s.page==KUI_SHELL_VMU_IMAGE);
    assert(!strcmp(s.vmu_image_path,active->path) && s.vmu_image_collection);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_CARD);
    s.vmu_card_selected=KUI_SHELL_VMU_CARD_RENAME;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_KEYBOARD && s.vmu_name_keyboard);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_CARD && !s.vmu_name_keyboard);
    assert(s.vmu_collection_generation==generation);
    press(KUI_SHELL_A,false);strcpy(s.keyboard," Shared saves ");
    assert(press(KUI_SHELL_START,false)==KUI_SHELL_VMU_RENAME && s.page==KUI_SHELL_VMU_COLLECTION);
    assert(!strcmp(s.vmu_name_path,active->path) && !strcmp(s.vmu_name_label,"Shared saves"));
    assert(!memcmp(s.vmu_name_identity,active->identity,16));
    assert(s.vmu_collection_generation!=generation && !s.vmu_collection.count && !s.vmu_emulation_enabled);
    kui_shell_set_vmu_collection(&s,generation,&cards);assert(!s.vmu_collection.count);
    strcpy(active->label,"Shared saves");
    kui_shell_set_vmu_collection(&s,s.vmu_collection_generation,&cards);
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_CARD);
    s.vmu_card_selected=KUI_SHELL_VMU_CARD_ASSIGN;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN && s.page==KUI_SHELL_VMU_WIZARD);
    assert(s.vmu_wizard_request.op==KUI_VMU_WIZARD_USE_CARD && !strcmp(s.vmu_wizard_request.card_path,active->path));
    struct kui_vmu_wizard_view wizard={.generation=s.vmu_wizard_generation,.stage=KUI_VMU_WIZARD_GAMES_STAGE,
        .cards=1,.existing_card=true,.copy_on_commit=true};
    kui_shell_set_vmu_wizard(&s,&wizard);render(&view);
    assert(strstr(drawn,"Creates a new shared card") && strstr(drawn,"original is kept"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_CARD);
    s.vmu_card_selected=KUI_SHELL_VMU_CARD_USERS;
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_VMU_WIZARD_RUN && s.vmu_wizard_request.view_only);
    wizard=(struct kui_vmu_wizard_view){.generation=s.vmu_wizard_generation,.stage=KUI_VMU_WIZARD_GAMES_STAGE,
        .cards=1,.view_only=true,.total=1,.count=1};
    strcpy(wizard.games[0].title,"Resident Evil Code Veronica");
    strcpy(wizard.games[0].product,"T-12301");wizard.games[0].supported=true;
    kui_shell_set_vmu_wizard(&s,&wizard);render(&view);
    assert(strstr(drawn,"Games using this card") && strstr(drawn,"Resident Evil Code Veronica"));
    assert(!strstr(drawn,"T-12301") && !strstr(drawn,"A Select") && !strstr(drawn,"START Review"));
    assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && press(KUI_SHELL_START,false)==KUI_SHELL_NONE);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_CARD);
    s.vmu_card_selected=KUI_SHELL_VMU_CARD_DETAILS;press(KUI_SHELL_A,false);render(&view);
    assert(s.vmu_card_details && strstr(drawn,"p0005") && strstr(drawn,"Card identity"));
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && !s.vmu_card_details);
    assert(press(KUI_SHELL_B,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_COLLECTION);
    s.vmu_collection.entries[0].readable=false;
    strcpy(s.vmu_collection.entries[0].games,"Journal belongs to another card");
    press(KUI_SHELL_A,false);render(&view);assert(strstr(drawn,"This card is unavailable"));
    for(unsigned i=0;i<KUI_SHELL_VMU_CARD_DETAILS;i++) {
        s.vmu_card_selected=i;
        assert(press(KUI_SHELL_A,false)==KUI_SHELL_NONE && s.page==KUI_SHELL_VMU_CARD);
    }
    s.vmu_card_selected=KUI_SHELL_VMU_CARD_DETAILS;press(KUI_SHELL_A,false);assert(s.vmu_card_details);
    puts("PASS VMU menu cleanup: direct destinations, Home groups, startup ABI, card identity/name requests, details, assignment route, Off browsing and unreadable-card guards");
}
static void game_vmu_card_info_controls(void) {
    struct kui_games_detail detail=launch_detail(false,false);strcpy(detail.product," t-12345 ");
    install_launch_detail(&detail);
    assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VMU_GAME_INFO && s.page==KUI_SHELL_GAMES_LAUNCH_OPTIONS);
    assert(s.games_vmu_loading && !strcmp(s.games_vmu_product,"T-12345"));
    unsigned generation=s.games_vmu_generation;
    struct kui_shell_view view={0};render(&view);assert(strstr(drawn,"Saving: Checking card"));
    kui_shell_set_game_vmu(&s,generation+1,"T-12345","Stale","wrong generation");
    kui_shell_set_game_vmu(&s,generation,"T-99999","Stale","wrong product");
    assert(s.games_vmu_loading && !s.games_vmu_label[0]);
    kui_shell_set_game_vmu(&s,generation,"T-12345","Shared saves","Compatibility checked at launch");
    assert(!s.games_vmu_loading && !strcmp(s.games_vmu_label,"Shared saves"));
    render(&view);assert(strstr(drawn,"Saving: Shared saves"));
    kui_shell_set_game_vmu(&s,generation,"T-12345","Duplicate","ignored");
    assert(!strcmp(s.games_vmu_label,"Shared saves"));
    s.vmu_emulation_known=true;s.vmu_emulation_enabled=false;render(&view);
    assert(strstr(drawn,"Saving: Physical VMU") && !strstr(drawn,"Saving: Shared saves"));
    install_launch_detail(&detail);assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VMU_GAME_INFO);
    assert(s.games_vmu_loading && press(KUI_SHELL_START,false)==KUI_SHELL_GAMES_RETAIL);
    install_launch_detail(&detail);assert(press(KUI_SHELL_Y,false)==KUI_SHELL_VMU_GAME_INFO);
    generation=s.games_vmu_generation;strcpy(s.games_detail.product,"OTHER");
    kui_shell_set_game_vmu(&s,generation,"T-12345","Old selection","ignored");
    assert(s.games_vmu_loading && !s.games_vmu_label[0]);
    puts("PASS game saving-card information: canonical product, stale/duplicate result rejection, Off display and launch without waiting for optional lookup");
}

int main(int argc,char **argv) {
    if(argc==2 && !strcmp(argv[1],"--salvage-drive")) {
        round_five_controls();salvage_drive_controls();round_five_rendering();
        puts("PASS salvage drive settings: default, navigation, wrap, busy guard, boot reset and rendering");return 0;
    }
    vmu_menu_cleanup_controls();game_vmu_card_info_controls();
    vmu_emulation_controls();
    vmu_wizard_controls();vmu_wizard_library_scrolling();vmu_wizard_publication_lock();vmu_wizard_rendering();
    if(argc==2 && !strcmp(argv[1],"--vmu-wizard")) return 0;
    if(argc==2 && !strcmp(argv[1],"--virtual-vmu")) {
        vmu_continuous_scrolling();vmu_catalog_scrolling();vmu_scroll_rendering();
        vmu_collection_copy_controls();vmu_collection_controls();vmu_collection_rendering();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--wifi")) {
        wifi_controls();wifi_rendering();wifi_update_controls();wifi_update_rendering();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--storage-tests")) {
        storage_test_controls();storage_test_rendering();return 0;
    }
    if(argc==2 && !strcmp(argv[1],"--ripper")) {
        ripper_saved_job_controls();ripper_folder_label_controls();ripper_label_keyboard_purpose_controls();
        destination_transaction();keyboard_transaction();advanced_navigation();restore_and_scan_controls();
        ripper_saved_job_rendering();reference_and_destination_rendering();music_and_boot_rendering();return 0;
    }
    vmu_continuous_scrolling();vmu_catalog_scrolling();vmu_scroll_rendering();small_tools_controls();small_tools_rendering();
    vmu_copy_all_controls();vmu_import_controls();vmu_bulk_import_rendering();
    vmu_collection_copy_controls();vmu_collection_controls();vmu_collection_rendering();
    games_remembered_selection(); games_controls(); games_background_titles(); games_views(); games_async_publication(); games_retail_controls(); games_armada_ce_music_controls(); games_bam4_ce_music_controls(); games_cdda_compatibility_status(); games_launch_options_controls(); games_cd_controls(); games_saved_launch_controls(); games_patch_controls(); games_image_defaults_controls(); games_zso_controls(); games_codec_controls(); games_check_baseline_controls(); games_variant_controls();
    if(argc==2 && !strcmp(argv[1],"--games-controls")) { puts("PASS Games navigation and launch controls"); return 0; }
    games_rendering(); games_native_reader_choices(); games_new_reader_defaults(); games_patch_rendering(); games_zso_rendering(); games_selected_art_rendering(); games_variant_rendering();
    if(argc==2 && !strcmp(argv[1],"--games")) { puts("PASS Games navigation, launch eligibility and rendering"); return 0; }
    storage_test_controls();storage_test_rendering();
    launcher_and_confirmation(); operation_lock_and_stop(); settings_transaction(); ripper_export_rendering();
    system_transaction_and_video(); app_navigation_and_vmu(); clock_and_defaults(); restore_and_scan_controls(); phase_eta();
    diagnostics(); destination_transaction(); keyboard_transaction(); advanced_navigation();ripper_parity_controls();
    ripper_saved_job_controls();ripper_folder_label_controls();ripper_label_keyboard_purpose_controls();
    ripper_saved_job_rendering();
    rendering_semantics(); reference_and_destination_rendering(); new_pages_rendering();
    music_and_boot_controls(); music_and_boot_rendering(); round_four_rendering(); round_five_controls(); salvage_drive_controls(); round_five_rendering();
    files_controls(); files_rendering();
    ftp_controls(); ftp_rendering();
    wifi_controls(); wifi_rendering(); wifi_update_controls(); wifi_update_rendering();
    puts("PASS shell: Games browsing/inspection, stale result guards, Stop lock, system/ripper preferences, reversible video actions, VMU paging, phase ETA, destination keyboard, reference grades, safe rendering");
    return 0;
}
