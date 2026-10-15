/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_WINCE_CDDA_PROFILES_H
#define KUI_WINCE_CDDA_PROFILES_H
#include "kui/retail_image.h"
#include <stddef.h>

/* Public disc metadata only. Each separately built module additionally
 * admits its complete kernel SHA and native sound implementation at load. */
enum kui_wince_cdda_profile {
    KUI_WINCE_CDDA_NONE, KUI_WINCE_CDDA_ARMADA, KUI_WINCE_CDDA_BUST_A_MOVE_4
};
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
    static const struct kui_wince_cdda_disc discs[]={
        {"Armada","T40301N 00",1253376u,548388u,5u,armada},
        {"Bust-A-Move 4","T-8117N",1830912u,548106u,26u,bam4}};
    return profile>=KUI_WINCE_CDDA_ARMADA && profile<=KUI_WINCE_CDDA_BUST_A_MOVE_4?
        &discs[profile-KUI_WINCE_CDDA_ARMADA]:NULL;
}
static inline uint32_t kui_wince_cdda_track_control(
        enum kui_wince_cdda_profile profile,uint32_t index) {
    if(profile==KUI_WINCE_CDDA_ARMADA) return index==1u || index==3u?0u:4u;
    return index==0u || index==2u || index==25u?4u:0u;
}
static inline bool kui_wince_cdda_text_is(const char *value,size_t bytes,const char *expected) {
    for(size_t i=0;i<bytes;i++) {
        if(value[i]!=expected[i]) return false;
        if(!expected[i]) return true;
    }
    return false;
}
static inline bool kui_wince_cdda_manifest_valid(const struct kui_retail_manifest *map,
        enum kui_wince_cdda_profile profile) {
    const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(profile);
    if(!map || !disc || map->track_count!=disc->track_count ||
       map->storage_transport!=KUI_STORAGE_SCI || map->session_lba!=45000u ||
       map->reader!=KUI_RETAIL_READER_STANDARD || map->boot_bytes!=disc->kernel_bytes ||
       (profile==KUI_WINCE_CDDA_BUST_A_MOVE_4 && map->boot_lba!=disc->boot_lba) ||
       !kui_wince_cdda_text_is(map->product,sizeof(map->product),disc->product) ||
       !kui_wince_cdda_text_is(map->bootfile,sizeof(map->bootfile),"0WINCEOS.BIN") ||
       map->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED) ||
       map->track_count+map->extent_count>KUI_RETAIL_MANIFEST_SLOTS) return false;
    for(unsigned i=0;i<disc->track_count;i++) {
        const struct kui_retail_track *track=&map->slots[i].track;
        if(track->start_lba!=disc->starts[i] || track->end_lba<=track->start_lba ||
           track->control!=kui_wince_cdda_track_control(profile,i) ||
           (profile==KUI_WINCE_CDDA_BUST_A_MOVE_4 &&
                kui_retail_track_sector_bytes(track)!=KUI_GAME_RAW_BYTES) ||
           kui_retail_track_file_offset(track) || !track->extent_count) return false;
    }
    return true;
}
#if KUI_WINCE_CDDA_BAM4
#define KUI_WINCE_CDDA_COMPILED_PROFILE KUI_WINCE_CDDA_BUST_A_MOVE_4
#define KUI_WINCE_CDDA_TRACK_COUNT 26u
#else
#define KUI_WINCE_CDDA_COMPILED_PROFILE KUI_WINCE_CDDA_ARMADA
#define KUI_WINCE_CDDA_TRACK_COUNT 5u
#endif
#endif
