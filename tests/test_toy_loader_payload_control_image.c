/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the independent SD protocol/card mock and CRC polynomial division.
 * Real image cache/copy and real SD CMD18/token/CRC/stop code remain linked.
 */
#define main sd_protocol_fixture_main
#include "test_loader_sd.c"
#undef main
#include "kui/toy_loader_payload_control.h"
#include "kui/retail_image.h"

static struct kui_retail_manifest control_manifest;
static struct kui_retail_image control_image;
static struct {
    struct kui_loader_sd card;
    struct kui_loader_sd_stream stream;
} control_storage;
static _Alignas(32) uint8_t control_cached[512];
static uint8_t control_output[2048];
static unsigned control_purges;
static uint8_t cleanup_packet[6];
static unsigned cleanup_packet_bytes;

uint32_t kui_toy_loader_payload_control_host_ccr(void) {return UINT32_C(0x101);}
uint8_t *kui_toy_loader_payload_control_host_cached(uint8_t *rx) {
    CHECK(rx==control_image.block);return control_cached;
}
void kui_toy_loader_payload_control_host_purge(uint8_t *cached,uint32_t bytes) {
    CHECK(cached==control_cached && bytes==512u);
    if(!(control_purges&1u)) memcpy(control_cached,control_image.block,512u);
    else memcpy(control_image.block,control_cached,512u);
    ++control_purges;
}
static bool controlled_block(void *context,const uint8_t *tx,uint8_t *rx,
    size_t count,bool slow,uint16_t *crc) {
    return kui_toy_loader_payload_call(transfer_block,context,tx,rx,count,slow,crc);
}
static uint8_t controlled_transfer(void *context,uint8_t data,bool slow) {
    struct mock *m=context;
    if(m->block_fault && data!=0xffu) {
        CHECK(cleanup_packet_bytes<sizeof(cleanup_packet));
        cleanup_packet[cleanup_packet_bytes++]=data;
    }
    return transfer(context,data,slow);
}
static int controlled_run(void *context,uint32_t lba,uint32_t available,uint8_t output[512]) {
    CHECK(context==&control_storage && output==control_image.block);
    if(!control_storage.stream.active) {
        if(kui_loader_sd_stream_start(&control_storage.card,&control_storage.stream,lba,available)!=KUI_LOADER_SD_OK)
            return -1;
    } else CHECK(control_storage.stream.next_lba==lba);
    return kui_loader_sd_stream_next(&control_storage.card,&control_storage.stream,output)==KUI_LOADER_SD_OK?0:-1;
}
static int controlled_single(void *context,uint32_t lba,uint8_t output[512]) {
    return controlled_run(context,lba,1u,output);
}
static void controlled_fixture(struct mock *m) {
    memset(&control_manifest,0,sizeof(control_manifest));
    memset(&control_storage,0,sizeof(control_storage));
    control_manifest.card_sectors=8192u;
    control_manifest.partition_start=50u;control_manifest.partition_end=8000u;
    control_manifest.track_count=1u;control_manifest.extent_count=1u;
    control_manifest.session_lba=control_manifest.boot_lba=45000u;
    control_manifest.boot_bytes=2048u;
    strcpy(control_manifest.title,"Payload ownership integration");
    strcpy(control_manifest.product,"KUITEST");
    strcpy(control_manifest.bootfile,"1ST_READ.BIN");
    strcpy(control_manifest.region,"JUE");
    control_manifest.slots[0].track=(struct kui_retail_track){
        .start_lba=45000u,.end_lba=45001u,.first_extent=1u,
        .extent_count=1u,.control=4u|KUI_RETAIL_TRACK_COOKED};
    control_manifest.slots[1].extent=(struct kui_retail_extent){.file_block=0u,.card_lba=100u,.blocks=4u};
    CHECK(kui_retail_image_init(&control_image,&control_manifest,controlled_single,&control_storage)==KUI_GAME_OK);
    control_image.read_run=controlled_run;
    kui_toy_loader_payload_control_host_bind(control_image.block);
    use_blocks=true;
    struct kui_loader_sd_bus b=bus(m);b.transfer_block=controlled_block;
    b.transfer=controlled_transfer;
    CHECK(kui_loader_sd_init_bus(&control_storage.card,&b)==KUI_LOADER_SD_OK);
    /* The 16-byte init CSD callback must remain unchanged and decline. */
    CHECK(m->block_calls==1u);
    m->block_calls=m->block_bytes=0u;
    kui_toy_loader_payload_control_host_reset();control_purges=cleanup_packet_bytes=0u;
    memset(control_image.block,0xa5,sizeof(control_image.block));
    memset(control_cached,0xc3,sizeof(control_cached));
    memset(control_output,0x6b,sizeof(control_output));
}
static void controlled_case(unsigned fault) {
    struct mock m={.version2=true,.high_capacity=true};
    controlled_fixture(&m);
    if(fault==1u) {m.fail_block_call=2u;m.fail_block_after=97u;}
    if(fault==2u) m.bad_crc_call=2u;
    enum kui_game_result result=kui_retail_image_read(&control_image,45000u,1u,
        KUI_GAME_SECTOR_MODE1,control_output,sizeof(control_output));
    CHECK(result==(fault?KUI_GAME_IO:KUI_GAME_OK));
    unsigned delivered=fault?512u:2048u;
    for(unsigned i=0;i<sizeof(control_output);++i)
        CHECK(control_output[i]==(i<delivered?pattern(100u+i/512u,i%512u):0x6b));
    CHECK(control_image.blocks_read==(fault?1u:4u));
    CHECK(control_image.cache_valid==!fault);
    CHECK(m.multi_commands==1u && !m.selected);
    /* A latched payload bus fault rejects cleanup traffic; the bounded
     * CMD12 attempt still happens, and the card is quarantined. */
    CHECK(m.stops==(fault==1u?0u:1u));
    if(fault==1u) {
        CHECK(cleanup_packet_bytes==6u && cleanup_packet[0]==0x4cu);
        for(unsigned i=1u;i<5u;++i) CHECK(!cleanup_packet[i]);
        CHECK(cleanup_packet[5]==reference_crc7(cleanup_packet,5u));
    }
    CHECK(!control_storage.stream.active && !control_storage.stream.remaining);
    CHECK(m.block_calls==(fault?2u:4u));
    if(fault==1u) CHECK(!control_storage.card.ready && m.block_fault);
    const struct kui_toy_loader_payload_control_counts *c=kui_toy_loader_payload_control_counts();
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 1
    CHECK(c->attempts==m.block_calls && !c->declines);
    CHECK(c->publications==m.block_calls && control_purges==m.block_calls*2u);
    CHECK(c->failed==(fault==1u));
#elif KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 2
    CHECK(c->attempts==m.block_calls && !c->declines && !control_purges);
    CHECK(c->publications==m.block_calls-(fault==1u) && c->failed==(fault==1u));
#else
    CHECK(!c->attempts && !c->declines && !c->publications && !c->failed && !control_purges);
#endif
}
int main(void) {
    controlled_case(0u);controlled_case(1u);controlled_case(2u);
    printf("Payload mode %u real image/SD CMD18/CRC/cleanup PASS\n",KUI_TOY_PILOT_DATA_PAYLOAD_MODE);
    return 0;
}
