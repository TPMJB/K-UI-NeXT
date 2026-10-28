/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent filesystem fixtures use published offsets and FAT values. */
#include "kui/vmu_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

struct image {
    uint8_t data[256][512];
    unsigned events, reads, writes, syncs, fail_at, flip_read_at;
    unsigned event_kind[1024], event_block[1024];
};
static struct image image;
static uint8_t blank_formatted[256][512];
static struct kui_vmu_image_workspace work;
static struct kui_vmu_image_summary summary;

static int event(unsigned kind, unsigned block) {
    assert(image.events < 1024);
    image.event_kind[image.events] = kind;
    image.event_block[image.events] = block;
    ++image.events;
    return image.fail_at && image.fail_at == image.events;
}
static int read_block(void *context, uint16_t block, void *data) {
    assert(context == &image && block < 256);
    ++image.reads;
    if(event(1, block)) return -1;
    memcpy(data, image.data[block], 512);
    if(image.flip_read_at == image.events) ((uint8_t *)data)[17] ^= 0x80;
    return 0;
}
static int write_block(void *context, uint16_t block, const void *data) {
    assert(context == &image && block < 256);
    ++image.writes;
    if(event(2, block)) return -1;
    memcpy(image.data[block], data, 512);
    return 0;
}
static int sync_image(void *context) {
    assert(context == &image);
    ++image.syncs;
    return event(3, 0) ? -1 : 0;
}
static const struct kui_vmu_image_io io = {&image,read_block,write_block,sync_image};

static void reset_events(void) {
    image.events = image.reads = image.writes = image.syncs = 0;
    image.fail_at = image.flip_read_at = 0;
}
static void restore(void) {
    memcpy(image.data, blank_formatted, sizeof(image.data));
    reset_events();
}
static void put16(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}
static void valid(unsigned files, unsigned blocks, unsigned games) {
    summary = (struct kui_vmu_image_summary){999,999,999,999};
    assert(kui_vmu_image_validate(&io, 131072, &work, &summary) == KUI_VMU_IMAGE_OK);
    assert(summary.files == files && summary.used_blocks == blocks);
    assert(summary.game_files == games && summary.free_blocks == 200 - blocks);
    assert(image.writes == 0 && image.syncs == 0);
}
static void invalid(int expected) {
    summary = (struct kui_vmu_image_summary){999,999,999,999};
    assert(kui_vmu_image_validate(&io, 131072, &work, &summary) == expected);
    assert(!summary.files && !summary.game_files && !summary.free_blocks && !summary.used_blocks);
    assert(image.writes == 0 && image.syncs == 0);
}
static uint8_t *entry(unsigned index) {
    assert(index < 208);
    return image.data[253 - index / 16] + (index % 16) * 32;
}
static void add_file(unsigned index, const char *name, const unsigned *blocks,
                     unsigned count, int game) {
    uint8_t *e = entry(index);
    memset(e, 0, 32);
    e[0] = game ? 0xcc : 0x33;
    put16(e + 2, blocks[0]);
    assert(strlen(name) <= 12);
    memcpy(e + 4, name, strlen(name));
    put16(e + 24, count);
    for(unsigned i = 0; i < count; ++i)
        put16(image.data[254] + blocks[i] * 2, i + 1 == count ? 0xfffa : blocks[i + 1]);
}

static void format_fixtures(void) {
    memset(&image, 0, sizeof(image));
    assert(kui_vmu_image_format_blank(&io, 131072, NULL, &work) == 0);
    assert(image.reads == 512 && image.writes == 256 && image.syncs == 2);
    assert(image.events == 770);
    for(unsigned i = 0; i < 256; ++i)
        assert(image.event_kind[i] == 1 && image.event_block[i] == i);
    for(unsigned i = 0; i < 255; ++i)
        assert(image.event_kind[256+i] == 2 && image.event_block[256+i] == i);
    assert(image.event_kind[511] == 3);
    assert(image.event_kind[512] == 2 && image.event_block[512] == 255);
    assert(image.event_kind[513] == 3);
    for(unsigned i = 0; i < 256; ++i)
        assert(image.event_kind[514+i] == 1 && image.event_block[514+i] == i);

    uint8_t expected[512] = {0};
    for(unsigned block = 0; block < 254; ++block)
        assert(!memcmp(image.data[block], expected, 512));
    memset(expected, 0x55, 16);
    const uint8_t time[] = {0x20,0,1,1,0,0,0,5};
    const uint8_t geometry[] = {
        255,0,0,0,255,0,254,0,1,0,253,0,13,0,0,0,200,0,31,0,0,0,0,0
    };
    memcpy(expected + 48, time, 8);
    memcpy(expected + 64, geometry, 24);
    assert(!memcmp(image.data[255], expected, 512));
    for(unsigned i = 0; i < 241; ++i) {
        assert(image.data[254][i*2] == 0xfc);
        assert(image.data[254][i*2+1] == 0xff);
    }
    static const uint8_t reserved[] = {
        0xfa,0xff,0xf1,0,0xf2,0,0xf3,0,0xf4,0,0xf5,0,0xf6,0,
        0xf7,0,0xf8,0,0xf9,0,0xfa,0,0xfb,0,0xfc,0,0xfa,0xff,0xfa,0xff
    };
    assert(!memcmp(image.data[254] + 482, reserved, sizeof(reserved)));
    memcpy(blank_formatted, image.data, sizeof(blank_formatted));
    reset_events(); valid(0,0,0);

    /* Free blocks may contain previous raw data; they are not orphan saves. */
    restore(); memset(image.data[42], 0x6e, 512); valid(0,0,0);
    restore(); assert(kui_vmu_image_format_blank(&io, 131072, NULL, &work) == KUI_VMU_IMAGE_NOT_BLANK);
    assert(!image.writes && !image.syncs);
    for(unsigned block = 0; block < 256; ++block) {
        memset(image.data, 0, sizeof(image.data)); reset_events();
        image.data[block][block] = 1;
        assert(kui_vmu_image_format_blank(&io, 131072, NULL, &work) == KUI_VMU_IMAGE_NOT_BLANK);
        assert(!image.writes && !image.syncs && image.data[block][block] == 1);
    }
}

static void formats_and_dates(void) {
    const uint8_t leap[] = {0x20,0x24,0x02,0x29,0x23,0x59,0x59,3};
    memset(image.data, 0, sizeof(image.data)); reset_events();
    assert(!kui_vmu_image_format_blank(&io, 131072, leap, &work));
    assert(!memcmp(image.data[255] + 48, leap, 8));
    static const unsigned offsets[] = {0,1,2,3,4,5,6,7,1,2};
    static const uint8_t values[] = {0x2a,0xfa,0x13,0x30,0x24,0x60,0x60,7,0x23,0};
    for(unsigned i = 0; i < sizeof(offsets)/sizeof(offsets[0]); ++i) {
        uint8_t stamp[8]; memcpy(stamp, leap, 8); stamp[offsets[i]] = values[i];
        reset_events();
        assert(kui_vmu_image_format_blank(&io, 131072, stamp, &work) == KUI_VMU_IMAGE_ARGUMENT);
        assert(!image.events);
    }
    uint8_t century_leap[] = {0x20,0,2,0x29,0,0,0,0};
    memset(image.data, 0, sizeof(image.data)); reset_events();
    assert(!kui_vmu_image_format_blank(&io, 131072, century_leap, &work));
    century_leap[0] = 0x19; reset_events();
    assert(kui_vmu_image_format_blank(&io, 131072, century_leap, &work) == KUI_VMU_IMAGE_ARGUMENT);
    assert(!image.events);
}

static void filesystem_cases(void) {
    const unsigned a[] = {199,50,3}, b[] = {11,12}, game[] = {0,1,2};
    restore(); add_file(0,"SAVE-A",a,3,0); add_file(17,"SAVE-B",b,2,0); valid(2,5,0);
    restore(); add_file(0,"GAME",game,3,1); add_file(100,"SAVE",a,3,0); valid(2,6,1);
    restore();
    for(unsigned i = 0; i < 200; ++i) {
        char name[13]; (void)snprintf(name,sizeof(name),"SAVE%03u",i);
        add_file(i,name,&i,1,0);
    }
    valid(200,200,0);
    restore(); unsigned chain[200];
    for(unsigned i = 0; i < 200; ++i) chain[i] = 199-i;
    add_file(207,"FULL-CARD",chain,200,0); put16(entry(207) + 26,199); valid(1,200,0);

    /* Named geometry remains bounded even when opaque metadata varies. */
    static const unsigned geometry_offsets[] = {0,16,70,72,74,76,78,80};
    static const unsigned geometry_values[] = {0,2,253,2,252,12,124,241};
    for(unsigned i = 0; i < sizeof(geometry_offsets)/sizeof(geometry_offsets[0]); ++i) {
        restore(); put16(image.data[255] + geometry_offsets[i],geometry_values[i]);
        invalid(KUI_VMU_IMAGE_GEOMETRY);
    }
    restore(); image.data[255][16]=1; image.data[255][17]=255;
    put16(image.data[255]+78,123); valid(0,0,0);
    /* Unknown root fields are not storage-protocol media-info fields. The
     * validator must neither reject their values nor rewrite original bytes. */
    for(unsigned offset=0x40u;offset<512u;offset++) {
        if(offset>=0x46u && offset<0x52u) continue;
        restore(); image.data[255][offset]^=0xffu;
        uint8_t before[512];memcpy(before,image.data[255],sizeof(before));
        valid(0,0,0);assert(!memcmp(before,image.data[255],sizeof(before)));
    }
    restore();memset(image.data[255]+0x40u,0xffu,6u);
    memset(image.data[255]+0x52u,0xa7u,512u-0x52u);valid(0,0,0);
    for(unsigned i=0;i<sizeof(geometry_offsets)/sizeof(geometry_offsets[0]);i++) {
        restore();memset(image.data[255]+0x40u,0xffu,6u);
        memset(image.data[255]+0x52u,0xa7u,512u-0x52u);
        put16(image.data[255]+geometry_offsets[i],geometry_values[i]);
        invalid(KUI_VMU_IMAGE_GEOMETRY);
    }
    for(unsigned block = 200; block < 256; ++block) {
        restore(); image.data[254][block*2] ^= 1; invalid(KUI_VMU_IMAGE_FILESYSTEM);
    }

    /* Complete chains are required; free storage contents do not matter. */
    restore(); add_file(0,"A",a,3,0); put16(image.data[254]+3*2,199); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"A",a,3,0); put16(image.data[254]+50*2,200); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"A",a,3,0); put16(image.data[254]+50*2,0xfffc); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"A",a,3,0); put16(entry(0)+24,2); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); put16(image.data[254]+42*2,0xfffa); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); put16(image.data[254]+42*2,0xfff7); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"A",a,3,0); add_file(1,"B",a,3,0); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"SAME",a,3,0); add_file(1,"SAME",b,2,0); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"SAME",a,3,0); add_file(1,"SAME    ",b,2,0); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"A",a,3,0); memset(entry(0)+4,' ',12); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"AB",a,3,0); entry(0)[4]=0; invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"A",a,3,0); entry(0)[0]=0x34; invalid(KUI_VMU_IMAGE_FILESYSTEM);
    /* Any copy-protect byte is valid; Crazy Taxi 2 writes 0x10. */
    for(unsigned copy = 1; copy < 0xffu; copy += 0x0fu) {
        restore(); add_file(0,"A",a,3,0); entry(0)[1]=(uint8_t)copy; valid(1,3,0);
    }
    restore(); add_file(0,"A",a,3,0); put16(entry(0)+26,3); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"A",a,3,0); put16(entry(0)+24,0); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"A",a,3,0); put16(entry(0)+24,201); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"A",a,3,0); put16(entry(0)+2,65535); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); entry(200)[28]=1; invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"GAME",a,3,1); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    const unsigned broken_game[] = {0,2,1};
    restore(); add_file(0,"GAME",broken_game,3,1); invalid(KUI_VMU_IMAGE_FILESYSTEM);
    restore(); add_file(0,"GAME-A",game,3,1); add_file(1,"GAME-B",game,3,1); invalid(KUI_VMU_IMAGE_FILESYSTEM);
}

static void failure_injection(void) {
    for(unsigned failure = 1; failure <= 770; ++failure) {
        memset(image.data, 0, sizeof(image.data)); reset_events(); image.fail_at = failure;
        assert(kui_vmu_image_format_blank(&io, 131072, NULL, &work) == KUI_VMU_IMAGE_IO);
        assert(image.events == failure);
        if(failure <= 256) assert(!image.writes && !image.syncs);
        if(failure <= 513) assert(image.data[255][0] == 0);
    }
    memset(image.data, 0, sizeof(image.data)); reset_events(); image.flip_read_at = 515 + 42;
    assert(kui_vmu_image_format_blank(&io, 131072, NULL, &work) == KUI_VMU_IMAGE_VERIFY);
    assert(image.writes == 256 && image.syncs == 2);
    for(unsigned failure = 1; failure <= 270; ++failure) {
        restore(); image.fail_at = failure; invalid(KUI_VMU_IMAGE_IO);
        assert(image.events == failure);
    }
}

static void argument_cases(void) {
    restore();
    assert(kui_vmu_image_format_blank(NULL,131072,NULL,&work)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_format_blank(&io,131071,NULL,&work)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_format_blank(&io,131073,NULL,&work)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_format_blank(&io,131072,NULL,NULL)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_validate(NULL,131072,&work,&summary)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_validate(&io,131071,&work,&summary)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_validate(&io,131073,&work,&summary)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_validate(&io,131072,NULL,&summary)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_validate(&io,131072,&work,NULL)==KUI_VMU_IMAGE_ARGUMENT);
    struct kui_vmu_image_io copy=io;
    copy.write_block=NULL;
    assert(kui_vmu_image_format_blank(&copy,131072,NULL,&work)==KUI_VMU_IMAGE_ARGUMENT);
    copy=io; copy.sync=NULL;
    assert(kui_vmu_image_format_blank(&copy,131072,NULL,&work)==KUI_VMU_IMAGE_ARGUMENT);
    copy=io; copy.read_block=NULL;
    assert(kui_vmu_image_format_blank(&copy,131072,NULL,&work)==KUI_VMU_IMAGE_ARGUMENT);
    assert(kui_vmu_image_validate(&copy,131072,&work,&summary)==KUI_VMU_IMAGE_ARGUMENT);
    assert(!image.events);
}

int main(void) {
    format_fixtures();
    formats_and_dates();
    filesystem_cases();
    failure_injection();
    argument_cases();
    puts("PASS VMU image: standard geometry, all sectors, bounded chains, 1040 injected I/O failures");
    return 0;
}
