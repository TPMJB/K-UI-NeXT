/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/boot_ui.h"
#include "kui/shell_font.h"
#include "kui/splash.h"
#include "kui/storage.h"
#include "kui/version.h"
#include <stdio.h>
#include <string.h>

enum { WHITE=0xf7bf, MUTED=0xad79, CYAN=0x5f1f, CRIMSON=0xd92a,
    PANEL=0x0863, SELECTED=0x30c8, EDGE=0x426b, AMBER=0xfdeb };
static void box(uint16_t *frame,unsigned x,unsigned y,unsigned w,unsigned h,uint16_t c) {
    for(unsigned row=y;row<y+h && row<480;row++)
        for(unsigned col=x;col<x+w && col<640;col++) frame[row*640+col]=c;
}
static void text(uint16_t *frame,unsigned x,unsigned y,unsigned right,uint16_t color,
        const char *s,bool large) {
    char clipped[160];
    if(!s) return;
    size_t n=0;
    while(s[n] && s[n]!='\n' && n+1<sizeof(clipped)) {
        unsigned char c=(unsigned char)s[n];
        clipped[n]=(c>=32 && c<127)?(char)c:'?';n++;
    }
    clipped[n]=0;
    bool shortened=s[n]!=0;
    while(n && kui_shell_font_width(clipped,large)>right-x) {
        clipped[--n]=0;shortened=true;
    }
    if(shortened) {
        while(n && (n+4>sizeof(clipped) ||
              kui_shell_font_width(clipped,large)+kui_shell_font_width("...",large)>right-x))
            clipped[--n]=0;
        if(n+4<=sizeof(clipped)) memcpy(clipped+n,"...",4);
    }
    kui_shell_font_draw(frame,(int)x,(int)y,color,clipped,large);
}
static void line(uint16_t *f,unsigned x,unsigned y,unsigned right,const char *s) {
    text(f,x,y,right,MUTED,s,false);
}
static const char *source(unsigned transport) {
    static const char *const names[]={"SCIF microSD","SCI microSD","IDE / CF","Auto: SCIF > SCI > IDE"};
    return names[transport<=KUI_STORAGE_AUTO?transport:KUI_STORAGE_AUTO];
}
static void menu(uint16_t *f,const struct kui_boot_ui *ui,const struct kui_boot_view *v) {
    static const char *const home[]={"Start K-UI","Recovery","Card tools","Diagnostics","Help"};
    static const char *const diagnostics[]={"Check optical drive","Storage write test","Save log to card","Run benchmark","Measure load time"};
    bool home_page=ui->page==KUI_BOOT_HOME;
    box(f,32,112,280,224,PANEL);
    box(f,320,112,288,224,PANEL);
    for(unsigned i=0;i<5;i++) {
        unsigned y=120+i*42;
        if(i==ui->selected) {box(f,38,y,268,38,SELECTED);box(f,38,y,4,38,CRIMSON);}
        text(f,50,y+7,298,i==ui->selected?WHITE:MUTED,
            home_page?home[i]:diagnostics[i],true);
    }
    const char *heading="",*details[6]={0};
    if(home_page) {
        switch(ui->selected) {
        case 0: heading="START / RETRY";
            details[0]="Load /KUI/runtime.kui";details[1]="then recovery if needed.";
            details[3]="No card at startup?";details[4]="Insert it while idle,";details[5]="then select Start K-UI.";break;
        case 1: heading="RECOVERY ONLY";
            details[0]="Load /KUI/recovery.kui";details[1]="without trying runtime.";
            details[3]="Keep a known-good copy";details[4]="on the FAT boot volume.";break;
        case 2: heading="UPDATEABLE TOOLS";
            details[0]="Load /KUI/tools.kui";details[1]="from the selected card.";
            details[3]="Install the boot utility";details[4]="to measure loading";details[5]="without a new CD.";break;
        case 3: heading="BUILT-IN UTILITIES";
            details[0]="Inspect the optical drive,";details[1]="test storage, save logs";details[2]="or run a benchmark.";
            details[4]="Writes ask before starting.";break;
        default: heading="KEEP THIS DISC";
            details[0]="File paths, boot controls";details[1]="and safe retry guidance.";
            details[3]="Update programs on card";details[4]="as K-UI develops.";break;
        }
    } else {
        switch(ui->selected) {
        case 0: heading="OPTICAL PROBE";
            details[0]="Inspect the inserted disc.";details[1]="This does not write a card.";
            details[3]="For a retail disc check,";details[4]="replace the boot CD with";details[5]="a known-good game disc.";break;
        case 1: heading="WRITES TEST DATA";
            details[0]="Create a test file and";details[1]="verify its contents.";
            details[3]="Requires writable";details[4]="single-volume FAT/exFAT.";break;
        case 2: heading="SAVE DIAGNOSTICS";
            details[0]="Write the complete log";details[1]="to the selected card.";
            details[3]="Requires writable";details[4]="single-volume FAT/exFAT.";break;
        case 3: heading="CONFIGURED BENCHMARK";
            details[0]="Read /KUI/bench.cfg.";details[1]="May write temporary files";details[2]="and a result report.";
            details[4]="B requests a safe stop.";break;
        default: heading="READ-ONLY LOAD TIMING";
            details[0]="Read and validate runtime.";details[1]="Show time and throughput";details[2]="without starting the app.";
            details[4]="No writes. Y shows logs.";break;
        }
    }
    text(f,336,126,596,CYAN,heading,false);
    for(unsigned i=0;i<6;i++) line(f,336,159+i*26,596,details[i]);
    if(!v->worker_available && !home_page)
        text(f,336,307,596,AMBER,"Diagnostics unavailable",false);
}
static void help(uint16_t *f) {
    static const char *const help_lines[]={
        "Start K-UI: runtime.kui, then recovery.kui if needed.",
        "Recovery (X): only /KUI/recovery.kui. Y opens the log.",
        "Card tools: optional /KUI/tools.kui; install it first.",
        "Boot reads are read-only. No format or repair is built in.",
        "Insert an SD card while idle, then select Start K-UI.",
        "Power off before changing adapters, wiring or IDE/CF.",
        "Auto searches SCIF, SCI, then IDE. Left/Right picks one.",
        "A FAT boot partition takes priority over ext4 data.",
        "Dirty ext4 needs external repair or a separate tool."
    };
    box(f,32,112,576,274,PANEL);
    for(unsigned i=0;i<sizeof(help_lines)/sizeof(help_lines[0]);i++)
        line(f,46,124+i*28,596,help_lines[i]);
}
void kui_boot_ui_draw(uint16_t *frame,const struct kui_boot_ui *ui,
        const struct kui_boot_view *view) {
    if(!frame || !ui || !view) return;
    kui_splash_draw(frame);
    if(view->countdown && !view->busy && ui->page==KUI_BOOT_HOME) {
        box(frame,32,360,576,96,PANEL);
        box(frame,32,360,5,96,CRIMSON);
        char countdown[64];snprintf(countdown,sizeof(countdown),"Starting K-UI in %u...",view->countdown);
        text(frame,48,373,592,WHITE,countdown,true);
        line(frame,48,407,592,"A Start now   B Boot menu   X Recovery");
        line(frame,48,432,592,"Any other button pauses automatic startup.");
        return;
    }
    /* The original art is a quiet backdrop. Opaque panels keep the controls
     * readable over RF/composite; no scaling, fine-line icons or animations. */
    for(unsigned i=0;i<640*480;i++) frame[i]=(uint16_t)((frame[i]&0xe79cu)>>2);
    box(frame,32,24,576,76,PANEL);
    box(frame,32,24,5,76,CRIMSON);
    text(frame,48,34,366,WHITE,"K-UI Boot",true);
    line(frame,48,65,366,"Dainsleif / CD recovery");
    text(frame,392,36,594,CYAN,KUI_RELEASE_SHORT,false);
    char build[40];snprintf(build,sizeof(build),"Build %.12s",view->build?view->build:"local");
    line(frame,392,65,594,build);
    if(ui->page==KUI_BOOT_HOME || ui->page==KUI_BOOT_DIAGNOSTICS) {
        menu(frame,ui,view);
        box(frame,32,346,576,34,PANEL);
        char label[80];snprintf(label,sizeof(label),"Source: %s",source(ui->transport));
        text(frame,46,354,420,CYAN,label,false);
        if(ui->page==KUI_BOOT_HOME) line(frame,442,354,596,"Left/Right");
    } else if(ui->page==KUI_BOOT_HELP) help(frame);
    else if(ui->page==KUI_BOOT_CONFIRM) {
        box(frame,32,112,576,268,PANEL);
        text(frame,48,130,592,AMBER,"This action writes to storage",true);
        char label[80];snprintf(label,sizeof(label),"%s",ui->confirm==KUI_BOOT_BENCH?
            "Sources and tests follow /KUI/bench.cfg.":source(ui->transport));
        line(frame,48,174,592,label);
        line(frame,48,210,592,ui->confirm==KUI_BOOT_WRITE_TEST?"The write test creates and verifies a test file.":
            ui->confirm==KUI_BOOT_SAVE_LOG?"Save the complete diagnostic log as a report.":
            "The configured benchmark may create temporary files.");
        line(frame,48,244,592,"Use a writable single-volume FAT/exFAT card.");
        line(frame,48,278,592,"Boot files and ext4 data are not repaired by this action.");
        text(frame,48,330,592,WHITE,"A Continue     B Cancel",true);
    } else {
        box(frame,32,112,576,278,PANEL);
        for(unsigned i=0;i<view->line_count && i<KUI_BOOT_LOG_ROWS;i++) {
            const char *s=view->lines[i]?view->lines[i]:"";
            unsigned length=(unsigned)strlen(s);
            line(frame,42,120+i*22,598,s+(ui->log_column<length?ui->log_column:length));
        }
        char label[96];snprintf(label,sizeof(label),"Log: %u lines   Scroll %u   Columns %u+",
            view->total_lines,ui->scroll,ui->log_column+1);
        text(frame,42,386,598,CYAN,label,false);
    }
    char status[100];
    if(view->busy) snprintf(status,sizeof(status),"%s",view->cancelled?"Stopping safely...":
        view->status?view->status:"Reading selected boot image...");
    else if(view->countdown) snprintf(status,sizeof(status),"Starting in %u... any button opens the menu.",view->countdown);
    else snprintf(status,sizeof(status),"%s",view->status?view->status:"Idle. Choose Start K-UI to try the selected source.");
    if(ui->page!=KUI_BOOT_LOG) text(frame,40,392,600,view->busy?CYAN:WHITE,status,false);
    box(frame,32,420,576,2,EDGE);
    const char *controls=view->busy?"B Stop safely   Y Log":
        ui->page==KUI_BOOT_LOG?"Up/Down Scroll   Left/Right Pan   Start Latest   B Back":
        ui->page==KUI_BOOT_CONFIRM?"A Continue   B Cancel":
        ui->page==KUI_BOOT_HELP?"B Back to boot menu":
        ui->page==KUI_BOOT_HOME?"D-pad Select   A Open   X Recovery   Y Log":
        "D-pad Select   A Open   B Boot menu   Y Log";
    text(frame,40,433,600,WHITE,controls,false);
}
