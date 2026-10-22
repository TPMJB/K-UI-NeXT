/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <sys/mman.h>
#include "../src/loader/retail_stage.c"

/* Execute the production failure report and stopped path. The linker drops
 * unrelated loading/hardware paths, so no successful launch is simulated. */
static jmp_buf terminal;
static unsigned rows,groups,hexes;
static uint32_t pointer_seen,mode_seen,reasons_seen,stored_seen,copy_seen,source_seen,detail_seen;
static uint32_t fields_seen[4];
void retail_display_line(const char *text) {
    rows++;
    if(!strcmp(text,"STORAGE WAS READ ONLY")) longjmp(terminal,1);
}
void retail_display_hex(const char *label,uint32_t value) {
    rows++;hexes++;
    if(!strcmp(label,"MAP REASONS")) reasons_seen=value;
    else if(!strcmp(label,"STORED CRC")) stored_seen=value;
    else if(!strcmp(label,"COPY CRC")) copy_seen=value;
    else if(!strcmp(label,"SOURCE CRC")) source_seen=value;
    else {assert(!strcmp(label,"DETAIL"));detail_seen=value;}
}
void retail_display_values(const char *label,const uint32_t *values,unsigned count) {
    rows+=2u;groups++;
    if(!strcmp(label,"MAP POINTER MODE")) {
        assert(count==2u);pointer_seen=values[0];mode_seen=values[1];
    } else {
        assert(!strcmp(label,"FLAGS READER TRACKS EXTENTS") && count==4u);
        memcpy(fields_seen,values,sizeof(fields_seen));
    }
}
static void encode(uint8_t *wire) {
    struct kui_retail_manifest map={.card_sectors=100000u,.partition_start=1u,
        .partition_end=100000u,.track_count=3u,.extent_count=3u,.session_lba=45000u,
        .boot_lba=45000u,.boot_bytes=2048u,.storage_transport=KUI_STORAGE_SCI,
        .reader=KUI_RETAIL_READER_ASYNC,.flags=KUI_RETAIL_IMAGE_BOOT_CRC};
    strcpy(map.title,"Generated failure-report fixture");strcpy(map.bootfile,"KERNEL.BIN");
    map.slots[0].track=(struct kui_retail_track){0u,1u,3u,1u,4u};
    map.slots[1].track=(struct kui_retail_track){1u,2u,4u,1u,0u};
    map.slots[2].track=(struct kui_retail_track){45000u,45020u,5u,1u,4u|KUI_RETAIL_TRACK_COOKED};
    map.slots[3].extent=(struct kui_retail_extent){0u,100u,5u};
    map.slots[4].extent=(struct kui_retail_extent){0u,200u,5u};
    map.slots[5].extent=(struct kui_retail_extent){0u,300u,80u};
    assert(kui_retail_manifest_encode(&map,wire)==KUI_GAME_OK);
}
static void report(const uint8_t *source) {
    rows=groups=hexes=0u;
    if(!setjmp(terminal)) report_invalid_map(source,KUI_GAME_INVALID);
    assert(rows==13u && groups==2u && hexes==5u && detail_seen==KUI_GAME_INVALID);
    /* The caller's LAUNCHER line makes14rows: no 444-pixel wrap. */
    assert(108u+(rows+1u)*22u<=444u);
    assert(pointer_seen==(uint32_t)(uintptr_t)source);
#if KUI_ARMADA_CE_STREAM
    assert(mode_seen==1u+(uint32_t)KUI_WINCE_CDDA_COMPILED_PROFILE);
#elif defined(KUI_RETAIL_CE)
    assert(mode_seen==1u);
#else
    assert(mode_seen==0u);
#endif
    assert(fields_seen[0]==KUI_RETAIL_IMAGE_BOOT_CRC && fields_seen[1]==KUI_RETAIL_READER_ASYNC &&
        fields_seen[2]==3u && fields_seen[3]==3u);
}
int main(void) {
    uint8_t *source=(void *)(uintptr_t)0x8c011000u;
    assert(mmap(source,4096u,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==source);
    encode(source);uint32_t valid_crc=kui_retail_map_diagnostic_crc(source);
    memcpy(wire_copy,source,sizeof(wire_copy));wire_copy[0]^=1u;
    uint8_t before[4096];memcpy(before,wire_copy,sizeof(before));
    report(source);
    assert(reasons_seen==(KUI_RETAIL_MAP_DIAG_MAGIC|KUI_RETAIL_MAP_DIAG_CRC|KUI_RETAIL_MAP_DIAG_SOURCE));
    assert(stored_seen==valid_crc && source_seen==valid_crc && copy_seen!=valid_crc);
    assert(!memcmp(before,wire_copy,sizeof(before)) && kui_retail_map_diagnostic_crc(source)==valid_crc);
    memcpy(source,wire_copy,sizeof(wire_copy));report(source);
    assert(reasons_seen==(KUI_RETAIL_MAP_DIAG_MAGIC|KUI_RETAIL_MAP_DIAG_CRC));
    assert(source_seen==copy_seen);
    /* Diagnostic pointer guards precede all additional source dereferences. */
    const uint32_t bad_pointer[]={0u,1u,0xa05f8000u,0x8d000000u,0x8c011001u};
    for(unsigned i=0;i<sizeof(bad_pointer)/sizeof(*bad_pointer);i++) {
        report((void *)(uintptr_t)bad_pointer[i]);
        assert(source_seen==0u && (reasons_seen&KUI_RETAIL_MAP_DIAG_SOURCE));
    }
    assert(munmap(source,4096u)==0);
    puts("PASS production map failure screen: source/copy separation, guarded pointers, profile identity and14-row layout");
    return 0;
}
