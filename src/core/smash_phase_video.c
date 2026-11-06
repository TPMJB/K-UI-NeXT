/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_phase_video.h"
#include "kui/retail_image.h"
#include "kui/smash_phase_trace.h"
#include <string.h>
static bool header(const struct kui_smash_phase_video_seed *s) {
    return s && s->magic0==KUI_SMASH_PHASE_VIDEO_MAGIC0 && s->magic1==KUI_SMASH_PHASE_VIDEO_MAGIC1 &&
        s->version==1u && s->bytes==sizeof(*s) && !s->reserved;
}
static bool display_shape(const uint32_t r[14]) {
    /* KOS vid_shutdown -> vid_init(DM_640x480,PM_RGB565) sets framebuffer0,
     * 640 RGB565 pixels per row, and enabled/unblanked scanout. Preserve
     * its cable-specific timing words exactly; never synthesize a mode.
     * KOS commit fcfa7d869471591ca1c777543261a7bfea7cb726 video.c. */
    bool interlace=(r[6]&UINT32_C(0x10))!=0u;
    uint32_t size=319u | ((interlace?239u:479u)<<10) | ((interlace?321u:1u)<<20);
    return (r[0]&15u)==5u && (r[1]&7u)==1u && !r[2] &&
        (!interlace || r[3]==1280u) && r[4]==size && !(r[10]&UINT32_C(0x108));
}
static uint32_t checksum(const struct kui_smash_phase_video_seed *s) {
    const uint32_t zero=0u;
    uint32_t crc=kui_retail_crc32(0u,s,offsetof(struct kui_smash_phase_video_seed,crc32));
    crc=kui_retail_crc32(crc,&zero,sizeof(zero));
    return kui_retail_crc32(crc,&s->reserved,sizeof(s->reserved));
}
bool kui_smash_phase_video_blank(const struct kui_smash_phase_video_seed *s) {
    if(!header(s) || s->crc32)return false;
    uint32_t any=0u;for(unsigned i=0u;i<14u;i++)any|=s->regs[i];return !any;
}
bool kui_smash_phase_video_valid(const struct kui_smash_phase_video_seed *s) {
    return header(s) && display_shape(s->regs) && s->crc32==checksum(s);
}
bool kui_smash_phase_video_capture(struct kui_smash_phase_video_seed *s,const uint32_t regs[14]) {
    if(!s || !regs || !display_shape(regs))return false;
    *s=(struct kui_smash_phase_video_seed){.magic0=KUI_SMASH_PHASE_VIDEO_MAGIC0,
        .magic1=KUI_SMASH_PHASE_VIDEO_MAGIC1,.version=1u,.bytes=sizeof(*s)};
    memcpy(s->regs,regs,sizeof(s->regs));s->crc32=checksum(s);return true;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
int kui_smash_phase_outer_profile_find(const void *payload,size_t bytes,size_t *offset) {
    if(offset)*offset=SIZE_MAX;
    if(!payload)return -1;
    const uint8_t *p=payload;size_t found=SIZE_MAX;
    for(size_t at=0u;at<=bytes && 16u<=bytes-at;at+=4u) {
        if(le32(p+at)!=KUI_SMASH_PHASE_VIDEO_MAGIC0 || le32(p+at+4u)!=KUI_SMASH_PHASE_OUTER_MAGIC1)continue;
        if(found!=SIZE_MAX || le32(p+at+8u)!=1u || le32(p+at+12u)!=16u)return -1;
        found=at;
    }
    if(found==SIZE_MAX)return 0;
    if(offset)*offset=found;
    return 1;
}
bool kui_smash_phase_pair_profile(const void *outer,size_t outer_bytes,
    const void *child,size_t child_bytes,size_t native_at,uint32_t *flags) {
    if(flags)*flags=0u;
    if(!child || ((uintptr_t)child&3u) || (native_at&3u) ||
       native_at>child_bytes || 304u>child_bytes-native_at)return false;
    int profile=kui_smash_phase_outer_profile_find(outer,outer_bytes,NULL);
    const uint8_t *p=child;unsigned timing=0u,video=0u;
    for(size_t at=0u;at<=child_bytes && 16u<=child_bytes-at;at+=4u) {
        if(le32(p+at)!=KUI_SMASH_PHASE_VIDEO_MAGIC0 || le32(p+at+8u)!=1u)continue;
        if(le32(p+at+4u)==KUI_SMASH_PHASE_TRACE_MAGIC1 && le32(p+at+12u)==KUI_SMASH_PHASE_TRACE_BYTES)++timing;
        if(le32(p+at+4u)==KUI_SMASH_PHASE_VIDEO_MAGIC1 && le32(p+at+12u)==KUI_SMASH_PHASE_VIDEO_BYTES)++video;
    }
    if(profile<0 || timing>1u || video>1u)return false;
    if(!profile && !timing && !video)return true;
    const size_t offset=304u+368u;
    if(profile!=1 || timing!=1u || video!=1u || native_at>child_bytes ||
       offset+KUI_SMASH_PHASE_TRACE_BYTES+KUI_SMASH_PHASE_VIDEO_BYTES>child_bytes-native_at)return false;
    if(!kui_smash_phase_trace_blank((const void *)(p+native_at+offset)) ||
       !kui_smash_phase_video_blank((const void *)(p+native_at+offset+KUI_SMASH_PHASE_TRACE_BYTES)))return false;
    if(flags)*flags=7u;
    return true;
}
