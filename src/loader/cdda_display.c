/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_display.h"
#include "kui/shell_font.h"
#include <stddef.h>
#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "local"
#endif
#ifndef CDDA_TEST_PROFILE
#define CDDA_TEST_PROFILE 0
#endif
#if CDDA_TEST_PROFILE == 1
#define CDDA_TITLE "K-UI: CDDA controls / SCI"
#elif CDDA_TEST_PROFILE == 2
#define CDDA_TITLE "K-UI: CDDA 15-minute soak / SCI"
#elif CDDA_TEST_PROFILE == 3
#define CDDA_TITLE "K-UI: CDDA + card stress / SCI"
#elif CDDA_TEST_PROFILE == 4
#define CDDA_TITLE "K-UI: CDDA clock calibration / SCI"
#elif CDDA_TEST_PROFILE == 5
#define CDDA_TITLE "K-UI: CDDA command tests / SCI"
#elif CDDA_TEST_PROFILE == 6
#define CDDA_TITLE "K-UI: CDDA mixed jobs / SCI"
#elif CDDA_TEST_PROFILE == 7
#define CDDA_TITLE "K-UI: CDDA service handoff / SCI"
#else
#define CDDA_TITLE "K-UI: isolated CDDA / SCI test"
#endif
static uint16_t *const fb=(uint16_t *)(uintptr_t)0xa5000000u;
static unsigned row;
void cdda_display_init(void) {
    for(unsigned n=0;n<640u*480u;n++) fb[n]=0x0864u;
    kui_shell_font_draw(fb,20,16,0x7fffu,CDDA_TITLE,true);
    kui_shell_font_draw(fb,20,47,0xffffu,"Build " KUI_BUILD_ID,false);
    kui_shell_font_draw(fb,20,70,0xffffu,"Detached / owned AICA / read-only exFAT",false);
    row=106;
}
void cdda_display_line(const char *text) {
    if(row>398) {
        for(unsigned y=106;y<398;y++) for(unsigned x=16;x<624;x++)
            fb[y*640+x]=fb[(y+20)*640+x];
        row=398;
    }
    for(unsigned y=row;y<row+20;y++) for(unsigned x=16;x<624;x++) fb[y*640+x]=0x0864u;
    kui_shell_font_draw(fb,20,(int)row,0xffffu,text,false); row+=20;
}
void cdda_display_number(const char *label,uint32_t value) {
    char text[96],reverse[10]; unsigned n=0,digits=0;
    while(*label && n<80) text[n++]=*label++;
    do { reverse[digits++]=(char)('0'+value%10); value/=10; } while(value);
    while(digits && n<sizeof(text)-1) text[n++]=reverse[--digits];
    text[n]=0;cdda_display_line(text);
}
void cdda_display_finish(unsigned failures) {
    for(unsigned y=425;y<480;y++) for(unsigned x=0;x<640;x++) fb[y*640+x]=0x0864u;
    kui_shell_font_draw(fb,20,425,failures?0xfba0u:0x87f0u,
        failures?"CDDA TEST STOPPED - photograph this screen":"CDDA TEST COMPLETE - photograph this screen",false);
    kui_shell_font_draw(fb,20,449,0xffffu,"Power off; restore runtime to return to K-UI.",false);
}
