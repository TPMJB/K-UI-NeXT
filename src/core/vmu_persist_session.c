/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_persist_session.h"
#if KUI_SMASH_PHASE_DIAGNOSTIC
#include "kui/smash_phase_trace.h"
extern void kui_smash_phase_stage_mark(uint32_t);
#endif
#include "kui/hash.h"
#include "kui/retail_image.h"
#include <string.h>

_Static_assert(sizeof(struct kui_vmu_persist_session)<=4096u,"Persistent session bounded RAM");
static int vmu_session_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y?y-x<an:x-y<bn);
}
static int vmu_session_fail(struct kui_vmu_persist_session *s,int result) {
    if(!s->faulted) s->last_result=result;
    s->faulted=1u;s->ready=0u;s->store.ready=0u;s->snapshot_valid=0u;
    return s->last_result;
}
static int vmu_session_health(struct kui_vmu_persist_session *s) {
    if(s->faulted) return s->last_result;
    if(s->callback_active) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_BUSY);
    s->callback_active=1u;bool ok=s->ops.healthy(s->ops.context);s->callback_active=0u;
    if(s->faulted) return s->last_result;
    return ok?0:vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_IO);
}
static int vmu_session_identity(struct kui_vmu_persist_session *s) {
    struct kui_vmu_persist_session_identity actual={0};
    if(vmu_session_health(s)) return s->last_result;
    s->callback_active=1u;int result=s->ops.identity(s->ops.context,&actual);s->callback_active=0u;
    if(result) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_IO);
    if(vmu_session_health(s)) return s->last_result;
    if(actual.card_sectors!=s->identity.card_sectors || actual.high_capacity!=s->identity.high_capacity ||
       memcmp(actual.cid,s->identity.cid,16u)) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_IDENTITY);
    return 0;
}
static int vmu_session_raw_read(struct kui_vmu_persist_session *s,uint64_t lba,void *out) {
    if(vmu_session_health(s)) return s->last_result;
    s->callback_active=1u;int result=s->ops.read_sector(s->ops.context,lba,out);s->callback_active=0u;
    if(result) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_IO);
    return vmu_session_health(s);
}
static int vmu_session_fs_read(void *context,uint32_t lba,uint8_t out[512]) {
    struct kui_vmu_persist_session *s=context;
    if(!s || s->self!=s || !s->busy || s->phase!=KUI_VMU_PERSIST_SESSION_ADMISSION ||
       lba>=s->identity.card_sectors || s->callback_active)
        return s?vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_BOUNDS):-1;
    return vmu_session_raw_read(s,lba,out);
}
static int vmu_session_allowed(struct kui_vmu_persist_session *s,uint64_t lba) {
    if(s->self!=s || !s->busy || !s->admitted || s->callback_active ||
       s->phase==KUI_VMU_PERSIST_SESSION_ADMISSION || lba>=s->identity.card_sectors) return 0;
    for(size_t i=0;i<s->image_count+s->journal_count;i++)
        if(lba>=s->allowed[i].first_lba && lba-s->allowed[i].first_lba<s->allowed[i].sectors) return 1;
    return 0;
}
static int vmu_session_bound_read(void *context,uint64_t lba,void *out) {
    struct kui_vmu_persist_session *s=context;
    if(!vmu_session_allowed(s,lba)) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_BOUNDS);
    return vmu_session_raw_read(s,lba,out);
}
static int vmu_session_bound_write(void *context,uint64_t lba,const void *data) {
    struct kui_vmu_persist_session *s=context;
    if(!vmu_session_allowed(s,lba)) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_BOUNDS);
    /* Probe the actual device before EVERY possible recovery/journal/base write. */
    if(vmu_session_identity(s)) return s->last_result;
    s->callback_active=1u;int result=s->ops.write_sector(s->ops.context,lba,data);s->callback_active=0u;
    if(result) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_IO);
    return vmu_session_health(s);
}
static int vmu_session_bound_sync(void *context) {
    struct kui_vmu_persist_session *s=context;
    if(s->self!=s || !s->busy || !s->admitted || s->callback_active ||
       s->phase==KUI_VMU_PERSIST_SESSION_ADMISSION)
        return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_BOUNDS);
    if(vmu_session_identity(s)) return s->last_result;
    s->callback_active=1u;int result=s->ops.sync(s->ops.context);s->callback_active=0u;
    if(result) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_IO);
    return vmu_session_health(s);
}
static uint64_t vmu_session_lba(const struct kui_vmu_store_extent *map,size_t count,unsigned block) {
    for(size_t i=0;i<count;i++) {
        if(block<map[i].sectors) return map[i].first_lba+block;
        block-=map[i].sectors;
    }
    return UINT64_MAX;
}
static int vmu_session_image_read(void *context,uint16_t block,void *out) {
    struct kui_vmu_persist_session *s=context;
    return kui_vmu_store_read(&s->store,block,out);
}
static int vmu_session_refresh(struct kui_vmu_persist_session *s,struct kui_vmu_image_workspace *work) {
    struct kui_vmu_image_io io={.context=s,.read_block=vmu_session_image_read};
    s->snapshot_valid=0u;
    s->image_result=kui_vmu_image_validate(&io,KUI_VMU_IMAGE_BYTES,work,&s->summary);
    if(s->image_result) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_IMAGE);
    struct kui_sha256 hash;kui_sha256_init(&hash);
    for(unsigned block=0;block<256u;block++) {
        s->store_result=kui_vmu_store_read(&s->store,block,s->scratch);
        if(s->store_result) return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_STORE);
        s->block_crc[block]=kui_retail_crc32(0,s->scratch,512u);
        kui_sha256_update(&hash,s->scratch,512u);
    }
    if(vmu_session_identity(s)) return s->last_result;
    kui_sha256_digest(&hash,s->base_sha256);s->snapshot_valid=1u;
    return 0;
}
static int vmu_session_begin(struct kui_vmu_persist_session *s,unsigned phase) {
    if(!s || s->self!=s || !s->started) return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    if(s->busy || s->callback_active) return KUI_VMU_PERSIST_SESSION_BUSY;
    if(s->faulted || !s->ready) return KUI_VMU_PERSIST_SESSION_BLOCKED;
    s->busy=1u;s->phase=phase;
    return 0;
}
static int vmu_session_end(struct kui_vmu_persist_session *s,int result) {
    if(result && !s->faulted) vmu_session_fail(s,result);
    if(!s->faulted) s->last_result=0;
    s->busy=0u;
    return s->faulted?s->last_result:result;
}
int kui_vmu_persist_session_init(struct kui_vmu_persist_session *s,
    const struct kui_vmu_persist_profile *expected,const struct kui_vmu_fs_input *input,
    const struct kui_vmu_persist_session_ops *ops,struct kui_vmu_fs_workspace *fs_work,
    struct kui_vmu_image_workspace *image_work) {
    if(!s || !expected || !input || !ops || !fs_work || !image_work ||
       !ops->identity || !ops->read_sector || !ops->write_sector || !ops->sync || !ops->healthy ||
       vmu_session_overlap(s,sizeof(*s),expected,sizeof(*expected)) ||
       vmu_session_overlap(s,sizeof(*s),input,sizeof(*input)) ||
       vmu_session_overlap(s,sizeof(*s),ops,sizeof(*ops)) ||
       vmu_session_overlap(s,sizeof(*s),fs_work,sizeof(*fs_work)) ||
       vmu_session_overlap(s,sizeof(*s),image_work,sizeof(*image_work)) ||
       vmu_session_overlap(fs_work,sizeof(*fs_work),expected,sizeof(*expected)) ||
       vmu_session_overlap(fs_work,sizeof(*fs_work),input,sizeof(*input)) ||
       vmu_session_overlap(fs_work,sizeof(*fs_work),ops,sizeof(*ops)) ||
       vmu_session_overlap(fs_work,sizeof(*fs_work),image_work,sizeof(*image_work)) ||
       vmu_session_overlap(image_work,sizeof(*image_work),expected,sizeof(*expected)) ||
       vmu_session_overlap(image_work,sizeof(*image_work),input,sizeof(*input)) ||
       vmu_session_overlap(image_work,sizeof(*image_work),ops,sizeof(*ops))) return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    if(s->started) return s->busy?KUI_VMU_PERSIST_SESSION_BUSY:KUI_VMU_PERSIST_SESSION_BLOCKED;
    if(kui_vmu_persist_profile_validate(expected)) return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    if(input->card_sectors!=expected->card_sectors || input->high_capacity!=expected->high_capacity ||
       memcmp(input->cid,expected->cid,16u) || memcmp(input->image_id,expected->image_id,16u))
        return KUI_VMU_PERSIST_SESSION_IDENTITY;
    memset(s,0,sizeof(*s));s->self=s;s->started=1u;s->busy=1u;s->ops=*ops;
    memcpy(s->identity.cid,expected->cid,16u);s->identity.card_sectors=expected->card_sectors;
    s->identity.high_capacity=expected->high_capacity;s->phase=KUI_VMU_PERSIST_SESSION_ADMISSION;
    if(vmu_session_identity(s)) return vmu_session_end(s,s->last_result);
    struct kui_vmu_fs_input fresh=*input;fresh.read_sector=vmu_session_fs_read;fresh.context=s;
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_FS_BEGIN);
#endif
    s->fs_result=kui_vmu_fs_admit(&fresh,expected,fs_work);
    if(s->fs_result) return vmu_session_end(s,KUI_VMU_PERSIST_SESSION_ALLOCATION);
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_FS_DONE);
#endif
    if(vmu_session_identity(s)) return vmu_session_end(s,s->last_result);
    s->image_count=expected->files[0].useful_count;s->journal_count=expected->files[1].useful_count;
    for(unsigned role=0;role<2u;role++) {
        size_t offset=role?s->image_count:0u;
        for(size_t i=0;i<expected->files[role].useful_count;i++) {
            s->allowed[offset+i].first_lba=expected->files[role].useful[i].lba;
            s->allowed[offset+i].sectors=expected->files[role].useful[i].blocks;
        }
    }
    struct kui_vmu_store_config config={.card_sectors=s->identity.card_sectors,
        .image_extents=s->allowed,.image_extent_count=s->image_count,
        .journal_extents=s->allowed+s->image_count,.journal_extent_count=s->journal_count};
    memcpy(config.image_id,expected->image_id,16u);
    struct kui_vmu_store_io io={s,vmu_session_bound_read,vmu_session_bound_write,vmu_session_bound_sync};
    s->admitted=1u;s->phase=KUI_VMU_PERSIST_SESSION_RECOVERY;
    s->store_result=kui_vmu_store_open(&s->store,&io,&config);
    if(s->store_result) return vmu_session_end(s,KUI_VMU_PERSIST_SESSION_STORE);
    if(vmu_session_identity(s)) return vmu_session_end(s,s->last_result);
    s->phase=KUI_VMU_PERSIST_SESSION_VALIDATION;
    int result=vmu_session_refresh(s,image_work);
    if(!result) s->ready=1u;
    return vmu_session_end(s,result);
}
struct vmu_session_witness_scope {
    struct kui_vmu_persist_session *session;
    kui_vmu_fs_witness_read_run read_run;
};
static int vmu_session_witness_read(void *context,uint32_t lba,uint32_t count,uint8_t *out) {
    struct vmu_session_witness_scope *scope=context;
    struct kui_vmu_persist_session *s=scope->session;
    if(s->self!=s || !s->started || !s->busy || s->admitted || s->callback_active ||
       s->phase!=KUI_VMU_PERSIST_SESSION_WITNESS_CHECK || !out || !count ||
       count>KUI_VMU_FS_WITNESS_RUN_BLOCKS || lba>=s->identity.card_sectors ||
       count>s->identity.card_sectors-lba)
        return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_BOUNDS);
    if(vmu_session_health(s))return s->last_result;
    s->callback_active=1u;
    int result=scope->read_run(s->ops.context,lba,count,out);
    s->callback_active=0u;
    if(result)return vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_IO);
    return vmu_session_health(s);
}
int kui_vmu_persist_session_attach(struct kui_vmu_persist_session *s,
    const struct kui_vmu_persist_profile *expected,const struct kui_vmu_persist_attach_source *source,
    const struct kui_vmu_persist_attach_ops *ops,void *scratch,size_t scratch_bytes,
    struct kui_vmu_image_workspace *image_work) {
    if(!s || !expected || !source || !ops || !scratch || !image_work ||
       !ops->io.identity || !ops->io.read_sector || !ops->io.write_sector ||
       !ops->io.sync || !ops->io.healthy || !ops->read_run || !source->header || !source->records ||
       !source->records_bytes || source->records_bytes>KUI_VMU_FS_WITNESS_MAX_RECORDS*KUI_VMU_FS_WITNESS_RECORD_BYTES ||
       source->records_bytes%KUI_VMU_FS_WITNESS_RECORD_BYTES ||
       scratch_bytes<KUI_VMU_FS_WITNESS_SCRATCH_BYTES || (uintptr_t)scratch%32u)
        return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    /* A serialized witness may never supply its own trusted profile, pin or
     * callback descriptor through an aliased source object. Copying a pin
     * below must not turn an untrusted header's digest into live authority. */
    const void *trusted[]={expected,source,ops};
    const size_t trusted_bytes[]={sizeof(*expected),sizeof(*source),sizeof(*ops)};
    for(unsigned i=0u;i<3u;i++)
        if(vmu_session_overlap(trusted[i],trusted_bytes[i],source->header,KUI_VMU_FS_WITNESS_HEADER_BYTES) ||
           vmu_session_overlap(trusted[i],trusted_bytes[i],source->records,source->records_bytes))
            return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    const void *inputs[]={expected,source,ops,source->header,source->records};
    const size_t input_bytes[]={sizeof(*expected),sizeof(*source),sizeof(*ops),
        KUI_VMU_FS_WITNESS_HEADER_BYTES,source->records_bytes};
    void *outputs[]={s,scratch,image_work};
    const size_t output_bytes[]={sizeof(*s),scratch_bytes,sizeof(*image_work)};
    for(unsigned i=0u;i<3u;i++) {
        for(unsigned j=0u;j<5u;j++)
            if(vmu_session_overlap(outputs[i],output_bytes[i],inputs[j],input_bytes[j]))
                return KUI_VMU_PERSIST_SESSION_ARGUMENT;
        for(unsigned j=0u;j<i;j++)
            if(vmu_session_overlap(outputs[i],output_bytes[i],outputs[j],output_bytes[j]))
                return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    }
    if(s->started)return s->busy?KUI_VMU_PERSIST_SESSION_BUSY:KUI_VMU_PERSIST_SESSION_BLOCKED;
    if(kui_vmu_persist_profile_validate(expected))return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    /* Copy the trusted receipt fields and coherent callback bundle before
     * entering any callback. This invocation owns the sole local attestation;
     * a persisted header, old success value or caller flag cannot mint it. */
    struct kui_vmu_persist_attach_source pinned=*source;
    struct kui_vmu_persist_attach_ops bound=*ops;
    uint8_t ownership[32];
    if(kui_vmu_persist_profile_ownership_sha256(expected,ownership) ||
       memcmp(ownership,pinned.ownership_sha256,32u) || !pinned.expected_binding.generation ||
       pinned.expected_binding.card_sectors!=expected->card_sectors ||
       pinned.expected_binding.partition_first!=expected->partition_first ||
       pinned.expected_binding.partition_end!=expected->partition_end ||
       pinned.expected_binding.high_capacity!=expected->high_capacity ||
       memcmp(pinned.expected_binding.cid,expected->cid,16u) ||
       memcmp(pinned.expected_binding.image_id,expected->image_id,16u))
        return KUI_VMU_PERSIST_SESSION_WITNESS;
    memset(s,0,sizeof(*s));s->self=s;s->started=1u;s->busy=1u;s->ops=bound.io;
    memcpy(s->identity.cid,expected->cid,16u);s->identity.card_sectors=expected->card_sectors;
    s->identity.high_capacity=expected->high_capacity;s->phase=KUI_VMU_PERSIST_SESSION_WITNESS_CHECK;
    if(vmu_session_identity(s))return vmu_session_end(s,s->last_result);
    struct vmu_session_witness_scope scope={s,bound.read_run};
    struct kui_vmu_fs_witness_stats stats={0};
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_FS_BEGIN);
#endif
    s->fs_result=kui_vmu_fs_witness_replay(pinned.header,pinned.records,pinned.records_bytes,
        &pinned.expected_binding,pinned.expected_manifest_sha256,vmu_session_witness_read,
        &scope,scratch,scratch_bytes,&stats);
    if(s->fs_result)return vmu_session_end(s,KUI_VMU_PERSIST_SESSION_WITNESS);
    if(vmu_session_identity(s))return vmu_session_end(s,s->last_result);
    if(kui_vmu_persist_profile_ownership_sha256(expected,ownership) ||
       memcmp(ownership,pinned.ownership_sha256,32u))
        return vmu_session_end(s,KUI_VMU_PERSIST_SESSION_WITNESS);
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_FS_DONE);
#endif
    /* Only fresh, complete witness success on this exact exclusive device
     * reaches this boundary. No attestation survives the transition. */
    s->image_count=expected->files[0].useful_count;s->journal_count=expected->files[1].useful_count;
    for(unsigned role=0u;role<2u;role++) {
        size_t offset=role?s->image_count:0u;
        for(size_t i=0u;i<expected->files[role].useful_count;i++) {
            s->allowed[offset+i].first_lba=expected->files[role].useful[i].lba;
            s->allowed[offset+i].sectors=expected->files[role].useful[i].blocks;
        }
    }
    struct kui_vmu_store_config config={.card_sectors=s->identity.card_sectors,
        .image_extents=s->allowed,.image_extent_count=s->image_count,
        .journal_extents=s->allowed+s->image_count,.journal_extent_count=s->journal_count};
    memcpy(config.image_id,expected->image_id,16u);
    struct kui_vmu_store_io io={s,vmu_session_bound_read,vmu_session_bound_write,vmu_session_bound_sync};
    s->admitted=1u;s->phase=KUI_VMU_PERSIST_SESSION_RECOVERY;
    s->store_result=kui_vmu_store_open(&s->store,&io,&config);
    if(s->store_result)return vmu_session_end(s,KUI_VMU_PERSIST_SESSION_STORE);
    if(vmu_session_identity(s))return vmu_session_end(s,s->last_result);
    s->phase=KUI_VMU_PERSIST_SESSION_VALIDATION;
    int result=vmu_session_refresh(s,image_work);
    if(!result && (kui_vmu_persist_profile_ownership_sha256(expected,ownership) ||
       memcmp(ownership,pinned.ownership_sha256,32u)))result=KUI_VMU_PERSIST_SESSION_WITNESS;
    if(!result)s->ready=1u;
    return vmu_session_end(s,result);
}
struct vmu_session_rebind_scope {
    const struct kui_vmu_persist_profile *profile;
    struct kui_vmu_persist_allocation_transfer *transfer;
    const struct kui_vmu_persist_allocation_transfer *pinned;
    struct kui_vmu_persist_session_ops io;
};
static int vmu_session_rebind_nonzero(const uint8_t *p,size_t bytes) {
    unsigned any=0u;for(size_t i=0u;i<bytes;i++)any|=p[i];return any!=0u;
}
static int vmu_session_rebind_role(uint32_t previous,uint32_t next) {
    return (previous==1u && next>=2u && next<=4u) || (previous==2u && next==1u);
}
static int vmu_session_rebind_header(const struct kui_vmu_persist_allocation_transfer *t) {
    return t->magic==KUI_VMU_ALLOCATION_TRANSFER_MAGIC &&
        t->version==KUI_VMU_ALLOCATION_TRANSFER_VERSION && t->bytes==sizeof(*t) &&
        t->state==KUI_VMU_ALLOCATION_TRANSFER_READY && !t->reserved &&
        t->generation && t->sequence>1u && vmu_session_rebind_role(t->previous_owner,t->next_owner) &&
        vmu_session_rebind_nonzero(t->domain_binding_sha256,32u);
}
static int vmu_session_rebind_guard(struct vmu_session_rebind_scope *scope,int profile) {
    const struct kui_vmu_persist_allocation_transfer *t=scope->transfer,*p=scope->pinned;
    if(t->state!=KUI_VMU_ALLOCATION_TRANSFER_BUSY ||
       memcmp(t,p,offsetof(struct kui_vmu_persist_allocation_transfer,state)) ||
       memcmp((const uint8_t *)t+16u,(const uint8_t *)p+16u,sizeof(*t)-16u))return 0;
    if(profile) {
        uint8_t current[32];
        if(kui_vmu_persist_profile_ownership_sha256(scope->profile,current) ||
           memcmp(current,p->ownership_sha256,32u))return 0;
    }
    return 1;
}
static int vmu_session_rebind_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    struct vmu_session_rebind_scope *scope=context;
    if(!vmu_session_rebind_guard(scope,1))return -1;
    int result=scope->io.identity(scope->io.context,out);
    return vmu_session_rebind_guard(scope,1)?result:-1;
}
static int vmu_session_rebind_read(void *context,uint64_t lba,void *out) {
    struct vmu_session_rebind_scope *scope=context;
    if(!vmu_session_rebind_guard(scope,0))return -1;
    int result=scope->io.read_sector(scope->io.context,lba,out);
    return vmu_session_rebind_guard(scope,0)?result:-1;
}
static int vmu_session_rebind_write(void *context,uint64_t lba,const void *data) {
    struct vmu_session_rebind_scope *scope=context;
    if(!vmu_session_rebind_guard(scope,1))return -1;
    int result=scope->io.write_sector(scope->io.context,lba,data);
    return vmu_session_rebind_guard(scope,1)?result:-1;
}
static int vmu_session_rebind_sync(void *context) {
    struct vmu_session_rebind_scope *scope=context;
    if(!vmu_session_rebind_guard(scope,1))return -1;
    int result=scope->io.sync(scope->io.context);
    return vmu_session_rebind_guard(scope,1)?result:-1;
}
static bool vmu_session_rebind_health(void *context) {
    struct vmu_session_rebind_scope *scope=context;
    if(!vmu_session_rebind_guard(scope,0))return false;
    bool result=scope->io.healthy(scope->io.context);
    return vmu_session_rebind_guard(scope,0) && result;
}
int kui_vmu_persist_session_rebind(struct kui_vmu_persist_session *s,
    const struct kui_vmu_persist_profile *expected,struct kui_vmu_persist_allocation_transfer *transfer,
    const struct kui_vmu_persist_session_ops *ops,struct kui_vmu_image_workspace *image_work) {
    if(!s || !expected || !transfer || !ops || !image_work || !ops->identity ||
       !ops->read_sector || !ops->write_sector || !ops->sync || !ops->healthy)
        return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    const void *inputs[]={expected,ops};
    const size_t input_bytes[]={sizeof(*expected),sizeof(*ops)};
    void *outputs[]={s,transfer,image_work};
    const size_t output_bytes[]={sizeof(*s),sizeof(*transfer),sizeof(*image_work)};
    for(unsigned i=0u;i<3u;i++) {
        for(unsigned j=0u;j<2u;j++)
            if(vmu_session_overlap(outputs[i],output_bytes[i],inputs[j],input_bytes[j]))
                return KUI_VMU_PERSIST_SESSION_ARGUMENT;
        for(unsigned j=0u;j<i;j++)
            if(vmu_session_overlap(outputs[i],output_bytes[i],outputs[j],output_bytes[j]))
                return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    }
    if(s->started)return s->busy?KUI_VMU_PERSIST_SESSION_BUSY:KUI_VMU_PERSIST_SESSION_BLOCKED;
    if(!vmu_session_rebind_header(transfer))return KUI_VMU_PERSIST_SESSION_ALLOCATION;
    struct kui_vmu_persist_allocation_transfer pinned=*transfer;
    struct vmu_session_rebind_scope scope={expected,transfer,&pinned,*ops};
    uint8_t ownership[32];
    if(kui_vmu_persist_profile_ownership_sha256(expected,ownership) ||
       memcmp(ownership,pinned.ownership_sha256,32u) ||
       pinned.card_sectors!=expected->card_sectors || pinned.high_capacity!=expected->high_capacity ||
       pinned.partition_first!=expected->partition_first || pinned.partition_end!=expected->partition_end ||
       memcmp(pinned.cid,expected->cid,16u) || memcmp(pinned.image_id,expected->image_id,16u))
        return KUI_VMU_PERSIST_SESSION_ALLOCATION;
    transfer->state=KUI_VMU_ALLOCATION_TRANSFER_BUSY;
    memset(s,0,sizeof(*s));s->self=s;s->started=1u;s->busy=1u;
    s->ops=(struct kui_vmu_persist_session_ops){&scope,vmu_session_rebind_identity,
        vmu_session_rebind_read,vmu_session_rebind_write,vmu_session_rebind_sync,vmu_session_rebind_health};
    memcpy(s->identity.cid,pinned.cid,16u);s->identity.card_sectors=pinned.card_sectors;
    s->identity.high_capacity=pinned.high_capacity;s->phase=KUI_VMU_PERSIST_SESSION_REBIND;
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_FS_BEGIN);
#endif
    int result=vmu_session_identity(s);
    if(result)goto done;
    s->image_count=expected->files[0].useful_count;s->journal_count=expected->files[1].useful_count;
    for(unsigned role=0u;role<2u;role++) {
        size_t offset=role?s->image_count:0u;
        for(size_t i=0u;i<expected->files[role].useful_count;i++) {
            s->allowed[offset+i].first_lba=expected->files[role].useful[i].lba;
            s->allowed[offset+i].sectors=expected->files[role].useful[i].blocks;
        }
    }
#if KUI_SMASH_PHASE_DIAGNOSTIC
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_FS_DONE);
#endif
    struct kui_vmu_store_config config={.card_sectors=s->identity.card_sectors,
        .image_extents=s->allowed,.image_extent_count=s->image_count,
        .journal_extents=s->allowed+s->image_count,.journal_extent_count=s->journal_count};
    memcpy(config.image_id,pinned.image_id,16u);
    const struct kui_vmu_store_io io={s,vmu_session_bound_read,vmu_session_bound_write,vmu_session_bound_sync};
    s->admitted=1u;s->phase=KUI_VMU_PERSIST_SESSION_RECOVERY;
    s->store_result=kui_vmu_store_open(&s->store,&io,&config);
    if(s->store_result) {result=KUI_VMU_PERSIST_SESSION_STORE;goto done;}
    result=vmu_session_identity(s);if(result)goto done;
    s->phase=KUI_VMU_PERSIST_SESSION_VALIDATION;
    result=vmu_session_refresh(s,image_work);
    if(!result && !vmu_session_rebind_guard(&scope,1))result=KUI_VMU_PERSIST_SESSION_ALLOCATION;
    if(!result)s->ready=1u;
done:
    if(!vmu_session_rebind_guard(&scope,1) && !result)result=KUI_VMU_PERSIST_SESSION_ALLOCATION;
    /* No stack guard or borrowed profile/descriptor survives this call. */
    s->ops=scope.io;
    result=vmu_session_end(s,result);
    transfer->state=result?KUI_VMU_ALLOCATION_TRANSFER_POISONED:KUI_VMU_ALLOCATION_TRANSFER_CONSUMED;
    return result;
}
int kui_vmu_persist_session_read(struct kui_vmu_persist_session *s,unsigned block,void *out) {
    if(!s || !out || block>=256u || vmu_session_overlap(s,sizeof(*s),out,512u))
        return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    int result=vmu_session_begin(s,KUI_VMU_PERSIST_SESSION_READ);
    if(result) return result;
    if(vmu_session_identity(s)) return vmu_session_end(s,s->last_result);
    s->store_result=kui_vmu_store_read(&s->store,block,s->scratch);
    if(s->store_result) result=KUI_VMU_PERSIST_SESSION_STORE;
    else if(kui_retail_crc32(0,s->scratch,512u)!=s->block_crc[block]) result=KUI_VMU_PERSIST_SESSION_VERIFY;
    else result=vmu_session_identity(s);
    if(!result) memcpy(out,s->scratch,512u);
    return vmu_session_end(s,result);
}
int kui_vmu_persist_session_commit(struct kui_vmu_persist_session *s,unsigned block,const void *data) {
    if(!s || !data || block>=256u || vmu_session_overlap(s,sizeof(*s),data,512u))
        return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    int result=vmu_session_begin(s,KUI_VMU_PERSIST_SESSION_COMMIT);
    if(result) return result;
    if(vmu_session_identity(s)) return vmu_session_end(s,s->last_result);
    s->snapshot_valid=0u;
    s->store_result=kui_vmu_store_write(&s->store,block,data);
    if(s->store_result) result=KUI_VMU_PERSIST_SESSION_STORE;
    else {
        s->store_result=kui_vmu_store_read(&s->store,block,s->scratch);
        if(s->store_result) result=KUI_VMU_PERSIST_SESSION_STORE;
        else if(memcmp(s->scratch,s->store.data,512u)) result=KUI_VMU_PERSIST_SESSION_VERIFY;
        else result=vmu_session_identity(s);
    }
    if(!result) s->block_crc[block]=kui_retail_crc32(0,s->scratch,512u);
    return vmu_session_end(s,result);
}
int kui_vmu_persist_session_checkpoint(struct kui_vmu_persist_session *s) {
    int result=vmu_session_begin(s,KUI_VMU_PERSIST_SESSION_CHECKPOINT);
    if(result) return result;
    if(vmu_session_identity(s)) return vmu_session_end(s,s->last_result);
    s->store_result=kui_vmu_store_checkpoint(&s->store);
    if(s->store_result) result=KUI_VMU_PERSIST_SESSION_STORE;
    else {
        uint64_t marker=vmu_session_lba(s->allowed+s->image_count,s->journal_count,1u);
        result=vmu_session_bound_read(s,marker,s->scratch);
        if(!result) for(unsigned i=0;i<512u;i++) if(s->scratch[i]) {result=KUI_VMU_PERSIST_SESSION_VERIFY;break;}
        if(!result) result=vmu_session_identity(s);
    }
    return vmu_session_end(s,result);
}
int kui_vmu_persist_session_refresh(struct kui_vmu_persist_session *s,struct kui_vmu_image_workspace *work) {
    if(!s || !work || vmu_session_overlap(s,sizeof(*s),work,sizeof(*work)))
        return KUI_VMU_PERSIST_SESSION_ARGUMENT;
    int result=vmu_session_begin(s,KUI_VMU_PERSIST_SESSION_VALIDATION);
    if(result) return result;
    if(vmu_session_identity(s)) return vmu_session_end(s,s->last_result);
    return vmu_session_end(s,vmu_session_refresh(s,work));
}
void kui_vmu_persist_session_poison(struct kui_vmu_persist_session *s) {
    if(s && s->self==s && s->started) (void)vmu_session_fail(s,KUI_VMU_PERSIST_SESSION_IO);
}
