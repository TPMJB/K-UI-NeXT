/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_persist_stage.h"
#if KUI_SMASH_PHASE_DIAGNOSTIC
#include "kui/smash_phase_trace.h"
extern void kui_smash_phase_stage_mark(uint32_t);
#endif
#include <string.h>

_Static_assert(sizeof(struct kui_vmu_persist_stage_workspace)<=65536u,"High-stage preparation scratch bound");
const struct kui_vmu_persist_stage_policy kui_vmu_persist_stage_exclusive_checkpoint={
    KUI_VMU_PERSIST_STAGE_POLICY_VERSION,sizeof(struct kui_vmu_persist_stage_policy),
    KUI_VMU_PERSIST_STAGE_CHECKPOINT_SNAPSHOT,0u};

static bool vmu_persist_stage_policy_valid(const struct kui_vmu_persist_stage_policy *p) {
    return !p || (p->version==KUI_VMU_PERSIST_STAGE_POLICY_VERSION && p->bytes==sizeof(*p) &&
        !(p->flags&~KUI_VMU_PERSIST_STAGE_CHECKPOINT_SNAPSHOT) && !p->reserved);
}
static bool vmu_persist_stage_snapshot_policy(const struct kui_vmu_persist_stage_policy *p) {
    return p && (p->flags&KUI_VMU_PERSIST_STAGE_CHECKPOINT_SNAPSHOT);
}

/* Only this isolated native stage needs the scanner's dot-entry comparison.
 * Ordinary minic and every existing release/observer mode remain unchanged. */
#if defined(__sh__) && (KUI_VMU_READONLY_PROOF == 4 || KUI_VMU_NATIVE_PERSISTENT_PROBE || KUI_VC2_VMU_PERSIST || KUI_SMASH_CARD_SESSION)
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
#if KUI_NATIVE_VMU_ONLY
       (m->storage_transport!=KUI_STORAGE_SCI && m->storage_transport!=KUI_STORAGE_SCIF) ||
#else
       m->storage_transport!=KUI_STORAGE_SCI ||
#endif
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
int kui_vmu_persist_disc_stage_admit(const struct kui_vmu_persist_profile *p,
    uint32_t transport) {
    return kui_vmu_persist_profile_validate(p) ||
        (transport!=KUI_STORAGE_SCI && transport!=KUI_STORAGE_SCIF)?
        KUI_VMU_PERSIST_STAGE_MAPPING:0;
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
static int vmu_persist_stage_finish(const struct kui_vmu_persist_profile *,
    const struct kui_vmu_persist_session_ops *,struct kui_vmu_persist_stage_workspace *,
    struct kui_vmu_durable_handoff *,bool);
static int vmu_persist_stage_prepare(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,uint32_t transport,bool physical_disc,
    const struct kui_vmu_persist_session_ops *ops,
    const struct kui_vmu_persist_stage_policy *policy,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    /* A disjoint output is cleared even for invalid ordinary arguments. An
     * aliased output cannot be cleared without damaging a live input/state. */
    if(!out || (w && vmu_persist_stage_overlap(w,sizeof(*w),out,sizeof(*out))) ||
       (p && vmu_persist_stage_overlap(out,sizeof(*out),p,sizeof(*p))) ||
       (m && vmu_persist_stage_overlap(out,sizeof(*out),m,sizeof(*m))) ||
       (ops && vmu_persist_stage_overlap(out,sizeof(*out),ops,sizeof(*ops))) ||
       (policy && vmu_persist_stage_overlap(out,sizeof(*out),policy,sizeof(*policy)))) return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!w || !out || !p || (!m && !physical_disc) || !ops || !ops->identity || !ops->healthy ||
       !ops->read_sector || !ops->write_sector || !ops->sync || !vmu_persist_stage_policy_valid(policy) ||
       vmu_persist_stage_overlap(w,sizeof(*w),p,sizeof(*p)) ||
       (m && vmu_persist_stage_overlap(w,sizeof(*w),m,sizeof(*m))) ||
       vmu_persist_stage_overlap(w,sizeof(*w),ops,sizeof(*ops)) ||
       (policy && vmu_persist_stage_overlap(w,sizeof(*w),policy,sizeof(*policy)))) return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    if(w->started) return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    const bool reuse_snapshot=vmu_persist_stage_snapshot_policy(policy);
    memset(w,0,sizeof(*w));w->started=1u;w->phase=KUI_VMU_PERSIST_STAGE_PREFLIGHT;
    if(physical_disc?kui_vmu_persist_disc_stage_admit(p,transport):kui_vmu_persist_stage_admit(p,m))
        return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_MAPPING);
    uint32_t protected_count=m?m->extent_count:0u;
    for(uint32_t i=0;i<protected_count;i++) {
        const struct kui_retail_extent *e=&m->slots[m->track_count+i].extent;
        w->protected[i]=(struct kui_vmu_fs_range){e->card_lba,e->blocks};
    }
    struct kui_vmu_fs_input input={.card_sectors=p->card_sectors,.partition_first=p->partition_first,
        .partition_end=p->partition_end,.high_capacity=p->high_capacity,.pair_path=p->pair_path,
        .forbidden=protected_count?w->protected:NULL,.forbidden_count=protected_count};
    memcpy(input.cid,p->cid,16u);memcpy(input.image_id,p->image_id,16u);
    w->phase=KUI_VMU_PERSIST_STAGE_RECOVER;
    int result=kui_vmu_persist_session_init(&w->session,p,&input,ops,&w->filesystem,&w->image);
    if(result) return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_SESSION);
    return vmu_persist_stage_finish(p,ops,w,out,reuse_snapshot);
}
static int vmu_persist_stage_finish(const struct kui_vmu_persist_profile *p,
    const struct kui_vmu_persist_session_ops *ops,struct kui_vmu_persist_stage_workspace *w,
    struct kui_vmu_durable_handoff *out,bool reuse_snapshot) {
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_SESSION_DONE);
#endif
    /* Only a live snapshot established by this new session can cross the
     * checked marker-only checkpoint. Commits and faults clear validity;
     * invalid snapshots and unknown backend contracts take the full refresh.
     * The local policy was copied before any callback, never retained. */
    const bool fresh_snapshot=reuse_snapshot && w->session.snapshot_valid;
    w->phase=KUI_VMU_PERSIST_STAGE_CHECKPOINT;
    if(kui_vmu_persist_session_checkpoint(&w->session)) return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_SESSION);
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_CHECKPOINT_DONE);
#endif
    w->phase=KUI_VMU_PERSIST_STAGE_SNAPSHOT;
    if((!fresh_snapshot || !w->session.snapshot_valid) &&
       kui_vmu_persist_session_refresh(&w->session,&w->image))
        return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_SESSION);
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_REFRESH_DONE);
#endif
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
int kui_vmu_persist_stage_attach_policy(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,const struct kui_vmu_persist_attach_source *source,
    const struct kui_vmu_persist_attach_ops *ops,
    const struct kui_vmu_persist_stage_policy *policy,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    if(!out)return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    const void *inputs[]={p,m,source,ops,source?source->header:NULL,source?source->records:NULL,policy};
    const size_t input_bytes[]={sizeof(*p),sizeof(*m),sizeof(*source),sizeof(*ops),
        KUI_VMU_FS_WITNESS_HEADER_BYTES,source?source->records_bytes:0u,sizeof(*policy)};
    if(w && vmu_persist_stage_overlap(w,sizeof(*w),out,sizeof(*out)))
        return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    for(unsigned i=0u;i<7u;i++)if(inputs[i] &&
       vmu_persist_stage_overlap(out,sizeof(*out),inputs[i],input_bytes[i]))
        return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!p || !m || !source || !ops || !w || !vmu_persist_stage_policy_valid(policy))return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    for(unsigned i=0u;i<7u;i++)if(inputs[i] &&
       vmu_persist_stage_overlap(w,sizeof(*w),inputs[i],input_bytes[i]))
        return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    if(w->started)return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    const bool reuse_snapshot=vmu_persist_stage_snapshot_policy(policy);
    uint8_t ownership[32];memcpy(ownership,source->ownership_sha256,32u);
    memset(w,0,sizeof(*w));w->started=1u;w->phase=KUI_VMU_PERSIST_STAGE_PREFLIGHT;
    if(kui_vmu_persist_stage_admit(p,m))
        return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_MAPPING);
    uintptr_t begin=(uintptr_t)w->filesystem.private_work.bytes;
    uintptr_t aligned=(begin+31u)&~(uintptr_t)31u;
    size_t scratch_bytes=sizeof(w->filesystem.private_work.bytes)-(size_t)(aligned-begin);
    w->phase=KUI_VMU_PERSIST_STAGE_RECOVER;
    int result=kui_vmu_persist_session_attach(&w->session,p,source,ops,
        (void *)aligned,scratch_bytes,&w->image);
    if(result)return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_SESSION);
    result=vmu_persist_stage_finish(p,&w->session.ops,w,out,reuse_snapshot);
    if(result)return result;
    uint8_t current[32];
    if(kui_vmu_persist_profile_ownership_sha256(p,current) || memcmp(current,ownership,32u))
        return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_HANDOFF);
    return 0;
}
struct vmu_persist_rebind_finish_scope {
    const struct kui_vmu_persist_profile *live,*pinned;
    struct kui_vmu_persist_allocation_transfer *transfer;
    const struct kui_vmu_persist_allocation_transfer *transfer_pin;
    struct kui_vmu_persist_session_ops io;
    struct kui_vmu_persist_stage_workspace *work;
};
static int vmu_persist_rebind_finish_guard(struct vmu_persist_rebind_finish_scope *s,int profile) {
    const struct kui_vmu_persist_allocation_transfer *t=s->transfer,*p=s->transfer_pin;
    if(t->state!=KUI_VMU_ALLOCATION_TRANSFER_CONSUMED ||
       memcmp(t,p,offsetof(struct kui_vmu_persist_allocation_transfer,state)) ||
       memcmp((const uint8_t *)t+16u,(const uint8_t *)p+16u,sizeof(*t)-16u))return 0;
    /* All ownership fields precede provenance. The private snapshot prevents
     * late callback count/map edits from being dereferenced by finish. This
     * bounded RAM comparison also refuses edits before every possible write;
     * it does not perform another filesystem or large SHA scan. */
    return !profile || !memcmp(s->live,s->pinned,offsetof(struct kui_vmu_persist_profile,base_sha256));
}
static int vmu_persist_rebind_finish_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    struct vmu_persist_rebind_finish_scope *s=context;
    if(!vmu_persist_rebind_finish_guard(s,1))return -1;
    int result=s->io.identity(s->io.context,out);
    return vmu_persist_rebind_finish_guard(s,1)?result:-1;
}
static int vmu_persist_rebind_finish_read(void *context,uint64_t lba,void *out) {
    struct vmu_persist_rebind_finish_scope *s=context;
    if(!vmu_persist_rebind_finish_guard(s,0))return -1;
    int result=s->io.read_sector(s->io.context,lba,out);
    return vmu_persist_rebind_finish_guard(s,0)?result:-1;
}
static int vmu_persist_rebind_finish_write(void *context,uint64_t lba,const void *data) {
    struct vmu_persist_rebind_finish_scope *s=context;
    if(!vmu_persist_rebind_finish_guard(s,1))return -1;
    int result=s->io.write_sector(s->io.context,lba,data);
    return vmu_persist_rebind_finish_guard(s,1)?result:-1;
}
static int vmu_persist_rebind_finish_sync(void *context) {
    struct vmu_persist_rebind_finish_scope *s=context;
    if(!vmu_persist_rebind_finish_guard(s,1))return -1;
    int result=s->io.sync(s->io.context);
    return vmu_persist_rebind_finish_guard(s,1)?result:-1;
}
static bool vmu_persist_rebind_finish_health(void *context) {
    struct vmu_persist_rebind_finish_scope *s=context;
    int profile=s->work->phase==KUI_VMU_PERSIST_STAGE_ZERO_MARKER;
    if(!vmu_persist_rebind_finish_guard(s,profile))return false;
    bool result=s->io.healthy(s->io.context);
    return vmu_persist_rebind_finish_guard(s,profile) && result;
}
int kui_vmu_persist_stage_rebind_policy(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,struct kui_vmu_persist_allocation_transfer *transfer,
    const struct kui_vmu_persist_session_ops *ops,
    const struct kui_vmu_persist_stage_policy *policy,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    if(!out)return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    const void *inputs[]={p,m,transfer,ops,policy};
    const size_t input_bytes[]={sizeof(*p),sizeof(*m),sizeof(*transfer),sizeof(*ops),sizeof(*policy)};
    if(w && vmu_persist_stage_overlap(w,sizeof(*w),out,sizeof(*out)))
        return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    for(unsigned i=0u;i<5u;i++)if(inputs[i] &&
       vmu_persist_stage_overlap(out,sizeof(*out),inputs[i],input_bytes[i]))
        return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!p || !m || !transfer || !ops || !w || !ops->identity || !ops->healthy ||
       !ops->read_sector || !ops->write_sector || !ops->sync || !vmu_persist_stage_policy_valid(policy))return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    for(unsigned i=0u;i<5u;i++) {
        if(!inputs[i])continue;
        if(vmu_persist_stage_overlap(w,sizeof(*w),inputs[i],input_bytes[i]))
            return KUI_VMU_PERSIST_STAGE_ARGUMENT;
        if(i!=2u && vmu_persist_stage_overlap(transfer,sizeof(*transfer),inputs[i],input_bytes[i]))
            return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    }
    if(w->started)return KUI_VMU_PERSIST_STAGE_ARGUMENT;
    const bool reuse_snapshot=vmu_persist_stage_snapshot_policy(policy);
    struct kui_vmu_persist_allocation_transfer pinned=*transfer;
    memset(w,0,sizeof(*w));w->started=1u;w->phase=KUI_VMU_PERSIST_STAGE_PREFLIGHT;
    if(kui_vmu_persist_stage_admit(p,m))
        return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_MAPPING);
    /* Rebind has no filesystem scanner. Borrow its existing aligned scratch
     * for a private validated profile, not additional retained RAM/stack. */
    _Static_assert(sizeof(struct kui_vmu_persist_profile)<=KUI_VMU_FS_WORK_BYTES,"Rebind profile scratch");
    struct kui_vmu_persist_profile *profile=(void *)w->filesystem.private_work.bytes;
    memcpy(profile,p,sizeof(*profile));
    w->phase=KUI_VMU_PERSIST_STAGE_RECOVER;
    int result=kui_vmu_persist_session_rebind(&w->session,p,transfer,ops,&w->image);
    if(result)return vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_SESSION);
    struct vmu_persist_rebind_finish_scope scope={p,profile,transfer,&pinned,w->session.ops,w};
    w->session.ops=(struct kui_vmu_persist_session_ops){&scope,vmu_persist_rebind_finish_identity,
        vmu_persist_rebind_finish_read,vmu_persist_rebind_finish_write,vmu_persist_rebind_finish_sync,
        vmu_persist_rebind_finish_health};
    result=vmu_persist_stage_finish(profile,&w->session.ops,w,out,reuse_snapshot);
    if(!result && !vmu_persist_rebind_finish_guard(&scope,1))
        result=vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_HANDOFF);
    w->session.ops=scope.io;
    uint8_t current[32];
    if(!result && (transfer->state!=KUI_VMU_ALLOCATION_TRANSFER_CONSUMED ||
       memcmp(transfer,&pinned,offsetof(struct kui_vmu_persist_allocation_transfer,state)) ||
       memcmp((const uint8_t *)transfer+16u,(const uint8_t *)&pinned+16u,sizeof(pinned)-16u) ||
       kui_vmu_persist_profile_ownership_sha256(p,current) || memcmp(current,pinned.ownership_sha256,32u)))
        result=vmu_persist_stage_fail(w,out,KUI_VMU_PERSIST_STAGE_HANDOFF);
    if(result)transfer->state=KUI_VMU_ALLOCATION_TRANSFER_POISONED;
    return result;
}
int kui_vmu_persist_stage_prepare(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,const struct kui_vmu_persist_session_ops *ops,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    return vmu_persist_stage_prepare(p,m,0u,false,ops,NULL,w,out);
}
int kui_vmu_persist_disc_stage_prepare(const struct kui_vmu_persist_profile *p,
    uint32_t transport,const struct kui_vmu_persist_session_ops *ops,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    return vmu_persist_stage_prepare(p,NULL,transport,true,ops,NULL,w,out);
}
int kui_vmu_persist_stage_prepare_policy(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,const struct kui_vmu_persist_session_ops *ops,
    const struct kui_vmu_persist_stage_policy *policy,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    return vmu_persist_stage_prepare(p,m,0u,false,ops,policy,w,out);
}
int kui_vmu_persist_disc_stage_prepare_policy(const struct kui_vmu_persist_profile *p,
    uint32_t transport,const struct kui_vmu_persist_session_ops *ops,
    const struct kui_vmu_persist_stage_policy *policy,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    return vmu_persist_stage_prepare(p,NULL,transport,true,ops,policy,w,out);
}
int kui_vmu_persist_stage_attach(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,const struct kui_vmu_persist_attach_source *source,
    const struct kui_vmu_persist_attach_ops *ops,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    return kui_vmu_persist_stage_attach_policy(p,m,source,ops,NULL,w,out);
}
int kui_vmu_persist_stage_rebind(const struct kui_vmu_persist_profile *p,
    const struct kui_retail_manifest *m,struct kui_vmu_persist_allocation_transfer *transfer,
    const struct kui_vmu_persist_session_ops *ops,
    struct kui_vmu_persist_stage_workspace *w,struct kui_vmu_durable_handoff *out) {
    return kui_vmu_persist_stage_rebind_policy(p,m,transfer,ops,NULL,w,out);
}
