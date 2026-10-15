/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_CDDA_PROFILE_H
#define KUI_GAMES_CDDA_PROFILE_H
#include "kui/game_image.h"
#include "kui/games_retail.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include "kui/mdk2_cdda_layout.h"
#include "kui/native_cdda_compat.h"
#include <string.h>

enum kui_games_cdda_profile {
    KUI_GAMES_CDDA_UNSUPPORTED, KUI_GAMES_CDDA_TOY_RAW, KUI_GAMES_CDDA_TOY_COOKED,
    KUI_GAMES_CDDA_MDK2_RAW,
    KUI_GAMES_CDDA_TOY_RAW_COMPAT, KUI_GAMES_CDDA_TOY_COOKED_COMPAT,
    KUI_GAMES_CDDA_MDK2_RAW_COMPAT
};
/* Routing is deliberately narrower than the binaries' final loaded-byte
 * checks. Every track, including the low-density separator, must be mapped;
 * the separator alone never establishes playable high-density CD audio.
 * Titles, product strings and filenames are not admission evidence. */
static inline enum kui_games_cdda_profile kui_games_cdda_toy_profile(
    const struct kui_game_image *image,const struct kui_retail_manifest *m) {
    static const uint32_t starts[15]={0u,7257u,45000u,201900u,219712u,234091u,
        255599u,272017u,282799u,303092u,323352u,341314u,356690u,374201u,377422u};
    static const uint32_t ends[15]={7107u,7753u,201750u,219712u,234091u,
        255599u,272017u,282799u,303092u,323352u,341314u,356690u,374201u,377272u,549150u};
    if(!image || !m || image->format!=KUI_GAME_IMAGE_GDI || image->cd_image ||
       image->scrambled || image->count!=15u ||
       kui_retail_manifest_validate(m)!=KUI_GAME_OK || m->track_count!=15u ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_STANDARD ||
       m->flags!=KUI_RETAIL_IMAGE_BOOT_CRC || m->session_lba!=45000u ||
       m->boot_lba!=548634u || m->boot_bytes!=748444u ||
       m->ip_crc32!=UINT32_C(0x38ba2868) || m->boot_crc32!=UINT32_C(0xcdc493b3) ||
       !m->extent_count || m->extent_count>64u-15u) return KUI_GAMES_CDDA_UNSUPPORTED;
    const bool cooked=m->gdi_crc32==UINT32_C(0xb7100f9e);
    if(!cooked && m->gdi_crc32!=UINT32_C(0xe25531d1)) return KUI_GAMES_CDDA_UNSUPPORTED;
    for(unsigned i=0;i<15u;i++) {
        const struct kui_retail_track *t=&m->slots[i].track;
        const bool data=i==0u || i==2u || i==14u;
        const uint32_t control=data?(4u|(cooked?KUI_RETAIL_TRACK_COOKED:0u)):0u;
        if(t->start_lba!=starts[i] || t->end_lba!=ends[i] || t->control!=control ||
           kui_retail_track_sector_bytes(t)!=(data && cooked?2048u:2352u) ||
           kui_retail_track_file_offset(t) || !t->extent_count)
            return KUI_GAMES_CDDA_UNSUPPORTED;
    }
    return cooked?KUI_GAMES_CDDA_TOY_COOKED:KUI_GAMES_CDDA_TOY_RAW;
}
/* Early capacity selection uses checked image identities without requiring
 * a map that has not yet been built. It grants storage capacity, not launch
 * admission: the complete track map and payload guards are checked later. */
static inline bool kui_games_cdda_mdk2_identity(
    const struct kui_game_image *image,const struct kui_retail_manifest *m) {
    return image && m && image->format==KUI_GAME_IMAGE_GDI && !image->cd_image &&
        !image->scrambled && image->count==31u && m->track_count==31u &&
        m->storage_transport==KUI_STORAGE_SCI && m->reader==KUI_RETAIL_READER_STANDARD &&
        m->flags==KUI_RETAIL_IMAGE_BOOT_CRC && m->session_lba==45000u &&
        m->boot_lba==548633u && m->boot_bytes==750804u &&
        m->ip_crc32==UINT32_C(0x34122049) && m->boot_crc32==UINT32_C(0x682d90e3) &&
        m->gdi_crc32==UINT32_C(0x1714d99a);
}
static inline unsigned kui_games_cdda_mapping_capacity(
    const struct kui_game_image *image,const struct kui_retail_manifest *m) {
    return image && m && image->format==KUI_GAME_IMAGE_GDI && !image->cd_image &&
        !image->scrambled && m->flags==KUI_RETAIL_IMAGE_BOOT_CRC &&
        kui_native_cdda_boot_binding(m->boot_bytes,m->boot_crc32)==KUI_NATIVE_CDDA_MDK2?
        KUI_MDK2_CDDA_MANIFEST_SLOTS:64u;
}
/* First MDK2 integration: the owner's exact 31-track raw GDI, all tracks
 * mapped, native SCI and the standard reader. Cooked/2048, CE, other media
 * and reader routes remain unsupported. Boot payload identity is checked
 * again by the selected stage; no title/product string grants admission. */
static inline enum kui_games_cdda_profile kui_games_cdda_mdk2_profile(
    const struct kui_game_image *image,const struct kui_retail_manifest *m) {
    static const uint32_t starts[31]={0u,14601u,45000u,52290u,74215u,94719u,
        115029u,134578u,154620u,174965u,193871u,213463u,235118u,253694u,
        278376u,298017u,314109u,330973u,349474u,368207u,373047u,377949u,
        382628u,387412u,396388u,400888u,402548u,416575u,420052u,424954u,429498u};
    static const uint32_t ends[31]={14451u,15127u,52140u,74215u,94719u,
        115029u,134578u,154620u,174965u,193871u,213463u,235118u,253694u,
        278376u,298017u,314109u,330973u,349474u,368207u,373047u,377949u,
        382628u,387412u,396388u,400888u,402548u,416575u,420052u,424954u,429348u,549150u};
    if(!kui_games_cdda_mdk2_identity(image,m) ||
       kui_retail_manifest_validate(m)!=KUI_GAME_OK ||
       !m->extent_count || m->extent_count>KUI_MDK2_CDDA_MANIFEST_SLOTS-31u)
        return KUI_GAMES_CDDA_UNSUPPORTED;
    for(unsigned i=0;i<31u;i++) {
        const struct kui_retail_track *t=&m->slots[i].track;
        const uint32_t control=(i==0u || i==2u || i==30u)?4u:0u;
        if(t->start_lba!=starts[i] || t->end_lba!=ends[i] || t->control!=control ||
           kui_retail_track_sector_bytes(t)!=2352u || kui_retail_track_file_offset(t) ||
           !t->extent_count) return KUI_GAMES_CDDA_UNSUPPORTED;
    }
    return KUI_GAMES_CDDA_MDK2_RAW;
}
static inline enum kui_games_cdda_profile kui_games_cdda_profile(
    const struct kui_game_image *image,const struct kui_retail_manifest *m) {
    enum kui_games_cdda_profile profile=kui_games_cdda_mdk2_profile(image,m);
    return profile!=KUI_GAMES_CDDA_UNSUPPORTED?profile:kui_games_cdda_toy_profile(image,m);
}
/* Select a family before building the complete map. Source CRC/size select
 * only a candidate; the later map check and loaded SHA remain mandatory. */
static inline enum kui_games_cdda_profile kui_games_cdda_boot_profile(
    const struct kui_game_image *image,const struct kui_retail_manifest *m) {
    if(!image || !m || image->format!=KUI_GAME_IMAGE_GDI || image->cd_image ||
       image->scrambled || !image->count || image->count>KUI_RETAIL_IMAGE_TRACKS ||
       image->count!=m->track_count || m->storage_transport!=KUI_STORAGE_SCI ||
       m->reader!=KUI_RETAIL_READER_STANDARD || m->flags!=KUI_RETAIL_IMAGE_BOOT_CRC)
        return KUI_GAMES_CDDA_UNSUPPORTED;
    enum kui_native_cdda_binding binding=kui_native_cdda_boot_binding(m->boot_bytes,m->boot_crc32);
    if(binding==KUI_NATIVE_CDDA_NONE) return KUI_GAMES_CDDA_UNSUPPORTED;
    bool raw=false,cooked=false,audio=false;
    for(unsigned i=0;i<image->count;i++) {
        const struct kui_game_image_track *t=&image->tracks[i];
        if(t->file_offset || (t->control!=0u && t->control!=4u))
            return KUI_GAMES_CDDA_UNSUPPORTED;
        if(t->control==4u) {
            if(t->sector_bytes==2048u) cooked=true;
            else if(t->sector_bytes==2352u) raw=true;
            else return KUI_GAMES_CDDA_UNSUPPORTED;
        } else {
            if(t->sector_bytes!=2352u) return KUI_GAMES_CDDA_UNSUPPORTED;
            if(t->start_lba>=45000u) audio=true;
        }
    }
    if(!audio || raw==cooked || (cooked && binding==KUI_NATIVE_CDDA_MDK2))
        return KUI_GAMES_CDDA_UNSUPPORTED;
    return binding==KUI_NATIVE_CDDA_MDK2?KUI_GAMES_CDDA_MDK2_RAW_COMPAT:
        cooked?KUI_GAMES_CDDA_TOY_COOKED_COMPAT:KUI_GAMES_CDDA_TOY_RAW_COMPAT;
}
static inline bool kui_games_cdda_is_experimental(enum kui_games_cdda_profile profile) {
    return profile==KUI_GAMES_CDDA_TOY_RAW_COMPAT ||
        profile==KUI_GAMES_CDDA_TOY_COOKED_COMPAT || profile==KUI_GAMES_CDDA_MDK2_RAW_COMPAT;
}
/* Only an absent separately delivered exact module permits this fallback.
 * A present exact module retains its strict byte identity and admission. */
static inline enum kui_games_cdda_profile kui_games_cdda_package_fallback(
    enum kui_games_cdda_profile profile,bool absent) {
    if(!absent) return profile;
    return profile==KUI_GAMES_CDDA_TOY_RAW?KUI_GAMES_CDDA_TOY_RAW_COMPAT:
        profile==KUI_GAMES_CDDA_TOY_COOKED?KUI_GAMES_CDDA_TOY_COOKED_COMPAT:
        profile==KUI_GAMES_CDDA_MDK2_RAW?KUI_GAMES_CDDA_MDK2_RAW_COMPAT:profile;
}
static inline enum kui_games_cdda_profile kui_games_cdda_compatible_profile(
    const struct kui_game_image *image,const struct kui_retail_manifest *m) {
    enum kui_games_cdda_profile exact=kui_games_cdda_profile(image,m);
    if(exact!=KUI_GAMES_CDDA_UNSUPPORTED) return exact;
    enum kui_games_cdda_profile candidate=kui_games_cdda_boot_profile(image,m);
    const bool cooked=candidate==KUI_GAMES_CDDA_TOY_COOKED_COMPAT;
    enum kui_native_cdda_binding binding=candidate==KUI_GAMES_CDDA_MDK2_RAW_COMPAT?
        KUI_NATIVE_CDDA_MDK2:candidate==KUI_GAMES_CDDA_UNSUPPORTED?
        KUI_NATIVE_CDDA_NONE:KUI_NATIVE_CDDA_TOY;
    return kui_native_cdda_manifest_compatible(m,binding,cooked)?candidate:KUI_GAMES_CDDA_UNSUPPORTED;
}
static inline const char *kui_games_cdda_package(enum kui_games_cdda_profile profile) {
    return profile==KUI_GAMES_CDDA_TOY_RAW?KUI_GAMES_RETAIL_CDDA_RAW_PACKAGE:
        profile==KUI_GAMES_CDDA_TOY_COOKED?KUI_GAMES_RETAIL_CDDA_COOKED_PACKAGE:
        profile==KUI_GAMES_CDDA_MDK2_RAW?KUI_GAMES_RETAIL_CDDA_MDK2_PACKAGE:
        profile==KUI_GAMES_CDDA_TOY_RAW_COMPAT?"0:/KUI/apps/games/cdda-native-toy-raw.kui":
        profile==KUI_GAMES_CDDA_TOY_COOKED_COMPAT?"0:/KUI/apps/games/cdda-native-toy-cooked.kui":
        profile==KUI_GAMES_CDDA_MDK2_RAW_COMPAT?"0:/KUI/apps/games/cdda-native-mdk2.kui":NULL;
}
/* Bind the runtime route to the checked MDK2 adapter allocation. The
 * unmodified executable and external driver still undergo their own loaded
 * byte guards before any game/sound patch is installed. */
static inline bool kui_games_cdda_mdk2_package_admit(const struct kui_runtime_image *image) {
    static const uint8_t sha[32]={
        0x0c,0x7d,0x9d,0x58,0x06,0x83,0xc5,0x80,0x35,0x35,0xd7,0x5d,0x75,0x24,0x7b,0x17,
        0xaa,0x42,0xbc,0x0d,0x4a,0x09,0x70,0x12,0xdf,0x68,0xb7,0x8c,0x5a,0xd4,0xe2,0x94};
    if(!image || !image->data || image->info.payload_bytes!=64524u ||
       image->info.memory_bytes!=64524u || image->info.crc32!=UINT32_C(0x976b0fb3) ||
       memcmp(image->info.build,"89c4650e444f",13u)) return false;
    struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);
    kui_sha256_update(&hash,image->data,image->info.payload_bytes);
    kui_sha256_digest(&hash,digest);
    return !memcmp(digest,sha,sizeof(digest));
}
/* Runtime parsing checks the envelope and its payload CRC first. Bind the
 * actual allocation to the delivered binary as well: build text alone is
 * insufficient. The map is still empty when this SHA is checked. */
static inline bool kui_games_cdda_package_admit(enum kui_games_cdda_profile profile,
    const struct kui_runtime_image *image) {
    if(kui_games_cdda_is_experimental(profile)) {
        static const struct kui_native_cdda_descriptor descriptors[3]={
            KUI_NATIVE_CDDA_TOY_DESCRIPTOR(0u),KUI_NATIVE_CDDA_TOY_DESCRIPTOR(1u),
            KUI_NATIVE_CDDA_MDK2_DESCRIPTOR};
        unsigned index=profile==KUI_GAMES_CDDA_TOY_RAW_COMPAT?0u:
            profile==KUI_GAMES_CDDA_TOY_COOKED_COMPAT?1u:2u;
        const uint8_t *identity=(const uint8_t *)&descriptors[index];
        if(!image || !image->data || image->info.payload_bytes<sizeof(descriptors[0]) ||
           image->info.payload_bytes>1024u*1024u ||
           image->info.memory_bytes!=image->info.payload_bytes ||
           kui_retail_crc32(0u,image->data,image->info.payload_bytes)!=image->info.crc32)
            return false;
        const uint8_t *payload=image->data;
        unsigned found=0u;
        for(uint32_t i=0;i<=image->info.payload_bytes-sizeof(descriptors[0]);i++) {
            if(memcmp(payload+i,identity,8u)) continue;
            if(memcmp(payload+i,identity,sizeof(descriptors[0])) || ++found>1u) return false;
        }
        return found==1u;
    }
    if(profile==KUI_GAMES_CDDA_MDK2_RAW) return kui_games_cdda_mdk2_package_admit(image);
    static const uint8_t raw_sha[32]={
        0x78,0xc1,0x5a,0x85,0x80,0x32,0x0d,0xc2,0xb5,0x30,0x1d,0x85,0x76,0xcd,0x48,0x77,
        0xdf,0x7a,0xe9,0xd8,0x4c,0xc5,0x51,0x86,0x71,0xc9,0xa9,0x58,0xaf,0x7e,0xd5,0x41};
    static const uint8_t cooked_sha[32]={
        0xcd,0xe2,0xa0,0x88,0xe6,0x32,0x92,0xc1,0x64,0xbd,0x62,0x78,0x0a,0x64,0x4e,0x83,
        0x78,0x7f,0x3e,0x1d,0x46,0x6b,0x29,0x20,0xad,0x68,0xa8,0xdd,0x6b,0x9b,0xcd,0x82};
    const bool cooked=profile==KUI_GAMES_CDDA_TOY_COOKED;
    if(!image || !image->data ||
       (profile!=KUI_GAMES_CDDA_TOY_RAW && !cooked) ||
       image->info.payload_bytes!=(cooked?82632u:82516u) ||
       image->info.memory_bytes!=image->info.payload_bytes ||
       image->info.crc32!=(cooked?UINT32_C(0x9b2dd51b):UINT32_C(0x00c91145)) ||
       memcmp(image->info.build,cooked?"a8eba3e74b03":"9739eb693b02",13u)) return false;
    struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);
    kui_sha256_update(&hash,image->data,image->info.payload_bytes);
    kui_sha256_digest(&hash,digest);
    return !memcmp(digest,cooked?cooked_sha:raw_sha,sizeof(digest));
}
#endif
