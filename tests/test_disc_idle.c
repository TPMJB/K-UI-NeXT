/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_idle.h"
#include <assert.h>
#include <limits.h>
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void grace(struct kui_disc_idle *idle,unsigned action,uint64_t now) {
    CHECK(kui_disc_idle_complete(idle,action,true,true,now,false)==KUI_DISC_IDLE_GRACE);
    CHECK(idle->armed && idle->deadline==now+KUI_DISC_IDLE_GRACE_MS);
}
int main(void) {
    struct kui_disc_idle idle={0};
    CHECK(!kui_disc_idle_take_stop(&idle,UINT64_MAX,true,false,false,false));
    grace(&idle,12u,1000u);
    CHECK(!kui_disc_idle_take_stop(&idle,45999u,true,false,false,false));
    CHECK(kui_disc_idle_take_stop(&idle,46000u,true,false,false,false));
    CHECK(!idle.armed && !idle.deadline);
    for(unsigned i=0u;i<100u;i++) CHECK(!kui_disc_idle_take_stop(&idle,50000u+i,true,false,false,false));
    /* A successful104 parks the worker for handoff; failed/cancelled104
     * reaches ordinary immediate cleanup. */
    grace(&idle,12u,1000u);grace(&idle,105u,2000u);
    CHECK(!kui_disc_idle_take_stop(&idle,47000u,false,false,false,false));
    CHECK(idle.armed);
    CHECK(kui_disc_idle_complete(&idle,104u,false,true,47000u,false)==KUI_DISC_IDLE_STOP);
    CHECK(!idle.armed);
    /* VMU-Off review does not invent optical work or extend a prior grace. */
    CHECK(kui_disc_idle_complete(&idle,105u,true,false,50000u,false)==KUI_DISC_IDLE_GRACE);
    CHECK(!idle.armed);
    grace(&idle,12u,1000u);
    for(unsigned i=0u;i<100u;i++) {
        CHECK(kui_disc_idle_complete(&idle,105u,true,false,2000u+i,false)==KUI_DISC_IDLE_GRACE);
        CHECK(idle.deadline==46000u);
    }
    CHECK(kui_disc_idle_take_stop(&idle,46000u,true,false,false,false));
    for(unsigned action=0u;action<=119u;action++) {
        bool cleanup=action==1u || (action>=4u && action<=7u) || action==12u || action==22u ||
            (action>=46u && action<=49u) || action==81u || action==96u || action==97u ||
            (action>=103u && action<=105u) || action==119u;
        CHECK(kui_disc_idle_cleanup(action)==cleanup);
        bool inspect=action==12u || action==103u || action==105u || action==119u;
        CHECK(kui_disc_idle_inspection(action)==inspect);
        for(unsigned passed=0u;passed<2u;passed++) {
            idle=(struct kui_disc_idle){45000u,true};
            enum kui_disc_idle_result result=kui_disc_idle_complete(&idle,action,passed!=0u,true,1000u,false);
            CHECK(result==(!cleanup?KUI_DISC_IDLE_NONE:inspect && passed?KUI_DISC_IDLE_GRACE:KUI_DISC_IDLE_STOP));
            CHECK(idle.armed==(!cleanup || (inspect && passed)));
        }
    }
    for(unsigned reason=1u;reason<8u;reason++) {
        grace(&idle,103u,1000u);
        CHECK(!kui_disc_idle_take_stop(&idle,UINT64_MAX,true,(reason&1u)!=0u,(reason&2u)!=0u,(reason&4u)!=0u));
        CHECK(!idle.armed && !idle.deadline);
    }
    grace(&idle,12u,1000u);
    CHECK(kui_disc_idle_complete(&idle,12u,true,true,1001u,true)==KUI_DISC_IDLE_NONE);
    CHECK(!idle.armed);
    CHECK(kui_disc_idle_complete(&idle,103u,true,true,UINT64_MAX-100u,false)==KUI_DISC_IDLE_GRACE);
    CHECK(idle.armed && idle.deadline==UINT64_MAX);
    CHECK(!kui_disc_idle_take_stop(&idle,UINT64_MAX-1u,true,false,false,false));
    CHECK(kui_disc_idle_take_stop(&idle,UINT64_MAX,true,false,false,false));
    CHECK(!kui_disc_idle_cleanup(UINT_MAX));
    printf("Disc spindle grace: %u checks; finite45s, identity/card/launch sequence, Off no-read, failure cleanup, ownership/media guards and one-shot expiry\n",checks);
    return 0;
}
