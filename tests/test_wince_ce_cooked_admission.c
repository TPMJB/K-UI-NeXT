/* SPDX-License-Identifier: GPL-3.0-only */
/* Generated metadata only; the private kernel bytes are never included. */
#include "kui/armada_ce_audio_admission.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#if KUI_WINCE_CDDA_WORMS
static const uint8_t sha[32]={0x39,0xb8,0x0e,0x30,0x59,0x47,0x05,0x65,0x51,0x31,0x8b,0xb6,0x55,0x29,0xcc,0xcc,
    0x88,0x60,0x21,0xbe,0x60,0x40,0x6c,0x06,0xf3,0x11,0x0f,0x7f,0xe0,0x3b,0xf5,0x71};
static const uint32_t crcs[3]={0x86bbb334,0xeffd7677,0xcd686951};
#elif KUI_WINCE_CDDA_BAM4
static const uint8_t sha[32]={0x2a,0x8d,0x24,0x73,0x8b,0xb5,0xba,0x06,0x87,0xfa,0x56,0xad,0x33,0xe8,0x14,0xfe,
    0x79,0x81,0x58,0xf9,0xcb,0x8c,0xac,0xfd,0x4a,0x8a,0x20,0xde,0xa6,0xbb,0xb3,0x5d};
static const uint32_t crcs[3]={0x8fb049c5,0x70513a3c,0xf6902800};
#else
static const uint8_t sha[32]={0xa7,0x62,0x8c,0x78,0x09,0x0f,0xce,0x2c,0x93,0x01,0x2d,0x5e,0x4d,0xb6,0x14,0x20,
    0x96,0xe1,0x38,0xdc,0xeb,0x7e,0xbb,0xf5,0x55,0x14,0x2e,0x90,0x2c,0xa0,0x3c,0x1f};
static const uint32_t crcs[3]={0x684f8040,0x6200729c,0xb9f755bc};
#endif
static bool admit(const struct kui_retail_manifest *map) {
    return kui_armada_ce_audio_admit(map,crcs[0],crcs[1],crcs[2],sha);
}
int main(void) {
    const enum kui_wince_cdda_profile profile=KUI_WINCE_CDDA_COMPILED_PROFILE;
    const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(profile);
    struct kui_retail_manifest raw={.track_count=disc->track_count,.session_lba=45000u,
        .boot_lba=disc->boot_lba,.boot_bytes=disc->kernel_bytes,.storage_transport=KUI_STORAGE_SCI,
        .reader=KUI_RETAIL_READER_ASYNC};
    strcpy(raw.product,disc->product);strcpy(raw.bootfile,"0WINCEOS.BIN");
    for(unsigned i=0;i<disc->track_count;i++) {
        raw.slots[i].track.start_lba=disc->starts[i];
        raw.slots[i].track.control=(uint8_t)kui_wince_cdda_track_control(profile,i);
    }
    assert(admit(&raw));
    struct kui_retail_manifest cooked=raw;cooked.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    for(unsigned i=0;i<disc->track_count;i++)
        if(cooked.slots[i].track.control==4u) cooked.slots[i].track.control|=KUI_RETAIL_TRACK_COOKED;
    assert(admit(&cooked));
    struct kui_retail_manifest changed=cooked;changed.flags=0u;assert(!admit(&changed));
    for(unsigned i=0;i<disc->track_count;i++) {
        changed=cooked;changed.slots[i].track.start_lba++;assert(!admit(&changed));
        changed=cooked;changed.slots[i].track.first_extent=0x100u;assert(!admit(&changed));
        changed=cooked;changed.slots[i].track.control^=4u;assert(!admit(&changed));
        if(!raw.slots[i].track.control) {
            changed=cooked;changed.slots[i].track.control|=KUI_RETAIL_TRACK_COOKED;assert(!admit(&changed));
        }
        const unsigned forbidden[]={1u,2u,KUI_RETAIL_TRACK_MODE2,KUI_RETAIL_TRACK_2336,KUI_RETAIL_TRACK_2448};
        for(unsigned j=0;j<sizeof(forbidden)/sizeof(*forbidden);j++) {
            changed=cooked;changed.slots[i].track.control|=forbidden[j];assert(!admit(&changed));
        }
    }
    changed=cooked;changed.product[0]^=1u;assert(!admit(&changed));
    changed=cooked;changed.boot_lba++;assert(!admit(&changed));
    changed=cooked;changed.boot_bytes--;assert(!admit(&changed));
    for(unsigned i=0;i<32u;i++) {
        uint8_t changed_sha[32];memcpy(changed_sha,sha,sizeof(sha));changed_sha[i]^=1u;
        assert(!kui_armada_ce_audio_admit(&cooked,crcs[0],crcs[1],crcs[2],changed_sha));
    }
    for(unsigned i=0;i<3u;i++) {
        uint32_t changed_crc[3];memcpy(changed_crc,crcs,sizeof(crcs));changed_crc[i]^=1u;
        assert(!kui_armada_ce_audio_admit(&cooked,changed_crc[0],changed_crc[1],changed_crc[2],sha));
    }
    printf("PASS %s cooked DATA admission: raw AUDIO, required boot CRC, exact identities/LBAs, flags and offset refusals\n",disc->name);
    return 0;
}
