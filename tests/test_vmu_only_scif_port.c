/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the MMIO/init model, substituting one dropped restoration write. */
#define main original_retail_sd_suite
#define kui_retail_sd_test_write16 modeled_write16
#include "test_retail_sd.c"
#undef kui_retail_sd_test_write16
#undef main

static bool drop_restore;
void kui_retail_sd_test_write16(uint32_t address,uint16_t value) {
    modeled_write16(address,value);
    if(drop_restore && address==SCR && value==3u) fake.scr=2u;
}
int main(void) {
    struct kui_loader_sd card={0},adopted={0};
    reset();assert(kui_retail_sd_init(&card)==KUI_LOADER_SD_OK);
    assert(!kui_retail_sd_faulted());
    assert(kui_retail_sd_acquire()==KUI_LOADER_SD_OK && kui_retail_sd_healthy());
    drop_restore=true;kui_retail_sd_release();
    assert(kui_retail_sd_faulted() && !kui_retail_sd_healthy());
    unsigned writes=fake.count,inits=fake.init_calls;
    for(unsigned i=0u;i<4u;i++) {
        /* Neither an ordinary GD acquisition nor adopting a prepared card
         * can erase the fault left by a VMU/GD restoration failure. */
        assert(kui_retail_sd_acquire()==KUI_LOADER_SD_NOT_READY);
        assert(kui_retail_sd_adopt(&adopted,&card)==KUI_LOADER_SD_NOT_READY);
        assert(kui_retail_sd_init(NULL)==KUI_LOADER_SD_ARGUMENT);
        kui_retail_sd_release();
        assert(kui_retail_sd_faulted() && fake.count==writes && fake.init_calls==inits);
    }
    /* Explicit initialization is a new protocol session, not an implicit
     * retry by the game reader or the VMU loan. It still refuses active I/O. */
    drop_restore=false;fake.scr=3u;
    assert(kui_retail_sd_init(&card)==KUI_LOADER_SD_OK);
    assert(!kui_retail_sd_faulted() && fake.init_calls==inits+1u);
    assert(kui_retail_sd_acquire()==KUI_LOADER_SD_OK);
    writes=fake.count;inits=fake.init_calls;
    assert(kui_retail_sd_init(&card)==KUI_LOADER_SD_NOT_READY);
    assert(fake.count==writes && fake.init_calls==inits);
    kui_retail_sd_release();assert(!kui_retail_sd_faulted());
    puts("VMU-only SCIF: restoration fault survives acquire/adopt; only explicit idle initialization resets the session");
    return 0;
}
