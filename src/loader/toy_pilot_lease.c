/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_pilot_lease.h"
#include "kui/toy_pilot_cache.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define MANAGER UINT32_C(0x8c10e55c)
#define SOUND_FIRST UINT32_C(0xa0830040)
#define SOUND_END UINT32_C(0xa09f4000)
#define HEAD_OFFSET 24u
#define TAIL_OFFSET UINT32_C(0x7e8)
#define RECORD_BYTES 20u
#define RECORDS 100u
#define CANARY UINT32_C(0x54544352)
#define SR_BL UINT32_C(0x10000000)
#define SR_IMASK UINT32_C(0x000000f0)

#ifdef KUI_TOY_PILOT_LEASE_TEST
extern uint32_t kui_toy_pilot_lease_test_read(uint32_t address);
extern void kui_toy_pilot_lease_test_write(uint32_t address,uint32_t value);
extern uint32_t kui_toy_pilot_lease_test_sr_read(void);
extern void kui_toy_pilot_lease_test_sr_write(uint32_t value);
static uint32_t read_word(uint32_t address) { return kui_toy_pilot_lease_test_read(address); }
static void write_word(uint32_t address,uint32_t value) { kui_toy_pilot_lease_test_write(address,value); }
static uint32_t sr_read(void) { return kui_toy_pilot_lease_test_sr_read(); }
static void sr_write(uint32_t value) { kui_toy_pilot_lease_test_sr_write(value); }
#else
static uint32_t read_word(uint32_t address) { return *(volatile uint32_t *)(uintptr_t)address; }
static void write_word(uint32_t address,uint32_t value) { *(volatile uint32_t *)(uintptr_t)address=value; }
static uint32_t sr_read(void) {
    uint32_t value;
    __asm__ volatile("stc sr,%0":"=r"(value)::"memory");
    return value;
}
static void sr_write(uint32_t value) { __asm__ volatile("ldc %0,sr"::"r"(value):"memory","t"); }
#endif

static void barrier(void) { __asm__ volatile("":::"memory"); }

/* The admitted game's SFX and FMV allocations use mode 2, alignment 4.
 * Native records form two contiguous stacks: head upward, tail downward.
 * A freed record retains its callback but clears its other four fields. */
static bool stack(uint32_t offset,bool tail,uint32_t lower,uint32_t upper,
                  uint32_t *frontier,uint32_t *next) {
    uint32_t top=tail?upper:lower;
    bool empty=false;
    *next=RECORDS;
    for(uint32_t i=0;i<RECORDS;i++) {
        uint32_t record=MANAGER+offset+i*RECORD_BYTES;
        uint32_t start=read_word(record),canary=read_word(record+4u);
        uint32_t reserved=read_word(record+8u),active=read_word(record+12u);
        if(!active) {
            if(start || canary || reserved) return false;
            if(!empty) *next=i;
            empty=true;
            continue;
        }
        if(active!=1u || empty || reserved<8u || (reserved&3u) || (start&3u) ||
           start<lower || start>=upper || reserved>upper-start) return false;
        uint32_t end=start+reserved;
        if(canary!=end-4u || (tail?end!=top:start!=top)) return false;
        top=tail?start:end;
    }
    *frontier=top;
    return true;
}

enum kui_toy_pilot_bus_result kui_toy_pilot_lease_allocate(
    uint32_t bytes,uint32_t alignment,uint32_t *address) {
    if(!address || ((uintptr_t)address&3u) || !bytes || (alignment!=4u && alignment!=32u) ||
       bytes>UINT32_MAX-3u) return KUI_TOY_PILOT_BUS_ARGUMENT;
    uint32_t saved=sr_read();
    if(saved&SR_BL) return KUI_TOY_PILOT_BUS_BUSY;
    sr_write(saved|SR_IMASK);
    barrier();
    enum kui_toy_pilot_bus_result result=KUI_TOY_PILOT_BUS_STATE;
    uint32_t live=read_word(MANAGER),free_bytes=read_word(MANAGER+4u);
    uint32_t total=read_word(MANAGER+8u),original=read_word(MANAGER+12u);
    uint32_t lower=read_word(MANAGER+16u),upper=read_word(MANAGER+20u);
    uint32_t head=0,tail=0,next_head=0,next_tail=0;
    if(live!=1u || original<SOUND_FIRST || original>lower || lower-original>3u ||
       lower<SOUND_FIRST || upper>SOUND_END || lower>=upper ||
       ((lower|upper|total|free_bytes)&3u) || total!=upper-lower || free_bytes>total)
        goto done;
    if(!stack(HEAD_OFFSET,false,lower,upper,&head,&next_head) ||
       !stack(TAIL_OFFSET,true,lower,upper,&tail,&next_tail) ||
       head>tail || free_bytes!=tail-head) goto done;
    if(next_tail==RECORDS) { result=KUI_TOY_PILOT_BUS_BUSY;goto done; }
    uint32_t payload=(bytes+3u)&~UINT32_C(3);
    if(payload>UINT32_MAX-4u) { result=KUI_TOY_PILOT_BUS_ARGUMENT;goto done; }
    uint32_t reserved=payload+4u;
    /* Preserve the SDK's conservative free-space admission, without wrap. */
    uint32_t margin=2u*(alignment-1u)+4u;
    if(bytes>UINT32_MAX-margin || bytes+margin>free_bytes || reserved>tail-lower) {
        result=KUI_TOY_PILOT_BUS_BUSY;goto done;
    }
    uint32_t start=(tail-reserved)&~(alignment-1u);
    if(start<head || start<lower) { result=KUI_TOY_PILOT_BUS_BUSY;goto done; }
    reserved=tail-start;
    if(reserved>free_bytes) { result=KUI_TOY_PILOT_BUS_BUSY;goto done; }
    result=kui_toy_pilot_bus_write(tail-4u,CANARY);
    if(result!=KUI_TOY_PILOT_BUS_OK) goto done;
    /* No native allocator can run during this masked scope. Publish the
     * active flag last, after the canary write has drained successfully. */
    uint32_t record=MANAGER+TAIL_OFFSET+next_tail*RECORD_BYTES;
    write_word(record,start);
    write_word(record+4u,tail-4u);
    write_word(record+8u,reserved);
    write_word(record+16u,0u);
    write_word(MANAGER+4u,free_bytes-reserved);
    barrier();
    write_word(record+12u,1u);
    barrier();
    kui_toy_pilot_cache_publish(record,RECORD_BYTES);
    kui_toy_pilot_cache_publish(MANAGER+4u,4u);
    *address=start;
done:
    barrier();
    sr_write(saved);
    return result;
}
