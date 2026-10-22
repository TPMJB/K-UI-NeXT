/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_write.h"
#include <string.h>

#define CE_WRITE_MAGIC UINT32_C(0x31574341) /* ACW1 */
static void ce_write_inc(uint32_t *n) {if(*n!=UINT32_MAX) ++*n;}
static int ce_write_span(const void *p,size_t n) {
    return p && n<=UINTPTR_MAX-(uintptr_t)p;
}
static int ce_write_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && x<y+bn && y<x+an;
}
static int ce_write_read(void *context,uint16_t block,uint8_t out[512]) {
    struct kui_armada_ce_vmu_write *s=context;
    int result=s->io.read_block(s->io.context,block,out);
    if(result==KUI_VMU_EMU_IO_OK) ce_write_inc(&s->reads);
    else if(result==KUI_VMU_EMU_IO_BUSY) ce_write_inc(&s->busy);
    else ce_write_inc(&s->io_errors);
    return result;
}
static int ce_write_commit(void *context,uint16_t block,const uint8_t data[512]) {
    struct kui_armada_ce_vmu_write *s=context;
    int result=s->io.commit_block(s->io.context,block,data);
    if(result==KUI_VMU_EMU_IO_OK) ce_write_inc(&s->commits);
    else if(result==KUI_VMU_EMU_IO_BUSY) ce_write_inc(&s->busy);
    else ce_write_inc(&s->io_errors);
    return result;
}
int kui_armada_ce_vmu_write_init(struct kui_armada_ce_vmu_write *s,
    const struct kui_vmu_emulation_io *io) {
    if(!ce_write_span(s,sizeof(*s)) || !ce_write_span(io,sizeof(*io)) ||
       ce_write_overlap(s,sizeof(*s),io,sizeof(*io)) ||
       !io->read_block || !io->commit_block)
        return KUI_ARMADA_CE_VMU_WRITE_ARGUMENT;
    if(s->started) return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;
    memset(s,0,sizeof(*s));s->io=*io;
    const struct kui_vmu_emulation_io wrapped={s,ce_write_read,ce_write_commit};
    if(kui_vmu_emulation_init(&s->protocol,2u,&wrapped))
        return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;
    s->self=s;s->magic=CE_WRITE_MAGIC;s->started=1u;return 0;
}
static unsigned ce_write_shape(const uint8_t *p,size_t bytes) {
    unsigned command=p[0];
    if(command==1u || command==2u)
        return bytes==4u && !p[3]?(command==1u?116u:196u):0u;
    if(bytes!=(command==12u?140u:12u) ||
       p[3]!=(command==12u?34u:2u) ||
       memcmp(p+4,"\0\0\0\2",4u) || p[8] || p[10]) return 0u;
    unsigned phase=p[9];
    if(command==10u) return !phase && !p[11]?32u:0u;
    if(command==11u) return !phase?524u:0u;
    if(command==12u) return phase<4u?8u:0u;
    if(command==13u) return phase<=1u?4u:phase==4u?8u:0u;
    return 0u;
}
int kui_armada_ce_vmu_write_handle(struct kui_armada_ce_vmu_write *s,
    const uint8_t *request,size_t request_bytes,uint8_t *response,
    size_t capacity,size_t *response_bytes) {
    if(!ce_write_span(s,sizeof(*s)) || !ce_write_span(request,request_bytes) ||
       !ce_write_span(response,capacity) || !ce_write_span(response_bytes,sizeof(*response_bytes)) ||
       ce_write_overlap(s,sizeof(*s),request,request_bytes) ||
       ce_write_overlap(s,sizeof(*s),response,capacity) ||
       ce_write_overlap(s,sizeof(*s),response_bytes,sizeof(*response_bytes)) ||
       ce_write_overlap(request,request_bytes,response,capacity) ||
       ce_write_overlap(request,request_bytes,response_bytes,sizeof(*response_bytes)) ||
       ce_write_overlap(response,capacity,response_bytes,sizeof(*response_bytes)))
        return KUI_ARMADA_CE_VMU_WRITE_ARGUMENT;
    if(s->self!=s || s->magic!=CE_WRITE_MAGIC || s->started!=1u ||
       !s->protocol.initialized || s->protocol.address!=2u || s->protocol.killed ||
       s->protocol.io.context!=s || s->protocol.io.read_block!=ce_write_read ||
       s->protocol.io.commit_block!=ce_write_commit || !s->io.read_block || !s->io.commit_block)
        return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;
    *response_bytes=0;
    if(s->handling) return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;
    if(request_bytes<3u || request[1]!=2u || request[2]) {
        ce_write_inc(&s->refused);return KUI_ARMADA_CE_VMU_WRITE_REFUSED;
    }
    unsigned command=request[0];
    unsigned needed=request_bytes>=4u?ce_write_shape(request,request_bytes):0u;
    if(!needed) {
        if(command!=1u && command!=2u && command!=10u) s->protocol.read_valid=0;
        ce_write_inc(&s->refused);return KUI_ARMADA_CE_VMU_WRITE_REFUSED;
    }
    if(capacity<needed) return KUI_ARMADA_CE_VMU_WRITE_CAPACITY;
    if(command==13u && request[9]<=1u) {
        if(s->protocol.active || !s->protocol.read_valid ||
           s->protocol.read_block!=request[11]) {
            s->protocol.read_valid=0;ce_write_inc(&s->refused);
            return KUI_ARMADA_CE_VMU_WRITE_REFUSED;
        }
        response[0]=7u;response[1]=0u;response[2]=2u;response[3]=0u;
        *response_bytes=4u;ce_write_inc(&s->requests);ce_write_inc(&s->read_finalizations);
        return KUI_ARMADA_CE_VMU_WRITE_HANDLED;
    }
    if(command==11u || command==12u || command==13u) s->protocol.read_valid=0;
    s->handling=1u;
    int result=kui_vmu_emulation_handle(&s->protocol,request,request_bytes,
        response,capacity,response_bytes);
    s->handling=0u;
    if(result!=1) {*response_bytes=0;return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;}
    if(command==12u && response[0]==7u) ce_write_inc(&s->write_phases);
    ce_write_inc(&s->requests);return KUI_ARMADA_CE_VMU_WRITE_HANDLED;
}
