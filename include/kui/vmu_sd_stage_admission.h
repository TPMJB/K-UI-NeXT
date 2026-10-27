/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_SD_STAGE_ADMISSION_H
#define KUI_VMU_SD_STAGE_ADMISSION_H
#include "kui/vmu_sd_profile.h"
#include "kui/vmu_backing.h"
#include "kui/retail_image.h"
#include <string.h>

/* High-stage admission only. The caller separately admits the exact title.
 * Every mapped game allocation, including audio tracks, is protected. */
static inline bool kui_vmu_sd_stage_admit(const struct kui_vmu_sd_profile *p,
    const struct kui_retail_manifest *m) {
    if(kui_vmu_sd_profile_validate(p) || kui_retail_manifest_validate(m)!=KUI_GAME_OK ||
       m->storage_transport!=KUI_STORAGE_SCI || m->reader!=KUI_RETAIL_READER_STANDARD ||
       (m->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       m->card_sectors>p->card_sectors || m->partition_start!=p->partition_first ||
       m->partition_end!=p->partition_end) return false;
    for(uint32_t i=0;i<p->extent_count;i++) {
        const struct kui_vmu_sd_profile_extent *v=&p->extents[i];
        uint64_t vend=(uint64_t)v->lba+v->blocks;
        for(uint32_t j=0;j<m->extent_count;j++) {
            const struct kui_retail_extent *g=&m->slots[m->track_count+j].extent;
            if((uint64_t)v->lba<(uint64_t)g->card_lba+g->blocks &&
               (uint64_t)g->card_lba<vend) return false;
        }
    }
    return true;
}

static inline bool kui_vmu_sd_stage_identity(const struct kui_vmu_sd_profile *p,
    const struct kui_vmu_sd_identity *actual) {
    return p && actual && actual->sectors==p->card_sectors &&
        (unsigned)actual->high_capacity==p->high_capacity && !memcmp(actual->cid,p->cid,16u);
}
#endif
