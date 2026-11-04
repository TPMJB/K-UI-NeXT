/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update.h"
#include "kui/hash.h"
#include <stdlib.h>
#include <string.h>

static const char *const catalog[]={
#define KUI_UPDATE_MEMBER(path) path,
#include "kui/update_catalog.inc"
#undef KUI_UPDATE_MEMBER
};
static const struct kui_update_legacy legacy[]={
#define KUI_UPDATE_LEGACY(path,build,bytes,sha) {path,build,sha,bytes},
#include "kui/update_legacy.inc"
#undef KUI_UPDATE_LEGACY
};
_Static_assert(sizeof(catalog)/sizeof(catalog[0])<=KUI_UPDATE_MAX_ENTRIES,"Update catalog bound");
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static uint64_t get64(const uint8_t *p) {return get32(p)|(uint64_t)get32(p+4u)<<32;}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(value>>(8u*i));
}
static void put64(uint8_t *p,uint64_t value) {put32(p,(uint32_t)value);put32(p+4u,(uint32_t)(value>>32));}
static bool span(const void *p,size_t bytes) {return p && bytes<=UINTPTR_MAX-(uintptr_t)p;}
static bool overlap(const void *a,size_t an,const void *b,size_t bn) {
    if(!span(a,an) || !span(b,bn))return true;
    uintptr_t aa=(uintptr_t)a,bb=(uintptr_t)b;
    if(aa<bb+bn && bb<aa+an)return true;
#if UINTPTR_MAX>UINT32_MAX
    if(aa>UINT32_MAX || bb>UINT32_MAX)return false;
#endif
    /* Cached and uncached SH aliases still name the same input bytes. */
    uint32_t ap=(uint32_t)aa&UINT32_C(0x1fffffff),bp=(uint32_t)bb&UINT32_C(0x1fffffff);
    uint32_t ar=(uint32_t)aa&UINT32_C(0xe0000000),br=(uint32_t)bb&UINT32_C(0xe0000000);
    return (ar==0u || ar==UINT32_C(0x80000000) || ar==UINT32_C(0xa0000000)) &&
        (br==0u || br==UINT32_C(0x80000000) || br==UINT32_C(0xa0000000)) &&
        ap>=UINT32_C(0x0c000000) && ap<UINT32_C(0x0d000000) &&
        bp>=UINT32_C(0x0c000000) && bp<UINT32_C(0x0d000000) &&
        an<=UINT32_C(0x0d000000)-ap && bn<=UINT32_C(0x0d000000)-bp && ap<bp+bn && bp<ap+an;
}
static bool zero(const void *p,size_t bytes) {
    const uint8_t *at=p;for(size_t i=0;i<bytes;i++)if(at[i])return false;return true;
}
static void digest(const void *p,size_t bytes,uint8_t out[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,p,bytes);kui_sha256_digest(&hash,out);
}
size_t kui_update_catalog_count(void) {return sizeof(catalog)/sizeof(catalog[0]);}
const char *kui_update_catalog_path(size_t index) {return index<kui_update_catalog_count()?catalog[index]:NULL;}
const struct kui_update_legacy *kui_update_legacy_for_path(const char *path) {
    if(!path)return NULL;
    for(size_t i=0;i<sizeof(legacy)/sizeof(legacy[0]);i++)if(!strcmp(path,legacy[i].path))return legacy+i;
    return NULL;
}
const char *kui_update_entry_build(const char *build,const struct kui_update_entry *entry) {
    if(!entry || !kui_update_build_valid(build))return NULL;
    if(!entry->policy)return build;
    if(entry->policy!=1u)return NULL;
    const struct kui_update_legacy *pin=kui_update_legacy_for_path(entry->path);
    if(!pin || entry->bytes!=pin->bytes)return NULL;
    static const char hex[]="0123456789abcdef";
    for(unsigned i=0;i<32u;i++)if(pin->sha256[2u*i]!=hex[entry->sha256[i]>>4u] ||
        pin->sha256[2u*i+1u]!=hex[entry->sha256[i]&15u])return NULL;
    return pin->build;
}
bool kui_update_build_valid(const char *build) {
    if(!span(build,KUI_UPDATE_BUILD_BYTES))return false;
    for(unsigned i=0;i<12u;i++)if(!((build[i]>='0' && build[i]<='9') || (build[i]>='a' && build[i]<='f')))return false;
    return !build[12u];
}
static size_t catalog_index(const char *path) {
    if(!span(path,KUI_UPDATE_PATH_BYTES))return SIZE_MAX;
    for(size_t i=0;i<kui_update_catalog_count();i++)if(!strcmp(path,catalog[i]))return i;
    return SIZE_MAX;
}
bool kui_update_bank_path(const char *build,const char *relative,char *out,size_t bytes) {
    if(!span(out,bytes) || !bytes)return false;
    if((build && overlap(build,KUI_UPDATE_BUILD_BYTES,out,bytes)) ||
       (relative && overlap(relative,KUI_UPDATE_PATH_BYTES,out,bytes)))return false;
    out[0]=0;
    size_t index=catalog_index(relative);
    if(!kui_update_build_valid(build) || index==SIZE_MAX)return false;
    size_t root=sizeof(KUI_UPDATE_SYSTEM)-1u,path=strlen(catalog[index]);
    size_t needed=root+1u+12u+1u+path+1u;
    if(bytes<needed)return false;
    memcpy(out,KUI_UPDATE_SYSTEM,root);out[root]='/';memcpy(out+root+1u,build,12u);
    out[root+13u]='/';memcpy(out+root+14u,catalog[index],path+1u);return true;
}
static bool scalar_valid(const struct kui_update_package *p) {
    return kui_update_build_valid(p->build) && p->channel<=1u && p->count &&
        p->count<=KUI_UPDATE_MAX_ENTRIES && p->payload_bytes<=KUI_UPDATE_MAX_PAYLOAD &&
        p->file_bytes==KUI_UPDATE_HEADER_BYTES+(uint64_t)p->count*KUI_UPDATE_ENTRY_BYTES+p->payload_bytes;
}
enum kui_update_result kui_update_header_decode(const uint8_t header[128],uint64_t file_bytes,
    struct kui_update_package *out) {
    if(!span(out,sizeof(*out)))return KUI_UPDATE_ARGUMENT;
    if(header && overlap(header,KUI_UPDATE_HEADER_BYTES,out,sizeof(*out)))return KUI_UPDATE_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!span(header,KUI_UPDATE_HEADER_BYTES))return KUI_UPDATE_ARGUMENT;
    if(memcmp(header,"KUIUPD1\0",8u) || get32(header+8u)!=1u || get32(header+12u)!=128u ||
       !zero(header+45u,19u) || !zero(header+96u,28u))return KUI_UPDATE_FORMAT;
    if(get32(header+124u)!=kui_crc32(0u,header,124u))return KUI_UPDATE_CHECKSUM;
    uint32_t count=get32(header+16u);
    if(!count || count>KUI_UPDATE_MAX_ENTRIES || get32(header+20u)!=count*KUI_UPDATE_ENTRY_BYTES ||
       get64(header+24u)>KUI_UPDATE_MAX_PAYLOAD || header[44u]>1u)return KUI_UPDATE_FORMAT;
    char build[KUI_UPDATE_BUILD_BYTES]={0};memcpy(build,header+32u,12u);
    if(!kui_update_build_valid(build) || file_bytes!=128u+(uint64_t)count*128u+get64(header+24u))return KUI_UPDATE_FORMAT;
    memcpy(out->build,build,sizeof(build));out->channel=header[44u];out->count=count;
    out->payload_bytes=get64(header+24u);out->file_bytes=file_bytes;memcpy(out->manifest_sha256,header+64u,32u);
    return KUI_UPDATE_OK;
}
static bool canonical_path(const void *field,const char *path) {
    size_t n=strlen(path);return n<KUI_UPDATE_PATH_BYTES && !memcmp(field,path,n+1u) &&
        zero((const uint8_t *)field+n+1u,KUI_UPDATE_PATH_BYTES-n-1u);
}
enum kui_update_result kui_update_manifest_decode(struct kui_update_package *package,const void *manifest,size_t bytes) {
    if(!span(package,sizeof(*package)))return KUI_UPDATE_ARGUMENT;
    if(manifest && overlap(manifest,bytes,package,sizeof(*package)))return KUI_UPDATE_ARGUMENT;
    if(!span(manifest,bytes)) {memset(package,0,sizeof(*package));return KUI_UPDATE_ARGUMENT;}
    struct kui_update_package *decoded=malloc(sizeof(*decoded));
    if(!decoded) {memset(package,0,sizeof(*package));return KUI_UPDATE_MEMORY;}
    memcpy(decoded,package,sizeof(*decoded));memset(package,0,sizeof(*package));
    memset(decoded->entries,0,sizeof(decoded->entries));enum kui_update_result result=KUI_UPDATE_FORMAT;
    if(!scalar_valid(decoded) || bytes!=(size_t)decoded->count*KUI_UPDATE_ENTRY_BYTES)goto done;
    if(decoded->count!=kui_update_catalog_count()) {result=KUI_UPDATE_COHORT;goto done;}
    uint8_t sha[32];digest(manifest,bytes,sha);
    if(memcmp(sha,decoded->manifest_sha256,32u)) {result=KUI_UPDATE_CHECKSUM;goto done;}
    uint64_t offset=128u+(uint64_t)bytes,total=0u;
    for(uint32_t i=0;i<decoded->count;i++) {
        const uint8_t *wire=(const uint8_t *)manifest+(size_t)i*128u;
        if(!canonical_path(wire,catalog[i])) {result=KUI_UPDATE_COHORT;goto done;}
        uint64_t size=get64(wire+80u);
        if(wire[120u]>1u || !zero(wire+121u,7u) || !size || size>KUI_UPDATE_MAX_PAYLOAD-total)goto done;
        struct kui_update_entry *entry=decoded->entries+i;
        memcpy(entry->path,catalog[i],strlen(catalog[i])+1u);entry->bytes=size;
        memcpy(entry->sha256,wire+88u,32u);entry->payload_offset=offset;offset+=size;total+=size;
        entry->policy=wire[120u];
        if(!kui_update_entry_build(decoded->build,entry)) {result=KUI_UPDATE_COHORT;goto done;}
    }
    if(total!=decoded->payload_bytes || offset!=decoded->file_bytes)goto done;
    memcpy(package,decoded,sizeof(*package));result=KUI_UPDATE_OK;
done:
    free(decoded);return result;
}
static enum kui_update_result full_valid(const struct kui_update_package *p) {
    if(!scalar_valid(p))return KUI_UPDATE_FORMAT;
    if(p->count!=kui_update_catalog_count())return KUI_UPDATE_COHORT;
    uint64_t total=0u;
    for(uint32_t i=0;i<p->count;i++) {
        if(!canonical_path(p->entries[i].path,catalog[i]))return KUI_UPDATE_COHORT;
        if(!p->entries[i].bytes || p->entries[i].bytes>KUI_UPDATE_MAX_PAYLOAD-total)return KUI_UPDATE_FORMAT;
        if(!kui_update_entry_build(p->build,p->entries+i))return KUI_UPDATE_COHORT;
        total+=p->entries[i].bytes;
    }
    return total==p->payload_bytes?KUI_UPDATE_OK:KUI_UPDATE_FORMAT;
}
enum kui_update_result kui_update_receipt_encode(const struct kui_update_package *package,void *wire,
    size_t capacity,size_t *written) {
    if(!span(written,sizeof(*written)) || !span(package,sizeof(*package)) || !span(wire,capacity) ||
       overlap(written,sizeof(*written),package,sizeof(*package)) || overlap(written,sizeof(*written),wire,capacity) ||
       overlap(package,sizeof(*package),wire,capacity))return KUI_UPDATE_ARGUMENT;
    *written=0u;enum kui_update_result result=full_valid(package);if(result!=KUI_UPDATE_OK)return result;
    size_t bytes=128u+(size_t)package->count*128u;if(capacity<bytes)return KUI_UPDATE_ARGUMENT;
    uint8_t *encoded=calloc(1u,bytes);if(!encoded)return KUI_UPDATE_MEMORY;
    for(uint32_t i=0;i<package->count;i++) {
        uint8_t *entry=encoded+128u+(size_t)i*128u;
        memcpy(entry,package->entries[i].path,strlen(catalog[i]));put64(entry+80u,package->entries[i].bytes);
        memcpy(entry+88u,package->entries[i].sha256,32u);
        entry[120u]=package->entries[i].policy;
    }
    uint8_t sha[32];digest(encoded+128u,bytes-128u,sha);
    if(memcmp(sha,package->manifest_sha256,32u)) {free(encoded);return KUI_UPDATE_CHECKSUM;}
    memcpy(encoded,"KUIUPD1\0",8u);put32(encoded+8u,1u);put32(encoded+12u,128u);
    put32(encoded+16u,package->count);put32(encoded+20u,(uint32_t)(bytes-128u));put64(encoded+24u,package->payload_bytes);
    memcpy(encoded+32u,package->build,12u);encoded[44u]=package->channel;memcpy(encoded+64u,sha,32u);
    put32(encoded+124u,kui_crc32(0u,encoded,124u));memcpy(wire,encoded,bytes);*written=bytes;free(encoded);return KUI_UPDATE_OK;
}
void kui_update_package_pin(const struct kui_update_package *package,struct kui_update_pin *out) {
    if(!span(out,sizeof(*out)) || (package && overlap(package,sizeof(*package),out,sizeof(*out))))return;
    memset(out,0,sizeof(*out));if(!span(package,sizeof(*package)) || !scalar_valid(package))return;
    memcpy(out->build,package->build,sizeof(out->build));out->channel=package->channel;
    memcpy(out->manifest_sha256,package->manifest_sha256,32u);out->file_bytes=package->file_bytes;
}
bool kui_update_pin_matches(const struct kui_update_pin *pin,const struct kui_update_package *package) {
    return span(pin,sizeof(*pin)) && span(package,sizeof(*package)) && scalar_valid(package) &&
        kui_update_build_valid(pin->build) && pin->channel<=1u && !strcmp(pin->build,package->build) &&
        pin->channel==package->channel && pin->file_bytes==package->file_bytes &&
        !memcmp(pin->manifest_sha256,package->manifest_sha256,32u);
}
const struct kui_update_entry *kui_update_package_find(const struct kui_update_package *package,const char *relative) {
    size_t index=catalog_index(relative);
    if(!span(package,sizeof(*package)) || index==SIZE_MAX || full_valid(package)!=KUI_UPDATE_OK)return NULL;
    return package->entries+index;
}
bool kui_update_capability_valid(const void *payload,size_t bytes) {
    if(!span(payload,bytes) || bytes<KUI_UPDATE_CAPABILITY_BYTES)return false;
    const uint8_t *p=payload;unsigned found=0u;
    for(size_t i=0;i<=bytes-KUI_UPDATE_CAPABILITY_BYTES;i++)
        if(!memcmp(p+i,"KUIBANK1",8u) && get32(p+i+8u)==1u && get32(p+i+12u)==UINT32_C(0xfffffffe))
            if(++found!=1u)return false;
    return found==1u;
}
static bool activation_valid(const struct kui_update_activation *a) {
    if(!a->sequence || (a->selected[0] && !kui_update_build_valid(a->selected)) ||
       (a->previous[0] && !kui_update_build_valid(a->previous)))return false;
    if(!a->selected[0] && !zero(a->manifest_sha256,32u))return false;
    return !a->selected[0] || !a->previous[0] || strcmp(a->selected,a->previous);
}
bool kui_update_activation_encode(const struct kui_update_activation *activation,uint8_t wire[128]) {
    if(!span(wire,128u) || (activation && overlap(activation,sizeof(*activation),wire,128u)))return false;
    memset(wire,0,128u);if(!span(activation,sizeof(*activation)) || !activation_valid(activation))return false;
    memcpy(wire,"KUIACT1\0",8u);put32(wire+8u,1u);put32(wire+12u,128u);put64(wire+16u,activation->sequence);
    if(activation->selected[0])memcpy(wire+24u,activation->selected,12u);
    if(activation->previous[0])memcpy(wire+36u,activation->previous,12u);
    memcpy(wire+48u,activation->manifest_sha256,32u);put32(wire+124u,kui_crc32(0u,wire,124u));return true;
}
bool kui_update_activation_decode(const uint8_t wire[128],struct kui_update_activation *out) {
    if(!span(out,sizeof(*out)) || (wire && overlap(wire,128u,out,sizeof(*out))))return false;
    memset(out,0,sizeof(*out));if(!span(wire,128u))return false;
    if(memcmp(wire,"KUIACT1\0",8u) || get32(wire+8u)!=1u || get32(wire+12u)!=128u ||
       !zero(wire+80u,44u) || get32(wire+124u)!=kui_crc32(0u,wire,124u))return false;
    struct kui_update_activation decoded={.sequence=get64(wire+16u)};
    memcpy(decoded.selected,wire+24u,12u);memcpy(decoded.previous,wire+36u,12u);memcpy(decoded.manifest_sha256,wire+48u,32u);
    if((!decoded.selected[0] && !zero(wire+24u,12u)) || (!decoded.previous[0] && !zero(wire+36u,12u)) ||
       !activation_valid(&decoded))return false;
    *out=decoded;return true;
}
const char *kui_update_result_name(enum kui_update_result result) {
    static const char *const names[]={"OK","Invalid update request","Update not found","Invalid update format",
        "Update checksum mismatch","Incomplete update cohort","Invalid runtime module","Update storage I/O failed",
        "Update stopped","Not enough free space","Update changed; inspect it again","Selected or previous bank is protected",
        "Not enough memory","Activation sequence exhausted"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"Unknown update result";
}
