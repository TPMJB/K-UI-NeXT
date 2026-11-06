/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_fs_witness.h"
#include "kui/hash.h"
#include <string.h>

_Static_assert(sizeof(struct kui_vmu_fs_witness_record)==36u,"Witness wire record");
_Static_assert(512u+8192u*36u==KUI_VMU_FS_WITNESS_FILE_BYTES,"Witness file bound");
_Static_assert(KUI_VMU_FS_WITNESS_INDEX_SLOTS==2u*KUI_VMU_FS_WITNESS_MAX_RECORDS,
    "Witness index has at least half its slots free at capacity");
static uint32_t witness_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t witness_u64(const uint8_t *p) {return witness_u32(p)|(uint64_t)witness_u32(p+4u)<<32;}
static void witness_put32(uint8_t *p,uint32_t n) {
    for(unsigned i=0u;i<4u;i++)p[i]=(uint8_t)(n>>(8u*i));
}
static void witness_put64(uint8_t *p,uint64_t n) {
    witness_put32(p,(uint32_t)n);witness_put32(p+4u,(uint32_t)(n>>32));
}
static int witness_zero(const void *data,size_t bytes) {
    const uint8_t *p=data;for(size_t i=0u;i<bytes;i++)if(p[i])return 0;return 1;
}
static int witness_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return an && bn && (x<=y?y-x<an:x-y<bn);
}
static int witness_binding_valid(const struct kui_vmu_fs_witness_binding *b) {
    if(!b || !b->card_sectors || b->card_sectors>UINT64_C(0x100000000) ||
       b->high_capacity>1u || (!b->high_capacity && b->card_sectors>UINT64_C(0x800000)) ||
       b->partition_first>=b->partition_end || b->partition_end>b->card_sectors ||
       !b->generation || witness_zero(b->cid,16u) || witness_zero(b->image_id,16u) ||
       witness_zero(b->binding_sha256,32u))return 0;
    unsigned ones=0u;for(unsigned i=0u;i<16u;i++)ones+=b->cid[i]==0xffu;
    return ones!=16u;
}
static int witness_lba_valid(const struct kui_vmu_fs_witness_binding *b,uint32_t lba) {
    return (uint64_t)lba<b->card_sectors && (!lba ||
        ((uint64_t)lba>=b->partition_first && (uint64_t)lba<b->partition_end));
}
static void witness_sector_sha(const void *sector,uint8_t out[32]) {
    struct kui_sha256 h;kui_sha256_init(&h);kui_sha256_update(&h,sector,512u);kui_sha256_digest(&h,out);
}
static int witness_capture_fault(struct kui_vmu_fs_witness_capture *c,int result) {
    if(!c->result)c->result=result;
    return result;
}
static unsigned witness_index(uint32_t lba) {
    lba^=lba>>16;lba*=UINT32_C(0x7feb352d);lba^=lba>>15;
    lba*=UINT32_C(0x846ca68b);lba^=lba>>16;
    return lba&(KUI_VMU_FS_WITNESS_INDEX_SLOTS-1u);
}
int kui_vmu_fs_witness_capture_begin(struct kui_vmu_fs_witness_capture *c,
    const struct kui_vmu_fs_witness_binding *b,kui_vmu_fs_witness_read_sector read,void *context) {
    if(!c || !read || !witness_binding_valid(b) || witness_overlap(c,sizeof(*c),b,sizeof(*b)))
        return KUI_VMU_FS_WITNESS_ARGUMENT;
    /* Record data beyond count is neither read nor serialized. Avoid a large
     * unnecessary clear while still dropping every former index/authority. */
    memset(c->index,0,sizeof(c->index));c->binding=*b;c->read=read;c->context=context;
    c->count=c->observed_reads=c->finished=0u;c->started=1u;c->result=0;return 0;
}
int kui_vmu_fs_witness_capture_read(void *context,uint32_t lba,uint8_t out[512]) {
    struct kui_vmu_fs_witness_capture *c=context;
    if(!c || c->started!=1u || c->finished)return KUI_VMU_FS_WITNESS_ARGUMENT;
    if(!out || !c->read || !witness_lba_valid(&c->binding,lba) ||
       witness_overlap(c,sizeof(*c),out,512u))
        return witness_capture_fault(c,KUI_VMU_FS_WITNESS_ARGUMENT);
    c->started=2u;int result=c->read(c->context,lba,out);c->started=1u;
    if(result)return witness_capture_fault(c,KUI_VMU_FS_WITNESS_IO);
    if(c->observed_reads!=UINT32_MAX)++c->observed_reads;
    if(c->result)return 0; /* Capture refusal never truncates ordinary admission. */
    uint8_t digest[32];witness_sector_sha(out,digest);unsigned slot=witness_index(lba);
    for(unsigned probes=0u;probes<KUI_VMU_FS_WITNESS_INDEX_SLOTS;probes++) {
        unsigned tag=c->index[slot];
        if(!tag) {
            if(c->count==KUI_VMU_FS_WITNESS_MAX_RECORDS) {
                (void)witness_capture_fault(c,KUI_VMU_FS_WITNESS_LIMIT);return 0;
            }
            struct kui_vmu_fs_witness_record *r=&c->records[c->count];
            witness_put32(r->lba,lba);memcpy(r->sha256,digest,32u);
            c->index[slot]=(uint16_t)(++c->count);return 0;
        }
        if(tag>c->count) {(void)witness_capture_fault(c,KUI_VMU_FS_WITNESS_FORMAT);return 0;}
        const struct kui_vmu_fs_witness_record *r=&c->records[tag-1u];
        if(witness_u32(r->lba)==lba) {
            if(memcmp(r->sha256,digest,32u))(void)witness_capture_fault(c,KUI_VMU_FS_WITNESS_CHANGED);
            return 0;
        }
        slot=(slot+1u)&(KUI_VMU_FS_WITNESS_INDEX_SLOTS-1u);
    }
    (void)witness_capture_fault(c,KUI_VMU_FS_WITNESS_LIMIT);return 0;
}
static void witness_swap(struct kui_vmu_fs_witness_record *a,struct kui_vmu_fs_witness_record *b) {
    struct kui_vmu_fs_witness_record temporary=*a;*a=*b;*b=temporary;
}
static void witness_sift(struct kui_vmu_fs_witness_record *r,unsigned root,unsigned count) {
    while(root<count/2u) {
        unsigned child=root*2u+1u;
        if(child+1u<count && witness_u32(r[child].lba)<witness_u32(r[child+1u].lba))++child;
        if(witness_u32(r[root].lba)>=witness_u32(r[child].lba))return;
        witness_swap(&r[root],&r[child]);root=child;
    }
}
static void witness_sort(struct kui_vmu_fs_witness_record *r,unsigned count) {
    for(unsigned i=count/2u;i>0u;)witness_sift(r,--i,count);
    for(unsigned end=count;end>1u;) {witness_swap(&r[0],&r[--end]);witness_sift(r,0u,end);}
}
static void witness_header(uint8_t out[512],const struct kui_vmu_fs_witness_binding *b,uint32_t count) {
    memset(out,0,512u);memcpy(out,"KUIFSW01",8u);witness_put32(out+8u,1u);
    witness_put32(out+12u,512u);witness_put32(out+16u,36u);witness_put32(out+20u,count);
    witness_put32(out+24u,b->generation);witness_put32(out+28u,b->high_capacity);
    witness_put64(out+32u,b->card_sectors);witness_put64(out+40u,b->partition_first);
    witness_put64(out+48u,b->partition_end);memcpy(out+56u,b->cid,16u);
    memcpy(out+72u,b->image_id,16u);memcpy(out+88u,b->binding_sha256,32u);
}
static void witness_manifest_sha(const uint8_t header[512],
    const struct kui_vmu_fs_witness_record *records,uint32_t count,uint8_t out[32]) {
    static const char domain[]="KUI/VMU/FS-WITNESS/SHA256/V1";
    static const uint8_t zero[32]={0};struct kui_sha256 h;kui_sha256_init(&h);
    kui_sha256_update(&h,domain,sizeof(domain));kui_sha256_update(&h,header,120u);
    kui_sha256_update(&h,zero,32u);kui_sha256_update(&h,header+152u,360u);
    kui_sha256_update(&h,records,(size_t)count*36u);kui_sha256_digest(&h,out);
}
int kui_vmu_fs_witness_capture_finish(struct kui_vmu_fs_witness_capture *c,
    int admission_result,uint8_t header[512],uint8_t digest[32]) {
    if(!c || !header || !digest || witness_overlap(c,sizeof(*c),header,512u) ||
       witness_overlap(c,sizeof(*c),digest,32u) || witness_overlap(header,512u,digest,32u))
        return KUI_VMU_FS_WITNESS_ARGUMENT;
    memset(header,0,512u);memset(digest,0,32u);
    if(c->started!=1u || c->finished)return KUI_VMU_FS_WITNESS_ARGUMENT;
    c->finished=1u;
    if(admission_result)return witness_capture_fault(c,KUI_VMU_FS_WITNESS_ADMISSION);
    if(c->result)return c->result;
    if(!c->count || c->count>KUI_VMU_FS_WITNESS_MAX_RECORDS || !witness_binding_valid(&c->binding))
        return witness_capture_fault(c,KUI_VMU_FS_WITNESS_FORMAT);
    witness_sort(c->records,c->count);
    for(unsigned i=0u;i<c->count;i++)
        if(!witness_lba_valid(&c->binding,witness_u32(c->records[i].lba)) ||
           (i && witness_u32(c->records[i-1u].lba)>=witness_u32(c->records[i].lba)))
            return witness_capture_fault(c,KUI_VMU_FS_WITNESS_FORMAT);
    witness_header(header,&c->binding,c->count);witness_manifest_sha(header,c->records,c->count,digest);
    memcpy(header+120u,digest,32u);return 0;
}
static void witness_header_binding(const uint8_t h[512],struct kui_vmu_fs_witness_binding *b) {
    b->generation=witness_u32(h+24u);b->high_capacity=witness_u32(h+28u);
    b->card_sectors=witness_u64(h+32u);b->partition_first=witness_u64(h+40u);
    b->partition_end=witness_u64(h+48u);memcpy(b->cid,h+56u,16u);
    memcpy(b->image_id,h+72u,16u);memcpy(b->binding_sha256,h+88u,32u);
}
int kui_vmu_fs_witness_header_count(const uint8_t header[512],uint32_t *out) {
    if(!header || !out || witness_overlap(header,512u,out,sizeof(*out)))return KUI_VMU_FS_WITNESS_ARGUMENT;
    *out=0u;struct kui_vmu_fs_witness_binding b;witness_header_binding(header,&b);
    uint32_t count=witness_u32(header+20u);
    if(memcmp(header,"KUIFSW01",8u) || witness_u32(header+8u)!=1u ||
       witness_u32(header+12u)!=512u || witness_u32(header+16u)!=36u ||
       !count || count>KUI_VMU_FS_WITNESS_MAX_RECORDS || !witness_binding_valid(&b) ||
       !witness_zero(header+152u,360u) || witness_zero(header+120u,32u))return KUI_VMU_FS_WITNESS_FORMAT;
    *out=count;return 0;
}
static int witness_same_binding(const uint8_t h[512],const struct kui_vmu_fs_witness_binding *b) {
    return witness_u32(h+24u)==b->generation && witness_u32(h+28u)==b->high_capacity &&
        witness_u64(h+32u)==b->card_sectors && witness_u64(h+40u)==b->partition_first &&
        witness_u64(h+48u)==b->partition_end && !memcmp(h+56u,b->cid,16u) &&
        !memcmp(h+72u,b->image_id,16u) && !memcmp(h+88u,b->binding_sha256,32u);
}
int kui_vmu_fs_witness_validate(const uint8_t header[512],
    const struct kui_vmu_fs_witness_record *records,size_t bytes,
    const struct kui_vmu_fs_witness_binding *b,const uint8_t expected[32]) {
    if(!header || !records || !expected || !witness_binding_valid(b) ||
       witness_overlap(expected,32u,header,512u) || witness_overlap(expected,32u,records,bytes))
        return KUI_VMU_FS_WITNESS_ARGUMENT;
    uint32_t count;int result=kui_vmu_fs_witness_header_count(header,&count);if(result)return result;
    if(bytes!=(size_t)count*36u)return KUI_VMU_FS_WITNESS_FORMAT;
    if(!witness_same_binding(header,b))return KUI_VMU_FS_WITNESS_BINDING;
    if(memcmp(header+120u,expected,32u))return KUI_VMU_FS_WITNESS_DIGEST;
    for(unsigned i=0u;i<count;i++) {
        uint32_t lba=witness_u32(records[i].lba);
        if(!witness_lba_valid(b,lba) || (i && witness_u32(records[i-1u].lba)>=lba))
            return KUI_VMU_FS_WITNESS_FORMAT;
    }
    uint8_t digest[32];witness_manifest_sha(header,records,count,digest);
    return memcmp(digest,expected,32u)?KUI_VMU_FS_WITNESS_DIGEST:0;
}
int kui_vmu_fs_witness_replay(const uint8_t header[512],
    const struct kui_vmu_fs_witness_record *records,size_t bytes,
    const struct kui_vmu_fs_witness_binding *b,const uint8_t expected[32],
    kui_vmu_fs_witness_read_run read,void *context,void *scratch,size_t scratch_bytes,
    struct kui_vmu_fs_witness_stats *out) {
    struct kui_vmu_fs_witness_stats stats={0u,0u,UINT32_MAX,KUI_VMU_FS_WITNESS_ARGUMENT};
    if(!scratch || !read || !header || !records || !b || !expected ||
       ((uintptr_t)scratch&31u) || scratch_bytes<KUI_VMU_FS_WITNESS_SCRATCH_BYTES ||
       witness_overlap(scratch,KUI_VMU_FS_WITNESS_SCRATCH_BYTES,header,512u) ||
       witness_overlap(scratch,KUI_VMU_FS_WITNESS_SCRATCH_BYTES,records,bytes) ||
       witness_overlap(scratch,KUI_VMU_FS_WITNESS_SCRATCH_BYTES,b,sizeof(*b)) ||
       witness_overlap(scratch,KUI_VMU_FS_WITNESS_SCRATCH_BYTES,expected,32u) ||
       (out && (witness_overlap(out,sizeof(*out),header,512u) ||
        witness_overlap(out,sizeof(*out),records,bytes) || witness_overlap(out,sizeof(*out),b,sizeof(*b)) ||
        witness_overlap(out,sizeof(*out),expected,32u) ||
        witness_overlap(out,sizeof(*out),scratch,KUI_VMU_FS_WITNESS_SCRATCH_BYTES))))return stats.result;
    stats.result=kui_vmu_fs_witness_validate(header,records,bytes,b,expected);
    if(stats.result)goto done;
    uint32_t count=(uint32_t)(bytes/36u);
    for(uint32_t at=0u;at<count;) {
        uint32_t first=witness_u32(records[at].lba),run=1u;stats.bad_lba=first;
        if(!witness_lba_valid(b,first)) {stats.result=KUI_VMU_FS_WITNESS_FORMAT;goto done;}
        while(run<KUI_VMU_FS_WITNESS_RUN_BLOCKS && run<count-at &&
            (uint64_t)first+run<=UINT32_MAX && witness_u32(records[at+run].lba)==first+run &&
            witness_lba_valid(b,first+run))++run;
        ++stats.commands;
        if(read(context,first,run,scratch)) {stats.result=KUI_VMU_FS_WITNESS_IO;goto done;}
        stats.sectors+=run;
        for(uint32_t i=0u;i<run;i++) {
            uint8_t digest[32];witness_sector_sha((const uint8_t *)scratch+i*512u,digest);
            if(memcmp(digest,records[at+i].sha256,32u)) {
                stats.bad_lba=first+i;stats.result=KUI_VMU_FS_WITNESS_CHANGED;goto done;
            }
        }
        at+=run;
    }
    /* Callback outputs are contractually disjoint. Recheck the complete
     * pinned manifest before reporting success, including after all I/O. */
    stats.result=kui_vmu_fs_witness_validate(header,records,bytes,b,expected);
    if(!stats.result)stats.bad_lba=UINT32_MAX;
done:
    if(out)*out=stats;
    return stats.result;
}
