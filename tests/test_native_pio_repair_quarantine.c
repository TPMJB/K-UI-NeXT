/* SPDX-License-Identifier: GPL-3.0-only */
/* Production native PIO39 reader, cursor and SCI. The existing fixture owns
 * CPU mappings, card bytes and peripheral timing; no retail bytes are used. */
#define KUI_RETAIL_FOREGROUND_FIXTURE_ONLY 1
#include "test_retail_async_foreground.c"

static uint32_t damaged_lba;
static unsigned damaged_attempts,bad_cursor_feeds,good_cursor_feeds,max_polled;
static bool damage_this_reception;

/* Observe the real cursor admission, without replacing its result or writes. */
enum kui_game_result __real_kui_retail_cursor_feed(struct kui_retail_cursor *,const uint8_t[512]);
enum kui_game_result __wrap_kui_retail_cursor_feed(struct kui_retail_cursor *cursor,const uint8_t block[512]) {
    if(cursor->block==damaged_lba) {
        if(block[176u]==1u && block[177u]==0x8bu)++bad_cursor_feeds;
        else {CHECK(!block[176u] && block[177u]==255u);++good_cursor_feeds;}
    }
    return __real_kui_retail_cursor_feed(cursor,block);
}
static void inject_lost_byte(void) {
    if(card.lba!=damaged_lba) {damage_this_reception=false;return;}
    /* A resumed DMA starts at146, not at the receive area's beginning. */
    if(!(m.dar&0xfffu)) {
        damage_this_reception=damaged_attempts<2u;
        if(damage_this_reception) {++damaged_attempts;m.overrun_after=144u;}
    }
}
static uint8_t faulted_wire(uint32_t lba,unsigned inside) {
    uint8_t value=content(lba,inside);
    /* receive() clears its host rx flag before clocking the modeled DMA;
     * SCR0x50 still distinguishes those clocks from programmed transfers.
     * CRC generation happens after the payload frame and sees clean bytes. */
    if(damage_this_reception && lba==damaged_lba && m.scr==0x50u &&
       card.frame<=card.gap+513u) {
        if(inside==176u)value^=1u;
        if(inside==177u)value^=0x74u;
    }
    return value;
}
static int32_t observed_gd(uint32_t function,uint32_t arg0,uint32_t arg1) {
    unsigned before=R.shared.stream.stats.polled;
    int32_t result=gd(function,arg0,arg1);
    unsigned polled=R.shared.stream.stats.polled-before;
    if(polled>max_polled)max_polled=polled;
    CHECK(polled<=1u);
    return result;
}
int main(int argc,char **argv) {
    bool legacy=argc==2 && !strcmp(argv[1],"--legacy-unsafe-control");
    CHECK(argc==1 || legacy);
    setup_pio(false,false);
    /* At logical byte69030 the request is at sector210 +48. Starting at
     *45022 makes the next raw sector233's header fall at card-byte176. */
    const uint32_t first_lba=45022u,next_sector=233u;
    uint32_t file_byte=next_sector*2352u,file_block=file_byte/512u;
    CHECK(file_byte%512u==176u);
    const struct kui_retail_track *track=&manifest.slots[2].track;
    bool found=false;
    for(unsigned i=0u;i<track->extent_count;i++) {
        const struct kui_retail_extent *extent=&manifest.slots[track->first_extent+i].extent;
        if(file_block>=extent->file_block && file_block-extent->file_block<extent->blocks) {
            damaged_lba=extent->card_lba+file_block-extent->file_block;found=true;break;
        }
    }
    CHECK(found && !content(damaged_lba,176u) && content(damaged_lba,177u)==255u);
    m.on_dma_start=inject_lost_byte;card_content=faulted_wire;
    int32_t token=pio_request(first_lba,257u);
    uint32_t done=0u;
    for(unsigned piece=0u;piece<107u;piece++) {
        uint32_t bytes=piece<105u?4096u:piece==105u?48u:4096u;
        uint32_t destination=pio_bases[1]+0x10000u;
        pio_put(PIO_PARAM,destination);pio_put(PIO_PARAM+4u,bytes);
        CHECK(observed_gd(KUI_GD_PIO_TRANSFER,(uint32_t)token,PIO_PARAM)==0);
        if(R.shared.service.error || R.engine.failed) {
            CHECK(legacy && piece==106u && done==0x69030u &&
                  bad_cursor_feeds==1u && damaged_attempts==1u &&
                  R.shared.service.error==KUI_GD_ERROR_IO &&
                  R.engine.cursor.offset==0u);
            printf("{\"legacy_malformed_header_fed\":true,\"piece_begin\":%u,\"damaged_dma_attempts\":%u,\"bad_cursor_feeds\":%u,\"hardware_executed\":false}\n",
                done,damaged_attempts,bad_cursor_feeds);
            return 0;
        }
        CHECK(!legacy || piece<106u);
        CHECK(R.shared.service.completed_bytes==done+bytes);
        const uint8_t *actual=pio_pointer(destination,bytes);
        for(uint32_t i=0u;i<bytes;i++) {
            uint32_t logical=done+i,sector=logical/2048u,inside=logical%2048u;
            CHECK(actual[i]==source(2u,(first_lba-45000u+sector)*2352u+16u+inside));
        }
        done+=bytes;
        CHECK(observed_gd(KUI_GD_EXEC,0u,0u)==0);R.shared.service.callback_due=0u;
    }
    CHECK(!legacy && done==0x6a030u && damaged_attempts==2u &&
          !bad_cursor_feeds && good_cursor_feeds && max_polled==1u);
    printf("{\"damaged_header_quarantined\":true,\"automatic_retry_completed\":true,\"damaged_dma_attempts\":%u,\"bad_cursor_feeds\":%u,\"clean_cursor_feeds\":%u,\"completed_bytes\":%u,\"max_pio_blocks_per_gd\":%u,\"hardware_executed\":false}\n",
        damaged_attempts,bad_cursor_feeds,good_cursor_feeds,done,max_polled);
    return 0;
}
