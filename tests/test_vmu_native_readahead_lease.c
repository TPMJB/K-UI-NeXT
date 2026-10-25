/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the independently authored allocator image/cell fixtures and run the
 * existing baseline suite as well as the selected reservation contracts. */
#define main kui_heap_baseline_tests
#include "test_vmu_native_heap.c"
#undef main
#include "kui/vmu_native_background.h"
#include "kui/vmu_native_stage_placement.h"
#include "kui/vmu_maple_observe_boot.h"
#include "vmu_native_background_guards.h"

int main(void) {
    CHECK(kui_heap_baseline_tests(1,NULL)==0);
    const uint32_t areas[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    const uint32_t lease_bytes=KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES;
    const bool expanded=KUI_NATIVE_READAHEAD || KUI_NATIVE_DUAL_READERS;
    CHECK(lease_bytes==(expanded?UINT32_C(0x60000):UINT32_C(0x30000)));
    CHECK(KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES==UINT32_C(0x20000));
    CHECK(KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET==UINT32_C(0x20400));
    CHECK(KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES==UINT32_C(0x760));
    for(unsigned a=0;a<3u;a++) {
        struct kui_vmu_native_heap h=fixture(0x800u,areas[a],true);
        uint32_t begin=UINT32_C(0x0c500000)|areas[a];
        struct kui_vmu_native_heap_lease lease;
        live(&h,begin);
        CHECK(kui_vmu_native_heap_fresh(&h,read_word,NULL,begin,0x80000u,1u,lease_bytes,&lease));
        CHECK(lease.payload==begin+0x80000u-lease_bytes);
        CHECK(lease.header==lease.payload-32u && lease.units==lease_bytes/32u+1u);
        CHECK(lease.remaining==0x4000u-lease.units);
        CHECK(kui_vmu_native_heap_preallocate(&lease,read_word,NULL,1u));
        allocated(&lease);
        CHECK(kui_vmu_native_heap_postallocate(&lease,read_word,NULL,1u,lease.payload));
        CHECK(kui_vmu_native_heap_lease_valid(&lease,read_word,NULL,1u));
        CHECK(kui_vmu_native_heap_lease_identity_valid(&lease,read_word,NULL,1u));
        lease.lease_bytes=expanded?0x30000u:0x60000u;
        CHECK(!kui_vmu_native_heap_lease_valid(&lease,read_word,NULL,1u));
    }
    /* Exercise the actual low destination/entry guards, including the full
     * newly owned leading/tail ranges and all three guest address aliases. */
    const uint32_t payloads[]={UINT32_C(0x0c010020),UINT32_C(0x0c700000),
        UINT32_C(0x0d000000)-KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES};
    for(unsigned p=0;p<3u;p++) {
        uint32_t payload=payloads[p],begin,end;
        struct kui_vmu_observe_boot_control c={.status=KUI_VMU_OBS_BOOT_INSTALLED,
            .lease=payload,.worker_end=(payload|UINT32_C(0xa0000000))+UINT32_C(0x20000),
            .terminal=(payload|UINT32_C(0x80000000))+256u,
            .sd_transaction=(payload|UINT32_C(0x80000000))+512u};
        CHECK(native_resident_lease(&c,&begin,&end));
        CHECK(begin==payload-32u && end==payload+lease_bytes);
        CHECK(native_resident_terminal(&c));
        CHECK(native_resident_transaction(&c,c.sd_transaction));
        for(unsigned a=0;a<3u;a++) {
            CHECK(native_resident_destination(&c,(begin-1u)|areas[a],1u));
            CHECK(!native_resident_destination(&c,(begin-1u)|areas[a],2u));
            for(uint32_t offset=0;offset<lease_bytes+32u;offset+=32u)
                CHECK(!native_resident_destination(&c,(begin+offset)|areas[a],1u));
            if(end<UINT32_C(0x0d000000))CHECK(native_resident_destination(&c,end|areas[a],1u));
        }
        c.lease=UINT32_C(0x0d000000)-lease_bytes+32u;
        CHECK(!native_resident_lease(&c,&begin,&end));
        CHECK(!native_resident_destination(&c,UINT32_C(0x8c200000),1u));
    }
    /* The allocation proof must exclude the complete selected chunk and its
     * header from the still-live installer. The expanded profile's leading
     * 192 KiB is the regression: the old tail-only proof would admit it. */
    struct kui_vmu_native_heap h=fixture(0x800u,0u,true);
    uint32_t tail=UINT32_C(0x0d000000)-lease_bytes-32u;
    const uint32_t boundaries[]={tail,UINT32_C(0x0d000000)-0x30020u};
    for(unsigned b=0;b<2u;b++)for(int delta=-64;delta<=64;delta++)
        for(unsigned a=0;a<3u;a++)for(unsigned aa=0;aa<3u;aa++) {
            uint32_t at=boundaries[b]+(uint32_t)delta;
            bool expected=at<tail;
            CHECK(kui_vmu_native_stage_allocation_safe(&h,0x100000u,at|areas[a],
                (at+1u)|areas[a],UINT32_C(0x0cc00000)|areas[aa],0x400000u)==expected);
        }
    CHECK(kui_vmu_native_stage_allocation_safe(&h,0x100000u,UINT32_C(0x8ce00000),
        UINT32_C(0x8ce40000),UINT32_C(0x8cc00000),0x400000u));
    printf("Selected native lease %u KiB: %u cumulative allocator/guard/placement checks passed\n",
        lease_bytes/1024u,checks);
    return 0;
}
