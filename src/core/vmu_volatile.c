/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_volatile.h"
#include <string.h>

static void volatile_card_increment(uint32_t *n) {if(*n!=UINT32_MAX) ++*n;}
static uint32_t volatile_card_le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static unsigned volatile_card_shape(uint32_t h,uint32_t p0,uint32_t p1) {
    unsigned cmd=h&255u,words=h>>24,phase=(p1>>8)&255u;
    if((h&UINT32_C(0x00ffff00))!=UINT32_C(0x00000100)) return 0;
    if(cmd==1u || cmd==2u) return words?0u:cmd==1u?116u:196u;
    if(p0!=KUI_VMU_EMU_FUNC_MEMCARD || (p1&UINT32_C(0x00ff00ff))) return 0;
    if(cmd==10u) return words==2u && !p1?32u:0u;
    if(cmd==11u) return words==2u && !phase?524u:0u;
    if(cmd==12u) return words==34u && phase<4u?4u:0u;
    if(cmd==13u) return words==2u && (phase==1u || phase==4u)?4u:0u;
    return 0;
}
static unsigned volatile_card_slot(const struct kui_vmu_volatile *s,uint16_t block) {
    for(unsigned i=0;i<s->cached_blocks && i<KUI_VMU_VOLATILE_BLOCKS;i++)
        if(s->blocks[i]==block) return i;
    return KUI_VMU_VOLATILE_BLOCKS;
}
static int volatile_card_full(struct kui_vmu_volatile *s) {
    s->faulted=1u;volatile_card_increment(&s->full_faults);return KUI_VMU_VOLATILE_FULL;
}
int kui_vmu_volatile_admit(struct kui_vmu_volatile *s,uint32_t h,uint32_t p0,uint32_t p1) {
    if(!s || !s->ready) return KUI_VMU_VOLATILE_ARGUMENT;
    if(s->faulted || s->cached_blocks>KUI_VMU_VOLATILE_BLOCKS)
        return KUI_VMU_VOLATILE_INTERNAL;
    if(!volatile_card_shape(h,p0,p1)) {volatile_card_increment(&s->refused);return KUI_VMU_VOLATILE_REFUSED;}
    if((h&255u)==12u && s->cached_blocks==KUI_VMU_VOLATILE_BLOCKS &&
       volatile_card_slot(s,(uint16_t)(p1>>24))==KUI_VMU_VOLATILE_BLOCKS) return volatile_card_full(s);
    return KUI_VMU_VOLATILE_HANDLED;
}
static int volatile_card_read_block(void *context,uint16_t block,uint8_t out[512]) {
    struct kui_vmu_volatile *s=context;
    if(!s || !s->ready || s->faulted || block>=256u ||
       s->cached_blocks>KUI_VMU_VOLATILE_BLOCKS) return KUI_VMU_EMU_IO_ERROR;
    unsigned i=volatile_card_slot(s,block);
    if(i<KUI_VMU_VOLATILE_BLOCKS) {
        memcpy(out,s->cache[i],512u);volatile_card_increment(&s->cache_reads);
    } else {
        if(s->reader(s->reader_context,block,out)) {
            s->faulted=1u;return KUI_VMU_EMU_IO_ERROR;
        }
        volatile_card_increment(&s->base_reads);
    }
    volatile_card_increment(&s->reads);return KUI_VMU_EMU_IO_OK;
}
static int volatile_card_commit_ram(void *context,uint16_t block,const uint8_t data[512]) {
    struct kui_vmu_volatile *s=context;
    if(!s || !s->ready || s->faulted || block>=256u ||
       s->cached_blocks>KUI_VMU_VOLATILE_BLOCKS) return KUI_VMU_EMU_IO_ERROR;
    unsigned i=volatile_card_slot(s,block);
    if(i==KUI_VMU_VOLATILE_BLOCKS) {
        if(s->cached_blocks==KUI_VMU_VOLATILE_BLOCKS) {
            (void)volatile_card_full(s);return KUI_VMU_EMU_IO_ERROR;
        }
        i=s->cached_blocks;s->blocks[i]=block;++s->cached_blocks;
    }
    memcpy(s->cache[i],data,512u);volatile_card_increment(&s->ram_commits);
    return KUI_VMU_EMU_IO_OK;
}
int kui_vmu_volatile_init(struct kui_vmu_volatile *s,
    int (*reader)(void *,uint16_t,uint8_t[512]),void *context) {
    if(!s) return KUI_VMU_VOLATILE_ARGUMENT;
    memset(s,0,sizeof(*s));
    if(!reader) return KUI_VMU_VOLATILE_ARGUMENT;
    const struct kui_vmu_emulation_io io={s,volatile_card_read_block,volatile_card_commit_ram};
    if(kui_vmu_emulation_init(&s->protocol,1u,&io)) return KUI_VMU_VOLATILE_INTERNAL;
    s->reader=reader;s->reader_context=context;s->ready=1u;return 0;
}
int kui_vmu_volatile_handle(struct kui_vmu_volatile *s,
    const uint8_t *request,size_t request_bytes,uint8_t *response,
    size_t capacity,size_t *response_bytes) {
    uint32_t h,p0=0,p1=0;unsigned needed,cmd;size_t n=0;
    uint8_t ack[8],*out=response;
    if(response_bytes) *response_bytes=0;
    if(!s || !s->ready || !request || !response || !response_bytes)
        return KUI_VMU_VOLATILE_ARGUMENT;
    if(s->faulted) return KUI_VMU_VOLATILE_INTERNAL;
    if(request_bytes<4u) goto refused;
    h=volatile_card_le32(request);cmd=h&255u;
    if(request_bytes!=4u+4u*(h>>24)) goto refused;
    if(request_bytes>=12u) {p0=volatile_card_le32(request+4u);p1=volatile_card_le32(request+8u);}
    needed=volatile_card_shape(h,p0,p1);if(!needed) goto refused;
    if(capacity<needed) return KUI_VMU_VOLATILE_CAPACITY;
    int admitted=kui_vmu_volatile_admit(s,h,p0,p1);
    if(admitted!=1) return admitted;
    /* The generic protocol reserves8B for possible FILEERR even when success
     * is a4B ACK. Keep those errors private and support an exact4B output. */
    if(cmd==12u || cmd==13u) out=ack;
    int handled=kui_vmu_emulation_handle(&s->protocol,request,request_bytes,
        out,out==ack?sizeof(ack):capacity,&n);
    unsigned expected=cmd==1u?5u:cmd==2u?6u:cmd==12u || cmd==13u?7u:8u;
    if(s->faulted || handled!=1 || n!=needed || out[0]!=expected ||
       out[1]!=0u || out[2]!=1u || 4u+4u*out[3]!=n) {
        s->faulted=1u;return KUI_VMU_VOLATILE_INTERNAL;
    }
    if(out==ack) memcpy(response,ack,needed);
    if(cmd==1u || cmd==2u) {
        static const char volatile_card_name[]="K-UI Volatile RAM Card";
        _Static_assert(sizeof(volatile_card_name)-1u<=30u,"Volatile name length");
        memset(response+22u,' ',30u);memcpy(response+22u,volatile_card_name,sizeof(volatile_card_name)-1u);
    }
    if(cmd==2u) {
        static const char volatile_card_version[]="K-UI volatile RAM writes; all changes lost on reset; SD image read-only";
        _Static_assert(sizeof(volatile_card_version)-1u<=80u,"Volatile version length");
        memset(response+116u,' ',80u);memcpy(response+116u,volatile_card_version,sizeof(volatile_card_version)-1u);
    }
    if(cmd==12u) volatile_card_increment(&s->write_phases);
    volatile_card_increment(&s->requests);*response_bytes=n;return KUI_VMU_VOLATILE_HANDLED;
refused:
    volatile_card_increment(&s->refused);return KUI_VMU_VOLATILE_REFUSED;
}
