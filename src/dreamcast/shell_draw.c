/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell.h"
#include "kui/version.h"
#include "kui/shell_font.h"
#include "kui/retail_image.h"
#include "kui/storage.h"
#include "shell_art.inc"
#include <stdio.h>
#include <string.h>

/* Original K-UI palette and split-pane launcher, drawn independently of the
 * legacy UI framework. Artwork and font masks are embedded: no device I/O,
 * decoding or allocation while the capture worker is active. */
enum { NAVY=0x0864, PANEL=0x10c6, SELECTED=0x494b, EDGE=0x318a,
    WHITE=0xf7bf, MUTED=0xadd9, CYAN=0x675e, PINK=0xf3fb,
    GREEN=0x8ef6, AMBER=0xfda9 };
struct paint { uint16_t *fb; kui_shell_text_fn text; void *ctx; bool crisp; };
static void path_words(struct paint *,unsigned,unsigned,unsigned,uint16_t,const char *);
static unsigned word_width(const struct paint *p,const char *text,bool large) {
    return p->crisp?kui_shell_font_width_crisp(text,large):kui_shell_font_width(text,large);
}
static void box(struct paint *p, unsigned x, unsigned y,
        unsigned w, unsigned h, uint16_t color) {
    if(x >= 640 || y >= 480) return;
    if(w > 640-x) w = 640-x;
    if(h > 480-y) h = 480-y;
    for(unsigned row=y; row<y+h; ++row)
        for(unsigned col=x; col<x+w; ++col) p->fb[row*640+col] = color;
}
static void words(struct paint *p, unsigned x, unsigned y,
        unsigned right, uint16_t color, const char *value, bool large) {
    if(!value || x<32 || x>=608 || y<24 || y>(large?424u:432u)) return;
    if(right>608) right=608;
    if(right<=x) return;
    char clipped[160];
    size_t n=0,read=0;
    while(n+1<sizeof(clipped) && value[read] && value[read]!='\n' && value[read]!='\r') {
        unsigned char c=(unsigned char)value[read++];
        clipped[n++]=(c>=32 && c<=126)?(char)c:'?';
        /* The embedded font is ASCII. A UTF-8 folder remains intact in the
         * model; show one replacement glyph, not a misleading blank per byte. */
        if(c>=0xc2 && c<=0xf4) {
            unsigned extra=c<0xe0?1:c<0xf0?2:3;
            while(extra && ((unsigned char)value[read]&0xc0u)==0x80u) {
                ++read; --extra;
            }
        }
    }
    clipped[n]='\0';
    bool truncated=value[read] && value[read]!='\n' && value[read]!='\r';
    while(n && word_width(p,clipped,large)>right-x) {
        clipped[--n]='\0'; truncated=true;
    }
    if(truncated) {
        while(n && (n+4>sizeof(clipped) ||
                word_width(p,clipped,large)+word_width(p,"...",large)>right-x))
            clipped[--n]='\0';
        if(n+4<=sizeof(clipped) && word_width(p,"...",large)<=right-x)
            memcpy(clipped+n,"...",4);
    }
    if(p->crisp) kui_shell_font_draw_crisp(p->fb,(int)x,(int)y,color,clipped,large);
    else kui_shell_font_draw(p->fb,(int)x,(int)y,color,clipped,large);
    if(p->text) p->text(p->ctx,x,y,color,clipped,large);
}
static void label(struct paint *p, unsigned x, unsigned y,
        uint16_t color, const char *value) {
    words(p,x,y,608,color,value,false);
}
static void title(struct paint *p, unsigned x, unsigned y, const char *value) {
    words(p,x,y,608,WHITE,value,true);
}
static void rule(struct paint *p, unsigned y) {
    box(p,32,y,576,2,EDGE);
}
static void panel(struct paint *p, unsigned x, unsigned y,
        unsigned w, unsigned h, uint16_t color) {
    /* Six-pixel corners remain legible on the console's interlaced output. */
    box(p,x+4,y,w-8,1,color); box(p,x+2,y+1,w-4,2,color);
    box(p,x+1,y+3,w-2,2,color); box(p,x,y+5,w,h-10,color);
    box(p,x+1,y+h-5,w-2,2,color); box(p,x+2,y+h-3,w-4,2,color);
    box(p,x+4,y+h-1,w-8,1,color);
}
static void art(struct paint *p, unsigned x, unsigned y,
        unsigned w, unsigned h, const uint16_t *pixels) {
    if(x>=640 || y>=480) return;
    unsigned rows=h<480-y?h:480-y, cols=w<640-x?w:640-x;
    for(unsigned row=0;row<rows;row++) for(unsigned col=0;col<cols;col++) {
        uint16_t color=pixels[row*w+col];
        if(color!=KUI_ART_TRANSPARENT) p->fb[(y+row)*640+x+col]=color;
    }
}
static const char *state(const struct kui_shell_view *v) {
    if(v->saving) return v->cancel_requested ? "CANCELLING SAVE" : "SAVING REPORT";
    if(v->busy) return v->cancel_requested ? "STOP REQUESTED" : "WORKING";
    return "READY";
}
static void heading(struct paint *p, const struct kui_shell *s,const struct kui_shell_view *v) {
    art(p,32,20,128,64,kui_art_brand);
    words(p,176,26,396,WHITE,KUI_RELEASE_SHORT,true);
    const char *credit=v->busy || v->saving?state(v):"by TPMJB / READY";
    words(p,176,52,396,MUTED,credit,false);
    /* Keep the project link beneath the brand, with normal letter spacing.
     * It has the whole left header width rather than a cramped middle column. */
    words(p,32,78,396,CYAN,"github.com/TPMJB/K-UI-NeXT",false);
    char music[48];
    const char *prefix=s->page==KUI_SHELL_HOME&&!v->busy?"Y Music":"Music";
    if(v->music_change_pending) snprintf(music,sizeof(music),"Music change queued");
    else if(!v->music_enabled) snprintf(music,sizeof(music),"%s off",prefix);
    else if(v->music_paused) snprintf(music,sizeof(music),"%s paused",prefix);
    else snprintf(music,sizeof(music),"%s %u%%",prefix,v->music_volume);
    words(p,412,26,608,v->music_playing?CYAN:MUTED,music,false);
    words(p,412,48,608,v->music_enabled?WHITE:MUTED,
        v->music_title&&v->music_title[0]?v->music_title:"No song selected",false);
    char build[40]; snprintf(build,sizeof(build),"Build %.12s",v->build?v->build:"local");
    label(p,416,74,MUTED,build);
    rule(p,98);
}
static void footer(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    rule(p,416);
    bool song_page=s->page==KUI_SHELL_HOME || s->page==KUI_SHELL_RIPPER;
    const char *controls=v->video_trial ? "A Keep mode   B Revert" :
        v->busy ? (s->page==KUI_SHELL_SALVAGE && !s->salvage_trial_result?
            (s->salvage_details?"Y Overview   B Stop safely":"Y Details   B Stop safely"):
            s->page==KUI_SHELL_WIFI && s->wifi.firmware_committing?"Finishing update; keep the console powered on":
            song_page?"B Stop safely   L/R Songs":s->page==KUI_SHELL_FTP?"B Stop the server":"B Stop safely") :
        s->confirm_gd_boot ? "A Exit to BIOS   B Cancel" :
        s->confirm_quick_resume ? "A Quick resume   B Cancel" :
        s->confirm_new ? "A Start capture   B Cancel" :
        s->confirm_clock ? "A Set clock   B Cancel" :
        s->confirm_defaults ? "A Use defaults   B Cancel" :
        s->confirm_vmu_wizard_reset ? "A Start over   B Keep this session" :
        s->confirm_vmu_restore ? "A Restore save   B Cancel" :
        s->confirm_vmu_delete ? "A Delete after backup   B Cancel" :
        s->confirm_vmu_copy ? "A Copy save   B Cancel" :
        s->confirm_vmu_copy_all ? "A Copy all saves   B Cancel" :
        s->confirm_music_clear ? "A Clear music cache   B Cancel" :
        s->confirm_restart ? "A Restart console   B Cancel" :
        s->confirm_repair_in_place ? "A Repair in place   B Cancel" :
        s->confirm_salvage_check ? "A Start full check   B Cancel" :
        s->confirm_salvage ? (s->confirm_salvage_defer?"A Resume past range   B Cancel":"A Start recovery   B Cancel") :
        s->confirm_storage_test ? "A Start test   B Cancel" :
        s->confirm_wifi_forget ? "A Forget the network   B Cancel" :
        s->confirm_wifi_update ? "A Update firmware   B Cancel" :
        s->page==KUI_SHELL_HOME ? "D-pad Select   A Open   Y Volume   L/R Songs" :
        s->page==KUI_SHELL_DISC_TOOLS ? "D-pad Select   A Open   B Home" :
        s->page==KUI_SHELL_SETTINGS ? "A Save / Open   B Back / discard" :
        s->page==KUI_SHELL_RIPPER_SETTINGS ? "A Save   B Back / discard" :
        s->page==KUI_SHELL_CLOCK ? "A Set clock   X Reload   B Settings" :
        s->page==KUI_SHELL_CRC_SCAN && s->scan_return==KUI_SHELL_DISC_TOOLS ? "A Verify saved dump   B Disc tools" :
        s->page==KUI_SHELL_CRC_SCAN ? (s->recovery_operation==4u?"A Start new copy   B Advanced":s->recovery_operation==1u || s->recovery_operation==3u?
            "A Repair / Resume   B Advanced":s->recovery_operation==2u?
            "A Reread again   B Advanced":"A Scan again   B Advanced") :
        s->page==KUI_SHELL_VMU_TOOLS ? "D-pad Select   A Open / Toggle   X Reload   B Home" :
        s->page==KUI_SHELL_GAMES_VMU_CARD ? (s->games_vmu_info.candidates_sequence?
            "A Use card   X Set default   Y Always ask   B Back":"A Use card   B Back") :
        s->page==KUI_SHELL_VMU_WIZARD ? (s->vmu_wizard.view_only?
            "UP/DOWN Scroll   LEFT/RIGHT Page   B Card":s->vmu_wizard.missing_card_confirmation?
            "A Use imported saves   B Cancel":s->vmu_wizard.stage==KUI_VMU_WIZARD_GROUPS?
            "A Choose game   X Backup only   START Review   B Sources":s->vmu_wizard.stage==KUI_VMU_WIZARD_GROUP_GAME?
            "A Assign game   X Backup only   LEFT/RIGHT Page   B Questions":s->vmu_wizard.stage==KUI_VMU_WIZARD_SAVES?
            "A Pick   L/R Source   LEFT/RIGHT Page   START Keep   B Games":s->vmu_wizard.stage==KUI_VMU_WIZARD_GAMES_STAGE?
            (s->vmu_wizard.publication_started?"L/R Card   LEFT/RIGHT Page   START Retry   B Saves":
             s->vmu_wizard.existing_card?"A Select   L/R Card   LEFT/RIGHT Page   START Review   B Saves":
             "A Saves   X Blank/remove   LEFT/RIGHT Page   START Review   B Sources"):
            s->vmu_wizard.stage==KUI_VMU_WIZARD_REVIEW?(s->vmu_wizard.publication_started?
            "A Retry saving these choices   B View games":s->vmu_wizard.automatic?
             (s->vmu_wizard.unresolved_groups?"A Answer questions   X Change matches   B Questions":
              "A Import saves   LEFT/RIGHT Page   X Change matches   B Back"):
             "A Create and assign cards   LEFT/RIGHT Page   B Games"):
            s->vmu_wizard.stage==KUI_VMU_WIZARD_DONE?"A VMU tools   X Start over":
            s->vmu_wizard.publication_started?"Y Card   START View games   X Start over   B Tools":
            "A Import   L/R Slot   Y Card   START Match saves   X Reset   B Tools") :
        s->page==KUI_SHELL_VMU_ACTIONS ? "A Open   B VMU   LEFT/RIGHT Copy target" :
        s->page==KUI_SHELL_VMU_IMAGE ? (s->vmu_image_collection?"UP/DOWN Scroll   B Card   START Page":"UP/DOWN Scroll   B SD backups   START Page") :
        s->page==KUI_SHELL_VMU_COLLECTION ? "A Open   X Actions   Y Refresh   B Back   START Page" :
        s->page==KUI_SHELL_VMU_CARD ? (s->vmu_card_details?"B Card":"D-pad Select   A Open   Y Details   B Virtual VMUs") :
        s->page==KUI_SHELL_VMU_IMPORT ? (s->confirm_vmu_import?"A Stage SD backup   B SD backups":
            s->vmu_import_staged?"B SD backups":"A Check again   B SD backups") :
        s->page==KUI_SHELL_SALVAGE ? (s->salvage_trial_result?
            (s->salvage_result_return==KUI_SHELL_SALVAGE?"A Recovery   B Recovery":"A Diagnostics   B Diagnostics"):
            "D-pad Select   A Start / Open   B Disc tools") :
        s->page==KUI_SHELL_SALVAGE_OPTIONS ? "UP/DOWN Select   A Change / Open   B Recovery" :
        s->page==KUI_SHELL_SALVAGE_DRIVE ? "D-pad Select   A Run / Open   B Recovery" :
        s->page==KUI_SHELL_SYSTEM_TOOLS ? "D-pad Select   A Open   B Settings" :
        s->page==KUI_SHELL_VMU_RESTORE ? "UP/DOWN Scroll   B Back   LEFT/RIGHT Target   START Page" :
        s->page==KUI_SHELL_DESTINATION ? "B Parent   START Cancel   LEFT/RIGHT Page" :
        s->page==KUI_SHELL_KEYBOARD ? "A Key   X Backspace   Y Shift   B Cancel" :
        s->page==KUI_SHELL_ADVANCED ? "D-pad Select   A Open   B Ripper" :
        s->page==KUI_SHELL_RIPPER ? (s->capture_job[0]?"LEFT Automatic job   START Advanced   L/R Songs   B Disc tools":
            "B Disc tools   START Advanced   L/R Songs") :
        s->page==KUI_SHELL_VMU ? "A Open   X Actions   LEFT/RIGHT VMU   R Backups   B Tools" :
        s->page==KUI_SHELL_GAMES ? (kui_shell_games_view(s)==KUI_GAMES_VIEW_LIST?
            "A Open   X Scan   Y View   B Back   START More":"A Open   X Scan   Y View   B Back   START More") :
        s->page==KUI_SHELL_GAMES_VARIANTS ? "D-pad Select   A Inspect   B Games" :
        s->page==KUI_SHELL_GAMES_DETAIL ? (kui_shell_games_retail_ready(s)?
            (s->games_from_files?"A Launch   Y Options   B Files":kui_shell_games_variant_label(s)?
                "A Launch   Y Options   B Versions":"A Launch   Y Options   B Games"):
            kui_shell_games_ce_probe_ready(s)?
            (s->games_from_files?"A Launch   Y Options   B Files":kui_shell_games_variant_label(s)?
                "A Launch   Y Options   B Versions":"A Launch   Y Options   B Games"):
            kui_shell_games_image_ready(s)?
            (s->games_from_files?"Y Options   X Inspect   B Files":kui_shell_games_variant_label(s)?
                "Y Options   X Inspect   B Versions":"Y Options   X Inspect   B Games"):
            kui_shell_games_zso_selected(s)?"Y ZSO codec   X Inspect again   B Back":
            s->games_from_files?"X Inspect again   B Files":kui_shell_games_variant_label(s)?
                "X Inspect again   B Versions":"X Inspect again   B Games") :
        s->page==KUI_SHELL_FILES ? "B Up   START Home   LEFT/RIGHT Page" :
        s->page==KUI_SHELL_FILES_ACTIONS ? "D-pad Select   A Choose   B Files" :
        s->page==KUI_SHELL_FILES_PICK ? "B Up   START Cancel   LEFT/RIGHT Page" :
        s->page==KUI_SHELL_FILES_CONFIRM ? (kui_shell_files_ready(s)?(s->files_job.op==KUI_FILES_OP_COPY?
            "A Copy   B Cancel":s->files_job.op==KUI_FILES_OP_MOVE?"A Move   B Cancel":"A Delete   B Cancel"):"B Back") :
        s->page==KUI_SHELL_FILES_INFO || s->page==KUI_SHELL_FILES_VIEW ? "B Files" :
        s->page==KUI_SHELL_FTP ? "A Start again   B Network" :
        s->page==KUI_SHELL_STORAGE_TESTS ? (s->storage_test_show_result?
            (s->storage_test_result.saved && s->storage_test_result.outcome==KUI_STORAGE_TEST_PASSED?
                (s->storage_test_details?"X Summary   Y Baseline   B Back":"X Details   Y Baseline   B Back"):
                (s->storage_test_details?"X Summary   B Back":"X Details   B Back")):
            "D-pad Select/change   A Open   B Diagnostics") :
        s->page==KUI_SHELL_SCI_ASYNC_PROBE ? "A Quick   X 60s stress   Y With screen   R Speed   B Back" :
        s->page==KUI_SHELL_STORAGE_TEST_HISTORY ? "A View   Y Baseline   X Refresh   B Back" :
        s->page==KUI_SHELL_WIFI ? (s->wifi.found?"A Join   X Scan   Y Forget   START Update   B Network":"X Look again   B Network") :
        s->page==KUI_SHELL_GAMES_ADVANCED ? "D-pad Select   A Open   B Games" :
        s->page==KUI_SHELL_GAMES_PROBE_CONFIRM ? "A Start test   B Tests" :
        s->page==KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM ? (kui_shell_games_image_ready(s)?
            "A Start test   B Tests":"B Tests") :
        s->page==KUI_SHELL_GAMES_RETAIL_CONFIRM ? (kui_shell_games_launch_ready(s)?
            "A Launch   Y Options   B Details":"B Image details") :
        s->page==KUI_SHELL_GAMES_LAUNCH_OPTIONS ? (kui_shell_games_launch_ready(s)?
            "D-pad Select/change   A Toggle/open   START Launch   B Back":
            "D-pad Select/change   A Toggle/open   B Back") :
        s->page==KUI_SHELL_GAMES_EXTRA_OPTIONS ? "D-pad Select/change   A Open   B Options" :
        s->page==KUI_SHELL_GAMES_PATCHES ? (s->games_patch_editing?"LEFT/RIGHT Digit   UP/DOWN Change   A Save   B Cancel":"D-pad Select   A Toggle/edit   B Advanced") :
        s->page==KUI_SHELL_GAMES_TESTS ? "D-pad Select   A Open test   B Back" :
        s->page==KUI_SHELL_GAMES_INFO ? "B Advanced" :
        s->page==KUI_SHELL_GAMES_CHECK ? "A Check again   B Advanced" :
        s->page==KUI_SHELL_GAMES_CODEC ? "D-pad Select   A Save and inspect   B Details" :
        s->page==KUI_SHELL_CD_AUDIO ? "B SD music   START Home   R Refresh" :
        s->page==KUI_SHELL_MUSIC ? "B Parent   START Home   L Audio CD   LEFT/RIGHT Page" : "B Home";
    words(p,40,430,song_page||s->page==KUI_SHELL_MUSIC||s->page==KUI_SHELL_CD_AUDIO||s->page==KUI_SHELL_VMU_RESTORE||s->page==KUI_SHELL_VMU_COLLECTION||s->page==KUI_SHELL_VMU_CARD||s->page==KUI_SHELL_VMU_IMAGE||s->page==KUI_SHELL_VMU_IMPORT||s->page==KUI_SHELL_VMU_WIZARD||s->page==KUI_SHELL_VMU_ACTIONS||s->page==KUI_SHELL_VMU||s->page==KUI_SHELL_GAMES_RETAIL_CONFIRM||s->page==KUI_SHELL_GAMES_LAUNCH_OPTIONS||s->page==KUI_SHELL_GAMES_EXTRA_OPTIONS||s->page==KUI_SHELL_GAMES_PATCHES||s->page==KUI_SHELL_GAMES_TESTS||s->page==KUI_SHELL_GAMES_INFO?608:500,MUTED,controls,false);
}
/* Each Home app by page, so its row can move without touching this table.
 * All icons are embedded RGB565 assets with separate list-sized artwork. */
struct home_app {enum kui_shell_page page;const char *name,*category,*details[3];unsigned art;};
static const struct home_app home_apps[KUI_SHELL_HOME_APPS]={
    {KUI_SHELL_DISC_TOOLS,"Disc tools","Copy, recover or play",
        {"Copy discs or recover damaged reads.","Resume and verify saved dumps.","Or boot the disc through the BIOS."},KUI_ART_ICON_DISC_RIPPER},
    {KUI_SHELL_VMU_TOOLS,"VMU tools","Save files",
        {"Browse virtual and physical cards.","Import saves and assign game cards.","Back up or restore your saves."},KUI_ART_ICON_VMU_MANAGER},
    {KUI_SHELL_NETWORK,"Network","Connectivity",
        {"Inspect your network adapter,","set up Wi-Fi and test it. Share","the SD card by FTP."},KUI_ART_ICON_NETWORK},
    {KUI_SHELL_SETTINGS,"Settings","System preferences",
        {"Choose video, memory display","and background music.","Save preferences to SD."},KUI_ART_ICON_SETTINGS},
    {KUI_SHELL_DIAGNOSTICS,"Diagnostics","Diagnostics",
        {"Inspect the disc, SD card and RAM.","Run tests and review messages.","Save a diagnostic report."},KUI_ART_ICON_DIAGNOSTICS},
    {KUI_SHELL_MUSIC,"Music Player","Music",
        {"Play WAV or Ogg music from SD.","Listen to audio CD tracks","or keep music in the background."},KUI_ART_ICON_MUSIC_PLAYER},
    {KUI_SHELL_GAMES,"Games","SD game library",
        {"Launch native GD images from SD.","Browse your game library.","V" KUI_VERSION ": compatibility varies."},KUI_ART_ICON_GAMES},
    {KUI_SHELL_FILES,"File Manager","SD card files",
        {"Browse every folder and file on SD.","Open games, music and pictures.","Copy, move, rename or delete."},KUI_ART_ICON_FILE_MANAGER}};
static const struct home_app *home_app(unsigned row) {
    enum kui_shell_page page=kui_shell_home_pages[row<KUI_SHELL_HOME_APPS?row:0];
    for(unsigned i=0;i<KUI_SHELL_HOME_APPS;i++) if(home_apps[i].page==page) return &home_apps[i];
    return &home_apps[0];
}
static void home(struct paint *p, const struct kui_shell *s,const struct kui_shell_view *v) {
    unsigned selected=s->home_selected<KUI_SHELL_HOME_APPS?s->home_selected:0;
    panel(p,32,112,208,296,PANEL);
    for(unsigned i=0;i<KUI_SHELL_HOME_APPS;i++) {
        const struct home_app *app=home_app(i);
        unsigned y=116+i*33;
        if(selected==i) {
            panel(p,32,y,208,26,SELECTED);
            box(p,32,y+4,3,18,PINK);
        }
        art(p,44,y+1,24,24,kui_art_small_icons[app->art]);
        words(p,80,y+4,230,selected==i?WHITE:MUTED,app->name,false);
    }
    if(s->system_saved.show_memory && v->memory_valid) {
        char ram[48];snprintf(ram,sizeof(ram),"RAM %lu / %lu KiB",
            (unsigned long)(v->memory_used/1024),(unsigned long)(v->memory_physical/1024));
        words(p,44,391,230,MUTED,ram,false);
    } else {
        char count[24];snprintf(count,sizeof(count),"%u applications",KUI_SHELL_HOME_APPS);
        words(p,44,391,230,MUTED,count,false);
    }
    const struct home_app *chosen=home_app(selected);
    title(p,264,112,chosen->name);
    if(chosen->page==KUI_SHELL_DISC_TOOLS) {
        char inserted[160];
        snprintf(inserted,sizeof(inserted),"Inserted: %s",
            v->inserted_title&&v->inserted_title[0]?v->inserted_title:"No disc detected");
        label(p,264,144,CYAN,inserted);
    } else label(p,264,144,CYAN,chosen->category);
    panel(p,264,172,344,140,PANEL);
    art(p,372,178,128,128,kui_art_icons[chosen->art]);
    for(unsigned i=0;i<3;i++) label(p,264,326+i*19,MUTED,chosen->details[i]);
    panel(p,264,382,344,28,CYAN);
    label(p,382,388,NAVY,"A  Open app");
}
static void disc_tools(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Disc tools");
    char inserted[160];snprintf(inserted,sizeof(inserted),"Inserted: %s",
        v->inserted_title&&v->inserted_title[0]?v->inserted_title:"No disc detected");
    label(p,40,140,CYAN,inserted);
    const char *names[]={"Copy a disc","Recover a damaged disc","Verify a saved dump","Play inserted disc"};
    const char *details[]={"Make a normal copy, or resume one already started.",
        "Keep readable data and work on damaged sectors.",
        "Check an existing dump saved on the SD card.",
        "Leave K-UI and boot the disc through the console BIOS."};
    for(unsigned i=0;i<4;i++) {
        unsigned y=164+i*58;panel(p,32,y,576,52,i==s->disc_tools_selected?SELECTED:PANEL);
        label(p,48,y+7,i==s->disc_tools_selected?WHITE:CYAN,names[i]);
        label(p,48,y+29,MUTED,details[i]);
    }
    label(p,40,396,MUTED,"Choose what you want to do with the disc or saved copy.");
}
static void memory(struct paint *p, unsigned y, const struct kui_shell_view *v) {
    char text[73];
    if(v->memory_valid)
        snprintf(text,sizeof(text),"RAM %lu / %lu KiB   PEAK %lu KiB",
            (unsigned long)(v->memory_used/1024),(unsigned long)(v->memory_physical/1024),
            (unsigned long)(v->memory_peak/1024));
    else snprintf(text,sizeof(text),"RAM snapshot unavailable.");
    label(p,40,y,MUTED,text);
}
static void ripper(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    static const char *phases[]={"Identifying disc","Checking saved prefix",
        "Capturing disc","Verifying saved files","Creating export","Completed"};
    title(p,40,108,"Disc Ripper");
    char title_line[160];
    snprintf(title_line,sizeof(title_line),"%s: %s",v->busy?"Disc":"Inserted",
        v->busy?(v->disc_title&&v->disc_title[0]?v->disc_title:
            v->inserted_title&&v->inserted_title[0]?v->inserted_title:"Identifying..."):
        (v->inserted_title&&v->inserted_title[0]?v->inserted_title:"No disc detected"));
    label(p,40,136,CYAN,title_line);
    char destination[160];
    if(s->capture_job[0]) {
        label(p,40,158,MUTED,"Resume/Verify job:");path_words(p,200,158,608,CYAN,s->capture_job);
    } else {
        snprintf(destination,sizeof(destination),"Destination: %s%s",s->capture_network?"PC ":"",s->destination);
        label(p,40,158,MUTED,destination);
    }
    panel(p,32,182,576,122,PANEL);
    const char *phase=v->saving ? "Saving diagnostic report" :
        v->cancel_requested && v->busy ? "Stopping safely..." :
        v->drive_reset_required && !v->busy ? "Drive stopped - restart required" :
        v->outcome==KUI_SHELL_OUTCOME_STOPPED && !v->busy ?
            (v->job_dir && v->job_dir[0]?"Stopped - partial dump kept":"Stopped") :
        v->outcome==KUI_SHELL_OUTCOME_FAILED && !v->busy ? "Operation failed - see diagnostics" :
        v->outcome==KUI_SHELL_OUTCOME_COMPLETE && !v->busy ? "Completed" :
        !v->busy && v->outcome==KUI_SHELL_OUTCOME_NONE ? "Ready for a disc" :
        v->phase<sizeof(phases)/sizeof(phases[0]) ? phases[v->phase] : "Working";
    if(v->outcome==KUI_SHELL_OUTCOME_COMPLETE && !v->busy && !v->drive_reset_required &&
       v->disc_title && v->disc_title[0]) {
        snprintf(title_line,sizeof(title_line),"Completed: %.128s",v->disc_title);
        phase=title_line;
    }
    label(p,48,192,(v->outcome==KUI_SHELL_OUTCOME_FAILED||v->drive_reset_required)&&!v->busy?AMBER:CYAN,phase);
    char line[112];
    snprintf(line,sizeof(line),"TRACK %u / %u   TOTAL RETRIES %u",v->track,v->tracks,v->retries);
    label(p,48,214,WHITE,line);
    snprintf(line,sizeof(line),"%u KiB/s",v->rate_kib);
    label(p,440,214,WHITE,line);
    box(p,48,238,544,12,EDGE);
    /* Clamp malformed snapshots without overflowing done*width. Progress values
     * are bytes, and whole-MiB scaling is too coarse for small audio tracks. */
    unsigned width=0;
    if(v->total) width=v->done>=v->total?544u:
        (unsigned)((double)v->done/(double)v->total*544.0);
    if(width) box(p,48,238,width,12,CYAN);
    unsigned percent=kui_shell_progress_tenths(v->done,v->total);
    snprintf(line,sizeof(line),"%u.%u%%   %lu / %lu MiB   SAVED %lu MiB",percent/10,percent%10,
        (unsigned long)(v->done/1048576),(unsigned long)(v->total/1048576),
        (unsigned long)(v->committed/1048576));
    label(p,48,258,WHITE,line);
    snprintf(line,sizeof(line),"ELAPSED %lu:%02lu",
        (unsigned long)(v->elapsed_ms/60000),
        (unsigned long)(v->elapsed_ms/1000%60));
    label(p,48,280,MUTED,line);
    if(v->busy && !v->saving && v->phase>=1 && v->phase<=4) {
        uint64_t eta;
        if(kui_shell_phase_eta(v,&eta)) {
            if(eta>=360000) snprintf(line,sizeof(line),"PHASE ETA >99h");
            else if(eta>=3600) snprintf(line,sizeof(line),"PHASE ETA %lu:%02lu:%02lu",
                (unsigned long)(eta/3600),(unsigned long)(eta/60%60),(unsigned long)(eta%60));
            else snprintf(line,sizeof(line),"PHASE ETA %lu:%02lu",
                (unsigned long)(eta/60),(unsigned long)(eta%60));
        } else snprintf(line,sizeof(line),"PHASE ETA %s",v->progress_age_ms>3000 ||
            v->cancel_requested?"waiting":"calculating");
        label(p,336,280,MUTED,line);
    }
    if(v->retry_attempt) {
        snprintf(line,sizeof(line),"Read retry %u/%u at FAD %lu",v->retry_attempt,v->retry_limit,(unsigned long)v->retry_fad);
        label(p,40,312,AMBER,line);
    } else label(p,40,312,v->drive_reset_required||v->dma_degraded?AMBER:v->busy?MUTED:WHITE,
        v->drive_reset_required?"Restart the console before another disc operation.":
        v->dma_degraded?"Drive errors: PIO active. Reboot to restore DMA.":
        s->capture_job[0]?"A New dump   X Resume selected   Y Verify selected":
        "A New dump   X Resume latest   Y Verify latest");
    if(!v->busy && v->outcome==KUI_SHELL_OUTCOME_COMPLETE) {
        uint16_t color=MUTED;
        const char *result="Reference not checked";
        if(v->reference_checked) switch(v->reference.result) {
        case KUI_KNOWN_FULL_MATCH: color=GREEN; result="FULL TRACK MATCH"; break;
        case KUI_KNOWN_DATA_MATCH: color=AMBER; result="Data tracks match; audio not confirmed"; break;
        case KUI_KNOWN_IDENTIFIED: color=AMBER; result="Listed data matches; incomplete reference"; break;
        case KUI_KNOWN_PARTIAL: color=AMBER; result="Partial match; not a full reference match"; break;
        case KUI_KNOWN_NO_DATABASE: result="No reference database on SD"; break;
        case KUI_KNOWN_NO_MATCH: result="No reference match; inconclusive"; break;
        case KUI_KNOWN_CANCELLED: result="Reference check cancelled"; break;
        case KUI_KNOWN_ERROR: result="Reference check unavailable"; break;
        default: break;
        }
        panel(p,32,336,576,28,PANEL); box(p,32,341,3,18,color);
        char badge[128]; snprintf(badge,sizeof(badge),"STREAM CRC: %s",result);
        label(p,44,342,color,badge);
        label(p,40,368,v->saved_verified?CYAN:MUTED,v->saved_verified?
            "Saved bytes reread and verified.":"Saved bytes not fully reread. Verify later if needed.");
    } else if(!v->busy && v->outcome==KUI_SHELL_OUTCOME_NONE) {
        label(p,40,342,MUTED,v->message && v->message[0]?v->message:s->capture_job[0]?
            "Resume/Verify: selected job. LEFT restores Automatic latest.":"Resume/Verify: Automatic latest matching job.");
        char folder[160];snprintf(folder,sizeof(folder),"New folder label: %s",
            s->capture_folder_label[0]?s->capture_folder_label:"Auto (disc title)");
        label(p,40,368,MUTED,folder);
    } else {
        label(p,40,342,MUTED,v->message && v->message[0]?v->message:
            "A new dump always uses a separate folder.");
        if(v->gdi_name && v->gdi_name[0]) label(p,40,368,MUTED,v->gdi_name);
        else if(v->job_dir && v->job_dir[0]) label(p,40,368,MUTED,v->job_dir);
    }
    memory(p,392,v);
}
static void destination(struct paint *p,const struct kui_shell *s,
        const struct kui_shell_view *v) {
    title(p,40,108,s->browse_for_capture_job?"Choose saved job":s->browse_for_scan?"Choose dump to scan":"Choose destination");
    words(p,40,138,478,CYAN,s->browse_path,false);
    char page[32]; snprintf(page,sizeof(page),"PAGE %u%s",s->browser_page+1,
        s->listing.has_more?" +":"");
    label(p,492,138,MUTED,page);
    panel(p,32,166,576,204,PANEL);
    unsigned count=s->listing.count<KUI_DEST_PAGE_SIZE?s->listing.count:KUI_DEST_PAGE_SIZE;
    if(!count) label(p,48,188,MUTED,v->busy?"Loading folders...":
        s->browse_for_capture_job?"No subfolders. X selects this saved-job folder.":
        s->browse_for_scan?"No subfolders. Y scans this dump folder.":
        "No subfolders. Y selects the current folder.");
    for(unsigned i=0;i<count;i++) {
        const struct kui_destination_entry *entry=&s->listing.entries[i];
        unsigned y=174+i*23;
        if(i==s->browser_selected) {
            panel(p,40,y,560,23,SELECTED);
            box(p,40,y+4,3,15,PINK);
        }
        uint16_t color=(s->browse_for_capture_job?!entry->job_selectable:entry->disabled)?AMBER:i==s->browser_selected?WHITE:MUTED;
        if(s->browse_for_capture_job) path_words(p,52,y+2,608,color,entry->name);
        else label(p,52,y+2,color,entry->name);
    }
    label(p,40,376,v->busy?MUTED:WHITE,s->browse_for_capture_job?
        "A Open   Y Select highlighted job   X Select this folder":s->browse_for_scan?
        "A Open folder   Y Scan this folder":"A Open folder   Y Use folder   X Type path");
    label(p,40,396,s->destination_notice[0]?AMBER:MUTED,
        s->destination_notice[0]?s->destination_notice:
        s->browse_for_capture_job?"Resume/Verify only; disc and checkpoint checks still apply.":
        s->browse_for_scan?"Choose the game folder containing its .gdi file.":
        "The selected destination is saved to SD.");
}
static void keyboard(struct paint *p,const struct kui_shell *s) {
    bool naming=s->files_keyboard,wifi=s->wifi_keyboard;
    char heading[80];
    if(wifi && s->wifi_typing_name) snprintf(heading,sizeof(heading),"Wi-Fi network name");
    else if(wifi) snprintf(heading,sizeof(heading),"Password for %.32s",s->wifi_request.ssid);
    title(p,40,108,s->vmu_name_keyboard?"Name this virtual VMU":s->capture_label_keyboard?"New dump folder label":s->storage_test_keyboard?"Card nickname":wifi?heading:!naming?"Type destination":s->files_job.op==KUI_FILES_OP_RENAME?"Rename":"New folder");
    label(p,40,136,MUTED,s->vmu_name_keyboard?"START or DONE saves its display name.":s->capture_label_keyboard?"START or DONE applies it. Empty uses Auto (disc title).":s->storage_test_keyboard?"Label for test reports; START or DONE applies it.":wifi?(s->wifi_typing_name?"As the router shows it; START or DONE goes on to its password.":
        "START or DONE joins; the board keeps the network for next time."):naming?
        "Name on the SD card; START or DONE applies it.":
        "Folder path on SD; START or DONE saves your choice.");
    panel(p,32,160,576,32,PANEL);
    char input[KUI_DEST_ROOT_CAP+8];
    const char *start=s->keyboard;
    bool clipped=false;
    while(*start && kui_shell_font_width(start,false)>510) {
        ++start;
        while(((unsigned char)*start&0xc0u)==0x80u) ++start;
        clipped=true;
    }
    snprintf(input,sizeof(input),"%s%s_",clipped?"...":"",start);
    label(p,44,167,WHITE,input);
    for(unsigned key=0;key<KUI_SHELL_KEY_COUNT;key++) {
        unsigned x,y,w;
        if(key<40) { x=40+(key%10)*56; y=204+(key/10)*34; w=52; }
        else { x=40+(key-40)*188; y=340; w=180; }
        bool selected=s->keyboard_selected==key;
        panel(p,x,y,w,28,selected?SELECTED:PANEL);
        const char *name=kui_shell_key_label(key,s->keyboard_layer);
        unsigned width=kui_shell_font_width(name,false);
        words(p,x+(w-width)/2,y+5,x+w-4,selected?WHITE:MUTED,name,false);
        if(selected) box(p,x+6,y+25,w-12,2,PINK);
    }
    label(p,40,378,CYAN,s->keyboard_layer==KUI_SHELL_KEYS_UPPER?"Y Shift: UPPERCASE":
        s->keyboard_layer==KUI_SHELL_KEYS_SYMBOLS?"Y Shift: symbols":"Y Shift: lowercase");
    label(p,40,398,s->destination_notice[0]?AMBER:MUTED,
        s->destination_notice[0]?s->destination_notice:s->vmu_name_keyboard?"Up to 48 characters. Saves and game assignments stay intact.":s->capture_label_keyboard?"New folders only; disc identity and saved job names stay unchanged.":s->storage_test_keyboard?"Up to 23 characters, e.g. Samsung 128GB. Optional.":wifi?(s->wifi_typing_name?
        "Leave the password empty on the next screen for an open network.":
        "Letters, digits and symbols (Y); 8 to 63 characters."):naming?
        "A name cannot contain / or end with a space or dot.":
        "Example: /Games   New folders are created when used.");
}
static void advanced(struct paint *p,const struct kui_shell *s) {
    static const char *names[]={"Verify saved files","Resume interrupted dump","Capture settings",
        "Quick resume (sizes only)","Destination folder","Advanced CRC scan","Damaged disc salvage",
        "Saved job for Resume/Verify","New dump folder label","Repair / resume a separate copy",
        "Suspect-sector reread evidence","Rip destination: local / PC","Single-session CD source",
        "Repair / resume in place","Start a new repair copy"};
    static const char *details[KUI_SHELL_RIPPER_TOOL_COUNT][3]={
        {"Reread saved tracks and check their recorded hashes.",
         "This checks the card; the stream CRC badge compares",
         "captured tracks with an independent reference."},
        {"Resume the latest job matching the inserted disc.",
         "Checks the checkpoint before capture continues.",
         "Read failures use the reader's bounded retries."},
        {"Choose CRC32 or SHA-256 and optional full readback.",
         "New dumps use your saved choices.",
         "Existing jobs keep their recorded hash mode."},
        {"Check saved file sizes, then continue the dump.",
         "Raw jobs can skip hashes of previously saved bytes.",
         "Direct 2048 and repaired jobs retain full verification."},
        {"Browse folders on SD or enter a destination path.",
         "A new dump uses a game-named folder there.",
         "Existing dumps are kept; new names get a number."},
        {"Scan a saved dump: track hashes and Mode1 sector checks.",
         "Writes a report. No disc reads or track modifications.",
         "Audio needs recorded hashes to be verified."},
        {"Read damaged media into a separate salvage job.",
         "Skips bad sectors and records their missing addresses.",
         "Automatically retries holes after collecting good data."},
        {"Choose an exact saved job instead of the latest match.",
         "Resume and Verify check it against the inserted disc.",
         "LEFT on Disc Ripper restores Automatic latest."},
        {"Choose the folder label for new dumps only.",
         "Auto uses the disc title; existing jobs keep their names.",
         "The label stays in memory until this launcher restarts."},
        {"Choose a raw saved dump and insert its matching disc.",
         "Repair supported bad sectors; continue a matching copy.",
         "Originals stay intact; stopped work resumes next time."},
        {"Reopen suspect saved sectors and compare repeat reads.",
         "Save byte differences and raw samples for diagnosis.",
         "No optical reads; original dump files stay intact."},
        {"A switches between local storage and a PC volume.",
         "PC needs /KUI/network-rip.cfg and the receiver tool.",
         "Resume and Verify use the same selected destination."},
        {"Enable only for a known single-session CD or audio CD.",
         "Captures firmware TOC INDEX01 intervals as BIN/CUE.",
         "Session count, INDEX00 and subchannels are not measured."},
        {"Repair the selected dump without copying every track.",
         "Retains original sectors and checked repair candidates.",
         "Matching disc required; confirm before changing files."},
        {"Create a separate repair transaction from this source.",
         "Use after the source changes or for a fresh attempt.",
         "Keeps previous repair jobs; later use Resume copy."}};
    unsigned selected=s->advanced_selected<KUI_SHELL_RIPPER_TOOL_COUNT?s->advanced_selected:0;
    title(p,40,108,"Advanced disc tools");
    label(p,40,138,MUTED,"Choose an operation for your disc or saved dump.");
    char position[16];snprintf(position,sizeof(position),"%u/%u",selected+1u,KUI_SHELL_RIPPER_TOOL_COUNT);
    label(p,552,138,CYAN,position);
    unsigned first=selected>=8u?selected-7u:0u;
    for(unsigned i=first;i<KUI_SHELL_RIPPER_TOOL_COUNT && i<first+8u;i++) {
        unsigned y=156+(i-first)*22;
        panel(p,32,y,576,20,selected==i?SELECTED:PANEL);
        if(selected==i) box(p,32,y+3,3,14,PINK);
        label(p,48,y+2,selected==i?WHITE:MUTED,names[i]);
    }
    for(unsigned i=0;i<3;i++) label(p,40,344+i*18,MUTED,details[selected][i]);
    if(selected==KUI_SHELL_RIPPER_TOOL_JOB) {
        panel(p,32,397,576,22,PANEL);
        path_words(p,40,401,608,CYAN,s->capture_job[0]?s->capture_job:"Automatic latest matching job");
    } else if(selected==KUI_SHELL_RIPPER_TOOL_LABEL) {
        panel(p,32,397,576,22,PANEL);
        label(p,40,401,CYAN,s->capture_folder_label[0]?s->capture_folder_label:"Auto (disc title)");
    } else if(selected==KUI_SHELL_RIPPER_TOOL_NETWORK) {
        label(p,40,401,CYAN,s->capture_network?"Selected: PC volume":"Selected: local storage");
    } else if(selected==KUI_SHELL_RIPPER_TOOL_CD_SOURCE) {
        label(p,40,401,CYAN,s->capture_cd_toc?"CD capture/recovery: enabled (single session)":"CD source: disabled");
    }
}
static void ripper_settings(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    title(p,40,108,"Ripper settings");
    label(p,40,134,MUTED,"UP/DOWN Choose   LEFT/RIGHT Change");
    const char *names[]={"Capture hashes","Read back saved files","Output format","Read retries","Strict sector checks"};
    char retries[16];snprintf(retries,sizeof(retries),"%u",s->draft.retry_limit?s->draft.retry_limit:10u);
    const char *values[]={s->draft.crc_only?"CRC32":"CRC32 + SHA-256",
        s->draft.capture_format==KUI_CAPTURE_FORMAT_GDI_DIRECT_2048?"ON (cooked required)":
        s->draft.capture_format>=KUI_CAPTURE_FORMAT_CSO?"ON (export required)":
        !s->draft.crc_only?"ON (SHA required)":s->draft.end_readback?"ON":"OFF",
        kui_capture_format_name(s->draft.capture_format),retries,s->draft.strict_sectors?"ON":"OFF"};
    for(unsigned i=0;i<5;i++) {
        unsigned y=164+i*30;
        panel(p,32,y,576,27,i==s->setting_selected?SELECTED:PANEL);
        box(p,32,y,4,27,i==s->setting_selected?CYAN:EDGE);
        label(p,48,y+6,WHITE,names[i]);
        label(p,416,y+6,i==s->setting_selected?CYAN:MUTED,values[i]);
    }
    const char *first,*second;
    if(s->setting_selected==0) {
        first="CRC32 is the faster capture option.";
        second="SHA-256 jobs always get a full readback.";
    } else if(s->setting_selected==1) {
        first=s->draft.capture_format==KUI_CAPTURE_FORMAT_GDI_DIRECT_2048?
            "Direct 2048 jobs always check every saved byte.":s->draft.capture_format>=KUI_CAPTURE_FORMAT_CSO?
            "Compressed exports require a full raw readback.":"Full readback checks every saved byte; it takes time.";
        second=s->draft.capture_format==KUI_CAPTURE_FORMAT_GDI_DIRECT_2048?
            "Payload hashes cannot match raw-track catalogues.":s->draft.capture_format>=KUI_CAPTURE_FORMAT_CSO?
            "Raw files are verified before the export starts.":"With it off, use Verify later or check on a PC.";
    } else if(s->setting_selected==3) {
        first="Additional attempts after an unreadable sector.";
        second="Choose 1, 5, 10, 20 or 50; default is 10.";
    } else if(s->setting_selected==4) {
        first="Check Mode1 address, EDC and P/Q during capture.";
        second="Strict mode refuses unsupported sector layouts.";
    } else if(s->draft.capture_format==KUI_CAPTURE_FORMAT_GDI_DIRECT_2048) {
        first="Save data as 2048-byte sectors while reading the disc.";
        second="Keeps raw audio; no full raw data tracks are stored.";
    } else if(s->draft.capture_format==KUI_CAPTURE_FORMAT_GDI_2048) {
        first="Create verified 2048-byte data tracks and a GDI.";
        second="Original raw data and audio tracks are retained.";
    } else {
        first=s->draft.capture_format>=KUI_CAPTURE_FORMAT_CSO?"Verify raw rip, then create the compressed export.":
            "Both keep raw tracks; only the descriptor changes.";
        second=s->draft.capture_format==KUI_CAPTURE_FORMAT_CHD?"CHD includes captured data and audio tracks.":
            s->draft.capture_format>=KUI_CAPTURE_FORMAT_CSO?"CSO/ZSO store data; raw audio tracks are kept.":
            "Resume keeps the format chosen for that job.";
    }
    label(p,40,330,MUTED,first); label(p,40,350,MUTED,second);
    bool dirty=kui_shell_settings_dirty(s);
    const char *notice=v->settings_notice && v->settings_notice[0]?v->settings_notice:NULL;
    label(p,40,376,dirty?AMBER:CYAN,dirty?
        "Unsaved changes. A saves; B discards.":notice?notice:"Ready to edit. A saves to SD.");
    label(p,40,396,MUTED,dirty && notice?notice:
        "New dumps use these choices. Resume keeps job hash mode.");
}
static void system_settings(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"System settings");
    label(p,40,136,MUTED,"UP/DOWN Choose   LEFT/RIGHT Change");
    const char *names[]={"Video mode","Home memory display","Background music","Music volume",
        "Startup chime","Start in","Console clock","Restore defaults","Safe area","System tools","Menu sounds"};
    char volume[16];snprintf(volume,sizeof(volume),"%u%%",s->system_draft.music_volume);
    const char *values[]={kui_system_video_name(s->system_draft.video_mode),
        s->system_draft.show_memory?"ON":"OFF",s->system_draft.music_enabled?"ON":"OFF",volume,
        s->system_draft.startup_chime?"ON":"OFF",kui_system_startup_name(s->system_draft.startup_app),
        "A Edit local time","A Review",s->system_draft.screen_inset==2?"32 px sides":
        s->system_draft.screen_inset==1?"16 px sides":"Full width","A Open",s->system_draft.menu_sounds?"ON":"OFF"};
    unsigned first=s->system_selected>=8?s->system_selected-6:0;
    for(unsigned i=first;i<first+8 && i<11;i++) {
        unsigned y=158+(i-first)*25;
        panel(p,32,y,576,23,i==s->system_selected?SELECTED:PANEL);
        if(i==s->system_selected) box(p,32,y+3,3,17,PINK);
        label(p,48,y+3,WHITE,names[i]);
        words(p,400,y+3,596,i==s->system_selected?CYAN:MUTED,values[i],false);
    }
    const char *detail=s->system_selected==0?"640x480 output. VGA follows the connected cable.":
        s->system_selected==1?"Ripper RAM and peak usage are always displayed.":
        s->system_selected<=3?"X Next song. Music continues when you leave this page.":
        s->system_selected==4?"Play the short K-UI sound when the next session starts.":
        s->system_selected==5?"Open this app after the splash; no operation starts.":
        s->system_selected==6?"Read or set the console's local date and time.":
        s->system_selected==7?"Resets the draft. Save to apply; B keeps your saved settings.":
        s->system_selected==8?"Inset the display for TVs that crop the picture at its sides.":
        s->system_selected==9?"Inspect hardware, save verified backups, or restart K-UI.":
        "Play a short sound for menu movement and selection.";
    label(p,40,366,MUTED,detail);
    label(p,40,390,kui_shell_system_dirty(s)?AMBER:CYAN,kui_shell_system_dirty(s)?
        "Unsaved changes. A saves; B discards.":v->settings_notice&&v->settings_notice[0]?
        v->settings_notice:"A saves preferences; clock, defaults and tools open separately.");
}
static void clock_page(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Console clock");
    label(p,40,136,MUTED,"UP/DOWN Choose field   LEFT/RIGHT Change");
    const char *names[]={"Year","Month","Day","Hour","Minute","Second"};
    const struct kui_datetime *d=&s->clock_draft;
    unsigned values[]={d->year,d->month,d->day,d->hour,d->minute,d->second};
    for(unsigned i=0;i<6;i++) {
        unsigned y=162+i*31;
        panel(p,32,y,576,27,i==s->clock_selected?SELECTED:PANEL);
        label(p,48,y+5,WHITE,names[i]);
        char number[16];snprintf(number,sizeof(number),s->clock_valid?"%02u":"--",values[i]);
        words(p,472,y+5,596,i==s->clock_selected?CYAN:MUTED,number,false);
    }
    label(p,40,366,s->clock_valid?MUTED:AMBER,s->clock_notice[0]?s->clock_notice:
        "Local wall time. A reviews before changing the hardware clock.");
    label(p,40,390,MUTED,"Future files use the console clock. Existing dates stay intact.");
}
static void app_status(struct paint *p,const struct kui_app_status *status,bool busy,unsigned y) {
    if(!status) return;
    uint16_t color=status->complete?(status->passed?CYAN:AMBER):CYAN;
    if(status->stopped) color=MUTED;
    label(p,40,y,color,status->message);
    if(status->total) {
        box(p,40,y+24,560,8,EDGE);
        unsigned width=status->done>=status->total?560:
            (unsigned)((double)status->done/(double)status->total*560);
        if(width) box(p,40,y+24,width,8,busy?CYAN:color);
    }
}
static void utility_page(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    bool memory_test=s->page==KUI_SHELL_MEMORY;
    title(p,40,108,memory_test?"Memory Test":"Network");
    label(p,40,138,MUTED,memory_test?"Tests an allocated RAM region using data patterns.":
        "BBA, LAN adapter, or a W5500 or the Wi-Fi board on the SCI port.");
    label(p,40,160,v->busy?MUTED:WHITE,memory_test?"A Run memory test":
        "A Inspect adapter   X Test network   Y FTP server   START Wi-Fi");
    const struct kui_app_status *status=v->app_status;
    panel(p,32,194,576,214,PANEL);
    app_status(p,status,v->busy,202);
    if(status) {
        unsigned count=status->line_count<KUI_APP_LINES?status->line_count:KUI_APP_LINES;
        /* Reserve the title/progress rows; show the most recent eight lines. */
        unsigned first=count>8?count-8:0;
        for(unsigned i=first;i<count;i++) label(p,40,250+(i-first)*18,MUTED,status->lines[i]);
    } else label(p,40,210,MUTED,"Ready. Press A to begin.");
}
static void vmu_range(char *line,size_t cap,const char *noun,unsigned page,unsigned count,unsigned total) {
    uint64_t start=(uint64_t)page*KUI_VMU_ROWS;
    if(!count || start>=total) snprintf(line,cap,"%s 0 of %u",noun,total);
    else {
        uint64_t end=start+count;
        if(end>total) end=total;
        snprintf(line,cap,"%s %u-%u of %u",noun,(unsigned)start+1,(unsigned)end,total);
    }
}
static void vmu_scrollbar(struct paint *p,unsigned page,unsigned selected,unsigned total) {
    const unsigned y=196,height=168;
    box(p,592,y,6,height,EDGE);
    if(!total) return;
    unsigned visible=total<KUI_VMU_ROWS?total:KUI_VMU_ROWS;
    unsigned thumb=(unsigned)((uint64_t)height*visible/total);
    if(thumb<12) thumb=12;
    uint64_t index=(uint64_t)page*KUI_VMU_ROWS+selected;
    if(index>=total) index=total-1;
    unsigned offset=total>1?(unsigned)(index*(height-thumb)/(total-1)):0;
    box(p,592,y+offset,6,thumb,CYAN);
}
static void vmu_save_rows(struct paint *p,const struct kui_vmu_view *view,unsigned page,unsigned selected) {
    unsigned count=view->count<KUI_VMU_ROWS?view->count:KUI_VMU_ROWS;
    for(unsigned i=0;i<count;i++) {
        const struct kui_vmu_entry *entry=&view->entries[i];
        unsigned y=196+i*21;
        if(i==selected) panel(p,40,y,544,21,SELECTED);
        const char *name=entry->description[0]?entry->description:entry->app_id[0]?entry->app_id:entry->name;
        words(p,52,y+1,486,i==selected?WHITE:MUTED,name,false);
        unsigned long used=entry->bytes/512u+(entry->bytes%512u!=0);
        char blocks[32];snprintf(blocks,sizeof(blocks),"%lu %s",used,used==1?"block":"blocks");
        words(p,492,y+1,584,MUTED,blocks,false);
    }
    vmu_scrollbar(p,page,selected,view->total);
    if(selected<count) {
        const struct kui_vmu_entry *entry=&view->entries[selected];
        char detail[112];
        snprintf(detail,sizeof(detail),"File: %s   %lu bytes%s%s",entry->name,(unsigned long)entry->bytes,
            entry->app_id[0]?"   App: ":"",entry->app_id);
        label(p,40,378,MUTED,detail);
    }
}
static void vmu_page(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Physical VMUs");
    char line[112],range[48];
    vmu_range(range,sizeof(range),"Saves",s->vmu_page,s->vmu.count,s->vmu.total);
    snprintf(line,sizeof(line),"VMU %c%u   %s   %u free blocks",
        'A'+s->vmu_slot/2,s->vmu_slot%2+1,range,s->vmu.free_blocks);
    label(p,40,138,CYAN,line);
    label(p,40,160,v->busy?MUTED:WHITE,"UP/DOWN Scroll   START Page / refresh   Y Back up all");
    panel(p,32,190,576,178,PANEL);
    unsigned count=s->vmu.count<KUI_VMU_ROWS?s->vmu.count:KUI_VMU_ROWS;
    if(!count) label(p,48,206,MUTED,v->busy?"Reading VMU...":
        s->vmu.present?"No saves on this page.":"No readable VMU in this slot.");
    vmu_save_rows(p,&s->vmu,s->vmu_page,s->vmu_selected);
    const struct kui_app_status *status=v->app_status?v->app_status:&s->vmu.status;
    label(p,40,396,status->complete&&!status->passed?AMBER:CYAN,status->message[0]?status->message:
        "UP/DOWN Scroll all saves   L Save / card actions");
}
static void vmu_tools(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"VMU tools");
    label(p,40,140,MUTED,"Virtual VMU is independent of reader and prefetch settings.");
    char preference[64];
    snprintf(preference,sizeof(preference),"Use virtual VMU: %s",s->vmu_emulation_known?
        (s->vmu_emulation_enabled?"On":"Off"):s->vmu_emulation_loading?"Reading...":"Unavailable");
    const char *names[]={"Virtual VMUs","Physical VMUs","Import saves","Backups & restore",preference};
    const char *details[]={"Cards, saves, free blocks and assigned games.",
        "Connected cards in controller ports A to D.",
        "Combine physical saves and choose their game cards.",
        "Browse saved backups and restore individual saves.",
        "For supported native games. Applies on next launch."};
    for(unsigned i=0;i<5;i++) {
        unsigned y=168+i*42;
        panel(p,32,y,576,38,i==s->vmu_tools_selected?SELECTED:PANEL);
        label(p,48,y+3,i==s->vmu_tools_selected?WHITE:CYAN,names[i]);
        label(p,48,y+20,MUTED,details[i]);
    }
    words(p,40,389,608,s->vmu_emulation_known?CYAN:AMBER,
        s->vmu_emulation_notice[0]?s->vmu_emulation_notice:
        "Reader and prefetch settings stay unchanged; cards are kept.",false);
}
static void wizard_message(struct paint *p,const char *message,uint16_t color) {
    unsigned row=0;
    while(message && *message && row<2u) {
        while(*message==' ') ++message;
        char text[160];snprintf(text,sizeof(text),"%s",message);
        size_t count=strlen(text),take=count;
        while(take && word_width(p,text,false)>568u) text[--take]=0;
        if(take<count) {
            size_t space=take;while(space && text[space-1u]!=' ') --space;
            if(space) take=space-1u;
            text[take]=0;
        }
        if(!take) break;
        label(p,40,374+row*22u,color,text);message+=take;++row;
    }
}
static void vmu_auto_wizard(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_vmu_wizard_view *w=&s->vmu_wizard;
    bool question=w->stage==KUI_VMU_WIZARD_GROUP_GAME,groups=w->stage==KUI_VMU_WIZARD_GROUPS;
    bool done=w->stage==KUI_VMU_WIZARD_DONE;char line[160];
    title(p,40,108,question?"Which game uses these saves?":groups?"Saves that need your help":
        done?"VMU import complete":"Ready to import saves");
    if(question) {
        words(p,40,140,608,CYAN,w->group_label,false);
        snprintf(line,sizeof(line),"VMU %c%u | %u file%s | %u blocks | Choose the game below",
            'A'+w->group_source_slot/2u,w->group_source_slot%2u+1u,w->group_files,
            w->group_files==1u?"":"s",w->group_blocks);
        label(p,40,162,MUTED,line);
    } else {
        snprintf(line,sizeof(line),"%u game%s ready | %u separate card%s | %u question%s",
            w->selected_games,w->selected_games==1?"":"s",w->variants,w->variants==1?"":"s",
            w->unresolved_groups,w->unresolved_groups==1?"":"s");label(p,40,140,CYAN,line);
        label(p,40,162,MUTED,groups?"The other saves were matched automatically.":"Each game and save version gets its own 200-block VMU.");
    }
    panel(p,32,188,576,177,PANEL);
    unsigned count=groups?w->groups_count:w->count;
    for(unsigned i=0;i<count && i<KUI_VMU_WIZARD_ROWS;i++) {
        unsigned y=193+i*21u;
        if((groups || question) && i==s->vmu_wizard_selected) panel(p,40,y,544,21,SELECTED);
        if(groups) {
            const struct kui_vmu_wizard_group *g=&w->groups[i];
            words(p,48,y+1,434,i==s->vmu_wizard_selected?WHITE:MUTED,g->label,false);
            snprintf(line,sizeof(line),"%c%u: %u file%s",'A'+g->source_slot/2u,g->source_slot%2u+1u,g->files,g->files==1?"":"s");
            words(p,442,y+1,592,CYAN,line,false);
        } else {
            const struct kui_vmu_wizard_game *g=&w->games[i];
            words(p,48,y+1,410,question?(g->supported?WHITE:MUTED):WHITE,g->title,false);
            if(question) snprintf(line,sizeof(line),"%s",g->supported?"Choose":"Unavailable");
            else if(g->variants>1u && g->free_min!=g->free_max)
                snprintf(line,sizeof(line),"%u cards:%u-%u free",g->variants,g->free_min,g->free_max);
            else if(g->variants>1u) snprintf(line,sizeof(line),"%u cards: %u free",g->variants,g->free_min);
            else snprintf(line,sizeof(line),"%u / 200 free",g->free_min);
            words(p,418,y+1,592,question?MUTED:CYAN,line,false);
        }
    }
    if(!count) label(p,48,210,MUTED,groups?"All save matches are ready. Press START to review.":
        question?"No games found in /Games; keep these saves as a backup.":"No game cards selected. Your sources will be backed up.");
    unsigned total=groups?w->groups_total:w->total,page=groups?w->groups_page:w->page;
    vmu_scrollbar(p,page,s->vmu_wizard_selected,total);
    if(groups && s->vmu_wizard_selected<count) {
        words(p,40,374,608,MUTED,w->groups[s->vmu_wizard_selected].reason,false);
        label(p,40,396,CYAN,"Choose its game, or keep it only in the full source backup.");
    } else if(question) {
        label(p,40,374,MUTED,"Related files stay together on this game's own card.");
        label(p,40,396,CYAN,"X keeps these saves in the source backup without assigning.");
    } else {
        bool off=done && s->vmu_emulation_known && !s->vmu_emulation_enabled;
        label(p,40,374,WHITE,off?"Cards saved; Virtual VMU remains Off.":done?"Separate game cards and full source backups saved.":
            w->unresolved_groups?"Answer the remaining questions before importing.":"A Import saves. Your physical VMUs are read only.");
        snprintf(line,sizeof(line),"%u source VMU%s preserved%s",w->sources,w->sources==1?"":"s",
            w->skipped_groups?"; backup-only saves are kept.":" in full backups.");
        label(p,40,396,MUTED,off?"Enable Virtual VMU in VMU tools to use these cards.":line);
    }
    if(v->busy || s->vmu_wizard_loading || (w->status.complete && !w->status.passed)) {
        box(p,32,368,576,48,NAVY);
        wizard_message(p,w->status.message[0]?w->status.message:"Working...",w->status.complete&&!w->status.passed?AMBER:CYAN);
    }
    if(w->missing_card_confirmation) {
        box(p,32,174,576,222,NAVY);panel(p,48,182,544,206,PANEL);
        title(p,64,198,"An old card is missing");
        snprintf(line,sizeof(line),"%u game choice%s %s to removed virtual cards.",w->missing_cards,w->missing_cards==1?"":"s",
            w->missing_cards==1?"refers":"refer");
        label(p,64,238,MUTED,line);
        label(p,64,264,WHITE,"Use your imported saves for these games instead?");
        label(p,64,290,MUTED,"Physical VMUs and all other cards are kept.");
        label(p,64,336,CYAN,"A Use imported saves   B Cancel");
    }
    if(s->confirm_vmu_wizard_reset) {
        box(p,32,166,576,218,NAVY);panel(p,48,174,544,177,PANEL);
        title(p,64,196,"Start a new import?");
        label(p,64,234,MUTED,"Leave this session and read your VMUs again.");
        label(p,64,259,MUTED,"Previously created cards and physical VMUs stay intact.");
        label(p,64,309,WHITE,"A Start over   B Keep this session");
    }
}
static void vmu_wizard(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_vmu_wizard_view *w=&s->vmu_wizard;
    if(w->automatic && (w->stage==KUI_VMU_WIZARD_GROUPS || w->stage==KUI_VMU_WIZARD_GROUP_GAME ||
       w->stage==KUI_VMU_WIZARD_REVIEW || w->stage==KUI_VMU_WIZARD_DONE)) {
        vmu_auto_wizard(p,s,v);return;
    }
    bool working=v->busy || s->vmu_wizard_loading;
    char line[160];
    title(p,40,108,w->view_only?"Games using this card":w->stage==KUI_VMU_WIZARD_SAVES?"Choose saves for this game":
        w->stage==KUI_VMU_WIZARD_GAMES_STAGE?(w->existing_card?"Choose games for this card":"Set up game cards"):
        w->stage==KUI_VMU_WIZARD_REVIEW?"Review VMU import":
        w->stage==KUI_VMU_WIZARD_DONE?"VMU import complete":"Import VMU saves");
    label(p,440,111,s->vmu_emulation_known && s->vmu_emulation_enabled?CYAN:AMBER,
        s->vmu_emulation_known?(s->vmu_emulation_enabled?"Virtual VMU On":"Virtual VMU Off"):"Virtual VMU ?");
    if(w->stage==KUI_VMU_WIZARD_SOURCES) {
        label(p,40,140,MUTED,"1 Read VMUs   /   2 Match saves   /   3 Import");
        panel(p,32,173,576,68,PANEL);
        snprintf(line,sizeof(line),"Source VMU %c%u",'A'+s->vmu_wizard_slot/2u,s->vmu_wizard_slot%2u+1u);
        label(p,48,184,CYAN,line);
        label(p,48,211,working?MUTED:WHITE,working?"Reading and checking the source...":w->publication_started?
            "Import started; source capture is locked for this session.":"A Import all saves from this VMU");
        snprintf(line,sizeof(line),"%u source VMU%s added  |  %u source card%s captured",
            w->sources,w->sources==1?"":"s",w->cards,w->cards==1?"":"s");
        label(p,40,260,WHITE,line);
        if(w->cards && w->card<w->cards) {
            const struct kui_vmu_wizard_card *c=&w->summary[w->card];
            snprintf(line,sizeof(line),"Current card %u: %u saves  |  %u / 200 blocks",w->card+1u,c->files,c->blocks);
            label(p,40,286,CYAN,line);
        }
        label(p,40,318,MUTED,w->publication_started?"Resume the same import from its review, or start over.":"Each game gets its own 200-block virtual VMU.");
        label(p,40,340,MUTED,s->vmu_emulation_known && !s->vmu_emulation_enabled?
            "Virtual VMU is Off; enable it later in VMU tools.":w->publication_started?
            "Prepared cards and published choices stay intact.":"Source cards stay separate; duplicate versions are kept.");
        label(p,40,370,w->cards?WHITE:MUTED,w->cards?
            "START Match saves and set up each game's own card.":"Add a VMU to begin. Nothing is saved until the final review.");
    } else if(w->stage==KUI_VMU_WIZARD_SAVES) {
        words(p,40,138,608,CYAN,w->game_title,false);
        snprintf(line,sizeof(line),"Source %u / %u  |  %u picked saves  |  %u / 200 blocks",
            w->card+1u,w->cards,w->saves_selected_files,w->saves_selected_blocks);
        label(p,40,160,MUTED,line);
        panel(p,32,188,576,177,PANEL);
        unsigned count=w->saves_count<KUI_VMU_WIZARD_ROWS?w->saves_count:KUI_VMU_WIZARD_ROWS;
        for(unsigned i=0;i<count;i++) {
            const struct kui_vmu_wizard_save *row=&w->saves[i];unsigned y=193+i*21;
            if(i==s->vmu_wizard_selected) panel(p,40,y,544,21,SELECTED);
            snprintf(line,sizeof(line),"[%c] %s  %s",row->picked?'x':' ',row->name,row->short_description);
            words(p,48,y+1,518,i==s->vmu_wizard_selected?WHITE:MUTED,line,false);
            snprintf(line,sizeof(line),"%u blk",row->blocks);words(p,522,y+1,592,row->picked?GREEN:MUTED,line,false);
        }
        if(!count) label(p,48,206,MUTED,"No saves on this source card.");
        vmu_scrollbar(p,w->saves_page,s->vmu_wizard_selected,w->saves_total);
        if(s->vmu_wizard_selected<count) {
            const struct kui_vmu_wizard_save *row=&w->saves[s->vmu_wizard_selected];
            snprintf(line,sizeof(line),"%s%s",row->suggested?"Possible match: ":"",row->long_description[0]?
                row->long_description:row->short_description[0]?row->short_description:row->name);
            words(p,40,374,608,row->suggested?CYAN:MUTED,line,false);
        } else label(p,40,374,MUTED,"Pick related files together; START keeps these choices.");
    } else if(w->stage==KUI_VMU_WIZARD_GAMES_STAGE) {
        if(w->view_only) {
            snprintf(line,sizeof(line),"%u game%s in this library use this card",w->total,w->total==1?"":"s");
            label(p,40,138,CYAN,line);
        } else if(!w->existing_card) {
            snprintf(line,sizeof(line),"%u selected games  |  %u game cards  |  200 blocks each",w->selected_games,w->variants);
            label(p,40,138,CYAN,line);
        } else if(w->card<w->cards) {
            const struct kui_vmu_wizard_card *c=&w->summary[w->card];
            snprintf(line,sizeof(line),"Card %u / %u  |  %u saves  |  %u blocks  |  %u games selected",
                w->card+1u,w->cards,c->files,c->blocks,c->games);
            label(p,40,138,CYAN,line);
        }
        unsigned start=w->count?w->page*KUI_VMU_WIZARD_ROWS+1u:0;
        unsigned end=start?w->page*KUI_VMU_WIZARD_ROWS+w->count:0;
        snprintf(line,sizeof(line),"Games %u-%u of %u  |  %s",start,end,w->total,w->view_only?
            "Read-only":w->publication_started?
            "Choices locked while this import is saved":w->existing_card?
            "Other game assignments stay unchanged":"Choose imported saves, or start with a blank game card");
        label(p,40,160,MUTED,w->copy_on_commit && !w->view_only?
            "Creates a new shared card; the original is kept.":line);
        panel(p,32,188,576,177,PANEL);
        unsigned count=w->count<KUI_VMU_WIZARD_ROWS?w->count:KUI_VMU_WIZARD_ROWS;
        for(unsigned i=0;i<count;i++) {
            const struct kui_vmu_wizard_game *g=&w->games[i];unsigned y=193+i*21;
            if(i==s->vmu_wizard_selected) panel(p,40,y,544,21,SELECTED);
            if(w->view_only) snprintf(line,sizeof(line),"%s",g->title[0]?g->title:"Game title unavailable");
            else snprintf(line,sizeof(line),"[%c] %s",g->selected?'x':g->supported?' ':'-',g->title[0]?g->title:g->product);
            words(p,48,y+1,w->view_only?584u:493u,i==s->vmu_wizard_selected?WHITE:MUTED,line,false);
            if(!w->view_only) {
                if(g->selected && !w->existing_card) snprintf(line,sizeof(line),g->blank?"Blank":"%u card%s",g->variants,g->variants==1?"":"s");
                else snprintf(line,sizeof(line),"%s",g->selected?"Selected":g->supported?"":"No VMU");
                words(p,499,y+1,592,g->selected?GREEN:MUTED,line,false);
            }
        }
        if(!count) label(p,48,206,MUTED,working?"Reading the game library...":w->view_only?
            "No matching games were found in this library.":"No game products found in /Games.");
        vmu_scrollbar(p,w->page,s->vmu_wizard_selected,w->total);
        if(w->view_only) label(p,40,374,MUTED,"Read-only. Game assignments stay unchanged.");
        else if(s->vmu_wizard_selected<count) {
            const struct kui_vmu_wizard_game *g=&w->games[s->vmu_wizard_selected];
            snprintf(line,sizeof(line),"%s  |  %s",g->product,w->publication_started?
                "START Return to review and retry":g->supported?
                (w->existing_card?"A Select; compatibility checked at launch":"A Choose saves; X Blank / remove"):
                "This loader cannot use this virtual card");
            label(p,40,374,g->supported?MUTED:AMBER,line);
        }
    } else {
        snprintf(line,sizeof(line),"%u source VMU%s  |  %u card%s  |  %u %s game%s",
            w->sources,w->sources==1?"":"s",w->cards,w->cards==1?"":"s",
            w->selected_games,w->stage==KUI_VMU_WIZARD_DONE?"assigned":"selected",w->selected_games==1?"":"s");
        if(!w->existing_card) snprintf(line,sizeof(line),"%u sources | %u game cards | %u %s games | %u/%u",w->sources,w->variants,w->selected_games,
            w->stage==KUI_VMU_WIZARD_DONE?"assigned":"selected",w->page+1u,
            w->total?(w->total-1u)/KUI_VMU_WIZARD_ROWS+1u:1u);
        label(p,40,140,CYAN,line);
        panel(p,32,166,576,174,PANEL);
        if(!w->existing_card) for(unsigned i=0;i<w->count;i++) {
            const struct kui_vmu_wizard_game *g=&w->games[i];
            snprintf(line,sizeof(line),"%s",g->title);words(p,48,174+i*20,425,g->selected?WHITE:MUTED,line,false);
            if(g->selected) snprintf(line,sizeof(line),g->blank?"Blank: 200 free":"%u card%s, %u save%s",g->variants,g->variants==1?"":"s",g->saves,g->saves==1?"":"s");
            else snprintf(line,sizeof(line),"Unchanged");
            words(p,433,174+i*20,596,g->selected?CYAN:MUTED,line,false);
        }
        else for(unsigned i=0;i<w->cards && i<KUI_VMU_WIZARD_CARDS;i++) {
            const struct kui_vmu_wizard_card *c=&w->summary[i];
            if(w->stage==KUI_VMU_WIZARD_DONE && c->pair)
                snprintf(line,sizeof(line),"Card %u: %u saves, %u blocks, %u assigned game%s",i+1u,c->files,c->blocks,c->games,c->games==1?"":"s");
            else snprintf(line,sizeof(line),"Card %u: %u saves, %u blocks, %u selected game%s",i+1u,c->files,c->blocks,c->games,c->games==1?"":"s");
            label(p,48,174+i*20,c->games?WHITE:MUTED,line);
        }
        label(p,40,352,WHITE,w->stage==KUI_VMU_WIZARD_DONE?
            (!s->vmu_emulation_known?"Cards saved; check virtual VMU status in VMU tools.":
             !s->vmu_emulation_enabled?"Cards saved; virtual VMU remains Off.":
             "Card choices saved; compatibility checked at game launch."):w->publication_started?
            "Retry saving the same cards and game choices.":w->copy_on_commit?
            "Creates a new shared card; the original is kept.":
            w->existing_card?"Save game choices; the existing card stays intact.":
            "Game cards get only chosen saves; source copies are kept.");
        label(p,40,374,MUTED,w->stage==KUI_VMU_WIZARD_DONE?
            (s->vmu_emulation_known && s->vmu_emulation_enabled?
             (s->vmu_wizard_return==KUI_SHELL_VMU_CARD?"A Virtual VMUs   X Begin another import":"A VMU tools   X Begin another import"):
             "Enable virtual VMU in VMU tools to use these cards."):w->publication_started?
            "View the frozen choices with B, or retry with A.":
            !s->vmu_emulation_known?"Check virtual VMU status in VMU tools before launching.":
            !s->vmu_emulation_enabled?
            "Cards and game choices are kept while virtual VMU is Off.":
            "Compatibility is checked when each selected game launches.");
    }
    const char *notice=w->status.message[0]?w->status.message:working?"Working...":"";
    if(w->stage==KUI_VMU_WIZARD_DONE && w->status.complete && w->status.passed &&
       (!s->vmu_emulation_known || !s->vmu_emulation_enabled))
        notice="Cards and game assignments saved.";
    label(p,40,396,w->status.complete&&!w->status.passed?AMBER:CYAN,
        notice);
    if(s->confirm_vmu_wizard_reset) {
        box(p,32,166,576,218,NAVY);
        panel(p,48,174,544,177,PANEL);box(p,52,178,536,4,PINK);
        title(p,64,196,"Start a new import?");
        label(p,64,234,MUTED,w->publication_started?"Leave this import and begin a fresh session.":
            "Discard the saves and game choices staged in this session.");
        label(p,64,259,MUTED,"Previously created cards and physical VMUs stay intact.");
        label(p,64,309,WHITE,"A Start over   B Keep this session");
    }
}
static void vmu_actions(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"VMU actions");
    const struct kui_vmu_entry *entry=s->vmu_selected<s->vmu.count?&s->vmu.entries[s->vmu_selected]:NULL;
    char line[120];snprintf(line,sizeof(line),"Source VMU %c%u: %s",'A'+s->vmu_slot/2,
        s->vmu_slot%2+1,entry?entry->name:"No selected save");
    label(p,40,138,CYAN,line);
    bool virtual_target=s->vmu_copy_slot==8;
    for(unsigned i=0;i<6;i++) {
        unsigned y=168+i*25;panel(p,32,y,576,23,i==s->vmu_action_selected?SELECTED:PANEL);
        if(i==1 || i==3) {
            if(virtual_target) snprintf(line,sizeof(line),"%s: Virtual VMU (new card)",i==3?"Copy all saves":"Copy selected");
            else snprintf(line,sizeof(line),i==3?"Copy all saves to VMU %c%u":"Copy to VMU %c%u",
                'A'+s->vmu_copy_slot/2,s->vmu_copy_slot%2+1);
        } else snprintf(line,sizeof(line),"%s",i==5?"Back up selected save":i==4?"Virtual VMUs":i==2?"Back up card image":"Delete selected save");
        label(p,48,y+4,i==s->vmu_action_selected?WHITE:MUTED,line);
    }
    if(s->vmu_action_selected==3) {
        const struct kui_vmu_view *bulk=virtual_target?&s->vmu_collection_copy:&s->vmu_copy_all;
        snprintf(line,sizeof(line),"Total %u   Verified %u   Remaining %u",bulk->bulk_total,
            bulk->bulk_completed,bulk->bulk_remaining);
        label(p,40,320,CYAN,line);
        label(p,40,344,bulk->bulk_unverified?AMBER:MUTED,virtual_target?
            "All saves keep companion files together on a new card.":bulk->bulk_unverified?
            "Destination save unverified; inspect it before retry.":
            "Completed copies stay. A starts a fresh check before retry.");
        label(p,40,366,MUTED,virtual_target?"New backup card; assign games from Virtual VMUs.":
            "All names and space are checked; existing saves are refused.");
    } else if((s->vmu_action_selected==1 && virtual_target) || s->vmu_action_selected==4) {
        label(p,40,320,MUTED,"Virtual cards are stored on SD; browse and extract saves.");
        label(p,40,344,MUTED,s->vmu_action_selected==1?
            "Selected copy keeps this file only; companions are not inferred.":
            "A opens the managed virtual card collection.");
        label(p,40,366,MUTED,"Use Virtual VMUs > Assign games when ready.");
    } else {
        label(p,40,320,MUTED,"Card image saves all 128 KiB as a verified VMD on SD.");
        label(p,40,344,MUTED,"Delete / copy checks the save and asks before writing.");
        label(p,40,366,MUTED,"Copy refuses existing names on the destination VMU.");
    }
    const struct kui_app_status *status=v->app_status?v->app_status:&s->vmu.status;
    if(virtual_target && (s->vmu_action_selected==1 || s->vmu_action_selected==3)) {
        if(!v->app_status) status=&s->vmu_collection_copy.status;
        label(p,40,386,status->complete&&!status->passed?AMBER:CYAN,s->vmu_collection_copy_stored?
            "Backup card saved; assign games from Virtual VMUs.":status->message);
    } else label(p,40,386,status->complete&&!status->passed?AMBER:CYAN,status->message);
}
/* Keep complete operation messages visible without crowding the dashboard.
 * Three short lines fit above the footer, including a full 127-byte error. */
static unsigned recovery_outcome(struct paint *p,const char *message,uint16_t color) {
    unsigned lines=0;
    while(message && *message && lines<3) {
        while(*message==' ')++message;
        char text[160];snprintf(text,sizeof(text),"%s",message);
        size_t count=strlen(text),take=count;
        while(take && word_width(p,text,false)>568u)text[--take]=0;
        if(take<count) {
            size_t space=take;while(space && text[space-1]!=' ')--space;
            if(space)take=space-1;
            text[take]=0;
        }
        if(!take)break;
        label(p,40,370+lines*15,color,text);++lines;message+=take;
    }
    return lines;
}
static void salvage(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_app_status *status=v->app_status;
    bool diagnostic=v->salvage_diagnostic==KUI_SALVAGE_DIAGNOSTIC_SAVED_READS ||
        v->salvage_diagnostic==KUI_SALVAGE_DIAGNOSTIC_G1_MODE ||
        v->salvage_diagnostic==KUI_SALVAGE_DIAGNOSTIC_REVALIDATE;
    bool trial_result=(v->salvage_control_trial || v->salvage_speed_probe || diagnostic) &&
        s->salvage_trial_result && status && status->complete;
    bool showing=v->busy || trial_result;
    title(p,40,108,showing && v->salvage_diagnostic==KUI_SALVAGE_DIAGNOSTIC_SAVED_READS?
        "Saved repair read comparison":showing && v->salvage_diagnostic==KUI_SALVAGE_DIAGNOSTIC_G1_MODE?
        "Direct G1 drive settings":showing && v->salvage_diagnostic==KUI_SALVAGE_DIAGNOSTIC_REVALIDATE?
        "Saved salvage revalidation":showing && v->salvage_speed_probe?
        "Drive speed support probe":showing?"Disc recovery":"Recover a damaged disc");
    if(showing && v->salvage_diagnostic==KUI_SALVAGE_DIAGNOSTIC_SAVED_READS) {
        label(p,40,138,MUTED,"Read-only SD test; saved repairs remain blocked");
    } else if(showing && v->salvage_diagnostic==KUI_SALVAGE_DIAGNOSTIC_G1_MODE) {
        label(p,40,138,MUTED,"Bypass BIOS; verify mode requests and restoration");
    } else if(showing && v->salvage_diagnostic==KUI_SALVAGE_DIAGNOSTIC_REVALIDATE) {
        label(p,40,138,MUTED,"Full saved-data check; recovery starts separately");
    } else if(showing && v->salvage_speed_probe) {
        label(p,40,138,MUTED,"Read-only disc test; saved recovery jobs unchanged");
    } else if((v->busy || trial_result) && v->salvage_control_trial) {
        label(p,40,138,MUTED,"Bounded trial: up to 8 sectors; fixed comparison profiles");
    } else if(v->busy && s->salvage_details && (s->salvage_drive_options.speed || s->salvage_drive_options.retries)) {
        char requested[96];
        snprintf(requested,sizeof(requested),"Requested speed: %s; drive retries: %s",
            kui_drive_speed_name(s->salvage_drive_options.speed),
            kui_drive_retry_name(s->salvage_drive_options.retries));
        label(p,40,138,MUTED,requested);
    } else if(showing) label(p,40,138,MUTED,v->disc_title && v->disc_title[0]?v->disc_title:
        v->inserted_title && v->inserted_title[0]?v->inserted_title:"Recovery keeps checked data and retries missing sectors.");
    if((v->busy || trial_result) && status) {
        panel(p,32,160,576,240,PANEL);
        if(diagnostic || v->salvage_speed_probe || v->salvage_control_trial || s->salvage_details) {
            app_status(p,status,v->busy,170);
            unsigned count=status->line_count<KUI_APP_LINES?status->line_count:KUI_APP_LINES;
            bool compact=v->salvage_speed_probe || diagnostic;
            unsigned limit=compact?9u:7u,spacing=compact?19u:23u;
            for(unsigned i=0;i<count && i<limit;i++) label(p,40,224+i*spacing,MUTED,status->lines[i]);
        } else {
            label(p,40,170,MUTED,"Collection / saved-data check progress");
            app_status(p,status,v->busy,194);
            const struct kui_salvage_dashboard *saved=&s->salvage_dashboard;
            char text[96];
            if(saved->known && saved->counts_known) {
                snprintf(text,sizeof(text),"Repairs saved: %lu",(unsigned long)saved->recovered);
                label(p,40,258,WHITE,text);
                snprintf(text,sizeof(text),"Sectors still unresolved: %lu",(unsigned long)saved->remaining);
                label(p,40,288,WHITE,text);
            } else {
                label(p,40,258,WHITE,"Checking the matching disc and saved recovery job.");
                label(p,40,288,MUTED,"Repair counts appear after saved progress is checked.");
            }
            label(p,40,336,MUTED,"Copied data and checked repairs are counted separately.");
            label(p,40,364,CYAN,"B stops safely and keeps saved progress.");
            label(p,40,384,MUTED,"Y shows sector addresses and detailed read results.");
        }
        return;
    }
    const struct kui_salvage_dashboard *saved=&s->salvage_dashboard;
    const char *disc=saved->known && saved->title[0]?saved->title:
        v->inserted_title && v->inserted_title[0]?v->inserted_title:"Insert the disc you want to recover";
    label(p,40,140,CYAN,disc);
    panel(p,32,160,576,66,PANEL);
    char summary[112];
    if(saved->known && saved->counts_known) {
        snprintf(summary,sizeof(summary),"Repairs saved: %lu     Sectors remaining: %lu",
            (unsigned long)saved->recovered,(unsigned long)saved->remaining);
        label(p,48,170,WHITE,summary);
    } else label(p,48,170,WHITE,saved->known?"Saved job found; its data is checked when work starts.":
        "Saved progress is checked when you continue.");
    label(p,48,198,v->drive_reset_required || (saved->known && saved->blocked) || (status && status->errors)?AMBER:MUTED,
        v->drive_reset_required?"The drive needs a restart before recovery can continue.":
        status && status->errors?"The last recovery attempt ended with an error; see below.":
        saved->known && saved->blocked?"Check saved progress before continuing recovery.":
        status && status->stopped?"Stopped safely; saved progress is kept.":
        saved->known?"Last checked progress; the disc and job are checked again on start.":
        "Continue finds the matching job and keeps its saved progress.");
    const char *names[]={saved->known && saved->blocked?"Check saved progress":"Continue recovery",
        "Start a new recovery","Recovery options","Diagnostics"};
    for(unsigned i=0;i<KUI_SHELL_RECOVERY_COUNT;i++) {
        unsigned y=240+i*32;panel(p,32,y,576,27,i==s->salvage_selected?SELECTED:PANEL);
        label(p,48,y+6,i==s->salvage_selected?WHITE:MUTED,names[i]);
    }
    static const char *const help[][2]={
        {"Continue collecting data, then retry the remaining sectors.","Your saved repairs and existing recovery job are kept."},
        {"Start a separate recovery job for the inserted disc.","Existing recovery jobs are kept; confirmation follows."},
        {"Set drive retries, recovery passes and final verification.","Advanced recovery actions are here too."},
        {"Check saved progress or investigate drive and SD problems.","Tests explain their scope before they start."}
    };
    unsigned selected=s->salvage_selected<KUI_SHELL_RECOVERY_COUNT?s->salvage_selected:0;
    if(v->drive_reset_required) {
        recovery_outcome(p,"Restart the console before continuing recovery. Your saved progress is kept.",AMBER);
    } else if(status && (status->complete || status->stopped || status->errors) && status->message[0]) {
        unsigned lines=recovery_outcome(p,status->message,status->stopped?MUTED:status->errors?AMBER:status->passed?CYAN:AMBER);
        if(status->passed && !status->errors && !status->stopped && status->line_count>4 && lines<3)
            label(p,40,370+lines*15,CYAN,status->lines[4]);
    } else if(selected==KUI_SHELL_RECOVERY_CONTINUE && saved->known && saved->blocked) {
        label(p,40,380,CYAN,"Saved progress needs verification before recovery.");
        label(p,40,398,MUTED,"Review the estimated duration before starting the check.");
    } else {
        label(p,40,380,CYAN,help[selected][0]);label(p,40,398,MUTED,help[selected][1]);
    }

}
static void salvage_options(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Recovery options");
    label(p,40,138,MUTED,"Settings apply when recovery starts; changing them starts no reads.");
    const char *names[]={"Firmware retries","Recovery pass limit","Verify completed files","Read speed",
        "Skip next unread range","Return to recovery"};
    for(unsigned i=0;i<KUI_SHELL_RECOVERY_OPTIONS_COUNT;++i) {
        unsigned y=164+i*27;panel(p,32,y,576,24,i==s->salvage_options_selected?SELECTED:PANEL);
        label(p,48,y+4,i==s->salvage_options_selected?WHITE:MUTED,names[i]);
        if(i==KUI_SHELL_RECOVERY_RETRIES)label(p,460,y+4,CYAN,kui_drive_retry_name(s->salvage_drive_options.retries));
        if(i==KUI_SHELL_RECOVERY_SPEED)label(p,460,y+4,CYAN,kui_drive_speed_name(s->salvage_drive_options.speed));
        if(i==KUI_SHELL_RECOVERY_VERIFY)label(p,548,y+4,CYAN,s->salvage_verify_files?"On":"Off");
        if(i==KUI_SHELL_RECOVERY_PASSES) {
            char value[24];if(s->salvage_passes)snprintf(value,sizeof(value),"%u",s->salvage_passes);
            else snprintf(value,sizeof(value),"Until stopped");
            label(p,460,y+4,CYAN,value);
        }
    }
    static const char *const help[][3]={
        {"The drive's own decoding retries for each read command.","Recovery passes retry the remaining sectors separately.","Requested settings are checked when recovery starts."},
        {"How many sweeps to make through unresolved sectors.","Until stopped keeps trying until complete or you press B.","Each sweep retains repairs that pass their checks."},
        {"After all sectors recover, read back every saved track.","This takes extra time and checks the stored files.","Catalogue comparisons remain separate from saved-byte checks."},
        {"Optional: some drives ignore requested speed changes.","A requested speed is not a measured physical speed.","Leave this unchanged unless testing your drive."},
        {"Advanced: continue past up to 32 uncommitted sectors.","They remain unresolved and need later disc recovery.","Requires confirmation; restart first if the drive requires it."},
        {"Return with these choices ready for recovery.","Choices reset when the console restarts.","Drive settings are restored when recovery stops safely."}
    };
    unsigned selected=s->salvage_options_selected<KUI_SHELL_RECOVERY_OPTIONS_COUNT?s->salvage_options_selected:0;
    for(unsigned i=0;i<3;++i)label(p,40,342+i*24,i?MUTED:CYAN,help[selected][i]);
}
static void salvage_drive(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Recovery diagnostics");
    label(p,40,138,MUTED,"Saved-data checks and optional drive tests.");
    const char *names[]={"Full saved progress check","Saved read comparison","Bounded recovery trial",
        "Speed support test","Direct G1 probe","Return to recovery"};
    for(unsigned i=0;i<KUI_SHELL_RECOVERY_DIAGNOSTICS_COUNT;++i) {
        unsigned y=164+i*27;panel(p,32,y,576,24,i==s->salvage_drive_selected?SELECTED:PANEL);
        label(p,48,y+4,i==s->salvage_drive_selected?WHITE:MUTED,names[i]);
    }
    static const char *const help[][3]={
        {"Check the journal, repair records and every saved track byte.","A full pass can unblock consistent saved progress.","May take 40-50 minutes for a GD-ROM; confirmation follows."},
        {"Compare saved repair reads using four SD transfer profiles.","Saved job files are read-only; this cannot unblock recovery.","Send the saved diagnostic report when it finishes."},
        {"Compare recovery settings on up to 8 unresolved sectors.","Checked repairs are saved; active reads finish before Stop.","Fixed profiles and a five-minute soft limit."},
        {"Test speed settings through the BIOS and restore them.","No saved job is needed or changed; no sectors are repaired.","Read times alone do not establish physical drive speed."},
        {"Test mode commands directly over G1, bypassing the BIOS.","Checks accepted settings and original restoration.","No sector repairs or saved recovery job changes."},
        {"Return to the recovery dashboard.","Starting recovery remains a separate action.","Keep reports when investigating an error."}
    };
    unsigned selected=s->salvage_drive_selected<KUI_SHELL_RECOVERY_DIAGNOSTICS_COUNT?s->salvage_drive_selected:0;
    for(unsigned i=0;i<3;++i)label(p,40,342+i*24,i?MUTED:CYAN,help[selected][i]);
}

static void system_tools(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"System tools");
    words(p,40,138,608,MUTED,s->system_compare_path[0]?s->system_compare_path:
        "Inspect hardware, save backups or compare with SD images.",false);
    const char *names[]={"Inspect system hardware","Back up settings flash","Back up visible BIOS bank",
        "Restart console","Compare settings flash with SD image","Compare visible BIOS with SD image"};
    for(unsigned i=0;i<6;i++) {
        unsigned y=160+i*27;panel(p,32,y,576,24,i==s->tools_selected?SELECTED:PANEL);
        label(p,48,y+3,i==s->tools_selected?WHITE:MUTED,names[i]);
    }
    const struct kui_app_status *status=v->app_status;
    app_status(p,status,v->busy,330);
    if(status && status->line_count) {
        unsigned n=status->line_count<KUI_APP_LINES?status->line_count:KUI_APP_LINES;
        for(unsigned i=n>2?n-2:0;i<n;i++) label(p,40,374+(i-(n>2?n-2:0))*18,MUTED,status->lines[i]);
    }
}
static void vmu_restore(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"VMU backups on SD");
    char line[112],range[48];vmu_range(range,sizeof(range),"Backups",s->backup_page,s->backups.count,s->backups.total);
    snprintf(line,sizeof(line),"Target VMU %c%u   %s",'A'+s->vmu_slot/2,s->vmu_slot%2+1,range);
    label(p,40,138,CYAN,line);
    label(p,40,160,v->busy?MUTED:WHITE,"A Open / check   X Refresh   Y Import save");
    panel(p,32,190,576,178,PANEL);
    unsigned count=s->backups.count<KUI_VMU_ROWS?s->backups.count:KUI_VMU_ROWS;
    if(!count) label(p,48,206,MUTED,v->busy?"Reading backups...":"No compatible backups on this page.");
    for(unsigned i=0;i<count;i++) {
        unsigned y=196+i*21;
        const struct kui_vmu_backup_entry *e=&s->backups.entries[i];
        if(i==s->backup_selected) panel(p,40,y,544,21,SELECTED);
        words(p,52,y+1,340,i==s->backup_selected?WHITE:MUTED,e->name,false);
        words(p,352,y+1,584,MUTED,e->folder,false);
    }
    vmu_scrollbar(p,s->backup_page,s->backup_selected,s->backups.total);
    const struct kui_app_status *status=v->app_status?v->app_status:&s->backups.status;
    label(p,40,378,status->complete&&!status->passed?AMBER:CYAN,status->message);
    label(p,40,396,MUTED,"VMD: browse / extract. Existing names are refused on restore.");
}
static void vmu_collection(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Virtual VMUs");
    char line[112],range[48];
    vmu_range(range,sizeof(range),"Cards",s->vmu_collection_page,s->vmu_collection.count,s->vmu_collection.total);
    snprintf(line,sizeof(line),"%s   Select a card to see games and saves",range);
    label(p,40,138,CYAN,line);
    label(p,40,160,v->busy?MUTED:WHITE,"A Open card   X Card actions   Y Refresh");
    panel(p,32,190,576,178,PANEL);
    unsigned count=s->vmu_collection.count<KUI_VMU_ROWS?s->vmu_collection.count:KUI_VMU_ROWS;
    if(!count) label(p,48,206,MUTED,v->busy?"Reading virtual cards...":"No virtual VMUs yet. Import saves to begin.");
    for(unsigned i=0;i<count;i++) {
        unsigned y=196+i*21;
        const struct kui_vmu_backup_entry *e=&s->vmu_collection.entries[i];
        if(i==s->vmu_collection_selected) panel(p,40,y,544,21,SELECTED);
        char card_name[160];
        if(e->custom_label && e->titles[0]) snprintf(card_name,sizeof(card_name),"%s - %.100s",e->label,e->titles);
        else snprintf(card_name,sizeof(card_name),"%s",e->label[0]?e->label:e->name);
        words(p,52,y+1,424,i==s->vmu_collection_selected?WHITE:MUTED,card_name,false);
        if(!e->readable) snprintf(line,sizeof(line),"Unavailable");
        else snprintf(line,sizeof(line),"%s | %u free",e->folder,e->free_blocks);
        words(p,432,y+1,584,e->readable?MUTED:AMBER,line,false);
    }
    vmu_scrollbar(p,s->vmu_collection_page,s->vmu_collection_selected,s->vmu_collection.total);
    const struct kui_app_status *status=v->app_status?v->app_status:&s->vmu_collection.status;
    if(count && s->vmu_collection_selected<count) {
        const struct kui_vmu_backup_entry *e=&s->vmu_collection.entries[s->vmu_collection_selected];
        if(!e->readable) {label(p,40,378,AMBER,e->games);label(p,40,396,MUTED,"X Card actions / Details shows the storage path.");}
        else {
            char context[224];
            snprintf(context,sizeof(context),e->users?"Used by: %s":"%s",e->users?e->titles:
                e->active?"No game assignments":"Backup snapshot; assign games to create an active copy");
            label(p,40,378,CYAN,context);
            if(e->contents[0]) snprintf(context,sizeof(context),"Contains: %s",e->contents);
            else snprintf(context,sizeof(context),"%s",e->free_blocks==200u?"Contains: Empty card":"Contains: Save files; open the card for filenames");
            label(p,40,396,MUTED,context);
        }
    } else {
        label(p,40,378,status->complete&&!status->passed?AMBER:CYAN,status->message);
        label(p,40,396,MUTED,"Open a card to view saves, rename it or choose games.");
    }
}
static void vmu_card(struct paint *p,const struct kui_shell *s) {
    const struct kui_vmu_backup_entry *card=&s->vmu_card;
    title(p,40,108,s->vmu_card_details?"Virtual VMU / Details":"Virtual VMU");
    label(p,40,140,CYAN,card->label[0]?card->label:card->name);
    char line[224];
    if(s->vmu_card_details) {
        label(p,40,178,MUTED,"Storage path");
        path_words(p,40,200,608,WHITE,card->path);
        snprintf(line,sizeof(line),"Storage ID: %s   Type: %s",card->folder,
            card->active?"Active game card":"Saved backup card");
        label(p,40,232,MUTED,line);
        char identity[33];for(unsigned i=0;i<16;i++) snprintf(identity+2*i,3,"%02x",card->identity[i]);
        snprintf(line,sizeof(line),"Card identity: %s",identity);label(p,40,256,MUTED,line);
        label(p,40,286,MUTED,"Assigned game product numbers");
        for(unsigned i=0;i<3;i++) {
            size_t offset=i*64u,n=strlen(card->games);
            if(offset>=n) break;
            snprintf(line,sizeof(line),"%.64s",card->games+offset);label(p,40,310+i*20,MUTED,line);
        }
        label(p,40,390,MUTED,"These identifiers describe the card; browsing never changes saves.");
        return;
    }
    if(!card->readable) {
        label(p,40,180,AMBER,"This card is unavailable.");
        label(p,40,208,MUTED,card->games[0]?card->games:"Refresh Virtual VMUs to check it again.");
    } else {
        snprintf(line,sizeof(line),"%u free blocks   |   %s",card->free_blocks,
            card->active?"Game card":"Backup card");
        label(p,40,172,WHITE,line);
        if(card->users) snprintf(line,sizeof(line),"Used by %u game%s",card->users,card->users==1?"":"s");
        else snprintf(line,sizeof(line),"%s",card->active?"No game assignments yet.":
            "Assign games to create a shared card from this backup.");
        label(p,40,198,MUTED,line);
    }
    const char *names[]={"View saves","Games using this card","Rename card","Assign games","Details"};
    for(unsigned i=0;i<KUI_SHELL_VMU_CARD_ACTIONS;i++) {
        unsigned y=240+i*28;bool available=card->readable || i==KUI_SHELL_VMU_CARD_DETAILS;
        panel(p,32,y,576,25,s->vmu_card_selected==i?SELECTED:PANEL);
        label(p,48,y+5,available?(s->vmu_card_selected==i?WHITE:CYAN):MUTED,names[i]);
    }
    label(p,40,390,MUTED,s->vmu_emulation_known && !s->vmu_emulation_enabled?
        "Virtual VMU is Off; browsing, renaming and imports remain available.":
        "Game compatibility is checked when the game launches.");
}
static void vmu_image(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,s->vmu_image_collection?"Virtual VMU / Saves":"VMU backup / Saves");
    char range[48];vmu_range(range,sizeof(range),"Saves",s->vmu_image_page,s->vmu_image.count,s->vmu_image.total);
    words(p,40,138,258,CYAN,range,false);
    words(p,270,138,608,MUTED,s->vmu_image_collection?
        (s->vmu_card.label[0]?s->vmu_card.label:s->vmu_card.name):s->vmu_image_path,false);
    label(p,40,160,v->busy?MUTED:WHITE,"A Refresh   X Extract selected   Y Extract all");
    panel(p,32,190,576,178,PANEL);
    const struct kui_vmu_view *image=&s->vmu_image;
    unsigned count=image->count<KUI_VMU_ROWS?image->count:KUI_VMU_ROWS;
    if(!count) label(p,48,206,MUTED,v->busy?"Reading card image...":
        image->present?"No saves on this page.":"Image not loaded. Press A to check again.");
    vmu_save_rows(p,image,s->vmu_image_page,s->vmu_image_selected);
    const struct kui_app_status *status=v->app_status?v->app_status:&image->status;
    label(p,40,396,status->complete&&!status->passed?AMBER:CYAN,status->message[0]?status->message:
        "Extracts verified SD backups only.");
}
static void vmu_import(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Import save to SD");
    words(p,40,138,608,CYAN,s->vmu_import_path,false);
    const struct kui_vmu_view *imported=&s->vmu_import;
    char line[120];
    if(imported->count) snprintf(line,sizeof(line),"%s   %lu bytes / %lu blocks",imported->entries[0].name,
        (unsigned long)imported->entries[0].bytes,(unsigned long)((imported->entries[0].bytes+511u)/512u));
    else snprintf(line,sizeof(line),"Checking the external save and original metadata...");
    label(p,40,160,MUTED,line);
    panel(p,32,184,576,184,PANEL);
    if(s->vmu_import_staged) {
        label(p,48,198,CYAN,"Verified SD backup:");
        words(p,48,224,592,WHITE,imported->backup_path,false);
        label(p,48,256,MUTED,"No VMU was written.");
        label(p,48,282,MUTED,"Restore separately from the SD backups list.");
        label(p,48,316,MUTED,"B opens the refreshed backup list.");
    } else {
        unsigned count=imported->status.line_count<KUI_APP_LINES?imported->status.line_count:KUI_APP_LINES;
        for(unsigned i=0;i<count && i<8;i++) label(p,48,194+i*21,MUTED,imported->status.lines[i]);
        if(!count) label(p,48,202,MUTED,v->busy?"Reading external save...":"Check the save before importing.");
    }
    const struct kui_app_status *status=v->app_status?v->app_status:&imported->status;
    label(p,40,378,status->errors || (status->complete&&!status->passed)?AMBER:CYAN,status->message);
    label(p,40,396,MUTED,"Creates an SD backup only; restore separately from backups.");
}
static void crc_scan(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,s->scan_return==KUI_SHELL_DISC_TOOLS && !s->recovery_operation?"Verify a saved dump":s->recovery_operation==4u?"Start a new repair copy":s->recovery_operation==3u?"Repair / resume in place":s->recovery_operation==1u?"Repair / resume a separate copy":s->recovery_operation==2u?
        "Suspect-sector reread evidence":"Advanced CRC scan");
    words(p,40,138,608,CYAN,s->browse_path,false);
    label(p,40,160,MUTED,s->recovery_operation==4u?"New transaction; source and previous repair jobs retained.":s->recovery_operation==3u?"Targeted original-track changes; durable backups retained.":s->recovery_operation==1u?"Matching disc required; original dump files are read-only.":
        s->recovery_operation==2u?"Repeat saved-sector reads; no optical reads or dump changes.":
        "Mode1 EDC/parity + saved hashes + catalogue; no disc reads.");
    bool repair_controls=(s->recovery_operation==1u || s->recovery_operation==3u || s->recovery_operation==4u) && !v->busy && !s->confirm_repair_in_place;
    panel(p,32,repair_controls?216:190,576,repair_controls?198:214,PANEL);
    const struct kui_app_status *status=v->app_status;
    app_status(p,status,v->busy,repair_controls?226:200);
    if(status) {
        unsigned count=status->line_count<KUI_APP_LINES?status->line_count:KUI_APP_LINES;
        unsigned first=count>7?count-7:0;
        for(unsigned i=first;i<count;i++) label(p,40,(repair_controls?276:250)+(i-first)*20,MUTED,status->lines[i]);
    } else label(p,40,repair_controls?230:204,MUTED,repair_controls?
        "A begins repair; B returns to Advanced.":"Ready to scan this dump; missing hashes are not a match.");
    if(repair_controls) {
        char passes[96];
        if(s->salvage_passes)snprintf(passes,sizeof(passes),"Recovery passes: %u   LEFT/RIGHT changes",s->salvage_passes);
        else snprintf(passes,sizeof(passes),"Recovery passes: Until stopped   LEFT/RIGHT changes");
        label(p,40,180,CYAN,passes);
        label(p,40,198,s->recovery_operation!=3u && s->repair_saved_identity?AMBER:MUTED,s->recovery_operation==3u?
            "Original identity required; use Copy for saved anchors":s->repair_saved_identity?
            "Identity fallback: saved anchors enabled   X disables":
            "Identity: original samples   X allows saved anchors");
    }
}
static void app_confirmation(struct paint *p,const struct kui_shell *s) {
    box(p,32,154,576,262,NAVY);panel(p,48,160,544,228,PANEL);box(p,52,164,536,4,PINK);
    if(s->confirm_repair_in_place) {
        label(p,72,188,WHITE,"REPAIR THIS DUMP IN PLACE?");
        path_words(p,72,220,560,CYAN,s->browse_path);
        label(p,72,250,MUTED,"Changes only recorded damaged sectors in this dump.");
        label(p,72,276,MUTED,"Original sectors and repair evidence are retained.");
        label(p,72,302,AMBER,"B stops and keeps checked progress for Resume.");
        label(p,72,334,CYAN,"A Repair in place");
    } else if(s->confirm_salvage_check) {
        label(p,72,188,WHITE,"CHECK ALL SAVED RECOVERY DATA?");
        label(p,72,222,MUTED,"Checks the journal, repair records and saved tracks.");
        label(p,72,248,MUTED,"Every saved track byte is read twice and compared.");
        label(p,72,276,AMBER,"Allow roughly 40-50 minutes for a full GD-ROM.");
        label(p,72,302,MUTED,"B stops safely. An incomplete check keeps recovery blocked.");
        label(p,72,328,MUTED,"After a pass, continue recovery as a separate action.");
        label(p,72,356,CYAN,"A Start full check");
    } else if(s->confirm_salvage) {
        char line[96];
        if(s->confirm_salvage_defer) {
            label(p,72,188,WHITE,"RESUME PAST THE NEXT RANGE?");
            label(p,72,222,MUTED,"Keeps the committed prefix of the latest matching job.");
            label(p,72,248,MUTED,"Marks its next range (up to 32 uncommitted sectors)");
            label(p,72,274,MUTED,"as unresolved zeros, then continues collection.");
            label(p,72,300,AMBER,"Missing sectors need later optical repair.");
            label(p,72,328,MUTED,"If drive reset is required, restart the console first.");
            label(p,72,356,CYAN,"A Resume past range");
        } else {
            label(p,72,188,WHITE,"START A NEW DISC RECOVERY?");
            label(p,72,222,MUTED,"Collects readable data and keeps existing recovery jobs.");
            label(p,72,248,MUTED,"Records missing sectors so they can be retried later.");
            if(s->salvage_passes) snprintf(line,sizeof(line),"Then retries holes for up to %u recovery passes.",s->salvage_passes);
            else snprintf(line,sizeof(line),"Then retries holes until resolved or you press B.");
            label(p,72,276,CYAN,line);
            label(p,72,302,AMBER,"Unresolved holes stay incomplete; B stops safely.");
            label(p,72,328,MUTED,s->salvage_verify_files?
                "After recovery: full readback and catalogue check.":
                "After recovery: catalogue check; saved readback off.");
            label(p,72,356,CYAN,"A Start recovery");
        }
    } else if(s->confirm_wifi_update) {
        char line[96],bytes[16];
        const struct kui_wifi_firmware *f=&s->wifi.firmware;
        kui_files_size_text(bytes,f->bytes);
        label(p,72,188,WHITE,"UPDATE WI-FI FIRMWARE?");
        snprintf(line,sizeof(line),"%s firmware %.32s (%s)",kui_wifi_chip_text(f->chip),f->version,bytes);
        label(p,72,218,CYAN,line);
        words(p,72,244,568,MUTED,f->path,false);
        label(p,72,270,MUTED,"The checked image will be installed on the Wi-Fi board.");
        label(p,72,294,MUTED,"Keep the console powered on while it finishes.");
        label(p,72,318,MUTED,"Saved Wi-Fi settings are kept. The board will restart.");
        label(p,72,350,CYAN,"A Update firmware");
    } else if(s->confirm_wifi_forget) {
        char line[96];
        label(p,72,188,WHITE,"FORGET THIS WI-FI NETWORK?");
        snprintf(line,sizeof(line),"%.32s",s->wifi.wifi.ssid[0]?s->wifi.wifi.ssid:"The saved network");
        label(p,72,222,CYAN,line);
        label(p,72,248,MUTED,"The board disconnects and drops its saved password.");
        label(p,72,276,MUTED,"Wi-Fi stays off until you join a network again.");
        label(p,72,334,CYAN,"A Forget");
    } else if(s->confirm_music_clear) {
        label(p,72,188,WHITE,"CLEAR CACHED MUSIC?");
        label(p,72,226,MUTED,"Playback stops and loaded audio is released from RAM.");
        label(p,72,252,MUTED,"Your music files remain on the SD card.");
        label(p,72,278,MUTED,"Choosing a song loads it again when storage is free.");
        label(p,72,334,CYAN,"A Clear cache");
    } else if(s->confirm_restart) {
        label(p,72,188,WHITE,"RESTART THE CONSOLE?");
        label(p,72,226,MUTED,"Closes K-UI and returns to the console BIOS.");
        label(p,72,252,MUTED,"Save any preference changes before restarting.");
        label(p,72,278,MUTED,"The inserted boot disc can start K-UI again.");
        label(p,72,334,CYAN,"A Restart");
    } else if(s->confirm_vmu_delete || s->confirm_vmu_copy) {
        bool copy=s->confirm_vmu_copy;
        const struct kui_vmu_entry *e=s->vmu_selected<s->vmu.count?&s->vmu.entries[s->vmu_selected]:NULL;
        bool virtual_target=copy && s->vmu_copy_slot==8;
        if(virtual_target && s->vmu_collection_copy.count) e=&s->vmu_collection_copy.entries[0];
        label(p,72,188,WHITE,virtual_target?"COPY THIS SAVE TO A VIRTUAL VMU?":
            copy?"COPY THIS SAVE TO ANOTHER VMU?":"DELETE THIS SAVE FROM THE VMU?");
        char line[112];snprintf(line,sizeof(line),"%s from VMU %c%u",e?e->name:"Selected save",
            'A'+s->vmu_slot/2,s->vmu_slot%2+1);label(p,72,222,CYAN,line);
        if(virtual_target) snprintf(line,sizeof(line),"Destination Virtual VMU (new card).");
        else if(copy) snprintf(line,sizeof(line),"Destination VMU %c%u; existing names are refused.",
            'A'+s->vmu_copy_slot/2,s->vmu_copy_slot%2+1);
        else snprintf(line,sizeof(line),"The selected VMU entry will be removed after backup.");
        label(p,72,248,MUTED,line);
        label(p,72,276,MUTED,virtual_target?"Only this file is copied; assign games from Virtual VMUs.":
            "A verified SD backup is required before any write.");
        label(p,72,302,MUTED,virtual_target?"Keep the source VMU and SD connected.":copy?"Keep both VMUs connected until the result appears.":
            "Keep the VMU connected until the result appears.");
        label(p,72,334,CYAN,copy?"A Copy save":"A Delete save");
    } else if(s->confirm_vmu_copy_all) {
        bool virtual_target=s->vmu_copy_slot==8;
        label(p,72,188,WHITE,virtual_target?"COPY ALL SAVES TO A VIRTUAL VMU?":"COPY ALL SAVES TO ANOTHER VMU?");
        char line[112];
        if(virtual_target) snprintf(line,sizeof(line),"%u saves from VMU %c%u to Virtual VMU (new card)",
            s->vmu_collection_copy.bulk_total,'A'+s->vmu_slot/2,s->vmu_slot%2+1);
        else snprintf(line,sizeof(line),"%u saves from VMU %c%u to VMU %c%u",s->vmu_copy_all.bulk_total,
                'A'+s->vmu_slot/2,s->vmu_slot%2+1,'A'+s->vmu_copy_slot/2,s->vmu_copy_slot%2+1);
        label(p,72,222,CYAN,line);
        label(p,72,248,MUTED,virtual_target?"All saves and companion files stay together.":"Existing destination names are refused.");
        label(p,72,276,MUTED,virtual_target?"New backup card; assign games from Virtual VMUs.":
            "Each save needs a verified SD backup before writing.");
        label(p,72,302,MUTED,virtual_target?"Keep the source VMU and SD connected.":"Keep both VMUs connected until the result appears.");
        label(p,72,334,CYAN,"A Copy all saves");
    } else if(s->confirm_defaults) {
        label(p,72,188,WHITE,"USE DEFAULT SYSTEM PREFERENCES?");
        label(p,72,222,MUTED,"This replaces your draft with the default values.");
        label(p,72,248,MUTED,"Press A Save afterwards to apply and store them.");
        label(p,72,274,MUTED,"The console clock and ripper settings stay intact.");
        label(p,72,334,CYAN,"A Use defaults");
    } else if(s->confirm_clock) {
        const struct kui_datetime *d=&s->clock_draft;
        char line[100];snprintf(line,sizeof(line),"%04u-%02u-%02u  %02u:%02u:%02u",
            d->year,d->month,d->day,d->hour,d->minute,d->second);
        label(p,72,188,WHITE,"SET THE CONSOLE CLOCK?");
        label(p,72,222,CYAN,line);
        label(p,72,258,MUTED,"This writes the console's local hardware clock.");
        label(p,72,284,MUTED,"Previously created file dates are unchanged.");
        label(p,72,334,CYAN,"A Set clock");
    } else {
        char line[100];snprintf(line,sizeof(line),"Restore %s to VMU %c%u?",s->restore_name,
            'A'+s->vmu_slot/2,s->vmu_slot%2+1);
        label(p,72,188,WHITE,"WRITE THIS SAVE TO THE VMU?");
        words(p,72,220,568,CYAN,line,false);
        snprintf(line,sizeof(line),"%lu bytes / %lu blocks checked from SD",
            (unsigned long)s->restore_bytes,(unsigned long)(((uint64_t)s->restore_bytes+511u)/512u));
        label(p,72,248,MUTED,line);
        label(p,72,276,MUTED,"Keep the VMU inserted until verification finishes.");
        label(p,72,302,MUTED,"Existing saves are never overwritten.");
        label(p,72,346,CYAN,"A Restore save");
    }
    label(p,384,(s->confirm_salvage || s->confirm_salvage_check)?356:s->confirm_vmu_restore?346:334,WHITE,"B Cancel");
}
static void video_trial(struct paint *p,const struct kui_shell_view *v) {
    box(p,32,132,576,284,NAVY);
    panel(p,48,158,544,214,PANEL);box(p,52,162,536,4,PINK);
    title(p,72,180,"Keep this video mode?");
    label(p,72,220,MUTED,"A keeps it and saves preferences. B restores the old mode.");
    char line[96];snprintf(line,sizeof(line),"Restoring previous mode in %u seconds",v->video_seconds);
    label(p,72,256,AMBER,line);
    label(p,72,302,CYAN,"A Keep mode");label(p,384,302,WHITE,"B Revert");
}
static void diagnostics(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    title(p,40,108,"Diagnostics");
    label(p,40,138,v->busy?MUTED:WHITE,"A Disc probe   X SD test   Y Save log");
    label(p,40,160,MUTED,"L Memory test   R Storage tests   UP/DOWN Scroll");
    panel(p,32,190,576,190,PANEL);
    unsigned count=v->log_count<KUI_SHELL_LOG_ROWS?v->log_count:KUI_SHELL_LOG_ROWS;
    if(!count) label(p,40,204,MUTED,"Diagnostic messages appear here.");
    else for(unsigned i=0;i<count;i++)
        label(p,40,200+i*17,MUTED,v->log_lines?v->log_lines[i]:NULL);
    char line[112];
    snprintf(line,sizeof(line),"%u lines  %s%s",v->total_log_lines,
        s->scroll?"SCROLLED":"LATEST",v->log_truncated?"  Earlier lines truncated":"");
    label(p,40,392,CYAN,line);
}
static const char *test_transport(unsigned transport) {
    return transport==KUI_STORAGE_SCIF?"SCIF":transport==KUI_STORAGE_SCI?"SCI":
        transport==KUI_STORAGE_IDE?"IDE":"Unknown";
}
static void test_recipe(char *line,size_t cap,const struct kui_storage_test_request *r) {
    if(r->preset==KUI_STORAGE_TEST_SOAK)
        snprintf(line,cap,"Soak: 16 MiB, 64 KiB chunks, %u minutes",r->soak_minutes);
    else snprintf(line,cap,"%s: %u MiB, %s KiB chunks, %u repeat%s",
        kui_storage_test_preset_name(r->preset),r->preset==KUI_STORAGE_TEST_QUICK?4:16,
        r->preset==KUI_STORAGE_TEST_QUICK?"64":"16/64/256",r->repeats,r->repeats==1?"":"s");
}
static void storage_test_confirmation(struct paint *p,const struct kui_shell *s,
        const struct kui_shell_view *v) {
    box(p,32,132,576,284,NAVY);panel(p,48,154,544,240,PANEL);
    box(p,52,158,536,4,PINK);
    label(p,64,177,WHITE,"RUN STORAGE TESTS?");
    char line[144];test_recipe(line,sizeof(line),&s->storage_test_request);
    label(p,64,207,CYAN,line);
    snprintf(line,sizeof(line),"Target: %s",v->storage_test_target?v->storage_test_target:"active storage device");
    label(p,64,232,WHITE,line);
    label(p,64,258,MUTED,"Writes a dedicated temporary test file, then rereads it.");
    label(p,64,282,MUTED,"Existing files stay intact. Music pauses during the test.");
    label(p,64,306,MUTED,"The result is saved; the temporary file is removed.");
    label(p,64,330,MUTED,"B stops safely. Keep the card connected until finished.");
    label(p,64,365,CYAN,"A Start test");label(p,384,365,WHITE,"B Cancel");
}
static void storage_test_progress(struct paint *p,const struct kui_shell_view *v) {
    const struct kui_storage_test_progress *r=v->storage_test_progress;
    title(p,40,108,"Storage tests");
    char line[144];snprintf(line,sizeof(line),"Target: %s",
        v->storage_test_target?v->storage_test_target:"active storage device");
    label(p,40,140,CYAN,line);
    panel(p,32,173,576,212,PANEL);
    title(p,48,190,v->cancel_requested?"Stopping safely...":r&&r->phase[0]?r->phase:"Preparing tests...");
    if(r) {
        unsigned pct=kui_shell_progress_tenths(r->done,r->total);
        snprintf(line,sizeof(line),"%llu / %llu KiB  (%u.%u%%)",
            (unsigned long long)(r->done/1024),(unsigned long long)(r->total/1024),pct/10,pct%10);
        label(p,48,227,WHITE,line);
        box(p,48,252,544,13,EDGE);box(p,48,252,544*pct/1000,13,CYAN);
        if(r->preset==KUI_STORAGE_TEST_SOAK)
            snprintf(line,sizeof(line),"Cycle %u  |  Elapsed %llu:%02llu  /  %llu min target",
                r->repeat,(unsigned long long)(r->elapsed_us/60000000),
                (unsigned long long)(r->elapsed_us/1000000%60),(unsigned long long)(r->target_us/60000000));
        else snprintf(line,sizeof(line),"Repeat %u / %u  |  Step %u / %u  |  Elapsed %llu:%02llu",
            r->repeat,r->repeats,r->step,r->steps,(unsigned long long)(r->elapsed_us/60000000),
            (unsigned long long)(r->elapsed_us/1000000%60));
        label(p,48,286,MUTED,line);
    }
    label(p,48,322,MUTED,"Write, flush, remount, then verify every saved byte.");
    label(p,48,348,v->cancel_requested?AMBER:MUTED,v->cancel_requested?
        "Finishing the current operation and removing the test file.":"B stops this run. Other storage actions are locked.");
    label(p,40,396,MUTED,"Read/write speeds and verification time are saved separately.");
}
static void test_rate_range(const struct kui_storage_test_result *r,uint32_t chunk,bool write,
        uint32_t *low,uint32_t *high,unsigned *count) {
    *low=UINT32_MAX;*high=0;*count=0;
    for(unsigned i=0;i<r->sample_count && i<KUI_STORAGE_TEST_SAMPLES;i++) {
        const struct kui_storage_test_sample *s=&r->samples[i];
        uint64_t us=write?s->write_us:s->read_us;
        if(s->chunk_bytes!=chunk || !s->verified || !us) continue;
        uint64_t value=(s->bytes/1024)*1000000/us;
        uint32_t rate=value>UINT32_MAX?UINT32_MAX:(uint32_t)value;
        if(rate<*low) *low=rate;
        if(rate>*high) *high=rate;
        ++*count;
    }
}
static void test_delta(char *line,size_t cap,const char *name,uint32_t rate,uint32_t baseline) {
    if(!rate || !baseline) {snprintf(line,cap,"%s n/a",name);return;}
    int64_t delta=((int64_t)rate-baseline)*1000/baseline;
    uint64_t magnitude=(uint64_t)(delta<0?-delta:delta);
    snprintf(line,cap,"%s %c%llu.%llu%%",name,delta<0?'-':'+',
        (unsigned long long)(magnitude/10),(unsigned long long)(magnitude%10));
}
static void storage_test_result(struct paint *p,const struct kui_shell *s) {
    const struct kui_storage_test_result *r=&s->storage_test_result;
    const struct kui_storage_test_result *b=&s->storage_test_history.baseline;
    char line[160];snprintf(line,sizeof(line),"Storage test #%lu  -  %s",(unsigned long)r->id,
        kui_storage_test_outcome_name(r->outcome));title(p,40,108,line);
    test_recipe(line,sizeof(line),&r->request);label(p,40,138,CYAN,line);
    if(s->storage_test_details) {
        snprintf(line,sizeof(line),"Build %.12s   Card: %.23s",r->metadata.build,
            r->request.card_label[0]?r->request.card_label:"unlabelled");label(p,40,162,WHITE,line);
        snprintf(line,sizeof(line),"%s   %.7s   Cluster %lu KiB   UI %u Hz   Music %s",
            test_transport(r->metadata.transport),r->metadata.filesystem,
            (unsigned long)(r->metadata.cluster_bytes/1024),r->metadata.ui_hz,
            r->metadata.music_playing?"on":"off");label(p,40,184,MUTED,line);
        snprintf(line,sizeof(line),"Elapsed %llu:%02llu   Cycles %llu   Verified %llu MiB",
            (unsigned long long)(r->elapsed_us/60000000),(unsigned long long)(r->elapsed_us/1000000%60),
            (unsigned long long)r->cycles,(unsigned long long)(r->verified_bytes/1048576));label(p,40,206,MUTED,line);
        snprintf(line,sizeof(line),"Errors %lu: CRC %lu  Timeout %lu  Rejected %lu  I/O %lu",
            (unsigned long)r->errors.total,(unsigned long)r->errors.crc_errors,
            (unsigned long)r->errors.timeout_errors,(unsigned long)r->errors.rejected_errors,
            (unsigned long)r->errors.io_errors);label(p,40,232,r->errors.total?AMBER:MUTED,line);
        snprintf(line,sizeof(line),"Last: %s / %s   Sector %lu +%lu",
            kui_storage_error_operation_name(r->errors.last_operation),
            kui_storage_error_result_name(r->errors.last_result),(unsigned long)r->errors.last_lba,
            (unsigned long)r->errors.last_count);label(p,40,254,MUTED,line);
        if(r->errors.sd_detail_valid) snprintf(line,sizeof(line),"SD CMD%u  Response 0x%02X   Filesystem error %u",
            r->errors.sd_command,r->errors.sd_response,r->fatfs_error);
        else snprintf(line,sizeof(line),"Filesystem error %u",r->fatfs_error);
        label(p,40,276,MUTED,line);
        snprintf(line,sizeof(line),"Phase %.23s   Byte offset %llu",r->failure_phase[0]?r->failure_phase:"none",
            (unsigned long long)r->failure_offset);label(p,40,298,MUTED,line);
        label(p,40,322,r->outcome==KUI_STORAGE_TEST_FAILED?AMBER:WHITE,r->message);
        label(p,40,348,MUTED,r->saved?"Saved report:":"Result could not be saved:");
        label(p,40,370,r->saved?CYAN:AMBER,r->path[0]?r->path:"No saved path");
        label(p,40,394,r->cleanup_failed?AMBER:MUTED,r->cleanup_failed?
            "Temporary file cleanup failed; see the report.":"X Summary   LEFT/RIGHT Select chunk on summary");
        return;
    }
    static const uint32_t chunks[]={16384,65536,262144};
    uint32_t chunk=r->request.preset==KUI_STORAGE_TEST_COMPARE?chunks[s->storage_test_chunk%3]:65536;
    uint32_t write=kui_storage_test_rate(r,chunk,true),read=kui_storage_test_rate(r,chunk,false);
    snprintf(line,sizeof(line),"%lu KiB chunks  |  Median write %lu / read %lu KiB/s",
        (unsigned long)(chunk/1024),(unsigned long)write,(unsigned long)read);
    label(p,40,166,WHITE,line);
    uint32_t wl,wh,rl,rh;unsigned wn,rn;
    test_rate_range(r,chunk,true,&wl,&wh,&wn);test_rate_range(r,chunk,false,&rl,&rh,&rn);
    if(wn>1 && rn>1) snprintf(line,sizeof(line),"Range: write %lu-%lu / read %lu-%lu KiB/s",
        (unsigned long)wl,(unsigned long)wh,(unsigned long)rl,(unsigned long)rh);
    else snprintf(line,sizeof(line),"%s",r->request.preset==KUI_STORAGE_TEST_SOAK?
        "Soak rate covers all complete cycles; X shows run details.":"One verified sample per chunk; use repeats to measure variation.");
    label(p,40,188,MUTED,line);
    snprintf(line,sizeof(line),"Verification: %s  |  Errors %lu  |  Cycles %llu",
        r->outcome==KUI_STORAGE_TEST_PASSED?"PASSED":r->outcome==KUI_STORAGE_TEST_STOPPED?"STOPPED / partial":"FAILED / incomplete",
        (unsigned long)r->errors.total,(unsigned long long)r->cycles);
    label(p,40,214,r->outcome==KUI_STORAGE_TEST_PASSED?GREEN:AMBER,line);
    snprintf(line,sizeof(line),"Worst pause: write %lu.%lu / read %lu.%lu ms",
        (unsigned long)(r->write_latency.max_us/1000),(unsigned long)(r->write_latency.max_us%1000/100),
        (unsigned long)(r->read_latency.max_us/1000),(unsigned long)(r->read_latency.max_us%1000/100));
    label(p,40,238,MUTED,line);
    snprintf(line,sizeof(line),"p95 upper bound: write %lu / read %lu ms",
        (unsigned long)(((uint64_t)r->write_latency.p95_upper_us+999)/1000),
        (unsigned long)(((uint64_t)r->read_latency.p95_upper_us+999)/1000));label(p,40,260,MUTED,line);
    if(!s->storage_test_history.baseline_valid) {
        label(p,40,286,CYAN,r->saved && r->outcome==KUI_STORAGE_TEST_PASSED?
            "No baseline. Y saves this passing result as the baseline.":
            "No baseline. Choose a saved passing result in History.");
        line[0]=0;
    } else {
        snprintf(line,sizeof(line),"Baseline #%lu: %.12s / %s / %.23s",(unsigned long)b->id,
            b->metadata.build,test_transport(b->metadata.transport),
            b->request.card_label[0]?b->request.card_label:"unlabelled");label(p,40,286,CYAN,line);
        if(r->id==b->id) snprintf(line,sizeof(line),"This result is the saved baseline.");
        else if(!kui_storage_test_comparable(r,b)) snprintf(line,sizeof(line),
            "No comparison: recipe, filesystem/settings or outcome differ.");
        else {
            char write_delta[48],read_delta[48];
            test_delta(write_delta,sizeof(write_delta),"Write",write,kui_storage_test_rate(b,chunk,true));
            test_delta(read_delta,sizeof(read_delta),"Read",read,kui_storage_test_rate(b,chunk,false));
            snprintf(line,sizeof(line),"%s  /  %s  (matching measurement settings)",write_delta,read_delta);
        }
    }
    label(p,40,308,MUTED,line);
    snprintf(line,sizeof(line),"This run: %.12s / %s / %.23s",r->metadata.build,
        test_transport(r->metadata.transport),r->request.card_label[0]?r->request.card_label:"unlabelled");
    label(p,40,332,MUTED,line);
    label(p,40,356,r->saved?MUTED:AMBER,r->saved?"Saved report:":"Not saved: keep a photo of these results.");
    label(p,40,376,r->saved?CYAN:AMBER,r->path[0]?r->path:r->message);
    label(p,40,398,r->cleanup_failed?AMBER:MUTED,r->cleanup_failed?
        "Temporary file cleanup failed; see X Details.":r->request.preset==KUI_STORAGE_TEST_COMPARE?
        "LEFT/RIGHT Select chunk   X Details":"X Details shows timing, metadata and error information.");
}
static void storage_tests(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    if(v->busy && v->storage_test_progress) {storage_test_progress(p,v);return;}
    if(s->storage_test_show_result) {storage_test_result(p,s);return;}
    title(p,40,108,"Storage tests");
    char line[144];snprintf(line,sizeof(line),"Target: %s",v->storage_test_target?v->storage_test_target:"active storage device");
    label(p,40,137,CYAN,line);
    const char *names[]={"Preset","Repeats","Soak duration","Card nickname","Start test","Repeat last test","History and baseline","Advanced benchmarks","SCI async probe"};
    for(unsigned i=0;i<9;i++) {
        unsigned y=156+i*24;bool selected=i==s->storage_test_selected;
        panel(p,32,y,576,22,selected?SELECTED:PANEL);
        if(selected) box(p,32,y+3,3,16,PINK);
        label(p,48,y+3,selected?WHITE:MUTED,names[i]);
        switch(i) {
        case 0: snprintf(line,sizeof(line),"< %s >",kui_storage_test_preset_name(s->storage_test_request.preset));break;
        case 1: snprintf(line,sizeof(line),s->storage_test_request.preset==KUI_STORAGE_TEST_SOAK?"Until time target":"< %u >",s->storage_test_request.repeats);break;
        case 2: snprintf(line,sizeof(line),s->storage_test_request.preset==KUI_STORAGE_TEST_SOAK?"< %u minutes >":"Soak only",s->storage_test_request.soak_minutes);break;
        case 3: snprintf(line,sizeof(line),"%.23s",s->storage_test_request.card_label[0]?s->storage_test_request.card_label:"A Edit (optional)");break;
        case 5: snprintf(line,sizeof(line),"%s",s->storage_test_last_valid?"A Review":"No previous test");break;
        case 7: snprintf(line,sizeof(line),"bench.cfg");break;
        case 8: snprintf(line,sizeof(line),"Experimental read test");break;
        default: snprintf(line,sizeof(line),"A Open");break;
        }
        words(p,350,y+3,594,selected?CYAN:MUTED,line,false);
    }
    test_recipe(line,sizeof(line),&s->storage_test_request);label(p,40,378,WHITE,line);
    label(p,40,399,MUTED,v->busy?"Working... B stops safely.":"Runs on the active device. Existing files are kept.");
}
static void sci_async_probe(struct paint *p,const struct kui_shell_view *v) {
    title(p,40,108,"SCI async probe");
    label(p,40,138,CYAN,"Tests whether the CPU can work during an SCI read.");
    const struct kui_app_status *r=v->app_status;
    bool result=r && r->complete;
    if(!result && !v->busy) {
        panel(p,32,174,576,225,PANEL);
        label(p,48,190,WHITE,"Read-only tests of existing sectors on the SCI card");
        label(p,48,216,MUTED,"A Quick: the same sector at slow and fast speeds.");
        label(p,48,238,MUTED,"X 60-second stress: 16 sectors spread across the card.");
        label(p,48,260,MUTED,"Y The same stress with the screen updating.");
        label(p,48,282,MUTED,"R Speed: ordinary vs async readers, and the CMD18 stream.");
        label(p,48,304,MUTED,"X and R pause screen updates; hold B to stop safely.");
        label(p,48,326,MUTED,"Only the final JSON report writes to /KUI/tests.");
        label(p,48,348,MUTED,"Normal game reads are unchanged by this experiment.");
        label(p,48,376,CYAN,"A Quick   X 60s stress   Y With screen   R Speed");
        return;
    }
    words(p,40,172,604,r && r->errors?AMBER:WHITE,v->cancel_requested?
        "Stopping safely, then checking normal read recovery...":
        r && r->message[0]?r->message:"Preparing probe...",false);
    if(result) {
        unsigned count=r->line_count<8?r->line_count:8;
        for(unsigned i=0;i<count;i++) label(p,40,220+i*23,i==6?CYAN:MUTED,r->lines[i]);
    } else if(v->sci_video_quiet) {
        label(p,40,246,MUTED,"This screen stays still until the read test finishes.");
        label(p,40,278,MUTED,"Reads, timer interrupts and controller input stay active.");
        label(p,40,310,MUTED,"Hold B to stop safely. Wait for the result screen.");
    } else {
        label(p,40,246,MUTED,"Reading baselines, then checking autonomous transfers.");
        label(p,40,278,MUTED,"Hold B to stop safely between reads and check recovery.");
        label(p,40,310,MUTED,"Keep the card connected until the result appears.");
    }
}
static void storage_test_history(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_storage_test_history *h=&s->storage_test_history;
    title(p,40,108,"Storage test history");
    label(p,40,137,MUTED,v->busy?"Reading saved results...":"Newest eight results on this storage device");
    unsigned count=h->count<KUI_STORAGE_TEST_HISTORY?h->count:KUI_STORAGE_TEST_HISTORY;
    char line[160];
    for(unsigned i=0;i<count;i++) {
        const struct kui_storage_test_result *r=&h->rows[i];
        unsigned y=158+i*24;bool selected=i==s->storage_history_selected;
        panel(p,32,y,576,22,selected?SELECTED:PANEL);
        snprintf(line,sizeof(line),"%c #%lu  %-7s  %-7s  %s  %.23s",
            h->baseline_valid && h->baseline.id==r->id?'*':' ',(unsigned long)r->id,
            kui_storage_test_preset_name(r->request.preset),kui_storage_test_outcome_name(r->outcome),
            test_transport(r->metadata.transport),r->request.card_label[0]?r->request.card_label:"unlabelled");
        label(p,40,y+3,selected?WHITE:MUTED,line);
    }
    if(!count && !v->busy) label(p,40,178,MUTED,"No saved test results yet. B returns to test setup.");
    if(h->baseline_valid) snprintf(line,sizeof(line),"* Baseline #%lu   %.12s / %s / %.23s",
        (unsigned long)h->baseline.id,h->baseline.metadata.build,test_transport(h->baseline.metadata.transport),
        h->baseline.request.card_label[0]?h->baseline.request.card_label:"unlabelled");
    else snprintf(line,sizeof(line),"Y selects a saved passing result as the baseline.");
    label(p,40,360,CYAN,line);
    if(count) {
        const struct kui_storage_test_result *r=&h->rows[s->storage_history_selected<count?s->storage_history_selected:0];
        snprintf(line,sizeof(line),"%.12s   %.7s   Cluster %lu KiB   Music %s",r->metadata.build,
            r->metadata.filesystem,(unsigned long)(r->metadata.cluster_bytes/1024),r->metadata.music_playing?"on":"off");
        label(p,40,380,MUTED,line);
    }
    label(p,40,400,MUTED,h->message[0]?h->message:"A opens rates, comparison, metadata and error details.");
}
static void confirmation(struct paint *p,const struct kui_shell *s,bool quick) {
    box(p,32,154,576,262,NAVY);
    box(p,48,160,544,180,EDGE); box(p,52,164,536,172,PANEL);
    box(p,52,164,536,4,PINK);
    label(p,72,188,WHITE,quick?"USE QUICK RESUME?":"START A NEW DUMP?");
    label(p,72,222,MUTED,quick?"Raw jobs can skip saved-prefix hash checks.":
        "Insert the retail GD-ROM and close the lid.");
    label(p,72,244,MUTED,quick?"Direct 2048 and repaired jobs verify saved bytes.":
        "A new folder keeps existing dumps intact.");
    if(quick) label(p,72,266,AMBER,"Use Verify to detect same-size raw-job damage.");
    else {
        char label[128];snprintf(label,sizeof(label),"New folder label: %s",
            s->capture_folder_label[0]?s->capture_folder_label:"Auto (disc title)");
        words(p,72,266,568,MUTED,label,false);
    }
    if(quick) path_words(p,72,282,568,MUTED,s->capture_job[0]?s->capture_job:"Automatic latest matching job");
    label(p,72,304,CYAN,quick?"A Quick resume":"A Start capture"); label(p,368,304,WHITE,"B Cancel");
}
static void gd_play(struct paint *p,const struct kui_shell_view *v) {
    title(p,40,108,"GD Play");
    char line[160];snprintf(line,sizeof(line),"Inserted: %s",
        v->inserted_title&&v->inserted_title[0]?v->inserted_title:"No disc detected");
    label(p,40,138,CYAN,line);
    panel(p,32,174,576,224,PANEL);
    art(p,60,214,128,128,kui_art_icons[0]);
    label(p,216,198,WHITE,"A Boot via console BIOS");
    words(p,216,232,592,MUTED,"Exits K-UI and starts the console BIOS.",false);
    words(p,216,258,592,MUTED,"Region and autostart settings still apply.",false);
    words(p,216,284,592,MUTED,"Use the BIOS Play option if needed.",false);
    words(p,216,326,592,MUTED,"The current boot disc can start K-UI again.",false);
}
static void gd_boot_confirmation(struct paint *p) {
    box(p,32,154,576,262,NAVY);
    panel(p,48,160,544,202,PANEL);box(p,52,164,536,4,PINK);
    label(p,72,188,WHITE,"EXIT K-UI AND BOOT VIA CONSOLE BIOS?");
    label(p,72,224,MUTED,"Console region and autostart settings still apply.");
    label(p,72,248,MUTED,"Select Play in the BIOS if the disc does not start.");
    label(p,72,282,MUTED,"This closes the current K-UI session.");
    label(p,72,322,CYAN,"A Exit to BIOS");label(p,368,322,WHITE,"B Cancel");
}
static void cd_audio(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Audio CD player");
    const struct kui_cd_audio_status *cd=&s->cd_audio;
    char line[112];snprintf(line,sizeof(line),"%u audio tracks   %s",cd->count,
        cd->paused?"PAUSED":cd->playing?"PLAYING":"STOPPED");
    label(p,40,136,CYAN,line);
    label(p,40,160,v->busy?MUTED:WHITE,"A Play track   Y Pause / resume   X Stop");
    panel(p,32,190,576,178,PANEL);
    unsigned count=cd->count<99?cd->count:99,first=s->cd_selected/8*8;
    if(!count) label(p,48,206,MUTED,v->busy?"Reading audio CD...":"Insert an audio CD, then press R Refresh.");
    for(unsigned i=first;i<count && i<first+8;i++) {
        unsigned y=196+(i-first)*21;
        if(i==s->cd_selected) panel(p,40,y,560,21,SELECTED);
        snprintf(line,sizeof(line),"Track %02u%s",cd->tracks[i].number,
            cd->current==cd->tracks[i].number && (cd->playing||cd->paused)?"  <":"");
        label(p,52,y+1,i==s->cd_selected?WHITE:MUTED,line);
        snprintf(line,sizeof(line),"%u:%02u",cd->tracks[i].seconds/60,cd->tracks[i].seconds%60);
        label(p,500,y+1,MUTED,line);
    }
    label(p,40,378,cd->poisoned?AMBER:CYAN,cd->message);
    label(p,40,396,MUTED,"CD playback stops before a rip or another disc operation.");
}
static void music_player(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Music Player");
    char line[160];snprintf(line,sizeof(line),"SD: %s",s->music_path);
    label(p,40,136,CYAN,line);
    label(p,40,160,v->busy?MUTED:WHITE,"A Play WAV / Ogg   X Clear cache   Y Stop   R Refresh");
    panel(p,32,190,576,178,PANEL);
    unsigned count=s->music_listing.count<KUI_MUSIC_PLAYER_ROWS?s->music_listing.count:KUI_MUSIC_PLAYER_ROWS;
    if(!count) label(p,48,206,MUTED,v->busy?"Opening card...":"No folders, WAV or Ogg files on this page.");
    for(unsigned i=0;i<count;i++) {
        unsigned y=196+i*21;
        const struct kui_music_player_entry *entry=&s->music_listing.entries[i];
        if(i==s->music_selected) panel(p,40,y,560,21,SELECTED);
        label(p,50,y+1,entry->disabled?AMBER:MUTED,entry->directory?"DIR":strstr(entry->name,".ogg")||strstr(entry->name,".OGG")?"OGG":"WAV");
        words(p,94,y+1,590,i==s->music_selected?WHITE:MUTED,entry->name,false);
    }
    const struct kui_app_status *status=v->app_status;
    label(p,40,378,status&&status->complete&&!status->passed?AMBER:CYAN,
        status&&status->message[0]?status->message:s->music_listing.message);
    snprintf(line,sizeof(line),"PAGE %u%s   Cached %lu.%lu MiB / 8 MiB",
        s->music_page+1,s->music_listing.has_more?" +":"",
        (unsigned long)(v->music_cache_bytes/1048576u),
        (unsigned long)((v->music_cache_bytes%1048576u)*10u/1048576u));
    label(p,40,396,MUTED,line);
}
/* Box art is already scaled to its cell; only whole rows are copied. */
static void cover_image(struct paint *p,unsigned x,unsigned y,unsigned edge,const uint16_t *pixels) {
    if(x>=640 || y>=480) return;
    unsigned rows=edge<480-y?edge:480-y,cols=edge<640-x?edge:640-x;
    for(unsigned row=0;row<rows;row++) memcpy(p->fb+(y+row)*640+x,pixels+row*edge,cols*2u);
}
static unsigned root_of(unsigned v) {
    unsigned r=0;
    while((r+1)*(r+1)<=v) ++r;
    return r;
}
static void round_spot(struct paint *p,unsigned cx,unsigned cy,unsigned radius,uint16_t color) {
    for(unsigned dy=0;dy<=2*radius;dy++) {
        unsigned off=dy>radius?dy-radius:radius-dy,dx=root_of(radius*radius-off*off);
        box(p,cx-dx,cy-radius+dy,2*dx+1,1,color);
    }
}
/* Drawn where a game has no box art yet: a disc; a folder for folders. */
static void placeholder(struct paint *p,unsigned x,unsigned y,unsigned edge,bool folder) {
    panel(p,x,y,edge,edge,PANEL);
    if(folder) {
        box(p,x+edge/5,y+edge*3/10,edge/4,edge/10,EDGE);
        box(p,x+edge/5,y+edge*2/5,edge*3/5,edge*7/20,EDGE);
        return;
    }
    round_spot(p,x+edge/2,y+edge/2,edge*3/10,EDGE);
    round_spot(p,x+edge/2,y+edge/2,edge/10,PANEL);
    round_spot(p,x+edge/2,y+edge/2,edge/30+1,EDGE);
}
/* During a page turn the page being left stays on screen unchanged. */
static unsigned shown_page(const struct kui_shell *s) {
    return s->games_turning?s->games_shown_page:s->games_page;
}
static unsigned shown_selected(const struct kui_shell *s) {
    return s->games_turning?s->games_shown_selected:s->games_selected;
}
static const char *entry_text(const struct kui_games_entry *e) {
    return e->title[0]?e->title:e->name;
}
static const uint16_t *entry_cover(const struct kui_shell *s,const struct kui_shell_view *v,unsigned i) {
    const struct kui_games_page *l=&s->games_listing;
    if(i!=shown_selected(s) || i>=l->count || !l->entries[i].cover || l->view!=kui_shell_games_view(s)) return NULL;
    return v->game_selected_cover?v->game_selected_cover:v->game_covers?v->game_covers[i]:NULL;
}
static void entry_art(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v,
        unsigned i,unsigned x,unsigned y,unsigned edge) {
    const uint16_t *pixels=entry_cover(s,v,i);
    if(pixels) cover_image(p,x,y,edge,pixels);
    else placeholder(p,x,y,edge,s->games_listing.entries[i].directory);
}
/* Text centred in a cell, shortened with an ellipsis when it cannot fit. */
static void centred(struct paint *p,unsigned x,unsigned y,unsigned width,uint16_t color,const char *text) {
    unsigned w=kui_shell_font_width(text,false);
    words(p,w<width?x+(width-w)/2:x,y,x+width,color,text,false);
}
static void games_scan(struct paint *p,const struct kui_shell_view *v) {
    const struct kui_app_status *st=v->app_status;
    panel(p,32,140,576,250,PANEL);
    label(p,48,152,CYAN,"Scanning for box art");
    label(p,48,176,WHITE,st && st->message[0]?st->message:"Finding games...");
    box(p,48,202,544,10,EDGE);
    if(st && st->total) box(p,48,202,(unsigned)(544u*(st->done<st->total?st->done:st->total)/st->total),10,CYAN);
    for(unsigned i=0;st && i<st->line_count && i<6;i++) label(p,48,224+i*22,MUTED,st->lines[i]);
    label(p,48,366,MUTED,"B stops safely; finished covers are kept.");
}
static void games(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_games_page *l=&s->games_listing;
    unsigned view=kui_shell_games_view(s),count=l->count<KUI_GAMES_ROWS?l->count:KUI_GAMES_ROWS;
    title(p,40,108,s->games_group[0]?"Discs":"Games");
    char line[180];snprintf(line,sizeof(line),"%s",s->games_group[0]?l->group_name:s->games_path);
    words(p,150,116,392,CYAN,line,false);
    unsigned pages=l->total?(l->total+KUI_GAMES_ROWS-1)/KUI_GAMES_ROWS:0;
    unsigned page=shown_page(s);
    if(pages) snprintf(line,sizeof(line),"Page %u of %u   %u items",page+1,
        pages>page?pages:page+1,l->total);
    else snprintf(line,sizeof(line),"Page %u%s",page+1,l->has_more?" +":"");
    unsigned width=kui_shell_font_width(line,false);
    words(p,width<196?600-width:404,116,608,MUTED,line,false);
    if(s->games_scanning && v->busy) {games_scan(p,v);return;}
    unsigned chosen=shown_selected(s)<count?shown_selected(s):0;
    for(unsigned i=0;i<count;i++) {
        const struct kui_games_entry *e=&l->entries[i];
        uint16_t ink=e->disabled?AMBER:i==chosen?WHITE:MUTED;
        if(view==KUI_GAMES_VIEW_COMPACT) {
            unsigned x=32+(i/4)*292,y=142+(i%4)*62;
            if(i==chosen) panel(p,x,y,284,58,SELECTED);
            entry_art(p,s,v,i,x+4,y+1,KUI_COVER_SMALL);
            words(p,x+70,y+10,x+280,ink,entry_text(e),false);
            words(p,x+70,y+32,x+280,MUTED,e->group[0]?"Choose a disc":e->directory?"Folder":e->variant_2048_path[0]?
                "Original / 2048-byte copy":strcmp(entry_text(e),e->name)?e->name:"Disc image",false);
        } else if(view==KUI_GAMES_VIEW_GALLERY) {
            unsigned x=32+(i%4)*144,y=134+(i/4)*130;
            if(i==chosen) panel(p,x+10,y-3,124,130,SELECTED);
            entry_art(p,s,v,i,x+20,y+2,KUI_COVER_MEDIUM);
            centred(p,x+12,y+110,120,ink,entry_text(e));
        } else {
            unsigned y=146+i*30;
            if(i==chosen) panel(p,32,y-5,388,28,SELECTED);
            words(p,44,y,e->directory || e->group[0]?368:412,ink,entry_text(e),false);
            if(e->directory) words(p,378,y,412,MUTED,"DIR",false);
            else if(e->group[0]) words(p,372,y,412,MUTED,"DISCS",false);
        }
    }
    if(view==KUI_GAMES_VIEW_LIST && count) {
        const struct kui_games_entry *e=&l->entries[chosen];
        const uint16_t *pixels=entry_cover(s,v,chosen);
        if(pixels) cover_image(p,436,146,KUI_COVER_LARGE,pixels);
        else placeholder(p,436,146,KUI_COVER_LARGE,e->directory);
        centred(p,436,316,KUI_COVER_LARGE,MUTED,e->directory?"Folder":pixels?(strcmp(entry_text(e),e->name)?e->name:""):
            s->games_art_loading?"Loading box art...":l->artwork?"No box art found":"No box art yet");
    }
    if(!count && !v->busy) label(p,40,210,MUTED,"No selectable disc images or folders in this view.");
    label(p,40,398,v->busy?CYAN:AMBER,!v->busy && count && l->entries[chosen].group[0]?
        "A chooses a disc. X scans for library changes.":!v->busy && count && l->entries[chosen].variant_2048_path[0]?
        "A chooses Original or 2048-byte copy.":!v->busy && count && !l->artwork?
        "No box art yet: press START, then Scan box art.":l->message);
}
static void games_variants(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Games / Choose version");
    if(s->games_selected<s->games_listing.count)
        words(p,40,140,608,CYAN,entry_text(&s->games_listing.entries[s->games_selected]),false);
    static const char *const names[]={"Original","2048-byte copy"};
    const char *paths[]={s->games_original_path,s->games_2048_path};
    for(unsigned i=0;i<2;i++) {
        unsigned y=178+i*96;
        panel(p,32,y,576,82,s->games_variant_selected==i?SELECTED:PANEL);
        label(p,48,y+12,s->games_variant_selected==i?WHITE:MUTED,names[i]);
        words(p,48,y+44,592,MUTED,paths[i],false);
    }
    label(p,40,398,MUTED,"Select a version to inspect, then choose its reader.");
}
static void games_variant_line(struct paint *p,const struct kui_shell *s,unsigned y) {
    const char *version=kui_shell_games_variant_label(s);
    if(version) {
        char line[64];snprintf(line,sizeof(line),"Version: %s",version);
        label(p,40,y,CYAN,line);
    }
}
static const char *game_background(const struct kui_shell *s) {
    if(s->games_detail.format==KUI_GAME_IMAGE_ZSO) return "Off";
    if(kui_shell_games_ce_probe_ready(s)) return "On (required)";
    if(s->games_launch_reader==KUI_GAME_READER_STANDARD) return "Off";
    if(kui_shell_games_retail_ready(s) && !s->games_launch_cdda)
        return s->games_launch_reader==KUI_GAME_READER_EAGER?"On - Legacy (Y)":"On - New (X)";
    return "On";
}
static void game_detail(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_games_detail *d=&s->games_detail;
    title(p,40,108,kui_shell_games_launch_ready(s)?"Games / Ready to play":"Games / Selected image");
    words(p,40,140,608,d->valid?CYAN:AMBER,d->valid?(d->title[0]?d->title:"Untitled image"):
        v->busy?"Inspecting image...":d->stopped?"Inspection stopped":"Could not inspect image",false);
    words(p,40,158,608,MUTED,s->games_selected_path,false);
    games_variant_line(p,s,176);
    panel(p,32,200,576,204,PANEL);
    bool cover=d->cover && v->game_detail_cover;
    unsigned right=cover?428:592;
    if(cover) cover_image(p,440,214,KUI_COVER_LARGE,v->game_detail_cover);
    if(kui_shell_games_launch_ready(s)) {
        char line[128];
        words(p,48,218,right,WHITE,d->format==KUI_GAME_IMAGE_ZSO?
            "ZSO launch test; compatibility untested":"Ready to launch",false);
        snprintf(line,sizeof(line),"Background loading: %s",game_background(s));
        words(p,48,256,right,MUTED,line,false);
        words(p,48,282,right,MUTED,s->games_launch_cdda?"CD music: On":"CD music: Off",false);
        if(kui_shell_games_cdda_ready(s))
            words(p,48,304,right,AMBER,kui_shell_games_cdda_status(s),false);
        words(p,48,328,right,CYAN,"A Launch game   Y Options",false);
        words(p,48,350,right,MUTED,s->games_baseline?"Temporary baseline: options are not saved.":"Options are saved for this image.",false);
        words(p,48,378,right,MUTED,"Keep the SD card inserted while playing.",false);
    } else {
        words(p,48,218,right,AMBER,d->message[0]?d->message:
            "This image cannot be launched yet.",false);
        words(p,48,256,right,MUTED,v->busy?"Reading image information from SD.":
            d->valid && d->format==KUI_GAME_IMAGE_ZSO?"Image information remains available in Options.":
            "Check the image files and inspect again.",false);
        words(p,48,294,right,MUTED,kui_shell_games_image_ready(s)?
            "Y Options   X Inspect again":kui_shell_games_zso_selected(s)?"Y Select ZSO codec   X Inspect again":"X Inspect again",false);
    }
}
static void games_information(struct paint *p,const struct kui_shell *s) {
    const struct kui_games_detail *d=&s->games_detail;
    title(p,40,108,"Games / Image information");
    words(p,40,140,608,CYAN,s->games_selected_path,false);
    games_variant_line(p,s,160);
    panel(p,32,188,576,214,PANEL);
    if(!d->valid) {label(p,48,208,AMBER,"Inspect the image to see its information.");return;}
    char line[160];snprintf(line,sizeof(line),"Product: %s   Region: %s",d->product,d->region);
    label(p,48,202,WHITE,line);
    snprintf(line,sizeof(line),"Boot file: %s   %lu bytes",d->boot_file,(unsigned long)d->boot_bytes);
    label(p,48,230,WHITE,line);
    if(d->format==KUI_GAME_IMAGE_ZSO)
        snprintf(line,sizeof(line),"Tracks: %u   Data: %u   Audio: unknown",d->tracks,d->data_tracks);
    else snprintf(line,sizeof(line),"Tracks: %u   Data: %u   Audio: %u",d->tracks,d->data_tracks,d->audio_tracks);
    label(p,48,258,WHITE,line);
    static const char *const formats[]={"GDI","ISO","CUE/BIN","CDI","BIN/IMG","ZSO"};
    const char *format=(unsigned)d->format<sizeof(formats)/sizeof(formats[0])?formats[d->format]:"Unknown";
    if(d->format==KUI_GAME_IMAGE_ZSO)format=d->zso_codec==KUI_ZSO_LZO1X?"ZSO (LZO1X)":"ZSO (standard LZ4)";
    snprintf(line,sizeof(line),"Format: %s   %llu bytes",format,(unsigned long long)d->bytes);
    label(p,48,286,MUTED,line);
    snprintf(line,sizeof(line),"Boot file starts at LBA %lu",(unsigned long)d->boot_lba);
    label(p,48,314,MUTED,line);
    label(p,48,342,MUTED,d->windows_ce?"Windows CE image":d->native_cd?"Native CD image":"Native GD image");
    label(p,48,376,MUTED,"Metadata checks do not verify every saved sector.");
}
static void games_advanced(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Games / Advanced");
    label(p,40,142,CYAN,"Source: SD card");
    const char *names[]={"Game library","Browse SD folders","Scan box art","Developer tests",
        "Scan for changes","Rebuild library"};
    const char *details[]={"Open /Games","Find a disc image elsewhere on the card",
        "Covers and titles for every game in /Games","Image and loader read tests",
        "Find added, moved or removed games","Build a fresh saved list; keep game settings"};
    for(unsigned i=0;i<6;i++) {
        unsigned y=156+i*38;
        panel(p,32,y,576,35,s->games_advanced_selected==i?SELECTED:PANEL);
        label(p,48,y+2,WHITE,names[i]);label(p,48,y+18,MUTED,details[i]);
    }
    label(p,40,396,MUTED,"IDE / CF sources are not available yet.");
}
static void games_probe_confirmation(struct paint *p,const struct kui_shell_view *v) {
    title(p,40,108,"Games / Loader read test");
    panel(p,32,152,576,250,PANEL);
    label(p,48,168,CYAN,v->busy?"Preparing the handoff...":"Run our test program?");
    label(p,48,204,WHITE,"Exits this menu and starts our test program.");
    label(p,48,230,WHITE,"Reads original SD test data after shutdown.");
    label(p,48,256,MUTED,"This does not launch a retail game.");
    label(p,48,294,WHITE,"Keep the SD card inserted throughout the test.");
    label(p,48,320,WHITE,"Photograph the final result, then power cycle.");
    if(v->busy && v->app_status && v->app_status->message[0])
        label(p,48,364,CYAN,v->app_status->message);
    else label(p,48,364,AMBER,"Power cycle to return to the launcher.");
}
static void games_image_probe_confirmation(struct paint *p,const struct kui_shell *s,
        const struct kui_shell_view *v) {
    title(p,40,108,"Games / Test image reads");
    words(p,40,140,608,CYAN,s->games_selected_path,false);
    games_variant_line(p,s,154);
    panel(p,32,168,576,234,PANEL);
    if(!kui_shell_games_image_ready(s)) {
        label(p,48,186,AMBER,"Image details changed; inspect the image again.");
        label(p,48,220,WHITE,"Return to game details and inspect again.");
        return;
    }
    label(p,48,186,CYAN,v->busy?"Preparing the handoff...":"Run the selected-image test?");
    label(p,48,218,WHITE,"Exits this menu and reads samples from this GDI.");
    label(p,48,244,WHITE,"Tests the GD requests used by retail games.");
    label(p,48,270,MUTED,"The game itself will not start.");
    label(p,48,302,WHITE,"Keep the SD card inserted throughout the test.");
    label(p,48,328,WHITE,"Photograph the final result, then power cycle.");
    label(p,48,354,MUTED,"Sample checks are not a full image verification.");
    if(v->busy && v->app_status && v->app_status->message[0])
        label(p,48,380,CYAN,v->app_status->message);
}
static void games_retail_confirmation(struct paint *p,const struct kui_shell *s,
        const struct kui_shell_view *v) {
    title(p,40,108,"Games / Launch game");
    words(p,40,140,608,CYAN,s->games_selected_path,false);
    games_variant_line(p,s,154);
    panel(p,32,168,576,234,PANEL);
    if(!kui_shell_games_launch_ready(s)) {
        label(p,48,186,AMBER,"Image details changed; launch is unavailable.");
        label(p,48,220,WHITE,"Press B, then X to inspect the image again.");
        return;
    }
    label(p,48,186,CYAN,v->busy?"Preparing the launch...":
        s->games_detail.title[0]?s->games_detail.title:"Launch selected game?");
    char line[128];
    snprintf(line,sizeof(line),"Background loading: %s",game_background(s));
    label(p,48,220,WHITE,line);
    label(p,48,246,WHITE,s->games_launch_cdda?"CD music: On":"CD music: Off");
    if(kui_shell_games_cdda_ready(s))
        label(p,48,268,AMBER,kui_shell_games_cdda_status(s));
    label(p,48,292,kui_game_ram_patches_enabled(&s->games_launch_patches)?AMBER:MUTED,
        kui_game_ram_patches_enabled(&s->games_launch_patches)?"RAM patches: On (one-time boot writes)":"RAM patches: Off");
    label(p,48,316,s->games_baseline?CYAN:WHITE,s->games_baseline?"Temporary baseline: options are not saved.":"Keep the SD card inserted while playing.");
    label(p,48,342,AMBER,"Power cycle to return to the launcher.");
    if(v->busy && v->app_status && v->app_status->message[0])
        label(p,48,380,CYAN,v->app_status->message);
    else label(p,48,380,MUTED,"Y opens saved game options.");
}
static void game_options_notice(struct paint *p,const struct kui_shell *s) {
    const char *notice=s->games_baseline?"Temporary baseline: these choices are not saved.":s->games_launch_notice;
    bool failed=strncmp(notice,"Not saved",9)==0 || strstr(notice,"could not")!=NULL;
    words(p,40,398,608,failed?AMBER:CYAN,notice,false);
}
static void games_launch_options(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Games / Options");
    words(p,40,140,608,CYAN,s->games_detail.title[0]?s->games_detail.title:s->games_selected_path,false);
    games_variant_line(p,s,160);
    const char *names[]={"Background loading","CD music","Advanced","Virtual VMU card","ZSO codec"};
    const char *values[]={game_background(s),
        kui_shell_games_cdda_ready(s)?(s->games_launch_cdda?"On":"Off"):"Unavailable","Open","Choose",s->games_launch_codec==KUI_ZSO_LZO1X?"LZO1X":"Standard LZ4"};
    for(unsigned i=0;i<(kui_shell_games_zso_selected(s)?KUI_SHELL_GAME_OPTION_COUNT:KUI_SHELL_GAME_OPTION_COUNT-1u);i++) {
        unsigned y=170+i*30;bool ready=kui_shell_games_option_ready(s,i);
        panel(p,32,y,576,28,s->games_launch_selected==i?SELECTED:PANEL);
        label(p,48,y+6,ready?WHITE:MUTED,names[i]);
        words(p,350,y+6,592,ready?CYAN:MUTED,values[i],false);
    }
    char saving[100];
    const char *card=s->games_vmu_loading?"Checking card...":s->games_vmu_label[0]?
        s->games_vmu_label:"Checked at launch";
    if(s->vmu_emulation_known && !s->vmu_emulation_enabled && !s->games_detail.windows_ce)
        card="Physical VMU (virtual VMU Off)";
    snprintf(saving,sizeof(saving),"Saving: %s",card);label(p,40,328,CYAN,saving);
    if(kui_shell_games_cdda_ready(s))
        label(p,40,350,AMBER,kui_shell_games_cdda_status(s));
    else label(p,40,350,MUTED,s->games_vmu_notice[0]?s->games_vmu_notice:
        "Virtual VMU compatibility is checked at game launch.");
    const char *hint="Extra settings, image information and developer tests.";
    if(s->games_launch_selected==KUI_SHELL_GAME_OPTION_READER)
        hint=s->games_detail.format==KUI_GAME_IMAGE_ZSO?"ZSO uses the Standard reader; background loading is unavailable.":
            kui_shell_games_ce_probe_ready(s)?"This Windows CE game requires background loading.":
            s->games_launch_cdda?"Changing background loading turns CD music off.":
            "Load from SD while the game continues working.";
    else if(s->games_launch_selected==KUI_SHELL_GAME_OPTION_CODEC)
        hint="Explicit codec selection. ZSO headers do not identify LZ4 versus LZO1X.";
    else if(s->games_launch_selected==KUI_SHELL_GAME_OPTION_VMU_CARD)
        hint="Choose a save version, set a default, or ask on each launch.";
    else if(s->games_launch_selected==KUI_SHELL_GAME_OPTION_CDDA)
        hint=kui_shell_games_cdda_ready(s)?(s->games_detail.windows_ce?
            "Play music from this game's audio tracks; runtime checks apply.":
            "Runtime checks apply. Turning On sets background loading Off."):
            "No playable audio tracks are represented by this image.";
    words(p,40,374,608,MUTED,hint,false);
    game_options_notice(p,s);
}
static void games_vmu_card_picker(struct paint *p,const struct kui_shell *s) {
    const struct kui_vmu_game_info *info=&s->games_vmu_info;
    title(p,40,108,"Choose virtual VMU card");
    words(p,40,140,608,CYAN,s->games_detail.title,false);
    char line[160];
    snprintf(line,sizeof(line),"%u card%s  |  %s",(unsigned)info->count,info->count==1?"":"s",
        info->default_pair?"Default saved":"Ask when multiple cards are available");
    label(p,40,164,MUTED,line);panel(p,32,188,576,177,PANEL);
    unsigned selected=s->games_vmu_card_selected;
    unsigned start=selected/KUI_VMU_WIZARD_ROWS*KUI_VMU_WIZARD_ROWS;
    for(unsigned row=0;row<KUI_VMU_WIZARD_ROWS && start+row<info->count && start+row<KUI_VMU_GAME_INFO_CARDS_MAX;row++) {
        unsigned i=start+row,y=193+row*21;const struct kui_vmu_game_info_card *card=&info->cards[i];
        if(i==selected) panel(p,40,y,544,21,SELECTED);
        snprintf(line,sizeof(line),"%s",card->label);words(p,48,y+1,460,i==selected?WHITE:MUTED,line,false);
        snprintf(line,sizeof(line),"p%04u%s",(unsigned)card->pair,card->pair==info->default_pair?" Default":"");
        words(p,468,y+1,592,card->pair==info->default_pair?GREEN:MUTED,line,false);
    }
    if(!info->count) words(p,48,208,592,MUTED,s->games_vmu_loading?"Checking game cards...":
        info->enabled?"No imported versions; a game card is selected at launch.":"Virtual VMU is Off; physical cards are used.",false);
    vmu_scrollbar(p,start/KUI_VMU_WIZARD_ROWS,selected%KUI_VMU_WIZARD_ROWS,info->count);
    label(p,40,374,MUTED,info->candidates_sequence?
        "A uses this card for this launch; X saves your default.":"A single existing card is selected automatically.");
    words(p,40,398,608,info->known?CYAN:AMBER,s->games_vmu_notice[0]?s->games_vmu_notice:info->notice,false);
}
static void games_check(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Games / Check executable");
    words(p,40,140,608,CYAN,s->games_selected_path,false);
    panel(p,32,182,576,220,PANEL);
    const struct kui_app_status *status=v->app_status;
    words(p,48,204,592,status && status->passed?CYAN:AMBER,status && status->message[0]?status->message:
        "Check the selected executable without starting the game.",false);
    label(p,48,264,WHITE,"Uses your current options, including temporary baseline.");
    label(p,48,308,MUTED,"Checks layout, exact boot bytes, package and mapping.");
    label(p,48,332,MUTED,"Does not verify every sector or prove compatibility.");
    label(p,48,372,MUTED,"Preparation details are logged in Diagnostics.");
}
static void games_codec(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Games / ZSO codec");
    words(p,40,140,608,CYAN,s->games_selected_path,false);
    const char *names[]={"Standard LZ4","Historical LZO1X"};
    const char *details[]={"Standard ZSO files, including maxcso ZSO output.",
        "Legacy LZO1X ZSO files, including older GD ripper output."};
    for(unsigned i=0;i<2;i++) {
        unsigned y=184+i*78;panel(p,32,y,576,66,s->games_codec_selected==i?SELECTED:PANEL);
        label(p,48,y+10,WHITE,names[i]);words(p,48,y+34,592,MUTED,details[i],false);
    }
    label(p,40,354,MUTED,"Both dialects share the ZISO header. Select the known codec.");
    label(p,40,376,MUTED,"Failed data is never retried with another decoder.");
    game_options_notice(p,s);
}
static void games_extra_options(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Games / Advanced");
    words(p,40,140,608,CYAN,s->games_detail.title[0]?s->games_detail.title:s->games_selected_path,false);
    games_variant_line(p,s,158);
    bool native_reader=kui_shell_games_retail_ready(s) && !s->games_launch_cdda &&
        s->games_detail.format!=KUI_GAME_IMAGE_ZSO;
    const char *names[]={"Boot encoding",native_reader?"Background reader":"Background speed","Image information","Developer tests","RAM patches","Restore defaults","Check executable","Temporary baseline","Virtual card slot"};
    const char *encodings[]={"Automatic","Plain","Scrambled"};
    bool encoding=kui_shell_games_encoding_ready(s);
    bool speed=kui_shell_games_background_ready(s) && !s->games_launch_cdda;
    const char *values[]={encoding?encodings[s->games_launch_encoding<KUI_GAME_ENCODING_COUNT?s->games_launch_encoding:0]:"Automatic",
        speed?(native_reader?(s->games_launch_reader==KUI_GAME_READER_STANDARD?"Off":kui_shell_games_reader_label(s)):
            s->games_launch_reader==KUI_GAME_READER_EAGER?"Aggressive":"Normal"):"Unavailable","Open","Open",
        kui_shell_games_patches_ready(s)?(kui_game_ram_patches_enabled(&s->games_launch_patches)?"On":"Off"):"Unavailable","Apply",kui_shell_games_launch_ready(s)?"Run":"Unavailable",s->games_baseline?"On (unsaved)":"Off",
        kui_shell_games_vmu_slot_ready(s)?(s->games_launch_vmu_slot==KUI_GAME_VMU_SLOT_A1?"A1":"A2"):"Unavailable"};
    _Static_assert(sizeof(names)/sizeof(names[0])==KUI_SHELL_GAME_TOOL_COUNT &&
                   sizeof(values)/sizeof(values[0])==KUI_SHELL_GAME_TOOL_COUNT,"one name and value per Advanced row");
    for(unsigned i=0;i<KUI_SHELL_GAME_TOOL_COUNT;i++) {
        /* Nine rows end above the hint line at 374. */
        unsigned y=174+i*22;bool ready=i==KUI_SHELL_GAME_TOOL_ENCODING?encoding:i==KUI_SHELL_GAME_TOOL_SPEED?speed:
            i==KUI_SHELL_GAME_TOOL_PATCHES?kui_shell_games_patches_ready(s):
            (i==KUI_SHELL_GAME_TOOL_CHECK || i==KUI_SHELL_GAME_TOOL_BASELINE)?kui_shell_games_launch_ready(s):
            i==KUI_SHELL_GAME_TOOL_VMU_SLOT?kui_shell_games_vmu_slot_ready(s):true;
        panel(p,32,y,576,20,s->game_tools_selected==i?SELECTED:PANEL);
        label(p,48,y+4,ready?WHITE:MUTED,names[i]);
        words(p,350,y+4,592,ready?CYAN:MUTED,values[i],false);
    }
    const char *hints[]={"Automatic uses the inspected CD boot encoding.",
        native_reader?"Choose New (X), Legacy (Y), or Off with A or left/right.":
            "Aggressive gives the background reader more work per step.",
        "Disc tracks, image format and boot file details.","Read tests run separately from normal game launches.",
        "Two one-time boot patches. Native games with CD music Off only.",
        "Save detected defaults for this image, including turning RAM patches Off.",
        "Check the executable and launch package without starting the game.",
        "Use defaults without saving. Select again to restore choices.",
        "Background loading's virtual card: A1 for games that only save there. Keep it empty."};
    _Static_assert(sizeof(hints)/sizeof(hints[0])==KUI_SHELL_GAME_TOOL_COUNT,"one hint per Advanced row");
    const char *hint=hints[s->game_tools_selected<KUI_SHELL_GAME_TOOL_COUNT?s->game_tools_selected:0];
    if(s->games_detail.format==KUI_GAME_IMAGE_ZSO) {
        if(s->game_tools_selected==KUI_SHELL_GAME_TOOL_SPEED)
            hint="ZSO uses the Standard reader; background loading is unavailable.";
        else if(s->game_tools_selected==KUI_SHELL_GAME_TOOL_PATCHES)
            hint="RAM patches are unavailable for ZSO images.";
    }
    words(p,40,374,608,MUTED,hint,false);
    game_options_notice(p,s);
}
static void games_patches(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Games / RAM patches");
    words(p,40,140,608,CYAN,s->games_detail.title[0]?s->games_detail.title:s->games_selected_path,false);
    label(p,40,160,MUTED,"One-time 32-bit writes after the original boot file is checked.");
    for(unsigned row=0;row<KUI_GAME_RAM_PATCHES_COUNT*3u;row++) {
        unsigned slot=row/3u,field=row%3u,y=186+row*29u;
        const struct kui_game_ram_patch *patch=&s->games_launch_patches.entries[slot];
        bool selected=row==s->games_patch_selected;
        panel(p,32,y,576,25,selected?SELECTED:PANEL);
        char name[32],value[16];
        snprintf(name,sizeof(name),"Patch %u / %s",slot+1u,field==0?"Enabled":field==1?"Address":"Value");
        label(p,48,y+4,WHITE,name);
        if(!field) label(p,400,y+4,patch->enabled?AMBER:CYAN,patch->enabled?"On":"Off");
        else {
            uint32_t word=selected && s->games_patch_editing?s->games_patch_draft:field==1?patch->address:patch->value;
            snprintf(value,sizeof(value),"%08lX",(unsigned long)word);
            label(p,400,y+4,CYAN,value);
            if(selected && s->games_patch_editing) {
                unsigned digit=s->games_patch_digit<8?s->games_patch_digit:0;
                char prefix[9];memcpy(prefix,value,digit);prefix[digit]=0;
                char glyph[2]={value[digit],0};
                box(p,400+word_width(p,prefix,false),y+21,word_width(p,glyph,false),2,AMBER);
            }
        }
    }
    char range[100];
    uint32_t last=s->games_detail.boot_bytes>=4?KUI_GAME_RAM_PATCH_EXEC_ADDRESS+s->games_detail.boot_bytes-4u:KUI_GAME_RAM_PATCH_EXEC_ADDRESS;
    snprintf(range,sizeof(range),"Boot range: %08lX - %08lX (4-byte aligned)",
        (unsigned long)KUI_GAME_RAM_PATCH_EXEC_ADDRESS,(unsigned long)(last&~UINT32_C(3)));
    label(p,40,370,MUTED,range);
    game_options_notice(p,s);
}
static void games_tests(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Games / Developer tests");
    words(p,40,140,608,CYAN,s->games_tests_from_options?s->games_selected_path:"SD loader diagnostics",false);
    const char *names[]={"Image read test","Loader read test"};
    const char *hints[]={"Read samples from the selected GDI.","Run the standalone SD loader test program."};
    for(unsigned i=0;i<2;i++) {
        unsigned y=186+i*72;bool ready=i || (kui_shell_games_image_ready(s) &&
            s->games_detail.format==KUI_GAME_IMAGE_GDI);
        panel(p,32,y,576,62,s->game_tests_selected==i?SELECTED:PANEL);
        label(p,48,y+8,ready?WHITE:MUTED,names[i]);
        label(p,48,y+34,MUTED,ready?hints[i]:"Requires an inspected GDI image.");
    }
    label(p,40,360,MUTED,"These tests do not start a game.");
    label(p,40,392,MUTED,"Each test has a separate confirmation before it runs.");
}
/* ---- File Manager ---- */
/* Long paths keep their end, the folder you are in, behind "...". */
static void path_words(struct paint *p,unsigned x,unsigned y,unsigned right,uint16_t color,const char *path) {
    const char *start=path?path:"";
    unsigned width=kui_shell_font_width(start,false),room=right>x?right-x:0;
    bool clipped=false;
    if(width>room) {
        unsigned dots=kui_shell_font_width("...",false)+KUI_SHELL_FONT_LETTER_SPACING;
        room=room>dots?room-dots:0;
        while(*start && width>room) {
            char one[2]={*start,0};
            unsigned step=kui_shell_font_width(one,false)+KUI_SHELL_FONT_LETTER_SPACING;
            width=width>step?width-step:0;
            ++start;
            while(((unsigned char)*start&0xc0u)==0x80u) ++start;
        }
        clipped=true;
    }
    char shown[KUI_FILES_PATH_CAP+4];
    snprintf(shown,sizeof(shown),"%s%.*s",clipped?"...":"",(int)(KUI_FILES_PATH_CAP-1),start);
    words(p,x,y,right,color,shown,false);
}
/* DIR, or the extension in capitals (four letters at most), or FILE. */
static void files_tag(char out[8],const struct kui_files_entry *e) {
    const char *dot=e->directory?NULL:strrchr(e->name,'.');
    if(e->directory) {snprintf(out,8,"DIR");return;}
    if(!dot || dot==e->name || !dot[1] || strlen(dot+1)>4) {snprintf(out,8,"FILE");return;}
    unsigned n=0;
    for(const char *c=dot+1;*c && n<4;c++) out[n++]=(char)(*c>='a' && *c<='z'?*c-32:*c);
    out[n]=0;
}
static void files_progress(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v,unsigned top) {
    const struct kui_app_status *st=v->app_status;
    enum kui_files_op op=s->files_job.op;
    panel(p,32,top,576,392-top,PANEL);
    label(p,48,top+12,CYAN,op==KUI_FILES_OP_COPY?"Copying":op==KUI_FILES_OP_MOVE?"Moving":
        op==KUI_FILES_OP_DELETE?"Deleting":op==KUI_FILES_OP_RENAME?"Renaming":op==KUI_FILES_OP_MKDIR?
        "Creating folder":"Checking");
    words(p,48,top+36,592,WHITE,st && st->message[0]?st->message:s->files_notice,false);
    box(p,48,top+62,544,10,EDGE);
    if(st && st->total) box(p,48,top+62,(unsigned)(544u*(st->done<st->total?st->done:st->total)/st->total),10,CYAN);
    if(st && st->total && op==KUI_FILES_OP_COPY) {
        unsigned tenths=kui_shell_progress_tenths(st->done,st->total);
        char line[64];
        snprintf(line,sizeof(line),"%u.%u%%: each file is copied, then read back",tenths/10u,tenths%10u);
        label(p,48,top+82,MUTED,line);
    }
    label(p,48,368,MUTED,op==KUI_FILES_OP_COPY?"B stops safely; the partial copy is removed.":
        op==KUI_FILES_OP_DELETE?"B stops; items already deleted stay deleted.":"B stops safely.");
}
static void files(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_files_page *l=&s->files_listing;
    unsigned count=l->count<KUI_FILES_ROWS?l->count:KUI_FILES_ROWS;
    title(p,40,108,"File Manager");
    path_words(p,40,136,608,CYAN,s->files_path);
    label(p,40,160,v->busy?MUTED:WHITE,"A Open   X Actions   Y New folder   R Refresh");
    if(v->busy && s->files_running) {files_progress(p,s,v,190);return;}
    panel(p,32,190,576,178,PANEL);
    if(!count) label(p,48,206,MUTED,v->busy?"Reading folder...":l->message[0]?l->message:"This folder is empty.");
    for(unsigned i=0;i<count;i++) {
        const struct kui_files_entry *e=&l->entries[i];
        unsigned y=196+i*21;
        bool chosen=i==s->files_selected;
        if(chosen) panel(p,40,y,560,21,SELECTED);
        char tag[8],size[16];
        files_tag(tag,e);
        uint16_t ink=e->disabled?AMBER:chosen?WHITE:MUTED;
        label(p,50,y+1,e->disabled?AMBER:e->directory?CYAN:MUTED,tag);
        words(p,100,y+1,e->directory?590:500,ink,e->name,false);
        if(!e->directory) {
            kui_files_size_text(size,e->bytes);
            unsigned width=kui_shell_font_width(size,false);
            words(p,590-width,y+1,592,chosen?WHITE:MUTED,size,false);
        }
    }
    const char *notice=s->files_notice[0]?s->files_notice:count?l->message:"";
    label(p,40,378,s->files_notice[0]&&s->files_notice_error?AMBER:CYAN,notice);
    char line[160];
    if(s->files_notice_detail[0]) snprintf(line,sizeof(line),"%s",s->files_notice_detail);
    else if(count) {
        const struct kui_files_entry *e=s->files_selected<count?&l->entries[s->files_selected]:&l->entries[0];
        char when[20];
        kui_files_date_text(when,e->date,e->time);
        snprintf(line,sizeof(line),"Items %u-%u of %u   Modified %s%s%s",l->before+1,l->before+count,l->total,when,
            e->attributes&AM_RDO?"   Read-only":"",e->attributes&AM_HID?"   Hidden":"");
    } else line[0]=0;
    label(p,40,396,MUTED,line);
}
static void files_actions(struct paint *p,const struct kui_shell *s) {
    static const char *const names[]={"Open","Copy to another folder","Move to another folder","Rename",
        "Delete","Details","New folder here"};
    static const char *const notes[]={"Open the folder, game, song or picture.",
        "Choose a folder; the copy is read back and checked.",
        "Choose a folder; moving on the same card is instant.",
        "Type a new name with the on-screen keyboard.",
        "Review what will be deleted, then confirm.",
        "Size, date, and what a folder holds.",
        "Create a folder in this folder."};
    const struct kui_files_entry *e=s->files_selected<s->files_listing.count?&s->files_listing.entries[s->files_selected]:NULL;
    char path[KUI_FILES_PATH_CAP];
    bool usable=e && !e->disabled && kui_files_join(path,s->files_path,e->name);
    bool locked=usable && kui_files_protected(path);
    unsigned selected=s->files_action_selected<7?s->files_action_selected:0;
    title(p,40,108,"File actions");
    words(p,40,138,608,CYAN,e?e->name:"No item selected",false);
    for(unsigned i=0;i<7;i++) {
        unsigned y=164+i*28;
        bool off=i!=6 && (!usable || (locked && i>=2 && i<=4));
        panel(p,32,y,576,24,selected==i?SELECTED:PANEL);
        if(selected==i) box(p,32,y+4,3,16,PINK);
        label(p,48,y+3,off?AMBER:selected==i?WHITE:MUTED,names[i]);
    }
    label(p,40,366,MUTED,notes[selected]);
    label(p,40,390,s->files_notice[0]?AMBER:locked?PINK:MUTED,s->files_notice[0]?s->files_notice:
        locked?"K-UI needs this to start: move, rename and delete are off.":!usable?
        "K-UI cannot use this name; rename it on a computer.":"");
}
static void files_pick(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_files_page *l=&s->files_pick;
    bool copy=s->files_job.op==KUI_FILES_OP_COPY;
    unsigned count=l->count<KUI_FILES_ROWS?l->count:KUI_FILES_ROWS;
    title(p,40,108,s->files_pick_import?"Choose an external save":
        s->files_pick_compare?(s->system_compare_bios?"Choose a BIOS image":"Choose a settings-flash image"):
        copy?"Copy to a folder":"Move to a folder");
    path_words(p,40,138,608,CYAN,s->files_pick_path);
    panel(p,32,166,576,204,PANEL);
    if(!count) label(p,48,188,MUTED,v->busy?"Reading folder...":l->message[0]?l->message:(s->files_pick_compare || s->files_pick_import)?"No files here.":"No folders here.");
    for(unsigned i=0;i<count;i++) {
        const struct kui_files_entry *e=&l->entries[i];
        unsigned y=174+i*23;
        if(i==s->files_pick_selected) {panel(p,40,y,560,23,SELECTED);box(p,40,y+4,3,15,PINK);}
        bool wrong_size=s->files_pick_compare && !e->directory &&
            e->bytes!=(s->system_compare_bios?2097152u:131072u);
        words(p,52,y+2,(s->files_pick_compare || s->files_pick_import)?492:608,e->disabled || wrong_size?AMBER:
            i==s->files_pick_selected?WHITE:MUTED,e->name,false);
        if(s->files_pick_compare || s->files_pick_import) {
            char size[16];kui_files_size_text(size,e->bytes);
            label(p,504,y+2,MUTED,e->directory?"Folder":size);
        }
    }
    label(p,40,376,v->busy?MUTED:WHITE,s->files_pick_import?"A Open folder / check save":
        s->files_pick_compare?"A Open folder / compare file":
        copy?"A Open folder   Y Copy here":"A Open folder   Y Move here");
    char line[180];
    if(count && l->message[0]) snprintf(line,sizeof(line),"%s",l->message);
    else if(s->files_pick_import) snprintf(line,sizeof(line),"Choose VMS, VMI or DCI. Import creates an SD backup only.");
    else if(s->files_pick_compare) snprintf(line,sizeof(line),"%s",s->system_compare_bios?
        "Exact 2 MiB image required. Comparison is read-only.":
        "Exact 128 KiB image required. Comparison is read-only.");
    else snprintf(line,sizeof(line),"%s %s",copy?"Copying":"Moving",kui_files_leaf(s->files_job.source));
    label(p,40,396,count && l->message[0]?AMBER:MUTED,line);
}
static void files_summary(char out[96],const struct kui_files_preview *pv) {
    char size[16];
    kui_files_size_text(size,pv->bytes);
    if(pv->directory) snprintf(out,96,"Holds %u file%s and %u folder%s, %s",pv->files,pv->files==1?"":"s",
        pv->folders,pv->folders==1?"":"s",size);
    else snprintf(out,96,"%s (%llu bytes)",size,(unsigned long long)pv->bytes);
}
static void files_confirm(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_files_preview *pv=&s->files_preview;
    enum kui_files_op op=s->files_job.op;
    const char *verb=op==KUI_FILES_OP_COPY?"COPY":op==KUI_FILES_OP_MOVE?"MOVE":"DELETE";
    char line[200],summary[96];
    title(p,40,108,op==KUI_FILES_OP_COPY?"Copy":op==KUI_FILES_OP_MOVE?"Move":"Delete");
    box(p,32,136,576,272,NAVY);panel(p,48,142,544,258,PANEL);box(p,52,146,536,4,PINK);
    words(p,72,196,568,CYAN,kui_files_leaf(s->files_job.source),false);
    if(v->busy) {
        label(p,72,166,WHITE,"CHECKING...");
        words(p,72,222,568,MUTED,v->app_status && v->app_status->message[0]?v->app_status->message:pv->status.message,false);
        label(p,72,352,MUTED,"B stops the check");
        return;
    }
    if(!kui_shell_files_ready(s)) {
        snprintf(line,sizeof(line),"CANNOT %s THIS ITEM",verb);
        label(p,72,166,AMBER,line);
        words(p,72,230,568,AMBER,pv->status.message[0]?pv->status.message:"The check did not finish.",false);
        label(p,384,352,WHITE,"B Back");
        return;
    }
    snprintf(line,sizeof(line),"%s THIS %s?",verb,pv->directory?"FOLDER":"FILE");
    label(p,72,166,WHITE,line);
    if(op==KUI_FILES_OP_MOVE && pv->directory) snprintf(summary,sizeof(summary),"The folder and everything in it");
    else files_summary(summary,pv);
    label(p,72,222,MUTED,summary);
    if(op==KUI_FILES_OP_DELETE) {
        label(p,72,250,AMBER,"This cannot be undone.");
        if(pv->read_only) {
            snprintf(line,sizeof(line),"Includes %u read-only item%s.",pv->read_only,pv->read_only==1?"":"s");
            label(p,72,276,MUTED,line);
        }
        label(p,72,302,MUTED,"B stops part way; items already deleted stay deleted.");
        label(p,72,352,CYAN,"A Delete");
    } else {
        char where[KUI_FILES_PATH_CAP+8];
        unsigned y=276;
        snprintf(where,sizeof(where),"To: %s",s->files_job.target);
        path_words(p,72,250,568,MUTED,where);
        if(pv->renamed) {
            snprintf(line,sizeof(line),"That name is taken there; it will be %s",s->files_job.name);
            words(p,72,y,568,AMBER,line,false);
            y+=24;
        }
        if(op==KUI_FILES_OP_COPY && pv->free_known) {
            char free_text[16];
            kui_files_size_text(free_text,pv->free_bytes);
            snprintf(line,sizeof(line),"%s free on the card.",free_text);
            label(p,72,y,MUTED,line);
            y+=24;
        }
        label(p,72,y,MUTED,op==KUI_FILES_OP_COPY?"The copy is read back and checked; B stops safely.":
            "Moving on the same card is instant.");
        label(p,72,352,CYAN,op==KUI_FILES_OP_COPY?"A Copy":"A Move");
    }
    label(p,384,352,WHITE,"B Cancel");
}
static void files_info(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_files_preview *pv=&s->files_preview;
    const char *path=s->files_job.source,*name=kui_files_leaf(path);
    char line[200],size[16],when[20];
    title(p,40,108,"Details");
    words(p,40,140,608,CYAN,name,false);
    panel(p,32,168,576,232,PANEL);
    if(v->busy || !pv->status.complete) {
        label(p,48,184,CYAN,"Reading details...");
        if(v->app_status && v->app_status->message[0]) words(p,48,210,592,MUTED,v->app_status->message,false);
        return;
    }
    if(!pv->status.passed) {
        words(p,48,184,592,AMBER,pv->status.message,false);
        return;
    }
    enum kui_files_kind kind=kui_files_kind(name,pv->directory);
    const char *dot=strrchr(name,'.');
    snprintf(line,sizeof(line),"Type: %s",kind==KUI_FILES_KIND_FOLDER?"Folder":kind==KUI_FILES_KIND_GDI?"GDI game image":
        kind==KUI_FILES_KIND_AUDIO?"Music (WAV or Ogg)":kind==KUI_FILES_KIND_PICTURE?"Picture":
        dot && dot!=name && dot[1]?"File":"File without an extension");
    label(p,48,180,WHITE,line);
    kui_files_size_text(size,pv->bytes);
    if(pv->directory) snprintf(line,sizeof(line),"Holds %u file%s and %u folder%s, %s in all",pv->files,pv->files==1?"":"s",
        pv->folders,pv->folders==1?"":"s",size);
    else snprintf(line,sizeof(line),"Size: %s (%llu bytes)",size,(unsigned long long)pv->bytes);
    words(p,48,206,592,WHITE,line,false);
    kui_files_date_text(when,pv->date,pv->time);
    snprintf(line,sizeof(line),"Modified: %s",when);
    label(p,48,232,WHITE,line);
    snprintf(line,sizeof(line),"Attributes: %s%s%s%s",pv->attributes&(AM_RDO|AM_HID|AM_SYS)?"":"None",
        pv->attributes&AM_RDO?"Read-only ":"",pv->attributes&AM_HID?"Hidden ":"",pv->attributes&AM_SYS?"System":"");
    label(p,48,258,WHITE,line);
    char parent[KUI_FILES_PATH_CAP],where[KUI_FILES_PATH_CAP+8];
    if(!kui_files_parent(parent,path)) parent[0]=0;
    snprintf(where,sizeof(where),"In: %s",parent);
    path_words(p,48,284,592,MUTED,where);
    if(kui_files_protected(path)) label(p,48,318,PINK,"K-UI needs this to start; it cannot be moved, renamed or deleted.");
    else if(kui_files_part_name(name)) label(p,48,318,AMBER,"A copy K-UI did not finish; it can be deleted.");
    if(pv->directory && pv->read_only) {
        snprintf(line,sizeof(line),"%u read-only item%s inside.",pv->read_only,pv->read_only==1?"":"s");
        label(p,48,344,MUTED,line);
    }
}
static void files_view(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_files_picture *pic=&s->files_picture;
    title(p,40,108,kui_files_leaf(pic->path));
    if(!pic->ok && !v->busy) {
        panel(p,32,136,576,120,PANEL);
        words(p,48,152,592,AMBER,pic->message[0]?pic->message:"This picture could not be shown.",false);
        label(p,48,180,MUTED,"PNG, JPEG and 16-bit PVR pictures up to 3 MB can be shown.");
        label(p,48,206,MUTED,"B returns to the File Manager.");
        return;
    }
    box(p,38,134,KUI_FILES_PICTURE_EDGE+4,KUI_FILES_PICTURE_EDGE+4,EDGE);
    if(pic->ok && v->files_picture) cover_image(p,40,136,KUI_FILES_PICTURE_EDGE,v->files_picture);
    else box(p,40,136,KUI_FILES_PICTURE_EDGE,KUI_FILES_PICTURE_EDGE,PANEL);
    if(!pic->ok) {label(p,336,140,CYAN,"Loading picture...");return;}
    char line[160],size[16];
    label(p,336,140,CYAN,pic->format && pic->format[0]?pic->format:"Picture");
    snprintf(line,sizeof(line),"%u x %u pixels",pic->width,pic->height);
    label(p,336,166,WHITE,line);
    kui_files_size_text(size,pic->bytes);
    snprintf(line,sizeof(line),"File: %s",size);
    label(p,336,192,WHITE,line);
    char parent[KUI_FILES_PATH_CAP];
    if(kui_files_parent(parent,pic->path)) path_words(p,336,218,608,MUTED,parent);
    label(p,336,258,MUTED,"Shown fitted to this square.");
}
/* FTP server: the address and password large, then each client's
 * transfer, the session's totals and its latest event. */
static void ftp_client(struct paint *p,const struct kui_ftp_client *c,unsigned y) {
    char line[160],done[16],total[16],rate[16];
    snprintf(line,sizeof(line),"%u.%u.%u.%u",c->ip[0],c->ip[1],c->ip[2],c->ip[3]);
    label(p,48,y,WHITE,line);
    if(!c->sending && !c->receiving) {
        words(p,184,y,592,MUTED,c->logged_in?"Logged in, waiting":"Connected, not logged in",false);
        return;
    }
    snprintf(line,sizeof(line),"%s %s",c->sending?"Sending":"Receiving",c->name);
    words(p,184,y,480,CYAN,line,false);
    kui_files_size_text(rate,c->rate);
    snprintf(line,sizeof(line),"%s/s",rate);
    words(p,488,y,592,MUTED,line,false);
    kui_files_size_text(done,c->done);
    if(c->total) {
        kui_files_size_text(total,c->total);
        unsigned tenths=kui_shell_progress_tenths(c->done,c->total);
        box(p,48,y+26,296,6,EDGE);
        box(p,48,y+26,(unsigned)(296u*(c->done<c->total?c->done:c->total)/c->total),6,CYAN);
        snprintf(line,sizeof(line),"%u%%  %s of %s",tenths/10u,done,total);
        words(p,356,y+19,592,MUTED,line,false);
    } else {
        snprintf(line,sizeof(line),"%s received so far",done);
        words(p,48,y+19,592,MUTED,line,false);
    }
}
static void ftp_page(struct paint *p,const struct kui_shell_view *v) {
    const struct kui_ftp_status *f=v->ftp;
    char line[160];
    title(p,40,108,"FTP Server");
    if(!f || f->state==KUI_FTP_STARTING) {
        panel(p,32,140,576,112,PANEL);
        label(p,48,152,CYAN,f && f->message[0]?f->message:"Starting");
        label(p,48,180,MUTED,"It needs the Wi-Fi board, or a W5500 and a cable to your router,");
        label(p,48,200,MUTED,"on the SCI port. The SD card stays on SCIF. B stops it at any time.");
        return;
    }
    bool ready=f->state==KUI_FTP_READY;
    panel(p,32,136,576,80,PANEL);
    if(ready) {
        if(f->port==KUI_FTP_PORT) snprintf(line,sizeof(line),"ftp://%u.%u.%u.%u",f->ip[0],f->ip[1],f->ip[2],f->ip[3]);
        else snprintf(line,sizeof(line),"ftp://%u.%u.%u.%u:%u",f->ip[0],f->ip[1],f->ip[2],f->ip[3],f->port);
        words(p,48,141,592,CYAN,line,true);
        snprintf(line,sizeof(line),"User: kui   Password: %s",f->password);
        words(p,48,167,592,WHITE,line,true);
        label(p,48,194,f->link?MUTED:AMBER,f->link?f->adapter:f->wifi?"Wi-Fi connection lost; the board is reconnecting":
            "No cable link: check the network cable");
    } else {
        bool failed=f->state==KUI_FTP_FAILED;
        words(p,48,141,592,failed?AMBER:CYAN,failed?"The FTP server stopped":"The FTP server is off",true);
        label(p,48,170,failed?AMBER:MUTED,f->message);
        label(p,48,192,MUTED,"A starts it again. B returns to Network.");
    }
    panel(p,32,222,576,124,PANEL);
    unsigned rows=0;
    for(unsigned i=0;i<KUI_FTP_SESSIONS;i++) {
        if(!f->clients[i].active) continue;
        ftp_client(p,&f->clients[i],228+rows*40);
        ++rows;
    }
    if(!rows) {
        label(p,48,232,MUTED,ready?"No client connected yet. Connect an FTP client to the":"No clients connected.");
        if(ready) {
            label(p,48,252,MUTED,"address above, in passive mode (the usual setting).");
            label(p,48,284,MUTED,"Plain FTP is not encrypted: use it on your home network.");
        }
    }
    char in[16],out[16];
    kui_files_size_text(in,f->bytes_in);
    kui_files_size_text(out,f->bytes_out);
    snprintf(line,sizeof(line),"Received %u file%s (%s), sent %u (%s)",f->files_in,f->files_in==1?"":"s",in,
        f->files_out,out);
    words(p,200,114,608,MUTED,line,false);
    /* The last upload's and download's speeds stay until the next one the
     * same way ends; recent events, newest first, fill the rest. */
    const char *lines[3];
    uint16_t colors[3];
    unsigned count=0;
    if(f->last_in[0]) {lines[count]=f->last_in;colors[count++]=CYAN;}
    if(f->last_out[0]) {lines[count]=f->last_out;colors[count++]=CYAN;}
    for(unsigned i=0;i<f->event_count && count<3;i++) {lines[count]=f->events[i];colors[count++]=i?MUTED:WHITE;}
    for(unsigned i=0;i<count;i++) label(p,40,352+i*21,colors[i],lines[i]);
}
/* Four bars for a network's signal. */
static void signal_bars(struct paint *p,unsigned x,unsigned y,int8_t rssi,bool selected) {
    unsigned bars=rssi>=-55?4u:rssi>=-67?3u:rssi>=-75?2u:1u;
    for(unsigned i=0;i<4;i++) box(p,x+i*6,y+13-(i+1)*3,4,(i+1)*3,i<bars?(selected?WHITE:CYAN):EDGE);
}
#define WIFI_ROWS 8u
static void wifi_page(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    const struct kui_wifi_view *w=&s->wifi;
    const struct kwh_wifi *st=&w->wifi;
    char line[128];
    title(p,40,108,"Wi-Fi");
    if(!w->found) {
        label(p,40,138,MUTED,"The Wi-Fi board on the SCI port (XIAO ESP32-C5 with K-UI's firmware).");
        panel(p,32,164,576,200,PANEL);
        label(p,48,176,w->failed?AMBER:CYAN,w->message[0]?w->message:"Looking for the Wi-Fi board");
        if(!w->working && w->failed) {
            label(p,48,210,MUTED,"Its select goes to GPIO6 (or GPIO7), READY to GPIO5, and it takes");
            label(p,48,230,MUTED,"5 V. Load its firmware first: see firmware/kui-wifi on GitHub.");
            label(p,48,264,MUTED,"X looks again. A W5500 on the port is not affected.");
        }
        return;
    }
    label(p,40,136,MUTED,w->board);
    if(w->firmware_updating) {
        panel(p,32,174,576,190,PANEL);
        label(p,48,190,CYAN,"Updating the Wi-Fi board");
        label(p,48,222,w->failed?AMBER:WHITE,w->message);
        char done[16],total[16];
        kui_files_size_text(done,w->firmware_done);
        kui_files_size_text(total,w->firmware_total);
        snprintf(line,sizeof(line),"%s / %s",done,total);
        label(p,48,258,MUTED,line);
        unsigned filled=w->firmware_total?(unsigned)((uint64_t)w->firmware_done*536u/w->firmware_total):0;
        if(filled>536u) filled=536u;
        box(p,48,284,536,12,EDGE);
        if(filled) box(p,48,284,filled,12,CYAN);
        label(p,48,320,MUTED,w->firmware_committing?"Finishing and checking the restarted board...":
            "B stops before the new firmware is activated.");
        return;
    }
    uint16_t color=st->state==KWM_WIFI_ONLINE?CYAN:st->state==KWM_WIFI_BAD_PASSWORD||st->state==KWM_WIFI_NOT_FOUND?AMBER:WHITE;
    if(st->state==KWM_WIFI_ONLINE) snprintf(line,sizeof(line),"Online on %.32s as %u.%u.%u.%u (%s GHz, %d dBm)",st->ssid,
        st->ip[0],st->ip[1],st->ip[2],st->ip[3],st->band==5?"5":"2.4",st->rssi);
    else if(st->state==KWM_WIFI_IDLE) snprintf(line,sizeof(line),"%s",st->saved?"Wi-Fi is off: choose a network below":
        "No network set up: choose one below");
    else snprintf(line,sizeof(line),"%.32s: %s",st->ssid,kui_wifi_state_text(st->state));
    label(p,40,158,color,line);
    unsigned rows=kui_shell_wifi_rows(s),selected=s->wifi_selected<rows?s->wifi_selected:0;
    unsigned first=selected>=WIFI_ROWS?selected-WIFI_ROWS+1:0;
    panel(p,32,182,576,190,PANEL);
    for(unsigned r=first;r<rows && r<first+WIFI_ROWS;r++) {
        unsigned y=188+(r-first)*22;
        bool chosen=r==selected;
        uint16_t text=chosen?WHITE:MUTED;
        if(chosen) panel(p,40,y,560,22,SELECTED);
        if(!r) {
            snprintf(line,sizeof(line),"Bands: %s",kui_wifi_band_text(st->band_mode));
            words(p,52,y+2,400,text,line,false);
            words(p,452,y+2,592,chosen?CYAN:MUTED,"LEFT/RIGHT",false);
        } else if(r==rows-1) {
            words(p,52,y+2,592,text,"Other network (type its name)",false);
        } else {
            const struct kui_wifi_network *n=&w->networks[r-1];
            bool here=st->state==KWM_WIFI_ONLINE && !strcmp(n->ssid,st->ssid);
            words(p,52,y+2,316,here?CYAN:text,n->ssid,false);
            snprintf(line,sizeof(line),"%s GHz ch %u",n->five?"5":"2.4",n->channel);
            words(p,324,y+2,440,text,line,false);
            signal_bars(p,448,y+4,n->rssi,chosen);
            words(p,482,y+2,592,text,here?"Joined":kui_wifi_security_text(n->security),false);
        }
    }
    if(!w->count) label(p,52,188+22,MUTED,v->busy?"Looking for networks...":"No networks listed yet: X scans.");
    label(p,40,380,w->failed?AMBER:v->busy?CYAN:MUTED,w->message);
    if(rows>WIFI_ROWS) {
        snprintf(line,sizeof(line),"%u networks",w->count);
        label(p,500,380,MUTED,line);
    }
}
void kui_shell_draw_content(uint16_t *frame, const struct kui_shell *s,
        const struct kui_shell_view *v, kui_shell_text_fn text, void *ctx) {
    if(!frame || !s || !v) return;
    struct paint p={.fb=frame,.text=text,.ctx=ctx,.crisp=false};
    heading(&p,s,v);
    switch(s->page) {
    case KUI_SHELL_HOME: home(&p,s,v); break;
    case KUI_SHELL_DISC_TOOLS: disc_tools(&p,s,v); break;
    case KUI_SHELL_RIPPER: ripper(&p,s,v); break;
    case KUI_SHELL_SETTINGS: system_settings(&p,s,v); break;
    case KUI_SHELL_RIPPER_SETTINGS: ripper_settings(&p,s,v); break;
    case KUI_SHELL_DIAGNOSTICS: diagnostics(&p,s,v); break;
    case KUI_SHELL_STORAGE_TESTS: storage_tests(&p,s,v); break;
    case KUI_SHELL_STORAGE_TEST_HISTORY: storage_test_history(&p,s,v); break;
    case KUI_SHELL_SCI_ASYNC_PROBE: sci_async_probe(&p,v); break;
    case KUI_SHELL_DESTINATION: destination(&p,s,v); break;
    case KUI_SHELL_KEYBOARD: keyboard(&p,s); break;
    case KUI_SHELL_ADVANCED: advanced(&p,s); break;
    case KUI_SHELL_VMU: vmu_page(&p,s,v); break;
    case KUI_SHELL_VMU_TOOLS: vmu_tools(&p,s); break;
    case KUI_SHELL_VMU_WIZARD: vmu_wizard(&p,s,v); break;
    case KUI_SHELL_GAMES_VMU_CARD: games_vmu_card_picker(&p,s); break;
    case KUI_SHELL_VMU_ACTIONS: vmu_actions(&p,s,v); break;
    case KUI_SHELL_SALVAGE: salvage(&p,s,v); break;
    case KUI_SHELL_SALVAGE_DRIVE: salvage_drive(&p,s); break;
    case KUI_SHELL_SALVAGE_OPTIONS: salvage_options(&p,s); break;
    case KUI_SHELL_SYSTEM_TOOLS: system_tools(&p,s,v); break;
    case KUI_SHELL_VMU_RESTORE: vmu_restore(&p,s,v); break;
    case KUI_SHELL_VMU_COLLECTION: vmu_collection(&p,s,v); break;
    case KUI_SHELL_VMU_CARD: vmu_card(&p,s); break;
    case KUI_SHELL_VMU_IMAGE: vmu_image(&p,s,v); break;
    case KUI_SHELL_VMU_IMPORT: vmu_import(&p,s,v); break;
    case KUI_SHELL_CLOCK: clock_page(&p,s); break;
    case KUI_SHELL_CRC_SCAN: crc_scan(&p,s,v); break;
    case KUI_SHELL_GD_PLAY: gd_play(&p,v); break;
    case KUI_SHELL_CD_AUDIO: cd_audio(&p,s,v); break;
    case KUI_SHELL_MUSIC: music_player(&p,s,v); break;
    case KUI_SHELL_GAMES: games(&p,s,v); break;
    case KUI_SHELL_GAMES_DETAIL: game_detail(&p,s,v); break;
    case KUI_SHELL_GAMES_VARIANTS: games_variants(&p,s); break;
    case KUI_SHELL_GAMES_ADVANCED: games_advanced(&p,s); break;
    case KUI_SHELL_GAMES_PROBE_CONFIRM: games_probe_confirmation(&p,v); break;
    case KUI_SHELL_GAMES_IMAGE_PROBE_CONFIRM: games_image_probe_confirmation(&p,s,v); break;
    case KUI_SHELL_GAMES_RETAIL_CONFIRM: games_retail_confirmation(&p,s,v); break;
    case KUI_SHELL_GAMES_LAUNCH_OPTIONS: games_launch_options(&p,s); break;
    case KUI_SHELL_GAMES_EXTRA_OPTIONS: games_extra_options(&p,s); break;
    case KUI_SHELL_GAMES_CHECK: games_check(&p,s,v); break;
    case KUI_SHELL_GAMES_CODEC: games_codec(&p,s); break;
    case KUI_SHELL_GAMES_PATCHES: games_patches(&p,s); break;
    case KUI_SHELL_GAMES_TESTS: games_tests(&p,s); break;
    case KUI_SHELL_GAMES_INFO: games_information(&p,s); break;
    case KUI_SHELL_MEMORY: case KUI_SHELL_NETWORK: utility_page(&p,s,v); break;
    case KUI_SHELL_FILES: files(&p,s,v); break;
    case KUI_SHELL_FILES_ACTIONS: files_actions(&p,s); break;
    case KUI_SHELL_FILES_PICK: files_pick(&p,s,v); break;
    case KUI_SHELL_FILES_CONFIRM: files_confirm(&p,s,v); break;
    case KUI_SHELL_FILES_INFO: files_info(&p,s,v); break;
    case KUI_SHELL_FILES_VIEW: files_view(&p,s,v); break;
    case KUI_SHELL_FTP: ftp_page(&p,v); break;
    case KUI_SHELL_WIFI: wifi_page(&p,s,v); break;
    }
    footer(&p,s,v);
    if(s->confirm_new || s->confirm_quick_resume) confirmation(&p,s,s->confirm_quick_resume);
    if(s->confirm_gd_boot) gd_boot_confirmation(&p);
    if(s->confirm_storage_test) storage_test_confirmation(&p,s,v);
    if(s->confirm_clock || s->confirm_defaults || s->confirm_vmu_restore || s->confirm_vmu_delete ||
       s->confirm_vmu_copy || s->confirm_vmu_copy_all || s->confirm_music_clear || s->confirm_restart || s->confirm_salvage || s->confirm_salvage_check || s->confirm_repair_in_place ||
       s->confirm_wifi_forget || s->confirm_wifi_update) app_confirmation(&p,s);
    if(v->video_trial) video_trial(&p,v);
}

void kui_shell_draw(uint16_t *frame, const struct kui_shell *s,
        const struct kui_shell_view *v, kui_shell_text_fn text, void *ctx) {
    if(!frame || !s || !v) return;
    struct paint p={.fb=frame,.text=text,.ctx=ctx,.crisp=false};
    box(&p,0,0,640,480,NAVY);
    kui_shell_draw_content(frame,s,v,text,ctx);
}
