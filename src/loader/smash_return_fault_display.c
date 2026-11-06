/* SPDX-License-Identifier: GPL-3.0-only */
#include "smash_return_fault_display.h"
#include <stdint.h>

volatile uint32_t kui_smash_return_progress_state[2];
void __attribute__((noipa)) kui_smash_return_progress(uint32_t phase,uint32_t detail) {
    kui_smash_return_progress_state[1]=detail;
    kui_smash_return_progress_state[0]=phase;
}

/* Pinned local KallistiOS dc/pvr/pvr_regs.h and hardware/video.c:
 * FB_CFG_1 0x44: enable bit0, pixel mode bits2..3 (555/565/888P/0888).
 * FB_SIZE 0x5c: active 32-bit words minus1, rows minus1, line modulo.
 * Scanout stride is (active_words_minus1 + modulo)*4. Interlace bit4
 * in IL_CFG selects both FB_ADDR/FB_IL_ADDR, each with that field stride.
 * VRAM uses the uncached 32-bit aperture, fixed retail capacity8MiB.
 * No timing/enable/base/stride register is ever changed. */
enum {FAULT_VRAM_BYTES=0x800000,FAULT_X=8,FAULT_Y=8,FAULT_W=152,FAULT_H=40};
#if defined(KUI_SMASH_RETURN_FAULT_HOST_TEST)
extern uint32_t kui_smash_return_fault_test_read(uint32_t);
extern void kui_smash_return_fault_test_store(uint32_t,uint32_t,unsigned);
extern void kui_smash_return_fault_test_border(uint32_t);
static uint32_t fault_read(uint32_t offset) {return kui_smash_return_fault_test_read(offset);}
static void fault_store(uint32_t at,uint32_t value,unsigned shift) {
    kui_smash_return_fault_test_store(at,value,1u<<shift);
}
static void fault_border(void) {kui_smash_return_fault_test_border(UINT32_C(0xff0000));}
#else
static uint32_t fault_read(uint32_t offset) {
    return *(volatile const uint32_t *)(uintptr_t)(UINT32_C(0xa05f8000)+offset);
}
static void fault_store(uint32_t at,uint32_t value,unsigned shift) {
    uintptr_t address=UINT32_C(0xa5000000)+at;
    if(shift==1u)*(volatile uint16_t *)address=(uint16_t)value;
    else *(volatile uint32_t *)address=value;
}
static void fault_border(void) {
    *(volatile uint32_t *)(uintptr_t)UINT32_C(0xa05f8040)=UINT32_C(0xff0000);
}
#endif
/* Original five-by-seven glyphs: hexadecimal digits followed by R,E,T. */
static const uint8_t fault_glyphs[19][7]={
    {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14},{14,17,17,15,1,1,14},
    {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
    {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
    {30,17,17,30,20,18,17},{31,16,16,30,16,16,31},
    {31,4,4,4,4,4,4}
};
static int fault_frame_valid(uint32_t base,uint32_t stride,uint32_t line,uint32_t rows) {
    /* All decoded factors are <=1024 or8184; these products cannot wrap32.
     * Check the whole displayed field, not merely this banner's pixels. */
    uint32_t bytes=(rows-1u)*stride+line;
    return !(base&3u) && base<FAULT_VRAM_BYTES && bytes<=FAULT_VRAM_BYTES-base;
}
static void fault_field(uint32_t base,uint32_t stride,unsigned shift,uint32_t background,
    uint32_t foreground,uint32_t reason,uint32_t detail) {
    for(unsigned y=FAULT_Y;y<FAULT_Y+FAULT_H;y++)
        for(unsigned x=FAULT_X;x<FAULT_X+FAULT_W;x++)
            fault_store(base+y*stride+(x<<shift),background,shift);
    for(unsigned line=0u;line<2u;line++)for(unsigned n=0u;n<(line?8u:12u);n++) {
        unsigned glyph;
        if(!line && n<3u)glyph=16u+n;
        else if(!line && n==3u)continue;
        else {
            unsigned digit=line?n:n-4u;
            glyph=((line?detail:reason)>>((7u-digit)*4u))&15u;
        }
        for(unsigned row=0u;row<7u;row++)for(unsigned column=0u;column<5u;column++)
            if(fault_glyphs[glyph][row]&(16u>>column))
                for(unsigned dy=0u;dy<2u;dy++)for(unsigned dx=0u;dx<2u;dx++) {
                    unsigned y=12u+line*18u+row*2u+dy,x=12u+n*12u+column*2u+dx;
                    fault_store(base+y*stride+(x<<shift),foreground,shift);
                }
    }
}
void kui_smash_return_fault_display(uint32_t reason,uint32_t detail) {
    uint32_t cfg=fault_read(0x44u),size=fault_read(0x5cu),il=fault_read(0xd0u);
    uint32_t base=fault_read(0x50u),odd=fault_read(0x54u);
    unsigned mode=(cfg>>2u)&3u,shift=mode==3u?2u:1u;
    uint32_t words=(size&1023u)+1u,line=words*4u,rows=((size>>10u)&1023u)+1u;
    uint32_t modulo=(size>>20u)&1023u,stride=(words-1u+modulo)*4u;
    /* Packed24 needs byte stores or neighbour preservation, neither is used
     * in this terminal path. Unknown control/size bits are also refused. */
    if(!(cfg&1u) || (cfg&~UINT32_C(0x0080000f)) || (size&UINT32_C(0xc0000000)) ||
       mode==2u || !modulo || stride<line || (line>>shift)<FAULT_X+FAULT_W ||
       rows<FAULT_Y+FAULT_H || !fault_frame_valid(base,stride,line,rows) ||
       ((il&16u) && !fault_frame_valid(odd,stride,line,rows))) {fault_border();return;}
    uint32_t foreground=mode==0u?UINT32_C(0x7fff):mode==1u?UINT32_C(0xffff):UINT32_C(0xffffff);
    uint32_t background=mode==0u?UINT32_C(0x5000):mode==1u?UINT32_C(0xa000):UINT32_C(0xa00000);
    fault_field(base,stride,shift,background,foreground,reason,detail);
    if(il&16u)fault_field(odd,stride,shift,background,foreground,reason,detail);
}
