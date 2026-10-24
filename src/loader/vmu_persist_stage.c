/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_persist_stage.h"
#include <string.h>

_Static_assert(sizeof(struct kui_vmu_persist_stage_workspace)<=65536u,"High-stage preparation scratch bound");

/* Only this isolated native stage needs the scanner's dot-entry comparison.
 * Ordinary minic and every existing release/observer mode remain unchanged. */
#if defined(__sh__) && (KUI_VMU_READONLY_PROOF == 4 || KUI_VMU_NATIVE_PERSISTENT_PROBE)
int strcmp(const char *left,const char *right) {
    const unsigned char *a=(const unsigned char *)left,*b=(const unsigned char *)right;
    while(*a && *a==*b) {++a;++b;}
    return *a<*b?-1:*a>*b?1:0;
}
#endif

static int vmu_persist_stage_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y?y-x<an:x-y<bn);
}
int kui_vmu_persist_stage_admit(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m) {
    if(kui_vmu_persist_profile_validate(p) || kui_retail_manifest_validate(m)!=KUI_GAME_OK ||
       m->storage_transport!=KUI_STORAGE_SCI ||
#if KUI_VMU_NATIVE_BACKGROUND
       (m->reader!=KUI_RETAIL_READER_ASYNC && m->reader!=KUI_RETAIL_READER_ASYNC_EAGER) ||
#else
       m->reader!=KUI_RETAIL_READER_STANDARD ||
#endif
       (m->flags&(KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED)) ||
       m->card_sectors>p->card_sectors || m->partition_start!=p->partition_first ||
       m->partition_end!=p->partition_end) return KUI_VMU_PERSIST_STAGE_MAPPING;
    for(unsigned role=0;role<3u;role++) for(uint32_t i=0;i<p->files[role].allocated_count;i++) {
        const struct kui_vmu_persist_extent *v=&p->files[role].allocated[i];
        uint64_t vend=(uint64_t)v->lba+v->blocks;
        for(uint32_t j=0;j<m->extent_count;j++) {
            const struct kui_retail_extent *g=&m->slots[m->track_count+j].extent;
            if((uint64_t)v->lba<(uint64_t)g->card_lba+g->blocks && (uint64_t)g->card_lba<vend)
                return KUI_VMU_PERSIST_STAGE_MAPPING;
        }
    }
    return 0;
}
int kui_vmu_persist_stage_handoff_matches(const struct kui_vmu_persist_profile *p,
    const struct kui_vmu_durable_handoff *h) {
    if(kui_vmu_persist_profile_validate(p) || kui_vmu_durable_handoff_validate(h) ||
       h->card_sectors!=p->card_sectors || h->high_capacity!=p->high_capacity ||
       memcmp(h->cid,p->cid,16u) || memcmp(h->image_id,p->image_id,16u) ||
       h->base_count!=p->files[0].useful_count || h->journal_count!=p->files[1].useful_count)
        return KUI_VMU_PERSIST_STAGE_HANDOFF;
    for(unsigned role=0;role<2u;role++) {
        const struct kui_vmu_durable_extent *map=role?h->journal:h->base;
        for(uint32_t i=0;i<p->files[role].useful_count;i++)
            if(map[i].lba!=p->files[role].useful[i].lba || map[i].blocks!=p->files[role].useful[i].blocks)
                return KUI_VMU_PERSIST_STAGE_HANDOFF;
    }
    return 0;
}
static int vmu_persist_stage_final_identity(const struct kui_vmu_persist_profile *p,
    const struct kui_vmu_persist_session_ops *ops) {
    struct kui_vmu_persist_session_identity actual={0};
    if(!ops->healthy(ops->context) || ops->identity(ops->context,&actual) || !ops->healthy(ops->context)) return -1;
    return actual.card_sectors!=p->card_sectors || actual.high_capacity!=p->high_capacity || memcmp(actual.cid,p->cid,16u)?-1:0;
}
static int vmu_persist_stage_fail(struct kui_vmu_persist_stage_workspace *w,
    struct kui_vmu_durable_handoff *out,int result) {
    memset(out,0,sizeof(*out));w->result=result;kui_vmu_persist_session_poison(&w->session);
    return result;
}
int kui_vmu_persist_stage_prepare(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,const struct kui_vmu_persist_session_ops *ops,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    /* A disjoint output is cleared even for invalid ordinary arguments. An
     * aliased output cannot be cleared without damaging a live input/state. */
    if(!out || (w && vmu_persist_stage_overlap(w,sizeof(*w),out,sizeof(*out))) ||
       (p && vmu_persist_stage_overlap(out,sizeof(*out),p,sizeof(*p))) ||
       (m && vmu_persist_stage_overlap(out,sizeof(*out),m,sizeof(*m))) ||
       (ops && vmu_persist_stage_overlap(out,sizeof(*out),ops,sizeof(*ops)))) return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!w || !out || !p || !m || !ops || !ops->identity || !ops->healthy ||
       !ops->read_sector || !ops->write_sector || !ops->sync ||
       vmu_persist_stage_overlap(w,sizeof(*w),p,sizeof(*p)) ||
       vmu_persist_stage_overlap(w,sizeof(*w),m,sizeof(*m)) ||
       vmu_persist_stage_overlap(w,sizeof(*w),ops,sizeof(*ops))) return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    if(w->started) return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    memset(w,0,sizeof(*w));w->started=1u;w->phase=KUI_VMU_PERSIST_STAGE_PREFLIGHT;
    if(kui_vmu_persist_stage_admit(p,m)) return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_MAPPING);
    for(uint32_t i=0;i<m->extent_count;i++) {
        const struct kui_retail_extent *e=&m->slots[m->track_count+i].extent;
        w->protected[i]=(struct kui_vmu_fs_range){e->card_lba,e->blocks};
    }
    struct kui_vmu_fs_input input={.card_sectors=p->card_sectors,.partition_first=p->partition_first,
        .partition_end=p->partition_end,.high_capacity=p->high_capacity,.pair_path=p->pair_path,
        .forbidden=w->protected,.forbidden_count=m->extent_count};
    memcpy(input.cid,p->cid,16u);memcpy(input.image_id,p->image_id,16u);
    w->phase=KUI_VMU_PERSIST_STAGE_RECOVER;
    int result=kui_vmu_persist_session_init(&w->session,p,&input,ops,&w->filesystem,&w->image);
    if(result) return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_SESSION);
    w->phase=KUI_VMU_PERSIST_STAGE_CHECKPOINT;
    if(kui_vmu_persist_session_checkpoint(&w->session)) return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_SESSION);
    w->phase=KUI_VMU_PERSIST_STAGE_SNAPSHOT;
    if(kui_vmu_persist_session_refresh(&w->session,&w->image)) return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_SESSION);
    w->phase=KUI_VMU_PERSIST_STAGE_ZERO_MARKER;
    uint32_t marker;
    if(kui_vmu_persist_profile_map_lba(p,KUI_VMU_PERSIST_JOURNAL,1u,&marker) ||
       vmu_persist_stage_final_identity(p,ops) || ops->read_sector(ops->context,marker,w->session.scratch) ||
       vmu_persist_stage_final_identity(p,ops)) return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_MARKER);
    for(unsigned i=0;i<512u;i++) if(w->session.scratch[i])
        return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_MARKER);
    w->phase=KUI_VMU_PERSIST_STAGE_SEAL;
    out->card_sectors=p->card_sectors;out->high_capacity=p->high_capacity;
    out->base_count=p->files[0].useful_count;out->journal_count=p->files[1].useful_count;
    memcpy(out->cid,p->cid,16u);memcpy(out->image_id,p->image_id,16u);
    for(unsigned role=0;role<2u;role++) {
        struct kui_vmu_durable_extent *map=role?out->journal:out->base;
        for(uint32_t i=0;i<p->files[role].useful_count;i++) {
            map[i].lba=p->files[role].useful[i].lba;map[i].blocks=p->files[role].useful[i].blocks;
        }
    }
    memcpy(out->block_crc,w->session.block_crc,sizeof(out->block_crc));
    memcpy(out->base_sha256,w->session.base_sha256,sizeof(out->base_sha256));
    if(!w->session.snapshot_valid || kui_vmu_durable_handoff_seal(out) ||
       kui_vmu_persist_stage_handoff_matches(p,out)) return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_HANDOFF);
    w->result=0;return 0;
}
