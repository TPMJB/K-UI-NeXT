/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_a2_publish.h"

static uint32_t a2_publish_inc(uint32_t n) {return n==UINT32_MAX?n:n+1u;}
static uint32_t a2_publish_le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool a2_publish_pointer(const void *p,size_t n) {
    return p && n && n<=UINTPTR_MAX-(uintptr_t)p;
}
static bool a2_publish_alias(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t x=(uintptr_t)a,y=(uintptr_t)b;
    return x<y+bn && y<x+an;
}
static bool a2_publish_ram(uint32_t a,uint32_t n) {
    return !(a&3u) && !(n&3u) && n && a>=0x0c010000u &&
        a<0x0d000000u && n<=0x0d000000u-a;
}
static bool a2_publish_overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    return a<b+bn && b<a+an;
}
static bool a2_publish_slice(uint32_t at,uint32_t n,uint32_t pool,uint32_t bytes) {
    return !(at&31u) && n>=4u && n<=1020u && !(n&3u) &&
        at>=pool && at<pool+bytes && n<=pool+bytes-at;
}
static enum kui_armada_ce_vmu_a2_publish_result a2_publish_fail(
    struct kui_armada_ce_vmu_a2_publish_snapshot *out,
    enum kui_armada_ce_vmu_a2_publish_result why,uint32_t at,uint32_t actual,
    uint32_t expected,uint32_t detail) {
    out->status[0]=a2_publish_inc(out->status[0]);
    out->status[3]|=1u<<why;
    if(!out->fault[0]) {
        out->fault[0]=(uint32_t)why;out->fault[1]=at;out->fault[2]=actual;
        out->fault[3]=expected;out->fault[4]=detail;
    }
    return why;
}
static unsigned a2_publish_parent_bytes(uint32_t request) {
    if(((request>>8)&0xffffu)!=0x20u) return 0;
    unsigned cmd=request&255u,words=request>>24;
    if(cmd==1u || cmd==2u) return words?0u:cmd==1u?116u:196u;
    return cmd==9u && words==1u?16u:0u;
}
static unsigned a2_publish_target_bytes(const uint8_t *p,unsigned n) {
    if(p[1]!=2u || p[2]) return 0;
    unsigned cmd=p[0];
    if(cmd==1u || cmd==2u) return n==4u && !p[3]?(cmd==1u?116u:196u):0u;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    if(cmd==12u) return n==140u && p[3]==34u && a2_publish_le32(p+4u)==0x02000000u &&
        !p[8] && p[9]<4u && !p[10]?4u:0u;
#endif
    if(n!=12u || p[3]!=2u || a2_publish_le32(p+4u)!=0x02000000u ||
       p[8] || p[10]) return 0;
    if(cmd==10u) return !p[9] && !p[11]?32u:0u;
    if(cmd==11u) return !p[9]?524u:0u;
    if(cmd==13u) return p[9]<=1u
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
        || p[9]==4u
#endif
        ?4u:0u;
    return 0;
}
static bool a2_publish_reply(const uint8_t *p,unsigned n,const uint8_t *request) {
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    unsigned command=request[0];
    if(n<4u || p[1] || p[2]!=2u || 4u+4u*p[3]!=n) return false;
    if(command==11u || command==12u || command==13u) {
        if(p[0]==0xfcu) return n==4u;
        if(p[0]==0xfbu) return n==8u && !p[4] && !p[5] && !p[6] &&
            (p[7]==1u || p[7]==2u || p[7]==4u || p[7]==8u || p[7]==16u);
    }
    unsigned expected=command==1u?5u:command==2u?6u:command==12u || command==13u?7u:8u;
    if(p[0]!=expected) return false;
    if(command==12u || command==13u) return n==4u;
    if(n!=(command==1u?116u:command==2u?196u:command==10u?32u:524u) ||
       a2_publish_le32(p+4u)!=0x02000000u) return false;
    return command!=11u || a2_publish_le32(p+8u)==a2_publish_le32(request+8u);
#else
    unsigned command=request[0],expected=command==1u?5u:command==2u?6u:command==13u?7u:8u;
    if(p[0]!=expected || p[1] || p[2]!=2u || 4u+4u*p[3]!=n) return false;
    if(command==13u) return n==4u;
    if(a2_publish_le32(p+4u)!=0x02000000u) return false;
    return command!=11u || a2_publish_le32(p+8u)==a2_publish_le32(request+8u);
#endif
}
static bool a2_publish_store(kui_armada_ce_vmu_a2_store32 store,void *context,
    struct kui_armada_ce_vmu_a2_publish_snapshot *out,uint32_t bus,uint32_t value) {
    out->status[4]=a2_publish_inc(out->status[4]);
    if(!store(context,bus|0xa0000000u,value)) return false;
    out->status[2]=a2_publish_inc(out->status[2]);return true;
}
enum kui_armada_ce_vmu_a2_publish_result kui_armada_ce_vmu_a2_publish(
    const struct kui_armada_ce_vmu_a2_input *in,kui_armada_ce_vmu_a2_backend backend,
    void *backend_context,kui_armada_ce_vmu_a2_admit admit,void *admit_context,
    kui_armada_ce_vmu_a2_store32 store,void *store_context,
    struct kui_armada_ce_vmu_a2_publish_scratch *work,
    struct kui_armada_ce_vmu_a2_publish_snapshot *out) {
    if(!a2_publish_pointer(in,sizeof(*in)) || !store || !admit ||
       !a2_publish_pointer(work,sizeof(*work)) || !a2_publish_pointer(out,sizeof(*out)))
        return KUI_ARMADA_CE_VMU_A2_PUBLISH_ARGUMENT;
    if(a2_publish_alias(in,sizeof(*in),work,sizeof(*work)) ||
       a2_publish_alias(in,sizeof(*in),out,sizeof(*out)) ||
       a2_publish_alias(work,sizeof(*work),out,sizeof(*out)))
        return KUI_ARMADA_CE_VMU_A2_PUBLISH_ARGUMENT;
    if(!out->counts[0]) out->counts[0]=KUI_ARMADA_CE_VMU_A2_PUBLISH_MAGIC;
    out->counts[1]=a2_publish_inc(out->counts[1]);out->status[4]=0;
    if(out->counts[0]!=KUI_ARMADA_CE_VMU_A2_PUBLISH_MAGIC || out->status[3])
        return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_STATE,0,out->counts[0],
            KUI_ARMADA_CE_VMU_A2_PUBLISH_MAGIC,out->status[3]);
    const struct kui_armada_ce_vmu_a2_parent *p=&in->parent;
    out->parent[0]=p->request_header;out->parent[1]=p->response_header;
    out->parent[2]=p->rx_bus;out->parent[3]=p->capacity;out->parent[4]=p->response_function;
    for(unsigned i=0;i<5u;i++) out->target[i]=0;
    if(!a2_publish_ram(in->pool_bus,in->pool_bytes) || in->pool_bytes!=0x2000u ||
       (in->pool_bus&0xfffu) || !a2_publish_ram(in->tx_bus,in->tx_bytes) ||
       (in->tx_bus&0xfffu) || in->tx_bytes>0x1000u ||
       !a2_publish_ram(in->protected_bus,in->protected_bytes) || in->target_present>1u)
        return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN,
            in->pool_bus,in->pool_bytes,0x2000u,0);
    if(a2_publish_overlap(in->pool_bus,in->pool_bytes,in->tx_bus,in->tx_bytes) ||
       a2_publish_overlap(in->pool_bus,in->pool_bytes,in->protected_bus,in->protected_bytes) ||
       a2_publish_overlap(in->tx_bus,in->tx_bytes,in->protected_bus,in->protected_bytes))
        return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_OVERLAP,
            in->pool_bus,in->tx_bus,in->protected_bus,0);
    unsigned parent_bytes=a2_publish_parent_bytes(p->request_header);
    unsigned parent_command=p->request_header&255u;
    unsigned response_command=parent_command==1u?5u:parent_command==2u?6u:8u;
    if(!parent_bytes || !a2_publish_slice(p->rx_bus,p->capacity,in->pool_bus,in->pool_bytes) ||
       p->capacity<parent_bytes || (p->response_header&0xffffu)!=response_command ||
       ((p->response_header>>16)&0xe0u)!=0x20u ||
       (p->response_header>>24)!=(parent_bytes/4u-1u) ||
       (parent_command==9u && p->request_function!=0x01000000u) ||
       (parent_command==9u?p->response_function!=0x01000000u:
           !(p->response_function&0x01000000u)))
        return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_PARENT,
            p->rx_bus,p->response_header,parent_bytes,p->response_function);
    if(p->response_header&0x00020000u)
        return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_CONFLICT,
            p->rx_bus,p->response_header,0x00020000u,0);
    unsigned needed=0;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
    uint8_t *request=work->request;
#endif
    if(in->target_present) {
        const struct kui_armada_ce_vmu_a2_target *t=&in->target;
        if(!backend || (t->request_bytes!=4u && t->request_bytes!=12u
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
            && t->request_bytes!=140u
#endif
            ))
            return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_ARGUMENT,
                t->rx_bus,t->request_bytes,12u,0);
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
        /* The large request occupies only the unused reply tail. A backend
         * sees a disjoint384-byte output prefix and may never overwrite the
         * immutable140-byte request. Small packets retain their own buffer. */
        volatile uint32_t *clean=work->reply;
        for(unsigned i=0;i<sizeof(work->reply)/sizeof(work->reply[0]);i++) clean[i]=0;
        if(t->request_bytes==140u)
            request=(uint8_t *)(void *)work->reply+KUI_ARMADA_CE_VMU_A2_WRITE_REQUEST_OFFSET;
        for(unsigned i=0;i<t->request_bytes;i++) request[i]=t->request[i];
        out->target[0]=a2_publish_le32(request);out->target[2]=t->rx_bus;
        out->target[3]=t->capacity;
        needed=a2_publish_target_bytes(request,t->request_bytes);
#else
        for(unsigned i=0;i<t->request_bytes;i++) work->request[i]=t->request[i];
        out->target[0]=a2_publish_le32(work->request);out->target[2]=t->rx_bus;
        out->target[3]=t->capacity;
        needed=a2_publish_target_bytes(work->request,t->request_bytes);
#endif
        if(!needed) return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_REQUEST,
            t->rx_bus,out->target[0],0,0);
        if(!a2_publish_slice(t->rx_bus,t->capacity,in->pool_bus,in->pool_bytes) || t->capacity<needed)
            return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_SPAN,
                t->rx_bus,t->capacity,needed,1);
        if(a2_publish_overlap(p->rx_bus,p->capacity,t->rx_bus,t->capacity))
            return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_OVERLAP,
                t->rx_bus,t->capacity,p->rx_bus,p->capacity);
        if(t->response_header!=UINT32_MAX)
            return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_CONFLICT,
                t->rx_bus,t->response_header,UINT32_MAX,1);
        /* Also bounds any malformed callback's claimed output to initialized
         * private bytes. Volatile stores avoid compiler-added CRT calls. */
#if !KUI_ARMADA_CE_MAPLE_WRITE_STAGING
        volatile uint32_t *clean=work->reply;
        for(unsigned i=0;i<sizeof(work->reply)/sizeof(work->reply[0]);i++) clean[i]=0;
#else
        /* Backends may only stage/enqueue bounded private work here;
         * storage I/O must run outside this masked callback. Prove the request's
         * current owner before that handoff, then repeat admission before
         * publishing any guest byte. This does not authorize masked I/O. */
        if((request[0]==12u || (request[0]==13u && request[9]==4u)) && !admit(admit_context))
            return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION,
                p->rx_bus,0,1u,needed);
#endif
        size_t produced=0;
        out->status[1]=a2_publish_inc(out->status[1]);
        int handled=backend(backend_context,
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
            request,t->request_bytes,(uint8_t *)(void *)work->reply,
            t->request_bytes==140u?KUI_ARMADA_CE_VMU_A2_WRITE_REQUEST_OFFSET:sizeof(work->reply),&produced);
#else
            work->request,t->request_bytes,(uint8_t *)(void *)work->reply,sizeof(work->reply),&produced);
#endif
        out->target[1]=work->reply[0];out->target[4]=(uint32_t)produced;
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
        if(handled==KUI_ARMADA_CE_VMU_A2_BACKEND_DEFERRED) {
            if(produced || t->request_bytes!=12u ||
               (request[0]!=11u && (request[0]!=13u || request[9]!=4u)))
                return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_BACKEND,
                    t->rx_bus,(uint32_t)handled,0u,(uint32_t)produced);
            if(!admit(admit_context))
                return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION,
                    p->rx_bus,0,1u,needed);
            /* No reply exists yet. In particular, do not publish AGAIN,
             * advertise the parent or count completion. The caller owns the
             * separate native hold/worker/fresh-admission contract. */
            return KUI_ARMADA_CE_VMU_A2_PUBLISH_PENDING;
        }
#endif
        if(handled!=1)
            return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_BACKEND,
                t->rx_bus,(uint32_t)handled,1u,(uint32_t)produced);
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
        /* Exact MAPLEDEV WRITE RVA7f5a requests4 bytes and RVA7f7e..7f8a
         * checks only the response header. MAPLE RVA2bf4..2c0c copies the
         * minimum of the logical response size and requested capacity.
         * Preserve a canonical private FILEERR8 header (including its one-word
         * payload count) in this one4-byte CMD12 slice; never extend a store
         * into the allocator bucket. CMD13 uses8 bytes and has no exception.
         * target[4] retains the logical8 bytes while needed bounds stores. */
        bool write_error_header_only=request[0]==12u && t->request_bytes==140u &&
            t->capacity==4u && produced==8u &&
            ((const uint8_t *)(const void *)work->reply)[0]==0xfbu;
#endif
        if(
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
            produced>sizeof(work->reply) || (produced>t->capacity && !write_error_header_only) ||
            !a2_publish_reply((const uint8_t *)(const void *)work->reply,(unsigned)produced,request)
#else
            produced!=needed || !a2_publish_reply((const uint8_t *)(const void *)work->reply,needed,work->request)
#endif
            )
            return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_REPLY,
                t->rx_bus,(uint32_t)produced,needed,out->target[1]);
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
        needed=write_error_header_only?4u:(unsigned)produced;
#endif
    }
    if(!admit(admit_context))
        return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION,
            p->rx_bus,0,1u,needed);
    if(in->target_present) {
        const struct kui_armada_ce_vmu_a2_target *t=&in->target;
        for(unsigned offset=4u;offset<needed;offset+=4u)
            if(!a2_publish_store(store,store_context,out,t->rx_bus+offset,work->reply[offset/4u]))
                return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE,
                    t->rx_bus+offset,work->reply[offset/4u],0,offset);
        if(!a2_publish_store(store,store_context,out,t->rx_bus,work->reply[0]))
            return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE,
                t->rx_bus,work->reply[0],0,0);
        out->counts[4]=a2_publish_inc(out->counts[4]);
#if KUI_ARMADA_CE_MAPLE_WRITE_STAGING
        unsigned command=request[0],index=command==1u?0u:command==2u?1u:
            command==10u?2u:command==11u?3u:4u;
        if(command!=12u && ((const uint8_t *)(const void *)work->reply)[0]<0xfbu)
            out->commands[index]=a2_publish_inc(out->commands[index]);
#else
        unsigned command=work->request[0],index=command==1u?0u:command==2u?1u:
            command==10u?2u:command==11u?3u:4u;
        out->commands[index]=a2_publish_inc(out->commands[index]);
#endif
    }
    uint32_t advertised=p->response_header|0x00020000u;
    if(!a2_publish_store(store,store_context,out,p->rx_bus,advertised))
        return a2_publish_fail(out,KUI_ARMADA_CE_VMU_A2_PUBLISH_STORE,
            p->rx_bus,advertised,0,needed);
    out->counts[3]=a2_publish_inc(out->counts[3]);
    out->counts[2]=a2_publish_inc(out->counts[2]);
    return KUI_ARMADA_CE_VMU_A2_PUBLISH_OK;
}
