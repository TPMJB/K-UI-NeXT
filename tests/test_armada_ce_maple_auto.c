/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_maple_auto.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    unsigned checks=0;
    /* No checkpoint can fire on an unrelated event, including counters
     * above one. A historical failure always wins over event selection. */
    for(unsigned mode=0;mode<=8u;mode++) for(unsigned mask=0;mask<16u;mask++) {
        uint32_t events[4];
        for(unsigned i=0;i<4u;i++) events[i]=(mask&(1u<<i))?UINT32_MAX:0;
        unsigned expected=mode>=1u && mode<=4u && events[mode-1u]?mode:0u;
        assert(kui_armada_ce_maple_auto_decide(mode,events[0],events[1],events[2],events[3],0)==expected);
        ++checks;
        assert(kui_armada_ce_maple_auto_decide(mode,events[0],events[1],events[2],events[3],1u)==
            KUI_ARMADA_CE_MAPLE_AUTO_FAULT);
        ++checks;
    }
    assert(KUI_ARMADA_CE_MAPLE_AUTO_REASON_ADDRESS==0xacfcff00u+120u);
    assert(KUI_ARMADA_CE_MAPLE_AUTO_PHASE_ADDRESS==KUI_ARMADA_CE_MAPLE_AUTO_REASON_ADDRESS+4u);
    assert(KUI_ARMADA_CE_MAPLE_AUTO_MENU_ADDRESS==KUI_ARMADA_CE_MAPLE_AUTO_PHASE_ADDRESS+4u);
    puts("CE automatic checkpoint: 288 decisions passed; terminal words follow A2 snapshot");
    return checks==288u?0:1;
}
