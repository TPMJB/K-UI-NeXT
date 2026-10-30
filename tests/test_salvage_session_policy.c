/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/salvage_session_policy.h"
#include <assert.h>
#include <stdio.h>

/* Model the worker's two invalidation sites: rejected foreground actions and
 * newly observed storage errors revoke its parked optimization epoch. The
 * actual core receipt still has independent admission checks. */
static void foreground(struct kui_salvage_session_policy *s,unsigned action,bool ordinary) {
    if(!kui_salvage_session_policy_action(action,ordinary))
        kui_salvage_session_policy_forget(s);
}
static void storage(struct kui_salvage_session_policy *s,uint32_t errors) {
    if(kui_salvage_session_policy_errors(s,errors))
        kui_salvage_session_policy_forget(s);
}
static void same_session_sequence(void) {
    struct kui_salvage_session_policy s={.epoch=17};
    storage(&s,4); /* Earlier failures are not new failures after admission. */
    s.parked=true; /* The core has just sealed a clean Stop receipt. */
    foreground(&s,0,false);
    foreground(&s,3,false); /* Automatic or explicit diagnostic report. */
    storage(&s,4);
    foreground(&s,12,false); /* Ordinary insertion identity observation. */
    foreground(&s,47,true); /* Continue, with no defer/trial/diagnostic. */
    assert(s.epoch==17 && s.parked && s.observed_errors && s.errors==4);

    /* Running another diagnostic is not the same as writing its report. */
    foreground(&s,48,false);
    assert(s.epoch==18 && !s.parked);
    s.parked=true;
    foreground(&s,47,false); /* A deferred-range request is not ordinary Continue. */
    assert(s.epoch==19 && !s.parked);
}
static void foreground_allowlist(void) {
    for(unsigned action=0;action<=101;++action) {
        for(unsigned ordinary=0;ordinary<2;++ordinary) {
            const bool retain=action==0 || action==3 || action==12 || (action==47 && ordinary);
            struct kui_salvage_session_policy s={.epoch=81,.parked=true};
            foreground(&s,action,ordinary!=0);
            assert(s.epoch==(retain?81u:82u));
            assert(s.parked==retain);
        }
    }
    struct kui_salvage_session_policy s={.epoch=9,.parked=true};
    foreground(&s,UINT32_MAX,true);
    assert(s.epoch==10 && !s.parked);
}
static void storage_failure_sequence(void) {
    struct kui_salvage_session_policy s={.epoch=1};
    storage(&s,0);
    assert(s.epoch==1 && s.observed_errors && s.errors==0);
    s.parked=true;
    storage(&s,0);
    assert(s.parked && s.epoch==1);
    storage(&s,1); /* Read, write, sync, init, or report I/O failure. */
    assert(!s.parked && s.epoch==2 && s.errors==1);
    s.parked=true;
    storage(&s,1);
    assert(s.parked && s.epoch==2);
    storage(&s,0); /* A counter reset cannot hide a preceding failure. */
    assert(!s.parked && s.epoch==3 && s.errors==0);

    s=(struct kui_salvage_session_policy){.epoch=20,.parked=true};
    storage(&s,UINT32_MAX);
    assert(!s.parked && s.epoch==21 && s.errors==UINT32_MAX);
    s.parked=true;
    storage(&s,UINT32_MAX); /* Saturation cannot prove absence of new errors. */
    assert(!s.parked && s.epoch==22);
}
static void epoch_wrap(void) {
    struct kui_salvage_session_policy s={.epoch=UINT64_MAX,.parked=true,
        .errors=6,.observed_errors=true};
    kui_salvage_session_policy_forget(&s);
    assert(s.epoch==1 && !s.parked && s.errors==6 && s.observed_errors);
    kui_salvage_session_policy_forget(&s);
    assert(s.epoch==2 && !s.parked);
    s=(struct kui_salvage_session_policy){.parked=true};
    kui_salvage_session_policy_forget(&s);
    assert(s.epoch==1 && !s.parked);
}
int main(void) {
    same_session_sequence();foreground_allowlist();storage_failure_sequence();epoch_wrap();
    puts("PASS salvage session policy: action isolation, clean Continue, error/reset/saturation, epoch wrap");
    return 0;
}
