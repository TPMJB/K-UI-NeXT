/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_silver_observe_admission.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Scalar disc metadata only; this fixture contains no executable bytes. */
static struct kui_retail_manifest profile(void) {
    static const uint32_t starts[3]={0u,660u,45000u};
    static const uint32_t ends[3]={510u,6610u,549150u};
    struct kui_retail_manifest m;
    memset(&m,0,sizeof(m));
    m.gdi_crc32=0x5e1b611au;m.track_count=3u;m.extent_count=3u;
    m.storage_transport=KUI_STORAGE_SCI;m.reader=KUI_RETAIL_READER_STANDARD;
    m.session_lba=45000u;m.boot_lba=547556u;m.boot_bytes=2956128u;
    for(unsigned i=0;i<3u;i++) {
        m.slots[i].track.start_lba=starts[i];m.slots[i].track.end_lba=ends[i];
        m.slots[i].track.control=i==1u?0u:4u;
        m.slots[i].track.extent_count=1u;
    }
    return m;
}

static void exact_owner_and_reader(void) {
    struct kui_retail_manifest m=profile();
    assert(KUI_VMU_SILVER_EXEC_BYTES==2956128u);
    assert(KUI_VMU_SILVER_EXEC_CRC==0x7d83cf24u);
    assert(KUI_VMU_SILVER_HEAP_BEGIN==0x8c2d9b80u);
    assert(KUI_VMU_SILVER_HEAP_BYTES==0x00d26480u);
    assert(kui_vmu_silver_observe_manifest(&m));
    assert(!kui_vmu_silver_observe_manifest(NULL));
    m.flags=KUI_RETAIL_IMAGE_BOOT_CRC;m.boot_crc32=0x7d83cf24u;
    assert(kui_vmu_silver_observe_manifest(&m));
    m.boot_crc32^=1u;assert(!kui_vmu_silver_observe_manifest(&m));
    m=profile();m.flags=KUI_RETAIL_IMAGE_CD;assert(!kui_vmu_silver_observe_manifest(&m));
    m=profile();m.flags=KUI_RETAIL_IMAGE_SCRAMBLED;assert(!kui_vmu_silver_observe_manifest(&m));
    m=profile();m.gdi_crc32^=1u;assert(!kui_vmu_silver_observe_manifest(&m));
    m=profile();m.track_count=15u;assert(!kui_vmu_silver_observe_manifest(&m));
    m=profile();m.session_lba++;assert(!kui_vmu_silver_observe_manifest(&m));
    m=profile();m.boot_lba++;assert(!kui_vmu_silver_observe_manifest(&m));
    m=profile();m.boot_bytes--;assert(!kui_vmu_silver_observe_manifest(&m));
    for(unsigned reader=KUI_RETAIL_READER_ASYNC;reader<=KUI_RETAIL_READER_ASYNC_EAGER;reader++) {
        m=profile();m.reader=reader;assert(!kui_vmu_silver_observe_manifest(&m));
    }
    const uint32_t transports[]={KUI_STORAGE_SCIF,KUI_STORAGE_IDE,UINT32_MAX};
    for(unsigned i=0;i<sizeof(transports)/sizeof(transports[0]);i++) {
        m=profile();m.storage_transport=transports[i];
        assert(!kui_vmu_silver_observe_manifest(&m));
    }
    /* This title-specific check follows the ordinary manifest validator.
     * These cases independently exercise its 64-slot admission budget. */
    m=profile();m.extent_count=61u;assert(kui_vmu_silver_observe_manifest(&m));
    m.extent_count=62u;assert(!kui_vmu_silver_observe_manifest(&m));
    m.extent_count=0u;assert(!kui_vmu_silver_observe_manifest(&m));
    m.extent_count=UINT32_MAX;assert(!kui_vmu_silver_observe_manifest(&m));
}

static void every_track_must_match(void) {
    struct kui_retail_manifest m;
    const uint8_t formats[]={KUI_RETAIL_TRACK_COOKED,KUI_RETAIL_TRACK_MODE2,
        KUI_RETAIL_TRACK_2336,KUI_RETAIL_TRACK_2448,KUI_RETAIL_TRACK_OFFSET_HIGH};
    for(unsigned i=0;i<3u;i++) {
        m=profile();m.slots[i].track.start_lba++;
        assert(!kui_vmu_silver_observe_manifest(&m));
        m=profile();m.slots[i].track.end_lba--;
        assert(!kui_vmu_silver_observe_manifest(&m));
        m=profile();m.slots[i].track.control^=4u;
        assert(!kui_vmu_silver_observe_manifest(&m));
        m=profile();m.slots[i].track.extent_count=0u;
        assert(!kui_vmu_silver_observe_manifest(&m));
        m=profile();m.slots[i].track.first_extent=0x100u;
        assert(!kui_vmu_silver_observe_manifest(&m));
        for(unsigned j=0;j<sizeof(formats)/sizeof(formats[0]);j++) {
            m=profile();m.slots[i].track.control|=formats[j];
            assert(!kui_vmu_silver_observe_manifest(&m));
        }
    }
}

static void protected_destinations_and_aliases(void) {
    const uint32_t aliases[]={0x0c000000u,0x8c000000u,0xac000000u};
    for(unsigned i=0;i<sizeof(aliases)/sizeof(aliases[0]);i++) {
        const uint32_t b=aliases[i];
        assert(kui_vmu_silver_observe_destination(b+0x8000u,1u));
        assert(!kui_vmu_silver_observe_destination(b+0x7fffu,1u));
        assert(kui_vmu_silver_observe_destination(b+0xfcffc0u,32u));
        assert(!kui_vmu_silver_observe_destination(b+0xfcffc0u,33u));
        assert(!kui_vmu_silver_observe_destination(b+0xfcffe0u,1u));
        assert(!kui_vmu_silver_observe_destination(b+0xfd0000u,1u));
        assert(!kui_vmu_silver_observe_destination(b+0xfdffffu,1u));
        assert(!kui_vmu_silver_observe_destination(b+0xfc0000u,0x20000u));
        assert(kui_vmu_silver_observe_destination(b+0xfe0000u,0x20000u));
        assert(kui_vmu_silver_observe_destination(b+0xffffffu,1u));
        assert(!kui_vmu_silver_observe_destination(b+0xffffffu,2u));
        assert(!kui_vmu_silver_observe_destination(b+0x8000u,UINT32_MAX));
        assert(!kui_vmu_silver_observe_destination(b+0x1000000u,1u));
        assert(!kui_vmu_silver_observe_destination(b+0x8000u,0u));
    }
    assert(!kui_vmu_silver_observe_destination(0xec010000u,512u));
    assert(!kui_vmu_silver_observe_destination(0x8b010000u,512u));
    assert(!kui_vmu_silver_observe_destination(0xa05f6c00u,4u));
}

int main(void) {
    exact_owner_and_reader();every_track_must_match();protected_destinations_and_aliases();
    puts("Silver passive observer manifest and destination admission: passed");
    return 0;
}
