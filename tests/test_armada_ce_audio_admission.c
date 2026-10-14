/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_audio_admission.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static const uint8_t sha[32]={
    0xa7,0x62,0x8c,0x78,0x09,0x0f,0xce,0x2c,0x93,0x01,0x2d,0x5e,0x4d,0xb6,0x14,0x20,
    0x96,0xe1,0x38,0xdc,0xeb,0x7e,0xbb,0xf5,0x55,0x14,0x2e,0x90,0x2c,0xa0,0x3c,0x1f};
static struct kui_retail_manifest valid(void) {
    static const uint32_t starts[]={0,756,45000,149842,434062};
    struct kui_retail_manifest m={0};m.track_count=5;m.session_lba=45000;
    m.boot_lba=548388;m.boot_bytes=1253376;m.storage_transport=KUI_STORAGE_SCI;
    m.reader=KUI_RETAIL_READER_ASYNC;
    memcpy(m.product,"T40301N 00",11);memcpy(m.bootfile,"0WINCEOS.BIN",13);
    for(unsigned i=0;i<5;i++) {
        m.slots[i].track.start_lba=starts[i];
        m.slots[i].track.control=i==1 || i==3?0:4;
    }
    return m;
}
static bool admit(const struct kui_retail_manifest *m,const uint8_t *digest) {
    return kui_armada_ce_audio_admit(m,0x684f8040,0x6200729c,0xb9f755bc,digest);
}
int main(void) {
    struct kui_retail_manifest m=valid();assert(admit(&m,sha));
    m.flags=KUI_RETAIL_IMAGE_BOOT_CRC;assert(admit(&m,sha));
    assert(!admit(NULL,sha));assert(!admit(&m,NULL));
    for(unsigned i=0;i<32;i++) {
        uint8_t changed[32];memcpy(changed,sha,32);changed[i]^=1;
        assert(!admit(&m,changed));
    }
    assert(!kui_armada_ce_audio_admit(&m,0x684f8041,0x6200729c,0xb9f755bc,sha));
    assert(!kui_armada_ce_audio_admit(&m,0x684f8040,0x6200729d,0xb9f755bc,sha));
    assert(!kui_armada_ce_audio_admit(&m,0x684f8040,0x6200729c,0xb9f755bd,sha));
    for(unsigned i=0;i<5;i++) {
        m=valid();m.slots[i].track.start_lba++;assert(!admit(&m,sha));
        m=valid();m.slots[i].track.control^=4;assert(!admit(&m,sha));
        m=valid();m.slots[i].track.control|=KUI_RETAIL_TRACK_COOKED;assert(!admit(&m,sha));
        m=valid();m.slots[i].track.first_extent=0x100;assert(!admit(&m,sha));
    }
    for(unsigned i=0;i<14;i++) {
        m=valid();
        switch(i) {
        case 0:m.track_count=4;break;case 1:m.track_count=6;break;
        case 2:m.session_lba++;break;case 3:m.boot_lba++;break;
        case 4:m.boot_bytes--;break;case 5:m.storage_transport=KUI_STORAGE_SCIF;break;
        case 6:m.storage_transport=KUI_STORAGE_IDE;break;case 7:m.reader=KUI_RETAIL_READER_STANDARD;break;
        case 8:m.reader=KUI_RETAIL_READER_ASYNC_EAGER;break;case 9:m.flags=KUI_RETAIL_IMAGE_CD;break;
        case 10:m.flags=KUI_RETAIL_IMAGE_SCRAMBLED;break;case 11:m.flags=0x80;break;
        case 12:memset(m.product,'X',sizeof(m.product));break;
        case 13:memset(m.bootfile,'X',sizeof(m.bootfile));break;
        }
        assert(!admit(&m,sha));
    }
    puts("PASS Armada CE audio admission: exact kernel/IP, raw track layout, SCI/background, immutable bounded identity");
    return 0;
}
