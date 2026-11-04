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
static void hex_digest(const char *hex,uint8_t bytes[32]) {
    for(unsigned i=0;i<32u;i++) {
        unsigned value=0u;CHECK(sscanf(hex+2u*i,"%2x",&value)==1);bytes[i]=(uint8_t)value;
    }
}
static void legacy_policy(void) {
    struct uf_package original=uf_build("012345abcdef",1u);struct kui_update_package package;
    CHECK(uf_decode(&original,&package)==KUI_UPDATE_OK);
    CHECK(!kui_update_legacy_for_path("runtime.kui") && !kui_update_legacy_for_path("../cdda-toy-raw.kui"));
    unsigned historical=0u;
    for(unsigned i=0;i<package.count;i++) {
        const struct kui_update_legacy *pin=kui_update_legacy_for_path(package.entries[i].path);
        CHECK(!strcmp(kui_update_entry_build(package.build,package.entries+i),package.build));
        if(!pin)continue;
        ++historical;struct kui_update_entry entry=package.entries[i];
        entry.policy=1u;entry.bytes=pin->bytes;hex_digest(pin->sha256,entry.sha256);
        CHECK(!strcmp(kui_update_entry_build(package.build,&entry),pin->build));
        entry.sha256[31]^=1u;CHECK(!kui_update_entry_build(package.build,&entry));entry.sha256[31]^=1u;
        ++entry.bytes;CHECK(!kui_update_entry_build(package.build,&entry));--entry.bytes;
        strcpy(entry.path,"runtime.kui");CHECK(!kui_update_entry_build(package.build,&entry));strcpy(entry.path,pin->path);
        entry.policy=2u;CHECK(!kui_update_entry_build(package.build,&entry));entry.policy=1u;
        /* Exercise real manifest decoding/receipt encoding, including policy
         * persistence; no historical executable bytes are fabricated here. */
        struct uf_package wire=uf_build(package.build,1u);
        uint8_t *member=uf_entry(&wire,i);uf_put64(member+80u,entry.bytes);
        memcpy(member+88u,entry.sha256,32u);member[120u]=1u;
        uint64_t payload=(uint64_t)wire.count*UF_FILE_BYTES-UF_FILE_BYTES+entry.bytes;
        uf_put64(wire.data+24u,payload);uf_manifest_hash(&wire);
        struct kui_update_package decoded;
        CHECK(kui_update_header_decode(wire.data,128u+128u*wire.count+payload,&decoded)==KUI_UPDATE_OK);
        CHECK(kui_update_manifest_decode(&decoded,wire.data+128u,128u*wire.count)==KUI_UPDATE_OK);
        CHECK(decoded.entries[i].policy==1u && !strcmp(kui_update_entry_build(decoded.build,decoded.entries+i),pin->build));
        uint8_t receipt[KUI_UPDATE_RECEIPT_MAX];size_t bytes=0u;
        CHECK(kui_update_receipt_encode(&decoded,receipt,sizeof(receipt),&bytes)==KUI_UPDATE_OK);
        CHECK(bytes==128u+128u*wire.count && !memcmp(receipt,wire.data,bytes));
        member[120u]=2u;uf_manifest_hash(&wire);
        CHECK(kui_update_header_decode(wire.data,128u+128u*wire.count+payload,&decoded)==KUI_UPDATE_OK);
        CHECK(kui_update_manifest_decode(&decoded,wire.data+128u,128u*wire.count)!=KUI_UPDATE_OK);
        member[120u]=1u;member[121u]=1u;uf_manifest_hash(&wire);
        CHECK(kui_update_header_decode(wire.data,128u+128u*wire.count+payload,&decoded)==KUI_UPDATE_OK);
        CHECK(kui_update_manifest_decode(&decoded,wire.data+128u,128u*wire.count)!=KUI_UPDATE_OK);
        uf_free(&wire);
    }
    CHECK(historical==3u);uf_free(&original);
}
static struct uf_package inherit_package(void) {
    struct uf_package p=uf_build("012345abcdef",1u);
    p.bytes=128u+128u*p.count+UF_FILE_BYTES;
    uf_put64(p.data+24u,UF_FILE_BYTES);
    for(unsigned i=1u;i<p.count;i++) {
        uint8_t *entry=uf_entry(&p,i);uf_put64(entry+80u,0u);memset(entry+88u,0,32u);
        entry[120u]=KUI_UPDATE_INHERIT_ROOT;
    }
    uf_manifest_hash(&p);return p;
}
static void inherit_root_policy(void) {
    struct uf_package wire=inherit_package();struct kui_update_package package;
    CHECK(uf_decode(&wire,&package)==KUI_UPDATE_OK);
    CHECK(package.payload_bytes==UF_FILE_BYTES && package.file_bytes==wire.bytes);
    CHECK(package.entries[0].policy==0u && package.entries[0].bytes==UF_FILE_BYTES);
    CHECK(!strcmp(kui_update_entry_build(package.build,package.entries),package.build));
    uint64_t end=128u+128u*wire.count+UF_FILE_BYTES;
    for(unsigned i=1u;i<package.count;i++) {
        const struct kui_update_entry *entry=package.entries+i;uint8_t zero[32]={0};
        CHECK(entry->policy==KUI_UPDATE_INHERIT_ROOT && !entry->bytes && entry->payload_offset==end);
        CHECK(!memcmp(entry->sha256,zero,sizeof(zero)));
        CHECK(kui_update_package_find(&package,kui_update_catalog_path(i))==entry);
        CHECK(!kui_update_entry_build(package.build,entry));
    }
    uint8_t receipt[KUI_UPDATE_RECEIPT_MAX];size_t bytes=0u;
    CHECK(kui_update_receipt_encode(&package,receipt,sizeof(receipt),&bytes)==KUI_UPDATE_OK);
    CHECK(bytes==128u+128u*wire.count && !memcmp(receipt,wire.data,bytes));
    struct kui_update_package roundtrip;
    CHECK(kui_update_header_decode(receipt,wire.bytes,&roundtrip)==KUI_UPDATE_OK);
    CHECK(kui_update_manifest_decode(&roundtrip,receipt+128u,bytes-128u)==KUI_UPDATE_OK);
    CHECK(roundtrip.payload_bytes==UF_FILE_BYTES && roundtrip.entries[1].policy==KUI_UPDATE_INHERIT_ROOT);
    struct kui_update_pin pin;kui_update_package_pin(&package,&pin);CHECK(kui_update_pin_matches(&pin,&roundtrip));
    uint8_t *saved=malloc(wire.bytes);CHECK(saved);memcpy(saved,wire.data,wire.bytes);
    /* The GUI is always supplied and checked; even otherwise canonical
     * zero-length inheritance metadata cannot turn it into a root alias. */
    uf_put64(uf_entry(&wire,0u)+80u,0u);memset(uf_entry(&wire,0u)+88u,0,32u);
    uf_entry(&wire,0u)[120u]=KUI_UPDATE_INHERIT_ROOT;uf_put64(wire.data+24u,0u);
    wire.bytes-=UF_FILE_BYTES;uf_manifest_hash(&wire);CHECK(uf_decode(&wire,&roundtrip)!=KUI_UPDATE_OK);
    wire.bytes+=UF_FILE_BYTES;memcpy(wire.data,saved,wire.bytes);
    /* A nonruntime inheritance descriptor has no serialized bytes or hash.
     * Repair package lengths and manifest hashes to isolate those rules. */
    uf_put64(uf_entry(&wire,1u)+80u,1u);uf_put64(wire.data+24u,UF_FILE_BYTES+1u);
    ++wire.bytes;uf_manifest_hash(&wire);CHECK(uf_decode(&wire,&roundtrip)!=KUI_UPDATE_OK);
    --wire.bytes;memcpy(wire.data,saved,wire.bytes);
    for(unsigned i=0u;i<32u;i++) {
        uf_entry(&wire,1u)[88u+i]=1u;uf_manifest_hash(&wire);
        CHECK(uf_decode(&wire,&roundtrip)!=KUI_UPDATE_OK);memcpy(wire.data,saved,wire.bytes);
    }
    for(unsigned i=0u;i<wire.count;i++) {
        uf_entry(&wire,i)[120u]=3u;uf_manifest_hash(&wire);
        CHECK(uf_decode(&wire,&roundtrip)!=KUI_UPDATE_OK);memcpy(wire.data,saved,wire.bytes);
    }
    /* Receipt encoding validates in-memory descriptors independently of the
     * decoder, before reproducing their canonical wire representation. */
    struct kui_update_package invalid=package;
    invalid.entries[0].policy=KUI_UPDATE_INHERIT_ROOT;invalid.entries[0].bytes=0u;
    memset(invalid.entries[0].sha256,0,32u);invalid.payload_bytes=0u;invalid.file_bytes-=UF_FILE_BYTES;
    CHECK(kui_update_receipt_encode(&invalid,receipt,sizeof(receipt),&bytes)!=KUI_UPDATE_OK);
    invalid=package;invalid.entries[1].bytes=1u;++invalid.payload_bytes;++invalid.file_bytes;
    CHECK(kui_update_receipt_encode(&invalid,receipt,sizeof(receipt),&bytes)!=KUI_UPDATE_OK);
    invalid=package;invalid.entries[1].sha256[0]=1u;
    CHECK(kui_update_receipt_encode(&invalid,receipt,sizeof(receipt),&bytes)!=KUI_UPDATE_OK);
    invalid=package;invalid.entries[1].policy=3u;
    CHECK(kui_update_receipt_encode(&invalid,receipt,sizeof(receipt),&bytes)!=KUI_UPDATE_OK);
    free(saved);uf_free(&wire);
}
static void withdrawn_releases(void) {
    static const char *const withdrawn[]={"28cadccf0e71","33b04057db86"};
    CHECK(!kui_update_build_withdrawn(NULL));
    CHECK(!kui_update_build_withdrawn("012345abcdef"));
    struct uf_package valid=uf_build("012345abcdef",1u);struct kui_update_package package;
    CHECK(uf_decode(&valid,&package)==KUI_UPDATE_OK);
    for(unsigned i=0u;i<sizeof(withdrawn)/sizeof(withdrawn[0]);i++) {
        CHECK(kui_update_build_valid(withdrawn[i]));CHECK(kui_update_build_withdrawn(withdrawn[i]));
        /* Withdrawal names two exact releases, without changing build syntax
         * or rejecting adjacent IDs that share their prefixes. */
        char neighbor[13];memcpy(neighbor,withdrawn[i],sizeof(neighbor));
        for(unsigned at=0u;at<12u;at++) {
            char saved=neighbor[at];neighbor[at]=saved=='0'?'1':'0';
            CHECK(kui_update_build_valid(neighbor) && !kui_update_build_withdrawn(neighbor));neighbor[at]=saved;
        }
        struct uf_package rejected=uf_build(withdrawn[i],1u);struct kui_update_package decoded;
        CHECK(kui_update_header_decode(rejected.data,rejected.bytes,&decoded)!=KUI_UPDATE_OK);
        CHECK(uf_decode(&rejected,&decoded)!=KUI_UPDATE_OK);
        /* Low-level runtime envelopes remain readable. The restriction is
         * applied at updater GUI admission and selection boundaries. */
        struct kui_runtime_info info;
        CHECK(kui_runtime_header(uf_file(&rejected,0u),UF_FILE_BYTES,&info)==KUI_RUNTIME_OK);
        CHECK(!strcmp(info.build,withdrawn[i]));
        decoded=package;memcpy(decoded.build,withdrawn[i],sizeof(decoded.build));
        CHECK(kui_update_manifest_decode(&decoded,valid.data+128u,128u*valid.count)!=KUI_UPDATE_OK);
        uint8_t receipt[KUI_UPDATE_RECEIPT_MAX];size_t bytes=0u;
        decoded=package;memcpy(decoded.build,withdrawn[i],sizeof(decoded.build));
        CHECK(kui_update_receipt_encode(&decoded,receipt,sizeof(receipt),&bytes)!=KUI_UPDATE_OK);
        CHECK(kui_update_receipt_encode(&package,receipt,sizeof(receipt),&bytes)==KUI_UPDATE_OK);
        memcpy(receipt+32u,withdrawn[i],12u);uf_put32(receipt+124u,kui_crc32(0,receipt,124u));
        CHECK(kui_update_header_decode(receipt,valid.bytes,&decoded)!=KUI_UPDATE_OK);
        /* Records preserve withdrawn IDs and their sequence numbers so boot
         * can skip them and publication can advance the real newest slot. */
        for(unsigned role=0u;role<3u;role++) {
            struct kui_update_activation activation={.sequence=UINT64_MAX-1u},got;
            strcpy(activation.selected,role==1u?"012345abcdef":withdrawn[i]);
            strcpy(activation.previous,role==0u?"fedcba543210":withdrawn[i^1u]);
            activation.manifest_sha256[0]=0x42u;uint8_t wire[128];
            CHECK(kui_update_activation_encode(&activation,wire));CHECK(kui_update_activation_decode(wire,&got));
            CHECK(got.sequence==activation.sequence && !strcmp(got.selected,activation.selected) &&
                !strcmp(got.previous,activation.previous) && !memcmp(got.manifest_sha256,activation.manifest_sha256,32u));
        }
        uf_free(&rejected);
    }
    uf_free(&valid);
}
int main(void) {package_codecs();capability_and_selection();legacy_policy();inherit_root_policy();withdrawn_releases();printf("PASS offline updater codec: %u checks; canonical cohort, byte integrity, malformed paths, overflow, exact legacy/root-inheritance policies, withdrawal and selection CRC\n",checks);return 0;}
