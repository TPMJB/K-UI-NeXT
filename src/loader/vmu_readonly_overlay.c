/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_readonly_overlay.h"
#include <string.h>

#define OVERLAY_MAGIC UINT32_C(0x56524f31)
#define RAM_BEGIN UINT32_C(0x0c000000)
#define RAM_END UINT32_C(0x0d000000)
#define CONTROLLER UINT32_C(0x01000000)
#define MEMCARD UINT32_C(0x02000000)
#define WRITE_REQUEST_OFFSET 384u
_Static_assert(WRITE_REQUEST_OFFSET+140u<=KUI_VMU_READONLY_OVERLAY_REPLY,
    "Full WRITE snapshot must fit the unused staging tail");

static void readonly_overlay_increment(uint32_t *value) {if(*value!=UINT32_MAX) ++*value;}
static uint32_t readonly_overlay_le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void readonly_overlay_put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(i*8u));
}
static int readonly_overlay_pointer_span(const void *pointer,size_t bytes) {
    return pointer && bytes<=UINTPTR_MAX-(uintptr_t)pointer;
}
static int readonly_overlay_pointer_overlap(const void *a,size_t an,const void *b,size_t bn) {
    uintptr_t ap=(uintptr_t)a,bp=(uintptr_t)b;
    return an && bn && ap<bp+bn && bp<ap+an;
}
static int readonly_overlay_ram_span(uint32_t address,uint32_t bytes) {
    return bytes && address>=RAM_BEGIN && address<RAM_END &&
        !(address&3u) && !(bytes&3u) && bytes<=RAM_END-address;
}
static int readonly_overlay_physical_overlap(uint32_t a,uint32_t an,uint32_t b,uint32_t bn) {
    return a<b+bn && b<a+an;
}
static int readonly_overlay_fail(struct kui_vmu_readonly_overlay *s,int result,uint32_t flag) {
    s->admitted=0;s->pending=0;s->stats.flags|=flag;readonly_overlay_increment(&s->stats.failures);
    return result;
}

int kui_vmu_readonly_overlay_init_backend_address(struct kui_vmu_readonly_overlay *s,uint8_t address,
    kui_vmu_readonly_overlay_handler handler,kui_vmu_readonly_overlay_admit admit,void *context) {
    if(!s) return KUI_VMU_OVERLAY_ARGUMENT;
    memset(s,0,sizeof(*s));
    if(!handler || (address!=1u && address!=2u)) return readonly_overlay_fail(s,KUI_VMU_OVERLAY_ARGUMENT,KUI_VMU_OVERLAY_BAD_ARGUMENT);
    s->magic=OVERLAY_MAGIC;s->handler=handler;s->admit=admit;s->address=address;
    s->context=context;s->admitted=1;
    return KUI_VMU_OVERLAY_OK;
}
int kui_vmu_readonly_overlay_init_backend(struct kui_vmu_readonly_overlay *s,
    kui_vmu_readonly_overlay_handler handler,kui_vmu_readonly_overlay_admit admit,void *context) {
    return kui_vmu_readonly_overlay_init_backend_address(s,1u,handler,admit,context);
}
int kui_vmu_readonly_overlay_init_address(struct kui_vmu_readonly_overlay *s,uint8_t address,
    kui_vmu_readonly_overlay_handler handler,void *context) {
    return kui_vmu_readonly_overlay_init_backend_address(s,address,handler,NULL,context);
}
int kui_vmu_readonly_overlay_init(struct kui_vmu_readonly_overlay *s,
    kui_vmu_readonly_overlay_handler handler,void *context) {
    return kui_vmu_readonly_overlay_init_backend(s,handler,NULL,context);
}

static unsigned readonly_overlay_command(uint32_t header) {return header&255u;}
static unsigned readonly_overlay_destination(uint32_t header) {return (header>>8)&255u;}
static unsigned readonly_overlay_payload_words(uint32_t header) {return header>>24;}
static int readonly_overlay_unicast_request(uint32_t header) {
    unsigned address=readonly_overlay_destination(header),unit=address&63u;
    return ((header>>16)&255u)==(address&192u) &&
        (unit==32u || (unit && unit<32u && !(unit&(unit-1u))));
}
static unsigned readonly_overlay_target_response_bytes(const struct kui_vmu_readonly_overlay_frame *f,int filtered) {
    unsigned cmd=readonly_overlay_command(f->header),words=readonly_overlay_payload_words(f->header);
    if(cmd==1u || cmd==2u) return words?0u:cmd==1u?116u:196u;
    if(filtered && cmd==3u) return words?0u:4u;
    if(filtered && (cmd==12u || cmd==13u)) {
        unsigned phase=(f->payload1>>8)&255u;
        return f->payload0==MEMCARD && !(f->payload1&UINT32_C(0x00ff00ff)) &&
            (cmd==12u?(words==34u && phase<4u):
             (words==2u && (phase==1u || phase==4u)))?4u:0u;
    }
    if((cmd!=10u && cmd!=11u) || words!=2u || f->payload0!=MEMCARD ||
       (f->payload1&UINT32_C(0x00ffffff)) || (cmd==10u && f->payload1)) return 0u;
    return cmd==10u?32u:524u;
}
static int readonly_overlay_parent_request(const struct kui_vmu_readonly_overlay_frame *f) {
    unsigned cmd=readonly_overlay_command(f->header),words=readonly_overlay_payload_words(f->header);
    return ((cmd==1u || cmd==2u) && !words) ||
        (cmd==9u && words==1u && f->payload0==CONTROLLER);
}

int kui_vmu_readonly_overlay_prepare(struct kui_vmu_readonly_overlay *s,
    const struct kui_vmu_readonly_overlay_frame *frames,size_t count,
    uint32_t source,uint32_t source_bytes,uint32_t protected_begin,uint32_t protected_bytes) {
    unsigned parents=0,targets=0,parent=0,minimum_source_bytes=0;
    if(!s) return KUI_VMU_OVERLAY_ARGUMENT;
    if(s->magic!=OVERLAY_MAGIC || !s->admitted || s->pending)
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_STATE,KUI_VMU_OVERLAY_BAD_STATE);
    if(!frames || !count || count>KUI_VMU_READONLY_OVERLAY_FRAMES ||
       !readonly_overlay_pointer_span(frames,count*sizeof(*frames)))
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_ARGUMENT,KUI_VMU_OVERLAY_BAD_ARGUMENT);
    if(readonly_overlay_pointer_overlap(frames,count*sizeof(*frames),s,sizeof(*s)))
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_OVERLAP,KUI_VMU_OVERLAY_BAD_OVERLAP);
    if((source&31u) || !readonly_overlay_ram_span(source,source_bytes) ||
       !readonly_overlay_ram_span(protected_begin,protected_bytes) ||
       readonly_overlay_physical_overlap(source,source_bytes,protected_begin,protected_bytes))
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_SPAN,KUI_VMU_OVERLAY_BAD_SPAN);
    for(size_t i=0;i<count;i++) {
        const struct kui_vmu_readonly_overlay_frame *f=&frames[i];
        s->stats.last_request=f->header;s->stats.last_rx=f->rx_physical;s->stats.last_reply=0;
        minimum_source_bytes+=12u+4u*readonly_overlay_payload_words(f->header);
        if(!readonly_overlay_unicast_request(f->header))
            return readonly_overlay_fail(s,KUI_VMU_OVERLAY_REQUEST,KUI_VMU_OVERLAY_BAD_REQUEST);
        if((f->rx_physical&31u) || f->rx_capacity<4u || f->rx_capacity>1024u ||
           !readonly_overlay_ram_span(f->rx_physical,f->rx_capacity) ||
           !readonly_overlay_pointer_span(f->rx_bytes,f->rx_capacity))
            return readonly_overlay_fail(s,KUI_VMU_OVERLAY_SPAN,KUI_VMU_OVERLAY_BAD_SPAN);
        if(readonly_overlay_physical_overlap(f->rx_physical,f->rx_capacity,source,source_bytes) ||
           readonly_overlay_physical_overlap(f->rx_physical,f->rx_capacity,protected_begin,protected_bytes) ||
           readonly_overlay_pointer_overlap(f->rx_bytes,f->rx_capacity,s,sizeof(*s)) ||
           readonly_overlay_pointer_overlap(f->rx_bytes,f->rx_capacity,frames,count*sizeof(*frames)))
            return readonly_overlay_fail(s,KUI_VMU_OVERLAY_OVERLAP,KUI_VMU_OVERLAY_BAD_OVERLAP);
        for(size_t j=0;j<i;j++)
            if(readonly_overlay_physical_overlap(f->rx_physical,f->rx_capacity,frames[j].rx_physical,frames[j].rx_capacity) ||
               readonly_overlay_pointer_overlap(f->rx_bytes,f->rx_capacity,frames[j].rx_bytes,frames[j].rx_capacity))
                return readonly_overlay_fail(s,KUI_VMU_OVERLAY_OVERLAP,KUI_VMU_OVERLAY_BAD_OVERLAP);
        if(readonly_overlay_destination(f->header)==32u) {
            ++parents;parent=(unsigned)i;
            unsigned needed=readonly_overlay_command(f->header)==1u?116u:readonly_overlay_command(f->header)==2u?196u:16u;
            if(!readonly_overlay_parent_request(f) || f->rx_capacity<needed)
                return readonly_overlay_fail(s,KUI_VMU_OVERLAY_REQUEST,KUI_VMU_OVERLAY_BAD_REQUEST);
        } else if(readonly_overlay_destination(f->header)==s->address) {
            unsigned needed=readonly_overlay_target_response_bytes(f,s->admit!=NULL);
            if(!needed || f->rx_capacity<needed)
                return readonly_overlay_fail(s,KUI_VMU_OVERLAY_REQUEST,KUI_VMU_OVERLAY_BAD_REQUEST);
            ++targets;
        }
    }
    if(parents!=1u)
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_PARENT,KUI_VMU_OVERLAY_BAD_PARENT_COUNT);
    if(source_bytes<minimum_source_bytes)
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_SPAN,KUI_VMU_OVERLAY_BAD_SPAN);
    for(size_t i=0;i<count;i++) if(readonly_overlay_destination(frames[i].header)==s->address) {
        const struct kui_vmu_readonly_overlay_frame *f=&frames[i];
        if(s->admit && s->admit(s->context,f->header,f->payload0,f->payload1)!=1)
            return readonly_overlay_fail(s,KUI_VMU_OVERLAY_REQUEST,KUI_VMU_OVERLAY_BAD_REQUEST);
        if(readonly_overlay_command(f->header)==12u) {
            if(!readonly_overlay_pointer_span(f->write_payload,128u) ||
               readonly_overlay_pointer_overlap(f->write_payload,128u,s,sizeof(*s)) ||
               readonly_overlay_pointer_overlap(f->write_payload,128u,frames,count*sizeof(*frames)))
                return readonly_overlay_fail(s,KUI_VMU_OVERLAY_SPAN,KUI_VMU_OVERLAY_BAD_SPAN);
            for(size_t j=0;j<count;j++)
                if(readonly_overlay_pointer_overlap(f->write_payload,128u,frames[j].rx_bytes,frames[j].rx_capacity))
                    return readonly_overlay_fail(s,KUI_VMU_OVERLAY_OVERLAP,KUI_VMU_OVERLAY_BAD_OVERLAP);
            uint8_t *request=(uint8_t *)(void *)s->staged[i]+WRITE_REQUEST_OFFSET;
            readonly_overlay_put32(request,f->header);readonly_overlay_put32(request+4u,f->payload0);readonly_overlay_put32(request+8u,f->payload1);
            memcpy(request+12u,f->write_payload,128u);
        }
    }
    memcpy(s->frames,frames,count*sizeof(*frames));
    for(size_t i=0;i<count;i++) s->frames[i].write_payload=NULL;
    s->frame_count=(uint32_t)count;s->parent_index=parent;s->target_count=targets;
    s->pending=1;readonly_overlay_increment(&s->stats.prepared);
    return KUI_VMU_OVERLAY_OK;
}

static int readonly_overlay_parent_response(const struct kui_vmu_readonly_overlay_frame *f) {
    const uint8_t *r=f->rx_bytes;
    unsigned cmd=readonly_overlay_command(f->header),expected=cmd==1u?5u:cmd==2u?6u:8u;
    unsigned words=cmd==1u?28u:cmd==2u?48u:3u;
    uint32_t function=readonly_overlay_le32(r+4u);
    return r[0]==expected && r[1]==0u && (r[2]&224u)==32u && r[3]==words &&
        (cmd==9u?function==CONTROLLER:!!(function&CONTROLLER));
}
static int readonly_overlay_generated_response(const struct kui_vmu_readonly_overlay_frame *f,
    const uint8_t *r,size_t bytes,int filtered,uint8_t address) {
    unsigned cmd=readonly_overlay_command(f->header),expected=cmd==1u?5u:cmd==2u?6u:cmd==3u || cmd==12u || cmd==13u?7u:8u;
    unsigned needed=readonly_overlay_target_response_bytes(f,filtered);
    /* The filtered durable backend may defer READ/final SYNC before acquiring
     * SCI, or a changed WRITE while a commit remains pending. Its exact
     * four-byte AGAIN is a retry, not a data/commit ACK. Publish no payload
     * and keep the card present for the next request.
     * Default physical-timeout readers retain their success-only contract. */
    if(filtered && (cmd==11u || cmd==12u || (cmd==13u && ((f->payload1>>8)&255u)==4u)) &&
       bytes==4u && bytes<=f->rx_capacity &&
       r[0]==0xfcu && !r[1] && r[2]==address && !r[3]) return 1;
    return bytes==needed && bytes<=f->rx_capacity && r[0]==expected &&
        r[1]==0u && r[2]==address && 4u+4u*r[3]==bytes &&
        ((cmd==3u || cmd==12u || cmd==13u) || readonly_overlay_le32(r+4u)==MEMCARD) &&
        (cmd!=11u || readonly_overlay_le32(r+8u)==f->payload1);
}

int kui_vmu_readonly_overlay_complete(struct kui_vmu_readonly_overlay *s) {
    if(!s) return KUI_VMU_OVERLAY_ARGUMENT;
    if(s->magic!=OVERLAY_MAGIC || !s->admitted || !s->pending)
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_STATE,KUI_VMU_OVERLAY_BAD_STATE);
    const struct kui_vmu_readonly_overlay_frame *parent=&s->frames[s->parent_index];
    s->stats.last_request=parent->header;s->stats.last_rx=parent->rx_physical;
    s->stats.last_reply=readonly_overlay_le32(parent->rx_bytes);
    if(!readonly_overlay_parent_response(parent))
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_PARENT,KUI_VMU_OVERLAY_BAD_PARENT_REPLY);
    if(parent->rx_bytes[2]&s->address)
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_CONFLICT,KUI_VMU_OVERLAY_PHYSICAL_CONFLICT);
    /* Every physical conflict is checked before invoking the backend. */
    for(unsigned i=0;!s->admit && i<s->frame_count;i++) if(readonly_overlay_destination(s->frames[i].header)==s->address) {
        const struct kui_vmu_readonly_overlay_frame *f=&s->frames[i];
        s->stats.last_request=f->header;s->stats.last_rx=f->rx_physical;
        s->stats.last_reply=readonly_overlay_le32(f->rx_bytes);
        if(s->stats.last_reply!=UINT32_MAX)
            return readonly_overlay_fail(s,KUI_VMU_OVERLAY_CONFLICT,KUI_VMU_OVERLAY_PHYSICAL_CONFLICT);
    }
    for(unsigned i=0;i<s->frame_count;i++) {
        const struct kui_vmu_readonly_overlay_frame *f=&s->frames[i];
        if(readonly_overlay_destination(f->header)!=s->address) continue;
        uint8_t request[12],*reply=(uint8_t *)(void *)s->staged[i];
        const uint8_t *input=request;size_t capacity=KUI_VMU_READONLY_OVERLAY_REPLY;
        size_t bytes=0;
        if(readonly_overlay_command(f->header)==12u) {
            input=reply+WRITE_REQUEST_OFFSET;capacity=WRITE_REQUEST_OFFSET;
        } else {
            readonly_overlay_put32(request,f->header);readonly_overlay_put32(request+4u,f->payload0);readonly_overlay_put32(request+8u,f->payload1);
        }
        readonly_overlay_put32(reply,0);s->stats.last_request=f->header;s->stats.last_rx=f->rx_physical;
        s->stats.last_reply=0;
        readonly_overlay_increment(&s->stats.backend_calls);
        int handled=s->handler(s->context,input,4u+4u*readonly_overlay_payload_words(f->header),reply,
                      capacity,&bytes);
        s->stats.last_reply=readonly_overlay_le32(reply);
        if(handled!=1 ||
           !readonly_overlay_generated_response(f,reply,bytes,s->admit!=NULL,(uint8_t)s->address))
            return readonly_overlay_fail(s,KUI_VMU_OVERLAY_HANDLER,KUI_VMU_OVERLAY_BAD_HANDLER);
        s->reply_bytes[i]=(uint16_t)bytes;
    }
    /* Volatile external stores preserve payload -> header -> advertise order.
     * SDK consumption is excluded until the caller returns from this hook. */
    for(unsigned i=0;i<s->frame_count;i++) {
        const struct kui_vmu_readonly_overlay_frame *f=&s->frames[i];
        if(readonly_overlay_destination(f->header)!=s->address) continue;
        const uint8_t *reply=(const uint8_t *)(const void *)s->staged[i];
        volatile uint8_t *out=f->rx_bytes;
        for(unsigned j=4u;j<s->reply_bytes[i];j++) out[j]=reply[j];
        for(unsigned j=0;j<4u;j++) out[j]=reply[j];
        readonly_overlay_increment(&s->stats.replies);
        unsigned cmd=readonly_overlay_command(f->header);
        if(cmd==1u) readonly_overlay_increment(&s->stats.devinfo);
        else if(cmd==2u) readonly_overlay_increment(&s->stats.allinfo);
        else if(cmd==10u) readonly_overlay_increment(&s->stats.media);
        else if(cmd==11u) readonly_overlay_increment(&s->stats.reads);
    }
    ((volatile uint8_t *)parent->rx_bytes)[2]=(uint8_t)(parent->rx_bytes[2]|s->address);
    if(!s->target_count) s->stats.last_reply=readonly_overlay_le32(parent->rx_bytes);
    readonly_overlay_increment(&s->stats.advertisements);readonly_overlay_increment(&s->stats.completed);s->pending=0;
    return KUI_VMU_OVERLAY_OK;
}

int __attribute__((noipa)) kui_vmu_readonly_overlay_complete_absent(struct kui_vmu_readonly_overlay *s) {
    if(!s) return KUI_VMU_OVERLAY_ARGUMENT;
    if(s->magic!=OVERLAY_MAGIC || !s->admitted || !s->pending || !s->admit)
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_STATE,KUI_VMU_OVERLAY_BAD_STATE);
    const struct kui_vmu_readonly_overlay_frame *parent=&s->frames[s->parent_index];
    s->stats.last_request=parent->header;s->stats.last_rx=parent->rx_physical;
    s->stats.last_reply=readonly_overlay_le32(parent->rx_bytes);
    if(!readonly_overlay_parent_response(parent))
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_PARENT,KUI_VMU_OVERLAY_BAD_PARENT_REPLY);
    if(parent->rx_bytes[2]&s->address)
        return readonly_overlay_fail(s,KUI_VMU_OVERLAY_CONFLICT,KUI_VMU_OVERLAY_PHYSICAL_CONFLICT);
    /* All admission is complete before publishing the first absent header.
     * Virtual packets never reached hardware; do not interpret their stale bytes
     * as physical replies, and never let the SDK consume those stale headers. */
    for(unsigned i=0;i<s->frame_count;i++) {
        const struct kui_vmu_readonly_overlay_frame *f=&s->frames[i];
        if(readonly_overlay_destination(f->header)!=s->address) continue;
        volatile uint8_t *out=f->rx_bytes;
        for(unsigned j=0;j<4u;j++) out[j]=UINT8_MAX;
        s->stats.last_request=f->header;s->stats.last_rx=f->rx_physical;
        s->stats.last_reply=UINT32_MAX;
    }
    readonly_overlay_increment(&s->stats.completed);s->pending=0;
    return KUI_VMU_OVERLAY_OK;
}
