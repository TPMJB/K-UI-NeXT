/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_map_diagnostic.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

/* Canonical maps come from the production encoder. No private disc data is
 * needed: all sectors, extents and title metadata are synthetic. */
static struct kui_retail_manifest map;
static uint8_t wire[KUI_RETAIL_IMAGE_WIRE_BYTES],clean[KUI_RETAIL_IMAGE_WIRE_BYTES];
static unsigned checks;
#define CHECK(test) do { ++checks; if(!(test)) { \
    fprintf(stderr,"line %d: %s\n",__LINE__,#test); assert(test); \
} } while(0)
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static void put64(uint8_t *p,uint64_t value) {
    put32(p,(uint32_t)value);put32(p+4u,(uint32_t)(value>>32));
}
static uint32_t independent_crc(const uint8_t *p) {
    static const uint8_t zero[4]={0};
    uint32_t crc=kui_retail_crc32(0,p,16u);
    crc=kui_retail_crc32(crc,zero,sizeof(zero));
    return kui_retail_crc32(crc,p+20u,KUI_RETAIL_IMAGE_WIRE_BYTES-20u);
}
static void refresh(void) {put32(wire+16u,independent_crc(wire));}
static void reset(void) {memcpy(wire,clean,sizeof(wire));}
static void fixture(bool cooked) {
    memset(&map,0,sizeof(map));
    map.card_sectors=4096u;map.partition_start=50u;map.partition_end=4000u;
    map.track_count=3u;map.extent_count=3u;map.session_lba=45000u;
    map.boot_lba=45016u;map.boot_bytes=4096u;
    map.storage_transport=KUI_STORAGE_SCI;map.reader=KUI_RETAIL_READER_ASYNC;
    map.flags=cooked?KUI_RETAIL_IMAGE_BOOT_CRC:0u;
    map.gdi_crc32=0x12345678u;map.ip_crc32=0x98765432u;map.boot_crc32=0x71727374u;
    strcpy(map.title,"Synthetic diagnostic disc");strcpy(map.product,"KUI-DIAG");
    strcpy(map.bootfile,"1ST_READ.BIN");strcpy(map.region,"JUE");
    static const uint32_t starts[3]={0u,20u,45000u},ends[3]={20u,30u,45100u};
    static const uint32_t physical[3]={100u,300u,1000u};
    for(unsigned i=0;i<3u;i++) {
        uint32_t stride=cooked && i!=1u?2048u:2352u;
        map.slots[i].track=(struct kui_retail_track){.start_lba=starts[i],.end_lba=ends[i],
            .first_extent=(uint16_t)(3u+i),.extent_count=1u,
            .control=i==1u?0u:4u|(cooked?KUI_RETAIL_TRACK_COOKED:0u)};
        map.slots[3u+i].extent=(struct kui_retail_extent){.file_block=0u,
            .card_lba=physical[i],.blocks=((ends[i]-starts[i])*stride+511u)/512u};
    }
    CHECK(kui_retail_manifest_encode(&map,wire)==KUI_GAME_OK);
    memcpy(clean,wire,sizeof(clean));
}
static void expect(uint32_t wanted,bool exact) {
    struct kui_retail_map_diagnostic diagnostic;
    uint8_t before[sizeof(wire)];memcpy(before,wire,sizeof(before));
    memset(&diagnostic,0xa5,sizeof(diagnostic));
    kui_retail_map_diagnose(wire,&diagnostic);
    if((exact && diagnostic.reasons!=wanted) || (!exact && (diagnostic.reasons&wanted)!=wanted))
        fprintf(stderr,"wanted reasons %08x%s, got %08x\n",wanted,exact?" exactly":" included",diagnostic.reasons);
    CHECK(exact?diagnostic.reasons==wanted:(diagnostic.reasons&wanted)==wanted);
    CHECK(!memcmp(wire,before,sizeof(wire)));
    CHECK(diagnostic.stored_crc==word(wire+16u));
    CHECK(diagnostic.actual_crc==independent_crc(wire));
    CHECK(diagnostic.flags==word(wire+268u));CHECK(diagnostic.reader==word(wire+264u));
    CHECK(diagnostic.track_count==word(wire+20u));CHECK(diagnostic.extent_count==word(wire+24u));
    CHECK(kui_retail_map_diagnostic_crc(wire)==independent_crc(wire));
    CHECK(!memcmp(wire,before,sizeof(wire)));
}
static void valid_policy_edges(void) {
    fixture(false);map.reader=KUI_RETAIL_READER_STANDARD;
    for(uint32_t transport=KUI_STORAGE_SCIF;transport<=KUI_STORAGE_IDE;transport++) {
        map.storage_transport=transport;
        CHECK(kui_retail_manifest_encode(&map,wire)==KUI_GAME_OK);expect(0u,true);
    }
    /* Ordinary CD sessions may start at zero; blank optional text fields and
     * the maximum representable card size remain canonical metadata. */
    map.storage_transport=KUI_STORAGE_SCI;map.session_lba=0u;map.boot_lba=16u;
    map.flags=KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED;
    map.card_sectors=UINT64_C(0x100000000);
    memset(map.product,0,sizeof(map.product));memset(map.region,0,sizeof(map.region));
    CHECK(kui_retail_manifest_encode(&map,wire)==KUI_GAME_OK);expect(0u,true);
    /* A standard map with 65 used slots is valid. The same correctly framed
     * metadata exceeds the background reader's 64-slot allocation. */
    fixture(false);map.reader=KUI_RETAIL_READER_STANDARD;map.extent_count=62u;
    map.slots[0].track.extent_count=60u;
    map.slots[1].track.first_extent=63u;map.slots[2].track.first_extent=64u;
    for(unsigned i=0;i<60u;i++) map.slots[3u+i].extent=(struct kui_retail_extent){
        .file_block=i,.card_lba=100u+i,.blocks=i==59u?33u:1u};
    map.slots[63].extent=(struct kui_retail_extent){.file_block=0u,.card_lba=300u,.blocks=46u};
    map.slots[64].extent=(struct kui_retail_extent){.file_block=0u,.card_lba=1000u,.blocks=460u};
    CHECK(kui_retail_manifest_encode(&map,wire)==KUI_GAME_OK);expect(0u,true);
    put32(wire+264u,KUI_RETAIL_READER_ASYNC);refresh();expect(KUI_RETAIL_MAP_DIAG_READER,true);
    put32(wire+264u,KUI_RETAIL_READER_ASYNC_EAGER);refresh();expect(KUI_RETAIL_MAP_DIAG_READER,true);
}
int main(void) {
    for(unsigned cooked=0;cooked<2u;cooked++) {
        fixture(cooked!=0u);expect(0u,true);
        for(uint32_t reader=KUI_RETAIL_READER_STANDARD;reader<=KUI_RETAIL_READER_ASYNC_EAGER;reader++) {
            map.reader=reader;
            CHECK(kui_retail_manifest_encode(&map,wire)==KUI_GAME_OK);expect(0u,true);
        }
    }
    fixture(false);
    wire[0]^=1u;refresh();expect(KUI_RETAIL_MAP_DIAG_MAGIC,true);
    reset();put32(wire+8u,KUI_RETAIL_IMAGE_VERSION+1u);refresh();expect(KUI_RETAIL_MAP_DIAG_VERSION,true);
    reset();put32(wire+12u,KUI_RETAIL_IMAGE_WIRE_BYTES-1u);refresh();expect(KUI_RETAIL_MAP_DIAG_SIZE,true);
    reset();wire[16]^=1u;expect(KUI_RETAIL_MAP_DIAG_CRC,true);
    reset();wire[68]^=1u;expect(KUI_RETAIL_MAP_DIAG_CRC,true);
    for(unsigned byte=272u;byte<320u;byte++) {
        reset();wire[byte]=1u;refresh();expect(KUI_RETAIL_MAP_DIAG_RESERVED,true);
    }
    reset();put32(wire+20u,0u);refresh();expect(KUI_RETAIL_MAP_DIAG_COUNT,false);
    reset();put32(wire+20u,UINT32_MAX);refresh();expect(KUI_RETAIL_MAP_DIAG_COUNT,false);
    reset();put32(wire+24u,UINT32_MAX);refresh();expect(KUI_RETAIL_MAP_DIAG_COUNT,false);
    reset();put32(wire+24u,0u);refresh();expect(KUI_RETAIL_MAP_DIAG_COUNT,false);
    const unsigned unused[]={320u+6u*12u,320u+160u*12u,4095u};
    for(unsigned i=0;i<sizeof(unused)/sizeof(unused[0]);i++) {
        reset();wire[unused[i]]=1u;refresh();expect(KUI_RETAIL_MAP_DIAG_UNUSED,true);
    }
    reset();wire[320u+11u]=1u;refresh();expect(KUI_RETAIL_MAP_DIAG_TRACK,true);
    reset();wire[320u+8u]|=1u;refresh();expect(KUI_RETAIL_MAP_DIAG_TRACK,true);
    reset();put32(wire+320u+4u,0u);refresh();expect(KUI_RETAIL_MAP_DIAG_TRACK,false);
    reset();put32(wire+320u+12u,19u);refresh();expect(KUI_RETAIL_MAP_DIAG_TRACK,true);
    reset();wire[320u+12u+8u]=KUI_RETAIL_TRACK_COOKED;refresh();expect(KUI_RETAIL_MAP_DIAG_TRACK,true);
    const unsigned text_start[]={72u,200u,216u,240u},text_size[]={128u,16u,24u,16u};
    for(unsigned i=0;i<4u;i++) {
        reset();wire[text_start[i]]=31u;refresh();expect(KUI_RETAIL_MAP_DIAG_TEXT,true);
        reset();wire[text_start[i]+text_size[i]-1u]='X';refresh();expect(KUI_RETAIL_MAP_DIAG_TEXT,true);
        reset();memset(wire+text_start[i],'X',text_size[i]);refresh();expect(KUI_RETAIL_MAP_DIAG_TEXT,true);
    }
    reset();wire[72u]=0u;refresh();expect(KUI_RETAIL_MAP_DIAG_TEXT,true);
    reset();wire[216u]=0u;refresh();expect(KUI_RETAIL_MAP_DIAG_TEXT,true);
    reset();put32(wire+268u,0x80000000u);refresh();expect(KUI_RETAIL_MAP_DIAG_FLAGS,true);
    reset();put32(wire+268u,KUI_RETAIL_IMAGE_SCRAMBLED);refresh();expect(KUI_RETAIL_MAP_DIAG_FLAGS,true);
    reset();put64(wire+32u,0u);refresh();expect(KUI_RETAIL_MAP_DIAG_BOUNDS,true);
    reset();put64(wire+32u,UINT64_C(0x100000001));refresh();expect(KUI_RETAIL_MAP_DIAG_BOUNDS,true);
    reset();put64(wire+40u,4000u);refresh();expect(KUI_RETAIL_MAP_DIAG_BOUNDS,true);
    reset();put64(wire+48u,4097u);refresh();expect(KUI_RETAIL_MAP_DIAG_BOUNDS,true);
    reset();put32(wire+264u,3u);refresh();expect(KUI_RETAIL_MAP_DIAG_READER,true);
    reset();put32(wire+28u,KUI_STORAGE_AUTO);refresh();
    expect(KUI_RETAIL_MAP_DIAG_BOUNDS|KUI_RETAIL_MAP_DIAG_READER,true);
    reset();put32(wire+28u,KUI_STORAGE_SCIF);refresh();expect(KUI_RETAIL_MAP_DIAG_READER,true);
    reset();put32(wire+56u,45001u);refresh();expect(KUI_RETAIL_MAP_DIAG_SESSION,true);
    reset();put32(wire+56u,0u);refresh();expect(KUI_RETAIL_MAP_DIAG_SESSION,true);
    reset();put32(wire+64u,0u);refresh();expect(KUI_RETAIL_MAP_DIAG_BOOT,true);
    reset();put32(wire+64u,KUI_RETAIL_IMAGE_BOOT_MAX+1u);refresh();expect(KUI_RETAIL_MAP_DIAG_BOOT,true);
    reset();put32(wire+60u,44999u);refresh();expect(KUI_RETAIL_MAP_DIAG_BOOT,true);
    reset();put32(wire+60u,45100u);refresh();expect(KUI_RETAIL_MAP_DIAG_BOOT,true);
    /* Multiple independent failures remain visible in one observation. */
    reset();wire[0]^=1u;wire[272u]=1u;put32(wire+264u,3u);refresh();
    expect(KUI_RETAIL_MAP_DIAG_MAGIC|KUI_RETAIL_MAP_DIAG_RESERVED|KUI_RETAIL_MAP_DIAG_READER,true);
    /* Metadata observations deliberately cannot certify allocation extents.
     * The canonical decoder must still reject an out-of-partition extent even
     * when framing, initial policy and the diagnostic summary are all clean. */
    reset();put32(wire+320u+3u*12u+4u,0u);refresh();expect(0u,true);
    static struct kui_retail_manifest decoded;
    CHECK(kui_retail_manifest_decode(wire,&decoded)==KUI_GAME_RANGE);
    valid_policy_edges();fixture(false);
    struct kui_retail_map_diagnostic diagnostic;
    memset(&diagnostic,0xa5,sizeof(diagnostic));kui_retail_map_diagnose(NULL,&diagnostic);
    CHECK(diagnostic.reasons==KUI_RETAIL_MAP_DIAG_NULL);
    CHECK(!diagnostic.stored_crc && !diagnostic.actual_crc && !diagnostic.flags &&
        !diagnostic.reader && !diagnostic.track_count && !diagnostic.extent_count);
    CHECK(kui_retail_map_diagnostic_crc(NULL)==0u);
    reset();kui_retail_map_diagnose(wire,NULL);CHECK(!memcmp(wire,clean,sizeof(wire)));
    printf("retail map diagnostic: %u checks passed; canonical raw/cooked maps, refusal reasons, immutable inputs\n",checks);
    return 0;
}
