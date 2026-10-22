/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_GAMES_CDDA_STATUS_H
#define KUI_GAMES_CDDA_STATUS_H
#include "kui/games.h"
#include "kui/game_metadata.h"
#include "kui/wince_cdda_profiles.h"
#include <string.h>

/* Evidence labels describe the owner's complete tested boot runtime, IP and
 * recorded disc geometry. Armada's original startup inventory did not record
 * track lengths; its end bounds are validated, not treated as known facts.
 * These labels never grant launch admission, and familiar title text
 * cannot upgrade an unfamiliar image. Unknown or modified releases stay
 * experimental even if a compatible runtime adapter is available. */
static inline unsigned kui_games_cdda_test_candidate(const struct kui_game_image *image,
        const struct kui_game_metadata *meta) {
    if(!image || !meta || !meta->ip_valid || !meta->boot_valid ||
       image->format!=KUI_GAME_IMAGE_GDI || image->cd_image || image->scrambled ||
       !image->count || image->count>KUI_GAME_TRACK_MAX ||
       image->data_lba!=45000u || meta->session_lba!=45000u ||
       meta->native_cd || meta->windows_ce==meta->native_gd ||
       !memchr(meta->product,0,sizeof(meta->product)) ||
       !memchr(meta->bootfile,0,sizeof(meta->bootfile)))
        return 0u;
    bool cooked_data=false,raw_data=false;
    for(unsigned i=0;i<image->count;i++) {
        const struct kui_game_image_track *t=&image->tracks[i];
        const bool cooked=t->control==4u && t->sector_bytes==2048u;
        if(t->number!=i+1u || (t->control!=0u && t->control!=4u) ||
           t->end_lba<=t->start_lba || t->end_lba>KUI_GAME_LBA_LIMIT ||
           (i && t->start_lba<image->tracks[i-1u].end_lba) ||
           (t->sector_bytes!=2352u && !cooked) || t->file_offset ||
           t->sector_mode!=(t->control?1u:0u) || t->data_offset!=(t->control && !cooked?16u:0u) ||
           t->file_bytes!=(uint64_t)(t->end_lba-t->start_lba)*t->sector_bytes)
            return 0u;
        if(t->control) {cooked_data|=cooked;raw_data|=!cooked;}
    }
    /* The owner's 2048 Worms copy booted three times from a cold start with
     * CD music On. That evidence covers the consistently converted DATA
     * layout, not arbitrary mixtures of raw/cooked tracks or other titles. */
    if(cooked_data && raw_data) return 0u;
    if(meta->windows_ce) {
        static const uint32_t bam4_ends[26]={300u,976u,370523u,371347u,372014u,
            372915u,373626u,379011u,384738u,390481u,395473u,398926u,404448u,
            425246u,445520u,466032u,474231u,485598u,486386u,487098u,487822u,
            498356u,508297u,527383u,547956u,549150u};
        static const uint32_t worms_ends[8]={300u,946u,91378u,117095u,
            232929u,329558u,379425u,549150u};
        for(enum kui_wince_cdda_profile p=KUI_WINCE_CDDA_ARMADA;
                p<=KUI_WINCE_CDDA_WORMS_ARMAGEDDON;p++) {
            const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(p);
            if((cooked_data && p!=KUI_WINCE_CDDA_WORMS_ARMAGEDDON) ||
               image->count!=disc->track_count || meta->boot_bytes!=disc->kernel_bytes ||
               meta->boot_lba!=disc->boot_lba || strcmp(meta->product,disc->product) ||
               strcmp(meta->bootfile,"0WINCEOS.BIN")) continue;
            const uint32_t *ends=p==KUI_WINCE_CDDA_BUST_A_MOVE_4?bam4_ends:
                p==KUI_WINCE_CDDA_WORMS_ARMAGEDDON?worms_ends:NULL;
            unsigned i=0;
            for(;i<image->count;i++) {
                const struct kui_game_image_track *t=&image->tracks[i];
                if(t->start_lba!=disc->starts[i] || (ends && t->end_lba!=ends[i]) ||
                   t->control!=kui_wince_cdda_track_control(p,i)) break;
            }
            if(i==image->count) return (unsigned)p;
        }
    } else if(meta->native_gd && image->count==15u && meta->boot_bytes==748444u &&
              meta->boot_lba==548634u) {
        static const uint32_t starts[15]={0u,7257u,45000u,201900u,219712u,234091u,
            255599u,272017u,282799u,303092u,323352u,341314u,356690u,374201u,377422u};
        static const uint32_t ends[15]={7107u,7753u,201750u,219712u,234091u,
            255599u,272017u,282799u,303092u,323352u,341314u,356690u,374201u,377272u,549150u};
        for(unsigned i=0;i<15u;i++) {
            const struct kui_game_image_track *t=&image->tracks[i];
            if(t->number!=i+1u || t->start_lba!=starts[i] || t->end_lba!=ends[i] ||
               t->sector_bytes!=2352u || t->file_offset ||
               t->control!=(i==0u || i==2u || i==14u?4u:0u)) return 0u;
        }
        return 4u;
    }
    return 0u;
}
static inline enum kui_game_cdda_compatibility kui_games_cdda_test_status(
        unsigned candidate,uint32_t ip_crc,const uint8_t boot_sha[32]) {
    static const uint32_t ip_crcs[4]={UINT32_C(0x684f8040),UINT32_C(0x8fb049c5),
        UINT32_C(0x86bbb334),UINT32_C(0x38ba2868)};
    static const uint8_t toy_sha[32]={
        0xee,0x68,0xa3,0x26,0xda,0x96,0x4f,0xec,0x58,0x92,0xb7,0x22,0x63,0x11,0xf2,0x29,
        0x3f,0xd3,0xa0,0xbb,0xf5,0xba,0xc4,0x96,0xec,0x82,0x99,0x19,0x64,0xd4,0x9b,0xbd};
    if(candidate<1u || candidate>4u || !boot_sha || ip_crc!=ip_crcs[candidate-1u])
        return KUI_GAME_CDDA_UNTESTED;
    const uint8_t *expected=candidate==4u?toy_sha:
        kui_wince_cdda_kernel_sha((enum kui_wince_cdda_profile)candidate);
    if(memcmp(expected,boot_sha,32u)) return KUI_GAME_CDDA_UNTESTED;
    return candidate==4u?KUI_GAME_CDDA_TESTED_LIMITED:KUI_GAME_CDDA_TESTED;
}
#endif
