/* SPDX-License-Identifier: GPL-3.0-only */
/* Focused regression for the authored pause adapter. */
#define KUI_TOY_PILOT_PAUSE_TEST 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/loader/toy_pilot_pause.c"

volatile uint32_t kui_toy_pilot_pause_entries, kui_toy_pilot_pause_retries;
volatile uint32_t kui_toy_pilot_pause_pumps, kui_toy_pilot_pause_retired;
volatile uint32_t kui_toy_pilot_pause_detail, kui_toy_pilot_pause_max_attempts;
static uint32_t mem[0x4000 / 4];
static uint32_t context = 0x8c100000u, sr, tick, game_sp;
static unsigned worker_visits, exec_visits, check_visits, fault_visits;
static bool ready, let_worker_finish, corrupt_after, corrupt_gd;
static uint32_t fault_sr;
static uint32_t captured_owner, captured_token, captured_flags, capture_sr;
static unsigned capture_visits, diagnostic_token_reads;
static struct kui_toy_pilot_snapshot snapshot;
static unsigned apply_after, supersede_after;
static uint32_t visit_ticks;
static bool model_ring_pause;
static uint32_t pause_ack_tick;
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) { return &snapshot; }
static uint32_t *at(uint32_t address) {
    assert(address >= context && address < context + sizeof(mem));
    assert(!(address & 3u)); return &mem[(address-context)/4u];
}
uint32_t kui_toy_pilot_pause_test_read(uint32_t a, unsigned bytes) {
    assert(bytes == 4u);
    if(a==context+WORK_BEGIN+60u) ++diagnostic_token_reads;
    if (a == TIMER_TCNT) return ~tick;
    if (a == NATIVE_CONTEXT) return context;
    if (a == NATIVE_WRAPPERS) return NATIVE_WRAPPER_TABLE;
    if (a == NATIVE_WRAPPER_TABLE+100u) return 0x8c0b23a8u;
    if (a == NATIVE_GD_TABLE+16u) return 0x8c0bd366u;
    if (a == NATIVE_GD_TABLE+36u) return 0x8c0bd494u;
    if (a == NATIVE_GD_TABLE+48u) return 0x8c0bd4e6u;
    if (a == NATIVE_GD_TABLE+64u) return corrupt_gd ? 0x8c010001u : 0x8c0bd552u;
    if (a == NATIVE_GD_TABLE+72u) return 0x8c0bd566u;
    return *at(a);
}
uint32_t kui_toy_pilot_pause_test_sr(void) { return sr; }
void kui_toy_pilot_pause_test_set_sr(uint32_t value) { sr = value; }
void kui_toy_pilot_worker_step(void) {
    ++worker_visits;
    assert(sr == 0x40000021u);
    if (let_worker_finish) ready = true;
    tick+=visit_ticks;
    if(model_ring_pause) {
        if(!pause_ack_tick && tick>=195313u) pause_ack_tick=tick; /*250ms*/
        const uint32_t settle=(uint32_t)(((uint64_t)32768u*781250u+44099u)/44100u)+15625u;
        if(pause_ack_tick && tick-pause_ack_tick>=settle)
            snapshot.applied_generation=snapshot.generation;
    }
    if(apply_after && worker_visits==apply_after)
        snapshot.applied_generation=snapshot.generation;
    if(supersede_after && worker_visits==supersede_after) snapshot.command=23u;
}
/* Model the admitted SDK's guarded callback contract: EXEC advances transport,
 * CHECK retires an existing work handle, and only that SDK callback changes
 * the native owner. The adapter itself must never fabricate retirement. */
uint32_t kui_toy_pilot_pause_test_game_sp(void) { return game_sp; }
static bool guest_map(uint32_t a,uint32_t bytes) {
    return a>=GAME_RAM_BEGIN && a<=GAME_RAM_END && bytes<=GAME_RAM_END-a &&
        !(bytes && a<KUI_TOY_PILOT_WORKER_END && a+bytes>NATIVE_RAM_END);
}
void kui_toy_pilot_pause_test_pump(uint32_t c,uint32_t borrowed_sp) {
    assert(c == context); assert((sr & 0xf0u) == 0xf0u);
    assert(borrowed_sp==game_sp);
    /* Native retirement frame68 + CHECK veneer frame24 places its 16-byte
     * BIOS CHECK output here. It must map as guest memory, while an equal
     * scratch span inside the authored worker reservation remains denied. */
    assert(guest_map(borrowed_sp-92u,16u));
    assert(!guest_map(KUI_TOY_PILOT_WORKER_BEGIN+0x100u,16u));
    assert(!*at(c+36u) || *at(c+36u)==0x8c04d542u);
    if (*at(c+32u)) return;
    ++exec_visits;
    if (*at(c+28u)) {
        ++check_visits; *at(c+32u) = 1u;
        if (ready) {
            uint32_t work = *at(c+28u);
            *at(c+28u) = 0u;
            *at(work+60u) = 0u; *at(work+72u) = 0u;
        }
        *at(c+32u) = 0u;
    }
    if (corrupt_after) *at(c+20u) += 4u;
}
void kui_toy_pilot_worker_pause_fault(uint32_t detail) {
    assert(capture_visits==1u);
    assert(detail == kui_toy_pilot_pause_detail); ++fault_visits; fault_sr = sr;
}
void kui_toy_pilot_gd_capture(uint32_t owner,uint32_t firstworktoken,uint32_t flags) {
    assert((sr&0xf0u)==0xf0u);
    assert(!kui_toy_pilot_pause_detail);
    ++capture_visits;capture_sr=sr;
    captured_owner=owner;captured_token=firstworktoken;captured_flags=flags;
}
static void reset(void) {
    memset(mem, 0, sizeof(mem));
    sr=0x40000021u; tick=0u; game_sp=0x8c0bedb4u;
    ready=false; let_worker_finish=false; corrupt_after=false; corrupt_gd=false;
    worker_visits=exec_visits=check_visits=fault_visits=0u; fault_sr=0;
    capture_visits=diagnostic_token_reads=0u;
    captured_owner=captured_token=captured_flags=capture_sr=0u;
    snapshot=(struct kui_toy_pilot_snapshot){0};
    apply_after=supersede_after=0u;visit_ticks=0u;
    model_ring_pause=false;pause_ack_tick=0u;
    kui_toy_pilot_pause_entries=kui_toy_pilot_pause_retries=0u;
    kui_toy_pilot_pause_pumps=kui_toy_pilot_pause_retired=0u;
    kui_toy_pilot_pause_detail=kui_toy_pilot_pause_max_attempts=0u;
    attempts=first_tick=last_tick=retry_context=0u;
    *at(context)=8u; *at(context+12u)=NATIVE_GD_TABLE;
    *at(context+36u)=0x8c04d542u; /* The title registers this during startup. */
    *at(context+20u)=context+WORK_BEGIN;
    *at(context+24u)=context+WORK_BEGIN+WORK_BYTES;
    *at(context+28u)=context+WORK_BEGIN;
    *at(context+WORK_BEGIN)=context;
    *at(context+WORK_BEGIN+60u)=7u; *at(context+WORK_BEGIN+72u)=6u;
}
int main(void) {
    assert(RETRY_LIMIT_ATTEMPTS==65536u);
    assert(native_stack(GAME_RAM_BEGIN+NATIVE_STACK_RESERVE));
    assert(!native_stack(GAME_RAM_BEGIN+NATIVE_STACK_RESERVE-4u));
    assert(native_stack(NATIVE_RAM_END));
    assert(!native_stack(NATIVE_RAM_END+4u));
    assert(!native_stack(KUI_TOY_PILOT_WORKER_END+NATIVE_STACK_RESERVE-4u));
    assert(native_stack(KUI_TOY_PILOT_WORKER_END+NATIVE_STACK_RESERVE));
    assert(native_stack(GAME_RAM_END));
    assert(!native_stack(GAME_RAM_END+4u));
    assert(!native_stack(0x8c0bedb5u));
    assert(ram(NATIVE_RAM_END-4u,4u));
    assert(!ram(NATIVE_RAM_END-4u,8u));
    assert(!ram(NATIVE_RAM_END,4u));
    assert(!ram(KUI_TOY_PILOT_WORKER_BEGIN,4u));
    reset();snapshot.command=22u;snapshot.generation=2u;snapshot.applied_generation=1u;
    apply_after=400u;visit_ticks=781u;
    kui_toy_pilot_pause_after(0);
    assert(worker_visits==400u && snapshot.generation==snapshot.applied_generation);
    assert(!exec_visits && !fault_visits && !kui_toy_pilot_pause_retries && sr==0x40000021u);

    reset();snapshot.command=22u;snapshot.generation=2u;snapshot.applied_generation=1u;
    model_ring_pause=true;visit_ticks=1000u;
    kui_toy_pilot_pause_after(0);
    assert(pause_ack_tick>=195313u && tick>781250u && tick<1562500u);
    assert(snapshot.generation==snapshot.applied_generation && !fault_visits);
    assert(!exec_visits && !kui_toy_pilot_pause_retries && sr==0x40000021u);

    reset();snapshot.command=22u;snapshot.generation=2u;
    supersede_after=2u;kui_toy_pilot_pause_after(0);
    assert(worker_visits==2u && !fault_visits && !exec_visits && sr==0x40000021u);

    reset();snapshot.command=22u;snapshot.generation=2u;visit_ticks=10000u;
    kui_toy_pilot_pause_after(0);
    assert(worker_visits==157u && kui_toy_pilot_pause_detail==3u && fault_sr==0x40000021u);

    reset();snapshot.command=22u;snapshot.generation=2u;
    kui_toy_pilot_pause_after(0);
    assert(worker_visits==APPLY_LIMIT_ATTEMPTS && kui_toy_pilot_pause_detail==3u);
    assert(!exec_visits && fault_sr==0x40000021u);

    reset(); let_worker_finish=true;
    kui_toy_pilot_pause_after(-13);
    assert(!*at(context+28u) && !*at(context+WORK_BEGIN+60u));
    assert(worker_visits==1u && exec_visits==1u && check_visits==1u);
    assert(kui_toy_pilot_pause_retired==1u && !fault_visits && sr==0x40000021u);
    kui_toy_pilot_pause_after(1); assert(attempts==0u && retry_context==0u);

    reset(); kui_toy_pilot_pause_after(-13);
    assert(*at(context+28u)==context+WORK_BEGIN && !kui_toy_pilot_pause_retired);
    assert(!fault_visits && sr==0x40000021u);

    reset(); *at(context+32u)=1u; kui_toy_pilot_pause_after(-13);
    assert(worker_visits==1u && !exec_visits && !kui_toy_pilot_pause_pumps);
    assert(*at(context+28u)==context+WORK_BEGIN && sr==0x40000021u);

    reset(); *at(context+28u)+=4u; kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==4u && !exec_visits && fault_sr==0x40000021u);
    assert(captured_owner==UINT32_MAX && captured_token==UINT32_MAX);
    assert(captured_flags==0x20u && !diagnostic_token_reads);

    reset(); *at(context)=2u; kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==4u && !exec_visits && fault_sr==0x40000021u);

    reset(); *at(context+WORK_BEGIN+36u)=0x8c04d542u; kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==4u && !exec_visits && fault_sr==0x40000021u);

    reset(); *at(context+WORK_BEGIN+52u)=0x8c04d542u; kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==4u && !exec_visits && fault_sr==0x40000021u);

    reset(); *at(context+36u)=0x8c04d542u; kui_toy_pilot_pause_after(-13);
    assert(!fault_visits && exec_visits==1u && sr==0x40000021u);

    reset(); *at(context+28u)=context+WORK_BEGIN+WORK_BYTES;
    *at(context+WORK_BEGIN+WORK_BYTES)=context;
    kui_toy_pilot_pause_after(-13);
    assert(!fault_visits && worker_visits==1u && !exec_visits && sr==0x40000021u);
    assert(!kui_toy_pilot_pause_pumps && !kui_toy_pilot_pause_retired);
    assert(*at(context+28u)==context+WORK_BEGIN+WORK_BYTES);
    /* Inject the pre-existing VBlank SDK callback after authored deferral.
     * It, rather than pause_after, completes the file work. The next real
     * title admission succeeds and resets the returning-retry budget. */
    uint32_t saved_sr=sr;
    sr|=0xf0u; ready=true;
    kui_toy_pilot_pause_test_pump(context,game_sp);
    sr=saved_sr;
    assert(!*at(context+28u));
    kui_toy_pilot_pause_after(0);
    assert(!attempts && !retry_context && !fault_visits && sr==0x40000021u);
    assert(!kui_toy_pilot_pause_pumps && !kui_toy_pilot_pause_retired);

    reset(); *at(context+28u)=context+WORK_BEGIN+WORK_BYTES;
    *at(context+WORK_BEGIN+WORK_BYTES)=context;
    *at(context+WORK_BEGIN+WORK_BYTES+36u)=0x8c04d542u;
    kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==4u && !exec_visits && fault_sr==0x40000021u);

    reset(); *at(context+28u)=context+WORK_BEGIN+WORK_BYTES;
    kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==4u && !exec_visits && fault_sr==0x40000021u);

    reset(); *at(context+28u)=context+WORK_BEGIN+WORK_BYTES;
    *at(context+WORK_BEGIN+WORK_BYTES)=context;
    for (unsigned i=0; i<=RETRY_LIMIT_ATTEMPTS; ++i) kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==3u && fault_visits==1u);
    assert(kui_toy_pilot_pause_max_attempts==65537u);
    assert(worker_visits==RETRY_LIMIT_ATTEMPTS && !exec_visits && sr==0x40000021u);
    assert(*at(context+28u)==context+WORK_BEGIN+WORK_BYTES);

    reset(); *at(context+36u)=0u;
    kui_toy_pilot_pause_after(-13);
    assert(!fault_visits && exec_visits==1u && sr==0x40000021u);

    reset(); game_sp=KUI_TOY_PILOT_WORKER_BEGIN+0x100u;
    kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==7u && !exec_visits && fault_sr==0x40000021u);

    reset(); game_sp=GAME_RAM_BEGIN+NATIVE_STACK_RESERVE-4u;
    kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==7u && !exec_visits && fault_sr==0x40000021u);

    reset(); corrupt_gd=true; kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==4u && !exec_visits && fault_sr==0x40000021u);

    reset(); *at(context+36u)=0x8c010001u; kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==4u && !exec_visits && fault_sr==0x40000021u);

    reset(); kui_toy_pilot_pause_after(-13); retry_context+=4u;
    kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==4u && exec_visits==1u && fault_sr==0x40000021u);

    reset(); corrupt_after=true; kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==5u && fault_sr==0x40000021u);
    assert(captured_owner==UINT32_MAX && captured_token==UINT32_MAX);
    assert(captured_flags==0x20u && !diagnostic_token_reads);

    reset(); kui_toy_pilot_pause_after(-13); tick=RETRY_LIMIT_TICKS+1u;
    kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==3u && worker_visits==1u && fault_sr==0x40000021u);
    assert(captured_owner==context+WORK_BEGIN && captured_token==7u);
    assert(captured_flags==0x28u && diagnostic_token_reads==1u);
    assert(capture_sr==0x400000f1u);
    kui_toy_pilot_pause_after(-13);
    assert(capture_visits==1u && diagnostic_token_reads==1u && fault_visits==2u);

    reset(); kui_toy_pilot_pause_after(-13); tick=GAP_LIMIT_TICKS+1u;
    kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==2u && worker_visits==1u && fault_sr==0x40000021u);

    reset(); sr|=SR_BL; kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==1u && !worker_visits && !exec_visits);
    assert(fault_sr==(0x40000021u|SR_BL));
    assert(captured_flags==(0x28u|SR_BL) && capture_sr==(0x400000f1u|SR_BL));

    reset(); *at(context+32u)=1u;sr=0x400000f1u;attempts=RETRY_LIMIT_ATTEMPTS;
    kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==3u && !worker_visits && !exec_visits);
    assert(captured_owner==context+WORK_BEGIN && captured_token==7u);
    assert(captured_flags==0xfcu && fault_sr==0x400000f1u);

    reset();
    for (unsigned i=0; i<=RETRY_LIMIT_ATTEMPTS; ++i) kui_toy_pilot_pause_after(-13);
    assert(kui_toy_pilot_pause_detail==3u && fault_visits==1u);
    assert(kui_toy_pilot_pause_max_attempts==65537u);
    assert(worker_visits==RETRY_LIMIT_ATTEMPTS && sr==0x40000021u);
    printf("pause adapter: registered startup handler, native retirement, retained pending work, SDK busy, bounds, post-pump corruption, callback identity, deferred file owner/VBlank retirement, borrowed guest scratch, time/attempt bounds and SR restoration passed\n");
    return 0;
}
