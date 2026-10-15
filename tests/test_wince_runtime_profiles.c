/* SPDX-License-Identifier: GPL-3.0-only */
/* Synthetic metadata only. No kernel, executable or audio payload fixtures. */
#define KUI_ARMADA_CE_STREAM 1
#include "kui/armada_ce_audio_admission.h"
#include "kui/armada_ce_stream_seed.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_retail_manifest full_map(enum kui_wince_cdda_profile profile,unsigned count) {
    struct kui_retail_manifest map={.card_sectors=1000000u,.partition_start=1u,
        .partition_end=1000000u,.track_count=count,.extent_count=count,.session_lba=45000u,
        .boot_lba=45010u,.boot_bytes=kui_wince_cdda_disc(profile)->kernel_bytes,
        .storage_transport=KUI_STORAGE_SCI,.reader=KUI_RETAIL_READER_STANDARD,
        .flags=KUI_RETAIL_IMAGE_BOOT_CRC,.ip_crc32=0x12345678u};
    strcpy(map.title,"Unfamiliar application");strcpy(map.product,"OTHER-REVISION");
    strcpy(map.bootfile,"RENAMED.BIN");strcpy(map.region,"JUE");
    uint32_t card=1u;
    for(unsigned i=0;i<count;i++) {
        uint32_t start=i<2u?i*100u:i==2u?45000u:46000u+(i-3u)*100u;
        uint32_t sectors=i==2u?1000u:100u;
        uint32_t blocks=(sectors*2352u+511u)/512u;
        map.slots[i].track=(struct kui_retail_track){start,start+sectors,
            (uint16_t)(count+i),1u,(uint8_t)(i==0u || i==2u?4u:0u)};
        map.slots[count+i].extent=(struct kui_retail_extent){0u,card,blocks};card+=blocks;
    }
    return map;
}
static struct kui_retail_manifest data_map(const struct kui_retail_manifest *full) {
    struct kui_retail_manifest map=*full;
    memset(map.slots+map.track_count,0,sizeof(map.slots)-map.track_count*sizeof(*map.slots));
    map.reader=KUI_RETAIL_READER_ASYNC;map.extent_count=0u;
    for(unsigned i=0;i<map.track_count;i++) {
        struct kui_retail_track *track=&map.slots[i].track;
        track->first_extent=(uint16_t)(map.track_count+map.extent_count);
        track->extent_count=0u;
        if(kui_retail_track_control(track)==4u) {
            track->extent_count=1u;
            map.slots[map.track_count+map.extent_count++].extent=
                full->slots[kui_retail_track_first_extent(&full->slots[i].track)].extent;
        }
    }
    return map;
}
int main(void) {
    assert(kui_wince_cdda_kernel_profile(0u,NULL)==KUI_WINCE_CDDA_NONE);
    for(enum kui_wince_cdda_profile profile=KUI_WINCE_CDDA_ARMADA;
            profile<=KUI_WINCE_CDDA_WORMS_WORLD_PARTY;profile++) {
        const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(profile);
        const uint8_t *sha=kui_wince_cdda_kernel_sha(profile);
        assert(kui_wince_cdda_kernel_profile(disc->kernel_bytes,sha)==profile);
        assert(kui_wince_cdda_kernel_profile(disc->kernel_bytes-1u,sha)==KUI_WINCE_CDDA_NONE);
        for(unsigned byte=0;byte<32u;byte++) {
            uint8_t corrupt[32];memcpy(corrupt,sha,32u);corrupt[byte]^=1u;
            assert(kui_wince_cdda_kernel_profile(disc->kernel_bytes,corrupt)==KUI_WINCE_CDDA_NONE);
        }
        struct kui_retail_manifest full=full_map(profile,11u),data=data_map(&full);
        assert(kui_retail_manifest_validate(&full)==KUI_GAME_OK);
        assert(kui_retail_manifest_validate(&data)==KUI_GAME_OK);
        assert(!kui_wince_cdda_manifest_valid(&full,profile)); /* unfamiliar image */
        assert(kui_wince_cdda_runtime_manifest_valid(&full,profile));
        assert(kui_wince_cdda_runtime_data_manifest_valid(&data,profile));
        assert(kui_armada_ce_stream_maps_match(&full,&data));
        struct kui_retail_manifest changed=full;
        changed.slots[4].track.start_lba++; /* valid changed audio interval */
        assert(kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed=full;changed.slots[4].track.extent_count=0u;
        assert(!kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed=full;changed.slots[4].track.first_extent++;
        assert(!kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed=full;changed.slots[4].track.start_lba=changed.slots[3].track.end_lba-1u;
        assert(!kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed=full;changed.slots[4].track.control=KUI_RETAIL_TRACK_COOKED;
        assert(!kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed=full;changed.slots[2].track.control=4u|KUI_RETAIL_TRACK_COOKED;
        assert(kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed.flags=0u;assert(!kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed=full;changed.storage_transport=KUI_STORAGE_SCIF;
        assert(!kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed=full;changed.track_count=100u;
        assert(!kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed=full;changed.extent_count=KUI_RETAIL_MANIFEST_SLOTS;
        assert(!kui_wince_cdda_runtime_manifest_valid(&changed,profile));
        changed=full;for(unsigned i=3u;i<changed.track_count;i++) changed.slots[i].track.control=4u;
        assert(!kui_wince_cdda_runtime_manifest_valid(&changed,profile)); /* warning audio only */
#if KUI_RETAIL_EXTENDED_MAP
        struct kui_retail_manifest large=full_map(profile,99u);
        assert(kui_retail_manifest_validate(&large)==KUI_GAME_OK);
        assert(kui_wince_cdda_runtime_manifest_valid(&large,profile));
        assert(!kui_wince_cdda_runtime_data_manifest_valid(&large,profile));
#endif
        if(profile==KUI_WINCE_CDDA_COMPILED_PROFILE) {
            static const uint32_t prefix[4]={0x6200729cu,0x70513a3cu,0xeffd7677u,0x7e29bc00u};
            static const uint32_t body[4]={0xb9f755bcu,0xf6902800u,0xcd686951u,0x12f68791u};
            unsigned index=(unsigned)(profile-KUI_WINCE_CDDA_ARMADA);
            assert(kui_armada_ce_audio_admit(&data,data.ip_crc32,prefix[index],body[index],sha));
            assert(!kui_armada_ce_audio_admit(&data,data.ip_crc32^1u,prefix[index],body[index],sha));
            assert(!kui_armada_ce_audio_admit(&data,data.ip_crc32,prefix[index]^1u,body[index],sha));
            assert(!kui_armada_ce_audio_admit(&data,data.ip_crc32,prefix[index],body[index]^1u,sha));
            assert(kui_armada_ce_stream_map_valid(&full));
        }
    }
    puts("PASS WinCE runtime families: unknown applications, dynamic tracks, exact kernel SHA, source/DATA ownership and capacity bounds");
    return 0;
}
