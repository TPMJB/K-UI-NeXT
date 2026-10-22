/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_service.h"

_Static_assert(sizeof(struct kui_vmu_service)<=4096u,"VMU service bounded RAM");

static int storage_fault(struct kui_vmu_service *s) {
    s->faulted=true;
    s->store.ready=0;
    return KUI_VMU_EMU_IO_ERROR;
}
static int reserve(struct kui_vmu_service *s) {
    if(s->faulted) return KUI_VMU_EMU_IO_ERROR;
    s->last_backing_result=kui_vmu_backing_reserve(&s->backing);
    if(s->last_backing_result==KUI_VMU_BACKING_BUSY) return KUI_VMU_EMU_IO_BUSY;
    return s->last_backing_result?storage_fault(s):KUI_VMU_EMU_IO_OK;
}
static int complete(struct kui_vmu_service *s,int store_result) {
    /* Preserve the first backing error; unreserve may return the subsequent
     * POISONED state instead of the SD/identity failure which caused it. */
    int backing_result=s->backing.last_result;
    int released=kui_vmu_backing_unreserve(&s->backing);
    s->last_backing_result=backing_result?backing_result:released;
    s->last_store_result=store_result;
    return store_result || released?storage_fault(s):KUI_VMU_EMU_IO_OK;
}
static int read_block(void *context,uint16_t block,uint8_t data[512]) {
    struct kui_vmu_service *s=context;
    int result=reserve(s);
    if(result) return result;
    return complete(s,kui_vmu_store_read(&s->store,block,data));
}
static int commit_block(void *context,uint16_t block,const uint8_t data[512]) {
    struct kui_vmu_service *s=context;
    int result=reserve(s);
    if(result) return result;
    return complete(s,kui_vmu_store_write(&s->store,block,data));
}
int kui_vmu_service_init(struct kui_vmu_service *s,
    const struct kui_vmu_backing_bus *bus,const struct kui_vmu_backing_config *config,
    uint8_t address) {
    if(!s || !bus || !config || !bus->healthy || !config->allow_writes)
        return KUI_VMU_SERVICE_ARGUMENT;
    if(s->faulted) return KUI_VMU_SERVICE_BLOCKED;
    if(s->ready || s->handling) return KUI_VMU_SERVICE_BLOCKED;
    s->handling=true;
    int result=KUI_VMU_SERVICE_ARGUMENT;
    const struct kui_vmu_emulation_io protocol_io={s,read_block,commit_block};
    if(kui_vmu_emulation_init(&s->protocol,address,&protocol_io))
        goto done;
    s->last_store_result=KUI_VMU_STORE_OK;
    s->last_backing_result=kui_vmu_backing_init(&s->backing,bus,config);
    if(s->last_backing_result) goto done;
    int acquired=reserve(s);
    if(acquired==KUI_VMU_EMU_IO_BUSY) {
        result=KUI_VMU_SERVICE_BUSY;goto done;
    }
    if(acquired) {result=KUI_VMU_SERVICE_STORAGE;goto done;}
    const struct kui_vmu_store_io store_io=kui_vmu_backing_io(&s->backing);
    int opened=kui_vmu_store_open(&s->store,&store_io,config->store);
    if(complete(s,opened)) {result=KUI_VMU_SERVICE_STORAGE;goto done;}
    s->ready=true;
    result=KUI_VMU_SERVICE_OK;
done:
    s->handling=false;
    return result;
}
int kui_vmu_service_handle(struct kui_vmu_service *s,const uint8_t *request,
    size_t request_bytes,uint8_t *response,size_t response_capacity,size_t *response_bytes) {
    if(response_bytes) *response_bytes=0;
    if(!s || !request || !response || !response_bytes) return -1;
    if(!s->ready) return KUI_VMU_SERVICE_HANDLE_NOT_READY;
    if(s->handling) return KUI_VMU_SERVICE_HANDLE_REENTRANT;
    s->handling=true;
    int result=kui_vmu_emulation_handle(&s->protocol,request,request_bytes,
        response,response_capacity,response_bytes);
    s->handling=false;
    return result;
}
