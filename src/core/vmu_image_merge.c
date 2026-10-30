/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_image_merge.h"
#include <string.h>

enum { BLOCK=512, FAT=254, DIR=253, END=0xfffa };
struct span {uintptr_t begin,end;};
static unsigned le16(const uint8_t *p) {return p[0]|((unsigned)p[1]<<8);}
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static int span(const void *p,size_t bytes,struct span *out) {
    uintptr_t at=(uintptr_t)p;
    if(!p || bytes>UINTPTR_MAX-at) return 0;
    out->begin=at;out->end=at+bytes;return 1;
}
static int overlap(struct span a,struct span b) {return a.begin<b.end && b.begin<a.end;}
static int read_image(void *context,uint16_t block,void *out) {
    if(block>=256u) return -1;
    memcpy(out,(const uint8_t *)context+(size_t)block*BLOCK,BLOCK);return 0;
}
static int normalize(const uint8_t *entry,uint8_t name[12]) {
    unsigned n=12u;
    while(n && (!entry[n+3u] || entry[n+3u]==' ')) --n;
    memset(name,0,12u);memcpy(name,entry+4u,n);return n!=0u;
}
static int same_file(const struct kui_vmu_image_merge_ref *a,
    const uint8_t *image,const uint8_t *entry) {
    if(memcmp(a->entry,entry,2u) || memcmp(a->entry+4u,entry+4u,28u)) return 0;
    unsigned left=le16(a->entry+2u),right=le16(entry+2u),blocks=le16(entry+24u);
    for(unsigned n=0;n<blocks;n++) {
        if(memcmp(a->image+(size_t)left*BLOCK,image+(size_t)right*BLOCK,BLOCK)) return 0;
        left=le16(a->image+FAT*BLOCK+left*2u);
        right=le16(image+FAT*BLOCK+right*2u);
    }
    return 1;
}
static void conflict(struct kui_vmu_image_merge_report *report,const uint8_t name[12]) {
    for(unsigned n=0;n<12u;n++)
        report->conflict_name[n]=name[n]>=32u && name[n]<127u?(char)name[n]:(name[n]?'_':0);
    report->conflict_name[12]=0;
}
int kui_vmu_image_merge(const void *base,const void *incoming,void *destination,
    size_t bytes,struct kui_vmu_image_merge_workspace *work,
    struct kui_vmu_image_merge_report *report) {
    struct span ranges[5];
    if(bytes!=KUI_VMU_IMAGE_BYTES || !span(base,bytes,ranges) ||
       !span(incoming,bytes,ranges+1) || !span(destination,bytes,ranges+2) ||
       !span(work,sizeof(*work),ranges+3) || !span(report,sizeof(*report),ranges+4))
        return KUI_VMU_MERGE_ARGUMENT;
    for(unsigned i=0;i<5u;i++) for(unsigned j=0;j<i;j++)
        if(overlap(ranges[i],ranges[j])) return KUI_VMU_MERGE_ARGUMENT;
    memset(report,0,sizeof(*report));
    const uint8_t *images[2]={base,incoming};
    unsigned count=0,used=0,games=0,base_count=0;
    for(unsigned source=0;source<2u;source++) {
        struct kui_vmu_image_summary checked;
        const struct kui_vmu_image_io io={(void *)images[source],read_image,NULL,NULL};
        if(kui_vmu_image_validate(&io,bytes,&work->validation,&checked)) return KUI_VMU_MERGE_INVALID;
        for(unsigned slot=0;slot<KUI_VMU_IMAGE_DIRECTORY_ENTRIES;slot++) {
            const uint8_t *entry=images[source]+(DIR-slot/16u)*BLOCK+(slot%16u)*32u;
            if(!entry[0]) continue;
            uint8_t name[12];
            if(!normalize(entry,name)) return KUI_VMU_MERGE_INVALID;
            /* An incoming duplicate must be unique within its own source,
             * even if it would otherwise be skipped against the base. */
            for(unsigned prior=0;prior<slot;prior++) {
                const uint8_t *old=images[source]+(DIR-prior/16u)*BLOCK+(prior%16u)*32u;
                uint8_t old_name[12];
                if(old[0] && normalize(old,old_name) && !memcmp(name,old_name,12u))
                    return KUI_VMU_MERGE_INVALID;
            }
            unsigned existing=0;
            while(existing<count && memcmp(work->files[existing].name,name,12u)) ++existing;
            if(existing<count) {
                if(source && existing<base_count && same_file(work->files+existing,images[source],entry)) {
                    ++report->duplicates;continue;
                }
                conflict(report,name);return KUI_VMU_MERGE_NAME_CONFLICT;
            }
            unsigned blocks=le16(entry+24u);
            if(count>=KUI_VMU_IMAGE_USER_BLOCKS || blocks>KUI_VMU_IMAGE_USER_BLOCKS-used)
                return KUI_VMU_MERGE_FULL;
            if(entry[0]==0xcc && games) {conflict(report,name);return KUI_VMU_MERGE_MINIGAME_CONFLICT;}
            work->files[count].image=images[source];work->files[count].entry=entry;
            memcpy(work->files[count].name,name,12u);++count;used+=blocks;
            if(entry[0]==0xcc) ++games;
            if(source) ++report->added;
        }
        if(!source) base_count=count;
    }
    uint8_t *out=destination;unsigned high=KUI_VMU_IMAGE_USER_BLOCKS;
    for(unsigned b=0;b<256u;b++) kui_vmu_image_blank_block((uint16_t)b,out+(size_t)b*BLOCK);
    for(unsigned i=0;i<count;i++) {
        const struct kui_vmu_image_merge_ref *file=work->files+i;
        unsigned blocks=le16(file->entry+24u),at=le16(file->entry+2u);
        int game=file->entry[0]==0xcc;
        unsigned first=game?0u:high-1u;
        uint8_t *entry=out+(DIR-i/16u)*BLOCK+(i%16u)*32u;
        memcpy(entry,file->entry,32u);put16(entry+2u,first);
        for(unsigned n=0;n<blocks;n++) {
            unsigned target=game?n:first-n;
            memcpy(out+(size_t)target*BLOCK,file->image+(size_t)at*BLOCK,BLOCK);
            put16(out+FAT*BLOCK+target*2u,n+1u==blocks?END:(game?target+1u:target-1u));
            at=le16(file->image+FAT*BLOCK+at*2u);
        }
        if(!game) high-=blocks;
    }
    const struct kui_vmu_image_io io={out,read_image,NULL,NULL};
    if(kui_vmu_image_validate(&io,bytes,&work->validation,&report->summary)) return KUI_VMU_MERGE_VERIFY;
    return KUI_VMU_MERGE_OK;
}
