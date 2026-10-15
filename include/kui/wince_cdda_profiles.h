/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_WINCE_CDDA_PROFILES_H
#define KUI_WINCE_CDDA_PROFILES_H
#include "kui/retail_image.h"
#include <stddef.h>
#include <string.h>

/* Public disc metadata only. Each separately built module additionally
 * admits its complete kernel SHA and native sound implementation at load. */
enum kui_wince_cdda_profile {
    KUI_WINCE_CDDA_NONE, KUI_WINCE_CDDA_ARMADA, KUI_WINCE_CDDA_BUST_A_MOVE_4,
    KUI_WINCE_CDDA_WORMS_ARMAGEDDON, KUI_WINCE_CDDA_WORMS_WORLD_PARTY
};
/* These select an implementation contract, not a game title. A different
 * application may reuse the same unmodified kernel and native sound ABI.
 * Unknown kernels cannot inherit addresses or memory reservations by size,
 * product text, track layout or a partial instruction match alone. */
static inline const uint8_t *kui_wince_cdda_kernel_sha(enum kui_wince_cdda_profile profile) {
    static const uint8_t hashes[4][32]={
        {0xa7,0x62,0x8c,0x78,0x09,0x0f,0xce,0x2c,0x93,0x01,0x2d,0x5e,0x4d,0xb6,0x14,0x20,
         0x96,0xe1,0x38,0xdc,0xeb,0x7e,0xbb,0xf5,0x55,0x14,0x2e,0x90,0x2c,0xa0,0x3c,0x1f},
        {0x2a,0x8d,0x24,0x73,0x8b,0xb5,0xba,0x06,0x87,0xfa,0x56,0xad,0x33,0xe8,0x14,0xfe,
         0x79,0x81,0x58,0xf9,0xcb,0x8c,0xac,0xfd,0x4a,0x8a,0x20,0xde,0xa6,0xbb,0xb3,0x5d},
        {0x39,0xb8,0x0e,0x30,0x59,0x47,0x05,0x65,0x51,0x31,0x8b,0xb6,0x55,0x29,0xcc,0xcc,
         0x88,0x60,0x21,0xbe,0x60,0x40,0x6c,0x06,0xf3,0x11,0x0f,0x7f,0xe0,0x3b,0xf5,0x71},
        {0x18,0x97,0x19,0xd8,0xd3,0x76,0x32,0x84,0xf7,0x4c,0x5e,0x66,0x70,0x47,0x10,0xb9,
         0xf9,0x96,0x81,0x18,0xcb,0x58,0xae,0xb0,0xa0,0x84,0x4b,0xd1,0x21,0xa6,0x29,0xab}};
    return profile>=KUI_WINCE_CDDA_ARMADA && profile<=KUI_WINCE_CDDA_WORMS_WORLD_PARTY?
        hashes[profile-KUI_WINCE_CDDA_ARMADA]:NULL;
}
struct kui_wince_cdda_disc {
    const char *name,*product;
    uint32_t kernel_bytes,boot_lba,track_count;
    const uint32_t *starts;
};
static inline const struct kui_wince_cdda_disc *kui_wince_cdda_disc(
        enum kui_wince_cdda_profile profile) {
    static const uint32_t armada[]={0u,756u,45000u,149842u,434062u};
    static const uint32_t bam4[]={0u,450u,45000u,370673u,371347u,372014u,
        372915u,373626u,379011u,384738u,390481u,395473u,398926u,404448u,
        425246u,445520u,466032u,474231u,485598u,486386u,487098u,487822u,
        498356u,508297u,527383u,548106u};
    static const uint32_t worms[]={0u,450u,45000u,91528u,117095u,232929u,329558u,379575u};
    static const uint32_t wwp[]={0u,300u,45000u,89954u,99685u,118988u,141520u,
        160930u,180346u,199619u,219848u,244069u,263559u,283684u,303393u,
        322564u,347640u,372686u};
    static const struct kui_wince_cdda_disc discs[]={
        {"Armada","T40301N 00",1253376u,548388u,5u,armada},
        {"Bust-A-Move 4","T-8117N",1830912u,548106u,26u,bam4},
        {"Worms Armageddon","T40601N",1110016u,548458u,8u,worms},
        {"Worms World Party","T22904N",1828864u,548107u,18u,wwp}};
    return profile>=KUI_WINCE_CDDA_ARMADA && profile<=KUI_WINCE_CDDA_WORMS_WORLD_PARTY?
        &discs[profile-KUI_WINCE_CDDA_ARMADA]:NULL;
}
static inline enum kui_wince_cdda_profile kui_wince_cdda_kernel_profile(
        uint32_t bytes,const uint8_t sha[32]) {
    if(!sha) return KUI_WINCE_CDDA_NONE;
    for(enum kui_wince_cdda_profile profile=KUI_WINCE_CDDA_ARMADA;
            profile<=KUI_WINCE_CDDA_WORMS_WORLD_PARTY;profile++) {
        const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(profile);
        if(bytes==disc->kernel_bytes && !memcmp(sha,kui_wince_cdda_kernel_sha(profile),32u))
            return profile;
    }
    return KUI_WINCE_CDDA_NONE;
}
static inline uint32_t kui_wince_cdda_track_control(
        enum kui_wince_cdda_profile profile,uint32_t index) {
    if(profile==KUI_WINCE_CDDA_ARMADA) return index==1u || index==3u?0u:4u;
    if(profile==KUI_WINCE_CDDA_WORMS_ARMAGEDDON) return index==0u || index==2u || index==7u?4u:0u;
    if(profile==KUI_WINCE_CDDA_WORMS_WORLD_PARTY) return index==0u || index==2u || index==17u?4u:0u;
    return index==0u || index==2u || index==25u?4u:0u;
}
static inline bool kui_wince_cdda_text_is(const char *value,size_t bytes,const char *expected) {
    for(size_t i=0;i<bytes;i++) {
        if(value[i]!=expected[i]) return false;
        if(!expected[i]) return true;
    }
    return false;
}
/* Conversion removes only Mode 1 DATA framing. Audio stays raw2352 and
 * cooked executable data is admitted only with the detached byte CRC. Do not
 * mask unknown storage flags into a seemingly valid DATA/audio control. */
static inline bool kui_wince_cdda_track_storage_valid(const struct kui_retail_track *track,
        uint32_t control,uint32_t flags) {
    return track && (track->control==control ||
        (control==4u && track->control==(4u|KUI_RETAIL_TRACK_COOKED) &&
         (flags&KUI_RETAIL_IMAGE_BOOT_CRC)));
}
static inline bool kui_wince_cdda_manifest_valid(const struct kui_retail_manifest *map,
        enum kui_wince_cdda_profile profile) {
    const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(profile);
    if(!map || !disc || map->track_count!=disc->track_count ||
       map->storage_transport!=KUI_STORAGE_SCI || map->session_lba!=45000u ||
       map->reader!=KUI_RETAIL_READER_STANDARD || map->boot_bytes!=disc->kernel_bytes ||
       (profile!=KUI_WINCE_CDDA_ARMADA && map->boot_lba!=disc->boot_lba) ||
       !kui_wince_cdda_text_is(map->product,sizeof(map->product),disc->product) ||
       !kui_wince_cdda_text_is(map->bootfile,sizeof(map->bootfile),"0WINCEOS.BIN") ||
       map->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED) ||
       map->track_count+map->extent_count>KUI_RETAIL_MANIFEST_SLOTS) return false;
    for(unsigned i=0;i<disc->track_count;i++) {
        const struct kui_retail_track *track=&map->slots[i].track;
        if(track->start_lba!=disc->starts[i] || track->end_lba<=track->start_lba ||
           !kui_wince_cdda_track_storage_valid(track,kui_wince_cdda_track_control(profile,i),map->flags) ||
           kui_retail_track_file_offset(track) || !track->extent_count) return false;
    }
    return true;
}
/* Additional policy after canonical full-manifest validation. Dynamic track
 * intervals belong to the selected disc; native addresses still come only
 * from the independently SHA-admitted kernel adapter. Every audio file must
 * be mapped, and only high-density audio establishes this playback route.
 * The low DATA reader has a separate, smaller capacity checked by its own
 * manifest preparation. This full source map may use the extended wire map. */
static inline bool kui_wince_cdda_runtime_tracks_valid(const struct kui_retail_manifest *map,
        enum kui_wince_cdda_profile profile,uint32_t reader,bool mapped_audio) {
    const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(profile);
    if(!map || !disc || !map->track_count || map->track_count>KUI_RETAIL_IMAGE_TRACKS ||
       map->track_count>KUI_RETAIL_MANIFEST_SLOTS ||
       !map->extent_count || map->extent_count>KUI_RETAIL_MANIFEST_SLOTS-map->track_count ||
       map->storage_transport!=KUI_STORAGE_SCI || map->session_lba<45000u ||
       map->reader!=reader || map->boot_bytes!=disc->kernel_bytes ||
       map->boot_lba<map->session_lba || map->flags&~KUI_RETAIL_IMAGE_BOOT_CRC)
        return false;
    bool session=false,audio=false;
    uint32_t next=map->track_count;
    for(unsigned i=0;i<map->track_count;i++) {
        const struct kui_retail_track *track=&map->slots[i].track;
        uint32_t control=kui_retail_track_control(track);
        if(track->end_lba<=track->start_lba || track->end_lba>KUI_GAME_LBA_LIMIT ||
           (i && track->start_lba<map->slots[i-1u].track.end_lba) ||
           !kui_wince_cdda_track_storage_valid(track,control,map->flags) ||
           kui_retail_track_file_offset(track) ||
           (!track->extent_count && (control==4u || mapped_audio)) ||
           kui_retail_track_first_extent(track)!=next ||
           track->extent_count>map->track_count+map->extent_count-next) return false;
        next+=track->extent_count;
        if(control==4u && track->start_lba==map->session_lba) session=true;
        if(control==0u && track->start_lba>=map->session_lba) audio=true;
    }
    return session && audio && next==map->track_count+map->extent_count;
}
static inline bool kui_wince_cdda_runtime_manifest_valid(const struct kui_retail_manifest *map,
        enum kui_wince_cdda_profile profile) {
    return kui_wince_cdda_runtime_tracks_valid(map,profile,KUI_RETAIL_READER_STANDARD,true);
}
static inline bool kui_wince_cdda_runtime_data_manifest_valid(const struct kui_retail_manifest *map,
        enum kui_wince_cdda_profile profile) {
    return map && map->track_count<=KUI_RETAIL_ASYNC_SLOTS &&
        map->extent_count<=KUI_RETAIL_ASYNC_SLOTS-map->track_count &&
        kui_wince_cdda_runtime_tracks_valid(map,profile,KUI_RETAIL_READER_ASYNC,false);
}
#if (KUI_WINCE_CDDA_WWP + KUI_WINCE_CDDA_WORMS + KUI_WINCE_CDDA_BAM4) > 1
#error Select only one Windows CE CDDA implementation profile
#endif
#if KUI_WINCE_CDDA_WWP
#define KUI_WINCE_CDDA_COMPILED_PROFILE KUI_WINCE_CDDA_WORMS_WORLD_PARTY
#define KUI_WINCE_CDDA_TRACK_COUNT 18u
#elif KUI_WINCE_CDDA_WORMS
#define KUI_WINCE_CDDA_COMPILED_PROFILE KUI_WINCE_CDDA_WORMS_ARMAGEDDON
#define KUI_WINCE_CDDA_TRACK_COUNT 8u
#elif KUI_WINCE_CDDA_BAM4
#define KUI_WINCE_CDDA_COMPILED_PROFILE KUI_WINCE_CDDA_BUST_A_MOVE_4
#define KUI_WINCE_CDDA_TRACK_COUNT 26u
#else
#define KUI_WINCE_CDDA_COMPILED_PROFILE KUI_WINCE_CDDA_ARMADA
#define KUI_WINCE_CDDA_TRACK_COUNT 5u
#endif
#endif
