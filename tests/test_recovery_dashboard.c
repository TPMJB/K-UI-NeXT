/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/recovery_dashboard.h"
#include <assert.h>
#include <stdio.h>
int main(void) {
    struct kui_salvage_dashboard view={0};
    assert(!view.known && !view.counts_known && !view.blocked);
    assert(!kui_recovery_dashboard_counts(&view,"/KUI/salvage/job-0003",7017,6190,827));
    assert(!kui_recovery_dashboard_guard(&view,"/KUI/salvage/job-0003",false));
    assert(!kui_recovery_dashboard_admit(&view,"/KUI/salvage/job-0000","POWER STONE"));
    assert(!kui_recovery_dashboard_admit(&view,"/KUI/salvage/job-0003/../0004","POWER STONE"));
    assert(kui_recovery_dashboard_admit(&view,"/KUI/salvage/job-0003","POWER STONE"));
    assert(view.known && !view.counts_known && !view.blocked);
    assert(kui_recovery_dashboard_guard(&view,view.job,true));
    assert(kui_recovery_dashboard_counts(&view,view.job,7017,6190,827));
    assert(view.blocked && view.counts_known && view.remaining==827 && view.recovered==6190);
    struct kui_salvage_dashboard prior=view;
    assert(!kui_recovery_dashboard_counts(&view,"/KUI/salvage/job-0004",1,1,0));
    assert(!kui_recovery_dashboard_counts(&view,view.job,10,11,0));
    assert(!kui_recovery_dashboard_counts(&view,view.job,7017,6190,0));
    assert(!kui_recovery_dashboard_guard(&view,"/KUI/salvage/job-0004",false));
    assert(!memcmp(&view,&prior,sizeof(view)));
    assert(kui_recovery_dashboard_admit(&view,view.job,NULL));
    assert(view.blocked && view.counts_known && !strcmp(view.title,"POWER STONE"));
    assert(kui_recovery_dashboard_admit(&view,view.job,view.title));
    assert(view.blocked && view.counts_known && !strcmp(view.title,"POWER STONE"));
    assert(kui_recovery_dashboard_guard(&view,view.job,false) && !view.blocked);
    assert(kui_recovery_dashboard_admit(&view,"/KUI/salvage/job-0005",view.title+6));
    assert(!strcmp(view.title,"STONE") && !view.counts_known && !view.blocked);
    assert(kui_recovery_dashboard_admit(&view,"/KUI/salvage/job-0004",NULL));
    assert(view.known && !view.counts_known && !view.blocked && !view.title[0]);
    assert(kui_recovery_dashboard_counts(&view,view.job,0,0,0) && view.counts_known);
    kui_recovery_dashboard_clear(&view);
    assert(!view.known && !view.counts_known && !view.blocked && !view.job[0] && !view.title[0]);
    puts("PASS recovery dashboard: unknown, admitted, counts, wrong job, guard, invalidation");
    return 0;
}
