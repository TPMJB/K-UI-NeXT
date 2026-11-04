/* SPDX-License-Identifier: GPL-3.0-only */
#include "update_fixture.h"
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Updater codec failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void package_codecs(void) {
    struct uf_package original=uf_build("012345abcdef",1u);struct kui_update_package package;
    CHECK(uf_decode(&original,&package)==KUI_UPDATE_OK);
    CHECK(!strcmp(package.build,"012345abcdef") && package.channel==1u && package.file_bytes==original.bytes);
    CHECK(package.count==original.count && package.payload_bytes==original.count*UF_FILE_BYTES);
    uint64_t offset=128u+128u*package.count;
    for(unsigned i=0;i<package.count;i++) {
        CHECK(!strcmp(package.entries[i].path,kui_update_catalog_path(i)));
        CHECK(package.entries[i].payload_offset==offset && package.entries[i].bytes==UF_FILE_BYTES);offset+=UF_FILE_BYTES;
        CHECK(kui_update_package_find(&package,kui_update_catalog_path(i))==&package.entries[i]);
    }
    CHECK(!kui_update_catalog_path(package.count) && !kui_update_package_find(&package,"../runtime.kui"));
    struct kui_update_pin pin;kui_update_package_pin(&package,&pin);CHECK(kui_update_pin_matches(&pin,&package));
    pin.manifest_sha256[0]^=1u;CHECK(!kui_update_pin_matches(&pin,&package));pin.manifest_sha256[0]^=1u;
    ++pin.file_bytes;CHECK(!kui_update_pin_matches(&pin,&package));--pin.file_bytes;
    pin.channel^=1u;CHECK(!kui_update_pin_matches(&pin,&package));pin.channel^=1u;
    pin.build[0]='f';CHECK(!kui_update_pin_matches(&pin,&package));
    uint8_t receipt[KUI_UPDATE_RECEIPT_MAX];size_t written=0;
    CHECK(kui_update_receipt_encode(&package,receipt,sizeof(receipt),&written)==KUI_UPDATE_OK);
    CHECK(written==128u+128u*package.count && !memcmp(receipt,original.data,written));
    CHECK(kui_update_receipt_encode(&package,receipt,written-1u,&written)!=KUI_UPDATE_OK);
    uint8_t *saved=malloc(original.bytes);CHECK(saved);memcpy(saved,original.data,original.bytes);
    for(unsigned i=0;i<128u;i++) {
        original.data[i]^=1u;CHECK(kui_update_header_decode(original.data,original.bytes,&package)!=KUI_UPDATE_OK);original.data[i]^=1u;
    }
    CHECK(kui_update_header_decode(original.data,original.bytes-1u,&package)!=KUI_UPDATE_OK);
    CHECK(kui_update_header_decode(original.data,original.bytes+1u,&package)!=KUI_UPDATE_OK);
    const unsigned reserved[]={45u,63u,96u,123u};
    for(unsigned i=0;i<sizeof(reserved)/sizeof(reserved[0]);i++) {
        original.data[reserved[i]]=1u;uf_header_crc(&original);
        CHECK(kui_update_header_decode(original.data,original.bytes,&package)!=KUI_UPDATE_OK);memcpy(original.data,saved,original.bytes);
    }
    uf_put64(original.data+24u,UINT64_MAX);uf_header_crc(&original);
    CHECK(kui_update_header_decode(original.data,UINT64_MAX,&package)!=KUI_UPDATE_OK);memcpy(original.data,saved,original.bytes);
    uf_put32(original.data+16u,65u);uf_put32(original.data+20u,65u*128u);uf_header_crc(&original);
    CHECK(kui_update_header_decode(original.data,original.bytes,&package)!=KUI_UPDATE_OK);memcpy(original.data,saved,original.bytes);
    original.data[44u]=2u;uf_header_crc(&original);CHECK(uf_decode(&original,&package)!=KUI_UPDATE_OK);memcpy(original.data,saved,original.bytes);
    static const char *const bad_paths[]={"../runtime.kui","/runtime.kui","apps/../runtime.kui","apps\\runtime.kui","runtime.kui/","KUI/vmu/cards/card.vmd"};
    for(unsigned i=0;i<sizeof(bad_paths)/sizeof(bad_paths[0]);i++) {
        memset(uf_entry(&original,0),0,80u);strcpy((char *)uf_entry(&original,0),bad_paths[i]);uf_manifest_hash(&original);
        CHECK(uf_decode(&original,&package)!=KUI_UPDATE_OK);memcpy(original.data,saved,original.bytes);
    }
    memcpy(uf_entry(&original,1),uf_entry(&original,0),128u);uf_manifest_hash(&original);
    CHECK(uf_decode(&original,&package)!=KUI_UPDATE_OK);memcpy(original.data,saved,original.bytes);
    uint8_t swap[128];memcpy(swap,uf_entry(&original,0),128u);memcpy(uf_entry(&original,0),uf_entry(&original,1),128u);memcpy(uf_entry(&original,1),swap,128u);
    uf_manifest_hash(&original);CHECK(uf_decode(&original,&package)!=KUI_UPDATE_OK);memcpy(original.data,saved,original.bytes);
    const unsigned bad_entry_bytes[]={79u,120u,127u};
    for(unsigned i=0;i<sizeof(bad_entry_bytes)/sizeof(bad_entry_bytes[0]);i++) {
        uf_entry(&original,0)[bad_entry_bytes[i]]=1u;uf_manifest_hash(&original);
        CHECK(uf_decode(&original,&package)!=KUI_UPDATE_OK);memcpy(original.data,saved,original.bytes);
    }
    uf_put64(uf_entry(&original,0)+80u,UINT64_MAX);uf_manifest_hash(&original);CHECK(uf_decode(&original,&package)!=KUI_UPDATE_OK);
    memcpy(original.data,saved,original.bytes);uf_put64(uf_entry(&original,0)+80u,0u);uf_manifest_hash(&original);
    CHECK(uf_decode(&original,&package)!=KUI_UPDATE_OK);
    free(saved);uf_free(&original);
}
static void capability_and_selection(void) {
    CHECK(kui_update_build_valid("012345abcdef"));
    static const char *const bad[]={"","123","012345abcdeF","../runtime12","012345abcdef0","012345abcdeg"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++)CHECK(!kui_update_build_valid(bad[i]));
    char path[192];CHECK(kui_update_bank_path("012345abcdef",kui_update_catalog_path(0u),path,sizeof(path)));
    CHECK(!strncmp(path,KUI_UPDATE_SYSTEM "/012345abcdef/",sizeof(KUI_UPDATE_SYSTEM)+13u));
    CHECK(!kui_update_bank_path("012345abcdef","../runtime.kui",path,sizeof(path)));
    CHECK(!kui_update_bank_path("012345abcdef","/runtime.kui",path,sizeof(path)));
    CHECK(!kui_update_bank_path("012345abcdef","KUI/vmu/cards/card.vmd",path,sizeof(path)));
    CHECK(!kui_update_bank_path("012345abcdef",kui_update_catalog_path(0u),path,8u));
    uint8_t bytes[80]={0};static const uint8_t marker[16]=KUI_UPDATE_CAPABILITY_INITIALIZER;
    CHECK(!kui_update_capability_valid(bytes,sizeof(bytes)));
    for(unsigned offset=0;offset<8u;offset++) {
        memset(bytes,0,sizeof(bytes));memcpy(bytes+offset,marker,sizeof(marker));CHECK(kui_update_capability_valid(bytes,sizeof(bytes)));
        bytes[offset+15u]^=1u;CHECK(!kui_update_capability_valid(bytes,sizeof(bytes)));
    }
    memset(bytes,0,sizeof(bytes));memcpy(bytes,marker,sizeof(marker));memcpy(bytes+32u,marker,sizeof(marker));
    CHECK(!kui_update_capability_valid(bytes,sizeof(bytes)) && !kui_update_capability_valid(marker,15u));
    struct kui_update_activation value={.sequence=9u},got;strcpy(value.selected,"012345abcdef");strcpy(value.previous,"fedcba543210");value.manifest_sha256[0]=0x42;
    uint8_t wire[128];CHECK(kui_update_activation_encode(&value,wire));CHECK(kui_update_activation_decode(wire,&got));
    CHECK(got.sequence==9u && !strcmp(got.selected,value.selected) && !strcmp(got.previous,value.previous) && !memcmp(got.manifest_sha256,value.manifest_sha256,32u));
    for(unsigned i=0;i<sizeof(wire);i++) {wire[i]^=1u;CHECK(!kui_update_activation_decode(wire,&got));wire[i]^=1u;}
    value.selected[0]=0;memset(value.manifest_sha256,0,32u);CHECK(kui_update_activation_encode(&value,wire) && kui_update_activation_decode(wire,&got));
    CHECK(!got.selected[0] && !strcmp(got.previous,value.previous));
}
int main(void) {package_codecs();capability_and_selection();printf("PASS offline updater codec: %u checks; canonical cohort, byte integrity, malformed paths, overflow, capability and selection CRC\n",checks);return 0;}
