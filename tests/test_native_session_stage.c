/* SPDX-License-Identifier: GPL-3.0-only */
/* Execute the actual stage binder with production wire decoders. */
#include "kui/native_session.h"
#include "kui/native_return.h"
#include "kui/retail_image.h"
#include "kui/vmu_native_passive.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>

#define KUI_NATIVE_SESSION_CONTROL UINT32_C(0x8c006080)
static uint8_t kui_native_session_descriptor[KUI_NATIVE_SESSION_WIRE_BYTES];
static struct kui_retail_manifest manifest,other;
static struct kui_vmu_native_passive_config cfg;
static jmp_buf stopped;
static unsigned error_code,checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void native_report(unsigned phase,unsigned code) __attribute__((noreturn));
static void native_report(unsigned phase,unsigned code) {
    CHECK(phase==8u);error_code=code;longjmp(stopped,1);
}
#include "vmu_native_session_stage.inc"

static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;++i)p[i]=(uint8_t)(value>>(8u*i));
}
static uint32_t crc32(const void *data,size_t bytes) {
    return kui_retail_crc32(0,data,bytes);
}
static void seal(void) {
    put32(kui_native_session_descriptor+KUI_NATIVE_SESSION_CRC_OFFSET,
        kui_native_session_crc(kui_native_session_descriptor));
}
static void map_fixture(struct kui_retail_manifest *m,unsigned physical) {
    memset(m,0,sizeof(*m));
    m->card_sectors=8192u;m->partition_start=16u;m->partition_end=8000u;
    m->track_count=1u;m->extent_count=1u;m->session_lba=45000u;
    m->boot_lba=45001u;m->boot_bytes=2048u;
    m->storage_transport=KUI_STORAGE_SCI;m->reader=KUI_RETAIL_READER_ASYNC;
    m->flags=KUI_RETAIL_IMAGE_BOOT_CRC;m->boot_crc32=UINT32_C(0x12345678);
    strcpy(m->title,"Synthetic disc");strcpy(m->product,"KUI-SESSION");
    strcpy(m->region,"JUE");strcpy(m->bootfile,"1ST_READ.BIN");
    m->slots[0].track=(struct kui_retail_track){45000u,45008u,1u,1u,4u|KUI_RETAIL_TRACK_COOKED};
    m->slots[1].extent=(struct kui_retail_extent){0u,physical,32u};
    CHECK(kui_retail_manifest_validate(m)==KUI_GAME_OK);
}
static void fixture(void) {
    map_fixture(&manifest,100u);map_fixture(&other,200u);
    memset(&cfg,0,sizeof(cfg));memset(kui_native_session_descriptor,0,sizeof(kui_native_session_descriptor));
    memcpy(kui_native_session_descriptor,KUI_NATIVE_SESSION_MAGIC,8u);
    put32(kui_native_session_descriptor+8u,1u);
    put32(kui_native_session_descriptor+12u,KUI_NATIVE_SESSION_WIRE_BYTES);
    put32(kui_native_session_descriptor+20u,KUI_NATIVE_SESSION_FLAG_DISCS);
    put32(kui_native_session_descriptor+24u,2u);
    CHECK(kui_retail_manifest_encode(&manifest,kui_native_session_descriptor+KUI_NATIVE_SESSION_MAP_OFFSET)==KUI_GAME_OK);
    CHECK(kui_retail_manifest_encode(&other,kui_native_session_descriptor+KUI_NATIVE_SESSION_MAP_OFFSET+KUI_NATIVE_SESSION_MAP_BYTES)==KUI_GAME_OK);
    seal();error_code=0u;
}
static void expect(unsigned code) {
    if(!setjmp(stopped)) {
        native_session_stage_configure(&cfg);
        CHECK(code==0u);
    } else CHECK(error_code==code);
    if(code)CHECK(!cfg.session_wire && !cfg.session_wire_bytes && !cfg.session_control);
    else {
        CHECK(cfg.session_wire==(uint32_t)(uintptr_t)kui_native_session_descriptor);
        CHECK(cfg.session_wire_bytes==KUI_NATIVE_SESSION_WIRE_BYTES);
        CHECK(cfg.session_control==KUI_NATIVE_SESSION_CONTROL);
    }
}
static void return_fixture(void) {
    struct kui_native_return_plan p={.version=1u,.bytes=sizeof(p),.file_bytes=68u,
        .extent_count=1u,.card_sectors=8192u,.partition_first=16u,.partition_end=8000u,
        .extents={{0u,300u,1u}}};
    memcpy(p.header,"KUIRUN1\0",8u);put32(p.header+8u,1u);put32(p.header+12u,64u);
    put32(p.header+16u,4u);put32(p.header+20u,KUI_NATIVE_RETURN_ADDRESS);
    put32(p.header+24u,KUI_NATIVE_RETURN_ADDRESS);put32(p.header+28u,4u);
    memcpy(p.header+40u,"0123456789ab",12u);put32(p.header+60u,crc32(p.header,60u));
    CHECK(kui_native_return_encode(kui_native_session_descriptor+KUI_NATIVE_SESSION_RETURN_OFFSET,&p));
    put32(kui_native_session_descriptor+20u,KUI_NATIVE_SESSION_FLAGS_ALL);seal();
}
int main(void) {
    fixture();expect(0u);
    fixture();kui_native_session_descriptor[16]^=1u;expect(0x70u);
    fixture();kui_native_session_descriptor[KUI_NATIVE_SESSION_MAP_OFFSET+20u]^=1u;seal();expect(0x71u);
    fixture();other.card_sectors++;
    CHECK(kui_retail_manifest_encode(&other,kui_native_session_descriptor+KUI_NATIVE_SESSION_MAP_OFFSET+KUI_NATIVE_SESSION_MAP_BYTES)==KUI_GAME_OK);
    seal();expect(0x71u);
    fixture();other.partition_start++;
    CHECK(kui_retail_manifest_encode(&other,kui_native_session_descriptor+KUI_NATIVE_SESSION_MAP_OFFSET+KUI_NATIVE_SESSION_MAP_BYTES)==KUI_GAME_OK);
    seal();expect(0x71u);
    fixture();other.reader=KUI_RETAIL_READER_ASYNC_EAGER;
    CHECK(kui_retail_manifest_encode(&other,kui_native_session_descriptor+KUI_NATIVE_SESSION_MAP_OFFSET+KUI_NATIVE_SESSION_MAP_BYTES)==KUI_GAME_OK);
    seal();expect(0x71u);
    fixture();manifest.title[0]='X';expect(0x72u);
    fixture();manifest.reader=KUI_RETAIL_READER_ASYNC_EAGER;expect(0x73u);
    fixture();manifest.storage_transport=KUI_STORAGE_SCIF;expect(0x73u);
    fixture();manifest.flags|=KUI_RETAIL_IMAGE_SCRAMBLED;expect(0x73u);
    fixture();manifest.slots[0].track.control=0u;expect(0x73u);
    fixture();other.slots[2]=other.slots[1];other.slots[0].track.first_extent=2u;
    other.track_count=2u;other.extent_count=2u;
    other.slots[1].track=(struct kui_retail_track){45008u,45016u,3u,1u,0u};
    other.slots[3].extent=(struct kui_retail_extent){0u,300u,37u};
    CHECK(kui_retail_manifest_encode(&other,kui_native_session_descriptor+KUI_NATIVE_SESSION_MAP_OFFSET+KUI_NATIVE_SESSION_MAP_BYTES)==KUI_GAME_OK);
    seal();expect(0x71u);
    fixture();other.flags|=KUI_RETAIL_IMAGE_CD|KUI_RETAIL_IMAGE_SCRAMBLED;
    CHECK(kui_retail_manifest_encode(&other,kui_native_session_descriptor+KUI_NATIVE_SESSION_MAP_OFFSET+KUI_NATIVE_SESSION_MAP_BYTES)==KUI_GAME_OK);
    seal();expect(0x71u);
    fixture();return_fixture();expect(0u);
    fixture();return_fixture();kui_native_session_descriptor[KUI_NATIVE_SESSION_RETURN_OFFSET+16u]^=1u;seal();expect(0x74u);
    fixture();return_fixture();manifest.card_sectors++;expect(0x74u);
    printf("PASS native session stage: %u checks; exact selected map, card/partition/reader binding, CRC and return admission\n",checks);
    return 0;
}
