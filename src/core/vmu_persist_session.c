/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_persist_session.h"
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
    s->fs_result=kui_vmu_fs_admit(&fresh,expected,fs_work);
    if(s->fs_result) return vmu_session_end(s,KUI_VMU_PERSIST_SESSION_ALLOCATION);
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
