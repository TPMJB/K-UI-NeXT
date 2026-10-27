/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_save_snapshot.h"
#include "kui/hash.h"
#include <string.h>

static int overlaps(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y?y-x<an:x-y<bn);
}
static int fail(struct kui_vmu_save_snapshot *w,int result) {
    if(!w->failed) w->result=result;
    w->failed=1u;w->ready=0u;
    memset(&w->summary,0,sizeof(w->summary));
    return w->result;
}
static int valid(struct kui_vmu_save_snapshot *w) {
    return w && w->self==w && w->started;
}
static int physical(struct kui_vmu_save_snapshot *w,uint16_t block,void *out) {
    if(w->source_reads!=UINT32_MAX) ++w->source_reads;
    if(w->source.read_block(w->source.context,block,out) || w->failed)
        return fail(w,KUI_VMU_SAVE_SNAPSHOT_IO);
    return 0;
}
/* No caller output is touched until both complete replies agree and the
 * block's original SHA agrees. The work buffers are private and aligned. */
static int checked_physical(struct kui_vmu_save_snapshot *w,uint16_t block) {
    w->last_block=block;
    if(physical(w,block,w->first) || physical(w,block,w->second))
        return w->result;
    if(memcmp(w->first,w->second,512u))
        return fail(w,KUI_VMU_SAVE_SNAPSHOT_CHANGED);
    struct kui_sha256 hash;uint8_t digest[32];
    kui_sha256_init(&hash);kui_sha256_update(&hash,w->first,512u);
    kui_sha256_digest(&hash,digest);
    if(w->pinned[block]) {
        if(memcmp(w->block_sha256[block],digest,32u))
            return fail(w,KUI_VMU_SAVE_SNAPSHOT_CHANGED);
    } else {
        memcpy(w->block_sha256[block],digest,32u);w->pinned[block]=1u;
    }
    return 0;
}
static int read_view(void *context,uint16_t block,void *out) {
    struct kui_vmu_save_snapshot *w=context;
    if(!valid(w) || !w->active || w->failed || block>=256u || !out)
        return -1;
    w->last_block=block;
    if(block<200u && !w->allocated[block]) {
        memset(out,0,512u);return 0;
    }
    if(checked_physical(w,block)) return -1;
    memcpy(out,w->first,512u);return 0;
}
int kui_vmu_save_snapshot_capture(struct kui_vmu_save_snapshot *w,
    const struct kui_vmu_image_io *source,size_t image_bytes) {
    if(!w) return KUI_VMU_SAVE_SNAPSHOT_ARGUMENT;
    if(w->started) {
        if(w->self!=w) return KUI_VMU_SAVE_SNAPSHOT_ARGUMENT;
        return fail(w,KUI_VMU_SAVE_SNAPSHOT_STATE);
    }
    w->self=w;w->started=1u;
    if(!source || image_bytes!=KUI_VMU_IMAGE_BYTES ||
       overlaps(w,sizeof(*w),source,sizeof(*source)) || !source->read_block ||
       source->write_block || source->sync ||
       (source->context && overlaps(w,sizeof(*w),source->context,1u)))
        return fail(w,KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
    w->source=*source;w->active=1u;
    /* Pin the complete FAT before deciding that any user block is free.
     * The production validator below independently rejects malformed chains,
     * orphan allocations and invalid metadata; no invalid file is omitted. */
    int result=checked_physical(w,254u);
    if(!result) {
        memcpy(w->fat,w->first,512u);
        for(unsigned b=0;b<200u;b++)
            w->allocated[b]=(uint8_t)(w->fat[b*2u]!=0xfcu || w->fat[b*2u+1u]!=0xffu);
        const struct kui_vmu_image_io view={w,read_view,NULL,NULL};
        struct kui_vmu_image_summary previous={0,0,0,0},observed;
        for(unsigned pass=0;pass<2u && !result;pass++) {
            w->image_result=kui_vmu_image_validate(&view,image_bytes,&w->image,&observed);
            if(w->image_result || w->failed) {
                result=fail(w,KUI_VMU_SAVE_SNAPSHOT_IMAGE);break;
            }
            for(unsigned b=0;b<200u;b++)
                if(w->image.used[b]!=w->allocated[b]) {
                    result=fail(w,KUI_VMU_SAVE_SNAPSHOT_IMAGE);break;
                }
            if(result) break;
            if(pass && (previous.files!=observed.files ||
                previous.game_files!=observed.game_files ||
                previous.used_blocks!=observed.used_blocks ||
                previous.free_blocks!=observed.free_blocks)) {
                result=fail(w,KUI_VMU_SAVE_SNAPSHOT_CHANGED);break;
            }
            previous=observed;
        }
        if(!result) w->summary=previous;
    }
    w->active=0u;
    if(result) return result;
    w->ready=1u;return KUI_VMU_SAVE_SNAPSHOT_OK;
}
int kui_vmu_save_snapshot_read_block(void *context,uint16_t block,void *out) {
    struct kui_vmu_save_snapshot *w=context;
    if(!valid(w)) return KUI_VMU_SAVE_SNAPSHOT_ARGUMENT;
    if(w->failed) return w->result;
    if(w->active || !w->ready) return fail(w,KUI_VMU_SAVE_SNAPSHOT_STATE);
    if(block>=256u || !out || overlaps(w,sizeof(*w),out,512u))
        return fail(w,KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
    w->active=1u;
    int result=read_view(w,block,out);
    w->active=0u;
    return result?w->result:KUI_VMU_SAVE_SNAPSHOT_OK;
}
