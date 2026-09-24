/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/viewport.h"
#include <string.h>
void kui_viewport_inset(uint16_t *pixels,unsigned width,unsigned height,unsigned inset,uint16_t border) {
    if(!pixels || !inset || width>640 || inset>=width/2) return;
    uint16_t row[640];unsigned inner=width-2*inset;
    for(unsigned y=0;y<height;y++) {
        uint16_t *out=pixels+y*width;memcpy(row,out,width*sizeof(*row));
        for(unsigned x=0;x<inset;x++) out[x]=out[width-1-x]=border;
        for(unsigned x=0;x<inner;x++) out[inset+x]=row[x*(width-1)/(inner-1)];
    }
}
