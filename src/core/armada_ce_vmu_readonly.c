/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_readonly.h"
#include <string.h>

#define CE_READONLY_MAGIC UINT32_C(0x31524f43)

static void ce_readonly_increment(uint32_t *n) {if(*n!=UINT32_MAX) ++*n;}
static int ce_readonly_span(const void *p,size_t n) {
    return p && n<=UINTPTR_MAX-(uintptr_t)p;
}
static int ce_readonly_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t ap=(uintptr_t)a,bp=(uintptr_t)b;
    return an && bn && ap<bp+bn && bp<ap+an;
}
static int ce_readonly_refused(struct kui_armada_ce_vmu_readonly *s) {
    ce_readonly_increment(&s->refused);return KUI_VMU_READONLY_REFUSED;
}
int kui_armada_ce_vmu_readonly_init(struct kui_armada_ce_vmu_readonly *s) {
    if(!s) return KUI_VMU_READONLY_ARGUMENT;
    memset(s,0,sizeof(*s));
    int result=kui_vmu_readonly_init(&s->backend);
    if(result) return result;
    s->self=s;s->magic=CE_READONLY_MAGIC;return 0;
}
#if KUI_ARMADA_CE_VMU_READONLY_HOST_TEST
int kui_armada_ce_vmu_readonly_init_reader(struct kui_armada_ce_vmu_readonly *s,
    int (*reader)(void *,uint16_t,uint8_t[512]),void *context) {
    if(!reader) return KUI_VMU_READONLY_ARGUMENT;
    int result=kui_armada_ce_vmu_readonly_init(s);if(result) return result;
    return kui_vmu_readonly_init_reader(&s->backend,reader,context);
}
#endif
int kui_armada_ce_vmu_readonly_handle(struct kui_armada_ce_vmu_readonly *s,
    const uint8_t *request,size_t request_bytes,
    uint8_t *response,size_t capacity,size_t *response_bytes) {
    if(!s || !ce_readonly_span(s,sizeof(*s)) ||
       !ce_readonly_span(request,request_bytes) || !ce_readonly_span(response,capacity) ||
       !ce_readonly_span(response_bytes,sizeof(*response_bytes)) ||
       ce_readonly_overlap(s,sizeof(*s),request,request_bytes) ||
       ce_readonly_overlap(s,sizeof(*s),response,capacity) ||
       ce_readonly_overlap(s,sizeof(*s),response_bytes,sizeof(*response_bytes)) ||
       ce_readonly_overlap(request,request_bytes,response,capacity) ||
       ce_readonly_overlap(request,request_bytes,response_bytes,sizeof(*response_bytes)) ||
       ce_readonly_overlap(response,capacity,response_bytes,sizeof(*response_bytes)))
        return KUI_VMU_READONLY_ARGUMENT;
    *response_bytes=0;
    if(s->self!=s || s->magic!=CE_READONLY_MAGIC || !s->backend.ready)
        return KUI_VMU_READONLY_ARGUMENT;
    if(s->faulted) return KUI_VMU_READONLY_INTERNAL;
#if !KUI_ARMADA_CE_VMU_READONLY_HOST_TEST
    /* The console experiment has no external reader callback. Retained
     * generic read_blank code must never enter its optional-reader branch. */
    if(s->backend.reader || s->backend.reader_context) {
        s->faulted=1u;s->read_valid=0u;return KUI_VMU_READONLY_INTERNAL;
    }
#endif
    if(request_bytes<3u || request[1]!=2u || request[2]!=0u)
        return ce_readonly_refused(s);
    unsigned command=request[0];
    if(command==13u) {
        if(request_bytes!=12u || request[3]!=2u ||
           memcmp(request+4,"\0\0\0\2",4u) || request[8] || request[9]>1u ||
           request[10] || !s->read_valid || request[11]!=s->read_block) {
            s->read_valid=0;return ce_readonly_refused(s);
        }
        if(capacity<4u) return KUI_VMU_READONLY_CAPACITY;
        response[0]=7u;response[1]=0u;response[2]=2u;response[3]=0u;
        *response_bytes=4u;ce_readonly_increment(&s->requests);
        ce_readonly_increment(&s->read_finalizations);
        return KUI_VMU_READONLY_HANDLED;
    }
    /* An unsuccessful new read must not leave a previous success available
     * to acknowledge. Rejected mutation commands also revoke that evidence. */
    if(command==11u || (command!=1u && command!=2u && command!=10u))
        s->read_valid=0;
    int result=kui_vmu_readonly_handle(&s->backend,request,request_bytes,
        response,capacity,response_bytes);
    if(result==KUI_VMU_READONLY_HANDLED) {
        ce_readonly_increment(&s->requests);
        if(command==11u) {
            s->read_block=request[11];s->read_valid=1u;
            ce_readonly_increment(&s->reads);
        }
    } else if(result==KUI_VMU_READONLY_REFUSED) ce_readonly_increment(&s->refused);
    else if(result==KUI_VMU_READONLY_INTERNAL) {s->faulted=1u;s->read_valid=0u;}
    return result;
}
