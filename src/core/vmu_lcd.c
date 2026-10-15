/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_lcd.h"
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* Independently drawn bitmap glyphs. Row bit 4 (large) / bit 2 (small) is left.
 * The tiny lowercase forms share a few shapes with capitals at this size. */
struct glyph {char code;uint8_t normal[7],tiny[5];};
static const struct glyph glyphs[]={
    {'0',{14,17,19,21,25,17,14},{7,5,5,5,7}},
    {'1',{4,12,4,4,4,4,14},{2,6,2,2,7}},
    {'2',{14,17,1,2,4,8,31},{6,1,2,4,7}},
    {'3',{30,1,1,14,1,1,30},{6,1,2,1,6}},
    {'4',{2,6,10,18,31,2,2},{5,5,7,1,1}},
    {'5',{31,16,16,30,1,1,30},{7,4,6,1,6}},
    {'6',{6,8,16,30,17,17,14},{3,4,6,5,2}},
    {'7',{31,1,2,4,8,8,8},{7,1,2,2,2}},
    {'8',{14,17,17,14,17,17,14},{2,5,2,5,2}},
    {'9',{14,17,17,15,1,2,12},{2,5,3,1,6}},
    {'A',{14,17,17,31,17,17,17},{2,5,7,5,5}},
    {'B',{30,17,17,30,17,17,30},{6,5,6,5,6}},
    {'C',{14,17,16,16,16,17,14},{3,4,4,4,3}},
    {'D',{30,17,17,17,17,17,30},{6,5,5,5,6}},
    {'E',{31,16,16,30,16,16,31},{7,4,6,4,7}},
    {'F',{31,16,16,30,16,16,16},{7,4,6,4,4}},
    {'G',{14,17,16,23,17,17,15},{3,4,5,5,3}},
    {'H',{17,17,17,31,17,17,17},{5,5,7,5,5}},
    {'I',{31,4,4,4,4,4,31},{7,2,2,2,7}},
    {'J',{7,2,2,2,2,18,12},{1,1,1,5,2}},
    {'K',{17,18,20,24,20,18,17},{5,5,6,5,5}},
    {'L',{16,16,16,16,16,16,31},{4,4,4,4,7}},
    {'M',{17,27,21,21,17,17,17},{5,7,7,5,5}},
    {'N',{17,25,25,21,19,19,17},{5,7,7,7,5}},
    {'O',{14,17,17,17,17,17,14},{2,5,5,5,2}},
    {'P',{30,17,17,30,16,16,16},{6,5,6,4,4}},
    {'Q',{14,17,17,17,21,18,13},{2,5,5,3,1}},
    {'R',{30,17,17,30,20,18,17},{6,5,6,5,5}},
    {'S',{15,16,16,14,1,1,30},{3,4,2,1,6}},
    {'T',{31,4,4,4,4,4,4},{7,2,2,2,2}},
    {'U',{17,17,17,17,17,17,14},{5,5,5,5,7}},
    {'V',{17,17,17,17,17,10,4},{5,5,5,5,2}},
    {'W',{17,17,17,21,21,27,17},{5,5,7,7,5}},
    {'X',{17,17,10,4,10,17,17},{5,5,2,5,5}},
    {'Y',{17,17,10,4,4,4,4},{5,5,2,2,2}},
    {'Z',{31,1,2,4,8,16,31},{7,1,2,4,7}},
    {'a',{0,0,14,1,15,17,15},{0,3,5,7,5}},
    {'b',{16,16,22,25,17,17,30},{4,4,6,5,6}},
    {'c',{0,0,14,16,16,17,14},{0,3,4,4,3}},
    {'d',{1,1,13,19,17,17,15},{1,1,3,5,3}},
    {'e',{0,0,14,17,31,16,14},{0,2,5,6,3}},
    {'f',{6,9,8,28,8,8,8},{1,2,7,2,2}},
    {'g',{0,15,17,17,15,1,14},{0,3,5,3,6}},
    {'h',{16,16,22,25,17,17,17},{4,4,6,5,5}},
    {'i',{4,0,12,4,4,4,14},{2,0,2,2,2}},
    {'j',{2,0,6,2,2,18,12},{1,0,1,1,6}},
    {'k',{16,16,18,20,24,20,18},{4,4,5,6,5}},
    {'l',{12,4,4,4,4,4,14},{6,2,2,2,3}},
    {'m',{0,0,26,21,21,21,21},{0,7,7,5,5}},
    {'n',{0,0,22,25,17,17,17},{0,6,5,5,5}},
    {'o',{0,0,14,17,17,17,14},{0,2,5,5,2}},
    {'p',{0,0,30,17,30,16,16},{0,6,5,6,4}},
    {'q',{0,0,15,17,15,1,1},{0,3,5,3,1}},
    {'r',{0,0,22,25,16,16,16},{0,6,5,4,4}},
    {'s',{0,0,15,16,14,1,30},{0,3,6,1,6}},
    {'t',{8,8,28,8,8,9,6},{2,7,2,2,3}},
    {'u',{0,0,17,17,17,19,13},{0,5,5,5,3}},
    {'v',{0,0,17,17,17,10,4},{0,5,5,5,2}},
    {'w',{0,0,17,21,21,21,10},{0,5,5,7,7}},
    {'x',{0,0,17,10,4,10,17},{0,5,2,2,5}},
    {'y',{0,0,17,17,15,1,14},{0,5,3,1,6}},
    {'z',{0,0,31,2,4,8,31},{0,7,1,4,7}},
    {'.',{0,0,0,0,0,12,12},{0,0,0,0,2}},
    {'-',{0,0,0,31,0,0,0},{0,0,7,0,0}},
    {'+',{0,4,4,31,4,4,0},{0,2,7,2,0}},
    {'_',{0,0,0,0,0,0,31},{0,0,0,0,7}},
    {' ',{0,0,0,0,0,0,0},{0,0,0,0,0}},
    {'?',{14,17,1,2,4,0,4},{6,1,2,0,2}}
};
_Static_assert(sizeof(struct kui_vmu_lcd_bitmap)==KUI_VMU_LCD_BYTES,"LCD frame is 192 bytes");
_Static_assert(_Alignof(struct kui_vmu_lcd_bitmap)>=4,"KOS LCD word alignment");
static const struct glyph *glyph(char code) {
    for(unsigned i=0;i<sizeof(glyphs)/sizeof(*glyphs);i++)if(glyphs[i].code==code)return &glyphs[i];
    return &glyphs[sizeof(glyphs)/sizeof(*glyphs)-1u];
}
static void pixel(struct kui_vmu_lcd_bitmap *out,unsigned x,unsigned y) {
    if(x<KUI_VMU_LCD_WIDTH && y<KUI_VMU_LCD_HEIGHT)
        ((uint8_t *)out->words)[y*6u+x/8u]|=(uint8_t)(0x80u>>(x%8u));
}
static void text(struct kui_vmu_lcd_bitmap *out,const char *value,unsigned count,
    unsigned y,bool tiny,unsigned scale) {
    unsigned width=tiny?3u:5u,height=tiny?5u:7u;
    unsigned span=((width+1u)*count-1u)*scale;
    unsigned left=(KUI_VMU_LCD_WIDTH-span)/2u;
    for(unsigned n=0;n<count;n++) {
        const struct glyph *g=glyph(value[n]);const uint8_t *rows=tiny?g->tiny:g->normal;
        for(unsigned row=0;row<height;row++)for(unsigned column=0;column<width;column++)
            if(rows[row]&(1u<<(width-column-1u)))
                for(unsigned dy=0;dy<scale;dy++)for(unsigned dx=0;dx<scale;dx++)
                    pixel(out,left+n*(width+1u)*scale+column*scale+dx,y+row*scale+dy);
    }
}
void kui_vmu_lcd_render(struct kui_vmu_lcd_bitmap *out,const char *version) {
    if(!out)return;
    memset(out,0,sizeof(*out));
    text(out,"K-UI",4u,3u,false,2u);
    unsigned length=0;char visible[24];
    if(version)while(length<25u && version[length])++length;
    if(!length) {version="?";length=1;}
    unsigned keep=length>24u?24u:length;
    memcpy(visible,version,keep);
    if(length>24u)visible[23]='+';
    if(keep<=8u)text(out,visible,keep,23u,false,1u);
    else if(keep<=12u)text(out,visible,keep,23u,true,1u);
    else {
        text(out,visible,12u,20u,true,1u);
        text(out,visible+12u,keep-12u,26u,true,1u);
    }
}
