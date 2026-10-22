/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/shell_font.h"
#include <limits.h>
#include <stddef.h>

struct glyph { uint16_t offset;uint8_t width,height,stride,advance,y; };
#include "shell_font_data.inc"

static const struct glyph *glyph_for(unsigned char code,bool large) {
    if(code<32 || code>126) code='?';
    if(large) return &large_glyphs[code-32];
    return &small_glyphs[code-32];
}
static unsigned font_width(const char *text,bool large,unsigned spacing) {
    if(!text) return 0;
    unsigned width=0;
    for(const unsigned char *p=(const unsigned char *)text;*p;p++) {
        unsigned step=glyph_for(*p,large)->advance;
        if(p!=(const unsigned char *)text) step+=spacing;
        if(width>UINT_MAX-step) return UINT_MAX;
        width+=step;
    }
    return width;
}
unsigned kui_shell_font_width(const char *text,bool large) {
    return font_width(text,large,KUI_SHELL_FONT_LETTER_SPACING);
}
unsigned kui_shell_font_width_crisp(const char *text,bool large) {
    return font_width(text,large,0);
}
static uint16_t blend(uint16_t background,uint16_t color,unsigned alpha) {
    unsigned inverse=15-alpha;
    unsigned r=(((color>>11)&31u)*alpha+((background>>11)&31u)*inverse+7u)/15u;
    unsigned g=(((color>>5)&63u)*alpha+((background>>5)&63u)*inverse+7u)/15u;
    unsigned b=((color&31u)*alpha+(background&31u)*inverse+7u)/15u;
    return (uint16_t)((r<<11)|(g<<5)|b);
}
static void font_draw(uint16_t *frame,int x,int y,uint16_t color,
                      const char *text,bool large,bool crisp) {
    unsigned height=large?KUI_SHELL_FONT_LARGE_HEIGHT:KUI_SHELL_FONT_SMALL_HEIGHT;
    if(!frame || !text || x>=640 || y>=480 || (int64_t)y+height<=0) return;
    const uint8_t *pixels=large?large_pixels:small_pixels;
    int64_t pen=x;
    for(const unsigned char *p=(const unsigned char *)text;*p && pen<640;p++) {
        const struct glyph *g=glyph_for(*p,large);
        if(pen+g->width>0) {
            unsigned first=pen<0?(unsigned)-pen:0;
            unsigned last=pen+g->width>640?(unsigned)(640-pen):g->width;
            for(unsigned row=0;row<g->height;row++) {
                int64_t dy=(int64_t)y+g->y+row;
                if(dy<0 || dy>=480) continue;
                const uint8_t *mask=pixels+g->offset+row*g->stride;
                for(unsigned col=first;col<last;col++) {
                    unsigned alpha=(mask[col/2]>>((col&1)?0:4))&15u;
                    if(!alpha || (crisp && alpha<8u)) continue;
                    uint16_t *target=frame+(size_t)dy*640+(size_t)(pen+col);
                    *target=crisp || alpha==15?color:blend(*target,color,alpha);
                }
            }
        }
        pen+=g->advance+(crisp?0:KUI_SHELL_FONT_LETTER_SPACING);
    }
}
void kui_shell_font_draw(uint16_t *frame,int x,int y,uint16_t color,
                         const char *text,bool large) {
    font_draw(frame,x,y,color,text,large,false);
}
void kui_shell_font_draw_crisp(uint16_t *frame,int x,int y,uint16_t color,
                               const char *text,bool large) {
    font_draw(frame,x,y,color,text,large,true);
}
