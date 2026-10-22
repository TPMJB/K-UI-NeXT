/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_sd.h"
#if KUI_ARMADA_CE_VMU_WRITE_RUNTIME || defined(KUI_ARMADA_CE_VMU_SD_TEST)
#include "kui/retail_image.h"
#include "../dreamcast/sci_sd_storage.h"
#include <string.h>

#define SD_MAGIC UINT32_C(0x31534441)
enum {SD_READ=1,SD_COMMIT=2};
enum {SD_CONTEXT=1,SD_LOAN,SD_PORT,SD_IDENTITY,SD_MAPPING,SD_MARKER,SD_IO,SD_VERIFY};
struct sd_scope {struct kui_armada_ce_vmu_sd *owner;struct kui_loader_sd card;unsigned binding;};
static void armada_sd_inc(uint32_t *v) {if(*v!=UINT32_MAX) ++*v;}
static bool armada_sd_span(const void *p,size_t n) {return p && n<=UINTPTR_MAX-(uintptr_t)p;}
static bool armada_sd_private(const void *p,size_t n) {
    if(!armada_sd_span(p,n)) return false;
#if defined(__sh__) && !defined(KUI_ARMADA_CE_VMU_SD_TEST)
    uintptr_t at=(uintptr_t)p;
    return at>=UINT32_C(0xac010000) && at<=UINT32_C(0xad000000)-n;
#else
    return true;
#endif
}
static bool armada_sd_overlaps(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;return x<=y?y-x<an:x-y<bn;
}
static bool armada_sd_zero(const void *p,size_t n) {
    const uint8_t *b=p;for(size_t i=0;i<n;i++) if(b[i]) return false;return true;
}
static bool armada_sd_valid(const struct kui_armada_ce_vmu_sd *s) {
    return armada_sd_private(s,sizeof(*s)) && !((uintptr_t)s&31u) &&
        s->self==s && s->magic==SD_MAGIC && s->started==1u &&
        s->ops.begin && s->ops.end && s->ops.guard.enter && s->ops.guard.leave && s->ops.guard.worker_ready;
}
static int armada_sd_fail(struct kui_armada_ce_vmu_sd *s,int why) {
    if(!s->faulted) {s->last_result=why;armada_sd_inc(&s->failures);}
    s->faulted=1u;s->store.ready=0u;return KUI_VMU_EMU_IO_ERROR;
}
static bool armada_sd_healthy(struct sd_scope *scope) {
    struct kui_armada_ce_vmu_sd *s=scope->owner;
    return armada_sd_valid(s) && s->busy && !s->faulted && scope->card.ready &&
        s->ops.guard.worker_ready(s->ops.guard.context) && kui_armada_ce_vmu_sd_pio_healthy(&s->pio);
}
static uint8_t armada_sd_wire(struct sd_scope *scope,uint8_t value) {
    return scope->card.bus.transfer(scope->card.bus.ctx,value,false);
}
static uint8_t armada_sd_crc7(const uint8_t *p) {
    uint8_t crc=0;for(unsigned i=0;i<5u;i++) {
        uint8_t v=p[i];for(unsigned b=0;b<8u;b++) {
            crc=(uint8_t)(crc<<1);if((crc^v)&0x80u) crc^=9u;v=(uint8_t)(v<<1);
        }
    }
    return (uint8_t)((crc<<1)|1u);
}
static uint16_t armada_sd_crc16(uint16_t crc,uint8_t v) {
    uint32_t x=(crc>>8)^v;x^=x>>4;return (uint16_t)((crc<<8)^(x<<12)^(x<<5)^x);
}
static bool armada_sd_wait_byte(struct sd_scope *scope,bool ready,uint8_t *v) {
    uint32_t start=scope->card.bus.ticks(scope->card.bus.ctx);
    for(unsigned i=0;i<1000000u;i++) {
        *v=armada_sd_wire(scope,0xffu);if(ready?*v==0xffu:*v!=0xffu) return true;
        if((uint32_t)(scope->card.bus.ticks(scope->card.bus.ctx)-start)>=500u*12500u) break;
    }
    return false;
}
/* Actual read-only CID/CSD, with command/data CRCs and finite byte bounds.
 * No reset, initialization, OCR update, erase or persistent card setting. */
static bool armada_sd_read_register(struct sd_scope *scope,unsigned command,uint8_t out[16]) {
    if(!armada_sd_healthy(scope)) return false;
    uint8_t v=0xffu,packet[6]={(uint8_t)(0x40u|command),0,0,0,0,0};
    packet[5]=armada_sd_crc7(packet);bool ok=false;scope->card.last_command=(uint8_t)command;
    scope->card.last_response=0xffu;scope->card.bus.select(scope->card.bus.ctx,true);
    if(!armada_sd_wait_byte(scope,true,&v)) goto done;
    for(unsigned i=0;i<6u;i++) (void)armada_sd_wire(scope,packet[i]);
    for(unsigned i=0;i<20u;i++) {v=armada_sd_wire(scope,0xffu);if(!(v&0x80u)) break;}
    scope->card.last_response=v;
    if(v || !armada_sd_wait_byte(scope,false,&v) || v!=0xfeu) goto done;
    uint16_t crc=0;for(unsigned i=0;i<16u;i++) {out[i]=armada_sd_wire(scope,0xffu);crc=armada_sd_crc16(crc,out[i]);}
    uint16_t expected=(uint16_t)armada_sd_wire(scope,0xffu)<<8;expected|=armada_sd_wire(scope,0xffu);
    ok=crc==expected;
done:
    scope->card.bus.select(scope->card.bus.ctx,false);(void)armada_sd_wire(scope,0xffu);
    return ok && armada_sd_healthy(scope);
}
static bool armada_sd_identity(struct sd_scope *scope) {
    struct kui_armada_ce_vmu_sd *s=scope->owner;uint8_t cid[16],csd[16];
    armada_sd_inc(&s->identity_checks);
    if(!armada_sd_read_register(scope,10u,cid) || memcmp(cid,s->handoff.cid,16u) ||
       !armada_sd_read_register(scope,9u,csd)) return false;
    uint64_t sectors=0;unsigned version=csd[0]>>6;
    if(version==1u && s->handoff.high_capacity) {
        uint32_t size=(uint32_t)(csd[7]&63u)<<16|(uint32_t)csd[8]<<8|csd[9];
        sectors=((uint64_t)size+1u)*1024u;
    } else if(!version && !s->handoff.high_capacity) {
        unsigned bits=csd[5]&15u,mult=((csd[9]&3u)<<1)|(csd[10]>>7);
        uint32_t size=(uint32_t)(csd[6]&3u)<<10|(uint32_t)csd[7]<<2|(csd[8]>>6);
        if(bits<9u || bits>11u) return false;
        /* CSDv1 has at most4096 units and a maximum2048-sector unit:
         * the bounded32-bit shift avoids an unaudited compiler helper. */
        sectors=(size+1u)<<(mult+2u+bits-9u);
    }
    return sectors==s->handoff.card_sectors && scope->card.blocks==sectors &&
        scope->card.high_capacity==!!s->handoff.high_capacity;
}
static uint32_t armada_sd_marker_lba(const struct kui_armada_ce_vmu_sd *s) {
    unsigned logical=1u;for(unsigned i=0;i<s->handoff.journal_count;i++) {
        if(logical<s->handoff.journal[i].blocks) return s->handoff.journal[i].lba+logical;
        logical-=s->handoff.journal[i].blocks;
    }
    return UINT32_MAX;
}
static bool armada_sd_allowed(struct sd_scope *scope,uint64_t lba) {
    struct kui_armada_ce_vmu_sd *s=scope->owner;
    if(!armada_sd_healthy(scope) || s->store.io.context!=scope || lba>UINT32_MAX) return false;
    for(unsigned kind=0;kind<2u;kind++) {
        const struct kui_vmu_durable_extent *map=kind?s->handoff.journal:s->handoff.base;
        unsigned count=kind?s->handoff.journal_count:s->handoff.base_count;
        for(unsigned i=0;i<count;i++)
            if(lba>=map[i].lba && lba-map[i].lba<map[i].blocks) return true;
    }
    return false;
}
static int armada_sd_sector_read(void *context,uint64_t lba,void *out) {
    struct sd_scope *scope=context;struct kui_armada_ce_vmu_sd *s=scope->owner;
    if(!out || !armada_sd_allowed(scope,lba) || (scope->binding && lba!=armada_sd_marker_lba(s))) return armada_sd_fail(s,SD_MAPPING);
    s->last_lba=(uint32_t)lba;s->last_sd_result=kui_loader_sd_read(&scope->card,(uint32_t)lba,1u,s->bounce);
    if(s->last_sd_result || !armada_sd_healthy(scope)) return armada_sd_fail(s,SD_IO);
    armada_sd_inc(&s->sector_reads);
    if(scope->binding && !armada_sd_zero(s->bounce,512u)) return armada_sd_fail(s,SD_MARKER);
    if(out!=s->bounce) memcpy(out,s->bounce,512u);
    return 0;
}
/* This separate symbol lets the native audit verify the unconditional zero
 * block callback before the only writer, preserving explicit PIO framing. */
static int __attribute__((noinline)) armada_sd_pio_write(struct kui_loader_sd *card,uint32_t lba,const void *data) {
    card->bus.transfer_block=NULL;return kui_sci_sd_write(card,lba,1u,data);
}
static int armada_sd_sector_write(void *context,uint64_t lba,const void *data) {
    struct sd_scope *scope=context;struct kui_armada_ce_vmu_sd *s=scope->owner;
    if(!data || scope->binding || s->operation!=SD_COMMIT || !armada_sd_allowed(scope,lba)) return armada_sd_fail(s,SD_MAPPING);
    if(!armada_sd_identity(scope)) return armada_sd_fail(s,SD_IDENTITY);
    s->last_lba=(uint32_t)lba;s->last_sd_result=armada_sd_pio_write(&scope->card,(uint32_t)lba,data);
    if(s->last_sd_result || !armada_sd_healthy(scope)) return armada_sd_fail(s,SD_IO);
    armada_sd_inc(&s->sector_writes);return 0;
}
static int armada_sd_sector_sync(void *context) {
    struct sd_scope *scope=context;struct kui_armada_ce_vmu_sd *s=scope->owner;
    if(scope->binding || s->operation!=SD_COMMIT || !armada_sd_healthy(scope) ||
       s->store.io.context!=scope || !armada_sd_identity(scope)) return armada_sd_fail(s,SD_IDENTITY);
    s->last_sd_result=kui_sci_sd_sync(&scope->card);
    return s->last_sd_result || !armada_sd_healthy(scope)?armada_sd_fail(s,SD_IO):0;
}
int kui_armada_ce_vmu_sd_init(struct kui_armada_ce_vmu_sd *s,
    const struct kui_vmu_durable_handoff *h,const struct kui_armada_ce_vmu_sd_ops *ops) {
    if(!armada_sd_private(s,sizeof(*s)) || ((uintptr_t)s&31u) ||
       !armada_sd_private(h,sizeof(*h)) || !armada_sd_private(ops,sizeof(*ops)) ||
       armada_sd_overlaps(s,sizeof(*s),h,sizeof(*h)) || armada_sd_overlaps(s,sizeof(*s),ops,sizeof(*ops)) ||
       !ops->begin || !ops->end || !ops->guard.enter || !ops->guard.leave || !ops->guard.worker_ready ||
       kui_vmu_durable_handoff_validate(h) || !armada_sd_zero(s,sizeof(*s))) return -1;
    if(kui_armada_ce_vmu_sd_pio_init(&s->pio)) return -1;
    s->self=s;s->handoff=*h;s->ops=*ops;s->magic=SD_MAGIC;s->started=1u;
    return 0;
}
static int armada_sd_execute(struct kui_armada_ce_vmu_sd *s,unsigned operation,uint16_t block,void *read_out,const void *data) {
    const void *buffer=operation==SD_READ?read_out:data;uint32_t saved;
    if(!armada_sd_valid(s) || block>=256u || !armada_sd_private(buffer,512u) ||
       armada_sd_overlaps(s,sizeof(*s),buffer,512u)) return KUI_VMU_EMU_IO_ERROR;
    if(!s->ops.guard.worker_ready(s->ops.guard.context)) return KUI_VMU_EMU_IO_ERROR;
    if(!s->ops.guard.enter(s->ops.guard.context,&saved)) return KUI_VMU_EMU_IO_BUSY;
    if(s->faulted || s->busy) {
        int result=s->faulted?KUI_VMU_EMU_IO_ERROR:KUI_VMU_EMU_IO_BUSY;
        s->ops.guard.leave(s->ops.guard.context,saved);return result;
    }
    s->busy=1u;s->operation=operation;s->block=block;
    s->ops.guard.leave(s->ops.guard.context,saved);
    struct sd_scope scope={.owner=s};bool loan=false,verified=false,candidate=false;
    s->last_loan_result=s->ops.begin(&scope.card);
    if(s->last_loan_result==1) {armada_sd_inc(&s->bus_busy);s->busy=0u;return KUI_VMU_EMU_IO_BUSY;}
    if(s->last_loan_result) {armada_sd_fail(s,SD_LOAN);goto finished;}
    loan=true;
    if(!scope.card.ready || scope.card.slow || scope.card.blocks!=s->handoff.card_sectors ||
       scope.card.high_capacity!=!!s->handoff.high_capacity) {armada_sd_fail(s,SD_IDENTITY);goto finished;}
    if(!s->ops.guard.enter(s->ops.guard.context,&saved)) {armada_sd_fail(s,SD_CONTEXT);goto finished;}
    int port=kui_armada_ce_vmu_sd_pio_acquire(&s->pio);
    s->ops.guard.leave(s->ops.guard.context,saved);
    if(port || kui_armada_ce_vmu_sd_pio_bus(&s->pio,&scope.card.bus)) {armada_sd_fail(s,SD_PORT);goto finished;}
    if(!armada_sd_identity(&scope)) {armada_sd_fail(s,SD_IDENTITY);goto finished;}
    s->store.io.context=&scope;scope.binding=!s->bound;
    if(scope.binding) {
        /* The store copies these maps before returning. Keep this one-time
         * conversion on the verified4KiB worker stack, rather than retaining
         * a third permanent copy beside the handoff and store-owned maps. */
        struct kui_vmu_store_extent maps[18];
        for(unsigned kind=0;kind<2u;kind++) {
            const struct kui_vmu_durable_extent *map=kind?s->handoff.journal:s->handoff.base;
            unsigned count=kind?s->handoff.journal_count:s->handoff.base_count,offset=kind?s->handoff.base_count:0u;
            for(unsigned i=0;i<count;i++) maps[offset+i]=(struct kui_vmu_store_extent){map[i].lba,map[i].blocks};
        }
        struct kui_vmu_store_config cfg={.card_sectors=s->handoff.card_sectors,
            .image_extents=maps,.image_extent_count=s->handoff.base_count,
            .journal_extents=maps+s->handoff.base_count,.journal_extent_count=s->handoff.journal_count};
        memcpy(cfg.image_id,s->handoff.image_id,16u);
        const struct kui_vmu_store_io io={&scope,armada_sd_sector_read,armada_sd_sector_write,armada_sd_sector_sync};
        s->last_store_result=kui_vmu_store_open(&s->store,&io,&cfg);
        if(s->last_store_result) {armada_sd_fail(s,SD_MARKER);goto finished;}
        scope.binding=0u;candidate=true;
    }
    if(operation==SD_COMMIT) {
        s->last_store_result=kui_vmu_store_write(&s->store,block,data);
        if(s->last_store_result) {armada_sd_fail(s,SD_IO);goto finished;}
    }
    s->last_store_result=kui_vmu_store_read(&s->store,block,s->bounce);
    if(s->last_store_result) {armada_sd_fail(s,SD_IO);goto finished;}
    s->pending_crc=kui_retail_crc32(0,s->bounce,512u);
    if(operation==SD_COMMIT?memcmp(s->bounce,data,512u):s->pending_crc!=s->handoff.block_crc[block]) {
        armada_sd_fail(s,SD_VERIFY);goto finished;
    }
    if(!armada_sd_identity(&scope)) {armada_sd_fail(s,SD_IDENTITY);goto finished;}
    verified=true;
finished:
    s->store.io.context=NULL; /* Nothing borrows the stack-local loan now. */
    if(s->pio.acquired) {
        if(s->ops.guard.enter(s->ops.guard.context,&saved)) {
            port=kui_armada_ce_vmu_sd_pio_release(&s->pio);
            s->ops.guard.leave(s->ops.guard.context,saved);
        } else port=-1;
        if(port) {armada_sd_fail(s,SD_PORT);verified=false;}
    }
    if(loan) {
        s->last_loan_result=s->ops.end(verified && !s->faulted);
        if(s->last_loan_result) {armada_sd_fail(s,SD_LOAN);verified=false;}
    }
    if(!s->ops.guard.enter(s->ops.guard.context,&saved)) {armada_sd_fail(s,SD_CONTEXT);s->busy=0u;return KUI_VMU_EMU_IO_ERROR;}
    int result=KUI_VMU_EMU_IO_ERROR;
    if(verified && !s->faulted) {
        if(candidate) s->bound=1u;
        if(operation==SD_COMMIT) {s->handoff.block_crc[block]=s->pending_crc;s->handoff.seal=0u;armada_sd_inc(&s->commits);}
        else {memcpy(read_out,s->bounce,512u);armada_sd_inc(&s->reads);}
        s->last_result=0;result=KUI_VMU_EMU_IO_OK;
    }
    s->busy=0u;s->ops.guard.leave(s->ops.guard.context,saved);return result;
}
int kui_armada_ce_vmu_sd_read(void *context,uint16_t block,uint8_t out[512]) {
    return armada_sd_execute(context,SD_READ,block,out,NULL);
}
int kui_armada_ce_vmu_sd_commit(void *context,uint16_t block,const uint8_t data[512]) {
    return armada_sd_execute(context,SD_COMMIT,block,NULL,data);
}
#endif
