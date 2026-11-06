/* SPDX-License-Identifier: GPL-3.0-only */
/* Production SCI stream, authored SCI/card boundary. The wire loses data
 * byte145 and corrupts two later raw-header bytes while the card still sends
 * its original CRC. No production function or CRC result is substituted. */
#include "sci_stream_model.h"
static struct kui_loader_sd sd;
static uint8_t areas[2][KUI_SCI_STREAM_AREA_BYTES] __attribute__((aligned(32)));
static bool wire_fault;
static const uint32_t wanted_lba=700u;
static uint8_t original(unsigned i) {
    static const uint8_t header[16]={0,255,255,255,255,255,255,255,255,255,255,0,0,2,0,1};
    return i>=176u && i<192u?header[i-176u]:(uint8_t)(i*13u+(i>>8)*3u+0x5au);
}
static uint8_t faulted_content(uint32_t lba,unsigned i) {
    uint8_t value=original(i);
    /* stream_byte increments frame before asking for a payload byte. Its
     * independent CRC generation occurs at gap+514/gap+515 and sees the
     * unchanged card source. Only the modeled payload wire is corrupted. */
    if(wire_fault && lba==wanted_lba && card.frame<=card.gap+513u) {
        if(i==176u)value^=1u;
        if(i==177u)value^=0x74u;
    }
    return value;
}
static void setup(void) {
    memset(&card,0,sizeof(card));card.nac=1u;card.nac_first=40u;
    card.busy=6u;card.blocks=100000u;card.high_capacity=true;
    memset(&m,0,sizeof(m));m.smr=0x80u;m.scr=0x30u;m.ssr=0x84u;
    m.cs_high=true;m.healthy=true;m.sar=0x11111111u;m.dar=0x22222222u;
    m.tcr=0x33u;m.dmaor=0x8201u;m.late_take=true;
    m.areas[0]=areas[0];m.areas[1]=areas[1];
    memset(&sd,0,sizeof(sd));sd.bus.select=bus_select;sd.bus.transfer=bus_transfer;
    sd.high_capacity=sd.ready=true;sd.blocks=card.blocks;
    card_content=faulted_content;wire_fault=true;
    assert(kui_sci_stream_open(&sd,areas[0],areas[1],false)==KUI_SCI_STREAM_OK);
}
int main(int argc,char **argv) {
    bool legacy=argc==2 && !strcmp(argv[1],"--legacy-unsafe-control");
    assert(argc==1 || legacy);setup();
    uint8_t clean[512];for(unsigned i=0u;i<512u;i++)clean[i]=original(i);
    /* RDR holds144,145 is lost; the late DMA take leaves exactly the same
     * one-based lost marker146 reported by the hardware snapshot. */
    m.overrun_after=144u;
    assert(kui_sci_stream_fetch(wanted_lba,LIMIT,false)==KUI_SCI_STREAM_OK);
    assert(kui_sci_stream_poll(false)==KUI_SCI_STREAM_PENDING);
    assert(kui_sci_stream_wait()==KUI_SCI_STREAM_OK);
    enum kui_sci_stream_result result;
    const uint8_t *block=kui_sci_stream_take(wanted_lba,&result);
    if(legacy) {
        assert(block && result==KUI_SCI_STREAM_OK);
        assert(block[176]==1u && block[177]==0x8bu);
        assert(block[145]==(uint8_t)(original(145u)^0xcbu));
        assert(memcmp(block,clean,sizeof(clean))!=0);
        assert(crc16(block,512u)==crc16(clean,512u));
        printf("{\"legacy_accepted_wrong_payload\":true,\"lost_index\":145,\"header_offset\":176,\"header_actual\":%u,\"header_expected\":0,\"crc16\":%u,\"hardware_executed\":false}\n",block[176],crc16(clean,512u));
        return 0;
    }
    assert(!block && result==KUI_SCI_STREAM_CRC);
    /* The rejected block never becomes the retained successful block. A
     * complete fresh programmed reception supplies all bytes and both CRC
     * bytes; normal take independently checks that untouched transmission. */
    wire_fault=false;unsigned starts=card.cmd18,stops=card.cmd12;
    assert(kui_sci_stream_fetch(wanted_lba,LIMIT,true)==KUI_SCI_STREAM_OK);
    block=kui_sci_stream_take(wanted_lba,&result);
    assert(block && result==KUI_SCI_STREAM_OK && !memcmp(block,clean,512u));
    assert(card.cmd18==starts+1u && card.cmd12==stops+1u);
    assert(kui_sci_stream_stop()==KUI_SCI_STREAM_OK);
    printf("{\"damaged_block_refused\":true,\"fresh_pio_crc_verified\":true,\"hardware_executed\":false}\n");
    return 0;
}
