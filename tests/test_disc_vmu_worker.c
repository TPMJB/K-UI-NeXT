/* SPDX-License-Identifier: GPL-3.0-only */
#define KUI_SPLIT_CONTROLLER_TEST_MAIN disc_split_regression
#include "test_vmu_native_split_controller.c"

_Static_assert(KUI_DISC_VMU_WORKER_BYTES==0x12000u,"compact worker retained");
_Static_assert(KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES==0x14000u,"one80KiB lease");

static void callback_tail_boundaries(void) {
    const uint32_t invalid[]={0x8c004100u,0xac712100u,0x0c712100u,0x8ce04000u,
        0x8c712000u,0x8c71203eu,0x8c714000u,0x8c712101u,0x8c711ffeu,0x8c712000u-0x12000u};
    for(unsigned slot=0;slot<3u;slot++) for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        split_prepare();
        uint32_t *targets[]={&input.sd_lease,&input.low_terminal,&input.low_heap_hook};
        *targets[slot]=invalid[i];
        CHECK(kui_vmu_native_passive_arm(&input,NULL)<0 && !state.armed);
        CHECK(!lease_calls && !block_callbacks && !sd.bytes && !sd.writes);
    }
    /* The complete support tail, including its header, stays protected from
     * every guest free alias just like the worker and its allocator header. */
    for(unsigned alias=0;alias<3u;alias++) {
        static const uint32_t areas[]={0u,0x80000000u,0xa0000000u};
        split_setup();
        frame.r4=(physical(config.lease.payload)+KUI_DISC_VMU_SUPPORT_OFFSET)|areas[alias];
        if(setjmp(terminal)==0) {kui_vmu_native_readonly_release_check(&frame);CHECK(false);}
        CHECK(terminal_error==0x30u && !lease_calls && !sd.bytes && !sd.writes);
    }
}
int main(void) {
    int result=disc_split_regression();CHECK(!result);
    callback_tail_boundaries();
    printf("Physical-disc worker: %u checks; virtual %s, 72KiB worker+8KiB owned callbacks, durable saves, physical polling and no BIOS callback authority\n",
        checks,KUI_VMU_NATIVE_VIRTUAL_NAME);
    return 0;
}
