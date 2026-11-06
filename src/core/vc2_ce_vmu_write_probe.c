/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_write_probe.h"
#if KUI_VC2_VMU_WRITE_PROBE || defined(KUI_VC2_VMU_WRITE_PROBE_TEST)
#include "kui/armada_ce_vmu_a2_publish.h"
#include "kui/vmu_image.h"
#include <string.h>

static bool probe_span(const void *p,size_t n) {
    return p && n<=UINTPTR_MAX-(uintptr_t)p;
}
static bool probe_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return an && bn && x<y+bn && y<x+an;
}
static void probe_inc(uint32_t *n) {if(*n!=UINT32_MAX)++*n;}
static uint32_t probe_le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint32_t probe_crc32(const uint8_t data[512]) {
    uint32_t crc=UINT32_MAX;
    for(unsigned i=0u;i<512u;++i) {
        crc^=data[i];
        for(unsigned bit=0u;bit<8u;++bit)
            crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
}
static int probe_read_blank(void *,uint16_t,uint8_t [512]);
static int probe_commit_pending(void *,uint16_t,const uint8_t [512]);
static bool probe_valid(const struct kui_vc2_ce_vmu_write_probe *s) {
    return probe_span(s,sizeof(*s)) && s->self==s &&
        s->magic==KUI_VC2_VMU_WRITE_PROBE_MAGIC && s->started==1u &&
        s->endpoint.self==&s->endpoint && s->endpoint.started==1u &&
        s->endpoint.protocol.initialized && s->endpoint.protocol.address==2u &&
        !s->endpoint.protocol.killed &&
        s->endpoint.protocol.io.context==&s->endpoint && s->endpoint.io.context==s &&
        s->endpoint.io.read_block==probe_read_blank &&
        s->endpoint.io.commit_block==probe_commit_pending &&
        !s->endpoint.commits && s->pending<=1u;
}
static int probe_read_blank(void *context,uint16_t block,uint8_t data[512]) {
    struct kui_vc2_ce_vmu_write_probe *s=context;
    if(!probe_valid(s) || s->handling!=1u || s->pending)
        return KUI_VMU_EMU_IO_ERROR;
    return kui_vmu_image_blank_block(block,data)?KUI_VMU_EMU_IO_ERROR:KUI_VMU_EMU_IO_OK;
}
static int probe_commit_pending(void *context,uint16_t block,const uint8_t data[512]) {
    struct kui_vc2_ce_vmu_write_probe *s=context;
    if(!probe_valid(s) || s->handling!=1u || s->pending ||
       !s->endpoint.protocol.active || s->endpoint.protocol.phases!=15u ||
       !s->endpoint.protocol.commit_attempted ||
       block!=s->endpoint.protocol.staged_block || data!=s->endpoint.protocol.staged)
        return KUI_VMU_EMU_IO_ERROR;
    /* BUSY is private and promises no acquired I/O. It lets the unchanged
     * engine validate phase4 and freeze its exact attempted transaction.
     * The wrapper then withholds the reply and the owner stops, so neither
     * AGAIN nor a durable ACK reaches the game for this transaction. */
    probe_inc(&s->commit_attempts);
    s->staged_crc=probe_crc32(data);s->pending=1u;
    return KUI_VMU_EMU_IO_BUSY;
}
int kui_vc2_ce_vmu_write_probe_init(struct kui_vc2_ce_vmu_write_probe *s) {
    if(!probe_span(s,sizeof(*s)))return KUI_ARMADA_CE_VMU_WRITE_ARGUMENT;
    if(s->started)return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;
    memset(s,0,sizeof(*s));
    const struct kui_vmu_emulation_io io={s,probe_read_blank,probe_commit_pending};
    if(kui_armada_ce_vmu_write_init(&s->endpoint,&io))
        return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;
    s->self=s;s->magic=KUI_VC2_VMU_WRITE_PROBE_MAGIC;s->started=1u;return 0;
}
static bool probe_sync(const uint8_t *p,size_t n,uint16_t block) {
    return n==12u && p[0]==13u && p[1]==2u && !p[2] && p[3]==2u &&
        probe_le32(p+4u)==KUI_VMU_EMU_FUNC_MEMCARD && !p[8] && p[9]==4u &&
        !p[10] && p[11]==block;
}
static void probe_identity(unsigned command,uint8_t *response) {
    /* Preserve the prior successful blank-card INFO/ALLINFO bytes. The card
     * remains read-only at the durable boundary despite volatile phase ACKs. */
    static const char name[]="K-UI Read-Only Blank";
    static const char version[]="K-UI read-only synthetic blank card; no persistence";
    memset(response+22u,' ',30u);memcpy(response+22u,name,sizeof(name)-1u);
    if(command==2u) {
        memset(response+116u,' ',80u);memcpy(response+116u,version,sizeof(version)-1u);
    }
}
int kui_vc2_ce_vmu_write_probe_handle(struct kui_vc2_ce_vmu_write_probe *s,
    const uint8_t *request,size_t request_bytes,uint8_t *response,size_t capacity,size_t *produced) {
    if(!probe_span(s,sizeof(*s)) || !probe_span(request,request_bytes) ||
       !probe_span(response,capacity) || !probe_span(produced,sizeof(*produced)) ||
       probe_overlap(s,sizeof(*s),request,request_bytes) ||
       probe_overlap(s,sizeof(*s),response,capacity) ||
       probe_overlap(s,sizeof(*s),produced,sizeof(*produced)) ||
       probe_overlap(request,request_bytes,response,capacity) ||
       probe_overlap(request,request_bytes,produced,sizeof(*produced)) ||
       probe_overlap(response,capacity,produced,sizeof(*produced)))
        return KUI_ARMADA_CE_VMU_WRITE_ARGUMENT;
    if(!probe_valid(s))return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;
    if(s->handling) {*produced=0u;return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;}
    if(s->pending) {
        *produced=0u;
        if(probe_sync(request,request_bytes,s->endpoint.protocol.staged_block))
            return capacity<8u?KUI_ARMADA_CE_VMU_WRITE_CAPACITY:
                KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED;
        probe_inc(&s->refused);return KUI_ARMADA_CE_VMU_WRITE_REFUSED;
    }
    s->handling=1u;
    int result=kui_armada_ce_vmu_write_handle(&s->endpoint,request,request_bytes,response,capacity,produced);
    s->handling=0u;
    /* A capacity refusal must preserve the entire probe just as the reused
     * adapter preserves its endpoint; record only fully admitted calls. */
    if(result==KUI_ARMADA_CE_VMU_WRITE_CAPACITY || result==KUI_ARMADA_CE_VMU_WRITE_ARGUMENT)
        return result;
    s->last_header=request_bytes>=4u?probe_le32(request):0u;
    s->last_location=request_bytes>=12u?probe_le32(request+8u):0u;
    s->last_capacity=(uint32_t)capacity;
#if SIZE_MAX > UINT32_MAX
    if(capacity>UINT32_MAX)s->last_capacity=UINT32_MAX;
#endif
    if(s->pending) {
        if(result==KUI_ARMADA_CE_VMU_WRITE_HANDLED && *produced==4u &&
           probe_le32(response)==UINT32_C(0x000200fc) &&
           probe_sync(request,request_bytes,s->endpoint.protocol.staged_block) &&
           s->endpoint.protocol.active && s->endpoint.protocol.phases==15u &&
           s->endpoint.protocol.commit_attempted) {
            *produced=0u;return KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED;
        }
        *produced=0u;return KUI_ARMADA_CE_VMU_WRITE_INTERNAL;
    }
    if(result==KUI_ARMADA_CE_VMU_WRITE_HANDLED && *produced==4u &&
       probe_le32(response)==UINT32_C(0x00020007) && request_bytes==140u &&
       request[0]==12u && request[9]<4u)probe_inc(&s->phase_acks[request[9]]);
    if(result==KUI_ARMADA_CE_VMU_WRITE_HANDLED && request_bytes==4u &&
       (request[0]==1u || request[0]==2u))probe_identity(request[0],response);
    return result;
}
bool kui_vc2_ce_vmu_write_probe_pending(const struct kui_vc2_ce_vmu_write_probe *s) {
    return probe_valid(s) && s->pending==1u && s->endpoint.protocol.active &&
        s->endpoint.protocol.phases==15u && s->endpoint.protocol.commit_attempted &&
        s->commit_attempts==1u;
}
void kui_vc2_ce_vmu_write_probe_snapshot(const struct kui_vc2_ce_vmu_write_probe *s,
    uint32_t out[KUI_VC2_VMU_WRITE_PROBE_WORDS]) {
    if(!probe_span(out,KUI_VC2_VMU_WRITE_PROBE_WORDS*4u) ||
       (s && probe_overlap(s,sizeof(*s),out,KUI_VC2_VMU_WRITE_PROBE_WORDS*4u)))return;
    memset(out,0,KUI_VC2_VMU_WRITE_PROBE_WORDS*4u);
    if(!probe_valid(s))return;
    out[0]=s->magic;out[1]=s->started;out[2]=s->endpoint.requests;out[3]=s->endpoint.reads;
    out[4]=s->endpoint.write_phases;out[5]=s->commit_attempts;
    out[6]=s->endpoint.protocol.staged_block;out[7]=s->endpoint.protocol.phases;
    out[8]=s->staged_crc;out[9]=s->pending;
    for(unsigned i=0u;i<4u;++i)out[10u+i]=s->phase_acks[i];
    out[14]=s->endpoint.commits;out[15]=s->endpoint.busy;
    out[16]=s->endpoint.refused;
    if(UINT32_MAX-out[16]<s->refused)out[16]=UINT32_MAX;else out[16]+=s->refused;
    out[17]=s->last_header;out[18]=s->last_location;out[19]=s->last_capacity;
}
#endif
