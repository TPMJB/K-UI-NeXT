/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/report_stage_cache.h"
#include <string.h>

static int poison(struct kui_report_stage_cache *s) {
    s->count[0]=s->count[1]=0u;s->poisoned=1u;return -1;
}
int kui_report_stage_cache_init(struct kui_report_stage_cache *s,uint64_t card_sectors,
    uint64_t first,uint64_t end,const struct kui_vmu_fs_range *forbidden,size_t ranges,
    int (*read_multi)(void *,uint32_t,uint32_t,uint8_t *),void *context) {
    if(!s)return -1;
    memset(s,0,sizeof(*s));
    if(!read_multi || !card_sectors || card_sectors>(UINT64_C(1)<<32) ||
       first>=end || end>card_sectors || ranges>256u || (ranges && !forbidden))return poison(s);
    for(size_t i=0;i<ranges;i++)
        if(!forbidden[i].blocks || forbidden[i].lba>=card_sectors ||
           forbidden[i].blocks>card_sectors-forbidden[i].lba)return poison(s);
    s->card_sectors=card_sectors;s->partition_first=first;s->partition_end=end;
    s->forbidden=forbidden;s->forbidden_count=ranges;s->read_multi=read_multi;s->context=context;
    s->initialized=1u;return 0;
}
int kui_report_stage_cache_read(void *context,uint32_t lba,uint8_t out[512]) {
    struct kui_report_stage_cache *s=context;
    if(!s || !out)return -1;
    uintptr_t a=(uintptr_t)s,b=(uintptr_t)out;
    if(a>UINTPTR_MAX-sizeof(*s) || b>UINTPTR_MAX-512u ||
       (a<b+512u && b<a+sizeof(*s)))return poison(s);
    if(s->initialized!=1u || s->poisoned || s->requests>=KUI_VMU_FS_MAX_READS)return poison(s);
    ++s->requests;
    if((uint64_t)lba>=s->card_sectors ||
       (lba && ((uint64_t)lba<s->partition_first || (uint64_t)lba>=s->partition_end)))return poison(s);
    uint64_t end=lba?s->partition_end:1u;
    if(end>s->card_sectors)end=s->card_sectors;
    for(size_t i=0;i<s->forbidden_count;i++) {
        const struct kui_vmu_fs_range *f=&s->forbidden[i];
        if((uint64_t)lba>=f->lba && (uint64_t)lba-f->lba<f->blocks)return poison(s);
        if(f->lba>lba && f->lba<end)end=f->lba;
    }
    for(unsigned i=0;i<2u;i++)if(s->count[i] && lba>=s->first[i] && lba-s->first[i]<s->count[i]) {
        memcpy(out,s->bytes[i]+(lba-s->first[i])*512u,512u);s->recent=i;++s->hits;return 0;
    }
    uint32_t count=(uint32_t)(end-lba>8u?8u:end-lba);
    if(!count || count>KUI_VMU_FS_MAX_READS-s->physical_sectors)return poison(s);
    unsigned way=!s->count[0]?0u:!s->count[1]?1u:s->recent^1u;
    s->count[way]=0u;++s->wire_calls;s->physical_sectors+=count;
    if(s->read_multi(s->context,lba,count,s->bytes[way]))return poison(s);
    s->first[way]=lba;s->count[way]=count;s->recent=way;
    memcpy(out,s->bytes[way],512u);return 0;
}
