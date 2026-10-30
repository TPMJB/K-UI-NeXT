/* SPDX-License-Identifier: GPL-3.0-only */
/* Optional PROFILE observations. The exact Evolution phase walk is unchanged.
 * Other titles can take one terminal raw owner-stack/video snapshot, sharing
 * the retired phase history's storage. Neither observation grants authority
 * to a VMU or storage operation. Failed samples never reject a pair. */
#include "kui/vmu_native_profile.h"
#define NATIVE_PHASE_RECORDS 16u
#define NATIVE_RAW_OWNER_WORDS 192u
#define NATIVE_RAW_OWNER_MODE 2u
#if !KUI_NATIVE_SPLIT_CONTROLLER_GENERAL
__asm__(".global __kui_native_raw_owner_terminal_worker\n"
        ".set __kui_native_raw_owner_terminal_worker,1\n");
#endif
enum native_raw_flags {
    NATIVE_RAW_SPAN=1u,NATIVE_RAW_WORDS=2u,NATIVE_RAW_VIDEO=4u
};
enum native_phase_flags {
    NATIVE_PHASE_CLOCK=1u,NATIVE_PHASE_READ=2u,NATIVE_PHASE_VIDEO=4u,
    NATIVE_PHASE_UNWIND=8u,NATIVE_PHASE_WAIT=16u,NATIVE_PHASE_WRAP=32u
};
typedef bool (*native_phase_read)(void *,uint32_t,unsigned,uint32_t *);
#if !KUI_NATIVE_SPLIT_CONTROLLER_GENERAL
static bool native_phase_image(uint32_t bytes,uint32_t load,const uint8_t digest[32]) {
    static const uint8_t expected[32]={
        0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
        0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
    return bytes==2143652u && load==UINT32_C(0x8c010000) && digest && !memcmp(digest,expected,32u);
}
#endif
struct native_phase_record {
    union {
        struct {
            uint8_t kind,flags;uint16_t reserved;
            uint32_t scan_begin,scan_end;
            uint32_t load,vblank,control,tick_begin,tick_end;
            uint32_t pc,pr,sr,event,video_counter;
        };
        uint32_t words[13];
    };
};
struct native_raw_owner {
    union {
        struct {uint32_t frame,words,flags,tick,pr,sr,load,vblank,control,scan,event;};
        uint32_t header[11];
    };
    uint32_t data[NATIVE_RAW_OWNER_WORDS];
};
#include "vmu_native_wait_capture.inc"
struct native_phase_state {
    uint32_t enabled,sequence,count,valid,waits,wraps;
    union {
        struct {struct native_phase_record pending,ring[NATIVE_PHASE_RECORDS];};
        struct native_raw_owner raw;
        struct native_wait_capture wait;
    };
};
_Static_assert(KUI_VMU_NATIVE_PROFILE_SUBMIT==0u && KUI_VMU_NATIVE_PROFILE_COMPLETE==1u,
    "Phase kind byte contains only submit or complete");
_Static_assert((NATIVE_PHASE_CLOCK|NATIVE_PHASE_READ|NATIVE_PHASE_VIDEO|
    NATIVE_PHASE_UNWIND|NATIVE_PHASE_WAIT|NATIVE_PHASE_WRAP)==0x3fu,
    "Every phase flag fits the private byte");
_Static_assert(sizeof(struct native_phase_record)==52u && sizeof(struct native_phase_state)==908u,
    "Terminal snapshot must not grow retained phase storage");
static void native_phase_reset(struct native_phase_state *p,bool enabled) {
    memset(p,0,sizeof(*p));p->enabled=enabled?1u:0u;
}
static bool native_phase_span_from(uint32_t frame,unsigned bytes,uint32_t lease,
    unsigned lease_bytes,uint32_t minimum) {
    uint32_t area=frame&UINT32_C(0xe0000000),at=frame&UINT32_C(0x1fffffff);
    uint32_t owned=lease&UINT32_C(0x1fffffff);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        !(frame&3u) && at>=minimum && at<UINT32_C(0x0d000000) &&
        bytes<=UINT32_C(0x0d000000)-at && owned>=UINT32_C(0x0c000000) &&
        owned<UINT32_C(0x0d000000) && lease_bytes && lease_bytes<=UINT32_C(0x0d000000)-owned &&
        !(at<owned+lease_bytes && owned<at+bytes);
}
static bool native_phase_span(uint32_t frame,unsigned bytes,uint32_t lease,unsigned lease_bytes) {
    return native_phase_span_from(frame,bytes,lease,lease_bytes,UINT32_C(0x0c010000));
}
/* Called only when PROFILE is deliberately stopping, while the successful
 * completion's original frame is still live and interrupts remain masked.
 * Preserve its alias: a P1 owner-stack write need not yet be visible in P2.
 * Words above that frame are raw observations, not an unwound PC or a claim
 * that the whole readable span is an active stack. No pointer is followed. */
static void native_raw_owner_capture(struct native_phase_state *p,bool profile,
    uint32_t frame,uint32_t pr,uint32_t sr,uint32_t tick,
    uint32_t lease,unsigned lease_bytes,native_phase_read read,void *context) {
    if(!profile || p->enabled) return;
    p->enabled=NATIVE_RAW_OWNER_MODE;
    struct native_raw_owner *r=&p->raw;memset(r,0,sizeof(*r));
    r->frame=frame;r->pr=pr;r->sr=sr;r->tick=tick;
    /* The inherited low stack may extend below the executable. Keep the
     * reader/IP boundary protected, including the low hook's private stack. */
    if(!native_phase_span_from(frame,sizeof(r->data),lease,lease_bytes,
       UINT32_C(0x0c008100))) return;
    r->flags=NATIVE_RAW_SPAN;
    static const uint32_t registers[]={UINT32_C(0xa05f80d8),UINT32_C(0xa05f80cc),
        UINT32_C(0xa05f80d0),UINT32_C(0xa05f810c),UINT32_C(0xff000028)};
    bool video=true;
    for(unsigned i=0u;i<5u;i++)
        if(!read(context,registers[i],4u,&r->header[6u+i])) video=false;
    if(video) r->flags|=NATIVE_RAW_VIDEO;
    for(;r->words<NATIVE_RAW_OWNER_WORDS;r->words++)
        if(!read(context,frame+4u*r->words,4u,&r->data[r->words])) return;
    r->flags|=NATIVE_RAW_WORDS;
}
static bool native_phase_word(native_phase_read read,void *context,uint32_t frame,
    unsigned offset,uint32_t expected,uint32_t alternate) {
    uint32_t value;
    /* Preserve the inherited alias: SDK stack writes may be dirty in P1. */
    return read(context,frame+offset,4u,&value) && (value==expected || value==alternate);
}
static bool native_phase_unwind(struct native_phase_record *r,uint32_t frame,
    uint32_t lease,unsigned lease_bytes,native_phase_read read,void *context) {
    /* Exact same ordered owner witnesses as the original straight-line walk.
     * Every alternate shares its expected word's upper half; retain those
     * bits explicitly rather than treating any RAM-looking value as a match. */
    struct witness {uint16_t offset,alternate_low;uint32_t expected;};
    static const struct witness submit[]={
        {76u,0x19b0u,UINT32_C(0x8c1b1944)},
        {96u,0x0a1cu,UINT32_C(0x8c1b0a1c)},
        {120u,0x1bacu,UINT32_C(0x8c1f1bac)},
        {128u,0x1ad8u,UINT32_C(0x8c1f1ad8)},
        {200u,0xa348u,UINT32_C(0x8c1da1bc)}};
    static const struct witness complete[]={
        {88u,0x1500u,UINT32_C(0x8c1b1500)},
        {120u,0x09f6u,UINT32_C(0x8c1b09f6)},
        {140u,0x0ad2u,UINT32_C(0x8c1b0ad2)},
        {148u,0xa348u,UINT32_C(0x8c1da1bc)}};
    unsigned pr,pc,sr,bytes,count;const struct witness *words;
    if(r->kind==KUI_VMU_NATIVE_PROFILE_SUBMIT) {
        bytes=460u;words=submit;count=5u;
        pr=448u;pc=452u;sr=456u;
    } else if(r->kind==KUI_VMU_NATIVE_PROFILE_COMPLETE) {
        bytes=400u;words=complete;count=4u;
        pr=388u;pc=392u;sr=396u;
    } else return false;
    if(!native_phase_span(frame,bytes,lease,lease_bytes)) return false;
    for(unsigned i=0u;i<count;i++)
        if(!native_phase_word(read,context,frame,words[i].offset,words[i].expected,
           (words[i].expected&UINT32_C(0xffff0000))|words[i].alternate_low)) return false;
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
    if(p->enabled!=1u) return;
    struct native_phase_record *r=&p->pending;memset(r,0,sizeof(*r));
    /* Unsupported observation kinds cannot narrow into a valid SDK walk. */
    r->kind=kind<=KUI_VMU_NATIVE_PROFILE_COMPLETE?(uint8_t)kind:UINT8_MAX;
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
    if(p->enabled!=1u) return;
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
