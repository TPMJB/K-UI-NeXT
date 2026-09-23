/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell.h"
#include <stdio.h>
#include <string.h>

/* Flat fills and short lines suit RGB565 and interlaced/RF output. No gradients,
 * fine scanlines, animated backgrounds or allocations compete with the worker. */
enum { NAVY=0x0843, PANEL=0x10c6, SELECTED=0x114a, EDGE=0x21cf,
    WHITE=0xef7d, MUTED=0x9d37, CYAN=0x2e9b, PINK=0xf297,
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
static void label(struct paint *p, unsigned x, unsigned y,
        uint16_t color, const char *value) {
    if(!p->text || !value || x<32 || x>=608 || y<32 || y>432) return;
    char clipped[73];
    unsigned limit=(608-x)/8, n=0;
    if(limit>72) limit=72;
    while(n<limit && value[n] && value[n]!='\n' && value[n]!='\r') {
        unsigned char c=(unsigned char)value[n];
        clipped[n]=(c>=32 && c<=126) ? (char)c : ' ';
        ++n;
    }
    if(n>=3 && value[n] && value[n]!='\n' && value[n]!='\r')
        clipped[n-3]=clipped[n-2]=clipped[n-1]='.';
    clipped[n]='\0';
    p->text(p->ctx,x,y,color,clipped);
}
static void rule(struct paint *p, unsigned y) {
    box(p,32,y,576,2,EDGE); box(p,32,y,64,2,CYAN);
    box(p,560,y,48,2,PINK);
}
static void logo(struct paint *p) {
    /* Original five-column letterforms, large enough to survive RF. */
    static const unsigned char letters[4][7] = {
        {17,18,20,24,20,18,17}, {0,0,0,31,0,0,0},
        {17,17,17,17,17,17,14}, {31,4,4,4,4,4,31}
    };
    for(unsigned c=0;c<4;c++) for(unsigned y=0;y<7;y++)
        for(unsigned x=0;x<5;x++) if(letters[c][y] & (1u<<(4-x)))
            box(p,40+c*24+x*4,36+y*4,4,4,c==1?PINK:CYAN);
}
static const char *state(const struct kui_shell_view *v) {
    if(v->saving) return v->cancel_requested ? "CANCELLING SAVE" : "SAVING REPORT";
    if(v->busy) return v->cancel_requested ? "STOP REQUESTED" : "WORKING";
    return "READY";
}
static void heading(struct paint *p, const struct kui_shell_view *v) {
    logo(p); label(p,152,42,WHITE,"NeXT | SD runtime");
    label(p,152,60,MUTED,"Katana User Interface");
    label(p,400,38,CYAN,state(v));
    char build[40]; snprintf(build,sizeof(build),"BUILD %.12s",v->build?v->build:"local");
    label(p,400,60,MUTED,build); rule(p,88);
}
static void footer(struct paint *p, const struct kui_shell *s,
        const struct kui_shell_view *v) {
    rule(p,416);
    const char *controls=v->busy ? "B Stop safely" :
        s->confirm_new ? "A Start capture   B Cancel" :
        s->page==KUI_SHELL_HOME ? "UP/DOWN Choose   A Open" :
        s->page==KUI_SHELL_SETTINGS ? "A Save   B Back / discard" : "B Home";
    label(p,40,430,WHITE,controls); label(p,456,430,MUTED,"L Memory");
}
static void icon(struct paint *p, unsigned kind, unsigned y, uint16_t color) {
    unsigned x=64;
    if(kind==0) {
        for(int dy=-14;dy<=14;dy++) for(int dx=-14;dx<=14;dx++) {
            int distance=dx*dx+dy*dy;
            if((distance>=100 && distance<=196) || distance<=9)
                box(p,(unsigned)((int)x+dx),(unsigned)((int)y+dy),1,1,color);
        }
    } else if(kind==1) {
        for(unsigned row=0;row<3;row++) {
            box(p,x-14,y-12+row*12,28,2,color);
            box(p,x-8+(row%2)*12,y-15+row*12,6,8,color);
        }
    } else {
        box(p,x-14,y-14,28,28,color); box(p,x-11,y-11,22,22,PANEL);
        box(p,x-8,y-6,4,3,color); box(p,x-5,y-3,4,3,color);
        box(p,x-8,y,4,3,color); box(p,x+2,y+5,7,2,color);
    }
}
static void home(struct paint *p, const struct kui_shell *s) {
    static const char *names[]={"DISC RIPPER","SETTINGS","DIAGNOSTICS"};
    static const char *details[]={"Capture, resume and verify a disc.",
        "Choose capture checks and memory display.","Disc and SD tests, reports and log viewer."};
    label(p,40,112,WHITE,"YOUR DREAMCAST. YOUR TOOLS.");
    label(p,40,134,MUTED,"Select a tool to begin.");
    for(unsigned i=0;i<3;i++) {
        unsigned y=164+i*78;
        bool selected=s->home_selected==i;
        box(p,32,y,576,66,selected?SELECTED:PANEL);
        box(p,32,y,4,66,selected?CYAN:EDGE);
        icon(p,i,y+33,selected?CYAN:MUTED);
        label(p,96,y+10,selected?WHITE:MUTED,names[i]);
        label(p,96,y+36,MUTED,details[i]);
        label(p,576,y+10,selected?PINK:MUTED,selected?">":" ");
    }
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
    label(p,40,110,WHITE,"DISC RIPPER");
    label(p,40,134,MUTED,"Capture retail discs to your SD card.");
    box(p,32,166,576,134,PANEL);
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
    label(p,40,110,WHITE,"SETTINGS");
    label(p,40,134,MUTED,"UP/DOWN Choose   LEFT/RIGHT Change");
    const char *names[]={"Capture hashes","Read back saved files","Show memory usage"};
    const char *values[]={s->draft.crc_only?"CRC32":"CRC32 + SHA-256",
        !s->draft.crc_only?"ON (SHA required)":s->draft.end_readback?"ON":"OFF",
        s->draft.show_memory?"ON":"OFF"};
    for(unsigned i=0;i<3;i++) {
        unsigned y=174+i*48;
        box(p,32,y,576,40,i==s->setting_selected?SELECTED:PANEL);
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
    label(p,40,110,WHITE,"DIAGNOSTICS");
    label(p,40,138,v->busy?MUTED:WHITE,"A Disc probe   X SD test   Y Save log");
    label(p,40,160,MUTED,"R Benchmarks   UP/DOWN Scroll   START Latest");
    box(p,32,190,576,190,PANEL);
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
void kui_shell_draw(uint16_t *frame, const struct kui_shell *s,
        const struct kui_shell_view *v, kui_shell_text_fn text, void *ctx) {
    if(!frame || !s || !v) return;
    struct paint p={frame,text,ctx};
    box(&p,0,0,640,480,NAVY);
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
