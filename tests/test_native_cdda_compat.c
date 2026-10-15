/* SPDX-License-Identifier: GPL-3.0-only */
#include "../src/core/games_cdda_profile.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

/* Invented disc geometry/storage only; no executable, firmware or audio. */
static struct kui_retail_manifest map;
static struct kui_game_image image;
static void extents(unsigned used) {
    assert(used>=10u && used<=KUI_RETAIL_MANIFEST_SLOTS);
    unsigned next=5u;uint32_t physical=100u;
    for(unsigned i=0;i<5u;i++) {
        struct kui_retail_track *t=&map.slots[i].track;
        unsigned pieces=i==0u?used-9u:1u;
        t->first_extent=(uint16_t)next;t->extent_count=(uint8_t)pieces;
        uint32_t blocks=((t->end_lba-t->start_lba)*kui_retail_track_sector_bytes(t)+511u)/512u;
        uint32_t offset=0u;
        for(unsigned j=0;j<pieces;j++) {
            uint32_t take=j+1u<pieces?1u:blocks-offset;
            assert(take);
            map.slots[next++].extent=(struct kui_retail_extent){offset,physical,take};
            physical+=take+3u;offset+=take;
        }
    }
    assert(next==used);map.extent_count=used-5u;
    map.card_sectors=physical+100u;map.partition_start=50u;map.partition_end=physical+50u;
    assert(kui_retail_manifest_validate(&map)==KUI_GAME_OK);
}
static void fixture(enum kui_native_cdda_binding binding,bool cooked) {
    static const uint32_t starts[5]={0u,400u,45000u,47000u,48000u};
    static const uint32_t ends[5]={300u,600u,47000u,48000u,49000u};
    memset(&map,0,sizeof(map));memset(&image,0,sizeof(image));
    map.track_count=image.count=5u;image.format=KUI_GAME_IMAGE_GDI;
    map.storage_transport=KUI_STORAGE_SCI;map.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    map.session_lba=45000u;map.boot_lba=45040u;
    map.boot_bytes=binding==KUI_NATIVE_CDDA_MDK2?750804u:748444u;
    map.boot_crc32=binding==KUI_NATIVE_CDDA_MDK2?UINT32_C(0x682d90e3):UINT32_C(0xcdc493b3);
    map.ip_crc32=UINT32_C(0x12345678);map.gdi_crc32=UINT32_C(0x87654321);
    strcpy(map.title,"Unknown disc using an admitted executable");strcpy(map.bootfile,"1ST_READ.BIN");
    for(unsigned i=0;i<5u;i++) {
        bool data=i%2u==0u;
        struct kui_retail_track *t=&map.slots[i].track;
        *t=(struct kui_retail_track){.start_lba=starts[i],.end_lba=ends[i],
            .control=(uint8_t)(data?(4u|(cooked?KUI_RETAIL_TRACK_COOKED:0u)):0u)};
        image.tracks[i]=(struct kui_game_image_track){.start_lba=starts[i],.end_lba=ends[i],
            .number=i+1u,.control=data?4u:0u,.sector_bytes=data && cooked?2048u:2352u};
    }
    extents(10u);
}
static void refused(void) {
    assert(kui_games_cdda_compatible_profile(&image,&map)==KUI_GAMES_CDDA_UNSUPPORTED);
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void package(const char *path,enum kui_games_cdda_profile profile) {
    FILE *file=fopen(path,"rb");assert(file && !fseek(file,0,SEEK_END));
    long size=ftell(file);assert(size>64 && size<1048576 && !fseek(file,0,SEEK_SET));
    uint8_t *bytes=malloc((size_t)size);assert(bytes);
    assert(fread(bytes,1,(size_t)size,file)==(size_t)size && !fclose(file));
    struct kui_runtime_image loaded={.data=bytes+64u};
    loaded.info.payload_bytes=le32(bytes+16u);loaded.info.memory_bytes=le32(bytes+28u);
    loaded.info.crc32=le32(bytes+32u);memcpy(loaded.info.build,bytes+40u,12u);
    assert(kui_games_cdda_package_admit(profile,&loaded));
    for(unsigned p=KUI_GAMES_CDDA_TOY_RAW_COMPAT;p<=KUI_GAMES_CDDA_MDK2_RAW_COMPAT;p++)
        if(p!=(unsigned)profile) assert(!kui_games_cdda_package_admit((enum kui_games_cdda_profile)p,&loaded));
    bytes[size-1]^=1u;assert(!kui_games_cdda_package_admit(profile,&loaded));bytes[size-1]^=1u;
    loaded.info.memory_bytes+=4u;assert(!kui_games_cdda_package_admit(profile,&loaded));
    loaded.info.memory_bytes-=4u;
    /* Even a freshly checksummed envelope cannot change this adapter's
     * capacity/format contract into another family. */
    unsigned descriptor=0u;
    for(unsigned i=0u;i+128u<=loaded.info.payload_bytes;i++)
        if(!memcmp(bytes+64u+i,"KUINCD01",8u)) {descriptor=i;break;}
    assert(descriptor);
    bytes[64u+descriptor+24u]^=1u;
    loaded.info.crc32=kui_retail_crc32(0u,loaded.data,loaded.info.payload_bytes);
    assert(!kui_games_cdda_package_admit(profile,&loaded));
    free(bytes);
}
int main(int argc,char **argv) {
    assert(argc==1 || argc==4);
    for(unsigned p=KUI_GAMES_CDDA_UNSUPPORTED;p<=KUI_GAMES_CDDA_MDK2_RAW_COMPAT;p++) {
        enum kui_games_cdda_profile exact=(enum kui_games_cdda_profile)p;
        /* Existing exact modules remain selected. Only confirmed absence
         * selects the corresponding typed experimental adapter. */
        assert(kui_games_cdda_package_fallback(exact,false)==exact);
        enum kui_games_cdda_profile fallback=kui_games_cdda_package_fallback(exact,true);
        if(exact==KUI_GAMES_CDDA_TOY_RAW || exact==KUI_GAMES_CDDA_TOY_COOKED ||
           exact==KUI_GAMES_CDDA_MDK2_RAW) {
            assert(fallback==exact+3);
            assert(kui_games_cdda_is_experimental(fallback));
            assert(kui_games_cdda_package(fallback));
            assert(strcmp(kui_games_cdda_package(fallback),kui_games_cdda_package(exact)));
        } else assert(fallback==exact);
    }
    for(unsigned family=KUI_NATIVE_CDDA_TOY;family<=KUI_NATIVE_CDDA_MDK2;family++) {
        for(unsigned cooked=0u;cooked<2u;cooked++) {
            enum kui_native_cdda_binding binding=(enum kui_native_cdda_binding)family;
            fixture(binding,cooked!=0u);
            if(binding==KUI_NATIVE_CDDA_MDK2 && cooked) {refused();continue;}
            enum kui_games_cdda_profile expected=binding==KUI_NATIVE_CDDA_MDK2?
                KUI_GAMES_CDDA_MDK2_RAW_COMPAT:cooked?KUI_GAMES_CDDA_TOY_COOKED_COMPAT:
                KUI_GAMES_CDDA_TOY_RAW_COMPAT;
            assert(kui_games_cdda_profile(&image,&map)==KUI_GAMES_CDDA_UNSUPPORTED);
            assert(kui_games_cdda_boot_profile(&image,&map)==expected);
            assert(kui_games_cdda_compatible_profile(&image,&map)==expected);
            assert(kui_games_cdda_is_experimental(expected));
            assert(kui_games_cdda_package(expected));
            map.gdi_crc32^=1u;map.ip_crc32^=1u;map.boot_lba++;
            assert(kui_games_cdda_compatible_profile(&image,&map)==expected);
            map.boot_crc32^=1u;refused();map.boot_crc32^=1u;
            map.boot_bytes--;refused();map.boot_bytes++;
            map.flags=0u;refused();map.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
            map.reader=KUI_RETAIL_READER_ASYNC;refused();map.reader=0u;
            map.storage_transport=KUI_STORAGE_SCIF;refused();map.storage_transport=KUI_STORAGE_SCI;
            image.scrambled=true;refused();image.scrambled=false;
            image.cd_image=true;refused();image.cd_image=false;
            image.format=KUI_GAME_IMAGE_CUE;refused();image.format=KUI_GAME_IMAGE_GDI;
            image.tracks[3].sector_bytes=2048u;refused();image.tracks[3].sector_bytes=2352u;
            map.slots[3].track.control=KUI_RETAIL_TRACK_MODE2;refused();map.slots[3].track.control=0u;
            map.slots[3].track.start_lba=46000u;refused();map.slots[3].track.start_lba=47000u;
            map.slots[3].track.first_extent|=UINT16_C(0x0100);refused();map.slots[3].track.first_extent&=255u;
            map.slots[3].track.extent_count=0u;refused();
            fixture(binding,cooked!=0u);
            unsigned capacity=kui_native_cdda_capacity(binding);
            extents(capacity);
            assert(kui_games_cdda_compatible_profile(&image,&map)==expected);
            map.extent_count=capacity-4u;refused();
            fixture(binding,cooked!=0u);map.extent_count=0u;
            assert(kui_games_cdda_boot_profile(&image,&map)==expected);refused();
        }
    }
    assert(!kui_native_cdda_boot_binding(748444u,0u));
    assert(!kui_native_cdda_manifest_compatible(NULL,KUI_NATIVE_CDDA_TOY,false));
    assert(!kui_games_cdda_is_experimental(KUI_GAMES_CDDA_TOY_RAW));
    assert(!kui_games_cdda_compatible_profile(NULL,&map));
    assert(!kui_games_cdda_compatible_profile(&image,NULL));
    if(argc==4) {
        package(argv[1],KUI_GAMES_CDDA_TOY_RAW_COMPAT);
        package(argv[2],KUI_GAMES_CDDA_TOY_COOKED_COMPAT);
        package(argv[3],KUI_GAMES_CDDA_MDK2_RAW_COMPAT);
    }
    puts("PASS native experimental CDDA: compatible executable routing, independent disc geometry, complete bounded track maps, unsupported binary/media/driver formats refused");
    return 0;
}
