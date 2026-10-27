/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_backing.h"
#include "sd_reader.h"
#if KUI_VMU_EXPERIMENTAL
#include "../dreamcast/sci_sd_storage.h"
#endif

#define VMU_WAIT_TICKS (500u*12500u)
#define VMU_MAX_FORBIDDEN 1024u

static uint16_t crc16(uint16_t crc,uint8_t value) {
    uint32_t x=(crc>>8)^value;x^=x>>4;
    return (uint16_t)((crc<<8)^(x<<12)^(x<<5)^x);
}
static uint8_t crc7(const uint8_t *packet) {
    uint8_t crc=0;
    for(unsigned i=0;i<5;i++) {
        uint8_t value=packet[i];
        for(unsigned j=0;j<8;j++) {
            crc=(uint8_t)(crc<<1);
            if((crc^value)&0x80u) crc^=9u;
            value=(uint8_t)(value<<1);
        }
    }
    return (uint8_t)((crc<<1)|1u);
}
static uint8_t byte(struct kui_loader_sd *card,uint8_t value) {
    return card->bus.transfer(card->bus.ctx,value,card->slow);
}
static bool wait_byte(struct kui_loader_sd *card,bool ready,uint8_t *value) {
    uint32_t start=card->bus.ticks(card->bus.ctx);
    for(unsigned i=0;i<1000000u;i++) {
        *value=byte(card,0xff);
        if(ready?*value==0xff:*value!=0xff) return true;
        if((uint32_t)(card->bus.ticks(card->bus.ctx)-start)>=VMU_WAIT_TICKS) break;
    }
    return false;
}
static bool useful_cid(const uint8_t *cid) {
    unsigned zero=0,ones=0;
    for(unsigned i=0;i<16;i++) {zero+=cid[i]==0;ones+=cid[i]==0xff;}
    return zero!=16 && ones!=16;
}
int kui_vmu_sd_identity_read(struct kui_loader_sd *card,struct kui_vmu_sd_identity *out) {
    if(!card || !out || !card->bus.select || !card->bus.transfer || !card->bus.ticks)
        return KUI_VMU_BACKING_ARGUMENT;
    if(!card->ready || card->slow || !card->blocks || card->blocks>(UINT64_C(1)<<32))
        return KUI_VMU_BACKING_IDENTITY;
    struct kui_vmu_sd_identity id={.sectors=card->blocks,.high_capacity=card->high_capacity};
    int result=KUI_VMU_BACKING_IO;
    uint8_t value=0xff;
    card->last_command=10;card->last_response=0xff;
    card->bus.select(card->bus.ctx,true);
    if(!wait_byte(card,true,&value)) goto done;
    uint8_t packet[6]={0x4a,0,0,0,0,0};packet[5]=crc7(packet);
    for(unsigned i=0;i<6;i++) (void)byte(card,packet[i]);
    for(unsigned i=0;i<20;i++) {
        value=byte(card,0xff);
        if(!(value&0x80u)) {card->last_response=value;break;}
    }
    if(value!=0 || !wait_byte(card,false,&value) || value!=0xfe) goto done;
    uint16_t crc=0;
    for(unsigned i=0;i<16;i++) {id.cid[i]=byte(card,0xff);crc=crc16(crc,id.cid[i]);}
    uint16_t expected=(uint16_t)byte(card,0xff)<<8;expected|=byte(card,0xff);
    if(crc!=expected) goto done;
    result=useful_cid(id.cid)?KUI_VMU_BACKING_OK:KUI_VMU_BACKING_IDENTITY;
done:
    card->bus.select(card->bus.ctx,false);(void)byte(card,0xff);
    if(result==KUI_VMU_BACKING_OK) *out=id;
    else card->ready=false;
    return result;
}

static bool extent_valid(const struct kui_vmu_store_extent *e,uint64_t first,uint64_t end) {
    return e->sectors && e->first_lba>=first && e->first_lba<end &&
        e->sectors<=end-e->first_lba;
}
static bool overlaps(const struct kui_vmu_store_extent *a,const struct kui_vmu_store_extent *b) {
    return a->first_lba<b->first_lba+b->sectors && b->first_lba<a->first_lba+a->sectors;
}
static bool append(struct kui_vmu_backing *b,const struct kui_vmu_store_extent *extents,
    size_t count,uint32_t expected,uint64_t first,uint64_t end) {
    if(!extents || !count || count>KUI_VMU_STORE_MAX_EXTENTS) return false;
    uint32_t sum=0;
    for(size_t i=0;i<count;i++) {
        const struct kui_vmu_store_extent *e=&extents[i];
        if(!extent_valid(e,first,end) || e->sectors>expected-sum) return false;
        for(size_t j=0;j<b->allowed_count;j++) if(overlaps(e,&b->allowed[j])) return false;
        b->allowed[b->allowed_count++]=*e;sum+=e->sectors;
    }
    return sum==expected;
}
int kui_vmu_backing_init(struct kui_vmu_backing *b,const struct kui_vmu_backing_bus *bus,
                        const struct kui_vmu_backing_config *cfg) {
    if(!b) return KUI_VMU_BACKING_ARGUMENT;
    *b=(struct kui_vmu_backing){0};
    b->last_result=KUI_VMU_BACKING_ARGUMENT;
    if(!bus || !cfg || !cfg->store || !bus->card || !bus->enter || !bus->leave ||
       !bus->acquire || !bus->release || (bus->reader_stream && !bus->reader_lease) ||
       (!cfg->forbidden && cfg->forbidden_count) || cfg->forbidden_count>VMU_MAX_FORBIDDEN)
        return b->last_result;
    const struct kui_vmu_store_config *s=cfg->store;
    uint64_t sectors=cfg->identity.sectors;
    if(!sectors || sectors>(UINT64_C(1)<<32) || sectors!=s->card_sectors ||
       (!cfg->identity.high_capacity && sectors>(UINT64_C(1)<<23)) ||
       !useful_cid(cfg->identity.cid))
        return b->last_result=KUI_VMU_BACKING_IDENTITY;
    unsigned nonzero=0;
    for(unsigned i=0;i<16;i++) nonzero|=s->image_id[i];
    if(!nonzero) return b->last_result=KUI_VMU_BACKING_IDENTITY;
    if(!cfg->partition_sectors || cfg->partition_first>=sectors ||
       cfg->partition_sectors>sectors-cfg->partition_first)
        return b->last_result=KUI_VMU_BACKING_MAPPING;
    uint64_t end=cfg->partition_first+cfg->partition_sectors;
    if(!append(b,s->image_extents,s->image_extent_count,KUI_VMU_STORE_BLOCKS,cfg->partition_first,end) ||
       !append(b,s->journal_extents,s->journal_extent_count,KUI_VMU_STORE_JOURNAL_SECTORS,cfg->partition_first,end))
        return b->last_result=KUI_VMU_BACKING_MAPPING;
    for(size_t i=0;i<cfg->forbidden_count;i++) {
        if(!extent_valid(&cfg->forbidden[i],0,sectors))
            return b->last_result=KUI_VMU_BACKING_MAPPING;
        for(size_t j=0;j<b->allowed_count;j++) if(overlaps(&cfg->forbidden[i],&b->allowed[j]))
            return b->last_result=KUI_VMU_BACKING_MAPPING;
    }
    b->bus=*bus;b->identity=cfg->identity;b->allow_writes=cfg->allow_writes;
    b->initialized=true;
    return b->last_result=KUI_VMU_BACKING_OK;
}
static int poison(struct kui_vmu_backing *b,int result) {
    b->poisoned=true;b->bus.card->ready=false;
    return b->last_result=result;
}
static bool authorized(const struct kui_vmu_backing *b,uint64_t lba) {
    if(lba>UINT32_MAX) return false;
    for(size_t i=0;i<b->allowed_count;i++) {
        const struct kui_vmu_store_extent *e=&b->allowed[i];
        if(lba>=e->first_lba && lba-e->first_lba<e->sectors) return true;
    }
    return false;
}
static int check_identity(struct kui_vmu_backing *b) {
    struct kui_loader_sd *card=b->bus.card;
    if(!card->ready || card->slow || card->blocks!=b->identity.sectors ||
       card->high_capacity!=b->identity.high_capacity)
        return poison(b,KUI_VMU_BACKING_IDENTITY);
    if(b->bus.healthy && !b->bus.healthy(b->bus.context)) return poison(b,KUI_VMU_BACKING_IO);
    struct kui_vmu_sd_identity actual;
    int result=kui_vmu_sd_identity_read(card,&actual);
    if(result!=KUI_VMU_BACKING_OK) return poison(b,result);
    for(unsigned i=0;i<16;i++) if(actual.cid[i]!=b->identity.cid[i])
        return poison(b,KUI_VMU_BACKING_IDENTITY);
    return KUI_VMU_BACKING_OK;
}
/* All exits after enter unwind through finish; failed acquisition never
 * releases somebody else's lease. No operation retries an uncertain command. */
static int begin(struct kui_vmu_backing *b,bool *entered,bool *acquired) {
    *entered=false;*acquired=false;
    if(b->inflight) return b->last_result=KUI_VMU_BACKING_BUSY;
    b->inflight=true;
    if(!b->bus.enter(b->bus.context)) {
        b->inflight=false;return b->last_result=KUI_VMU_BACKING_BUSY;
    }
    *entered=true;
    if(b->bus.reader_stream && b->bus.reader_stream->active && !*b->bus.reader_lease)
        return poison(b,KUI_VMU_BACKING_IO);
    if(b->bus.reader_lease && *b->bus.reader_lease) {
        int stopped=KUI_LOADER_SD_OK;
        if(b->bus.reader_stream)
            stopped=kui_loader_sd_stream_stop(b->bus.card,b->bus.reader_stream);
        b->bus.release(b->bus.context);*b->bus.reader_lease=false;
        if(stopped!=KUI_LOADER_SD_OK) {
            b->last_sd_result=stopped;return poison(b,KUI_VMU_BACKING_IO);
        }
    }
    struct kui_loader_sd *card=b->bus.card;
    if(!card->ready || card->slow || card->blocks!=b->identity.sectors ||
       card->high_capacity!=b->identity.high_capacity)
        return poison(b,KUI_VMU_BACKING_IDENTITY);
    b->last_sd_result=b->bus.acquire(b->bus.context);
    if(b->last_sd_result!=KUI_LOADER_SD_OK) return b->last_result=KUI_VMU_BACKING_BUSY;
    *acquired=true;
    return check_identity(b);
}
static int finish(struct kui_vmu_backing *b,bool entered,bool acquired,int result) {
    if(!entered) return result;
    if(acquired) {
        if(b->bus.healthy && !b->bus.healthy(b->bus.context) && !result)
            result=poison(b,KUI_VMU_BACKING_IO);
        b->bus.release(b->bus.context);
        /* Bus restoration itself can latch a hardware fault. Sample while
         * the shared lock/IRQ mask still belongs to us, after the lease is
         * returned, so a save ACK never hides a failed release. */
        if(b->bus.healthy && !b->bus.healthy(b->bus.context) && !result)
            result=poison(b,KUI_VMU_BACKING_IO);
    }
    b->inflight=false;b->bus.leave(b->bus.context);
    return b->last_result=result;
}
static int admission(struct kui_vmu_backing *b,bool writing) {
    if(!b || !b->initialized) return KUI_VMU_BACKING_ARGUMENT;
    if(b->poisoned) return b->last_result=KUI_VMU_BACKING_POISONED;
    if(writing) {
#if KUI_VMU_EXPERIMENTAL
        if(!b->allow_writes) return b->last_result=KUI_VMU_BACKING_READ_ONLY;
#else
        return b->last_result=KUI_VMU_BACKING_READ_ONLY;
#endif
    }
    return KUI_VMU_BACKING_OK;
}
struct call_scope {bool entered,acquired,reserved_call;};
static int call_begin(struct kui_vmu_backing *b,struct call_scope *scope) {
    *scope=(struct call_scope){0};
    if(!b->reserved) return begin(b,&scope->entered,&scope->acquired);
    if(b->callback_active) return b->last_result=KUI_VMU_BACKING_BUSY;
    b->callback_active=true;scope->reserved_call=true;
    return check_identity(b);
}
static int call_finish(struct kui_vmu_backing *b,const struct call_scope *scope,int result) {
    if(!scope->reserved_call) return finish(b,scope->entered,scope->acquired,result);
    if(b->bus.healthy && !b->bus.healthy(b->bus.context) && !result)
        result=poison(b,KUI_VMU_BACKING_IO);
    b->callback_active=false;
    return b->last_result=result;
}
int kui_vmu_backing_reserve(struct kui_vmu_backing *b) {
    int result=admission(b,true);
    if(result) return result;
    bool entered=false,acquired=false;result=begin(b,&entered,&acquired);
    if(result) return finish(b,entered,acquired,result);
    b->reserved=true;
    return b->last_result=KUI_VMU_BACKING_OK;
}
int kui_vmu_backing_unreserve(struct kui_vmu_backing *b) {
    if(!b || !b->initialized || !b->reserved) return KUI_VMU_BACKING_ARGUMENT;
    if(b->callback_active) return b->last_result=KUI_VMU_BACKING_BUSY;
    int result=b->poisoned?KUI_VMU_BACKING_POISONED:KUI_VMU_BACKING_OK;
    b->reserved=false;
    return finish(b,true,true,result);
}
int kui_vmu_backing_read(void *context,uint64_t lba,void *out) {
    struct kui_vmu_backing *b=context;int result=admission(b,false);
    if(result) return result;
    if(!out) return b->last_result=KUI_VMU_BACKING_ARGUMENT;
    if(!authorized(b,lba)) return b->last_result=KUI_VMU_BACKING_MAPPING;
    struct call_scope scope;result=call_begin(b,&scope);
    if(!result) {
        b->last_sd_result=kui_loader_sd_read(b->bus.card,(uint32_t)lba,1,out);
        if(b->last_sd_result!=KUI_LOADER_SD_OK) result=poison(b,KUI_VMU_BACKING_IO);
    }
    return call_finish(b,&scope,result);
}
int kui_vmu_backing_write(void *context,uint64_t lba,const void *in) {
    struct kui_vmu_backing *b=context;int result=admission(b,true);
    if(result) return result;
    if(!in) return b->last_result=KUI_VMU_BACKING_ARGUMENT;
    if(!authorized(b,lba)) return b->last_result=KUI_VMU_BACKING_MAPPING;
#if KUI_VMU_EXPERIMENTAL
    struct call_scope scope;result=call_begin(b,&scope);
    if(!result) {
        b->last_sd_result=kui_sci_sd_write(b->bus.card,(uint32_t)lba,1,in);
        if(b->last_sd_result!=KUI_LOADER_SD_OK) result=poison(b,KUI_VMU_BACKING_IO);
    }
    return call_finish(b,&scope,result);
#else
    return KUI_VMU_BACKING_READ_ONLY;
#endif
}
int kui_vmu_backing_sync(void *context) {
    struct kui_vmu_backing *b=context;int result=admission(b,true);
    if(result) return result;
#if KUI_VMU_EXPERIMENTAL
    struct call_scope scope;result=call_begin(b,&scope);
    if(!result) {
        b->last_sd_result=kui_sci_sd_sync(b->bus.card);
        if(b->last_sd_result!=KUI_LOADER_SD_OK) result=poison(b,KUI_VMU_BACKING_IO);
    }
    return call_finish(b,&scope,result);
#else
    return KUI_VMU_BACKING_READ_ONLY;
#endif
}
struct kui_vmu_store_io kui_vmu_backing_io(struct kui_vmu_backing *b) {
    return (struct kui_vmu_store_io){b,kui_vmu_backing_read,kui_vmu_backing_write,kui_vmu_backing_sync};
}
