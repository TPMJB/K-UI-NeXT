/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_durable.h"
#include "kui/vmu_backing.h"
#include "kui/retail_image.h"
#include "../loader/sd_reader.h"
#include "../dreamcast/sci_sd_storage.h"
#include <string.h>

_Static_assert(offsetof(struct kui_vmu_durable_handoff,seal)==1268u,"Durable seal offset");
_Static_assert(sizeof(void *)!=4u || offsetof(struct kui_loader_sd,bus.transfer_block)==24u,
    "Durable audited native block callback offset");
enum {DURABLE_READ=1,DURABLE_WRITE=2};
struct durable_card_scope {
    struct kui_vmu_durable *owner;
    struct kui_loader_sd card;
    unsigned binding;
};
static int durable_card_zero(const void *bytes,size_t count) {
    const uint8_t *p=bytes;for(size_t i=0;i<count;i++) if(p[i]) return 0;return 1;
}
static int durable_card_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && (x<=y?y-x<an:x-y<bn);
}
static int durable_card_geometry(const struct kui_vmu_durable_handoff *h) {
    if(!h || !h->card_sectors || h->card_sectors>(UINT64_C(1)<<32) || h->high_capacity>1u ||
       (!h->high_capacity && h->card_sectors>(UINT64_C(1)<<23)) ||
       !h->base_count || h->base_count>16u || !h->journal_count || h->journal_count>2u ||
       durable_card_zero(h->cid,16u) || durable_card_zero(h->image_id,16u) ||
       durable_card_zero(h->base_sha256,32u)) return -1;
    unsigned ones=0u;for(unsigned i=0;i<16u;i++) ones+=h->cid[i]==0xffu;if(ones==16u) return -1;
    for(unsigned kind=0;kind<2u;kind++) {
        const struct kui_vmu_durable_extent *map=kind?h->journal:h->base;
        unsigned count=kind?h->journal_count:h->base_count,limit=kind?2u:16u;
        unsigned expected=kind?2u:256u,total=0u;
        for(unsigned i=0;i<limit;i++) {
            if(i>=count) {if(map[i].lba || map[i].blocks) return -1;continue;}
            if(!map[i].blocks || map[i].blocks>expected-total ||
               (uint64_t)map[i].lba+map[i].blocks>h->card_sectors ||
               (i && (uint64_t)map[i-1u].lba+map[i-1u].blocks==map[i].lba)) return -1;
            total+=map[i].blocks;
            for(unsigned other=0;other<=kind;other++) {
                const struct kui_vmu_durable_extent *m=other?h->journal:h->base;
                unsigned n=other==kind?i:h->base_count;
                for(unsigned j=0;j<n;j++) if((uint64_t)map[i].lba<m[j].lba+(uint64_t)m[j].blocks &&
                    (uint64_t)m[j].lba<map[i].lba+(uint64_t)map[i].blocks) return -1;
            }
        }
        if(total!=expected) return -1;
    }
    return 0;
}
int kui_vmu_durable_handoff_seal(struct kui_vmu_durable_handoff *h) {
    if(!h) return -1;
    h->seal=0u;h->magic=KUI_VMU_DURABLE_MAGIC;h->version=KUI_VMU_DURABLE_VERSION;
    h->bytes=sizeof(*h);h->clean_checkpoint=KUI_VMU_DURABLE_CLEAN;
    if(durable_card_geometry(h)) return -1;
    h->seal=kui_retail_crc32(0,h,offsetof(struct kui_vmu_durable_handoff,seal));return 0;
}
int kui_vmu_durable_handoff_validate(const struct kui_vmu_durable_handoff *h) {
    return !h || h->magic!=KUI_VMU_DURABLE_MAGIC || h->version!=KUI_VMU_DURABLE_VERSION ||
        h->bytes!=sizeof(*h) || h->clean_checkpoint!=KUI_VMU_DURABLE_CLEAN || durable_card_geometry(h) ||
        h->seal!=kui_retail_crc32(0,h,offsetof(struct kui_vmu_durable_handoff,seal))?-1:0;
}
static void durable_card_increment(uint32_t *n) {if(*n!=UINT32_MAX) ++*n;}
static int __attribute__((noipa)) durable_card_fault(struct kui_vmu_durable *s,int reason) {
    if(!s->faulted) {s->sd_last_result=reason;durable_card_increment(&s->sd_faults);}
    s->faulted=1u;s->store.ready=0u;return -1;
}
static uint32_t durable_card_lba(const struct kui_vmu_durable *s,unsigned kind,unsigned logical) {
    const struct kui_vmu_durable_extent *map=kind?s->handoff.journal:s->handoff.base;
    unsigned count=kind?s->handoff.journal_count:s->handoff.base_count;
    for(unsigned i=0;i<count;i++) {
        if(logical<map[i].blocks) return map[i].lba+logical;
        logical-=map[i].blocks;
    }
    return UINT32_MAX;
}
static int durable_card_allowed(const struct durable_card_scope *scope,uint64_t lba) {
    const struct kui_vmu_durable *s=scope->owner;
    if(s->self!=s || !s->inflight || !s->invoked || s->faulted || s->store.io.context!=scope ||
       lba>=s->handoff.card_sectors || lba>UINT32_MAX) return 0;
    for(unsigned i=0;i<s->handoff.base_count+s->handoff.journal_count;i++)
        if(lba>=s->maps[i].first_lba && lba-s->maps[i].first_lba<s->maps[i].sectors) return 1;
    return 0;
}
static int durable_card_sector_read(void *context,uint64_t lba,void *out) {
    struct durable_card_scope *scope=context;if(!scope) return -1;
    struct kui_vmu_durable *s=scope->owner;
    if(!out || !durable_card_allowed(scope,lba) ||
       (scope->binding && lba!=durable_card_lba(s,1u,1u)))
        return durable_card_fault(s,KUI_VMU_DURABLE_SD_MAPPING);
    s->last_lba=(uint32_t)lba;
    s->last_sd_result=kui_loader_sd_read(&scope->card,(uint32_t)lba,1u,s->bounce);
    if(s->last_sd_result!=KUI_LOADER_SD_OK || !scope->card.ready)
        return durable_card_fault(s,KUI_VMU_DURABLE_SD_READ);
    durable_card_increment(&s->sd_reads);
    if(scope->binding && !durable_card_zero(s->bounce,512u))
        return durable_card_fault(s,KUI_VMU_DURABLE_SD_MARKER);
    if(out!=s->bounce) memcpy(out,s->bounce,512u);
    return 0;
}
/* Kept as a distinct admitted high-code symbol so the native audit can
 * witness the zero block callback immediately before the sole writer call. */
static int __attribute__((noipa)) durable_card_pio_write(struct kui_loader_sd *card,
    uint32_t lba,const void *data) {
    card->bus.transfer_block=NULL;
    return kui_sci_sd_write(card,lba,1u,data);
}
static int durable_card_sector_write(void *context,uint64_t lba,const void *data) {
    struct durable_card_scope *scope=context;if(!scope) return -1;
    struct kui_vmu_durable *s=scope->owner;
    if(!data || scope->binding || s->operation!=DURABLE_WRITE || !durable_card_allowed(scope,lba))
        return durable_card_fault(s,KUI_VMU_DURABLE_SD_MAPPING);
    s->last_lba=(uint32_t)lba;
    s->last_sd_result=durable_card_pio_write(&scope->card,(uint32_t)lba,data);
    if(s->last_sd_result!=KUI_LOADER_SD_OK || !scope->card.ready)
        return durable_card_fault(s,KUI_VMU_DURABLE_SD_STORE);
    return 0;
}
static int durable_card_sector_sync(void *context) {
    struct durable_card_scope *scope=context;if(!scope) return -1;
    struct kui_vmu_durable *s=scope->owner;
    if(scope->binding || s->operation!=DURABLE_WRITE || s->faulted ||
       !s->inflight || !s->invoked || s->store.io.context!=scope)
        return durable_card_fault(s,KUI_VMU_DURABLE_SD_MAPPING);
    s->last_sd_result=kui_sci_sd_sync(&scope->card);
    if(s->last_sd_result!=KUI_LOADER_SD_OK || !scope->card.ready)
        return durable_card_fault(s,KUI_VMU_DURABLE_SD_STORE);
    return 0;
}
int kui_vmu_durable_transaction(void *context,struct kui_loader_sd *loan) {
    struct kui_vmu_durable *s=context;
    if(!s || s->self!=s || !s->ready || !s->handling || !s->inflight || s->invoked || s->faulted ||
       (s->operation!=DURABLE_READ && s->operation!=DURABLE_WRITE) || !loan) {
        if(loan) loan->ready=false;
        if(s && s->self==s) return durable_card_fault(s,KUI_VMU_DURABLE_SD_CONTEXT);
        return -1;
    }
    s->invoked=1u;
    if(!loan->ready || loan->slow || !loan->bus.select || !loan->bus.transfer || !loan->bus.ticks ||
       loan->blocks!=s->handoff.card_sectors || loan->high_capacity!=!!s->handoff.high_capacity) {
        loan->ready=false;return durable_card_fault(s,KUI_VMU_DURABLE_SD_IDENTITY);
    }
    struct kui_vmu_sd_identity id;durable_card_increment(&s->cid_checks);
    if(kui_vmu_sd_identity_read(loan,&id)!=KUI_VMU_BACKING_OK ||
       id.sectors!=s->handoff.card_sectors || id.high_capacity!=!!s->handoff.high_capacity ||
       memcmp(id.cid,s->handoff.cid,16u)) {
        loan->ready=false;return durable_card_fault(s,KUI_VMU_DURABLE_SD_IDENTITY);
    }
    struct durable_card_scope scope={.owner=s,.card=*loan,.binding=!s->bound};
    /* Writes use only bounded PIO in this worker, preserving the resident's
     * read-only block/DMA capability and its fixed low-memory footprint. */
    if(s->operation==DURABLE_WRITE) scope.card.bus.transfer_block=NULL;
    s->store.io.context=&scope;
    if(scope.binding) {
        struct kui_vmu_store_config cfg={.card_sectors=s->handoff.card_sectors,
            .image_extents=s->maps,.image_extent_count=s->handoff.base_count,
            .journal_extents=s->maps+s->handoff.base_count,.journal_extent_count=s->handoff.journal_count};
        memcpy(cfg.image_id,s->handoff.image_id,16u);
        const struct kui_vmu_store_io io={&scope,durable_card_sector_read,durable_card_sector_write,durable_card_sector_sync};
        s->last_store_result=kui_vmu_store_open(&s->store,&io,&cfg);
        if(s->last_store_result) goto failed;
        scope.binding=0u;s->bound_candidate=1u;
    }
    if(s->operation==DURABLE_WRITE) {
        s->last_store_result=kui_vmu_store_write(&s->store,s->block,s->protocol.staged);
        if(s->last_store_result) goto failed;
    }
    s->last_store_result=kui_vmu_store_read(&s->store,s->block,s->bounce);
    if(s->last_store_result) goto failed;
    s->crc_actual=kui_retail_crc32(0,s->bounce,512u);
    if(s->operation==DURABLE_WRITE?memcmp(s->bounce,s->store.data,512u):s->crc_actual!=s->crc_expected) {
        (void)durable_card_fault(s,KUI_VMU_DURABLE_SD_CRC);goto failed;
    }
    s->pending_crc=s->crc_actual;s->verified=1u;s->store.io.context=NULL;
    return 0;
failed:
    loan->ready=false;s->store.io.context=NULL;
    if(!s->faulted) (void)durable_card_fault(s,KUI_VMU_DURABLE_SD_STORE);
    return -1;
}
static int durable_card_execute(struct kui_vmu_durable *s,unsigned op,uint16_t block) {
    if(s->faulted) return KUI_VMU_EMU_IO_ERROR;
    if(!s->handling || s->inflight || block>=256u) {
        (void)durable_card_fault(s,KUI_VMU_DURABLE_SD_CONTEXT);return KUI_VMU_EMU_IO_ERROR;
    }
    s->operation=op;s->block=block;s->last_block=block;s->crc_expected=s->handoff.block_crc[block];
    s->crc_actual=0u;s->inflight=1u;s->invoked=s->verified=s->bound_candidate=0u;
    if(op==DURABLE_WRITE) durable_card_increment(&s->commit_attempts);
    s->last_lease_result=s->lease(s->transaction_entry,s);
    s->inflight=0u;s->store.io.context=NULL;
    if(s->last_lease_result==KUI_VMU_SD_LEASE_BUSY && !s->invoked && !s->faulted) {
        durable_card_increment(&s->sd_busy);s->sd_last_result=KUI_VMU_DURABLE_SD_BUSY;
        return KUI_VMU_EMU_IO_BUSY;
    }
    if(s->last_lease_result!=KUI_VMU_SD_LEASE_OK || !s->invoked || !s->verified || s->faulted) {
        if(!s->faulted) (void)durable_card_fault(s,KUI_VMU_DURABLE_SD_LEASE);
        return KUI_VMU_EMU_IO_ERROR;
    }
    if(s->bound_candidate) s->bound=1u;
    s->sd_last_result=KUI_VMU_DURABLE_SD_OK;
    if(op==DURABLE_WRITE) {
        s->handoff.block_crc[block]=s->pending_crc;s->handoff.seal=0u;
        durable_card_increment(&s->commits);
    } else durable_card_increment(&s->reads);
    return KUI_VMU_EMU_IO_OK;
}
static int durable_card_read(void *context,uint16_t block,uint8_t out[512]) {
    struct kui_vmu_durable *s=context;int result=durable_card_execute(s,DURABLE_READ,block);
    if(!result) memcpy(out,s->bounce,512u);
    return result;
}
static int durable_card_commit(void *context,uint16_t block,const uint8_t data[512]) {
    struct kui_vmu_durable *s=context;
    if(data!=s->protocol.staged) {durable_card_fault(s,KUI_VMU_DURABLE_SD_CONTEXT);return KUI_VMU_EMU_IO_ERROR;}
    return durable_card_execute(s,DURABLE_WRITE,block);
}
int kui_vmu_durable_init(struct kui_vmu_durable *s,const struct kui_vmu_durable_handoff *h,
    int (*lease)(kui_vmu_sd_transaction,void *),kui_vmu_sd_transaction entry) {
    if(!s || !h || !lease || !entry || durable_card_overlap(s,sizeof(*s),h,sizeof(*h)) ||
       kui_vmu_durable_handoff_validate(h)) return KUI_VMU_DURABLE_ARGUMENT;
    if(s->started) return KUI_VMU_DURABLE_INTERNAL;
    memset(s,0,sizeof(*s));s->self=s;s->started=1u;s->handoff=*h;s->lease=lease;s->transaction_entry=entry;
    for(unsigned kind=0;kind<2u;kind++) {
        const struct kui_vmu_durable_extent *map=kind?h->journal:h->base;
        unsigned count=kind?h->journal_count:h->base_count,offset=kind?h->base_count:0u;
        for(unsigned i=0;i<count;i++) s->maps[offset+i]=(struct kui_vmu_store_extent){map[i].lba,map[i].blocks};
    }
    const struct kui_vmu_emulation_io io={s,durable_card_read,durable_card_commit};
    if(kui_vmu_emulation_init(&s->protocol,1u,&io)) return KUI_VMU_DURABLE_INTERNAL;
    s->ready=1u;return 0;
}
static uint32_t durable_card_le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned durable_card_shape(uint32_t h,uint32_t p0,uint32_t p1) {
    unsigned cmd=h&255u,words=h>>24,phase=(p1>>8)&255u;
    if((h&UINT32_C(0x00ffff00))!=UINT32_C(0x00000100)) return 0u;
    if(cmd==1u || cmd==2u) return words?0u:cmd==1u?116u:196u;
    if(p0!=KUI_VMU_EMU_FUNC_MEMCARD || (p1&UINT32_C(0x00ff00ff))) return 0u;
    if(cmd==10u) return words==2u && !p1?32u:0u;
    if(cmd==11u) return words==2u && !phase?524u:0u;
    if(cmd==12u) return words==34u && phase<4u?8u:0u;
    if(cmd==13u) return words==2u && (phase==1u || phase==4u)?8u:0u;
    return 0u;
}
int kui_vmu_durable_admit(struct kui_vmu_durable *s,uint32_t h,uint32_t p0,uint32_t p1) {
    if(!s || s->self!=s || !s->ready) return KUI_VMU_DURABLE_ARGUMENT;
    if(s->faulted || s->handling || s->inflight) return KUI_VMU_DURABLE_INTERNAL;
    return durable_card_shape(h,p0,p1)?KUI_VMU_DURABLE_HANDLED:KUI_VMU_DURABLE_REFUSED;
}
int kui_vmu_durable_handle(struct kui_vmu_durable *s,const uint8_t *request,size_t bytes,
    uint8_t *response,size_t capacity,size_t *response_bytes) {
    if(!response_bytes || (s && durable_card_overlap(s,sizeof(*s),response_bytes,sizeof(*response_bytes))) ||
       (request && durable_card_overlap(request,bytes,response_bytes,sizeof(*response_bytes))) ||
       (response && durable_card_overlap(response,capacity,response_bytes,sizeof(*response_bytes))))
        return KUI_VMU_DURABLE_ARGUMENT;
    *response_bytes=0u;
    if(!s || s->self!=s || !s->ready || !request || !response || !response_bytes || bytes<4u ||
       durable_card_overlap(s,sizeof(*s),request,bytes) || durable_card_overlap(s,sizeof(*s),response,capacity) ||
       durable_card_overlap(request,bytes,response,capacity)) return KUI_VMU_DURABLE_ARGUMENT;
    if(s->handling || s->inflight) return KUI_VMU_DURABLE_INTERNAL;
    uint32_t h=durable_card_le32(request),p0=0u,p1=0u;
    if(bytes>=12u) {p0=durable_card_le32(request+4u);p1=durable_card_le32(request+8u);}
    unsigned needed=durable_card_shape(h,p0,p1);
    if(!needed || bytes!=4u+4u*(h>>24)) {durable_card_increment(&s->refused);return KUI_VMU_DURABLE_REFUSED;}
    if(capacity<(s->faulted?8u:needed)) return KUI_VMU_DURABLE_CAPACITY;
    if(s->faulted) {
        memset(response,0,8u);response[0]=0xfbu;response[2]=1u;response[3]=1u;response[7]=8u;
        *response_bytes=8u;return KUI_VMU_DURABLE_HANDLED;
    }
    s->handling=1u;int result=kui_vmu_emulation_handle(&s->protocol,request,bytes,response,capacity,response_bytes);
    s->handling=0u;
    if(result!=1) {durable_card_fault(s,KUI_VMU_DURABLE_SD_CONTEXT);*response_bytes=0u;return KUI_VMU_DURABLE_INTERNAL;}
    if((h&255u)==12u && response[0]==7u) durable_card_increment(&s->write_phases);
    durable_card_increment(&s->requests);return KUI_VMU_DURABLE_HANDLED;
}
