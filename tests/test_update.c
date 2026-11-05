/* SPDX-License-Identifier: GPL-3.0-only */
#include "update_fixture.h"
#include <stdio.h>
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){fprintf(stderr,"Updater codec failed %u: %s\n",__LINE__,#x);exit(1);}}while(0)
static void package_codecs(void) {
    struct uf_package original=uf_build("012345abcdef",1u);struct kui_update_package package;
    CHECK(uf_decode(&original,&package)==KUI_UPDATE_OK);
    CHECK(!strcmp(package.build,"012345abcdef") && package.channel==1u && package.file_bytes==original.bytes);
    CHECK(package.count==original.count && package.payload_bytes==original.bytes-128u-128u*original.count);
    uint64_t offset=128u+128u*package.count;
    for(unsigned i=0;i<package.count;i++) {
        CHECK(!strcmp(package.entries[i].path,kui_update_catalog_path(i)));
        CHECK(package.entries[i].payload_offset==offset);
        CHECK((package.entries[i].policy==KUI_UPDATE_REPLACE && package.entries[i].bytes==UF_FILE_BYTES) ||
            (package.entries[i].policy==KUI_UPDATE_INHERIT_ROOT && !package.entries[i].bytes));
        offset+=package.entries[i].bytes;
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
        if(package.entries[i].policy==KUI_UPDATE_INHERIT_ROOT)CHECK(!kui_update_entry_build(package.build,package.entries+i));
        else CHECK(!strcmp(kui_update_entry_build(package.build,package.entries+i),package.build));
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
        uint64_t payload=wire.bytes-128u-128u*wire.count-UF_FILE_BYTES+entry.bytes;
        uf_put64(wire.data+24u,payload);uf_manifest_hash(&wire);
        struct kui_update_package decoded;
        CHECK(kui_update_header_decode(wire.data,128u+128u*wire.count+payload,&decoded)==KUI_UPDATE_OK);
        CHECK(kui_update_manifest_decode(&decoded,wire.data+128u,128u*wire.count)==KUI_UPDATE_OK);
        CHECK(decoded.entries[i].policy==1u && !strcmp(kui_update_entry_build(decoded.build,decoded.entries+i),pin->build));
        struct kui_update_entry effective;
        CHECK(kui_update_package_member(&decoded,pin->path,&effective) && effective.policy==KUI_UPDATE_APPROVED_LEGACY &&
            effective.bytes==decoded.entries[i].bytes && effective.payload_offset==decoded.entries[i].payload_offset);
        CHECK(!memcmp(effective.sha256,decoded.entries[i].sha256,32u) && !strcmp(effective.path,pin->path));
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
/* Build the declared wire entries and payload only; do not expand an older
 * manifest to today's catalog or change its selected manifest identity. */
static struct uf_package subset_build(const unsigned *indices,unsigned count,uint64_t inherited) {
    struct uf_package full=uf_build("012345abcdef",1u),p={0};p.count=count;
    CHECK(count && count<=full.count);
    size_t capacity=128u+(size_t)count*(128u+UF_FILE_BYTES);
    p.data=calloc(1u,capacity);CHECK(p.data);memcpy(p.data,full.data,128u);
    p.bytes=128u+128u*count;
    for(unsigned i=0u;i<count;i++) {
        CHECK(indices[i]<full.count);uint8_t *entry=uf_entry(&p,i);
        memcpy(entry,uf_entry(&full,indices[i]),128u);
        if((inherited&(UINT64_C(1)<<i)) || entry[120u]==KUI_UPDATE_INHERIT_ROOT) {
            uf_put64(entry+80u,0u);memset(entry+88u,0,32u);entry[120u]=KUI_UPDATE_INHERIT_ROOT;
        } else {
            memcpy(p.data+p.bytes,uf_file(&full,indices[i]),UF_FILE_BYTES);p.bytes+=UF_FILE_BYTES;
        }
    }
    uf_put32(p.data+16u,count);uf_put32(p.data+20u,128u*count);
    uf_put64(p.data+24u,p.bytes-128u-128u*count);uf_manifest_hash(&p);uf_free(&full);return p;
}
static void same_entry(const struct kui_update_entry *a,const struct kui_update_entry *b) {
    CHECK(!strcmp(a->path,b->path) && a->bytes==b->bytes && a->policy==b->policy &&
        a->payload_offset==b->payload_offset && !memcmp(a->sha256,b->sha256,32u));
}
static void subset_roundtrip(struct uf_package *wire,const unsigned *indices,uint64_t inherited) {
    struct kui_update_package package,roundtrip;CHECK(uf_decode(wire,&package)==KUI_UPDATE_OK);
    CHECK(package.count==wire->count && package.file_bytes==wire->bytes &&
        !memcmp(package.manifest_sha256,wire->data+64u,32u));
    uint64_t offset=128u+128u*wire->count;
    for(unsigned i=0u;i<wire->count;i++) {
        const struct kui_update_entry *entry=package.entries+i;
        CHECK(!strcmp(entry->path,kui_update_catalog_path(indices[i])) && entry->payload_offset==offset);
        bool inherited_entry=(inherited&(UINT64_C(1)<<i)) || strstr(entry->path,".bin")!=NULL;
        CHECK(entry->bytes==(inherited_entry?0u:UF_FILE_BYTES));
        CHECK(entry->policy==(inherited_entry?KUI_UPDATE_INHERIT_ROOT:KUI_UPDATE_REPLACE));
        CHECK(!memcmp(entry->sha256,uf_entry(wire,i)+88u,32u));
        if(entry->bytes) {
            uint8_t digest[32];uf_hash(wire->data+(size_t)offset,(size_t)entry->bytes,digest);
            CHECK(!memcmp(digest,entry->sha256,32u));
        }
        CHECK(kui_update_package_find(&package,entry->path)==entry);offset+=entry->bytes;
    }
    CHECK(offset==wire->bytes && package.payload_bytes==offset-128u-128u*wire->count);
    uint8_t receipt[KUI_UPDATE_RECEIPT_MAX];size_t bytes=0u;
    CHECK(kui_update_receipt_encode(&package,receipt,sizeof(receipt),&bytes)==KUI_UPDATE_OK);
    CHECK(bytes==128u+128u*wire->count && !memcmp(receipt,wire->data,bytes));
    CHECK(kui_update_header_decode(receipt,wire->bytes,&roundtrip)==KUI_UPDATE_OK);
    CHECK(kui_update_manifest_decode(&roundtrip,receipt+128u,bytes-128u)==KUI_UPDATE_OK);
    CHECK(roundtrip.count==wire->count && roundtrip.file_bytes==wire->bytes &&
        roundtrip.payload_bytes==package.payload_bytes && !memcmp(roundtrip.manifest_sha256,package.manifest_sha256,32u));
    for(unsigned i=0u;i<package.count;i++)same_entry(package.entries+i,roundtrip.entries+i);
    /* Effective lookup copies declared metadata verbatim, and supplies empty
     * root inheritance for permitted roles absent from this wire cohort. */
    for(unsigned index=0u;index<kui_update_catalog_count();index++) {
        const char *path=kui_update_catalog_path(index);unsigned declared=0u;
        while(declared<wire->count && indices[declared]!=index)++declared;
        struct kui_update_entry member;memset(&member,0xa5,sizeof(member));
        CHECK(kui_update_package_member(&package,path,&member));
        if(declared<wire->count) {
            CHECK(kui_update_package_find(&package,path)==package.entries+declared);
            same_entry(&member,package.entries+declared);
        } else {
            uint8_t zero[32]={0};CHECK(!kui_update_package_find(&package,path));
            CHECK(!strcmp(member.path,path) && member.policy==KUI_UPDATE_INHERIT_ROOT && !member.bytes &&
                !member.payload_offset && !memcmp(member.sha256,zero,32u));
        }
    }
    struct kui_update_entry member;
    CHECK(kui_update_package_member(&package,"apps/games/not-compiled.kui",&member) &&
        member.policy==KUI_UPDATE_INHERIT_ROOT && !member.bytes && !strcmp(member.path,"apps/games/not-compiled.kui"));
    CHECK(!kui_update_package_member(&package,"../runtime.kui",&member));
    CHECK(!kui_update_package_member(&package,"0:/KUI/runtime.kui",&member));
    CHECK(!kui_update_package_member(NULL,"runtime.kui",&member));
    CHECK(!kui_update_package_member(&package,"runtime.kui",NULL));
    struct kui_update_entry retained=package.entries[0];
    CHECK(!kui_update_package_member(&package,"runtime.kui",package.entries));same_entry(&retained,package.entries);
    memset(&member,0,sizeof(member));strcpy(member.path,"runtime.kui");
    CHECK(!kui_update_package_member(&package,member.path,&member) && !strcmp(member.path,"runtime.kui"));
    CHECK(package.count==wire->count && package.file_bytes==wire->bytes &&
        !memcmp(package.manifest_sha256,wire->data+64u,32u));
}
static void subset_cohorts(void) {
    unsigned count=(unsigned)kui_update_catalog_count();CHECK(count>=26u && count<=KUI_UPDATE_MAX_ENTRIES);
    unsigned old[26];for(unsigned i=0u;i<26u;i++)old[i]=i;
    struct uf_package wire=subset_build(old,26u,0u);subset_roundtrip(&wire,old,0u);uf_free(&wire);
    uint64_t old_inherited=(UINT64_C(1)<<26u)-2u;
    wire=subset_build(old,26u,old_inherited);subset_roundtrip(&wire,old,old_inherited);uf_free(&wire);
    const unsigned last=count-1u;
    const unsigned gui[]={0u},sparse2[]={0u,last},sparse3[]={0u,7u,last};
    wire=subset_build(gui,1u,0u);subset_roundtrip(&wire,gui,0u);uf_free(&wire);
    wire=subset_build(sparse2,2u,0u);subset_roundtrip(&wire,sparse2,0u);uf_free(&wire);
    wire=subset_build(sparse3,3u,0u);subset_roundtrip(&wire,sparse3,0u);uf_free(&wire);
    wire=subset_build(sparse3,3u,UINT64_C(2));subset_roundtrip(&wire,sparse3,UINT64_C(2));uf_free(&wire);
    const unsigned reordered[]={0u,last,7u};
    wire=subset_build(reordered,3u,0u);subset_roundtrip(&wire,reordered,0u);uf_free(&wire);
    const unsigned rejected[][3]={{1u,last,0u},{7u,0u,last},{0u,7u,7u},{0u,0u,last}};
    for(unsigned i=0u;i<sizeof(rejected)/sizeof(rejected[0]);i++) {
        unsigned count=i==0u?2u:3u;wire=subset_build(rejected[i],count,0u);struct kui_update_package decoded;
        CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    }
    static const char *const bad_paths[]={"apps/games/save.vmd","apps/games/../probe.kui","Runtime.kui"};
    for(unsigned i=0u;i<sizeof(bad_paths)/sizeof(bad_paths[0]);i++) {
        wire=subset_build(sparse2,2u,0u);memset(uf_entry(&wire,1u),0,80u);
        strcpy((char *)uf_entry(&wire,1u),bad_paths[i]);uf_manifest_hash(&wire);struct kui_update_package decoded;
        CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    }
    wire=subset_build(sparse2,2u,0u);uf_entry(&wire,1u)[79u]=1u;uf_manifest_hash(&wire);
    struct kui_update_package decoded;CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    wire=subset_build(gui,1u,UINT64_C(1));CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    for(unsigned kind=0u;kind<4u;kind++) {
        wire=subset_build(sparse2,2u,UINT64_C(2));uint8_t *entry=uf_entry(&wire,1u);
        if(kind==0u) {uf_put64(entry+80u,1u);++wire.bytes;uf_put64(wire.data+24u,UF_FILE_BYTES+1u);}
        if(kind==1u)entry[88u]=1u;
        if(kind==2u)entry[120u]=3u;
        if(kind==3u)entry[121u]=1u;
        uf_manifest_hash(&wire);CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    }
    /* Invalid in-memory duplicates cannot bypass receipt validation or obtain
     * effective root inheritance through the copy helper. */
    wire=subset_build(sparse3,3u,0u);CHECK(uf_decode(&wire,&decoded)==KUI_UPDATE_OK);
    decoded.entries[1]=decoded.entries[2];
    uint8_t receipt[KUI_UPDATE_RECEIPT_MAX];size_t bytes=0u;struct kui_update_entry member;
    CHECK(kui_update_receipt_encode(&decoded,receipt,sizeof(receipt),&bytes)!=KUI_UPDATE_OK);
    CHECK(!kui_update_package_member(&decoded,kui_update_catalog_path(2u),&member));
    CHECK(!kui_update_package_find(&decoded,"runtime.kui"));uf_free(&wire);
}
static struct uf_package path_package(const char *const *paths,unsigned count,uint64_t inherited) {
    struct uf_package full=uf_build("012345abcdef",1u),p={0};p.count=count;
    CHECK(count && count<=KUI_UPDATE_MAX_ENTRIES);
    p.data=calloc(1u,128u+(size_t)count*(128u+UF_FILE_BYTES));CHECK(p.data);
    memcpy(p.data,full.data,128u);p.bytes=128u+128u*count;
    for(unsigned i=0u;i<count;i++) {
        CHECK(strlen(paths[i])<KUI_UPDATE_PATH_BYTES);uint8_t *entry=uf_entry(&p,i);
        memcpy(entry,uf_entry(&full,i?1u:0u),128u);memset(entry,0,80u);strcpy((char *)entry,paths[i]);
        if(inherited&(UINT64_C(1)<<i)) {
            uf_put64(entry+80u,0u);memset(entry+88u,0,32u);entry[120u]=KUI_UPDATE_INHERIT_ROOT;
        } else {memcpy(p.data+p.bytes,uf_file(&full,i?1u:0u),UF_FILE_BYTES);p.bytes+=UF_FILE_BYTES;}
    }
    uf_put32(p.data+16u,count);uf_put32(p.data+20u,128u*count);
    uf_put64(p.data+24u,p.bytes-128u-128u*count);uf_manifest_hash(&p);uf_free(&full);return p;
}
static void namespace_paths(void) {
    static const char *const valid[]={"runtime.kui","apps/games/a.kui","apps/disc/0.kui",
        "apps/games/future-loader-v9.kui","apps/disc/native.v2-9_x.kui"};
    static const char *const invalid[]={"","Runtime.kui","runtime.kui/","apps/games/.a.kui","apps/games/-a.kui",
        "apps/games/_a.kui","apps/games/.kui","apps/games/A.kui","Apps/games/a.kui","apps/game/a.kui",
        "apps/discs/a.kui","0:/KUI/apps/games/a.kui","/apps/games/a.kui","apps/games/a.KUI",
        "apps/games/a.kui/","apps/games/a/b.kui","apps/disc/a\\b.kui","apps/games/a..b.kui","apps/games/a..kui",
        "apps/games/../a.kui","apps/games/a b.kui","apps/games/a\tb.kui","apps/games/a\nb.kui",
        "apps/games/a:b.kui","apps/games/a%b.kui","apps/games/\177.kui","KUI/runtime.kui","saves/a.kui"};
    CHECK(!kui_update_module_path_valid(NULL));
    for(unsigned i=0u;i<sizeof(valid)/sizeof(valid[0]);i++)CHECK(kui_update_module_path_valid(valid[i]));
    CHECK(kui_update_module_path_valid("apps/games/a.bin"));
    for(unsigned i=0u;i<sizeof(invalid)/sizeof(invalid[0]);i++)CHECK(!kui_update_module_path_valid(invalid[i]));
    char longest[81];memcpy(longest,"apps/games/",11u);memset(longest+11u,'a',64u);memcpy(longest+75u,".kui",5u);
    CHECK(strlen(longest)==79u && kui_update_module_path_valid(longest));
    memset(longest+11u,'a',65u);memcpy(longest+76u,".kui",5u);
    CHECK(strlen(longest)==80u && !kui_update_module_path_valid(longest));
    static const char *const raw[]={"apps/games/smash-vc2-vmu-helper.bin","apps/games/smash-swirl-vmu-helper.bin",
        "apps/games/vc2-vmu-helper.bin"};
    struct kui_update_package decoded;
    for(unsigned i=0u;i<3u;i++) {
        CHECK(kui_update_module_path_valid(raw[i]));
        const char *paths[]={"runtime.kui",raw[i]};struct uf_package wire=path_package(paths,2u,UINT64_C(2));
        CHECK(uf_decode(&wire,&decoded)==KUI_UPDATE_OK && decoded.entries[1].policy==KUI_UPDATE_INHERIT_ROOT);
        uf_free(&wire);wire=path_package(paths,2u,0u);CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    }
    /* A later 34-entry receipt must remain readable even though this branch
     * deliberately keeps main's 26-role default rather than installing new
     * launch families merely to satisfy package admission. */
    const char *expanded[34];
    for(unsigned i=0u;i<26u;i++)expanded[i]=kui_update_catalog_path(i);
    static const char *const later[]={"apps/games/retail-smash-menu.kui",
        "apps/games/retail-smash-native-menu.kui","apps/games/retail-smash-native.kui",
        "apps/games/retail-smash-vc2.kui","apps/games/retail-smash-swirl.kui",
        "apps/games/smash-vc2-vmu-helper.bin","apps/games/smash-swirl-vmu-helper.bin",
        "apps/games/vc2-vmu-helper.bin"};
    for(unsigned i=0u;i<8u;i++)expanded[26u+i]=later[i];
    struct uf_package later_wire=path_package(expanded,34u,
        (UINT64_C(1)<<31u)|(UINT64_C(1)<<32u)|(UINT64_C(1)<<33u));
    CHECK(uf_decode(&later_wire,&decoded)==KUI_UPDATE_OK && decoded.count==34u);
    uint8_t later_receipt[KUI_UPDATE_RECEIPT_MAX];size_t later_bytes=0u;
    CHECK(kui_update_receipt_encode(&decoded,later_receipt,sizeof(later_receipt),&later_bytes)==KUI_UPDATE_OK);
    CHECK(later_bytes==128u+34u*128u && !memcmp(later_receipt,later_wire.data,later_bytes));
    uf_free(&later_wire);
    /* New software roles have no relationship to the default inventory, and
     * declaration order after the GUI carries no catalog interpretation. */
    const char *paths[]={"runtime.kui","apps/games/z-future-v9.kui","apps/disc/new-bridge.kui","apps/games/a-future.kui"};
    struct uf_package wire=path_package(paths,4u,0u);CHECK(uf_decode(&wire,&decoded)==KUI_UPDATE_OK);
    uint8_t receipt[KUI_UPDATE_RECEIPT_MAX];size_t bytes=0u;
    CHECK(kui_update_receipt_encode(&decoded,receipt,sizeof(receipt),&bytes)==KUI_UPDATE_OK);
    CHECK(bytes==128u+4u*128u && !memcmp(receipt,wire.data,bytes));
    for(unsigned i=0u;i<4u;i++) {
        struct kui_update_entry member;CHECK(kui_update_package_member(&decoded,paths[i],&member));
        CHECK(kui_update_package_find(&decoded,paths[i])==decoded.entries+i);same_entry(&member,decoded.entries+i);
        CHECK(member.policy==KUI_UPDATE_REPLACE && member.bytes==UF_FILE_BYTES &&
            member.payload_offset==128u+4u*128u+(uint64_t)i*UF_FILE_BYTES);
    }
    struct kui_update_entry member;
    CHECK(!kui_update_package_find(&decoded,"apps/games/next-generation.kui"));
    CHECK(kui_update_package_member(&decoded,"apps/games/next-generation.kui",&member) &&
        member.policy==KUI_UPDATE_INHERIT_ROOT && !member.bytes);
    CHECK(kui_update_package_member(&decoded,"apps/games/unapproved-helper.bin",&member) &&
        member.policy==KUI_UPDATE_INHERIT_ROOT && !member.bytes);
    CHECK(!kui_update_package_member(&decoded,"apps/games/save.vmd",&member));
    CHECK(!kui_update_package_member(&decoded,"saves/a.kui",&member));
    uf_free(&wire);
    /* Prove the wire count is bounded by the format's 64 slots, rather than
     * by the roles present in this checkout's default package inventory. */
    char names[63][KUI_UPDATE_PATH_BYTES];const char *many[64]={"runtime.kui"};
    for(unsigned i=1u;i<64u;i++) {
        snprintf(names[i-1u],sizeof(names[i-1u]),"apps/games/future-%02u.kui",64u-i);many[i]=names[i-1u];
    }
    wire=path_package(many,64u,0u);CHECK(uf_decode(&wire,&decoded)==KUI_UPDATE_OK && decoded.count==64u);
    CHECK(kui_update_receipt_encode(&decoded,receipt,sizeof(receipt),&bytes)==KUI_UPDATE_OK && bytes==KUI_UPDATE_HEADER_BYTES+64u*KUI_UPDATE_ENTRY_BYTES);
    CHECK(!memcmp(receipt,wire.data,bytes));uf_free(&wire);
    for(unsigned i=0u;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        const char *bad[]={"runtime.kui",invalid[i]};wire=path_package(bad,2u,0u);
        CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    }
    const char *unknown_raw[]={"runtime.kui","apps/games/unapproved-helper.bin"};
    wire=path_package(unknown_raw,2u,UINT64_C(2));CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    const char *duplicates[]={"runtime.kui","apps/games/future.kui","apps/disc/bridge.kui","apps/games/future.kui"};
    wire=path_package(duplicates,4u,0u);CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    const char *padding[]={"runtime.kui","apps/games/future.kui"};
    wire=path_package(padding,2u,0u);uf_entry(&wire,1u)[79u]=1u;uf_manifest_hash(&wire);
    CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
    wire=path_package(padding,2u,0u);memset(uf_entry(&wire,1u),'a',80u);uf_manifest_hash(&wire);
    CHECK(uf_decode(&wire,&decoded)!=KUI_UPDATE_OK);uf_free(&wire);
}
int main(void) {package_codecs();capability_and_selection();legacy_policy();inherit_root_policy();withdrawn_releases();subset_cohorts();namespace_paths();printf("PASS offline updater codec: %u checks; bounded software namespace, preserved receipts, byte integrity, exact legacy/root-inheritance policies, withdrawal and selection CRC\n",checks);return 0;}
