/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the production bus's independent MMIO/byte model. Unused legacy
 * block/profile suites are discarded; this target never links block DMA. */
#undef KUI_RETAIL_TRANSPORT
#define main baseline_sci_bus_main
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wreturn-type"
#include "test_sci_sd_bus.c"
#pragma GCC diagnostic pop
#undef main
#define KUI_RETAIL_TRANSPORT 1
#include "retail_async.h"
#include "kui/vmu_backing.h"
#if KUI_VMU_NATIVE_BACKGROUND
static kui_retail_async_region_type fixture_region;
kui_retail_async_region_type *kui_retail_async_region_storage=&fixture_region;
#else
struct kui_retail_async_region kui_retail_async_region;
#endif

int main(void) {
    const struct kui_loader_sd_bus *bus=kui_sci_sd_bus();
    assert(bus && bus->select && bus->transfer);
    assert(!bus->begin && !bus->end && !bus->transfer_block);
#if KUI_VMU_NATIVE_BACKGROUND
    assert(bus->ticks);
    reset();assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    uint32_t before=bus->ticks(NULL);
    (void)bus->transfer(NULL,0xff,false);
    assert(bus->ticks(NULL)-before==8u);
    before=bus->ticks(NULL);
    (void)bus->transfer(NULL,0xff,true);
    assert(bus->ticks(NULL)-before==256u);
    kui_retail_async_region.shared.port.work=UINT32_MAX-3u;
    (void)bus->transfer(NULL,0xff,false);
    assert(bus->ticks(NULL)==4u);
    kui_sci_sd_release();restored();assert(!kui_sci_sd_faulted());

    /* Real CMD10 identity parser using the actual gated NO_BLOCK descriptor.
     * This fails with the old NULL clock before any wire command. */
    reset();assert(kui_sci_sd_acquire()==KUI_LOADER_SD_OK);
    hw.patterned_rx=hw.use_custom_rx=true;
    memset(hw.custom_rx,0xff,sizeof(hw.custom_rx));
    uint8_t cid[16];for(unsigned i=0;i<16u;++i) cid[i]=(uint8_t)(3u+i*13u);
    hw.custom_rx[7]=0u;hw.custom_rx[8]=0xfeu;
    memcpy(hw.custom_rx+9u,cid,sizeof(cid));
    uint16_t crc=crc16_reference(cid,sizeof(cid));
    hw.custom_rx[25]=(uint8_t)(crc>>8);hw.custom_rx[26]=(uint8_t)crc;
    struct kui_loader_sd copied={.bus=*bus,.blocks=2048u,.high_capacity=true,.ready=true};
    struct kui_vmu_sd_identity identity;
    assert(kui_vmu_sd_identity_read(&copied,&identity)==KUI_VMU_BACKING_OK);
    assert(identity.sectors==2048u && identity.high_capacity && !memcmp(identity.cid,cid,sizeof(cid)));
    assert(copied.last_command==10u && !copied.last_response && copied.ready);
    assert(hw.bytes==28u && !hw.dma_writes && !hw.irq_disables && !hw.irq_restores);
    assert(bus->ticks(NULL)==28u*8u+4u);
    kui_sci_sd_release();restored();assert(!kui_sci_sd_faulted());
    puts("native Background VMU bus: byte-work clock, wrap, real CMD10 identity and exact restoration passed");
#else
    assert(!bus->ticks);
    puts("ordinary NO_BLOCK SCI bus retains its command-only descriptor");
#endif
    return 0;
}
