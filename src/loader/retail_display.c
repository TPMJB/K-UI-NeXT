/* SPDX-License-Identifier: GPL-3.0-only */
#include "retail_display.h"
#include <stddef.h>
#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "LOCAL"
#endif
/* Register meanings: KallistiOS fcfa7d869471591ca1c777543261a7bfea7cb726,
 * dc/pvr/pvr_regs.h and hardware/video.c. Capture the cable-appropriate
 * 640x480 RGB565 mode left by arch_shutdown, rather than inventing timings.
 * Restore is only for a stopped launch/fatal diagnostic, never normal IO. */
static const uint16_t offsets[14] = {
    0x44,0x48,0x50,0x54,0x5c,0xcc,0xd0,0xd4,0xd8,0xdc,0xe8,0xec,0xf0,0x40
};
static volatile uint32_t *reg(unsigned i) {
    return (volatile uint32_t *)(uintptr_t)(0xa05f8000u + offsets[i]);
}
/* Original compact 5x7 glyphs, rows encoded low-five-bits, A-Z then 0-9.
 * The complete renderer/font belongs to each linked image. */
static const uint8_t glyphs[36][7] = {
 {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
 {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
 {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
 {14,17,16,23,17,17,15},{17,17,17,31,17,17,17},
 {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
 {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
 {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
 {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
 {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
 {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
 {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
 {17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
 {17,17,10,4,4,4,4},{31,1,2,4,8,16,31},
 {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
 {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
 {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
 {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
 {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
};
static unsigned row;
void retail_display_capture(struct retail_display_state *s) {
    for(unsigned i=0;i<14;i++) s->regs[i]=*reg(i);
}
void retail_display_line(const char *text) {
    volatile uint16_t *frame=(volatile uint16_t *)(uintptr_t)0xa5000000u;
    unsigned x=20;
    if(row>444) row=124;
    for(unsigned y=row;y<row+22;y++)
        for(unsigned a=16;a<624;a++) frame[y*640+a]=0x0864;
    while(*text && x<612) {
        unsigned char c=(unsigned char)*text++;
        if(c>='a' && c<='z') c-=32;
        int n=c>='A'&&c<='Z'?c-'A':c>='0'&&c<='9'?c-'0'+26:-1;
        for(unsigned y=0;y<7;y++) {
            unsigned bits=n>=0?glyphs[n][y]:c=='-'&&y==3?14:
                c==':'&&(y==2||y==5)?4:c=='.'&&y==6?4:
                c=='/'?1u<<(y<5?y:4):0;
            for(unsigned a=0;a<5;a++) if(bits&(16u>>a))
                for(unsigned dy=0;dy<2;dy++)
                    for(unsigned dx=0;dx<2;dx++)
                        frame[(row+y*2+dy)*640+x+a*2+dx]=0xffff;
        }
        x+=12;
    }
    row+=22;
}
void retail_display_restore(const struct retail_display_state *s) {
    *reg(0)=s->regs[0]&~1u;
    for(unsigned i=1;i<14;i++) *reg(i)=s->regs[i];
    *reg(0)=s->regs[0];
    volatile uint16_t *frame=(volatile uint16_t *)(uintptr_t)0xa5000000u;
    for(unsigned i=0;i<640u*480u;i++) frame[i]=0x0864;
    row=20;
    retail_display_line("K-UI V1.5 GAME LAUNCH");
    retail_display_line("BUILD " KUI_BUILD_ID);
    retail_display_line("NATIVE GD IMAGE");
    row=108;
}
void retail_display_hex(const char *label,uint32_t value) {
    char out[50]; unsigned n=0;
    while(*label && n<39) out[n++]=*label++;
    out[n++]=' ';
    for(unsigned i=0;i<8;i++) out[n++]="0123456789ABCDEF"[(value>>(28-4*i))&15];
    out[n]=0; retail_display_line(out);
}
void retail_display_progress(uint32_t done,uint32_t total) {
    /* Fixed bottom bar, independent of scrolling diagnostic rows. */
    if(!total || done>total) return;
    uint32_t filled=(uint32_t)((uint64_t)done*600u/total);
    volatile uint16_t *frame=(volatile uint16_t *)(uintptr_t)0xa5000000u;
    for(unsigned y=464;y<472;y++)
        for(unsigned x=0;x<600;x++) frame[y*640+20+x]=x<filled?0x07e0:0x2104;
}
void retail_display_pause(void) {
    /* Keep handoff text visible for about 3 seconds at 50/60 Hz without
     * borrowing any TMU channel. A stopped scan generator cannot hang us. */
    volatile uint32_t *scan=(volatile uint32_t *)(uintptr_t)0xa05f810cu;
    uint32_t before=*scan&0x3ffu, frames=0;
    for(uint32_t budget=0;budget<30000000u && frames<180u;budget++) {
        uint32_t now=*scan&0x3ffu;
        if(now<before) ++frames;
        before=now;
    }
}
