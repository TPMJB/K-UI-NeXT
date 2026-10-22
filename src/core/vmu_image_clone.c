/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_image_clone.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include <string.h>

struct image_clone_reader {
    struct kui_vmu_image_clone *work;
    const struct kui_vmu_image_io *io;
    unsigned destination;
};
static int image_clone_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y?y-x<an:x-y<bn);
}
static void image_clone_increment(uint32_t *value) {if(*value!=UINT32_MAX) ++*value;}
static int image_clone_fail(struct kui_vmu_image_clone *w,int result) {
    if(!w->failed) w->result=result;
    w->failed=1u;w->captured=0u;w->completed=0u;
    return w->result;
}
static int image_clone_valid(struct kui_vmu_image_clone *w) {
    return w && w->self==w && w->started && !w->failed;
}
static int image_clone_read(struct kui_vmu_image_clone *w,const struct kui_vmu_image_io *io,
    unsigned destination,uint16_t block,void *out) {
    if(!image_clone_valid(w) || !w->active || block>=256u || !out) return -1;
    w->last_block=block;
    image_clone_increment(destination?&w->destination_reads:&w->source_reads);
    if(io->read_block(io->context,block,out) || w->failed)
        return image_clone_fail(w,KUI_VMU_CLONE_IO);
    return 0;
}
static int image_clone_checked_read(void *context,uint16_t block,void *out) {
    struct image_clone_reader *r=context;struct kui_vmu_image_clone *w=r->work;
    if(image_clone_read(w,r->io,r->destination,block,out)) return -1;
    if(kui_retail_crc32(0,out,512u)!=w->block_crc[block])
        return image_clone_fail(w,r->destination?KUI_VMU_CLONE_VERIFY:KUI_VMU_CLONE_CHANGED);
    return 0;
}
static int image_clone_digest(struct kui_vmu_image_clone *w,const struct kui_vmu_image_io *io,
    unsigned destination,unsigned capture) {
    struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);
    for(unsigned block=0;block<256u;block++) {
        if(image_clone_read(w,io,destination,(uint16_t)block,w->buffer)) return w->result;
        uint32_t crc=kui_retail_crc32(0,w->buffer,512u);
        if(capture) w->block_crc[block]=crc;
        else if(crc!=w->block_crc[block])
            return image_clone_fail(w,destination?KUI_VMU_CLONE_VERIFY:KUI_VMU_CLONE_CHANGED);
        kui_sha256_update(&hash,w->buffer,512u);
    }
    kui_sha256_digest(&hash,digest);
    if(capture) memcpy(w->sha256,digest,32u);
    else if(memcmp(w->sha256,digest,32u))
        return image_clone_fail(w,destination?KUI_VMU_CLONE_VERIFY:KUI_VMU_CLONE_CHANGED);
    return 0;
}
static int image_clone_validate(struct kui_vmu_image_clone *w,const struct kui_vmu_image_io *io,
    unsigned destination) {
    struct image_clone_reader reader={w,io,destination};
    const struct kui_vmu_image_io checked={&reader,image_clone_checked_read,NULL,NULL};
    struct kui_vmu_image_summary summary;
    w->image_result=kui_vmu_image_validate(&checked,KUI_VMU_IMAGE_BYTES,&w->image,&summary);
    if(w->image_result || w->failed) return image_clone_fail(w,KUI_VMU_CLONE_IMAGE);
    if(destination) {
        if(summary.files!=w->summary.files || summary.game_files!=w->summary.game_files ||
           summary.used_blocks!=w->summary.used_blocks || summary.free_blocks!=w->summary.free_blocks)
            return image_clone_fail(w,KUI_VMU_CLONE_VERIFY);
    } else w->summary=summary;
    return 0;
}
int kui_vmu_image_clone_capture(struct kui_vmu_image_clone *w,
    const struct kui_vmu_image_io *source,size_t image_bytes) {
    if(!w) return KUI_VMU_CLONE_ARGUMENT;
    if(w->started) {
        if(w->self!=w) return KUI_VMU_CLONE_ARGUMENT;
        return image_clone_fail(w,KUI_VMU_CLONE_STATE);
    }
    w->self=w;w->started=1u;w->phase=KUI_VMU_CLONE_CAPTURE;
    if(!source || image_bytes!=KUI_VMU_IMAGE_BYTES ||
       image_clone_overlap(w,sizeof(*w),source,sizeof(*source)) ||
       !source->read_block || source->write_block || source->sync)
        return image_clone_fail(w,KUI_VMU_CLONE_ARGUMENT);
    w->source=*source;w->active=1u;
    int result=image_clone_digest(w,&w->source,0u,1u);
    if(!result) {w->phase=KUI_VMU_CLONE_VALIDATE;result=image_clone_validate(w,&w->source,0u);}
    w->active=0u;
    if(result) return result;
    w->captured=1u;w->phase=KUI_VMU_CLONE_READY;return 0;
}
int kui_vmu_image_clone_verify_source(struct kui_vmu_image_clone *w) {
    if(!w || w->self!=w || !w->started) return KUI_VMU_CLONE_ARGUMENT;
    if(w->failed) return w->result;
    if(w->active || !w->captured || w->copy_started)
        return image_clone_fail(w,KUI_VMU_CLONE_STATE);
    w->active=1u;w->phase=KUI_VMU_CLONE_RECHECK;
    int result=image_clone_digest(w,&w->source,0u,0u);w->active=0u;
    if(!result) w->phase=KUI_VMU_CLONE_READY;
    return result;
}
static int image_clone_write(struct kui_vmu_image_clone *w,const struct kui_vmu_image_io *io,
    uint16_t block) {
    if(w->failed) return w->result;
    w->last_block=block;image_clone_increment(&w->destination_writes);
    if(io->write_block(io->context,block,w->buffer) || w->failed)
        return image_clone_fail(w,KUI_VMU_CLONE_IO);
    return 0;
}
static int image_clone_sync(struct kui_vmu_image_clone *w,const struct kui_vmu_image_io *io) {
    if(w->failed) return w->result;
    image_clone_increment(&w->syncs);
    if(io->sync(io->context) || w->failed) return image_clone_fail(w,KUI_VMU_CLONE_IO);
    return 0;
}
int kui_vmu_image_clone_copy(struct kui_vmu_image_clone *w,
    const struct kui_vmu_image_io *destination,size_t image_bytes) {
    if(!w || w->self!=w || !w->started) return KUI_VMU_CLONE_ARGUMENT;
    if(w->failed) return w->result;
    if(w->active || !w->captured || w->copy_started)
        return image_clone_fail(w,KUI_VMU_CLONE_STATE);
    if(!destination || image_bytes!=KUI_VMU_IMAGE_BYTES ||
       image_clone_overlap(w,sizeof(*w),destination,sizeof(*destination)) ||
       !destination->read_block || !destination->write_block || !destination->sync ||
       (destination->context==w->source.context && destination->read_block==w->source.read_block))
        return image_clone_fail(w,KUI_VMU_CLONE_ARGUMENT);
    /* Pin the descriptor itself; callback contexts are caller-owned and must
     * stay stable. No pointer to this destination survives the call. */
    const struct kui_vmu_image_io io=*destination;
    if(kui_vmu_image_clone_verify_source(w)) return w->result;
    w->copy_started=1u;w->active=1u;w->phase=KUI_VMU_CLONE_ZERO;
    int result=0;
    for(unsigned block=0;block<256u;block++) {
        if(image_clone_read(w,&io,1u,(uint16_t)block,w->buffer)) {result=w->result;goto done;}
        for(unsigned i=0;i<512u;i++) if(w->buffer[i]) {
            result=image_clone_fail(w,KUI_VMU_CLONE_NOT_ZERO);goto done;
        }
    }
    struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);
    w->phase=KUI_VMU_CLONE_BODY;
    for(unsigned block=0;block<256u;block++) {
        if(image_clone_read(w,&w->source,0u,(uint16_t)block,w->buffer)) {result=w->result;goto done;}
        if(kui_retail_crc32(0,w->buffer,512u)!=w->block_crc[block]) {
            result=image_clone_fail(w,KUI_VMU_CLONE_CHANGED);goto done;
        }
        kui_sha256_update(&hash,w->buffer,512u);
        if(block<255u && image_clone_write(w,&io,(uint16_t)block)) {result=w->result;goto done;}
    }
    /* buffer still holds the freshly checked source root. An incomplete body
     * or changed full snapshot cannot publish a valid destination root. */
    kui_sha256_digest(&hash,digest);
    if(memcmp(digest,w->sha256,32u)) {result=image_clone_fail(w,KUI_VMU_CLONE_CHANGED);goto done;}
    if(image_clone_sync(w,&io)) {result=w->result;goto done;}
    w->phase=KUI_VMU_CLONE_ROOT;
    if(image_clone_write(w,&io,255u) || image_clone_sync(w,&io)) {result=w->result;goto done;}
    w->phase=KUI_VMU_CLONE_READBACK;
    result=image_clone_digest(w,&io,1u,0u);
    if(!result) result=image_clone_validate(w,&io,1u);
done:
    w->active=0u;
    if(result) return result;
    w->completed=1u;w->phase=KUI_VMU_CLONE_DONE;return 0;
}
