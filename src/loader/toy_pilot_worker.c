/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot.h"
#include "kui/toy_pilot_driver_load.h"
#include "kui/retail_image.h"
#include "kui/hash.h"
#include <stddef.h>
#include <string.h>

/* An exact Toy profile. All sound writes go through the game's installed
 * SDK. In particular this worker does not reset ARM, master audio or timers. */
#define SOUND_BASE UINT32_C(0xa0800000)
#define SOUND_END UINT32_C(0xa09f4000)
#define SOUND_FIRST UINT32_C(0xa0830040)
#define QUEUE_BASE UINT32_C(0xa080b200)
#define PORT_MASK UINT32_C(0xc0000000)
#define NONE UINT32_MAX
#define TIMER_TSTR UINT32_C(0xffd80004)
#define TIMER_TCOR UINT32_C(0xffd80008)
#define TIMER_TCNT UINT32_C(0xffd8000c)
#define CLOCK_FRQCR UINT32_C(0xffc00000)
#define TIMER_TCR UINT32_C(0xffd80010)
#define FINITE_GUARD_TICKS 15625u /* 20ms after a FULL nominal finite interval */
#define CLOCK_GAP_LIMIT 7812500u /* Fail closed on >10s or a clock reset. */
#define APPLY_LIMIT_TICKS 781250u /* 1s; checked only while game service runs. */
#define STACK_PATTERN UINT32_C(0xa55a5aa5)
#define CMD_PLAY 20u
#define CMD_PLAY2 21u
#define CMD_PAUSE 22u
#define CMD_RELEASE 23u
#define CMD_SEEK 27u
#define CMD_STOP 33u

extern uint8_t __toy_pilot_stack_bottom[] __asm__("__toy_pilot_stack_bottom");
extern uint8_t __toy_pilot_stack_top[] __asm__("__toy_pilot_stack_top");
extern uint8_t __toy_pilot_worker_end[] __asm__("__toy_pilot_worker_end");
volatile uint32_t kui_toy_pilot_bridge_active,kui_toy_pilot_bridge_skips;

struct mailbox { uint32_t command, p[3], first_fad, end_fad, track, generation; };
static struct {
    struct kui_toy_pilot_config config;
    struct kui_toy_pilot_snapshot stats;
    struct kui_toy_pilot_model model;
    struct kui_toy_pilot_driver_load driver_load;
    volatile struct mailbox mailbox;
    volatile uint32_t configured, sdk_ready, disabled;
    uint32_t verified_driver_epoch;
    uint32_t handled_generation;
    uint32_t first_fad,end_fad,track,fill_frame,resume_frame,repeat_left;
    uint32_t last_tick,clock_seen,play_ack_tick,play_ack_seen,play_seen;
    uint32_t queue_slot,queue_wait,stop_slot,stop_wait;
    uint32_t queue_tick,stop_tick;
    uint32_t transaction_bank,transaction_step,stop_action;
    uint32_t last_end_tick,gap_pending;
    _Alignas(32) uint8_t raw[8192];
    _Alignas(32) int16_t left[128],right[128];
} owner;

static const uint8_t driver_digest[32]={
    0x47,0x7e,0xde,0x37,0x66,0xc2,0x7f,0xa5,0x8e,0x4c,0x14,0xd5,0x21,0x8c,0x58,0x3b,
    0x78,0x06,0xa2,0x9c,0x32,0x8a,0x6e,0x0d,0x49,0x65,0x29,0x33,0x75,0xb7,0x04,0xe5
};

static uint32_t word(uint32_t a) { return *(volatile const uint32_t *)(uintptr_t)a; }
static uint16_t half(uint32_t a) { return *(volatile const uint16_t *)(uintptr_t)a; }
static uint8_t byte(uint32_t a) { return *(volatile const uint8_t *)(uintptr_t)a; }
static uint32_t mask_begin(void) {
    uint32_t sr,masked;
    __asm__ __volatile__("stc sr,%0" : "=r"(sr));
    masked=sr|0xf0u;
    __asm__ __volatile__("ldc %0,sr" : : "r"(masked) : "memory");
    return sr;
}
static void mask_end(uint32_t sr) {
    __asm__ __volatile__("ldc %0,sr" : : "r"(sr) : "memory");
}
static void publish(void) { __asm__ __volatile__("" : : : "memory"); }
static uint32_t ticks(void) { return ~word(TIMER_TCNT); }
static void maximum(uint32_t *out,uint32_t value) { if(value>*out) *out=value; }
static uint32_t crc32(const void *data,uint32_t bytes) {
    const uint8_t *p=data;uint32_t crc=UINT32_MAX;
    for(uint32_t i=0;i<bytes;i++) {
        crc^=p[i];
        for(unsigned b=0;b<8u;b++) crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
}
static void fault(uint32_t reason) {
    if(!owner.stats.fault) owner.stats.fault=reason;
    owner.stats.state=KUI_TOY_PILOT_FAULT;owner.disabled=1;
    publish();owner.stats.applied_generation=owner.stats.generation;
    /* Every submitted play is finite. Fault never enables a hardware loop or
     * writes a potentially active bank. Existing SDK shutdown still owns its
     * normal all-port teardown; a worker fault cannot replace that teardown. */
}
static void stack_sample(void) {
    const uint32_t *p=(const uint32_t *)__toy_pilot_stack_bottom;
    const uint32_t *end=(const uint32_t *)__toy_pilot_stack_top;
    for(unsigned i=0;i<16u;i++) if(p[i]!=STACK_PATTERN) {
        owner.stats.stack_fault=1;fault(KUI_TOY_PILOT_FAULT_STACK);return;
    }
    while(p<end && *p==STACK_PATTERN) ++p;
    maximum(&owner.stats.stack_used,(uint32_t)((uintptr_t)end-(uintptr_t)p));
}
static const struct kui_retail_manifest *manifest(void) {
    return (const struct kui_retail_manifest *)(uintptr_t)owner.config.manifest;
}
/* No private-stack selection, allocation or SDK call. Mandatory lifecycle
 * revocation is safe even when it interrupts a suspended worker bridge. */
void kui_toy_pilot_worker_revoke(void) {
    kui_toy_pilot_driver_load_clear(&owner.driver_load);
    owner.verified_driver_epoch=0;
    owner.sdk_ready=0;owner.disabled=1;owner.stats.driver_verified=0;
    owner.stats.sound_generation=0;
    if(owner.mailbox.generation<0x7fffffffu) ++owner.mailbox.generation;
    owner.stats.generation=owner.mailbox.generation;
    publish();owner.stats.applied_generation=owner.stats.generation;
}
static bool single_audio(uint32_t track,uint32_t *first,uint32_t *end) {
    const struct kui_retail_manifest *m=manifest();
    if(!m || !track || track>m->track_count || m->track_count>99u) return false;
    const struct kui_retail_track *t=&m->slots[track-1u].track;
    if(kui_retail_track_control(t) || !t->extent_count ||
       kui_retail_track_sector_bytes(t)!=2352u || t->start_lba>=t->end_lba ||
       t->end_lba>719850u) return false;
    *first=t->start_lba+150u;*end=t->end_lba+150u;
    return true;
}
uint32_t kui_toy_pilot_worker_initialize(const struct kui_toy_pilot_config *c) {
    if(!c || c->magic!=KUI_TOY_PILOT_MAGIC || c->version!=KUI_TOY_PILOT_API || c->bytes!=sizeof(*c) ||
       c->manifest<0x8c000000u || c->manifest>=0x8c010000u ||
       c->read_raw<0x8c000000u || c->read_raw>=0x8c010000u || (c->read_raw&1u) ||
       c->resident_active<0x8c000000u || c->resident_active>=0x8c010000u ||
       c->data_pending<0x8c000000u || c->data_pending>=0x8c010000u ||
       (c->resident_active&3u) || (c->data_pending&3u) ||
       c->main_lease_begin!=KUI_TOY_PILOT_WORKER_BEGIN || c->main_lease_end!=0x8d000000u ||
       c->worker_begin!=KUI_TOY_PILOT_WORKER_BEGIN || c->worker_end!=(uint32_t)(uintptr_t)__toy_pilot_worker_end ||
       c->worker_end>KUI_TOY_PILOT_WORKER_END || !c->code_bytes ||
       c->code_bytes>c->worker_end-c->worker_begin) return 0;
    owner.config=*c;owner.configured=1;owner.transaction_bank=NONE;
    kui_toy_pilot_model_init(&owner.model);
    owner.stats=(struct kui_toy_pilot_snapshot){.magic=KUI_TOY_PILOT_MAGIC,.version=1,
        .bytes=sizeof(owner.stats),.state=KUI_TOY_PILOT_STOPPED,.generation=1,
        .main_begin=c->main_lease_begin,.main_end=c->main_lease_end,.worker_end=c->worker_end};
    owner.mailbox.generation=1;owner.handled_generation=1;
    stack_sample();return owner.stats.fault?0u:1u;
}
/* amInit has not loaded the driver yet. Remember its allocated destination;
 * do not read that uninitialized allocation. The lifecycle wrapper already
 * revoked the old generation on the original game stack before this bridge. */
void kui_toy_pilot_worker_pre_init(uint32_t image,uint32_t bytes) {
    uint32_t sr=mask_begin();
    owner.sdk_ready=0;owner.disabled=1;owner.stats.driver_verified=0;
    owner.verified_driver_epoch=0;
    owner.stats.driver_crc=0;
    owner.stats.sound_address=owner.stats.sound_bytes=owner.stats.sound_generation=0;
    kui_toy_pilot_driver_load_clear(&owner.driver_load);
    if(owner.stats.driver_generation==UINT32_MAX) {
        fault(KUI_TOY_PILOT_FAULT_GENERATION);mask_end(sr);return;
    }
    ++owner.stats.driver_generation;
    owner.stats.driver_bytes=bytes;
    if(!owner.configured || !kui_toy_pilot_driver_load_prepare(&owner.driver_load,
       image,bytes,owner.stats.driver_generation,owner.mailbox.generation)) {
        fault(KUI_TOY_PILOT_FAULT_DRIVER);mask_end(sr);return;
    }
    mask_end(sr);
    stack_sample();
}
/* The exact original file loader has returned, but amInit has not initialized
 * the SDK or installed the ARM driver. Its output is now valid for hashing.
 * Failed, different, repeated or revoked loads never establish an identity. */
void kui_toy_pilot_worker_post_load(uint32_t result,uint32_t image) {
    struct kui_toy_pilot_driver_load ticket;
    uint32_t sr=mask_begin();
    if(!kui_toy_pilot_driver_load_take(&owner.driver_load,result,image,
       owner.stats.driver_generation,owner.mailbox.generation,&ticket)) {
        fault(KUI_TOY_PILOT_FAULT_DRIVER);mask_end(sr);return;
    }
    mask_end(sr);
    struct kui_sha256 sha;uint8_t digest[32];
    kui_sha256_init(&sha);kui_sha256_update(&sha,(const void *)(uintptr_t)image,ticket.bytes);
    kui_sha256_digest(&sha,digest);
    uint32_t crc=crc32((const void *)(uintptr_t)image,ticket.bytes);
    bool digest_matches=!memcmp(digest,driver_digest,sizeof(digest));
    sr=mask_begin();
    if(!kui_toy_pilot_driver_load_current(&ticket,owner.stats.driver_generation,
       owner.mailbox.generation)) {
        fault(KUI_TOY_PILOT_FAULT_GENERATION);mask_end(sr);return;
    }
    owner.stats.driver_crc=crc;
    if(!digest_matches) { fault(KUI_TOY_PILOT_FAULT_DRIVER);mask_end(sr);return; }
    owner.stats.driver_verified=1;owner.disabled=0;owner.stats.fault=0;
    owner.verified_driver_epoch=ticket.epoch;
    owner.stats.state=KUI_TOY_PILOT_STOPPED;
    owner.stats.sound_generation=owner.stats.driver_generation;
    owner.stop_wait=owner.queue_wait=owner.play_ack_seen=owner.play_seen=0;
    owner.transaction_bank=NONE;owner.clock_seen=0;
    kui_toy_pilot_model_init(&owner.model);
    owner.model.generation=owner.stats.generation;
    owner.handled_generation=owner.mailbox.generation;
    mask_end(sr);
    stack_sample();
}
void kui_toy_pilot_worker_post_init(uint32_t result) {
    uint32_t sr=mask_begin();
    owner.stats.sdk_init_result=result;
    owner.sdk_ready=result==1u && owner.stats.driver_verified && !owner.disabled &&
        owner.verified_driver_epoch && owner.verified_driver_epoch==owner.mailbox.generation;
    if(!owner.sdk_ready) fault(KUI_TOY_PILOT_FAULT_DRIVER);
    mask_end(sr);
    stack_sample();
}
void kui_toy_pilot_worker_shutdown(void) {
    if(!owner.configured) return;
    ++owner.stats.shutdowns;kui_toy_pilot_worker_revoke();
    owner.stats.sound_generation=0;owner.stats.state=KUI_TOY_PILOT_OFF;
    owner.stop_wait=owner.queue_wait=0;owner.transaction_bank=NONE;
    publish();owner.stats.applied_generation=owner.stats.generation;
}
void kui_toy_pilot_worker_allstop(uint32_t upper,uint32_t lower) {
    if(upper!=UINT32_MAX || lower!=UINT32_MAX || !owner.configured) return;
    /* Initializer's known global stop comes before our first allocation.
     * Preserve the freshly verified input identity for that completed init. */
    if(!owner.stats.sound_address) return;
    kui_toy_pilot_worker_revoke();owner.stats.state=KUI_TOY_PILOT_OFF;
    owner.stop_wait=owner.queue_wait=0;owner.transaction_bank=NONE;
    publish();owner.stats.applied_generation=owner.stats.generation;
}
uint32_t kui_toy_pilot_request(uint32_t command,uint32_t p0,uint32_t p1,uint32_t p2) {
    if(!owner.configured || owner.mailbox.generation>=0x7fffffffu) return 0;
    if(command!=KUI_TOY_PILOT_RESET && command!=CMD_STOP && (owner.disabled || !owner.sdk_ready)) return 0;
    uint32_t first=owner.mailbox.first_fad,end=owner.mailbox.end_fad,track=owner.mailbox.track;
    if(owner.mailbox.generation==owner.handled_generation) {
        first=owner.first_fad;end=owner.end_fad;track=owner.track;
    }
    if(command==CMD_PLAY) {
        if(p0!=p1 || (p2!=0u && p2!=15u) || !single_audio(p0,&first,&end)) return 0;
        track=p0;
    } else if(command==CMD_PLAY2) {
        /* Native PLAY21 endpoint inclusivity is not admitted by this pilot. */
        return 0;
    } else if(command!=CMD_STOP && command!=CMD_PAUSE && command!=CMD_RELEASE &&
              command!=KUI_TOY_PILOT_RESET) return 0;
    if((command==CMD_PAUSE || command==CMD_RELEASE) && (!first || first>=end)) return 0;
    uint32_t generation=owner.mailbox.generation+1u;
    owner.mailbox.command=command;owner.mailbox.p[0]=p0;owner.mailbox.p[1]=p1;owner.mailbox.p[2]=p2;
    owner.mailbox.first_fad=first;owner.mailbox.end_fad=end;owner.mailbox.track=track;
    publish();owner.mailbox.generation=generation;
    owner.stats.command=command;owner.stats.parameters[0]=p0;
    owner.stats.parameters[1]=p1;owner.stats.parameters[2]=p2;
    owner.stats.generation=generation;
    if(!owner.sdk_ready || owner.disabled) {
        owner.stats.state=KUI_TOY_PILOT_STOPPED;publish();owner.stats.applied_generation=generation;
    }
    return generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) {
    owner.stats.service_skips=kui_toy_pilot_bridge_skips;
    return &owner.stats;
}

/* Existing SDK scalar G2 observation: stable paired reads with the game's
 * DMA suspend/resume and exact interrupt-mask restoration. Never admit a
 * queue acknowledgement or finite retirement from an unguarded sound load. */
static bool sound_word(uint32_t address,uint32_t *out) {
    if(!out || ((uintptr_t)out&3u) || address<SOUND_BASE || address>=SOUND_END || (address&3u)) return false;
    uint32_t sr=mask_begin();
    if(owner.disabled || !owner.sdk_ready) { mask_end(sr);return false; }
    typedef int (*read_fn)(uint32_t,uint32_t *);
    int result=((read_fn)(uintptr_t)0x8c0840d6u)(address,out);
    mask_end(sr);return result==1;
}
static bool live_driver(void) {
    uint32_t base,active,current,heap;
    return owner.sdk_ready && owner.stats.driver_verified &&
        word(0x8c0a7318u)==1u && word(0x8c0a8940u)==1u && word(0x8c0af74cu)==1u &&
        word(0x8c112b08u)==QUEUE_BASE &&
        sound_word(SOUND_BASE+0x1464u,&base) && base==0x00800000u &&
        sound_word(SOUND_BASE+0xe0u,&active) && active==0x1468u &&
        sound_word(SOUND_BASE+0xe8u,&current) && current==0x14f0u &&
        sound_word(SOUND_BASE+0xecu,&heap) && heap==0x30040u;
}
static bool clock_sample(uint32_t *now) {
    /* Exact known retail clock profile: enabled PLLs and divider tuple0x00a,
     * running TMU0 PCLK/64 with full32-bit reload. Read-only admission;
     * never alter the game's timer or clock registers. */
    if(!(byte(TIMER_TSTR)&1u) || (half(TIMER_TCR)&0x27u)!=2u ||
       word(TIMER_TCOR)!=UINT32_MAX || (half(CLOCK_FRQCR)&0x0fffu)!=0x0e0au) return false;
    *now=ticks();
    if(owner.clock_seen) {
        uint32_t gap=*now-owner.last_tick;
        if(gap>CLOCK_GAP_LIMIT) return false;
        maximum(&owner.stats.service_gap_max,gap);
    }
    owner.last_tick=*now;owner.clock_seen=1;return true;
}
static uint32_t slot(void) {
    uint32_t producer=half(0x8c112b0cu);
    owner.stats.queue_producer=producer;
    if(!sound_word(SOUND_BASE+0x399cu,&owner.stats.queue_consumer)) fault(KUI_TOY_PILOT_FAULT_PORT);
    return QUEUE_BASE+16u*(producer&31u);
}
static bool consumed(uint32_t address) {
    uint32_t value;
    if(address<QUEUE_BASE || address>=QUEUE_BASE+512u || (address&15u)) return false;
    if(!sound_word(address,&value)) { fault(KUI_TOY_PILOT_FAULT_PORT);return false; }
    /* This SDK refuses occupied slots, so later reuse can delay our zero
     * observation but cannot precede dispatch of our successfully queued packet. */
    return !(value&0xffffu);
}
static bool observe_ports(void) {
    uint32_t flags,left,right;
    if(!sound_word(SOUND_BASE+0x14a4u,&flags) ||
       !sound_word(SOUND_BASE+0x15e8u,&left) || !sound_word(SOUND_BASE+0x15ecu,&right)) return false;
    owner.stats.active_left=(flags>>16)&255u;owner.stats.active_right=flags>>24;
    owner.stats.cursor_left=left;owner.stats.cursor_right=right;return true;
}
static uint32_t plane_address(uint32_t bank,uint32_t right) {
    return owner.stats.sound_address+bank*KUI_TOY_PILOT_BANK_BYTES+right*KUI_TOY_PILOT_MONO_BYTES;
}
static bool template_matches(uint32_t bank,uint32_t frames,bool playing) {
    for(uint32_t right=0;right<2u;right++) {
        uint32_t t=SOUND_BASE+0x2ca8u+right*0x48u,c,low,start,end,pitch;
        if(!sound_word(t,&c) || !sound_word(t+4u,&low) || !sound_word(t+8u,&start) ||
           !sound_word(t+12u,&end) || !sound_word(t+0x18u,&pitch)) return false;
        if(((c&0x7fu)<<16 | (low&0xffffu))!=(plane_address(bank,right)&0x7fffffu) ||
           (c&0x380u) || start!=0u || end!=frames-1u || pitch!=0u)
            return false;
        if(playing && (c&0x200u)) return false;
    }
    return true;
}
static bool sound_allocate(void) {
    uint32_t sr=mask_begin(),generation=owner.stats.driver_generation,address=0;
    if(owner.disabled || !owner.sdk_ready || owner.stats.sound_address) { mask_end(sr);return false; }
    typedef int (*alloc_fn)(uint32_t *,uint32_t,uint32_t,uint32_t,void *);
    int result=((alloc_fn)(uintptr_t)0x8c069c00u)(&address,KUI_TOY_PILOT_SOUND_BYTES,32u,1u,NULL);
    if(!result || owner.disabled || !owner.sdk_ready || owner.stats.driver_generation!=generation ||
       address<SOUND_FIRST || address>SOUND_END-KUI_TOY_PILOT_SOUND_BYTES || (address&31u)) {
        mask_end(sr);return false;
    }
    owner.stats.sound_address=address;owner.stats.sound_bytes=KUI_TOY_PILOT_SOUND_BYTES;
    owner.stats.sound_generation=generation;mask_end(sr);return true;
}
static bool api_port(uint32_t address,uint32_t port) {
    return ((int (*)(uint32_t))(uintptr_t)address)(port)!=0;
}
static bool api_pair(uint32_t address,uint32_t upper,uint32_t lower) {
    return ((int (*)(uint32_t,uint32_t))(uintptr_t)address)(upper,lower)!=0;
}
static bool api_setting(uint32_t address,uint32_t port,uint32_t setting) {
    return ((int (*)(uint32_t,uint32_t))(uintptr_t)address)(port,setting)!=0;
}
static bool queue_operation(uint32_t bank,uint32_t operation) {
    uint32_t sr=mask_begin();bool result=false;
    if(owner.disabled || owner.mailbox.generation!=owner.model.generation) { mask_end(sr);return false; }
    switch(operation) {
    case 0:case 1:result=api_port(0x8c068f20u,62u+operation);break;
    case 2:case 3: {
        uint32_t right=operation-2u;
        typedef int (*open_fn)(uint32_t,uint32_t,uint32_t,uint32_t,uint32_t);
        result=((open_fn)(uintptr_t)0x8c068b28u)(62u+right,plane_address(bank,right),
            owner.model.banks[bank].frames*2u,0u,44100u)!=0;break;
    }
    case 4:case 5:result=api_setting(0x8c069158u,62u+operation-4u,operation==4u?31u:0u);break;
    case 6:case 7:result=api_setting(0x8c0690d4u,62u+operation-6u,15u);break;
    case 8:result=((int (*)(uint32_t,uint32_t,uint32_t))(uintptr_t)0x8c068a04u)(0u,PORT_MASK,0u)!=0;break;
    default:mask_end(sr);return false;
    }
    /* SDK increments producer BEFORE selecting the packet slot. */
    uint32_t s=result?slot():0u;mask_end(sr);
    if(!result) { ++owner.stats.queue_errors;return false; }
    owner.queue_slot=s;owner.queue_wait=1;owner.queue_tick=ticks();return true;
}
static uint32_t finite_ticks(uint32_t frames) {
    /* Existing game TMU0:781250ticks/s. This bounded numerator fits32bits. */
    return (frames*15625u+881u)/882u+FINITE_GUARD_TICKS;
}
static bool prior_interval_over(uint32_t now) {
    uint32_t bank=owner.model.active_bank!=NONE?owner.model.active_bank:owner.model.pending_bank;
    if(bank==NONE) return true;
    if(!owner.play_ack_seen) return false;
    uint32_t elapsed=now-owner.play_ack_tick;
    if(elapsed>CLOCK_GAP_LIMIT) { fault(KUI_TOY_PILOT_FAULT_CLOCK);return false; }
    return elapsed>=finite_ticks(owner.model.banks[bank].frames);
}
static bool stop_begin(uint32_t action) {
    uint32_t sr=mask_begin();
    if(owner.disabled || owner.mailbox.generation!=owner.model.generation) { mask_end(sr);return false; }
    bool result=api_pair(0x8c068a96u,PORT_MASK,0u);
    uint32_t s=result?slot():0u;mask_end(sr);
    if(!result) { ++owner.stats.queue_errors;return false; }
    owner.stop_slot=s;owner.stop_wait=1;owner.stop_action=action;owner.stop_tick=ticks();
    owner.queue_wait=0;owner.transaction_bank=NONE;return true;
}
static void apply_action(uint32_t now) {
    uint32_t sr=mask_begin();struct mailbox m;
    m.command=owner.mailbox.command;m.p[0]=owner.mailbox.p[0];m.p[1]=owner.mailbox.p[1];m.p[2]=owner.mailbox.p[2];
    m.first_fad=owner.mailbox.first_fad;m.end_fad=owner.mailbox.end_fad;
    m.track=owner.mailbox.track;m.generation=owner.mailbox.generation;mask_end(sr);
    if(m.generation==owner.handled_generation) return;
    if(!kui_toy_pilot_model_reset(&owner.model)) { fault(KUI_TOY_PILOT_FAULT_GENERATION);return; }
    owner.model.generation=m.generation;owner.handled_generation=m.generation;
    if(m.command==CMD_PAUSE || m.command==CMD_RELEASE) {
        uint32_t bank=owner.model.active_bank;
        if(bank!=NONE) {
            uint32_t cursor=owner.stats.cursor_left<owner.stats.cursor_right?owner.stats.cursor_left:owner.stats.cursor_right;
            if(cursor<owner.model.banks[bank].frames)
                owner.resume_frame=(owner.model.banks[bank].first_frame+cursor)&~1u;
        }
    }
    if(m.command==CMD_PLAY) {
        owner.first_fad=m.first_fad;owner.end_fad=m.end_fad;owner.track=m.track;
        owner.fill_frame=owner.resume_frame=0;owner.repeat_left=m.p[2];
        owner.stats.position_fad=m.first_fad;owner.stats.track=m.track;owner.stats.end_fad=m.end_fad;
    } else if(m.command==CMD_RELEASE) owner.fill_frame=owner.resume_frame;
    else if(m.command==CMD_SEEK && m.p[0]>=owner.first_fad && m.p[0]<owner.end_fad)
        owner.fill_frame=owner.resume_frame=(m.p[0]-owner.first_fad)*588u;
    if(!stop_begin(m.command==CMD_SEEK?CMD_STOP:m.command)) {
        if(!owner.disabled && owner.mailbox.generation==owner.model.generation) fault(KUI_TOY_PILOT_FAULT_QUEUE);
        else ++owner.stats.stale_actions;
        return;
    }
    owner.stats.state=(m.command==CMD_PAUSE)?KUI_TOY_PILOT_PAUSED:KUI_TOY_PILOT_PREFILL;
    (void)now;
}
static void stop_step(uint32_t now) {
    if(!owner.stop_wait) return;
    now=ticks();
    ++owner.stats.stop_waits;
    if(now-owner.stop_tick>APPLY_LIMIT_TICKS) { fault(KUI_TOY_PILOT_FAULT_QUEUE);return; }
    uint32_t old_bank=owner.model.active_bank!=NONE?owner.model.active_bank:owner.model.pending_bank;
    if(old_bank!=NONE && !owner.play_ack_seen && consumed(owner.queue_slot)) {
        owner.play_ack_seen=1;owner.play_ack_tick=ticks();now=owner.play_ack_tick;
    }
    if(!consumed(owner.stop_slot) || !prior_interval_over(now)) return;
    if(owner.stats.active_left || owner.stats.active_right) return;
    if(!kui_toy_pilot_model_stop_applied(&owner.model,owner.model.generation,true,true,true)) {
        fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
    }
    owner.stop_wait=owner.play_ack_seen=owner.play_seen=0;
    if(owner.stop_action==CMD_PAUSE) owner.stats.state=KUI_TOY_PILOT_PAUSED;
    else if(owner.stop_action==CMD_STOP || owner.stop_action==KUI_TOY_PILOT_RESET ||
            (owner.stop_action==CMD_SEEK && !owner.first_fad)) owner.stats.state=KUI_TOY_PILOT_STOPPED;
    else owner.stats.state=KUI_TOY_PILOT_PREFILL;
    if(owner.stats.state==KUI_TOY_PILOT_STOPPED || owner.stats.state==KUI_TOY_PILOT_PAUSED) {
        publish();owner.stats.applied_generation=owner.model.generation;
    }
}
static void playback_step(uint32_t now) {
    uint32_t bank=owner.model.active_bank;
    if(bank==NONE) bank=owner.model.pending_bank;
    if(bank==NONE || owner.stop_wait) return;
    if(!owner.play_ack_seen) {
        if(!owner.queue_wait || !consumed(owner.queue_slot)) {
            if(now-owner.queue_tick>APPLY_LIMIT_TICKS) fault(KUI_TOY_PILOT_FAULT_QUEUE);
            ++owner.stats.start_waits;return;
        }
        owner.queue_wait=0;owner.play_ack_seen=1;owner.play_ack_tick=ticks();
        if(!template_matches(bank,owner.model.banks[bank].frames,true)) { fault(KUI_TOY_PILOT_FAULT_PORT);return; }
    }
    if(!template_matches(bank,owner.model.banks[bank].frames,true)) { fault(KUI_TOY_PILOT_FAULT_PORT);return; }
    now=ticks();
    if(owner.stats.active_left==255u && owner.stats.active_right==255u && !owner.play_seen) {
        if(!kui_toy_pilot_model_start_applied(&owner.model,bank,owner.model.banks[bank].generation,true,true)) {
            fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
        }
        owner.play_seen=1;++owner.stats.started_observed;owner.stats.state=KUI_TOY_PILOT_PLAYING;
        publish();owner.stats.applied_generation=owner.model.banks[bank].generation;
    }
    uint32_t cursor=owner.stats.cursor_left<owner.stats.cursor_right?owner.stats.cursor_left:owner.stats.cursor_right;
    if(cursor<owner.model.banks[bank].frames)
        owner.stats.position_fad=owner.first_fad+(owner.model.banks[bank].first_frame+cursor)/588u;
    uint32_t elapsed=now-owner.play_ack_tick;
    /* A TMU reset during this callback must not look like a completed finite
     * interval through unsigned underflow before the next service clock check. */
    if(elapsed>CLOCK_GAP_LIMIT) { fault(KUI_TOY_PILOT_FAULT_CLOCK);return; }
    if(elapsed<finite_ticks(owner.model.banks[bank].frames)) return;
    if(!owner.play_seen) { fault(KUI_TOY_PILOT_FAULT_PHASE);return; }
    if(owner.stats.active_left || owner.stats.active_right) {
        if(elapsed>finite_ticks(owner.model.banks[bank].frames)+APPLY_LIMIT_TICKS)
            fault(KUI_TOY_PILOT_FAULT_PHASE);
        return;
    }
    uint32_t frames=owner.model.banks[bank].frames,first=owner.model.banks[bank].first_frame;
    if(!kui_toy_pilot_model_finite_end(&owner.model,bank,owner.model.banks[bank].generation,true,true,true)) {
        fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
    }
    owner.stats.retired_frames+=frames;owner.resume_frame=first+frames;
    owner.stats.position_fad=owner.first_fad+owner.resume_frame/588u;
    ++owner.stats.bank_ends;++owner.stats.finite_ends;owner.play_ack_seen=owner.play_seen=0;
    owner.transaction_bank=NONE;owner.gap_pending=1;owner.last_end_tick=now;
    owner.stats.state=KUI_TOY_PILOT_PREFILL;
}
static bool copy_plane(uint32_t address,const void *source,uint32_t bytes,uint32_t generation,uint32_t bank) {
    uint32_t sr=mask_begin();
    if(owner.disabled || owner.mailbox.generation!=generation || owner.model.generation!=generation ||
       owner.stats.sound_generation!=owner.stats.driver_generation ||
       bank==owner.model.active_bank || bank==owner.model.pending_bank) { mask_end(sr);return false; }
    uint32_t started=ticks();
    /* Generic SDK copy restores SR in r0; it has no boolean success ABI.
     * Its nonnull/aligned/positive word-count preconditions are ours to prove. */
    if(!source || ((uintptr_t)source&3u) || !bytes || bytes>256u || (bytes&3u) ||
       owner.stats.sound_bytes!=KUI_TOY_PILOT_SOUND_BYTES || address<owner.stats.sound_address ||
       address>owner.stats.sound_address+owner.stats.sound_bytes-bytes || (address&3u)) { mask_end(sr);return false; }
    typedef void (*copy_fn)(uint32_t,const void *,uint32_t);
    ((copy_fn)(uintptr_t)0x8c083faeu)(address,source,bytes);
    uint32_t elapsed=ticks()-started;mask_end(sr);
    maximum(&owner.stats.copy_ticks_max,elapsed);++owner.stats.copy_calls;
    return true;
}
static void fill_step(void) {
    uint32_t data=word(owner.config.data_pending);
    if(word(owner.config.resident_active) || data==16u || data==17u) return;
    uint32_t bank=NONE;
    for(uint32_t i=0;i<2u;i++) if(owner.model.banks[i].state==KUI_TOY_PILOT_BANK_FILLING) {bank=i;break;}
    uint32_t total=(owner.end_fad-owner.first_fad)*588u;
    if(bank==NONE) {
        if(owner.fill_frame>=total) return;
        for(uint32_t i=0;i<2u;i++) if(owner.model.banks[i].state==KUI_TOY_PILOT_BANK_EMPTY &&
            i!=owner.model.active_bank && i!=owner.model.pending_bank) { bank=i;break; }
        if(bank==NONE) return;
        uint32_t frames=total-owner.fill_frame;
        if(frames>KUI_TOY_PILOT_BANK_FRAMES) frames=KUI_TOY_PILOT_BANK_FRAMES;
        if(!kui_toy_pilot_model_fill_begin(&owner.model,bank,owner.model.generation,owner.fill_frame,frames)) {
            fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
        }
    }
    if(bank==owner.model.active_bank || bank==owner.model.pending_bank) {
        ++owner.stats.active_bank_writes;fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
    }
    uint32_t generation=owner.model.generation,sector=owner.fill_frame/588u,skip=owner.fill_frame%588u;
    uint32_t left=owner.model.banks[bank].frames-owner.model.banks[bank].filled;
    uint32_t sectors=(left+skip+587u)/588u;
    if(sectors>2u) sectors=2u;
    if(sectors>owner.end_fad-owner.first_fad-sector) sectors=owner.end_fad-owner.first_fad-sector;
    if(!sectors) { fault(KUI_TOY_PILOT_FAULT_RANGE);return; }
    typedef int (*raw_fn)(uint32_t,uint32_t,void *);
    /* Prevent an IRQ action between the final epoch check and the callback's
     * own atomic card claim. This scope covers only the raw read; the callback
     * itself already requires/makes a masked claim and restores exact SR. */
    uint32_t sr=mask_begin();
    if(owner.disabled || owner.mailbox.generation!=generation) {
        mask_end(sr);++owner.stats.stale_actions;return;
    }
    ++owner.stats.raw_calls;
    int result=((raw_fn)(uintptr_t)owner.config.read_raw)(owner.first_fad-150u+sector,sectors,owner.raw);
    mask_end(sr);
    if(result) { ++owner.stats.raw_errors;fault(KUI_TOY_PILOT_FAULT_CARD);return; }
    owner.stats.raw_bytes+=sectors*2352u;
    if(owner.disabled || owner.mailbox.generation!=generation) { ++owner.stats.stale_actions;return; }
    uint32_t frames=sectors*588u-skip;if(frames>left) frames=left;
    for(uint32_t done=0;done<frames;) {
        if(owner.disabled || owner.mailbox.generation!=generation) { ++owner.stats.stale_actions;return; }
        uint32_t take=frames-done;if(take>128u) take=128u;
        if(take<2u || (take&1u)) { fault(KUI_TOY_PILOT_FAULT_RANGE);return; }
        for(uint32_t i=0;i<take;i++) {
            const uint8_t *p=owner.raw+(skip+done+i)*4u;
            owner.left[i]=(int16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8);
            owner.right[i]=(int16_t)((uint16_t)p[2]|(uint16_t)p[3]<<8);
        }
        uint32_t offset=(owner.model.banks[bank].filled+done)*2u;
        if(!copy_plane(plane_address(bank,0)+offset,owner.left,take*2u,generation,bank)) {
            if(!owner.disabled && owner.mailbox.generation==generation) fault(KUI_TOY_PILOT_FAULT_PORT);
            else ++owner.stats.stale_actions;
            return;
        }
        if(!copy_plane(plane_address(bank,1)+offset,owner.right,take*2u,generation,bank)) {
            if(!owner.disabled && owner.mailbox.generation==generation) fault(KUI_TOY_PILOT_FAULT_PORT);
            else ++owner.stats.stale_actions;
            return;
        }
        if(owner.disabled || owner.mailbox.generation!=generation) { ++owner.stats.stale_actions;return; }
        done+=take;
    }
    if(!kui_toy_pilot_model_fill_commit(&owner.model,bank,generation,frames)) {
        fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
    }
    owner.fill_frame+=frames;owner.stats.filled_frames+=frames;
    if(owner.model.banks[bank].state==KUI_TOY_PILOT_BANK_READY) ++owner.stats.bank_fills;
}
static void start_step(uint32_t now) {
    if(owner.model.active_bank!=NONE || owner.model.pending_bank!=NONE || owner.stop_wait) return;
    if(owner.transaction_bank==NONE) {
        uint32_t selected=NONE;
        for(uint32_t i=0;i<2u;i++) if(owner.model.banks[i].state==KUI_TOY_PILOT_BANK_READY &&
            (selected==NONE || owner.model.banks[i].first_frame<owner.model.banks[selected].first_frame)) selected=i;
        if(selected==NONE) return;
        owner.transaction_bank=selected;owner.transaction_step=0;
    }
    if(owner.queue_wait) {
        if(!consumed(owner.queue_slot)) {
            if(now-owner.queue_tick>APPLY_LIMIT_TICKS) fault(KUI_TOY_PILOT_FAULT_QUEUE);
            ++owner.stats.start_waits;return;
        }
        owner.queue_wait=0;
    }
    uint32_t bank=owner.transaction_bank;
    if(owner.transaction_step==8u && !template_matches(bank,owner.model.banks[bank].frames,false)) {
        fault(KUI_TOY_PILOT_FAULT_PORT);return;
    }
    if(!queue_operation(bank,owner.transaction_step)) {
        if(!owner.disabled && owner.mailbox.generation==owner.model.generation) fault(KUI_TOY_PILOT_FAULT_QUEUE);
        else ++owner.stats.stale_actions;
        return;
    }
    if(owner.transaction_step++==8u) {
        if(!kui_toy_pilot_model_start_queued(&owner.model,bank,owner.model.generation)) {
            fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
        }
        owner.stats.state=KUI_TOY_PILOT_START_WAIT;++owner.stats.bank_starts;
        if(owner.gap_pending) {
            ++owner.stats.handoff_gaps;maximum(&owner.stats.gap_ticks_max,now-owner.last_end_tick);
            owner.gap_pending=0;
        }
        owner.play_ack_seen=owner.play_seen=0;
    }
}
void kui_toy_pilot_worker_step(void) {
    if(!owner.configured || owner.disabled) return;
    uint32_t sr;
    __asm__ __volatile__("stc sr,%0" : "=r"(sr));
    if(sr&0x10000000u) { ++owner.stats.service_skips;return; }
    ++owner.stats.service_calls;
    if(!owner.sdk_ready || word(0x8c0a7318u)!=1u) return;
    if(!live_driver()) { fault(KUI_TOY_PILOT_FAULT_DRIVER);return; }
    uint32_t now;if(!clock_sample(&now)) { fault(KUI_TOY_PILOT_FAULT_CLOCK);return; }
    if(!owner.stats.sound_address && !sound_allocate()) { fault(KUI_TOY_PILOT_FAULT_HEAP);return; }
    if(!observe_ports()) { fault(KUI_TOY_PILOT_FAULT_PORT);return; }
    apply_action(now);if(owner.disabled) return;
    stop_step(now);if(owner.stop_wait || owner.disabled) { stack_sample();return; }
    playback_step(now);if(owner.disabled) return;
    if(owner.stats.state==KUI_TOY_PILOT_PREFILL || owner.stats.state==KUI_TOY_PILOT_PLAYING ||
       owner.stats.state==KUI_TOY_PILOT_START_WAIT) {
        fill_step();if(owner.disabled) return;
        start_step(now);
        uint32_t total=(owner.end_fad-owner.first_fad)*588u;
        if(owner.fill_frame==total && owner.model.active_bank==NONE && owner.model.pending_bank==NONE &&
           owner.model.banks[0].state==KUI_TOY_PILOT_BANK_EMPTY && owner.model.banks[1].state==KUI_TOY_PILOT_BANK_EMPTY) {
            if(owner.repeat_left) { if(owner.repeat_left!=15u) --owner.repeat_left;owner.fill_frame=owner.resume_frame=0; }
            else owner.stats.state=KUI_TOY_PILOT_EOF;
        }
    }
    maximum(&owner.stats.step_ticks_max,ticks()-now);stack_sample();
}
