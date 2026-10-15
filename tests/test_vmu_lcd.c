/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_lcd.h"
#include "kui/version.h"
#include <dc/maple.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned attached,enumerations,submissions;
static maple_device_t devices[MAPLE_PORT_COUNT*MAPLE_UNIT_COUNT];
static struct kui_vmu_lcd_bitmap expected;
maple_device_t *maple_enum_type(int index,uint32_t function) {
    assert(function==MAPLE_FUNC_LCD && index>=0 && (unsigned)index<sizeof(devices)/sizeof(*devices));
    assert((unsigned)index==enumerations++);
    return (unsigned)index<attached?&devices[index]:NULL;
}
int vmu_draw_lcd_rotated(maple_device_t *device,const void *bitmap) {
    assert(device==&devices[submissions] && ((uintptr_t)bitmap&3u)==0);
    assert(!memcmp(bitmap,&expected,KUI_VMU_LCD_BYTES));
    /* Failures must be skipped without retry or a blocking completion wait. */
    return submissions++%3u==0?0:-1;
}
static unsigned pixel(const struct kui_vmu_lcd_bitmap *bitmap,unsigned x,unsigned y) {
    return (((const uint8_t *)bitmap->words)[y*6u+x/8u]>>(7u-x%8u))&1u;
}
static void bands(const char *version,unsigned first,unsigned end) {
    struct {uint32_t before;struct kui_vmu_lcd_bitmap bitmap;uint32_t after;} g;
    memset(&g,0xa5,sizeof(g));kui_vmu_lcd_render(&g.bitmap,version);
    assert(g.before==UINT32_C(0xa5a5a5a5) && g.after==UINT32_C(0xa5a5a5a5));
    unsigned logo=0,version_pixels=0;
    for(unsigned y=0;y<32u;y++)for(unsigned x=0;x<48u;x++) {
        unsigned p=pixel(&g.bitmap,x,y);
        if(y>=3u && y<17u) {logo+=p;assert(!p || (x>=1u && x<=46u));}
        else if(y>=first && y<end)version_pixels+=p;
        else assert(!p);
    }
    assert(logo==196u && version_pixels);
    /* Independent K glyph anchors and separator cell spacing. */
    assert(pixel(&g.bitmap,1,3) && pixel(&g.bitmap,9,3) && !pixel(&g.bitmap,5,3));
    assert(!pixel(&g.bitmap,11,3) && !pixel(&g.bitmap,12,3));
    assert(pixel(&g.bitmap,13,9) && !pixel(&g.bitmap,13,7));
}
static uint32_t reversed(uint32_t value) {
    uint32_t out=0;for(unsigned i=0;i<32;i++)out=(out<<1)|((value>>i)&1u);return out;
}
static void orientation(void) {
    struct kui_vmu_lcd_bitmap bitmap;kui_vmu_lcd_render(&bitmap,"2.0-rc10");
    const uint8_t *in=(const uint8_t *)bitmap.words;uint8_t wire[192];
    /* Pinned KOS vmu_draw_lcd_rotated reverses the 48 32-bit words and bits.
     * Serialize SH-4 little-endian words explicitly, independent of host ABI. */
    for(unsigned i=0;i<48;i++) {
        const uint8_t *p=in+(47u-i)*4u;
        uint32_t n=(uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
        n=reversed(n);for(unsigned j=0;j<4;j++)wire[i*4u+j]=(uint8_t)(n>>(8u*j));
    }
    for(unsigned y=0;y<32;y++)for(unsigned x=0;x<48;x++) {
        unsigned rx=47u-x,ry=31u-y;
        assert(pixel(&bitmap,x,y)==((wire[ry*6u+rx/8u]>>(7u-rx%8u))&1u));
    }
    assert(!pixel(&bitmap,0,23) && pixel(&bitmap,1,23) && pixel(&bitmap,0,29));
    assert(pixel(&bitmap,46,24)); /* Eight 5x7 cells occupy 47 pixels, no crop. */
}
static void startup(void) {
    kui_vmu_lcd_render(&expected,KUI_VERSION);
    const unsigned cases[]={0u,3u,MAPLE_PORT_COUNT*MAPLE_UNIT_COUNT};
    for(unsigned i=0;i<sizeof(cases)/sizeof(*cases);i++) {
        attached=cases[i];enumerations=submissions=0;kui_vmu_lcd_startup();
        assert(submissions==attached);
        assert(enumerations==attached+(attached<MAPLE_PORT_COUNT*MAPLE_UNIT_COUNT));
    }
    /* No VMU block/file operations are linked into this test or helper. */
}
int main(int argc,char **argv) {
    _Static_assert(sizeof(struct kui_vmu_lcd_bitmap)==192u,"exact LCD frame");
    _Static_assert(_Alignof(struct kui_vmu_lcd_bitmap)>=4u,"KOS requires aligned words");
    if(argc==3 && !strcmp(argv[1],"--bitmap")) {
        struct kui_vmu_lcd_bitmap bitmap;kui_vmu_lcd_render(&bitmap,KUI_VERSION);
        FILE *out=fopen(argv[2],"wb");assert(out);
        assert(fwrite(&bitmap,1,sizeof(bitmap),out)==sizeof(bitmap) && !fclose(out));return 0;
    }
    bands("2.0-rc10",23,30);bands("123456789",23,28);bands("123456789012",23,28);
    bands("1234567890123",20,31);bands("123456789012123456789012",20,31);
    bands("123456789012123456789012-long",20,31);bands(NULL,23,30);bands("",23,30);
    struct kui_vmu_lcd_bitmap a,b;
    kui_vmu_lcd_render(&a,"123456789012123456789012-long");
    kui_vmu_lcd_render(&b,"12345678901212345678901+");assert(!memcmp(&a,&b,sizeof(a)));
    kui_vmu_lcd_render(&a,"\001");kui_vmu_lcd_render(&b,"?");assert(!memcmp(&a,&b,sizeof(a)));
    kui_vmu_lcd_render(NULL,KUI_VERSION);orientation();startup();
    puts("PASS VMU LCD: geometry, guards, aligned 192-byte bitmap, KOS wire orientation, version fallback and one-shot LCD-only startup");
    return 0;
}
