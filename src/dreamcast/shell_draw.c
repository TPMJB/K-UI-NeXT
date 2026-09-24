/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell.h"
#include "kui/shell_font.h"
#include "shell_art.inc"
#include <stdio.h>
#include <string.h>

/* Original K-UI palette and split-pane launcher, drawn independently of the
 * legacy UI framework. Artwork and font masks are embedded: no device I/O,
 * decoding or allocation while the capture worker is active. */
enum { NAVY=0x0864, PANEL=0x10c6, SELECTED=0x494b, EDGE=0x318a,
    WHITE=0xf7bf, MUTED=0xadd9, CYAN=0x675e, PINK=0xf3fb,
    GREEN=0x8ef6, AMBER=0xfda9 };
struct paint { uint16_t *fb; kui_shell_text_fn text; void *ctx; };
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
    while(n && kui_shell_font_width(clipped,large)>right-x) {
        clipped[--n]='\0'; truncated=true;
    }
    if(truncated) {
        while(n && (n+4>sizeof(clipped) ||
                kui_shell_font_width(clipped,large)+kui_shell_font_width("...",large)>right-x))
            clipped[--n]='\0';
        if(n+4<=sizeof(clipped) && kui_shell_font_width("...",large)<=right-x)
            memcpy(clipped+n,"...",4);
    }
    kui_shell_font_draw(p->fb,(int)x,(int)y,color,clipped,large);
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
    words(p,176,26,396,WHITE,"Katana UI",true);
    words(p,176,52,396,MUTED,"by TPMJB / SD runtime",false);
    words(p,176,74,396,CYAN,state(v),false);
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
    label(p,416,74,MUTED,build); rule(p,98);
}
static void footer(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    rule(p,416);
    bool song_page=s->page==KUI_SHELL_HOME || s->page==KUI_SHELL_RIPPER;
    const char *controls=v->video_trial ? "A Keep mode   B Revert" :
        v->busy ? (song_page?"B Stop safely   L/R Songs":"B Stop safely") :
        s->confirm_gd_boot ? "A Exit to BIOS   B Cancel" :
        s->confirm_quick_resume ? "A Quick resume   B Cancel" :
        s->confirm_new ? "A Start capture   B Cancel" :
        s->confirm_clock ? "A Set clock   B Cancel" :
        s->confirm_defaults ? "A Use defaults   B Cancel" :
        s->confirm_vmu_restore ? "A Restore save   B Cancel" :
        s->confirm_vmu_delete ? "A Delete after backup   B Cancel" :
        s->confirm_vmu_copy ? "A Copy save   B Cancel" :
        s->confirm_music_clear ? "A Clear music cache   B Cancel" :
        s->confirm_restart ? "A Restart console   B Cancel" :
        s->confirm_salvage ? "A Start salvage   B Cancel" :
        s->page==KUI_SHELL_HOME ? "D-pad Select   A Open   Y Volume   L/R Songs" :
        s->page==KUI_SHELL_SETTINGS ? "A Save / Open   B Back / discard" :
        s->page==KUI_SHELL_RIPPER_SETTINGS ? "A Save   B Back / discard" :
        s->page==KUI_SHELL_CLOCK ? "A Set clock   X Reload   B Settings" :
        s->page==KUI_SHELL_CRC_SCAN ? "A Scan again   B Advanced" :
        s->page==KUI_SHELL_VMU_ACTIONS ? "A Review   B VMU   LEFT/RIGHT Copy target" :
        s->page==KUI_SHELL_SALVAGE ? "A Start / Open   B Advanced" :
        s->page==KUI_SHELL_SYSTEM_TOOLS ? "D-pad Select   A Open   B Settings" :
        s->page==KUI_SHELL_VMU_RESTORE ? "B VMU   LEFT/RIGHT Target   START Page" :
        s->page==KUI_SHELL_DESTINATION ? "B Parent   START Cancel   LEFT/RIGHT Page" :
        s->page==KUI_SHELL_KEYBOARD ? "A Key   X Backspace   Y Shift   B Cancel" :
        s->page==KUI_SHELL_ADVANCED ? "D-pad Select   A Open   B Ripper" :
        s->page==KUI_SHELL_RIPPER ? "B Home   START Advanced   L/R Songs" :
        s->page==KUI_SHELL_VMU ? "B Home   LEFT/RIGHT VMU   L Actions" :
        s->page==KUI_SHELL_CD_AUDIO ? "B SD music   START Home   R Refresh" :
        s->page==KUI_SHELL_MUSIC ? "B Parent   START Home   L Audio CD   LEFT/RIGHT Page" : "B Home";
    words(p,40,430,song_page||s->page==KUI_SHELL_MUSIC||s->page==KUI_SHELL_CD_AUDIO||s->page==KUI_SHELL_VMU_RESTORE||s->page==KUI_SHELL_VMU_ACTIONS||s->page==KUI_SHELL_VMU?608:500,MUTED,controls,false);
    if(!v->video_trial && !song_page && s->page!=KUI_SHELL_MUSIC && s->page!=KUI_SHELL_VMU_RESTORE && s->page!=KUI_SHELL_VMU_ACTIONS && s->page!=KUI_SHELL_VMU)
        label(p,512,430,MUTED,"L Memory");
}
static void utility_icon(struct paint *p,unsigned app,unsigned x,unsigned y) {
    if(app==1) {
        panel(p,x+30,y+10,68,108,CYAN); box(p,x+38,y+22,52,38,NAVY);
        box(p,x+48,y+76,24,7,NAVY); box(p,x+56,y+68,8,24,NAVY);
        box(p,x+78,y+76,8,8,PINK); box(p,x+80,y+92,8,8,NAVY);
    } else if(app==2) {
        for(unsigned i=0;i<5;i++) {
            box(p,x+20+i*20,y+12,6,104,CYAN);
            box(p,x+12,y+20+i*20,104,6,CYAN);
        }
        box(p,x+24,y+24,80,80,EDGE); box(p,x+30,y+30,68,68,PANEL);
        words(p,x+40,y+54,x+98,WHITE,"RAM",true);
    } else if(app==7) {
        box(p,x+36,y+32,8,62,CYAN);box(p,x+88,y+20,8,62,PINK);
        box(p,x+40,y+28,52,8,CYAN);box(p,x+40,y+20,52,8,PINK);
        panel(p,x+16,y+84,28,18,CYAN);panel(p,x+68,y+72,28,18,PINK);
    } else {
        box(p,x+28,y+38,72,4,CYAN); box(p,x+62,y+40,4,44,CYAN);
        box(p,x+26,y+40,4,44,CYAN); box(p,x+98,y+40,4,44,CYAN);
        panel(p,x+44,y+10,40,30,CYAN);
        panel(p,x+10,y+82,36,28,CYAN); panel(p,x+46,y+82,36,28,PINK);
        panel(p,x+82,y+82,36,28,CYAN);
    }
}
static void small_utility_icon(struct paint *p,unsigned app,unsigned x,unsigned y) {
    if(app==1) {
        panel(p,x+5,y+1,14,22,CYAN);box(p,x+7,y+4,10,8,NAVY);
        box(p,x+8,y+16,5,2,NAVY);box(p,x+10,y+14,2,6,NAVY);
        box(p,x+15,y+16,2,2,PINK);
    } else if(app==2) {
        for(unsigned i=0;i<3;i++) {
            box(p,x+5+i*6,y,2,24,CYAN);box(p,x,y+5+i*6,24,2,CYAN);
        }
        box(p,x+4,y+4,16,16,EDGE);box(p,x+7,y+7,10,10,PANEL);
    } else if(app==7) {
        box(p,x+7,y+5,3,15,CYAN);box(p,x+18,y+2,3,15,PINK);
        box(p,x+8,y+2,13,3,CYAN);
        box(p,x+2,y+18,8,5,CYAN);box(p,x+13,y+15,8,5,PINK);
    } else {
        box(p,x+11,y+4,2,14,CYAN);box(p,x+3,y+12,18,2,CYAN);
        box(p,x+3,y+12,2,8,CYAN);box(p,x+19,y+12,2,8,CYAN);
        box(p,x+8,y+1,8,7,CYAN);box(p,x,y+17,8,7,CYAN);
        box(p,x+8,y+17,8,7,PINK);box(p,x+16,y+17,8,7,CYAN);
    }
}
static void home(struct paint *p, const struct kui_shell *s,const struct kui_shell_view *v) {
    static const char *names[]={"Disc Ripper","VMU Manager","Memory Test","Network Test","Settings","Diagnostics","GD Play","Music Player"};
    static const char *category[]={"Disc tools","Save files","System tools","Connectivity","System preferences","Diagnostics","Disc boot","Music"};
    static const unsigned icons[]={0,2,2,2,1,2,0,2};
    static const char *details[8][3]={
        {"Capture discs, check CRCs and", "resume interrupted dumps.", "Verify saved files when needed."},
        {"Browse, copy or delete saves.","Back up to SD, then restore", "checked backups to a free name."},
        {"Check available application RAM", "with data patterns and report", "any mismatches found."},
        {"Inspect your network adapter", "or connect and test a network.","View results and save a log."},
        {"Choose video, memory display", "and background music.", "Save preferences to SD."},
        {"Inspect the disc and SD card.", "Run probes, review messages", "and save a diagnostic report."},
        {"Exit K-UI and boot the disc", "through the console BIOS.", "Console region rules still apply."},
        {"Play WAV or Ogg music from SD.", "Listen to audio CD tracks", "or keep music in the background."}};
    unsigned selected=s->home_selected<8?s->home_selected:0;
    panel(p,32,112,208,296,PANEL);
    for(unsigned i=0;i<8;i++) {
        unsigned y=116+i*34;
        if(selected==i) {
            panel(p,32,y,208,32,SELECTED);
            box(p,32,y+5,3,22,PINK);
        }
        if((i>=1 && i<=3) || i==7) small_utility_icon(p,i,44,y+4);
        else art(p,44,y+4,24,24,kui_art_small_icons[icons[i]]);
        words(p,80,y+8,230,selected==i?WHITE:MUTED,names[i],false);
    }
    if(s->system_saved.show_memory && v->memory_valid) {
        char ram[48];snprintf(ram,sizeof(ram),"RAM %lu / %lu KiB",
            (unsigned long)(v->memory_used/1024),(unsigned long)(v->memory_physical/1024));
        words(p,44,391,230,MUTED,ram,false);
    } else words(p,44,391,230,MUTED,"8 applications",false);
    title(p,264,112,names[selected]);
    if(selected==0 || selected==6) {
        char inserted[160];
        snprintf(inserted,sizeof(inserted),"Inserted: %s",
            v->inserted_title&&v->inserted_title[0]?v->inserted_title:"No disc detected");
        label(p,264,144,CYAN,inserted);
    } else label(p,264,144,CYAN,category[selected]);
    panel(p,264,172,344,140,PANEL);
    if((selected>=1 && selected<=3) || selected==7) utility_icon(p,selected,372,178);
    else art(p,372,178,128,128,kui_art_icons[icons[selected]]);
    for(unsigned i=0;i<3;i++) label(p,264,326+i*19,MUTED,details[selected][i]);
    panel(p,264,382,344,28,CYAN);
    label(p,382,388,NAVY,"A  Open app");
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
        "Capturing disc","Verifying saved files","Completed"};
    title(p,40,108,"Disc Ripper");
    char title_line[160];
    snprintf(title_line,sizeof(title_line),"%s: %s",v->busy?"Disc":"Inserted",
        v->busy?(v->disc_title&&v->disc_title[0]?v->disc_title:
            v->inserted_title&&v->inserted_title[0]?v->inserted_title:"Identifying..."):
        (v->inserted_title&&v->inserted_title[0]?v->inserted_title:"No disc detected"));
    label(p,40,136,CYAN,title_line);
    char destination[160];
    snprintf(destination,sizeof(destination),"Destination: %s",s->destination);
    label(p,40,158,MUTED,destination);
    panel(p,32,182,576,122,PANEL);
    const char *phase=v->saving ? "Saving diagnostic report" :
        v->cancel_requested && v->busy ? "Stopping safely..." :
        v->drive_reset_required && !v->busy ? "Drive stopped - restart required" :
        v->outcome==KUI_SHELL_OUTCOME_STOPPED && !v->busy ?
            (v->job_dir && v->job_dir[0]?"Stopped - partial dump kept":"Stopped") :
        v->outcome==KUI_SHELL_OUTCOME_FAILED && !v->busy ? "Operation failed - see diagnostics" :
        v->outcome==KUI_SHELL_OUTCOME_COMPLETE && !v->busy ? "Completed" :
        !v->busy && v->outcome==KUI_SHELL_OUTCOME_NONE ? "Ready for a disc" :
        v->phase<5 ? phases[v->phase] : "Working";
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
    if(v->busy && !v->saving && v->phase>=1 && v->phase<=3) {
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
    title(p,40,108,s->browse_for_scan?"Choose dump to scan":"Choose destination");
    words(p,40,138,478,CYAN,s->browse_path,false);
    char page[32]; snprintf(page,sizeof(page),"PAGE %u%s",s->browser_page+1,
        s->listing.has_more?" +":"");
    label(p,492,138,MUTED,page);
    panel(p,32,166,576,204,PANEL);
    unsigned count=s->listing.count<KUI_DEST_PAGE_SIZE?s->listing.count:KUI_DEST_PAGE_SIZE;
    if(!count) label(p,48,188,MUTED,v->busy?"Loading folders...":
        s->browse_for_scan?"No subfolders. Y scans this dump folder.":
        "No subfolders. Y selects the current folder.");
    for(unsigned i=0;i<count;i++) {
        const struct kui_destination_entry *entry=&s->listing.entries[i];
        unsigned y=174+i*23;
        if(i==s->browser_selected) {
            panel(p,40,y,560,23,SELECTED);
            box(p,40,y+4,3,15,PINK);
        }
        label(p,52,y+2,entry->disabled?AMBER:i==s->browser_selected?WHITE:MUTED,
            entry->name);
    }
    label(p,40,376,v->busy?MUTED:WHITE,s->browse_for_scan?
        "A Open folder   Y Scan this folder":"A Open folder   Y Use folder   X Type path");
    label(p,40,396,s->destination_notice[0]?AMBER:MUTED,
        s->destination_notice[0]?s->destination_notice:
        s->browse_for_scan?"Choose the game folder containing its .gdi file.":
        "The selected destination is saved to SD.");
}
static void keyboard(struct paint *p,const struct kui_shell *s) {
    title(p,40,108,"Type destination");
    label(p,40,136,MUTED,"Folder path on SD; START or DONE saves your choice.");
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
        const char *name=kui_shell_key_label(key,s->keyboard_upper);
        unsigned width=kui_shell_font_width(name,false);
        words(p,x+(w-width)/2,y+5,x+w-4,selected?WHITE:MUTED,name,false);
        if(selected) box(p,x+6,y+25,w-12,2,PINK);
    }
    label(p,40,378,CYAN,s->keyboard_upper?"Y Shift: UPPERCASE":"Y Shift: lowercase");
    label(p,40,398,s->destination_notice[0]?AMBER:MUTED,
        s->destination_notice[0]?s->destination_notice:
        "Example: /Games   New folders are created when used.");
}
static void advanced(struct paint *p,const struct kui_shell *s) {
    static const char *names[]={"Verify saved files","Resume interrupted dump","Capture settings",
        "Quick resume (sizes only)","Destination folder","Advanced CRC scan","Damaged disc salvage"};
    static const char *details[7][3]={
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
         "Previously saved bytes are not reread.",
         "Same-size corruption is not detected."},
        {"Browse folders on SD or enter a destination path.",
         "A new dump uses a game-named folder there.",
         "Existing dumps are kept; new names get a number."},
        {"Scan a saved dump: track hashes and Mode1 sector checks.",
         "Writes a report. No disc reads or track modifications.",
         "Audio needs recorded hashes to be verified."},
        {"Read damaged media into a separate salvage job.",
         "Unreadable sectors can be recorded as unresolved holes.",
         "Explicit retry passes revisit only the missing sectors."}};
    unsigned selected=s->advanced_selected<7?s->advanced_selected:0;
    title(p,40,108,"Advanced disc tools");
    label(p,40,138,MUTED,"Choose an operation for your disc or saved dump.");
    for(unsigned i=0;i<7;i++) {
        unsigned y=156+i*25;
        panel(p,32,y,576,23,selected==i?SELECTED:PANEL);
        if(selected==i) box(p,32,y+4,3,17,PINK);
        label(p,48,y+4,selected==i?WHITE:MUTED,names[i]);
    }
    for(unsigned i=0;i<3;i++) label(p,40,340+i*23,MUTED,details[selected][i]);
}
static void ripper_settings(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    title(p,40,108,"Ripper settings");
    label(p,40,134,MUTED,"UP/DOWN Choose   LEFT/RIGHT Change");
    const char *names[]={"Capture hashes","Read back saved files"};
    const char *values[]={s->draft.crc_only?"CRC32":"CRC32 + SHA-256",
        !s->draft.crc_only?"ON (SHA required)":s->draft.end_readback?"ON":"OFF"};
    for(unsigned i=0;i<2;i++) {
        unsigned y=174+i*48;
        panel(p,32,y,576,40,i==s->setting_selected?SELECTED:PANEL);
        box(p,32,y,4,40,i==s->setting_selected?CYAN:EDGE);
        label(p,48,y+12,WHITE,names[i]);
        label(p,416,y+12,i==s->setting_selected?CYAN:MUTED,values[i]);
    }
    const char *first,*second;
    if(s->setting_selected==0) {
        first="CRC32 is the faster capture option.";
        second="SHA-256 jobs always get a full readback.";
    } else if(s->setting_selected==1) {
        first="Full readback checks every saved byte; it takes time.";
        second="With it off, use Verify later or check on a PC.";
    } else {first="";second="";}
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
    title(p,40,108,memory_test?"Memory Test":"Network Test");
    label(p,40,138,MUTED,memory_test?"Tests an allocated RAM region using data patterns.":
        "Inspect the adapter, then test its network connection.");
    label(p,40,160,v->busy?MUTED:WHITE,memory_test?"A Run memory test":"A Inspect adapter   X Connect / test network");
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
static void vmu_page(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"VMU Manager");
    char line[100];
    snprintf(line,sizeof(line),"VMU %c%u   PAGE %u   %u saves   %u free blocks",
        'A'+s->vmu_slot/2,s->vmu_slot%2+1,s->vmu_page+1,s->vmu.total,s->vmu.free_blocks);
    label(p,40,138,CYAN,line);
    label(p,40,160,v->busy?MUTED:WHITE,"A Refresh   X Back up selected   Y Back up all");
    panel(p,32,190,576,178,PANEL);
    unsigned count=s->vmu.count<KUI_VMU_ROWS?s->vmu.count:KUI_VMU_ROWS;
    if(!count) label(p,48,206,MUTED,v->busy?"Reading VMU...":
        s->vmu.present?"No saves on this page.":"No readable VMU in this slot.");
    for(unsigned i=0;i<count;i++) {
        unsigned y=196+i*21;
        if(i==s->vmu_selected) panel(p,40,y,560,21,SELECTED);
        label(p,52,y+1,i==s->vmu_selected?WHITE:MUTED,s->vmu.entries[i].name);
        snprintf(line,sizeof(line),"%lu bytes",(unsigned long)s->vmu.entries[i].bytes);
        label(p,452,y+1,MUTED,line);
    }
    const struct kui_app_status *status=v->app_status?v->app_status:&s->vmu.status;
    label(p,40,378,status->complete&&!status->passed?AMBER:CYAN,status->message);
    label(p,40,396,MUTED,"R SD backups   L Copy / delete selected   START Page");
}
static void vmu_actions(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"VMU save actions");
    const struct kui_vmu_entry *entry=s->vmu_selected<s->vmu.count?&s->vmu.entries[s->vmu_selected]:NULL;
    char line[120];snprintf(line,sizeof(line),"Source VMU %c%u: %s",'A'+s->vmu_slot/2,
        s->vmu_slot%2+1,entry?entry->name:"No selected save");
    label(p,40,138,CYAN,line);
    for(unsigned i=0;i<2;i++) {
        unsigned y=184+i*48;panel(p,32,y,576,40,i==s->vmu_action_selected?SELECTED:PANEL);
        if(i) snprintf(line,sizeof(line),"Copy to VMU %c%u",'A'+s->vmu_copy_slot/2,s->vmu_copy_slot%2+1);
        else snprintf(line,sizeof(line),"Delete selected save");
        label(p,48,y+10,i==s->vmu_action_selected?WHITE:MUTED,line);
    }
    label(p,40,300,MUTED,"Both actions create and verify an SD backup first.");
    label(p,40,326,MUTED,"A checks the selected save and reviews before any write.");
    label(p,40,352,MUTED,"Copy never overwrites a save already on the destination.");
    const struct kui_app_status *status=v->app_status?v->app_status:&s->vmu.status;
    label(p,40,386,status->complete&&!status->passed?AMBER:CYAN,status->message);
}
static void salvage(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Damaged disc salvage");
    label(p,40,138,MUTED,"Separate recovery jobs; normal captures remain untouched.");
    const struct kui_app_status *status=v->app_status;
    if(v->busy && status && status->line_count) {
        panel(p,32,160,576,240,PANEL);
        app_status(p,status,true,170);
        unsigned count=status->line_count<KUI_APP_LINES?status->line_count:KUI_APP_LINES;
        for(unsigned i=0;i<count && i<6;i++) label(p,40,224+i*27,MUTED,status->lines[i]);
        return;
    }
    const char *names[]={"New salvage job","Resume latest salvage","Retry unresolved sectors","Zero-fill unreadable sectors","Retry pass limit"};
    for(unsigned i=0;i<5;i++) {
        unsigned y=160+i*30;panel(p,32,y,576,27,i==s->salvage_selected?SELECTED:PANEL);
        label(p,48,y+5,i==s->salvage_selected?WHITE:MUTED,names[i]);
        if(i>=3) {
            char value[16];
            if(i==3) snprintf(value,sizeof(value),"%s",s->salvage_zero_fill?"ON":"OFF");
            else snprintf(value,sizeof(value),"%u",s->salvage_passes);
            label(p,508,y+5,i==3&&s->salvage_zero_fill?AMBER:CYAN,value);
        }
    }
    label(p,40,322,MUTED,status && status->line_count>1?status->lines[1]:
        "LEFT/RIGHT changes zero-fill and retry limit.");
    label(p,40,344,AMBER,status && status->line_count>2?status->lines[2]:
        "Unresolved holes are never reported as verified recovery.");
    app_status(p,v->app_status,v->busy,372);
}
static void system_tools(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"System tools");
    label(p,40,138,MUTED,"Read console hardware or save a verified backup to SD.");
    const char *names[]={"Inspect system hardware","Back up settings flash","Back up visible BIOS bank","Restart console"};
    for(unsigned i=0;i<4;i++) {
        unsigned y=164+i*34;panel(p,32,y,576,30,i==s->tools_selected?SELECTED:PANEL);
        label(p,48,y+6,i==s->tools_selected?WHITE:MUTED,names[i]);
    }
    label(p,40,306,MUTED,"X Settings-flash backup   Y BIOS backup");
    const struct kui_app_status *status=v->app_status;
    app_status(p,status,v->busy,330);
    if(status && status->line_count) {
        unsigned n=status->line_count<KUI_APP_LINES?status->line_count:KUI_APP_LINES;
        for(unsigned i=n>2?n-2:0;i<n;i++) label(p,40,374+(i-(n>2?n-2:0))*18,MUTED,status->lines[i]);
    }
}
static void vmu_restore(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Restore VMU backup");
    char line[112];snprintf(line,sizeof(line),"Target VMU %c%u   PAGE %u   %u backups",
        'A'+s->vmu_slot/2,s->vmu_slot%2+1,s->backup_page+1,s->backups.total);
    label(p,40,138,CYAN,line);
    label(p,40,160,v->busy?MUTED:WHITE,"A Check selected backup   X Refresh");
    panel(p,32,190,576,178,PANEL);
    unsigned count=s->backups.count<KUI_VMU_ROWS?s->backups.count:KUI_VMU_ROWS;
    if(!count) label(p,48,206,MUTED,v->busy?"Reading backups...":"No compatible backups on this page.");
    for(unsigned i=0;i<count;i++) {
        unsigned y=196+i*21;
        const struct kui_vmu_backup_entry *e=&s->backups.entries[i];
        if(i==s->backup_selected) panel(p,40,y,560,21,SELECTED);
        words(p,52,y+1,340,i==s->backup_selected?WHITE:MUTED,e->name,false);
        words(p,352,y+1,590,MUTED,e->folder,false);
    }
    const struct kui_app_status *status=v->app_status?v->app_status:&s->backups.status;
    label(p,40,378,status->complete&&!status->passed?AMBER:CYAN,status->message);
    label(p,40,396,MUTED,"Adds a new save only. Existing names are refused.");
}
static void crc_scan(struct paint *p,const struct kui_shell *s,const struct kui_shell_view *v) {
    title(p,40,108,"Advanced CRC scan");
    words(p,40,138,608,CYAN,s->browse_path,false);
    label(p,40,160,MUTED,"Mode1 EDC/parity + saved hashes + catalogue; no disc reads.");
    panel(p,32,190,576,214,PANEL);
    const struct kui_app_status *status=v->app_status;
    app_status(p,status,v->busy,200);
    if(status) {
        unsigned count=status->line_count<KUI_APP_LINES?status->line_count:KUI_APP_LINES;
        unsigned first=count>7?count-7:0;
        for(unsigned i=first;i<count;i++) label(p,40,250+(i-first)*20,MUTED,status->lines[i]);
    } else label(p,40,204,MUTED,"Ready to scan this dump; missing hashes are not a match.");
}
static void app_confirmation(struct paint *p,const struct kui_shell *s) {
    box(p,32,154,576,262,NAVY);panel(p,48,160,544,228,PANEL);box(p,52,164,536,4,PINK);
    if(s->confirm_salvage) {
        label(p,72,188,WHITE,"START A DAMAGED-DISC SALVAGE JOB?");
        label(p,72,222,MUTED,"Writes a separate job under /KUI/salvage.");
        label(p,72,248,s->salvage_zero_fill?AMBER:CYAN,s->salvage_zero_fill?
            "Zero-fill ON: unreadable sectors become tracked holes.":
            "Zero-fill OFF: stop before saving unreadable data.");
        label(p,72,276,MUTED,"Placeholders are not repaired or verified sectors.");
        label(p,72,302,MUTED,"Normal dumps and their checkpoints stay intact.");
        label(p,72,334,CYAN,"A Start salvage");
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
        label(p,72,188,WHITE,copy?"COPY THIS SAVE TO ANOTHER VMU?":"DELETE THIS SAVE FROM THE VMU?");
        char line[112];snprintf(line,sizeof(line),"%s from VMU %c%u",e?e->name:"Selected save",
            'A'+s->vmu_slot/2,s->vmu_slot%2+1);label(p,72,222,CYAN,line);
        if(copy) snprintf(line,sizeof(line),"Destination VMU %c%u; existing names are refused.",
            'A'+s->vmu_copy_slot/2,s->vmu_copy_slot%2+1);
        else snprintf(line,sizeof(line),"The selected VMU entry will be removed after backup.");
        label(p,72,248,MUTED,line);
        label(p,72,276,MUTED,"A verified SD backup is required before any write.");
        label(p,72,302,MUTED,copy?"Keep both VMUs connected until the result appears.":
            "Keep the VMU connected until the result appears.");
        label(p,72,334,CYAN,copy?"A Copy save":"A Delete save");
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
    label(p,384,s->confirm_vmu_restore?346:334,WHITE,"B Cancel");
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
    label(p,40,160,MUTED,"R Benchmarks   UP/DOWN Scroll   START Latest");
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
static void confirmation(struct paint *p,bool quick) {
    box(p,32,154,576,262,NAVY);
    box(p,48,160,544,180,EDGE); box(p,52,164,536,172,PANEL);
    box(p,52,164,536,4,PINK);
    label(p,72,188,WHITE,quick?"QUICK RESUME WITHOUT REREADING?":"START A NEW DUMP?");
    label(p,72,222,MUTED,quick?"Previously saved bytes will not be reread.":
        "Insert the retail GD-ROM and close the lid.");
    label(p,72,244,MUTED,quick?"Same-size damage is not detected by this check.":
        "A new folder keeps existing dumps intact.");
    if(quick) label(p,72,266,AMBER,"Use Verify later to check the saved data.");
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
void kui_shell_draw_content(uint16_t *frame, const struct kui_shell *s,
        const struct kui_shell_view *v, kui_shell_text_fn text, void *ctx) {
    if(!frame || !s || !v) return;
    struct paint p={frame,text,ctx};
    heading(&p,s,v);
    switch(s->page) {
    case KUI_SHELL_HOME: home(&p,s,v); break;
    case KUI_SHELL_RIPPER: ripper(&p,s,v); break;
    case KUI_SHELL_SETTINGS: system_settings(&p,s,v); break;
    case KUI_SHELL_RIPPER_SETTINGS: ripper_settings(&p,s,v); break;
    case KUI_SHELL_DIAGNOSTICS: diagnostics(&p,s,v); break;
    case KUI_SHELL_DESTINATION: destination(&p,s,v); break;
    case KUI_SHELL_KEYBOARD: keyboard(&p,s); break;
    case KUI_SHELL_ADVANCED: advanced(&p,s); break;
    case KUI_SHELL_VMU: vmu_page(&p,s,v); break;
    case KUI_SHELL_VMU_ACTIONS: vmu_actions(&p,s,v); break;
    case KUI_SHELL_SALVAGE: salvage(&p,s,v); break;
    case KUI_SHELL_SYSTEM_TOOLS: system_tools(&p,s,v); break;
    case KUI_SHELL_VMU_RESTORE: vmu_restore(&p,s,v); break;
    case KUI_SHELL_CLOCK: clock_page(&p,s); break;
    case KUI_SHELL_CRC_SCAN: crc_scan(&p,s,v); break;
    case KUI_SHELL_GD_PLAY: gd_play(&p,v); break;
    case KUI_SHELL_CD_AUDIO: cd_audio(&p,s,v); break;
    case KUI_SHELL_MUSIC: music_player(&p,s,v); break;
    case KUI_SHELL_MEMORY: case KUI_SHELL_NETWORK: utility_page(&p,s,v); break;
    }
    footer(&p,s,v);
    if(s->confirm_new || s->confirm_quick_resume) confirmation(&p,s->confirm_quick_resume);
    if(s->confirm_gd_boot) gd_boot_confirmation(&p);
    if(s->confirm_clock || s->confirm_defaults || s->confirm_vmu_restore || s->confirm_vmu_delete ||
       s->confirm_vmu_copy || s->confirm_music_clear || s->confirm_restart || s->confirm_salvage) app_confirmation(&p,s);
    if(v->video_trial) video_trial(&p,v);
}

void kui_shell_draw(uint16_t *frame, const struct kui_shell *s,
        const struct kui_shell_view *v, kui_shell_text_fn text, void *ctx) {
    if(!frame || !s || !v) return;
    struct paint p={frame,text,ctx};
    box(&p,0,0,640,480,NAVY);
    kui_shell_draw_content(frame,s,v,text,ctx);
}
