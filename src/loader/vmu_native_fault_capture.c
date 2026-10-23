/* SPDX-License-Identifier: GPL-3.0-only */
#include "vmu_native_fault_capture.h"
#include <stddef.h>
#include <string.h>

volatile uint32_t kui_vmu_native_fault_capture_state[8];
#define VECTOR UINT32_C(0x8c00f500)
#define VBR UINT32_C(0x8c00f400)
#define COMMON UINT32_C(0x8c1da10c)
#define DEFAULT UINT32_C(0x8c1d9f50)

#ifdef KUI_VMU_NATIVE_FAULT_CAPTURE_HOST_TEST
extern uint32_t kui_fault_test_sr(void),kui_fault_test_vbr(void);
extern uint32_t kui_fault_test_read(uint32_t);
extern void kui_fault_test_write(uint32_t,uint32_t);
extern void kui_fault_test_publish(uint32_t,uint32_t);
#define owner_sr kui_fault_test_sr
#define owner_vbr kui_fault_test_vbr
#define word kui_fault_test_read
#define store kui_fault_test_write
#define publish kui_fault_test_publish
extern uint32_t kui_fault_test_gateway(void);
extern bool kui_fault_test_owned(void);
#define GATEWAY kui_fault_test_gateway()
#define owned_targets kui_fault_test_owned
#else
#include "kui/vmu_native_passive.h"
extern const struct kui_vmu_native_passive_exports kui_vmu_native_worker_exports;
extern uint8_t fault_bss_begin[] __asm__("__vmu_native_worker_bss_begin");
extern uint8_t fault_bss_end[] __asm__("__vmu_native_worker_bss_end");
extern uint8_t fault_stack_bottom[] __asm__("__vmu_native_worker_stack_bottom");
extern uint8_t fault_stack_top[] __asm__("__vmu_native_worker_stack_top");
static uint32_t owner_sr(void) {uint32_t v;__asm__ volatile("stc sr,%0":"=r"(v));return v;}
static uint32_t owner_vbr(void) {uint32_t v;__asm__ volatile("stc vbr,%0":"=r"(v));return v;}
/* Inspect the same P1 alias the SDK uses, including a still-dirty owner
 * operand line. Publication later writes these lines back before ICI. */
static uint32_t word(uint32_t at) {return *(volatile const uint32_t *)(uintptr_t)at;}
static void store(uint32_t at,uint32_t v) {*(volatile uint32_t *)(uintptr_t)at=v;}
/* Implemented in a P2 assembly island so CCR.ICI is never written while
 * executing a cached instruction. Original CCR mode bits are retained. */
extern void kui_vmu_native_fault_capture_publish(uint32_t,uint32_t);
#define publish kui_vmu_native_fault_capture_publish
#define GATEWAY ((uint32_t)(uintptr_t)kui_vmu_native_fault_capture_gateway)
static bool owned_targets(void) {
    uint32_t base=(uint32_t)(uintptr_t)&kui_vmu_native_worker_exports;
    uint32_t begin=(uint32_t)(uintptr_t)fault_bss_begin;
    uint32_t end=(uint32_t)(uintptr_t)fault_bss_end;
    uint32_t low=(uint32_t)(uintptr_t)fault_stack_bottom;
    uint32_t high=(uint32_t)(uintptr_t)fault_stack_top;
    uint32_t scratch=(uint32_t)(uintptr_t)kui_vmu_native_fault_capture_state;
    uint32_t terminal=(uint32_t)(uintptr_t)kui_vmu_native_runtime_fault_hook;
    uint32_t gate=GATEWAY;
    return (base&UINT32_C(0xff000000))==UINT32_C(0xac000000) &&
        !(base&31u) && begin>=base+80u && begin<=end && end<=low &&
        low==base+UINT32_C(0x1f000) && high==base+UINT32_C(0x20000) &&
        scratch>=begin && scratch<=end-sizeof(kui_vmu_native_fault_capture_state) &&
        gate>=base-UINT32_C(0x20000000)+80u &&
        gate<begin-UINT32_C(0x20000000) && !(terminal&1u) &&
        terminal>=base-UINT32_C(0x20000000)+80u &&
        terminal<begin-UINT32_C(0x20000000);
}
#endif

int kui_vmu_native_fault_capture_install(bool enabled,
    const struct kui_vmu_native_discovery *driver) {
    static const uint8_t digest[32]={
        0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
        0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
    static const uint32_t template[15]={
        0x00090009u,0x00090009u,0x2f160009u,0xc7076103u,0xe1ff2012u,
        0x41184128u,0x61127124u,0x2012d002u,0xd002d104u,0x0009412bu,
        0x8c1da418u,0u,0xac004000u,COMMON,0x00090009u};
    /* The unchanged common prologue saves exception control before using the
     * ordinary SDK frame/dispatch path. We never redirect that chain. */
    static const uint32_t common[3]={0x4f3361f6u,0x4f224f43u,0x4f234f13u};
    if(!enabled) return 0;
    if(!driver) return -1;
    if(memcmp(driver->image_sha256,digest,sizeof(digest))) return 0;
    if(driver->version!=KUI_VMU_NATIVE_DISCOVERY_VERSION || driver->bytes!=sizeof(*driver) ||
       driver->family!=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 ||
       driver->requirements!=KUI_VMU_NATIVE_REQUIREMENTS ||
       driver->load_address!=UINT32_C(0x8c010000) || driver->image_bytes!=2143652u ||
       (owner_sr()&0xf0u)!=0xf0u || owner_vbr()!=VBR ||
       kui_vmu_native_fault_capture_state[7] || (GATEWAY&1u) ||
       (GATEWAY&UINT32_C(0xff000000))!=UINT32_C(0x8c000000) || !owned_targets()) return -1;
    for(unsigned i=0;i<15u;i++) {
        /* The original vector self-modifies only its saved-R0 word. */
        if(i!=11u && word(VECTOR+4u*i)!=template[i]) return -1;
    }
    for(unsigned i=0;i<3u;i++) if(word(COMMON+4u*i)!=common[i]) return -1;
    if(word(UINT32_C(0x8c1da270))!=UINT32_C(0x8c1da418)) return -1;
    /* Called only after ordinary arm admission owns this relocated worker.
     * Direct linked symbols carry no retired-stage pointers or ABI exports. */
    kui_vmu_native_fault_capture_state[4]=VECTOR+8u;
    kui_vmu_native_fault_capture_state[5]=DEFAULT;
    /* Prepare the unreachable literal before publishing the entry redirect.
     * Under IMASK15, no owner handler can run during these bounded writes. */
    store(VECTOR+0x38u,GATEWAY);
    store(VECTOR+4u,UINT32_C(0x0009402b));
    store(VECTOR,UINT32_C(0xd00d6f03));
    publish(VECTOR,VECTOR+0x3cu);
    if(word(VECTOR)!=UINT32_C(0xd00d6f03) || word(VECTOR+4u)!=UINT32_C(0x0009402b) ||
       word(VECTOR+0x38u)!=GATEWAY) {
        /* Roll back only our three writes before reporting unsupported. */
        store(VECTOR,template[0]);store(VECTOR+4u,template[1]);store(VECTOR+0x38u,template[14]);
        publish(VECTOR,VECTOR+0x3cu);return -1;
    }
    kui_vmu_native_fault_capture_state[7]=1u;
    return 1;
}
