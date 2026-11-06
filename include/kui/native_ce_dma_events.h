/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_CE_DMA_EVENTS_H
#define KUI_NATIVE_CE_DMA_EVENTS_H
#include <stdint.h>
#ifndef KUI_NATIVE_CE_EVENTS_ADDRESS
#define KUI_NATIVE_CE_EVENTS_ADDRESS(address) ((uintptr_t)(address))
#endif

/* A native bootstrap may enter CE only later. Admit its live IRQ dispatch,
 * rather than assuming that its stage loaded a CE kernel. These are the same
 * masked instruction contracts used by the separate CE stage: the platform
 * handler call followed by pending-mask/ring/reschedule bookkeeping. No
 * kernel addresses, SYSINTR table patches or executable writes are assumed. */
static int kui_native_ce_dma_bind(uint32_t vbr,volatile uint32_t bound[3]) {
    static const uint16_t pattern[]={
        0x567a,0xd000,0x6163,0x4109,0x4101,0x011e,0x072a,0x410b,
        0x0009,0x472a,0xd700,0x8800,0x8900,0x8802,0x8900,
        0x70f8,0x4011,0x8f00,0xe301,0xd100,0x6212,0x430d,0x2238,
        0x8f00,0x223b,0x2122,0xd100,0x6312,0x313c,0x8014,0x3138,
        0x6033,0x7001,0xc91f,0x2102,0xd100,
    };
    if((vbr&3u) || vbr<0x8c010000u || vbr>0x8cfff000u)return 0;
    const volatile uint16_t *code=(const volatile uint16_t *)KUI_NATIVE_CE_EVENTS_ADDRESS(vbr+0x600u);
    for(unsigned i=0;i<sizeof(pattern)/sizeof(pattern[0]);i++) {
        uint16_t want=pattern[i];
        /* Only PC-relative loads and branch displacements are wildcarded.
         * CMP/EQ #0 (8800) must retain its exact immediate value. */
        uint16_t got=code[i];
        if((want&255u)||want==0x8800u) {if(got!=want)return 0;}
        else if((got>>8)!=(want>>8))return 0;
    }
    uint32_t at=vbr+0x626u;
    for(unsigned delta=14u;;delta+=4u) {
        uint32_t literal=(at&~3u)+4u+
            (*(const volatile uint16_t *)KUI_NATIVE_CE_EVENTS_ADDRESS(at)&255u)*4u;
        uint32_t value=*(const volatile uint32_t *)KUI_NATIVE_CE_EVENTS_ADDRESS(literal);
        if((value&3u) || value<0x8c010000u || value>0x8cffffd8u)return 0;
        /* Only private metadata is updated. Rejection is terminal before
         * command acceptance; a partial private binding is never consumed. */
        *bound++=value;
        if(delta==22u)break;
        at+=delta;
    }
    return 1;
}

/* The serialized native GD entry holds IMASK15. Queue exactly the device
 * SYSINTRs CE's own dispatch uses: 20 drive, 21 G1 DMA completion. Pending
 * events are not inserted twice; all stores use CE's own cached P1 view. */
static int kui_native_ce_dma_raise(const volatile uint32_t bound[3],uint32_t sysintr) {
    volatile uint32_t *pending=(volatile uint32_t *)KUI_NATIVE_CE_EVENTS_ADDRESS(bound[0]);
    volatile uint32_t *head=(volatile uint32_t *)KUI_NATIVE_CE_EVENTS_ADDRESS(bound[1]);
    uint32_t index=sysintr-8u,bit=1u<<index;
    if(!(*pending&bit)) {
        uint32_t next=*head;
        if(next>=32u)return 0;
        *pending|=bit;
        ((volatile uint8_t *)head)[4u+next]=(uint8_t)index;
        *head=(next+1u)&31u;
    }
    *(volatile uint8_t *)KUI_NATIVE_CE_EVENTS_ADDRESS(bound[2])|=1u;
    return 1;
}
#endif
