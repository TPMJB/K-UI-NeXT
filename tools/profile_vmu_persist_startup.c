/* SPDX-License-Identifier: GPL-3.0-only */
/* Developer-only I/O counts from the existing synthetic persistence fixture.
 * No device, uploaded save, or physical-card file is opened. */
#define KUI_VMU_PERSIST_STAGE_MAIN profile_unused_stage_tests
#include "../tests/test_vmu_persist_stage.c"
static unsigned phase_reads[8];
static int profile_read(void *context,uint64_t sector,void *out) {
    assert(card.work.phase<8u);++phase_reads[card.work.phase];
    return stage_read(context,sector,out);
}
static void profile_prepare(const char *name) {
    memset(phase_reads,0,sizeof(phase_reads));
    struct kui_vmu_persist_session_ops ops=stage_ops();ops.read_sector=profile_read;
    assert(!kui_vmu_persist_stage_prepare(&card.expected,&card.game,&ops,&card.work,&card.handoff));
    printf("%s: sector_reads=%u identities=%u writes=%u syncs=%u fs_reads=%u fat_steps=%u directory_sectors=%u\n",
        name,card.reads,card.identities,card.writes,card.syncs,card.work.filesystem.stats.reads,
        card.work.filesystem.stats.fat_entries,card.work.filesystem.stats.directory_sectors);
    printf("  recover=%u checkpoint=%u fresh_snapshot=%u final_marker=%u\n",
        phase_reads[KUI_VMU_PERSIST_STAGE_RECOVER],phase_reads[KUI_VMU_PERSIST_STAGE_CHECKPOINT],
        phase_reads[KUI_VMU_PERSIST_STAGE_SNAPSHOT],phase_reads[KUI_VMU_PERSIST_STAGE_ZERO_MARKER]);
}
int main(void) {
    stage_fresh();profile_prepare("clean");
    stage_fresh();stage_seed_real_marker();profile_prepare("pending recovery");
    stage_imported_pair_fresh();card.reads=card.identities=0u;profile_prepare("imported opaque root");
    return 0;
}
