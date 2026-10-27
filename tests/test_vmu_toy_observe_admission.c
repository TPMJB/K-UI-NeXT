/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_toy_observe_admission.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_retail_manifest profile(void) {
    const uint32_t starts[15]={0,7257,45000,201900,219712,234091,255599,
        272017,282799,303092,323352,341314,356690,374201,377422};
    const uint32_t ends[15]={7107,7753,201750,219712,234091,255599,
        272017,282799,303092,323352,341314,356690,374201,377272,549150};
    struct kui_retail_manifest m;
    memset(&m,0,sizeof(m));
    m.gdi_crc32=0xe25531d1u;m.track_count=15u;m.extent_count=15u;
    m.storage_transport=KUI_STORAGE_SCI;m.reader=KUI_RETAIL_READER_STANDARD;
    m.session_lba=45000u;m.boot_lba=548634u;m.boot_bytes=KUI_VMU_TOY_EXEC_BYTES;
    for(unsigned i=0;i<15u;i++) {
        m.slots[i].track.start_lba=starts[i];m.slots[i].track.end_lba=ends[i];
        m.slots[i].track.control=(i==0u || i==2u || i==14u)?4u:0u;
        m.slots[i].track.extent_count=1u;
    }
    return m;
}

static void exact_profile(void) {
    struct kui_retail_manifest m=profile();
    assert(kui_vmu_toy_observe_manifest(&m));
    assert(!kui_vmu_toy_observe_manifest(NULL));
    m.flags=KUI_RETAIL_IMAGE_BOOT_CRC;m.boot_crc32=KUI_VMU_TOY_EXEC_CRC;
    assert(kui_vmu_toy_observe_manifest(&m));
    m.boot_crc32^=1u;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.flags=KUI_RETAIL_IMAGE_CD;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.flags=KUI_RETAIL_IMAGE_SCRAMBLED;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.gdi_crc32^=1u;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.track_count=99u;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.extent_count=50u;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.extent_count=0u;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.reader=KUI_RETAIL_READER_ASYNC;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.reader=KUI_RETAIL_READER_ASYNC_EAGER;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.storage_transport=KUI_STORAGE_SCIF;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.storage_transport=KUI_STORAGE_IDE;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.session_lba++;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.boot_lba++;assert(!kui_vmu_toy_observe_manifest(&m));
    m=profile();m.boot_bytes--;assert(!kui_vmu_toy_observe_manifest(&m));
    for(unsigned i=0;i<15u;i++) {
        m=profile();m.slots[i].track.start_lba++;assert(!kui_vmu_toy_observe_manifest(&m));
        m=profile();m.slots[i].track.end_lba--;assert(!kui_vmu_toy_observe_manifest(&m));
        m=profile();m.slots[i].track.control^=4u;assert(!kui_vmu_toy_observe_manifest(&m));
        m=profile();m.slots[i].track.control|=KUI_RETAIL_TRACK_COOKED;
        assert(!kui_vmu_toy_observe_manifest(&m));
        m=profile();m.slots[i].track.first_extent=0x100u;
        assert(!kui_vmu_toy_observe_manifest(&m));
        m=profile();m.slots[i].track.extent_count=0u;
        assert(!kui_vmu_toy_observe_manifest(&m));
    }
}

static void owned_region(void) {
    /* Test all three ordinary physical aliases, exact boundaries, wrap and
     * spans which start outside the lease and cross into it. */
    const uint32_t bases[3]={0x0c000000u,0x8c000000u,0xac000000u};
    for(unsigned i=0;i<3u;i++) {
        uint32_t base=bases[i];
        assert(kui_vmu_toy_observe_destination(base+0x8000u,512u));
        assert(!kui_vmu_toy_observe_destination(base+0x7fffu,1u));
        assert(kui_vmu_toy_observe_destination(base+0xfcffc0u,32u));
        assert(!kui_vmu_toy_observe_destination(base+0xfcffc0u,33u));
        assert(!kui_vmu_toy_observe_destination(base+0xfcffe0u,1u));
        assert(!kui_vmu_toy_observe_destination(base+0xfd0000u,512u));
        assert(!kui_vmu_toy_observe_destination(base+0xfdffffu,1u));
        assert(kui_vmu_toy_observe_destination(base+0xfe0000u,512u));
        assert(kui_vmu_toy_observe_destination(base+0xffffffu,1u));
        assert(!kui_vmu_toy_observe_destination(base+0xffffffu,2u));
        assert(!kui_vmu_toy_observe_destination(base+0xffffffu,0xffffffffu));
        assert(!kui_vmu_toy_observe_destination(base+0x1000000u,1u));
        assert(!kui_vmu_toy_observe_destination(base+0xfe0000u,0u));
    }
    assert(!kui_vmu_toy_observe_destination(0xec010000u,512u));
    assert(!kui_vmu_toy_observe_destination(0x8b010000u,512u));
    assert(!kui_vmu_toy_observe_destination(0xa05f6c00u,4u));
    assert(!kui_vmu_toy_observe_destination(0x8cfc0000u,0x20000u));
}

int main(void) {
    exact_profile();owned_region();
    puts("Toy VMU observation admission: passed");
    return 0;
}
