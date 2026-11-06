/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the production lookup against an independent complete-table
 * reference. No result, FAT word or target is omitted from the scan. */
#define KUI_VMU_FS_TARGET_INDEX_TEST 1
#include "../src/core/vmu_fs_admission.c"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static struct vmu_fs_state tested;
static uint64_t reference_probes,checks;
static int reference(const struct vmu_fs_state *s,uint32_t cluster) {
    unsigned begin=0u,end=s->target_count;
    while(begin<end) {
        ++reference_probes;unsigned middle=begin+(end-begin)/2u;
        if(s->targets[middle].cluster==cluster)return (int)middle;
        if(s->targets[middle].cluster<cluster)begin=middle+1u;else end=middle;
    }
    return -1;
}
static void equal(uint32_t cluster) {
    ++checks;assert(vmu_fs_target_index(&tested,cluster)==reference(&tested,cluster));
}
static void populate(unsigned count,uint32_t first,uint32_t step) {
    memset(&tested,0,sizeof(tested));tested.target_count=count;
    for(unsigned i=0;i<count;i++) {
        uint32_t c=first+i*step;tested.targets[i].cluster=c;
        tested.target_filter[(c>>5u)&(VMU_FS_TARGET_FILTER_WORDS-1u)]|=UINT32_C(1)<<(c&31u);
    }
}
static void adversarial(void) {
    populate(0u,0u,0u);equal(0u);equal(UINT32_MAX);
    for(unsigned count=1u;count<=VMU_FS_TARGETS;count++) {
        populate(count,2u,4099u);
        for(unsigned i=0;i<count;i++) {
            uint32_t c=tested.targets[i].cluster;
            equal(c);equal(c-1u);equal(c+1u);
            /* Exact filter collisions must still fail the sorted lookup. */
            equal(c^2048u);equal(c+4096u);
        }
        equal(0u);equal(UINT32_MAX);equal(UINT32_C(0x0fffffff));
    }
    populate(VMU_FS_TARGETS,2u,1u);
    for(uint32_t c=0u;c<65536u;c++)equal(c);
    populate(1u,UINT32_MAX,0u);equal(UINT32_MAX);equal(0u);equal(UINT32_MAX-1u);
    populate(1u,0u,0u);equal(0u);equal(2048u);equal(UINT32_MAX);
    uint32_t random=UINT32_C(0x5b615e31);
    populate(VMU_FS_TARGETS,2u,16129u);
    for(unsigned i=0;i<100000u;i++) {random=random*1664525u+1013904223u;equal(random);}
}
static void full_scan(const char *name,uint32_t step) {
    populate(VMU_FS_TARGETS,2u,step);
    reference_probes=vmu_fs_target_index_test_probes=0u;
    for(uint32_t c=2u;c<KUI_VMU_FS_MAX_FAT_ENTRIES;c++)equal(c);
    printf("%s: queries=%u reference_comparisons=%llu exact_comparisons=%llu\n",name,
        KUI_VMU_FS_MAX_FAT_ENTRIES-2u,(unsigned long long)reference_probes,
        (unsigned long long)vmu_fs_target_index_test_probes);
    assert(vmu_fs_target_index_test_probes<reference_probes/4u);
}
int main(void) {
    adversarial();full_scan("contiguous targets",1u);full_scan("scattered targets",16129u);
    printf("PASS production target-filter equivalence: %llu checks; every candidate still uses exact lookup\n",
        (unsigned long long)checks);
    return 0;
}
