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
    size_t n=0;
    while(n+1<sizeof(clipped) && value[n] && value[n]!='\n' && value[n]!='\r') {
        unsigned char c=(unsigned char)value[n];
        clipped[n]=(c>=32 && c<=126)?(char)c:' ';
        ++n;
    }
    clipped[n]='\0';
    bool truncated=value[n] && value[n]!='\n' && value[n]!='\r';
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
static void heading(struct paint *p, const struct kui_shell_view *v) {
    art(p,32,20,128,64,kui_art_brand);
    title(p,184,26,"Katana User Interface");
    label(p,184,52,MUTED,"by TPMJB   /   SD runtime");
    words(p,184,74,396,CYAN,state(v),false);
    char build[40]; snprintf(build,sizeof(build),"Build %.12s",v->build?v->build:"local");
    label(p,416,74,MUTED,build); rule(p,98);
}
static void footer(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    rule(p,416);
    const char *controls=v->busy ? "B Stop safely" :
        s->confirm_new ? "A Start capture   B Cancel" :
        s->page==KUI_SHELL_HOME ? "D-pad Select   A Open" :
        s->page==KUI_SHELL_SETTINGS ? "A Save   B Back / discard" : "B Home";
    label(p,40,430,MUTED,controls); label(p,512,430,MUTED,"L Memory");
}
static void home(struct paint *p, const struct kui_shell *s) {
    static const char *names[]={"Disc Ripper","Settings","Diagnostics"};
    static const char *category[]={"Disc tools","Preferences","System tools"};
    static const char *details[3][3]={
        {"Capture discs, check CRCs and", "resume interrupted dumps.", "Verify saved files when needed."},
        {"Choose capture checks and", "memory display. Save preferences", "to the SD card."},
        {"Inspect the disc and SD card.", "Run probes, review messages", "and save a diagnostic report."}};
    unsigned selected=s->home_selected<3?s->home_selected:0;
    panel(p,32,112,208,296,PANEL);
    for(unsigned i=0;i<3;i++) {
        unsigned y=120+i*54;
        if(selected==i) {
            panel(p,32,y,208,44,SELECTED);
            box(p,32,y+5,3,34,PINK);
        }
        art(p,44,y+10,24,24,kui_art_small_icons[i]);
        words(p,80,y+14,230,selected==i?WHITE:MUTED,names[i],false);
    }
    label(p,44,382,MUTED,"3 applications");
    title(p,264,112,names[selected]);
    label(p,264,144,CYAN,category[selected]);
    panel(p,264,172,344,140,PANEL);
    art(p,372,178,128,128,kui_art_icons[selected]);
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
    else snprintf(text,sizeof(text),"RAM snapshot unavailable. L retries.");
    label(p,40,y,MUTED,text);
}
static void ripper(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    static const char *phases[]={"Identifying disc","Checking saved prefix",
        "Capturing disc","Verifying saved files","Completed"};
    title(p,40,108,"Disc Ripper");
    label(p,40,134,MUTED,"Capture retail discs to your SD card.");
    panel(p,32,166,576,134,PANEL);
    const char *phase=v->saving ? "Saving diagnostic report" :
        v->cancel_requested && v->busy ? "Stopping safely..." :
        v->outcome==KUI_SHELL_OUTCOME_STOPPED && !v->busy ?
            (v->job_dir && v->job_dir[0]?"Stopped - partial dump kept":"Stopped") :
        v->outcome==KUI_SHELL_OUTCOME_FAILED && !v->busy ? "Operation failed - see diagnostics" :
        v->outcome==KUI_SHELL_OUTCOME_COMPLETE && !v->busy ? "Completed" :
        !v->busy && v->outcome==KUI_SHELL_OUTCOME_NONE ? "Ready for a disc" :
        v->phase<5 ? phases[v->phase] : "Working";
    label(p,48,178,v->outcome==KUI_SHELL_OUTCOME_FAILED&&!v->busy?AMBER:CYAN,phase);
    char line[73];
    snprintf(line,sizeof(line),"TRACK %u / %u",v->track,v->tracks);
    label(p,48,204,WHITE,line);
    snprintf(line,sizeof(line),"%u KiB/s",v->rate_kib);
    label(p,440,204,WHITE,line);
    box(p,48,234,544,12,EDGE);
    /* Clamp malformed snapshots without overflowing done*width. Progress values
     * are bytes, and whole-MiB scaling is too coarse for small audio tracks. */
    unsigned width=0;
    if(v->total) width=v->done>=v->total?544u:
        (unsigned)((double)v->done/(double)v->total*544.0);
    if(width) box(p,48,234,width,12,CYAN);
    snprintf(line,sizeof(line),"%lu / %lu MiB   SAVED %lu MiB",
        (unsigned long)(v->done/1048576),(unsigned long)(v->total/1048576),
        (unsigned long)(v->committed/1048576));
    label(p,48,258,WHITE,line);
    snprintf(line,sizeof(line),"ELAPSED %lu:%02lu   RETRIES %u",
        (unsigned long)(v->elapsed_ms/60000),
        (unsigned long)(v->elapsed_ms/1000%60),v->retries);
    label(p,48,280,MUTED,line);
    label(p,40,316,v->busy?MUTED:WHITE,"A New dump   X Resume latest   Y Verify latest");
    if(!v->busy && v->outcome==KUI_SHELL_OUTCOME_COMPLETE)
        label(p,40,342,v->saved_verified?GREEN:CYAN,
            v->saved_verified?"Saved bytes reread and verified.":"Capture complete. Saved bytes not fully reread.");
    else label(p,40,342,MUTED,v->message && v->message[0]?v->message:
        "A new dump always uses a separate folder.");
    if(v->job_dir && v->job_dir[0]) label(p,40,366,MUTED,v->job_dir);
    if(s->saved.show_memory) memory(p,392,v);
}
static void settings(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    title(p,40,108,"Settings");
    label(p,40,134,MUTED,"UP/DOWN Choose   LEFT/RIGHT Change");
    const char *names[]={"Capture hashes","Read back saved files","Show memory usage"};
    const char *values[]={s->draft.crc_only?"CRC32":"CRC32 + SHA-256",
        !s->draft.crc_only?"ON (SHA required)":s->draft.end_readback?"ON":"OFF",
        s->draft.show_memory?"ON":"OFF"};
    for(unsigned i=0;i<3;i++) {
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
    } else {
        first="Show main RAM use on the Disc Ripper screen.";
        second="L always records a detailed memory snapshot.";
    }
    label(p,40,330,MUTED,first); label(p,40,350,MUTED,second);
    bool dirty=kui_shell_settings_dirty(s);
    const char *notice=v->settings_notice && v->settings_notice[0]?v->settings_notice:NULL;
    label(p,40,376,dirty?AMBER:CYAN,dirty?
        "Unsaved changes. A saves; B discards.":notice?notice:"Ready to edit. A saves to SD.");
    label(p,40,396,MUTED,dirty && notice?notice:
        "New dumps use these choices. Resume keeps job hash mode.");
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
    char line[73];
    snprintf(line,sizeof(line),"%u lines  %s%s",v->total_log_lines,
        s->scroll?"SCROLLED":"LATEST",v->log_truncated?"  Earlier lines truncated":"");
    label(p,40,392,CYAN,line);
}
static void confirmation(struct paint *p) {
    box(p,32,154,576,254,NAVY);
    box(p,48,160,544,180,EDGE); box(p,52,164,536,172,PANEL);
    box(p,52,164,536,4,PINK);
    label(p,72,188,WHITE,"START A NEW DUMP?");
    label(p,72,224,MUTED,"Insert the retail GD-ROM and close the lid.");
    label(p,72,246,MUTED,"A new folder keeps existing dumps intact.");
    label(p,72,294,CYAN,"A Start capture"); label(p,368,294,WHITE,"B Cancel");
}
void kui_shell_draw_content(uint16_t *frame, const struct kui_shell *s,
        const struct kui_shell_view *v, kui_shell_text_fn text, void *ctx) {
    if(!frame || !s || !v) return;
    struct paint p={frame,text,ctx};
    heading(&p,v);
    switch(s->page) {
    case KUI_SHELL_HOME: home(&p,s); break;
    case KUI_SHELL_RIPPER: ripper(&p,s,v); break;
    case KUI_SHELL_SETTINGS: settings(&p,s,v); break;
    case KUI_SHELL_DIAGNOSTICS: diagnostics(&p,s,v); break;
    }
    footer(&p,s,v);
    if(s->confirm_new) confirmation(&p);
}

void kui_shell_draw(uint16_t *frame, const struct kui_shell *s,
        const struct kui_shell_view *v, kui_shell_text_fn text, void *ctx) {
    if(!frame || !s || !v) return;
    struct paint p={frame,text,ctx};
    box(&p,0,0,640,480,NAVY);
    kui_shell_draw_content(frame,s,v,text,ctx);
}
