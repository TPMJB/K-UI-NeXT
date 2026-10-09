/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot.h"
#if KUI_TOY_PILOT_ASYNC_CDDA && !KUI_TOY_PILOT_SHARED_SCI
#error Asynchronous CDDA requires the shared SCI owner
#endif
#if KUI_TOY_PILOT_SHARED_SCI
#include "toy_pilot_sci.h"
extern uint8_t __toy_pilot_gd_stack_bottom[] __asm__("__toy_pilot_gd_stack_bottom");
extern uint8_t __toy_pilot_gd_stack_top[] __asm__("__toy_pilot_gd_stack_top");
#endif
#include "kui/toy_pilot_driver_load.h"
#include "kui/toy_pilot_bus.h"
#include "kui/toy_pilot_lease.h"
#include "kui/toy_pilot_ring.h"
#include "kui/retail_image.h"
#include "kui/hash.h"
#include <stddef.h>
#include <string.h>

/* Exact installed Toy driver protocol. All new G2 work is bounded; no calls
 * into the game's unbounded sound helpers, ARM reset or timer programming. */
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
#define CPU_CCR UINT32_C(0xff00001c)
#define TIMER_TCR UINT32_C(0xffd80010)
#define FINITE_GUARD_TICKS 15625u /* 20ms after the full post-STOP interval. */
#define CLOCK_GAP_LIMIT 7812500u /* Fail closed on >10s or a clock reset. */
#define APPLY_LIMIT_TICKS 781250u /* 1s; checked only while game service runs. */
#define SETUP_BURST_TICKS 1563u /* 2ms plus at most one bounded publication. */
#define REFILL_VISIT_TICKS 12500u /* 16ms admission budget, not a card timeout. */
#define REFILL_QUANTA 4u
#define STACK_PATTERN UINT32_C(0xa55a5aa5)
#define CMD_PLAY 20u
#define CMD_PLAY2 21u
#define CMD_PAUSE 22u
#define CMD_RELEASE 23u
#define CMD_STOP 33u
#define RING_RESTART UINT32_C(0x10001)
#define RING_EOF UINT32_C(0x10002)
_Static_assert(KUI_TOY_PILOT_BLOCKS==KUI_TOY_RING_BLOCKS &&
    KUI_TOY_PILOT_BLOCK_FRAMES==KUI_TOY_RING_BLOCK &&
    KUI_TOY_RING_BLOCK*KUI_TOY_RING_BLOCKS==KUI_TOY_RING_FRAMES &&
    KUI_TOY_PILOT_RING_MONO_BYTES==KUI_TOY_RING_FRAMES*2u &&
    KUI_TOY_PILOT_SOUND_BYTES==KUI_TOY_PILOT_RING_MONO_BYTES*2u,
    "Toy block ownership covers the unchanged stereo hardware ring");

extern uint8_t __toy_pilot_stack_bottom[] __asm__("__toy_pilot_stack_bottom");
extern uint8_t __toy_pilot_stack_top[] __asm__("__toy_pilot_stack_top");
extern uint8_t __toy_pilot_worker_end[] __asm__("__toy_pilot_worker_end");
volatile uint32_t kui_toy_pilot_bridge_active,kui_toy_pilot_bridge_skips;
volatile uint32_t kui_toy_pilot_updater_entries,kui_toy_pilot_updater_returns;
volatile uint32_t kui_toy_pilot_pause_entries,kui_toy_pilot_pause_retries;
volatile uint32_t kui_toy_pilot_pause_pumps,kui_toy_pilot_pause_retired;
volatile uint32_t kui_toy_pilot_pause_detail,kui_toy_pilot_pause_max_attempts;
extern volatile uint32_t kui_toy_pilot_native_diagnostics[8];

struct mailbox {
    uint32_t command, first_fad, end_fad, track, generation;
    uint32_t play_generation, repeats;
};
struct packet_ack {
    uint32_t words[4], driver_generation, request_generation;
};
static struct {
    struct kui_toy_pilot_config config;
    struct kui_toy_pilot_snapshot stats;
#if KUI_TOY_PILOT_SHARED_SCI
    uint32_t sci_report[16]; /* Appended telemetry; v8prefix stays448bytes. */
#endif
    struct kui_toy_pilot_driver_load driver_load;
    volatile struct mailbox mailbox;
    volatile uint32_t configured, sdk_ready, disabled;
    volatile uint32_t pause_fence;
    uint32_t terminal_reported;
    uint32_t verified_driver_epoch;
    uint32_t handled_generation,selected_play_generation;
    uint32_t first_fad,end_fad,track,fill_frame,resume_frame,repeat_left;
    uint32_t last_tick,clock_seen,play_ack_seen,play_seen;
    uint32_t ring_queued,ring_running,ring_cursor,ring_tick,ring_sample_age;
    uint32_t ring_played,ring_consumed,ring_origin,ring_end,ring_fill_stream;
    uint32_t ring_observed_tick,ring_probe_seen;
    uint32_t ring_window_tick[KUI_TOY_PILOT_BLOCKS],ring_window_calls[KUI_TOY_PILOT_BLOCKS];
    struct kui_toy_ring_probe ring_probe;
    uint32_t stop_ack_seen,stop_proof_tick;
    uint32_t queue_slot,queue_wait,stop_slot,stop_wait;
    struct packet_ack queue_ack,stop_ack;
    uint32_t queue_tick,stop_tick;
    uint32_t transaction_bank,transaction_step,stop_action;
    uint32_t last_end_tick,gap_pending;
    uint32_t bus_defer_tick,bus_defer_seen,bus_deferred;
    uint32_t data_blocked_tick;
    uint32_t raw_generation,raw_lba;
#if KUI_TOY_PILOT_ASYNC_CDDA
    uint32_t raw_request_generation,raw_request_lba,raw_request_tick;
#endif
    _Alignas(32) uint8_t raw[KUI_TOY_PILOT_RAW_BYTES*KUI_TOY_PILOT_RAW_SECTORS];
    _Alignas(32) int16_t left[128],right[128];
    /* Keep mailbox/control scalars near the unchanged telemetry prefix.
     * Eight private block records need no external offset contract. */
    struct kui_toy_pilot_model model;
} owner;

static const uint8_t driver_digest[32]={
    0x47,0x7e,0xde,0x37,0x66,0xc2,0x7f,0xa5,0x8e,0x4c,0x14,0xd5,0x21,0x8c,0x58,0x3b,
    0x78,0x06,0xa2,0x9c,0x32,0x8a,0x6e,0x0d,0x49,0x65,0x29,0x33,0x75,0xb7,0x04,0xe5
};

#ifdef KUI_TOY_PILOT_WORKER_TEST
extern uint32_t kui_toy_pilot_worker_test_read(uint32_t,unsigned);
extern uint32_t kui_toy_pilot_worker_test_sr(void);
extern void kui_toy_pilot_worker_test_set_sr(uint32_t);
extern void kui_toy_pilot_worker_test_terminal(void);
static uint32_t word(uint32_t a) { return kui_toy_pilot_worker_test_read(a,4); }
static uint16_t half(uint32_t a) { return (uint16_t)kui_toy_pilot_worker_test_read(a,2); }
static uint8_t byte(uint32_t a) { return (uint8_t)kui_toy_pilot_worker_test_read(a,1); }
static uint32_t status_register(void) { return kui_toy_pilot_worker_test_sr(); }
static uint32_t mask_begin(void) {
    uint32_t sr=status_register();kui_toy_pilot_worker_test_set_sr(sr|0xf0u);return sr;
}
static void mask_end(uint32_t sr) { kui_toy_pilot_worker_test_set_sr(sr); }
static void publish(void) { }
#else
static uint32_t word(uint32_t a) { return *(volatile const uint32_t *)(uintptr_t)a; }
static uint16_t half(uint32_t a) { return *(volatile const uint16_t *)(uintptr_t)a; }
static uint8_t byte(uint32_t a) { return *(volatile const uint8_t *)(uintptr_t)a; }
static uint32_t mask_begin(void) {
    uint32_t sr,masked;
    __asm__ __volatile__("stc sr,%0" : "=r"(sr));
    masked=sr|0xf0u;
    __asm__ __volatile__("ldc %0,sr" : : "r"(masked) : "memory","t");
    return sr;
}
static void mask_end(uint32_t sr) {
    __asm__ __volatile__("ldc %0,sr" : : "r"(sr) : "memory","t");
}
static void publish(void) { __asm__ __volatile__("" : : : "memory"); }
static uint32_t status_register(void) {
    uint32_t sr;__asm__ __volatile__("stc sr,%0":"=r"(sr));return sr;
}
#endif
static uint32_t ticks(void) { return ~word(TIMER_TCNT); }
static void maximum(uint32_t *out,uint32_t value) { if(value>*out) *out=value; }
/* Diagnostics only: retain the sampled eligibility-denial interval until
 * its next opportunity or control transition. These short publication
 * masks serialize with mandatory lifecycle revoke; they cover neither card
 * nor G2 work. A closed interval performs no timer, card or G2 read. */
static void data_blocked_close_masked(void) {
    if(!owner.stats.data_blocked_open) return;
    uint32_t elapsed=ticks()-owner.data_blocked_tick;
    maximum(&owner.stats.data_blocked_ticks_max,elapsed);
    owner.stats.data_blocked_ticks_total+=elapsed;
    ++owner.stats.data_blocked_intervals;
    owner.stats.data_blocked_open=0u;
}
static void data_blocked_close(void) {
    uint32_t sr=mask_begin();data_blocked_close_masked();mask_end(sr);
}
static void data_blocked_sample(uint32_t command) {
    uint32_t sr=mask_begin();
    /* Revoke can interrupt just before this mask. Do not reopen a sampled
     * interval for its revoked driver or a superseded control generation. */
    if(owner.disabled || !owner.sdk_ready || owner.mailbox.generation!=owner.model.generation ||
       (command!=16u && command!=17u) || !owner.ring_running || owner.stop_wait ||
       owner.model.active_bank>=KUI_TOY_PILOT_BLOCKS ||
       (owner.model.banks[(owner.ring_fill_stream/KUI_TOY_RING_BLOCK)%KUI_TOY_PILOT_BLOCKS].state!=
           KUI_TOY_PILOT_BANK_EMPTY &&
       owner.model.banks[(owner.ring_fill_stream/KUI_TOY_RING_BLOCK)%KUI_TOY_PILOT_BLOCKS].state!=
           KUI_TOY_PILOT_BANK_FILLING)) {
        data_blocked_close_masked();mask_end(sr);return;
    }
    ++owner.stats.data_blocked_calls;
    if(!owner.stats.data_blocked_open) {
        owner.data_blocked_tick=ticks();owner.stats.data_blocked_open=1u;
    }
    mask_end(sr);
}
static uint32_t crc32(const void *data,uint32_t bytes) {
    const uint8_t *p=data;uint32_t crc=UINT32_MAX;
    for(uint32_t i=0;i<bytes;i++) {
        crc^=p[i];
        for(unsigned b=0;b<8u;b++) crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
}
static void fault(uint32_t reason) {
#if KUI_TOY_PILOT_ASYNC_CDDA
    /* Pending DMA owns private transport staging even when a later clock
     * or driver check prevents another fill attempt. Revoke its tuple and
     * fence that RAW lease before disabling this worker generation. */
    if(!owner.stats.fault) {
        uint32_t sr=mask_begin();kui_toy_pilot_sci_audio_cancel();
        owner.raw_request_generation=0u;mask_end(sr);
    }
#endif
    /* A protocol/card/control failure can still have a known sound owner.
     * Make one bounded key-off publication before reporting. Bus/driver/
     * clock corruption does not permit this attempt; either result retains
     * every owned byte and never treats a packet as a reuse fence. */
    if(!owner.stats.fault && owner.ring_queued && owner.sdk_ready &&
       owner.stats.driver_verified && owner.stats.sound_generation==owner.stats.driver_generation &&
       (reason==KUI_TOY_PILOT_FAULT_CARD || reason==KUI_TOY_PILOT_FAULT_RANGE ||
        reason==KUI_TOY_PILOT_FAULT_PHASE || reason==KUI_TOY_PILOT_FAULT_NATIVE_CONTROL)) {
        uint32_t packet[4]={0xff9du,PORT_MASK,0u,0u},slot=0u,sr=mask_begin();
        enum kui_toy_pilot_bus_result result=kui_toy_pilot_bus_publish(packet,&slot);
        if(result!=KUI_TOY_PILOT_BUS_OK) ++owner.stats.queue_errors;
        mask_end(sr);
    }
    data_blocked_close();
    if(!owner.stats.fault) owner.stats.fault=reason;
    owner.stats.state=KUI_TOY_PILOT_FAULT;owner.disabled=1;
    publish();owner.stats.applied_generation=owner.stats.generation;
    /* A loop retains its entire lease on every fault. A failing G2/driver
     * path cannot safely synthesize teardown; terminal reporting may leave
     * owned audio repeating, but never authorizes another sample write. */
}
/* Fault helpers always return through their bounded SR scopes first. The low
 * resident entry then owns the nonreturning stack transfer and diagnostic;
 * the suspended game updater must not resume after a worker fault. */
static void terminal_dispatch(void) {
    if(!owner.configured || !owner.stats.fault || owner.terminal_reported) return;
    owner.terminal_reported=1;
#ifdef KUI_TOY_PILOT_WORKER_TEST
    kui_toy_pilot_worker_test_terminal();
#else
    ((void (*)(void))(uintptr_t)owner.config.terminal_entry)();
    __builtin_unreachable();
#endif
}
/* The pause adapter has already unwound its native admission/retirement
 * masks. Latch its scalar failure atomically, then report with the exact
 * inherited SR restored rather than abandoning a temporary mask scope. */
void kui_toy_pilot_worker_pause_fault(uint32_t detail) {
    uint32_t sr=mask_begin();
    if(!owner.stats.fault && owner.configured)
        kui_toy_pilot_native_diagnostics[4]|=(word(owner.config.resident_active)&1u)<<24;
    if(!kui_toy_pilot_pause_detail) kui_toy_pilot_pause_detail=detail;
    if(owner.configured) fault(KUI_TOY_PILOT_FAULT_NATIVE_CONTROL);
    mask_end(sr);
    terminal_dispatch();
}
static void stack_sample(void) {
#ifndef KUI_TOY_PILOT_WORKER_TEST
    const uint32_t *p=(const uint32_t *)__toy_pilot_stack_bottom;
    const uint32_t *end=(const uint32_t *)__toy_pilot_stack_top;
    for(unsigned i=0;i<16u;i++) if(p[i]!=STACK_PATTERN) {
        owner.stats.stack_fault=1;fault(KUI_TOY_PILOT_FAULT_STACK);return;
    }
    while(p<end && *p==STACK_PATTERN) ++p;
    maximum(&owner.stats.stack_used,(uint32_t)((uintptr_t)end-(uintptr_t)p));
#endif
}
static bool bus_result(enum kui_toy_pilot_bus_result result) {
    owner.stats.bus_last_result=(uint32_t)result;
    if(result==KUI_TOY_PILOT_BUS_OK) return true;
    if(result==KUI_TOY_PILOT_BUS_BUSY) {
        ++owner.stats.bus_deferrals;owner.bus_deferred=1;
        uint32_t now=ticks();
        if(!owner.bus_defer_seen) { owner.bus_defer_seen=1;owner.bus_defer_tick=now; }
        else if(now-owner.bus_defer_tick>APPLY_LIMIT_TICKS) fault(KUI_TOY_PILOT_FAULT_BUS);
    } else fault(KUI_TOY_PILOT_FAULT_BUS);
    return false;
}
static const struct kui_retail_manifest *manifest(void) {
    return (const struct kui_retail_manifest *)(uintptr_t)owner.config.manifest;
}
/* No private-stack selection, allocation or SDK call. Mandatory lifecycle
 * revocation is safe even when it interrupts a suspended worker bridge. */
#if KUI_TOY_PILOT_SHARED_SCI
bool kui_sci_sd_healthy(void) {
    return owner.configured &&
        ((bool (*)(void))(uintptr_t)owner.config.sci_healthy)();
}
#endif
void kui_toy_pilot_worker_revoke(void) {
#if KUI_TOY_PILOT_SHARED_SCI
    uint32_t sci_sr=mask_begin();
    kui_toy_pilot_sci_audio_cancel();
#if KUI_TOY_PILOT_ASYNC_CDDA
    owner.raw_request_generation=0u;
#endif
    mask_end(sci_sr);
#endif
    data_blocked_close();
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
       c->terminal_entry<0x8c004000u || c->terminal_entry>=0x8c007800u || (c->terminal_entry&1u) ||
       (c->resident_active&3u) || (c->data_pending&3u) ||
       c->main_lease_begin!=KUI_TOY_PILOT_WORKER_BEGIN || c->main_lease_end!=0x8d000000u ||
       c->worker_begin!=KUI_TOY_PILOT_WORKER_BEGIN || c->worker_end!=(uint32_t)(uintptr_t)__toy_pilot_worker_end ||
       c->worker_end>KUI_TOY_PILOT_WORKER_END || !c->code_bytes ||
       c->code_bytes>c->worker_end-c->worker_begin) return 0;
#if KUI_TOY_PILOT_SHARED_SCI
    if(c->sci_card<0x8c004000u || c->sci_card>=0x8c007800u || (c->sci_card&3u) ||
       c->sci_acquire<0x8c004000u || c->sci_acquire>=0x8c007800u || (c->sci_acquire&1u) ||
       c->sci_release<0x8c004000u || c->sci_release>=0x8c007800u || (c->sci_release&1u) ||
       c->sci_healthy<0x8c004000u || c->sci_healthy>=0x8c007800u || (c->sci_healthy&1u)) return 0;
#endif
    owner.config=*c;owner.configured=1;owner.transaction_bank=NONE;
#if KUI_TOY_PILOT_SHARED_SCI
    for(uint32_t *p=(uint32_t *)__toy_pilot_gd_stack_bottom;
        p<(uint32_t *)__toy_pilot_gd_stack_top;p++) *p=UINT32_C(0xa55a4aa5);
    kui_toy_pilot_sci_init(manifest(),(const struct kui_loader_sd *)(uintptr_t)c->sci_card,
        (enum kui_loader_sd_result (*)(void))(uintptr_t)c->sci_acquire,
        (void (*)(void))(uintptr_t)c->sci_release);
#endif
    kui_toy_pilot_model_init(&owner.model);
    owner.stats=(struct kui_toy_pilot_snapshot){.magic=KUI_TOY_PILOT_MAGIC,.version=KUI_TOY_PILOT_API,
        .bytes=sizeof(owner.stats),.state=KUI_TOY_PILOT_STOPPED,.generation=1,
        .main_begin=c->main_lease_begin,.main_end=c->main_lease_end,.worker_end=c->worker_end,
        .cache_policy=word(CPU_CCR)&0x105u,.service_visits_per_update=2u};
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
    owner.terminal_reported=0;
    owner.verified_driver_epoch=ticket.epoch;
    owner.stats.state=KUI_TOY_PILOT_STOPPED;
    owner.stats.sound_generation=owner.stats.driver_generation;
    owner.stop_wait=owner.queue_wait=owner.play_ack_seen=owner.play_seen=0;
    owner.transaction_bank=NONE;owner.clock_seen=owner.pause_fence=0;
    owner.ring_queued=owner.ring_running=owner.ring_probe_seen=owner.stop_ack_seen=0;
    owner.bus_defer_seen=owner.bus_defer_tick=owner.bus_deferred=0;
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
    uint32_t first=owner.mailbox.first_fad,end=owner.mailbox.end_fad,track=owner.mailbox.track;
    uint32_t play_generation=owner.mailbox.play_generation,repeats=owner.mailbox.repeats;
    if(owner.mailbox.generation==owner.handled_generation) {
        first=owner.first_fad;end=owner.end_fad;track=owner.track;
        play_generation=owner.selected_play_generation;
    }
    uint32_t generation=owner.mailbox.generation+1u;
    if(command==CMD_PLAY) {
        if(owner.disabled || !owner.sdk_ready || p0!=p1 ||
           (p2!=0u && p2!=15u) || !single_audio(p0,&first,&end)) return 0;
        track=p0;play_generation=generation;repeats=p2;
    } else if(command==CMD_PLAY2) {
        /* Native PLAY21 endpoint inclusivity is not admitted by this pilot. */
        return 0;
    } else if(command!=CMD_STOP && command!=CMD_PAUSE && command!=CMD_RELEASE &&
              command!=KUI_TOY_PILOT_RESET) return 0;
#if KUI_TOY_PILOT_SHARED_SCI
    /* Accepted replacement epochs revoke pending audio transport work. */
    kui_toy_pilot_sci_audio_cancel();
#if KUI_TOY_PILOT_ASYNC_CDDA
    owner.raw_request_generation=0u;
#endif
#endif
    if(command==KUI_TOY_PILOT_RESET) first=end=track=play_generation=repeats=0u;
    /* Native startup/movie control may pause or release before any PLAY.
     * Those commands are scalar no-ops while no audio bank is owned. A later
     * control also retains a PLAY accepted before the worker could select it. */
    owner.mailbox.command=command;
    owner.mailbox.first_fad=first;owner.mailbox.end_fad=end;owner.mailbox.track=track;
    owner.mailbox.play_generation=play_generation;owner.mailbox.repeats=repeats;
    if(command==CMD_PAUSE) owner.pause_fence=1u;
    publish();owner.mailbox.generation=generation;
    owner.stats.command=command;owner.stats.parameters[0]=p0;
    owner.stats.parameters[1]=p1;owner.stats.parameters[2]=p2;
    owner.stats.generation=generation;
    if(command==KUI_TOY_PILOT_RESET)
        owner.stats.position_fad=owner.stats.track=owner.stats.end_fad=0u;
    if(!owner.sdk_ready || owner.disabled) {
        if(command==KUI_TOY_PILOT_RESET) {
            /* A revoked/uninitialized sound generation owns no reusable
             * pilot voice. Its immediately applied RESET cancels selection
             * too; a later driver install must not resurrect an old track. */
            owner.mailbox.first_fad=owner.mailbox.end_fad=owner.mailbox.track=0u;
            owner.mailbox.play_generation=owner.mailbox.repeats=0u;
            owner.selected_play_generation=owner.first_fad=owner.end_fad=owner.track=0u;
            owner.fill_frame=owner.resume_frame=owner.repeat_left=0u;
            owner.pause_fence=0u;
        }
        owner.stats.state=command==CMD_PAUSE?KUI_TOY_PILOT_PAUSED:KUI_TOY_PILOT_STOPPED;
        publish();owner.stats.applied_generation=generation;
    }
    return generation;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) {
    if(owner.mailbox.play_generation!=owner.selected_play_generation) {
        /* Logical selection belongs to the accepted source even when a
         * subsequent control arrives before the main worker can apply it. */
        owner.stats.track=owner.mailbox.track;owner.stats.end_fad=owner.mailbox.end_fad;
        owner.stats.position_fad=owner.mailbox.first_fad;
    }
    owner.stats.service_skips=kui_toy_pilot_bridge_skips;
    owner.stats.updater_entries=kui_toy_pilot_updater_entries;
    owner.stats.updater_returns=kui_toy_pilot_updater_returns;
    owner.stats.pause_entries=kui_toy_pilot_pause_entries;
    owner.stats.pause_retries=kui_toy_pilot_pause_retries;
    owner.stats.pause_pumps=kui_toy_pilot_pause_pumps;
    owner.stats.pause_retired=kui_toy_pilot_pause_retired;
    owner.stats.pause_detail=kui_toy_pilot_pause_detail;
    owner.stats.pause_max_attempts=kui_toy_pilot_pause_max_attempts;
    owner.stats.cache_policy=word(CPU_CCR)&0x105u;
    owner.stats.service_visits_per_update=2u;
    owner.stats.native_owner=kui_toy_pilot_native_diagnostics[0];
    owner.stats.native_work_token=kui_toy_pilot_native_diagnostics[1];
    owner.stats.gd_owned_command=kui_toy_pilot_native_diagnostics[2];
    owner.stats.gd_owned_token=kui_toy_pilot_native_diagnostics[3];
    owner.stats.native_gd_state=kui_toy_pilot_native_diagnostics[4];
    owner.stats.check_token=kui_toy_pilot_native_diagnostics[5];
    owner.stats.check_destination=kui_toy_pilot_native_diagnostics[6];
    owner.stats.check_result=kui_toy_pilot_native_diagnostics[7];
#if KUI_TOY_PILOT_SHARED_SCI
    _Static_assert(sizeof(*kui_toy_pilot_sci_snapshot())==64u,"Shared transport page");
    memcpy(owner.sci_report,kui_toy_pilot_sci_snapshot(),64u);
#endif
    return &owner.stats;
}

/* Bounded stable observations refuse concurrent G2 DMA and restore exact SR. */
static bool sound_word(uint32_t address,uint32_t *out) {
    if(!out || ((uintptr_t)out&3u) || address<SOUND_BASE || address>=SOUND_END || (address&3u)) return false;
    uint32_t sr=mask_begin();
    if(owner.disabled || !owner.sdk_ready) { mask_end(sr);return false; }
    enum kui_toy_pilot_bus_result result=kui_toy_pilot_bus_read(address,out);
    mask_end(sr);return bus_result(result);
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
        if(gap>CLOCK_GAP_LIMIT) {
            if(!owner.pause_fence) return false;
            /* Intentional movie pauses can suspend service or reset TMU.
             * Restart the FULL post-STOP proof; a reset never proves elapsed
             * sample fetching. Ordinary clock resets still fail closed. */
            if(owner.stop_wait) owner.stop_tick=*now;
            if(owner.stop_ack_seen) owner.stop_proof_tick=*now;
            owner.bus_defer_seen=0;
        }
        maximum(&owner.stats.service_gap_max,gap);
    }
    owner.last_tick=*now;owner.clock_seen=1;return true;
}
static bool consumed(uint32_t address,const struct packet_ack *ack) {
    uint32_t value,sr;bool result=false;
    if(address<QUEUE_BASE || address>=QUEUE_BASE+512u || (address&15u)) return false;
    sr=mask_begin();
    if(owner.disabled || !owner.sdk_ready || !ack->driver_generation ||
       ack->driver_generation!=owner.stats.driver_generation ||
       ack->driver_generation!=owner.stats.sound_generation ||
       owner.mailbox.generation!=owner.model.generation ||
       (ack->request_generation!=owner.model.generation &&
        (!owner.stop_wait || !ack->request_generation ||
         ack->request_generation>owner.model.generation))) goto done;
    if(!sound_word(address,&value)) goto done;
    if(!(value&0xffffu) || value!=ack->words[0]) { result=true;goto done; }
    /* The original SDK and this publisher both refuse occupied slots. ARM
     * changes only the opcode/marker bytes, after dispatch. A different
     * packet therefore proves later reuse, even if native sound commands
     * hide the transient empty header between worker visits. Keep one outer
     * mask across the comparison: no SH publisher may replace a payload
     * underneath it. ARM clearing either low byte also proves dispatch.
     * Equal headers are insufficient: native group START/STOP has the same
     * opcode and marker, but different masks from our excluded port pair. */
    for(unsigned i=1u;i<4u;i++) {
        if(!sound_word(address+i*4u,&value)) goto done;
        if(value!=ack->words[i]) { result=true;goto done; }
    }
done:
    mask_end(sr);return result;
}
static bool observe_ports(void) {
    uint32_t flags,left,right;
    if(!sound_word(SOUND_BASE+0x14a4u,&flags) ||
       !sound_word(SOUND_BASE+0x15e8u,&left) || !sound_word(SOUND_BASE+0x15ecu,&right)) return false;
    owner.stats.active_left=(flags>>16)&255u;owner.stats.active_right=flags>>24;
    owner.stats.cursor_left=left;owner.stats.cursor_right=right;return true;
}
static uint32_t plane_address(uint32_t bank,uint32_t right) {
    return owner.stats.sound_address+right*KUI_TOY_PILOT_RING_MONO_BYTES+
        bank*KUI_TOY_PILOT_MONO_BYTES;
}
static bool template_matches(uint32_t bank,uint32_t frames,bool playing) {
    for(uint32_t right=0;right<2u;right++) {
        uint32_t t=SOUND_BASE+0x2ca8u+right*0x48u,c,low,start,end,pitch;
        if(!sound_word(t,&c) || !sound_word(t+4u,&low) || !sound_word(t+8u,&start) ||
           !sound_word(t+12u,&end) || !sound_word(t+0x18u,&pitch)) return false;
        if(((c&0x7fu)<<16 | (low&0xffffu))!=(plane_address(bank,right)&0x7fffffu) ||
           (c&0x180u) || start!=0u || end!=frames-1u || pitch!=0u)
            return false;
        if(playing && !(c&0x200u)) return false;
    }
    return true;
}
static bool sound_allocate(void) {
    uint32_t sr=mask_begin(),generation=owner.stats.driver_generation,address=0;
    if(owner.disabled || !owner.sdk_ready || owner.stats.sound_address) { mask_end(sr);return false; }
    enum kui_toy_pilot_bus_result result=kui_toy_pilot_lease_allocate(KUI_TOY_PILOT_SOUND_BYTES,32u,&address);
    if(!bus_result(result) || owner.disabled || !owner.sdk_ready || owner.stats.driver_generation!=generation ||
       address<SOUND_FIRST || address>SOUND_END-KUI_TOY_PILOT_SOUND_BYTES || (address&31u)) {
        mask_end(sr);return false;
    }
    owner.stats.sound_address=address;owner.stats.sound_bytes=KUI_TOY_PILOT_SOUND_BYTES;
    owner.stats.sound_generation=generation;mask_end(sr);return true;
}
static bool queue_operation(uint32_t bank,uint32_t operation) {
    uint32_t sr=mask_begin(),packet[4]={0},s=0;
    if(owner.disabled || owner.mailbox.generation!=owner.model.generation) { mask_end(sr);return false; }
    switch(operation) {
    case 0:case 1:packet[0]=0xff91u | (62u+operation)<<16;break;
    case 2:case 3: {
        uint32_t right=operation-2u;
        packet[0]=0xff90u | (62u+right)<<16;packet[1]=plane_address(bank,right);
        packet[2]=KUI_TOY_RING_FRAMES*2u;break;
    }
    case 4:case 5:packet[0]=0xff97u | (62u+operation-4u)<<16 | (operation==4u?31u:0u)<<24;break;
    case 6:case 7:packet[0]=0xff96u | (62u+operation-6u)<<16 | 15u<<24;break;
    case 8:packet[0]=0x0001ff9cu;packet[1]=PORT_MASK;break;
    default:mask_end(sr);return false;
    }
    enum kui_toy_pilot_bus_result result=kui_toy_pilot_bus_publish(packet,&s);
    owner.stats.queue_producer=half(0x8c112b0cu);
    if(result==KUI_TOY_PILOT_BUS_OK) {
        for(unsigned i=0;i<4u;i++) owner.queue_ack.words[i]=packet[i];
        owner.queue_ack.driver_generation=owner.stats.driver_generation;
        owner.queue_ack.request_generation=owner.model.generation;
    }
    mask_end(sr);
    if(!bus_result(result)) {
        if(result!=KUI_TOY_PILOT_BUS_BUSY) ++owner.stats.queue_errors;
        return false;
    }
    owner.queue_slot=s;owner.queue_wait=1;owner.queue_tick=ticks();return true;
}
static uint32_t finite_ticks(uint32_t frames) {
    /* Existing game TMU0:781250ticks/s. This bounded numerator fits32bits. */
    return (frames*15625u+881u)/882u+FINITE_GUARD_TICKS;
}
static bool prior_interval_over(uint32_t now) {
    if(!owner.ring_queued) return true;
    if(!owner.stop_ack_seen) return false;
    uint32_t elapsed=now-owner.stop_proof_tick;
    if(elapsed>CLOCK_GAP_LIMIT) { fault(KUI_TOY_PILOT_FAULT_CLOCK);return false; }
    return elapsed>=finite_ticks(KUI_TOY_RING_FRAMES);
}
static bool stop_begin(uint32_t action) {
    uint32_t sr=mask_begin();
    if(owner.disabled || owner.mailbox.generation!=owner.model.generation) { mask_end(sr);return false; }
    uint32_t packet[4]={0xff9du,PORT_MASK,0u,0u},s=0;
    enum kui_toy_pilot_bus_result result=kui_toy_pilot_bus_publish(packet,&s);
    owner.stats.queue_producer=half(0x8c112b0cu);
    if(result==KUI_TOY_PILOT_BUS_OK) {
        for(unsigned i=0;i<4u;i++) owner.stop_ack.words[i]=packet[i];
        owner.stop_ack.driver_generation=owner.stats.driver_generation;
        owner.stop_ack.request_generation=owner.model.generation;
    }
    mask_end(sr);
    if(!bus_result(result)) {
        if(result!=KUI_TOY_PILOT_BUS_BUSY) ++owner.stats.queue_errors;
        return false;
    }
    owner.stop_slot=s;owner.stop_wait=1;owner.stop_action=action;owner.stop_tick=ticks();
    owner.stop_ack_seen=0;
    owner.queue_wait=0;owner.transaction_bank=NONE;return true;
}
/* A control can supersede an accepted PLAY before the worker runs. Preserve
 * that selected source and repeat policy independently of the latest opcode;
 * never take a cursor from the previous track for a newly selected one. */
static bool select_play(const volatile struct mailbox *m) {
    if(m->play_generation==owner.selected_play_generation) return false;
    owner.selected_play_generation=m->play_generation;
    owner.first_fad=m->first_fad;owner.end_fad=m->end_fad;owner.track=m->track;
    owner.fill_frame=owner.resume_frame=0;owner.repeat_left=m->repeats;
    owner.stats.position_fad=m->first_fad;owner.stats.track=m->track;owner.stats.end_fad=m->end_fad;
    return true;
}
static void reset_selection(uint32_t generation) {
    uint32_t sr=mask_begin();
    if(owner.mailbox.generation==generation && owner.mailbox.command==KUI_TOY_PILOT_RESET) {
        owner.mailbox.first_fad=owner.mailbox.end_fad=owner.mailbox.track=0u;
        owner.mailbox.play_generation=owner.mailbox.repeats=0u;
        owner.selected_play_generation=owner.first_fad=owner.end_fad=owner.track=0u;
        owner.fill_frame=owner.resume_frame=owner.repeat_left=0u;
        owner.stats.position_fad=owner.stats.track=owner.stats.end_fad=0u;
        owner.pause_fence=0u;
    }
    mask_end(sr);
}
static void ring_resume(void);
static void apply_action(uint32_t now) {
    uint32_t sr=mask_begin();struct mailbox m;
    m.command=owner.mailbox.command;
    m.first_fad=owner.mailbox.first_fad;m.end_fad=owner.mailbox.end_fad;
    m.track=owner.mailbox.track;m.generation=owner.mailbox.generation;
    m.play_generation=owner.mailbox.play_generation;m.repeats=owner.mailbox.repeats;mask_end(sr);
    if(m.generation==owner.handled_generation) return;
    data_blocked_close();
    struct kui_toy_pilot_bank held[KUI_TOY_PILOT_BLOCKS];
    memcpy(held,owner.model.banks,sizeof(held));
    if(!kui_toy_pilot_model_reset(&owner.model)) { fault(KUI_TOY_PILOT_FAULT_GENERATION);return; }
    if(owner.ring_queued) memcpy(owner.model.banks,held,sizeof(held));
    owner.model.generation=m.generation;
    bool changed=select_play(&m);
    if(!changed && (m.command==CMD_PAUSE || m.command==CMD_RELEASE) &&
       owner.stop_action!=CMD_PAUSE) {
        if(owner.ring_queued) ring_resume();
    }
    if(m.command==CMD_RELEASE) owner.fill_frame=owner.resume_frame;
    if(!stop_begin(m.command)) {
        if(!owner.disabled && owner.mailbox.generation==owner.model.generation) {
            if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_QUEUE);
        } else ++owner.stats.stale_actions;
        return;
    }
    owner.handled_generation=m.generation;
    owner.stats.state=(m.command==CMD_PAUSE)?KUI_TOY_PILOT_PAUSED:KUI_TOY_PILOT_PREFILL;
    (void)now;
}
/* STOP removes hardware LPCTL. Only its acknowledged, full-ring finite
 * horizon (not a prior START horizon) can fence residual sample fetching. */
static void stop_step(uint32_t now) {
    if(!owner.stop_wait) return;
    ++owner.stats.stop_waits;now=ticks();
    if(!owner.stop_ack_seen) {
        if(!consumed(owner.stop_slot,&owner.stop_ack)) {
            if(now-owner.stop_tick>APPLY_LIMIT_TICKS) fault(KUI_TOY_PILOT_FAULT_QUEUE);
            return;
        }
        owner.stop_ack_seen=1;owner.stop_proof_tick=ticks();now=owner.stop_proof_tick;
    }
    if(!prior_interval_over(now)) return;
    if(owner.ring_queued && !template_matches(0u,KUI_TOY_RING_FRAMES,false)) {
        if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_PORT);
        return;
    }
    if(!observe_ports()) return; /* Fresh AFTER the post-STOP ownership fence. */
    if(owner.stats.active_left || owner.stats.active_right) {
        if(now-owner.stop_proof_tick>finite_ticks(KUI_TOY_RING_FRAMES)+APPLY_LIMIT_TICKS)
            fault(KUI_TOY_PILOT_FAULT_PHASE);
        return;
    }
    if(!kui_toy_pilot_model_stop_applied(&owner.model,owner.model.generation,true,true,true)) {
        fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
    }
    owner.stop_wait=owner.play_ack_seen=owner.play_seen=0;
    owner.ring_queued=owner.ring_running=owner.ring_probe_seen=0;
    owner.transaction_bank=NONE;
    if(owner.stop_action==CMD_PAUSE) owner.stats.state=KUI_TOY_PILOT_PAUSED;
    else if(owner.stop_action==RING_EOF) owner.stats.state=KUI_TOY_PILOT_EOF;
    else if(owner.stop_action==CMD_STOP || owner.stop_action==KUI_TOY_PILOT_RESET ||
            (owner.stop_action==CMD_RELEASE && !owner.first_fad))
        owner.stats.state=KUI_TOY_PILOT_STOPPED;
    else owner.stats.state=KUI_TOY_PILOT_PREFILL;
    if(owner.stop_action==RING_RESTART || owner.stop_action==CMD_RELEASE)
        owner.fill_frame=owner.resume_frame;
    owner.ring_origin=owner.fill_frame;owner.ring_played=owner.ring_consumed=owner.ring_fill_stream=0;
    owner.ring_end=owner.repeat_left==15u?NONE:(owner.end_fad-owner.first_fad)*588u-owner.ring_origin;
    /* RELEASE at a once-only source end has no samples to restart. */
    if(owner.stats.state==KUI_TOY_PILOT_PREFILL && !owner.ring_end)
        owner.stats.state=KUI_TOY_PILOT_EOF;
    if(owner.stats.state!=KUI_TOY_PILOT_PREFILL) {
        if(owner.stop_action==KUI_TOY_PILOT_RESET) reset_selection(owner.model.generation);
        publish();owner.stats.applied_generation=owner.model.generation;
    }
}
static void ring_resume(void) {
    uint32_t total=(owner.end_fad-owner.first_fad)*588u;
    if(!total) { owner.resume_frame=0;return; }
    if(owner.repeat_left!=15u && owner.ring_consumed>=owner.ring_end) owner.resume_frame=total;
    else owner.resume_frame=(owner.ring_origin+owner.ring_consumed%total)%total;
    owner.resume_frame&=~1u;
    owner.stats.position_fad=owner.first_fad+owner.resume_frame/588u;
}
static void ring_recover(uint32_t action,uint32_t reason) {
    if(owner.stop_wait || owner.mailbox.generation!=owner.model.generation) return;
    uint32_t cursor_age=owner.ring_running?ticks()-owner.ring_observed_tick:0u;
    uint32_t command=word(owner.config.data_pending);
    ring_resume();
    if(stop_begin(action)) {
        data_blocked_close();
        if(action==RING_RESTART) {
            ++owner.stats.recovery_counts[reason-1u];
            owner.stats.recovery_last_reason=reason;
            owner.stats.recovery_last_gd_command=command;
            owner.stats.recovery_last_cursor_age=cursor_age;
        }
        owner.stats.state=KUI_TOY_PILOT_PREFILL;
        owner.gap_pending=1;owner.last_end_tick=ticks();
    } else if(!owner.bus_deferred && !owner.disabled) fault(KUI_TOY_PILOT_FAULT_QUEUE);
}
/* Cooperative publication probe. A completed proof can be used until the
 * protected target's conservative future-entry deadline; slow ARM publication never forces a
 * long SH busywait or a new handshake before every sample burst. */
static bool ring_observe(void) {
    if(!template_matches(0u,KUI_TOY_RING_FRAMES,true)) {
        if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_PORT);
        return false;
    }
    uint32_t began=ticks(),generation=owner.model.generation;
    for(unsigned attempt=0;attempt<KUI_TOY_RING_PROBE_READS;attempt++) {
        if(ticks()-began>=KUI_TOY_RING_PROBE_TICKS) break;
        uint32_t baseline=ticks();
        if(!observe_ports()) return false;
        uint32_t now=ticks(),left=owner.stats.cursor_left,right=owner.stats.cursor_right;
        if(owner.disabled || owner.mailbox.generation!=generation) return false;
        if(left>=KUI_TOY_RING_FRAMES || right>=KUI_TOY_RING_FRAMES ||
           owner.stats.active_left!=255u || owner.stats.active_right!=255u) {
            ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_PORTS);return false;
        }
        if(!owner.ring_probe_seen || now-owner.ring_probe.began>KUI_TOY_RING_FRESH_TICKS) {
            /* Baseline precedes BOTH reads: ARM may publish left while the
             * right-side bus observation is still in progress. */
            kui_toy_ring_probe_begin(&owner.ring_probe,left,right,baseline);
            owner.ring_probe_seen=1;continue;
        }
        if(!kui_toy_ring_probe_observe(&owner.ring_probe,left,right,baseline,now)) {
            uint32_t distance=left>right?left-right:right-left;
            /* ARM publishes the two cursor words independently. Their
             * capture times may differ by this bounded probe's whole age;
             * only separation beyond that advance triggers phase recovery.
             * An ambiguous pair grants NO ownership: acceptance still needs
             * <=64 frames in the same half, and the old proof still expires
             * at the protected target's conservative future-entry deadline. */
            uint32_t limit=KUI_TOY_RING_PHASE_FRAMES+
                kui_toy_ring_frames(now-owner.ring_probe.began);
            if(owner.ring_probe.changes[0]==2u && owner.ring_probe.changes[1]==2u &&
               distance>limit && distance<KUI_TOY_RING_FRAMES-limit) {
                ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_PHASE);return false;
            }
            continue;
        }
        uint32_t cursor=left>right?left:right,age=kui_toy_ring_probe_age(&owner.ring_probe,now);
        uint32_t delta=cursor;
        if(owner.ring_running) {
            uint32_t elapsed=now-owner.ring_observed_tick;
            delta=(cursor-owner.ring_cursor)&(KUI_TOY_RING_FRAMES-1u);
            if(elapsed>=kui_toy_ring_ticks(KUI_TOY_RING_HALF) || delta>=KUI_TOY_RING_HALF) {
                ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_UNOBSERVED_HALF);return false;
            }
            uint32_t allowance=age+owner.ring_sample_age;
            uint32_t upper=kui_toy_ring_frames(elapsed+allowance)+KUI_TOY_RING_PHASE_FRAMES;
            uint32_t lower=elapsed>allowance?kui_toy_ring_frames(elapsed-allowance):0u;
            if(delta>upper || delta+KUI_TOY_RING_PHASE_FRAMES<lower ||
               owner.ring_played>UINT32_MAX-delta) {
                ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_PROGRESS);return false;
            }
        } else {
            if(now-owner.queue_tick>=kui_toy_ring_ticks(KUI_TOY_RING_HALF)-KUI_TOY_RING_RESERVE_TICKS) {
                ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_START_PROOF);return false;
            }
            uint32_t upper=kui_toy_ring_frames(now-owner.queue_tick+KUI_TOY_RING_RESERVE_TICKS)+
                KUI_TOY_RING_PHASE_FRAMES;
            if(cursor>upper) { ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_START_PROOF);return false; }
            if(!kui_toy_pilot_model_start_applied(&owner.model,0u,owner.model.generation,true,true)) {
                fault(KUI_TOY_PILOT_FAULT_GENERATION);return false;
            }
            owner.ring_running=owner.play_seen=1;++owner.stats.started_observed;
            for(uint32_t i=0;i<KUI_TOY_PILOT_BLOCKS;i++) {
                owner.ring_window_tick[i]=now;owner.ring_window_calls[i]=owner.stats.service_calls;
            }
            owner.stats.state=KUI_TOY_PILOT_PLAYING;
            publish();owner.stats.applied_generation=generation;
        }
        uint32_t old=owner.ring_played,bank=cursor/KUI_TOY_RING_BLOCK;
        uint32_t candidate=old+delta;
        bool missing=false;
        /* The faster channel may cross several blocks in one accepted proof.
         * Check every intervening absolute epoch, not just its final slot.
         * A matching physical ID from an old ring turn is never readiness. */
        uint32_t first_crossed=old/KUI_TOY_RING_BLOCK+1u;
        uint32_t last_crossed=candidate/KUI_TOY_RING_BLOCK;
        for(uint32_t absolute=first_crossed;absolute<=last_crossed;absolute++) {
            uint32_t slot=absolute%KUI_TOY_PILOT_BLOCKS;
            struct kui_toy_pilot_bank *b=&owner.model.banks[slot];
            uint32_t expected=absolute*KUI_TOY_RING_BLOCK;
            if(b->state!=KUI_TOY_PILOT_BANK_READY || b->generation!=generation ||
               b->first_frame!=expected || b->frames!=KUI_TOY_RING_BLOCK ||
               b->filled!=KUI_TOY_RING_BLOCK) {
                /* Cursor motion through stale/unfilled PCM is not source
                 * progress. Resume at the first missing block's boundary. */
                missing=true;candidate=expected;break;
            }
        }
        owner.ring_played=candidate;owner.ring_cursor=cursor;
        owner.ring_tick=now-age;owner.ring_observed_tick=now;owner.ring_sample_age=age;
        owner.ring_probe_seen=0;
        uint32_t slower=old+delta-(left>right?left-right:right-left);
        if(slower>candidate) slower=candidate;
        if(slower<owner.ring_consumed) slower=owner.ring_consumed;
        uint32_t credited=slower-owner.ring_consumed;
        if(owner.ring_end!=NONE) {
            if(owner.ring_consumed>=owner.ring_end) credited=0;
            else if(credited>owner.ring_end-owner.ring_consumed) credited=owner.ring_end-owner.ring_consumed;
        }
        owner.ring_consumed+=credited;
        owner.stats.retired_frames+=credited;ring_resume();
        if(owner.ring_end!=NONE && owner.ring_consumed>=owner.ring_end) {
            ++owner.stats.finite_ends;ring_recover(RING_EOF,0u);return false;
        }
        if(missing) { ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_MISSING_HALF);return false; }
        for(uint32_t absolute=first_crossed;absolute<=last_crossed;absolute++)
            owner.model.banks[absolute%KUI_TOY_PILOT_BLOCKS].state=KUI_TOY_PILOT_BANK_PLAYING;
        owner.model.active_bank=bank;
        /* The lower independently captured cursor retires memory. A pair
         * straddling a4096-frame boundary leaves the old block PLAYING even
         * though active_bank names the faster channel's new block. Its old
         * generation/first_frame survive until BOTH captures pass its end. */
        for(uint32_t slot=0;slot<KUI_TOY_PILOT_BLOCKS;slot++) {
            struct kui_toy_pilot_bank *b=&owner.model.banks[slot];
            if(b->state==KUI_TOY_PILOT_BANK_PLAYING &&
               slower>=b->first_frame+b->frames) {
                *b=(struct kui_toy_pilot_bank){0};++owner.stats.bank_ends;
                owner.ring_window_tick[slot]=now;
                owner.ring_window_calls[slot]=owner.stats.service_calls;
            }
        }
        return true;
    }
    return false;
}
static bool ring_write_allowed(uint32_t bank,uint32_t site) {
    uint32_t sr=mask_begin();
    if(!owner.ring_queued) { mask_end(sr);return true; }
    if(owner.disabled || !owner.sdk_ready || owner.mailbox.generation!=owner.model.generation ||
       !owner.ring_running || owner.stop_wait || bank>=KUI_TOY_PILOT_BLOCKS ||
       bank==owner.model.active_bank || bank==owner.model.pending_bank ||
       owner.model.banks[bank].state==KUI_TOY_PILOT_BANK_PLAYING ||
       owner.model.banks[bank].state==KUI_TOY_PILOT_BANK_START_WAIT) {
        mask_end(sr);return false;
    }
    /* Source order selects the only next fill slot. Its first_frame is an
     * absolute future ring epoch. The fastest accepted capture and its age
     * bound protect the exact next use of THIS target, rather than expiring
     * every write at the current block or half boundary. */
    uint32_t first=owner.model.banks[bank].state==KUI_TOY_PILOT_BANK_FILLING?
        owner.model.banks[bank].first_frame:owner.ring_fill_stream;
    if(bank!=(owner.ring_fill_stream/KUI_TOY_RING_BLOCK)%KUI_TOY_PILOT_BLOCKS ||
       first%KUI_TOY_RING_BLOCK || first<owner.ring_played ||
       first-owner.ring_played>KUI_TOY_RING_FRAMES) {
        mask_end(sr);return false;
    }
    uint32_t remaining=first-owner.ring_played;
    uint32_t now=ticks(),age=now-owner.ring_tick;
    if(age>=kui_toy_ring_ticks(remaining) ||
       kui_toy_ring_ticks(remaining)-age<=KUI_TOY_RING_RESERVE_TICKS) {
        /* Capture one coherent set of the actual denied gate's inputs.
         * This short SR-preserving scope grants no new write permission;
         * STOP publication below retains its own generation/bus checks. */
        owner.stats.reserve_last_cursor=owner.ring_cursor;
        owner.stats.reserve_last_proof_age=age;
        owner.stats.reserve_last_sample_age=owner.ring_sample_age;
        owner.stats.reserve_last_remaining=remaining;
        owner.stats.reserve_last_bank=bank;
        owner.stats.reserve_last_bank_filled=owner.model.banks[bank].filled;
        owner.stats.reserve_last_fill_stream=owner.ring_fill_stream;
        owner.stats.reserve_last_site=site;
        owner.stats.reserve_last_bank_state=owner.model.banks[bank].state;
        owner.stats.reserve_last_probe_age=owner.ring_probe_seen?now-owner.ring_probe.began:0u;
        owner.stats.reserve_last_window_ticks=now-owner.ring_window_tick[bank];
        owner.stats.reserve_last_window_calls=owner.stats.service_calls-owner.ring_window_calls[bank]+1u;
        mask_end(sr);
        ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_COPY_RESERVE);return false;
    }
    mask_end(sr);
    return true;
}
static void playback_step(uint32_t now) {
    if(!owner.ring_queued || owner.stop_wait) return;
    if(!owner.play_ack_seen) {
        if(!owner.queue_wait || !consumed(owner.queue_slot,&owner.queue_ack)) {
            if(now-owner.queue_tick>APPLY_LIMIT_TICKS) fault(KUI_TOY_PILOT_FAULT_QUEUE);
            ++owner.stats.start_waits;return;
        }
        owner.queue_wait=0;owner.play_ack_seen=1;
        if(!template_matches(0u,KUI_TOY_RING_FRAMES,true)) {
            if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_PORT);
            return;
        }
    }
    if(owner.ring_running && now-owner.ring_observed_tick>=kui_toy_ring_ticks(KUI_TOY_RING_HALF)) {
        ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_UNOBSERVED_HALF);return;
    }
    (void)ring_observe();
    if(owner.ring_running && !owner.stop_wait) {
        uint32_t bank=(owner.ring_fill_stream/KUI_TOY_RING_BLOCK)%KUI_TOY_PILOT_BLOCKS;
        if(owner.model.banks[bank].state==KUI_TOY_PILOT_BANK_EMPTY ||
           owner.model.banks[bank].state==KUI_TOY_PILOT_BANK_FILLING)
            (void)ring_write_allowed(bank,KUI_TOY_PILOT_RESERVE_PLAYBACK);
    }
    if(!owner.ring_running && !owner.stop_wait &&
       ticks()-owner.queue_tick>=kui_toy_ring_ticks(KUI_TOY_RING_HALF)-KUI_TOY_RING_RESERVE_TICKS)
        ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_START_PROOF);
}
static bool copy_plane(uint32_t address,const void *source,uint32_t bytes,uint32_t generation,uint32_t bank) {
    uint32_t sr=mask_begin();
    if(owner.disabled || owner.mailbox.generation!=generation || owner.model.generation!=generation ||
       owner.stats.sound_generation!=owner.stats.driver_generation ||
       bank>=KUI_TOY_PILOT_BLOCKS || owner.model.banks[bank].generation!=generation ||
       owner.model.banks[bank].state!=KUI_TOY_PILOT_BANK_FILLING ||
       owner.model.banks[bank].frames!=KUI_TOY_RING_BLOCK ||
       owner.model.banks[bank].filled>owner.model.banks[bank].frames ||
       bank==owner.model.active_bank || bank==owner.model.pending_bank || owner.stop_wait ||
       !ring_write_allowed(bank,KUI_TOY_PILOT_RESERVE_COPY_PLANE)) { mask_end(sr);return false; }
    uint32_t started=ticks();
    if(!source || ((uintptr_t)source&3u) || !bytes || bytes>256u || (bytes&3u) ||
       owner.stats.sound_bytes!=KUI_TOY_PILOT_SOUND_BYTES || address<owner.stats.sound_address ||
       address>owner.stats.sound_address+owner.stats.sound_bytes-bytes || (address&3u)) { mask_end(sr);return false; }
    uint32_t offset=(address-owner.stats.sound_address)%KUI_TOY_PILOT_RING_MONO_BYTES;
    uint32_t block_begin=bank*KUI_TOY_PILOT_MONO_BYTES;
    if(offset<block_begin || offset>block_begin+KUI_TOY_PILOT_MONO_BYTES-bytes) {
        mask_end(sr);return false;
    }
    enum kui_toy_pilot_bus_result result=kui_toy_pilot_bus_copy(address,source,bytes);
    uint32_t elapsed=ticks()-started;mask_end(sr);
    maximum(&owner.stats.copy_ticks_max,elapsed);++owner.stats.copy_calls;
    return bus_result(result);
}
static void fill_step(void) {
    uint32_t data=word(owner.config.data_pending);
#if KUI_TOY_PILOT_SHARED_SCI
    data_blocked_sample(0u);
#else
    data_blocked_sample(data);
#endif
    if(word(owner.config.resident_active) ||
#if !KUI_TOY_PILOT_SHARED_SCI
       data==16u || data==17u ||
#endif
       owner.stop_wait ||
       (owner.ring_queued && !owner.ring_running)) return;
    uint32_t bank=(owner.ring_fill_stream/KUI_TOY_RING_BLOCK)%KUI_TOY_PILOT_BLOCKS;
    uint32_t total=(owner.end_fad-owner.first_fad)*588u;
    if(!total) return;
    if(owner.model.banks[bank].state!=KUI_TOY_PILOT_BANK_FILLING) {
        if(owner.ring_fill_stream>UINT32_MAX-KUI_TOY_RING_BLOCK) {
            ring_recover(RING_RESTART,KUI_TOY_PILOT_RECOVERY_STREAM_OVERFLOW);return;
        }
        if(owner.model.banks[bank].state!=KUI_TOY_PILOT_BANK_EMPTY ||
           bank==owner.model.active_bank || bank==owner.model.pending_bank) return;
        if(!ring_write_allowed(bank,KUI_TOY_PILOT_RESERVE_FILL_BEGIN)) return;
        if(!kui_toy_pilot_model_fill_begin(&owner.model,bank,owner.model.generation,
            owner.ring_fill_stream,KUI_TOY_RING_BLOCK)) {
            fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
        }
    }
    if(!ring_write_allowed(bank,KUI_TOY_PILOT_RESERVE_PRE_CARD)) return;
    uint32_t generation=owner.model.generation,sector=owner.fill_frame/588u,skip=owner.fill_frame%588u;
    uint32_t left=owner.model.banks[bank].frames-owner.model.banks[bank].filled;
    if(!left) { fault(KUI_TOY_PILOT_FAULT_RANGE);return; }
    bool silence=owner.fill_frame==total;
    if(owner.fill_frame>total) { fault(KUI_TOY_PILOT_FAULT_RANGE);return; }
    uint32_t frames=silence?588u:588u-skip;if(frames>left) frames=left;
    if(!silence) {
        uint32_t lba=owner.first_fad-150u+sector;
        uint32_t sr=mask_begin();
        if(owner.disabled || owner.mailbox.generation!=generation) {
            mask_end(sr);++owner.stats.stale_actions;return;
        }
        data=word(owner.config.data_pending);
        if(word(owner.config.resident_active)
#if !KUI_TOY_PILOT_SHARED_SCI
           || data==16u || data==17u
#endif
           ) {
            data_blocked_sample(data);mask_end(sr);return;
        }
        /* Sector remnants survive a block boundary or deferred plane copy. */
        if(!owner.raw_generation || owner.raw_generation!=generation || owner.raw_lba!=lba) {
#if KUI_TOY_PILOT_ASYNC_CDDA
            if(owner.raw_request_generation!=generation || owner.raw_request_lba!=lba) {
                owner.raw_generation=0u;
                owner.raw_request_generation=generation;owner.raw_request_lba=lba;
                owner.raw_request_tick=ticks();
            }
            /* One bounded transport visit can arm or consume private DMA
             * staging. Pending restores the caller's SR and returns to the
             * game without decoding or publishing any partial sector. */
            int result=kui_toy_pilot_sci_audio_read(lba,generation,owner.raw);
            if(result==KUI_TOY_SCI_PENDING) {
                data_blocked_sample(data);mask_end(sr);return;
            }
            if(owner.disabled || !owner.sdk_ready || owner.mailbox.generation!=generation ||
               owner.model.generation!=generation) {
                owner.raw_request_generation=0u;
                mask_end(sr);++owner.stats.stale_actions;return;
            }
            if(result!=KUI_TOY_SCI_OK) {
                owner.raw_request_generation=0u;
                mask_end(sr);++owner.stats.raw_errors;fault(KUI_TOY_PILOT_FAULT_CARD);return;
            }
            /* Async latency includes game execution between the first
             * request and complete verified delivery. It does not measure
             * time with CPU interrupts blocked or time spent inside DMA. */
            uint32_t elapsed=ticks()-owner.raw_request_tick;
            owner.raw_request_generation=0u;++owner.stats.raw_calls;
            owner.stats.raw_read_ticks_last=elapsed;
            maximum(&owner.stats.raw_read_ticks_max,elapsed);
            owner.stats.raw_read_ticks_total+=elapsed;
            ++owner.stats.raw_read_timing_calls;
#else
            typedef int (*raw_fn)(uint32_t,uint32_t,void *);
#if KUI_TOY_PILOT_SHARED_SCI
            int lease=kui_toy_pilot_sci_audio_acquire();
            if(lease>0) { data_blocked_sample(data);mask_end(sr);return; }
            if(lease<0) { mask_end(sr);++owner.stats.raw_errors;fault(KUI_TOY_PILOT_FAULT_CARD);return; }
#endif
            owner.raw_generation=0u;++owner.stats.raw_calls;
            uint32_t read_began=ticks();
            int result=((raw_fn)(uintptr_t)owner.config.read_raw)(lba,1u,owner.raw);
#if KUI_TOY_PILOT_SHARED_SCI
            kui_toy_pilot_sci_audio_release();
#endif
            /* The existing callback scope already preserves exact SR.
             * Do not perform new timing reads for a revoked/stale return. */
            if(!owner.disabled && owner.sdk_ready && owner.mailbox.generation==generation &&
               owner.model.generation==generation) {
                uint32_t elapsed=ticks()-read_began;
                owner.stats.raw_read_ticks_last=elapsed;
                maximum(&owner.stats.raw_read_ticks_max,elapsed);
                owner.stats.raw_read_ticks_total+=elapsed;
                ++owner.stats.raw_read_timing_calls;
            }
            if(result) { mask_end(sr);++owner.stats.raw_errors;fault(KUI_TOY_PILOT_FAULT_CARD);return; }
#endif
            owner.stats.raw_bytes+=2352u;
            if(owner.disabled || owner.mailbox.generation!=generation) {
                mask_end(sr);++owner.stats.stale_actions;return;
            }
            owner.raw_lba=lba;owner.raw_generation=generation;
        }
        mask_end(sr);
    }
    if(owner.disabled || owner.mailbox.generation!=generation) { ++owner.stats.stale_actions;return; }
    /* Card latency may have used the target's future-use window. Refresh when
     * possible, otherwise use only the still-unexpired conservative proof. */
    if(owner.ring_running) (void)ring_observe();
    if(owner.stop_wait || owner.bus_deferred ||
       !ring_write_allowed(bank,KUI_TOY_PILOT_RESERVE_POST_CARD)) return;
    for(uint32_t done=0;done<frames;) {
        if(owner.disabled || owner.mailbox.generation!=generation) { ++owner.stats.stale_actions;return; }
        uint32_t take=frames-done;if(take>128u) take=128u;
        if(take<2u || (take&1u)) { fault(KUI_TOY_PILOT_FAULT_RANGE);return; }
        for(uint32_t i=0;i<take;i++) {
            if(silence) owner.left[i]=owner.right[i]=0;
            else {
                const uint8_t *p=owner.raw+(skip+done+i)*4u;
                owner.left[i]=(int16_t)((uint16_t)p[0]|(uint16_t)p[1]<<8);
                owner.right[i]=(int16_t)((uint16_t)p[2]|(uint16_t)p[3]<<8);
            }
        }
        uint32_t offset=(owner.model.banks[bank].filled+done)*2u;
        if(!copy_plane(plane_address(bank,0)+offset,owner.left,take*2u,generation,bank) ||
           !copy_plane(plane_address(bank,1)+offset,owner.right,take*2u,generation,bank)) {
            if(!owner.disabled && owner.mailbox.generation==generation && !owner.stop_wait && !owner.bus_deferred)
                fault(KUI_TOY_PILOT_FAULT_PORT);
            return;
        }
        done+=take;
    }
    if(!kui_toy_pilot_model_fill_commit(&owner.model,bank,generation,frames)) {
        fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
    }
    owner.ring_fill_stream+=frames;
    if(!silence) {
        owner.fill_frame+=frames;owner.stats.filled_frames+=frames;
        if(owner.fill_frame==total && owner.repeat_left==15u) owner.fill_frame=0;
    }
    if(owner.model.banks[bank].state==KUI_TOY_PILOT_BANK_READY) ++owner.stats.bank_fills;
}
/* One quantum consumes at most the remaining frames of ONE raw sector.
 * A4096-frame boundary can split that sector into two ordered blocks. Do
 * not charge its cached tail as another sector admission; repeat every
 * existing block/plane/proof/control gate for the second fragment. This
 * admits no additional callback and bounds work to588 source frames and
 * at most two fragments, even with a stopped service timer. */
static void fill_quantum(void) {
    uint32_t before=owner.ring_fill_stream,source=owner.fill_frame;
    fill_step();
    if(owner.disabled || owner.bus_deferred || owner.stop_wait ||
       owner.mailbox.generation!=owner.model.generation ||
       owner.ring_fill_stream==before || owner.fill_frame==source ||
       owner.ring_fill_stream%KUI_TOY_RING_BLOCK || !(owner.fill_frame%588u))
        return;
    uint32_t total=(owner.end_fad-owner.first_fad)*588u;
    if(owner.fill_frame<total && owner.raw_generation==owner.model.generation &&
       owner.raw_lba==owner.first_fad-150u+owner.fill_frame/588u) fill_step();
}
static bool ring_primed(void) {
    for(uint32_t i=0;i<KUI_TOY_PILOT_BLOCKS;i++)
        if(owner.model.banks[i].state!=KUI_TOY_PILOT_BANK_READY ||
           owner.model.banks[i].generation!=owner.model.generation ||
           owner.model.banks[i].first_frame!=i*KUI_TOY_RING_BLOCK ||
           owner.model.banks[i].filled!=KUI_TOY_RING_BLOCK) return false;
    return true;
}
static bool ring_empty(void) {
    for(uint32_t i=0;i<KUI_TOY_PILOT_BLOCKS;i++)
        if(owner.model.banks[i].state!=KUI_TOY_PILOT_BANK_EMPTY) return false;
    return true;
}
static void start_step(uint32_t now) {
    if(owner.ring_queued || owner.model.active_bank!=NONE || owner.model.pending_bank!=NONE || owner.stop_wait) return;
    if(owner.transaction_bank==NONE) {
        if(!ring_primed()) return;
        owner.transaction_bank=0u;owner.transaction_step=0;
    }
    if(owner.queue_wait) {
        if(!consumed(owner.queue_slot,&owner.queue_ack)) {
            if(now-owner.queue_tick>APPLY_LIMIT_TICKS) fault(KUI_TOY_PILOT_FAULT_QUEUE);
            ++owner.stats.start_waits;return;
        }
        owner.queue_wait=0;
    }
    uint32_t bank=owner.transaction_bank;
    /* Sequential ARM dispatch makes consumption of the final configuration
     * packet an acknowledgement for all eight earlier setup operations.
     * Bounded publication can stop at any partial queue; no START is issued
     * until final consumption is observed and templates validate. A single
     * immediate check may avoid a forced extra visit; it never polls. */
    uint32_t began=ticks();
    while(owner.transaction_step<8u) {
        uint32_t elapsed=ticks()-began;
        if(elapsed>CLOCK_GAP_LIMIT) { fault(KUI_TOY_PILOT_FAULT_CLOCK);return; }
        if(elapsed>=SETUP_BURST_TICKS) return;
        if(!queue_operation(bank,owner.transaction_step)) {
            if(!owner.disabled && owner.mailbox.generation==owner.model.generation) {
                if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_QUEUE);
            } else ++owner.stats.stale_actions;
            return;
        }
        ++owner.transaction_step;
    }
    if(owner.queue_wait) {
        uint32_t elapsed=ticks()-began;
        if(elapsed>CLOCK_GAP_LIMIT) { fault(KUI_TOY_PILOT_FAULT_CLOCK);return; }
        if(elapsed>=SETUP_BURST_TICKS || !consumed(owner.queue_slot,&owner.queue_ack)) return;
        owner.queue_wait=0;
    }
    if(!template_matches(0u,KUI_TOY_RING_FRAMES,false)) {
        if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_PORT);
        return;
    }
    if(!queue_operation(bank,owner.transaction_step)) {
        if(!owner.disabled && owner.mailbox.generation==owner.model.generation) {
            if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_QUEUE);
        } else ++owner.stats.stale_actions;
        return;
    }
    if(owner.transaction_step++==8u) {
        if(!kui_toy_pilot_model_start_queued(&owner.model,bank,owner.model.generation)) {
            fault(KUI_TOY_PILOT_FAULT_GENERATION);return;
        }
        owner.stats.state=KUI_TOY_PILOT_START_WAIT;++owner.stats.bank_starts;
        if(owner.gap_pending) {
            ++owner.stats.handoff_gaps;maximum(&owner.stats.gap_ticks_max,ticks()-owner.last_end_tick);
            owner.gap_pending=0;
        }
        owner.play_ack_seen=owner.play_seen=0;
        owner.ring_queued=1;owner.ring_running=owner.ring_probe_seen=0;
        ++owner.stats.hardware_loops;owner.stop_action=0u;
        uint32_t sr=mask_begin();
        if(owner.mailbox.generation==owner.model.generation) owner.pause_fence=0u;
        mask_end(sr);
    }
}
/* No hardware packet can still be owned in these states. Intentional idle
 * time does not count as a broken service clock, and stop/reset need no bus. */
static bool quiescent(void) {
    return (owner.stats.state==KUI_TOY_PILOT_STOPPED || owner.stats.state==KUI_TOY_PILOT_PAUSED ||
            owner.stats.state==KUI_TOY_PILOT_EOF) && !owner.stop_wait && !owner.queue_wait &&
        owner.transaction_bank==NONE && !owner.ring_queued && owner.model.active_bank==NONE && owner.model.pending_bank==NONE &&
        ring_empty();
}
static bool service_idle(void) {
    uint32_t sr=mask_begin();bool idle=quiescent();
    if(idle && owner.mailbox.generation!=owner.handled_generation) {
        uint32_t command=owner.mailbox.command;
        if(command==CMD_STOP || command==KUI_TOY_PILOT_RESET || command==CMD_PAUSE ||
           (command==CMD_RELEASE && !owner.mailbox.play_generation)) {
            if(!kui_toy_pilot_model_reset(&owner.model)) fault(KUI_TOY_PILOT_FAULT_GENERATION);
            else {
                owner.model.generation=owner.handled_generation=owner.mailbox.generation;
                if(command==KUI_TOY_PILOT_RESET) reset_selection(owner.model.generation);
                else (void)select_play(&owner.mailbox);
                (void)kui_toy_pilot_model_stop_applied(&owner.model,owner.model.generation,true,true,true);
                owner.stats.state=command==CMD_PAUSE?KUI_TOY_PILOT_PAUSED:KUI_TOY_PILOT_STOPPED;
                publish();owner.stats.applied_generation=owner.model.generation;
            }
        } else idle=false;
    }
    if(idle) owner.clock_seen=owner.bus_defer_seen=0;
    mask_end(sr);return idle;
}
void kui_toy_pilot_worker_step(void) {
    if(!owner.configured) return;
    if(owner.disabled) goto done;
    uint32_t sr=status_register();
    if(sr&0x10000000u) { ++owner.stats.service_skips;goto done; }
    ++owner.stats.service_calls;
    if(service_idle()) goto done;
    if(!owner.sdk_ready || word(0x8c0a7318u)!=1u) goto done;
    owner.bus_deferred=0;
    uint32_t now;if(!clock_sample(&now)) { fault(KUI_TOY_PILOT_FAULT_CLOCK);goto done; }
    if(!live_driver()) { if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_DRIVER);goto done; }
    if(!owner.stats.sound_address && !sound_allocate()) {
        if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_HEAP);
        goto done;
    }
    if(!observe_ports()) { if(!owner.bus_deferred) fault(KUI_TOY_PILOT_FAULT_PORT);
        goto done; }
    if(!sound_word(SOUND_BASE+0x399cu,&owner.stats.queue_consumer)) goto done;
    apply_action(now);if(owner.disabled) goto done;
    if(owner.bus_deferred) goto done;
    stop_step(now);if(owner.stop_wait || owner.disabled) {
        if(!owner.bus_deferred) owner.bus_defer_seen=0;
        stack_sample();goto done;
    }
    playback_step(now);if(owner.disabled) goto done;
    /* Add bounded capture opportunities at both work boundaries. The same
     * proof and expiry rules apply; the admission clock remains the original
     * now above, so entry observations consume the16ms visit budget too. */
    if(owner.ring_running && !owner.stop_wait && !owner.bus_deferred &&
       owner.mailbox.generation==owner.model.generation) (void)ring_observe();
    if(owner.bus_deferred) goto done;
    if(owner.stats.state==KUI_TOY_PILOT_PREFILL || owner.stats.state==KUI_TOY_PILOT_PLAYING ||
       owner.stats.state==KUI_TOY_PILOT_START_WAIT) {
        /* Start a completely primed stereo ring before further card work. */
        bool start_attempted=owner.model.active_bank==NONE && owner.model.pending_bank==NONE && ring_primed();
        if(start_attempted) {
            start_step(now);if(owner.disabled || owner.bus_deferred) goto done;
        }
        /* Fill ordered safe blocks without assuming a
         * 60Hz updater. Each quantum retains its own one-sector card claim
         * and restores SR before another begins. Check the visit budget
         * before EXTRA work: a slow first quantum is still allowed, but
         * cannot cause an unbounded burst of slow reads. The quantum cap
         * also bounds a stopped timer. Existing card/G2 deadlines bound each operation;
         * the 16ms admission budget can overrun by one complete quantum. */
        for(unsigned quantum=0;quantum<REFILL_QUANTA;quantum++) {
            if(quantum && ticks()-now>=REFILL_VISIT_TICKS) break;
            uint32_t before=owner.ring_fill_stream;
            fill_quantum();if(owner.disabled) goto done;
            if(owner.bus_deferred || owner.mailbox.generation!=owner.model.generation ||
               owner.ring_fill_stream==before || owner.stop_wait) break;
        }
        if(owner.bus_deferred) goto done;
        if(owner.ring_running && !owner.stop_wait && !owner.bus_deferred &&
           owner.mailbox.generation==owner.model.generation) (void)ring_observe();
        if(!start_attempted) start_step(now);
    }
    if(!owner.bus_deferred) owner.bus_defer_seen=0;
    maximum(&owner.stats.step_ticks_max,ticks()-now);stack_sample();
done:
    terminal_dispatch();
}
#ifdef KUI_TOY_PILOT_WORKER_TEST
/* Fixture drives the actual service function, with mapped registers and
 * bus/lease adapters supplied by the host regression, never linked to SH. */
void kui_toy_pilot_worker_test_prepare(uint32_t state,uint32_t command,bool pending) {
    memset(&owner,0,sizeof(owner));kui_toy_pilot_model_init(&owner.model);
    kui_toy_pilot_pause_entries=kui_toy_pilot_pause_retries=0u;
    kui_toy_pilot_pause_pumps=kui_toy_pilot_pause_retired=0u;
    kui_toy_pilot_pause_detail=kui_toy_pilot_pause_max_attempts=0u;
    owner.configured=owner.sdk_ready=owner.stats.driver_verified=1;
    owner.stats.magic=KUI_TOY_PILOT_MAGIC;owner.stats.version=KUI_TOY_PILOT_API;
    owner.stats.bytes=sizeof(owner.stats);owner.stats.service_visits_per_update=2u;
    owner.stats.driver_generation=owner.stats.sound_generation=1;
    owner.stats.state=state;owner.stats.generation=owner.handled_generation=1;
    owner.transaction_bank=NONE;owner.mailbox.generation=pending?2u:1u;
    owner.stats.generation=owner.mailbox.generation;owner.mailbox.command=command;
    owner.mailbox.first_fad=200;owner.mailbox.end_fad=210;owner.mailbox.track=2;
    owner.mailbox.play_generation=pending && command==CMD_PLAY?2u:0u;
}
void kui_toy_pilot_worker_test_manifest(const struct kui_retail_manifest *m) {
    owner.config.manifest=(uint32_t)(uintptr_t)m;
}
#endif
