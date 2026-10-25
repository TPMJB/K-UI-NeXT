/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_native_background.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    _Static_assert(KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES<=
        KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES-KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES,
        "Background map fits the existing allocation tail");
    const uint32_t aliases[]={0u,UINT32_C(0x80000000),UINT32_C(0xa0000000)};
    uint32_t leases=0;
    const uint32_t last_payload=UINT32_C(0x0d000000)-KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES;
    for(uint32_t physical=UINT32_C(0x0c010020);physical<=last_payload;physical+=32u) {
        uint32_t owned=(physical|UINT32_C(0x80000000))+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES;
        uint32_t region=(physical|UINT32_C(0x80000000))+KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET;
        for(unsigned a=0;a<3u;a++) {
            uint32_t lease=physical|aliases[a];
            assert(kui_vmu_native_background_map_address(lease,owned));
            assert(!kui_vmu_native_background_map_address(lease,owned-32u));
            assert(!kui_vmu_native_background_map_address(lease,owned+32u));
            assert(!kui_vmu_native_background_map_address(lease,owned+UINT32_C(0x20000000)));
            assert(!kui_vmu_native_background_map_address(lease,owned&UINT32_C(0x1fffffff)));
            assert(!kui_vmu_native_background_map_address(lease+4u,owned));
            assert(kui_vmu_native_background_region_address(lease,region));
            assert(!kui_vmu_native_background_region_address(lease,region-32u));
            assert(!kui_vmu_native_background_region_address(lease,region+32u));
            assert(!kui_vmu_native_background_region_address(lease,region+UINT32_C(0x20000000)));
        }
        assert(owned+KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES<=
            (physical|UINT32_C(0x80000000))+KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES);
        assert(region+KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES<=
            (physical|UINT32_C(0x80000000))+KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES);
        ++leases;
    }
    for(uint32_t area=0;area<8u;area++) {
        if(area==0u || area==4u || area==5u) continue;
        assert(!kui_vmu_native_background_map_address((area<<29)|UINT32_C(0x0c100000),
            UINT32_C(0x8c120000)));
    }
    const uint32_t invalid[]={0u,UINT32_C(0x0c000000),UINT32_C(0x0c010000),
        UINT32_C(0x0d000000)-KUI_VMU_NATIVE_BACKGROUND_LEASE_BYTES+32u,
        UINT32_C(0x0d000000),UINT32_C(0xffffffe0)};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)
        assert(!kui_vmu_native_background_map_address(invalid[i],
            (invalid[i]&UINT32_C(0x1fffffff))+UINT32_C(0x80020000)));
    uint32_t first=UINT32_C(0x8ce00000),last=UINT32_C(0x8cfe0000)-KUI_VMU_NATIVE_BACKGROUND_MAP_BYTES;
    assert(kui_vmu_native_background_stage_map_address(first));
    assert(kui_vmu_native_background_stage_map_address(last));
    assert(!kui_vmu_native_background_stage_map_address(first-4u));
    assert(!kui_vmu_native_background_stage_map_address(first+1u));
    assert(!kui_vmu_native_background_stage_map_address(last+4u));
    assert(!kui_vmu_native_background_stage_map_address(first+UINT32_C(0x20000000)));
    uint32_t last_region=(UINT32_C(0x8cfe0000)-KUI_VMU_NATIVE_BACKGROUND_REGION_BYTES)&~31u;
    assert(kui_vmu_native_background_stage_region_address(first));
    assert(kui_vmu_native_background_stage_region_address(last_region));
    assert(!kui_vmu_native_background_stage_region_address(first-32u));
    assert(!kui_vmu_native_background_stage_region_address(first+4u));
    assert(!kui_vmu_native_background_stage_region_address(last_region+32u));
    assert(!kui_vmu_native_background_stage_region_address(first+UINT32_C(0x20000000)));
    printf("Native Background map geometry: %u aligned leases and all aliases checked\n",leases);
    return 0;
}
