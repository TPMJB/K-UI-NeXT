/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/screenshot.h"
#include <string.h>

static void put16(uint8_t *p,uint16_t n) {
    p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);
}
static void put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(n>>(i*8u));
}
bool kui_screenshot_bmp_size(size_t pixel_count,unsigned width,unsigned height,
    unsigned stride,uint32_t *bytes) {
    if(!bytes || !width || width>KUI_SCREENSHOT_WIDTH_MAX ||
       !height || height>KUI_SCREENSHOT_HEIGHT_MAX || stride<width ||
       (size_t)(height-1u)>(SIZE_MAX-width)/stride ||
       (size_t)(height-1u)*stride+width>pixel_count)return false;
    uint32_t row=((uint32_t)width*3u+3u)&~UINT32_C(3);
    *bytes=54u+row*height;return true;
}
bool kui_screenshot_bmp_rgb565(const uint16_t *pixels,size_t pixel_count,
    unsigned width,unsigned height,unsigned stride,kui_screenshot_write_fn write,
    void *context) {
    uint32_t bytes;
    if(!pixels || !write || !kui_screenshot_bmp_size(pixel_count,width,height,stride,&bytes))return false;
    uint8_t header[54]={0};header[0]='B';header[1]='M';
    put32(header+2,bytes);put32(header+10,sizeof(header));
    put32(header+14,40u);put32(header+18,width);put32(header+22,height);
    put16(header+26,1u);put16(header+28,24u);put32(header+34,bytes-sizeof(header));
    if(!write(context,header,sizeof(header)))return false;
    uint8_t row[KUI_SCREENSHOT_WIDTH_MAX*3u];
    size_t row_bytes=((size_t)width*3u+3u)&~(size_t)3u;
    for(unsigned y=height;y>0u;y--) {
        const uint16_t *source=pixels+(size_t)(y-1u)*stride;
        memset(row,0,row_bytes);
        for(unsigned x=0;x<width;x++) {
            unsigned value=source[x],blue=value&31u,green=(value>>5)&63u,red=(value>>11)&31u;
            row[x*3u]=(uint8_t)((blue<<3)|(blue>>2));
            row[x*3u+1u]=(uint8_t)((green<<2)|(green>>4));
            row[x*3u+2u]=(uint8_t)((red<<3)|(red>>2));
        }
        if(!write(context,row,row_bytes))return false;
    }
    return true;
}
