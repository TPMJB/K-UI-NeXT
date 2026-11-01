/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_vmu_storage.h"
#include "kui/storage.h"
#include <limits.h>
#include <string.h>

#define BOUND UINT32_C(0x44564d31)

static bool disjoint(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an<=UINTPTR_MAX-x && bn<=UINTPTR_MAX-y &&
        (x+an<=y || y+bn<=x);
}
static bool zeroed(const void *data,size_t bytes) {
    const unsigned char *p=data;
    for(size_t i=0u;i<bytes;i++) if(p[i]) return false;
    return true;
}
static bool cid_valid(const uint8_t cid[16]) {
    unsigned any=0u,not_ff=0u;
    for(unsigned i=0u;i<16u;i++) {any|=cid[i];not_ff|=(unsigned)(cid[i]^255u);}
    return any && not_ff;
}
static bool card_valid(const struct kui_loader_sd *card) {
    return card->ready && !card->slow && card->blocks &&
        card->blocks<=UINT64_C(0x100000000) &&
        (card->high_capacity || card->blocks<=UINT64_C(0x800000)) && card->bus.begin &&
        card->bus.end && card->bus.select && card->bus.transfer && card->bus.ticks;
}
static bool same_card(const struct kui_loader_sd *a,const struct kui_loader_sd *b) {
    return a->blocks==b->blocks && a->high_capacity==b->high_capacity &&
        a->slow==b->slow && a->bus.ctx==b->bus.ctx &&
        a->bus.begin==b->bus.begin && a->bus.end==b->bus.end &&
        a->bus.select==b->bus.select && a->bus.transfer==b->bus.transfer &&
        a->bus.ticks==b->bus.ticks && a->bus.transfer_block==b->bus.transfer_block;
}
static bool same_ops(const struct kui_disc_vmu_storage_ops *a,
    const struct kui_disc_vmu_storage_ops *b) {
    return a->current_sr==b->current_sr && a->stream_active==b->stream_active &&
        a->acquire==b->acquire && a->release==b->release &&
        a->healthy==b->healthy && a->faulted==b->faulted;
}
static bool same_config(const struct kui_disc_vmu_storage_config *a,
    const struct kui_disc_vmu_storage_config *b) {
    return a->transport==b->transport && !memcmp(a->cid,b->cid,sizeof(a->cid)) &&
        a->transaction==b->transaction && a->transaction_context==b->transaction_context &&
        a->card.ready==b->card.ready && same_card(&a->card,&b->card);
}
static void count(uint32_t *value) {if(*value!=UINT32_MAX) ++*value;}
static int fault(struct kui_disc_vmu_storage *owner) {
    if(!owner->fault) count(&owner->faults);
    owner->fault=true;owner->busy=false;
    return KUI_VMU_SD_LEASE_FAULT;
}
static int busy(struct kui_disc_vmu_storage *owner) {
    count(&owner->busy_refusals);owner->busy=false;
    return KUI_VMU_SD_LEASE_BUSY;
}
bool kui_disc_vmu_storage_bind(struct kui_disc_vmu_storage *owner,
    const struct kui_disc_vmu_storage_config *config,
    const struct kui_disc_vmu_storage_ops *ops,void *platform_context) {
    if(!owner || !config || !ops ||
       !disjoint(owner,sizeof(*owner),config,sizeof(*config)) ||
       !disjoint(owner,sizeof(*owner),ops,sizeof(*ops)) ||
       !zeroed(owner,sizeof(*owner)) ||
       (config->transport!=KUI_STORAGE_SCI && config->transport!=KUI_STORAGE_SCIF) ||
       !cid_valid(config->cid) || !card_valid(&config->card) || !config->transaction ||
       !ops->current_sr || !ops->stream_active || !ops->acquire || !ops->release ||
       !ops->healthy || !ops->faulted ||
       (ops->current_sr(platform_context)&UINT32_C(0xf0))!=UINT32_C(0xf0)) return false;
    struct kui_disc_vmu_storage admitted={0};
    admitted.self=(uintptr_t)owner;admitted.state=BOUND;admitted.config=*config;
    admitted.ops=*ops;admitted.platform_context=platform_context;
    *owner=admitted;
    return true;
}
int kui_disc_vmu_storage_loan(struct kui_disc_vmu_storage *owner,
    kui_vmu_sd_transaction transaction,void *transaction_context) {
    if(!owner || owner->self!=(uintptr_t)owner || owner->state!=BOUND ||
       transaction!=owner->config.transaction || transaction_context!=owner->config.transaction_context ||
       !transaction || owner->fault) return KUI_VMU_SD_LEASE_FAULT;
    /* Reentry never clears the outer request's ownership latch. */
    if(owner->busy) {count(&owner->busy_refusals);return KUI_VMU_SD_LEASE_BUSY;}
    const struct kui_disc_vmu_storage_ops ops=owner->ops;
    const struct kui_disc_vmu_storage_config config=owner->config;
    void *const platform_context=owner->platform_context;
    if(!ops.current_sr || !ops.stream_active || !ops.acquire || !ops.release ||
       !ops.healthy || !ops.faulted ||
       (ops.current_sr(platform_context)&UINT32_C(0xf0))!=UINT32_C(0xf0))
        return KUI_VMU_SD_LEASE_FAULT;
    if(!card_valid(&config.card) || !cid_valid(config.cid) ||
       (config.transport!=KUI_STORAGE_SCI && config.transport!=KUI_STORAGE_SCIF)) return fault(owner);
    owner->busy=true;
    if(ops.faulted(platform_context)) return fault(owner);
    if(ops.stream_active(platform_context)) return busy(owner);
    enum kui_loader_sd_result acquired=ops.acquire(platform_context);
    if(acquired!=KUI_LOADER_SD_OK) {
        if((acquired==KUI_LOADER_SD_NOT_READY || acquired==KUI_LOADER_SD_UNSUPPORTED) &&
           !ops.faulted(platform_context)) return busy(owner);
        return fault(owner);
    }
    count(&owner->acquired);
    struct kui_loader_sd loan=config.card;
    bool valid=ops.healthy(platform_context) && !ops.faulted(platform_context);
    int result=KUI_VMU_SD_LEASE_FAULT;
    if(valid) result=transaction(transaction_context,&loan);
    owner->last_transaction_result=result;
    owner->last_command=loan.last_command;owner->last_response=loan.last_response;
    valid=valid && result==0 && loan.ready && same_card(&loan,&config.card);
    /* Use the original release callback even if the transaction changed a
     * descriptor or resident metadata. A successful acquire has one release. */
    bool healthy=ops.healthy(platform_context),port_fault=ops.faulted(platform_context);
    ops.release(platform_context);
    bool restored=!ops.faulted(platform_context);
    valid=valid && healthy && !port_fault && restored && owner->self==(uintptr_t)owner &&
        owner->state==BOUND && !owner->fault && owner->busy &&
        owner->platform_context==platform_context && same_ops(&owner->ops,&ops) &&
        same_config(&owner->config,&config) &&
        (ops.current_sr(platform_context)&UINT32_C(0xf0))==UINT32_C(0xf0);
    if(!valid) return fault(owner);
    count(&owner->completed);owner->busy=false;
    return KUI_VMU_SD_LEASE_OK;
}
