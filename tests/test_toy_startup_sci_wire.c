/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual SCI stream receiver/card/MMIO model, independently clocked between
 * masked DATA entries. Legacy fixture audio calls abort if accidentally used. */
#include <stdint.h>
int kui_toy_pilot_sci_audio_read(uint32_t,uint32_t,void *);
void kui_toy_pilot_sci_audio_cancel(void);
#define main shared_wire_fixture_main
#include "test_toy_pilot_sci_async_integration.c"
#undef main
int kui_toy_pilot_sci_audio_read(uint32_t lba,uint32_t generation,void *output) {
    (void)lba;(void)generation;(void)output;
    assert(!"startup DATA reached raw audio transport");return -1;
}
void kui_toy_pilot_sci_audio_cancel(void) {assert(!"startup DATA reached audio cancellation");}

static void startup_data_wire(bool scattered,uint32_t offset,bool bad_crc) {
    setup(scattered,offset);
    if(bad_crc) {card.corrupt_lba=physical_block(45000u);card.corrupt_count=1u;}
    uint32_t token=data_request(45000u,6u);
    CHECK(pump(false)==KUI_TOY_SCI_PENDING && m.rx);
    elapse(120u);CHECK(m.tcr>0u && m.tcr<513u);
    for(unsigned i=0;i<6u*2048u;i++)CHECK(ram[OUT-BEGIN+i]==0xa5u);
    unsigned programmed=hw.programmed_bytes;
    (void)pump(true);
    CHECK(service.pending && !service.completed_bytes && m.rx);
    CHECK(hw.programmed_bytes==programmed && !waits);
    for(unsigned n=0;n<150u && service.pending;n++) {
        elapse(2200u);irq();(void)pump((n&1u)!=0u);
    }
    CHECK(!service.pending && !service.error && service.completed_bytes==6u*2048u);
    CHECK(kui_retail_image_read(&reference,45000u,6u,KUI_GAME_SECTOR_MODE1,expected,sizeof(expected))==KUI_GAME_OK);
    CHECK(!memcmp(ram+OUT-BEGIN,expected,6u*2048u));
    CHECK(kui_retail_gd_dispatch(&service,token,STATUS,0u,KUI_GD_CHECK)==KUI_GD_COMPLETED);
    CHECK(!hw.leased && !m.rx && !waits && !active_stops);
    if(bad_crc)CHECK(kui_sci_stream_stats()->crc_errors && kui_toy_pilot_sci_snapshot()->retries);
    hw.in_entry=true;CHECK(kui_toy_pilot_sci_quiesce());kui_toy_pilot_sci_retire();hw.in_entry=false;
    CHECK(!kui_toy_pilot_sci_startup_active() && hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    unsigned clocks=card.clocks,starts=m.dma_starts;
    for(unsigned n=0;n<4u;n++) {elapse(5000u);irq();(void)pump(true);}
    CHECK(card.clocks==clocks && m.dma_starts==starts && !hw.leased && !m.rx);
    cleanup();
}
static void startup_wire_partial_quiesce(void) {
    setup(true,511u);uint32_t token=data_request(45000u,6u);
    CHECK(pump(false)==KUI_TOY_SCI_PENDING);elapse(120u);
    CHECK(m.rx && m.tcr>0u && m.tcr<513u && !service.completed_bytes);
    hw.in_entry=hw.cancel_allowed=true;
    CHECK(kui_toy_pilot_sci_quiesce());kui_toy_pilot_sci_retire();
    hw.in_entry=hw.cancel_allowed=false;
    CHECK(!hw.leased && !m.rx && !m.flying && !(m.chcr&1u) && active_stops==1u);
    CHECK(hw.vbr==GAME_VBR && hw.iprb==0x5a0fu);
    for(unsigned i=0;i<6u*2048u;i++)CHECK(ram[OUT-BEGIN+i]==0xa5u);
    CHECK(!kui_retail_gd_dispatch(&service,token,0u,0u,KUI_GD_ABORT));
    hw.frozen=true;for(unsigned n=0;n<3u;n++) {elapse(5000u);irq();(void)pump(true);}
    hw.frozen=false;
    CHECK(kui_retail_gd_dispatch(&service,token,STATUS,0u,KUI_GD_CHECK)==KUI_GD_FAILED);
    CHECK(service.completed_bytes==0u);cleanup();
}
int main(void) {
    startup_data_wire(false,0u,false);startup_data_wire(true,511u,false);
    startup_data_wire(true,511u,true);startup_wire_partial_quiesce();
    printf("Startup DATA SCI real-wire integration: %u checks passed; CMD18/CRC/fragmentation/partial fence and retired inactivity\n",checks);
    return 0;
}
