/* SPDX-License-Identifier: GPL-3.0-only */
#include "native_return_hardware.h"
#include <stddef.h>

#define MAPLE_ADDR UINT32_C(0xa05f6c04)
#define MAPLE_SELECT UINT32_C(0xa05f6c10)
#define MAPLE_ENABLE UINT32_C(0xa05f6c14)
#define MAPLE_STATE UINT32_C(0xa05f6c18)
#define MAPLE_PROTECT UINT32_C(0xa05f6c8c)
#define PVR_DMA_ENABLE UINT32_C(0xa05f7c14)
#define PVR_DMA_STATE UINT32_C(0xa05f7c18)
#define SORT_DMA_STATE UINT32_C(0xa05f6820)
#define POLL_LIMIT 1000000u
static _Alignas(32) volatile uint32_t controller_tx[8];
static _Alignas(32) volatile uint32_t controller_rx[256];
static bool controller_fault;
#ifdef KUI_NATIVE_RETURN_HARDWARE_HOST_TEST
extern uint32_t kui_native_return_hw_test_read(uint32_t);
extern void kui_native_return_hw_test_write(uint32_t,uint32_t);
extern uint32_t kui_native_return_hw_test_sr(void);
extern uint32_t kui_native_return_hw_test_physical(const void *);
extern void kui_native_return_hw_test_cache_off(void);
#define read_hw kui_native_return_hw_test_read
#define write_hw kui_native_return_hw_test_write
#define read_sr kui_native_return_hw_test_sr
#define physical kui_native_return_hw_test_physical
#define cache_off kui_native_return_hw_test_cache_off
uint32_t *kui_native_return_hw_test_receive(void) {return (uint32_t *)(uintptr_t)controller_rx;}
const uint32_t *kui_native_return_hw_test_transmit(void) {return (const uint32_t *)(uintptr_t)controller_tx;}
void kui_native_return_hw_test_reset(void) {controller_fault=false;}
#else
static uint32_t read_hw(uint32_t address) {return *(volatile const uint32_t *)(uintptr_t)address;}
static void write_hw(uint32_t address,uint32_t value) {*(volatile uint32_t *)(uintptr_t)address=value;}
static uint32_t read_sr(void) {uint32_t value;__asm__ volatile("stc sr,%0":"=r"(value));return value;}
static uint32_t physical(const void *p) {return (uint32_t)(uintptr_t)p&UINT32_C(0x1fffffff);}
extern void kui_native_return_cache_off(void);
#define cache_off kui_native_return_cache_off
#endif
static bool irq_masked(void) {return (read_sr()&UINT32_C(0x400000f0))==UINT32_C(0x400000f0);}
static bool wait_clear(uint32_t address,uint32_t mask) {
    for(unsigned poll=0u;poll<POLL_LIMIT;++poll)if(!(read_hw(address)&mask))return true;
    return false;
}
static bool dma_buffer(uint32_t address,size_t bytes) {
    return !(address&31u) && address>=UINT32_C(0x0c010000) &&
        address<=UINT32_C(0x0d000000)-bytes;
}
int kui_native_return_controller(uint32_t *buttons) {
    if(buttons)*buttons=0u;
    if(!buttons || controller_fault || !irq_masked() || (read_hw(MAPLE_STATE)&1u))return -1;
    uint32_t tx=physical((const void *)(uintptr_t)controller_tx);
    uint32_t rx=physical((const void *)(uintptr_t)controller_rx);
    if(!dma_buffer(tx,sizeof(controller_tx)) || !dma_buffer(rx,sizeof(controller_rx)))return -1;
    uint32_t old_addr=read_hw(MAPLE_ADDR),old_protect=read_hw(MAPLE_PROTECT);
    for(unsigned i=0;i<256u;++i)controller_rx[i]=UINT32_MAX;
    controller_tx[0]=UINT32_C(0x80000001);controller_tx[1]=rx;
    controller_tx[2]=UINT32_C(0x01002009);controller_tx[3]=UINT32_C(0x01000000);
    /* This terminal path owns the bus; a software trigger cannot resurrect
     * the game's VBlank chain. Full16MiB protection includes both buffers. */
    write_hw(MAPLE_ENABLE,0u);write_hw(MAPLE_SELECT,0u);
    write_hw(MAPLE_PROTECT,UINT32_C(0x6155404f));write_hw(MAPLE_ADDR,tx);
    write_hw(MAPLE_ENABLE,1u);write_hw(MAPLE_STATE,1u);
    bool complete=wait_clear(MAPLE_STATE,1u);
    write_hw(MAPLE_ENABLE,0u);
    if(!complete) {
        write_hw(MAPLE_STATE,0u);
        if(!wait_clear(MAPLE_STATE,1u)) {controller_fault=true;return -1;}
    }
    /* The menu is terminal: retain software triggering and disabled Maple.
     * Restoring the game's VBlank enable here could reissue its old chain
     * while its CPU and SDK driver are parked. */
    write_hw(MAPLE_ADDR,old_addr);write_hw(MAPLE_SELECT,0u);
    write_hw(MAPLE_PROTECT,old_protect);write_hw(MAPLE_ENABLE,0u);
    if(!complete)return 0;
    uint32_t header=controller_rx[0];
    if((header&UINT32_C(0xff00ffff))!=UINT32_C(0x03000008) ||
       (header&UINT32_C(0x00e00000))!=UINT32_C(0x00200000) ||
       controller_rx[1]!=UINT32_C(0x01000000))return 0;
    *buttons=(~controller_rx[2])&UINT32_C(0xffff);return 1;
}
bool kui_native_return_quiesce(void) {
    if(!irq_masked() || controller_fault || !wait_clear(MAPLE_STATE,1u) ||
       (read_hw(UINT32_C(0xffa0001c))&1u) ||
       !wait_clear(UINT32_C(0xa05f7418),1u))return false;
    /* Disable hardware re-trigger before draining each G2 transfer. */
    for(unsigned channel=0;channel<4u;++channel) {
        uint32_t base=UINT32_C(0xa05f7800)+channel*32u;
        write_hw(base+20u,0u);
        if(!wait_clear(base+24u,1u))return false;
    }
    /* Retail MAIGO also excludes the independent PVR-interface and sort
     * DMA engines before replacing RAM. PDEN remains armed after a normal
     * completion; disarm it before draining PDST. Sort DMA has no separate
     * hardware retrigger enable: let its current transfer finish rather
     * than treating a write to its start bit as proof of retirement. */
    write_hw(PVR_DMA_ENABLE,0u);
    if(!wait_clear(PVR_DMA_STATE,1u) || !wait_clear(SORT_DMA_STATE,1u) ||
       !wait_clear(UINT32_C(0xa05f6808),1u))return false;
    write_hw(MAPLE_ENABLE,0u);
    /* No game interrupt handler may run while its text is replaced. */
    for(unsigned bank=0u;bank<3u;++bank)
        for(unsigned kind=0u;kind<3u;++kind)
            write_hw(UINT32_C(0xa05f6910)+bank*16u+kind*4u,0u);
    for(unsigned channel=0u;channel<4u;++channel) {
        uint32_t chcr=UINT32_C(0xffa0000c)+channel*16u;
        write_hw(chcr,read_hw(chcr)&~UINT32_C(1));
    }
    write_hw(UINT32_C(0xa0702c00),read_hw(UINT32_C(0xa0702c00))|1u);
    cache_off();return true;
}
