/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_readonly.h"
#include "kui/vmu_image.h"
#include <string.h>

static void increment(uint32_t *counter) {
    if(*counter!=UINT32_MAX) ++*counter;
}

static int read_blank(void *context,uint16_t block,uint8_t data[512]) {
    struct kui_vmu_readonly *s=context;
    if(!s || !s->ready || s->faulted ||
       kui_vmu_image_blank_block(block,data)!=KUI_VMU_IMAGE_OK)
        return KUI_VMU_EMU_IO_ERROR;
    increment(&s->reads);
    return KUI_VMU_EMU_IO_OK;
}

static int commit_refused(void *context,uint16_t block,const uint8_t data[512]) {
    struct kui_vmu_readonly *s=context;
    (void)block;(void)data;
    if(s) {increment(&s->commit_attempts);s->faulted=1u;}
    return KUI_VMU_EMU_IO_ERROR;
}

int kui_vmu_readonly_init(struct kui_vmu_readonly *s) {
    if(!s) return KUI_VMU_READONLY_ARGUMENT;
    memset(s,0,sizeof(*s));
    const struct kui_vmu_emulation_io io={s,read_blank,commit_refused};
    if(kui_vmu_emulation_init(&s->protocol,KUI_VMU_READONLY_ADDRESS,&io))
        return KUI_VMU_READONLY_INTERNAL;
    s->ready=1u;
    return 0;
}

int kui_vmu_readonly_handle(struct kui_vmu_readonly *s,
    const uint8_t *request,size_t request_bytes,
    uint8_t *response,size_t response_capacity,size_t *response_bytes) {
    size_t needed;
    unsigned command;
    if(response_bytes) *response_bytes=0;
    if(!s || !s->ready || !request || !response || !response_bytes)
        return KUI_VMU_READONLY_ARGUMENT;
    if(request_bytes<4u) goto refused;
    command=request[0];
    if(request[1]!=KUI_VMU_READONLY_ADDRESS || request[2]!=0u) goto refused;
    if(command==1u || command==2u) {
        if(request_bytes!=4u || request[3]!=0u) goto refused;
        needed=command==1u?116u:196u;
    } else if(command==10u || command==11u) {
        if(request_bytes!=12u || request[3]!=2u ||
           memcmp(request+4,"\0\0\0\2",4u) ||
           request[8]!=0u || request[9]!=0u || request[10]!=0u ||
           (command==10u && request[11]!=0u)) goto refused;
        needed=command==10u?32u:KUI_VMU_READONLY_MAX_RESPONSE_BYTES;
    } else goto refused;
    if(s->faulted || s->commit_attempts) return KUI_VMU_READONLY_INTERNAL;
    if(response_capacity<needed) return KUI_VMU_READONLY_CAPACITY;
    int result=kui_vmu_emulation_handle(&s->protocol,request,request_bytes,
        response,response_capacity,response_bytes);
    if(s->faulted || s->commit_attempts || result!=1 || *response_bytes!=needed) {
        s->faulted=1u;*response_bytes=0;
        return KUI_VMU_READONLY_INTERNAL;
    }
    if(command==1u || command==2u) {
        static const char name[]="K-UI Read-Only Blank";
        _Static_assert(sizeof(name)-1u<=30u,"Read-only device name length");
        memset(response+22u,' ',30u);
        memcpy(response+22u,name,sizeof(name)-1u);
    }
    if(command==2u) {
        static const char version[]="K-UI read-only synthetic blank card; no persistence";
        _Static_assert(sizeof(version)-1u<=80u,"Read-only version field length");
        /* Extended-info version text follows the112-byte standard payload. */
        memset(response+116u,' ',80u);
        memcpy(response+116u,version,sizeof(version)-1u);
    }
    increment(&s->requests);
    return KUI_VMU_READONLY_HANDLED;
refused:
    increment(&s->refused);
    return KUI_VMU_READONLY_REFUSED;
}
