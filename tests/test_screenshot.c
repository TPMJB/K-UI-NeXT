/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/screenshot.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct output {uint8_t data[128];size_t size,calls,fail_at;bool count_only;};
static bool write_bytes(void *context,const void *data,size_t size) {
    struct output *out=context;++out->calls;
    if(out->calls==out->fail_at)return false;
    if(!out->count_only) {
        assert(out->size+size<=sizeof(out->data));memcpy(out->data+out->size,data,size);
    }
    out->size+=size;return true;
}
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void pixels_and_padding(void) {
    const uint16_t pixels[]={0xf800,0x07e0,0x001f,0xdead,0xffff,0x0000,0x0821};
    struct output out={0};
    assert(kui_screenshot_bmp_rgb565(pixels,7,3,2,4,write_bytes,&out));
    assert(out.size==78 && out.calls==3 && !memcmp(out.data,"BM",2));
    assert(get32(out.data+2)==78 && get32(out.data+10)==54 && get32(out.data+14)==40);
    assert(get32(out.data+18)==3 && get32(out.data+22)==2);
    assert(out.data[26]==1 && out.data[28]==24 && get32(out.data+30)==0 && get32(out.data+34)==24);
    static const uint8_t expected[]={255,255,255,0,0,0,8,4,8,0,0,0,
        0,0,255,0,255,0,255,0,0,0,0,0};
    assert(!memcmp(out.data+54,expected,sizeof(expected)));
    puts("PASS screenshot BMP: dimensions, bottom-up BGR pixels, channel expansion and zero row padding");
}
static void bounds_and_failures(void) {
    uint16_t pixel=0;uint32_t bytes=1234;struct output out={0};
    assert(!kui_screenshot_bmp_size(1,0,1,1,&bytes) && bytes==1234);
    assert(!kui_screenshot_bmp_size(SIZE_MAX,641,1,641,&bytes));
    assert(!kui_screenshot_bmp_size(SIZE_MAX,1,481,1,&bytes));
    assert(!kui_screenshot_bmp_size(4,2,2,1,&bytes));
    assert(!kui_screenshot_bmp_size(6,3,2,4,&bytes));
    assert(!kui_screenshot_bmp_size(1,1,1,1,NULL));
    assert(!kui_screenshot_bmp_rgb565(&pixel,0,1,1,1,write_bytes,&out));
    assert(!kui_screenshot_bmp_rgb565(NULL,1,1,1,1,write_bytes,&out));
    assert(!kui_screenshot_bmp_rgb565(&pixel,1,1,1,1,NULL,&out) && out.calls==0);
    for(size_t failed=1;failed<=2;failed++) {
        out=(struct output){.fail_at=failed};
        assert(!kui_screenshot_bmp_rgb565(&pixel,1,1,1,1,write_bytes,&out));
        assert(out.calls==failed);
    }
    assert(kui_screenshot_bmp_size(640u*480u,640,480,640,&bytes) && bytes==921654u);
    uint16_t *full=calloc(640u*480u,sizeof(*full));assert(full);
    out=(struct output){.count_only=true};
    assert(kui_screenshot_bmp_rgb565(full,640u*480u,640,480,640,write_bytes,&out));
    assert(out.size==921654u && out.calls==481u);free(full);
    puts("PASS screenshot BMP: invalid geometry writes nothing, callback failures stop, full 640x480 size");
}
int main(void) {pixels_and_padding();bounds_and_failures();return 0;}
