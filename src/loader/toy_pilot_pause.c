/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot.h"
#include <stdint.h>

/* The admitted title's pause helper retries -13 without returning to its
 * normal main-loop service. Supply one existing SDK retirement opportunity
 * per failed attempt, after giving the authored worker a bounded visit.
 * The SDK, rather than this adapter, continues to own and retire its work.
 * This covers a missed/delayed main or VBlank pump; it is not evidence that
 * the native VBlank callback was absent on the reported console run. */
#define NATIVE_CONTEXT UINT32_C(0x8c0c69ec)
#define NATIVE_WRAPPERS UINT32_C(0x8c0c69f8)
#define NATIVE_WRAPPER_TABLE UINT32_C(0x8c0c5980)
#define NATIVE_GD_TABLE UINT32_C(0x8c0c6260)
#define NATIVE_BOOT_END UINT32_C(0x8c0c6b9c)
/* The final allocator header precedes the worker lease by 32 bytes. Native
 * SDK containers and work records must not overlap either reservation. */
#define NATIVE_RAM_END UINT32_C(0x8cfcffe0)
#define GAME_RAM_BEGIN UINT32_C(0x8c008000)
#define GAME_RAM_END UINT32_C(0x8d000000)
#define BRIDGE_ANCHOR_BYTES 16u
/* SDK retirement 68 + native CHECK scratch/veneer 24 + resident GD caller
 * save 36 consume 128 bytes; the 192-byte admission includes spare room.
 * The BIOS stub between the latter two is a tail jump. */
#define NATIVE_STACK_RESERVE 192u
#define TIMER_TCNT UINT32_C(0xffd8000c)
#define SR_BL UINT32_C(0x10000000)
#define RETRY_LIMIT_TICKS 781250u
#define APPLY_LIMIT_TICKS 1562500u
#define GAP_LIMIT_TICKS 7812500u
#define RETRY_LIMIT_ATTEMPTS 65536u
/* Successful PAUSE may owe up to 1s of bounded STOP dispatch followed by
 * the full ~0.764s loop-quiescence proof. Keep a separate 2s drain deadline
 * and stopped-clock cap; native -13 retries retain their original 1s bound. */
#define APPLY_LIMIT_ATTEMPTS 1048576u
#define WORK_BEGIN 0x1080u
#define WORK_BYTES 84u
#define WORK_COUNT 8u
#define CONTEXT_BYTES 0x1374u
_Static_assert(WORK_BEGIN+(WORK_COUNT+1u)*WORK_BYTES==CONTEXT_BYTES,
    "Toy SDK container allocation");

/* Numeric breadcrumbs consumed by the worker's terminal snapshot. Details
 * identify adapter admission/liveness failures, not native SDK error codes. */
extern volatile uint32_t kui_toy_pilot_pause_entries;
extern volatile uint32_t kui_toy_pilot_pause_retries;
extern volatile uint32_t kui_toy_pilot_pause_pumps;
extern volatile uint32_t kui_toy_pilot_pause_retired;
extern volatile uint32_t kui_toy_pilot_pause_detail;
extern volatile uint32_t kui_toy_pilot_pause_max_attempts;
static uint32_t attempts, first_tick, last_tick, retry_context;

extern void kui_toy_pilot_worker_step(void);
extern void kui_toy_pilot_worker_pause_fault(uint32_t detail);
extern const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void);
extern void kui_toy_pilot_gd_capture(uint32_t,uint32_t,uint32_t);

#ifdef KUI_TOY_PILOT_PAUSE_TEST
extern uint32_t kui_toy_pilot_pause_test_read(uint32_t address,unsigned bytes);
extern uint32_t kui_toy_pilot_pause_test_sr(void);
extern void kui_toy_pilot_pause_test_set_sr(uint32_t sr);
extern uint32_t kui_toy_pilot_pause_test_game_sp(void);
extern void kui_toy_pilot_pause_test_pump(uint32_t context,uint32_t game_sp);
static uint32_t word(uint32_t a) { return kui_toy_pilot_pause_test_read(a,4); }
static uint32_t read_sr(void) { return kui_toy_pilot_pause_test_sr(); }
static void write_sr(uint32_t sr) { kui_toy_pilot_pause_test_set_sr(sr); }
static uint32_t native_stack_pointer(void) { return kui_toy_pilot_pause_test_game_sp(); }
static void native_pump(uint32_t context) {
    kui_toy_pilot_pause_test_pump(context,native_stack_pointer());
}
#else
extern const uint8_t __toy_pilot_stack_top[] __asm__("__toy_pilot_stack_top");
extern void kui_toy_pilot_native_pump(uint32_t context);
static uint32_t word(uint32_t a) { return *(volatile const uint32_t *)(uintptr_t)a; }
static uint32_t read_sr(void) {
    uint32_t sr;__asm__ __volatile__("stc sr,%0":"=r"(sr));return sr;
}
static void write_sr(uint32_t sr) {
    __asm__ __volatile__("ldc %0,sr"::"r"(sr):"memory","t");
}
static uint32_t native_stack_pointer(void) {
    return word((uint32_t)(uintptr_t)__toy_pilot_stack_top-BRIDGE_ANCHOR_BYTES);
}
static void native_pump(uint32_t context) { kui_toy_pilot_native_pump(context); }
#endif

static bool ram(uint32_t a,uint32_t bytes) {
    return !(a&3u) && a>=NATIVE_BOOT_END && a<NATIVE_RAM_END &&
        bytes<=NATIVE_RAM_END-a;
}
static bool native_stack(uint32_t sp) {
    if((sp&3u) || sp<GAME_RAM_BEGIN+NATIVE_STACK_RESERVE || sp>GAME_RAM_END)
        return false;
    return sp<=NATIVE_RAM_END || sp-NATIVE_STACK_RESERVE>=KUI_TOY_PILOT_WORKER_END;
}
static bool native_context(uint32_t context,uint32_t *pending) {
    if(!ram(context,CONTEXT_BYTES) || word(NATIVE_WRAPPERS)!=NATIVE_WRAPPER_TABLE ||
       word(NATIVE_WRAPPER_TABLE+100u)!=0x8c0b23a8u ||
       word(context+12u)!=NATIVE_GD_TABLE ||
       word(NATIVE_GD_TABLE+16u)!=0x8c0bd366u ||
       word(NATIVE_GD_TABLE+36u)!=0x8c0bd494u ||
       word(NATIVE_GD_TABLE+48u)!=0x8c0bd4e6u ||
       word(NATIVE_GD_TABLE+64u)!=0x8c0bd552u ||
       word(NATIVE_GD_TABLE+72u)!=0x8c0bd566u) return false;
    uint32_t error_callback=word(context+36u);
    if(error_callback && error_callback!=0x8c04d542u) return false;
    uint32_t count=word(context);
    if(count!=WORK_COUNT ||
       word(context+20u)!=context+WORK_BEGIN ||
       word(context+24u)!=context+WORK_BEGIN+WORK_BYTES ||
       word(context+32u)>1u) return false;
    uint32_t owner=word(context+28u);
    if(owner) {
        uint32_t first=context+WORK_BEGIN;
        if(owner<first || owner>=first+(count+1u)*WORK_BYTES ||
           (owner-first)%WORK_BYTES || word(owner)!=context ||
           word(owner+36u) || word(owner+52u)) return false;
    }
    *pending=owner;return true;
}
static void fail(uint32_t detail) {
    if(!kui_toy_pilot_pause_detail) {
        /* Observe the first terminal failure under the same interrupt mask
         * as native validation, retaining the caller's original SR bits.
         * Invalid contexts expose sentinels, never unchecked work fields. */
        uint32_t sr=read_sr();write_sr(sr|0xf0u);
        uint32_t context=word(NATIVE_CONTEXT),owner=UINT32_MAX,token=UINT32_MAX;
        uint32_t flags=sr&(0xf0u|SR_BL);
        if(native_context(context,&owner)) {
            flags|=8u;
            if(word(context+32u)) flags|=4u;
            token=word(context+WORK_BEGIN+60u);
        }
        kui_toy_pilot_gd_capture(owner,token,flags);
        write_sr(sr);
        kui_toy_pilot_pause_detail=detail;
    }
    kui_toy_pilot_worker_pause_fault(detail);
}

static void apply_successful_pause(void) {
    if(read_sr()&SR_BL) { fail(1u);return; }
    uint32_t began=~word(TIMER_TCNT);
    for(uint32_t visits=0;visits<APPLY_LIMIT_ATTEMPTS;visits++) {
        kui_toy_pilot_worker_step();
        const struct kui_toy_pilot_snapshot *p=kui_toy_pilot_snapshot();
        /* A native success acknowledges submission. Before the title enters
         * a movie, retire our PAUSE ownership on the private worker stack.
         * A newer command retains its own lifecycle; never drain it here. */
        if(!p || p->fault || p->command!=22u ||
           p->generation==p->applied_generation) return;
        uint32_t elapsed=~word(TIMER_TCNT)-began;
        if(elapsed>GAP_LIMIT_TICKS) { fail(2u);return; }
        if(elapsed>APPLY_LIMIT_TICKS) { fail(3u);return; }
    }
    fail(3u);
}

void kui_toy_pilot_pause_after(int32_t native_result) {
    ++kui_toy_pilot_pause_entries;
    if(native_result!=-13) {
        attempts=0;retry_context=0;
        if(native_result==0) apply_successful_pause();
        return;
    }
    ++kui_toy_pilot_pause_retries;
    uint32_t entry_sr=read_sr();
    if(entry_sr&SR_BL) { fail(1u);return; }
    uint32_t now=~word(TIMER_TCNT);
    if(!attempts) { first_tick=now;last_tick=now; }
    else if(now-last_tick>GAP_LIMIT_TICKS) { fail(2u);return; }
    last_tick=now;
    if(++attempts>kui_toy_pilot_pause_max_attempts)
        kui_toy_pilot_pause_max_attempts=attempts;
    if(attempts>RETRY_LIMIT_ATTEMPTS || now-first_tick>RETRY_LIMIT_TICKS) {
        fail(3u);return;
    }

    /* Worker C is already on the shared private stack. Its bus transactions
     * restore their masks before this call reaches the native SDK pump. */
    kui_toy_pilot_worker_step();
    uint32_t sr=read_sr();write_sr(sr|0xf0u);
    uint32_t context=word(NATIVE_CONTEXT),before=0;
    if(!native_context(context,&before) || (retry_context && retry_context!=context)) {
        write_sr(sr);fail(4u);return;
    }
    retry_context=context;
    if(word(context+32u)==1u) { write_sr(sr);return; }
    /* A known file work record remains owned by the existing VBlank SDK
     * callback. Restore its IRQ opportunity and keep the native -13 result;
     * no authored pump invokes file completion. Only the control record is
     * eligible for automatic main-context retirement here. */
    if(before && before!=context+WORK_BEGIN) { write_sr(sr);return; }
    /* A registered native error handler is normal startup state. The exact
     * scalar CHECK contract returns success/progress or errors -28/-32;
     * the admitted control pump's other local errors are -19/-28. The title
     * returns directly from these. Its delegate-taking -23/-33 paths require
     * sense classes this GD encoder never emits. Validate callback identity
     * above, rather than rejecting registration before an error exists. */
    if(!native_stack(native_stack_pointer())) { write_sr(sr);fail(7u);return; }
    ++kui_toy_pilot_pause_pumps;
    /* Native SDK CHECK uses its own stack as a GD scratch destination.
     * Borrow the suspended game stack below the preserved bridge frame;
     * the authored worker, bridge anchor, and owner remain protected. The
     * callback guard above permits only directly returning native targets. */
    native_pump(context);
    uint32_t after=0;
    bool valid=word(NATIVE_CONTEXT)==context && native_context(context,&after) &&
        (!after || after==context+WORK_BEGIN);
    if(valid && before && after!=before) ++kui_toy_pilot_pause_retired;
    write_sr(sr);
    if(!valid) fail(5u);
}
