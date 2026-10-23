/* SPDX-License-Identifier: GPL-3.0-only */
/* Optional exact-Evolution observation. All stores are private; the reader is
 * separate from VMU/storage authority. A failed sample never rejects a pair.
 * The caller enables this only for PROFILE and the exact original image. */
#include "kui/vmu_native_profile.h"
#define NATIVE_PHASE_RECORDS 16u
enum native_phase_flags {
    NATIVE_PHASE_CLOCK=1u,NATIVE_PHASE_READ=2u,NATIVE_PHASE_VIDEO=4u,
    NATIVE_PHASE_UNWIND=8u,NATIVE_PHASE_WAIT=16u,NATIVE_PHASE_WRAP=32u
};
typedef bool (*native_phase_read)(void *,uint32_t,unsigned,uint32_t *);
static bool native_phase_image(uint32_t bytes,uint32_t load,const uint8_t digest[32]) {
    static const uint8_t expected[32]={
        0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
        0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
    return bytes==2143652u && load==UINT32_C(0x8c010000) && digest && !memcmp(digest,expected,32u);
}
struct native_phase_record {
    uint32_t kind,flags,scan_begin,scan_end,tick_begin,tick_end;
    uint32_t load,vblank,control,pc,pr,sr,event,video_counter;
};
struct native_phase_state {
    uint32_t enabled,sequence,count,valid,waits,wraps;
    struct native_phase_record pending,ring[NATIVE_PHASE_RECORDS];
};
static void native_phase_reset(struct native_phase_state *p,bool enabled) {
    memset(p,0,sizeof(*p));p->enabled=enabled?1u:0u;
}
static bool native_phase_span(uint32_t frame,unsigned bytes,uint32_t lease,unsigned lease_bytes) {
    uint32_t area=frame&UINT32_C(0xe0000000),at=frame&UINT32_C(0x1fffffff);
    uint32_t owned=lease&UINT32_C(0x1fffffff);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        !(frame&3u) && at>=UINT32_C(0x0c010000) && at<UINT32_C(0x0d000000) &&
        bytes<=UINT32_C(0x0d000000)-at && owned>=UINT32_C(0x0c000000) &&
        owned<UINT32_C(0x0d000000) && lease_bytes && lease_bytes<=UINT32_C(0x0d000000)-owned &&
        !(at<owned+lease_bytes && owned<at+bytes);
}
static bool native_phase_word(native_phase_read read,void *context,uint32_t frame,
    unsigned offset,uint32_t expected,uint32_t alternate) {
    uint32_t value;
    /* Preserve the inherited alias: SDK stack writes may be dirty in P1. */
    return read(context,frame+offset,4u,&value) && (value==expected || value==alternate);
}
static bool native_phase_unwind(struct native_phase_record *r,uint32_t frame,
    uint32_t lease,unsigned lease_bytes,native_phase_read read,void *context) {
    unsigned pr,pc,sr;
    if(r->kind==KUI_VMU_NATIVE_PROFILE_SUBMIT) {
        if(!native_phase_span(frame,460u,lease,lease_bytes) ||
           !native_phase_word(read,context,frame,76u,UINT32_C(0x8c1b1944),UINT32_C(0x8c1b19b0)) ||
           !native_phase_word(read,context,frame,96u,UINT32_C(0x8c1b0a1c),UINT32_C(0x8c1b0a1c)) ||
           !native_phase_word(read,context,frame,120u,UINT32_C(0x8c1f1bac),UINT32_C(0x8c1f1bac)) ||
           !native_phase_word(read,context,frame,128u,UINT32_C(0x8c1f1ad8),UINT32_C(0x8c1f1ad8)) ||
           !native_phase_word(read,context,frame,200u,UINT32_C(0x8c1da1bc),UINT32_C(0x8c1da348))) return false;
        pr=448u;pc=452u;sr=456u;
    } else if(r->kind==KUI_VMU_NATIVE_PROFILE_COMPLETE) {
        if(!native_phase_span(frame,400u,lease,lease_bytes) ||
           !native_phase_word(read,context,frame,88u,UINT32_C(0x8c1b1500),UINT32_C(0x8c1b1500)) ||
           !native_phase_word(read,context,frame,120u,UINT32_C(0x8c1b09f6),UINT32_C(0x8c1b09f6)) ||
           !native_phase_word(read,context,frame,140u,UINT32_C(0x8c1b0ad2),UINT32_C(0x8c1b0ad2)) ||
           !native_phase_word(read,context,frame,148u,UINT32_C(0x8c1da1bc),UINT32_C(0x8c1da348))) return false;
        pr=388u;pc=392u;sr=396u;
    } else return false;
    return read(context,frame+pr,4u,&r->pr) && read(context,frame+pc,4u,&r->pc) &&
        read(context,frame+sr,4u,&r->sr);
}
static bool native_phase_wait(const struct native_phase_record *r) {
    return (!(r->pc&1u) && r->pc>=UINT32_C(0x8c1f3628) && r->pc<=UINT32_C(0x8c1f362e)) ||
        (!(r->pc&1u) && r->pc>=UINT32_C(0x8c1f1f36) && r->pc<=UINT32_C(0x8c1f1f3a) &&
         r->pr==UINT32_C(0x8c1f362c));
}
static void native_phase_begin(struct native_phase_state *p,unsigned kind,uint32_t frame,
    uint32_t lease,unsigned lease_bytes,const struct kui_vmu_native_profile_sample *clock,
    native_phase_read read,void *context) {
    if(!p->enabled) return;
    struct native_phase_record *r=&p->pending;memset(r,0,sizeof(*r));r->kind=kind;
    if(clock) r->tick_begin=clock->counter;
    if(kui_vmu_native_profile_sample_valid(clock)) r->flags|=NATIVE_PHASE_CLOCK;
    bool got=read(context,UINT32_C(0xa05f80d8),4u,&r->load);
    got=read(context,UINT32_C(0xa05f80cc),4u,&r->vblank) && got;
    got=read(context,UINT32_C(0xa05f80d0),4u,&r->control) && got;
    got=read(context,UINT32_C(0xa05f810c),4u,&r->scan_begin) && got;
    got=read(context,UINT32_C(0xff000028),4u,&r->event) && got;
    got=read(context,UINT32_C(0x8c358d18),4u,&r->video_counter) && got;
    if(got) r->flags|=NATIVE_PHASE_READ;
    if(native_phase_unwind(r,frame,lease,lease_bytes,read,context)) {
        r->flags|=NATIVE_PHASE_UNWIND;
        if(native_phase_wait(r)) r->flags|=NATIVE_PHASE_WAIT;
    }
}
static void native_phase_end(struct native_phase_state *p,
    const struct kui_vmu_native_profile_sample *clock,native_phase_read read,void *context) {
    if(!p->enabled) return;
    struct native_phase_record *r=&p->pending;uint32_t load=0u,vblank=0u,control=0u;
    if(clock) r->tick_end=clock->counter;
    if(!kui_vmu_native_profile_sample_valid(clock) ||
       r->tick_begin-r->tick_end>KUI_VMU_NATIVE_PROFILE_MAX_INTERVAL) r->flags&=~NATIVE_PHASE_CLOCK;
    bool got=read(context,UINT32_C(0xa05f80d8),4u,&load);
    got=read(context,UINT32_C(0xa05f80cc),4u,&vblank) && got;
    got=read(context,UINT32_C(0xa05f80d0),4u,&control) && got;
    got=read(context,UINT32_C(0xa05f810c),4u,&r->scan_end) && got;
    if(!got) r->flags&=~NATIVE_PHASE_READ;
    if(got && load==r->load && vblank==r->vblank && control==r->control) r->flags|=NATIVE_PHASE_VIDEO;
    unsigned required=NATIVE_PHASE_CLOCK|NATIVE_PHASE_READ|NATIVE_PHASE_VIDEO;
    if((r->flags&required)==required) {
        if(p->valid!=UINT32_MAX) ++p->valid;
        if(r->flags&NATIVE_PHASE_WAIT) {if(p->waits!=UINT32_MAX) ++p->waits;}
        uint32_t begin=r->scan_begin&1023u,end=r->scan_end&1023u;
        if(begin>end && end) {r->flags|=NATIVE_PHASE_WRAP;if(p->wraps!=UINT32_MAX) ++p->wraps;}
    }
    p->ring[p->sequence&(NATIVE_PHASE_RECORDS-1u)]=*r;
    ++p->sequence;if(p->count<NATIVE_PHASE_RECORDS) ++p->count;
}
