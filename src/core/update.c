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
    if(entry->policy==KUI_UPDATE_SELF_KUI)return kui_update_build_valid(entry->build)?entry->build:NULL;
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
bool kui_update_build_withdrawn(const char *build) {
    return kui_update_build_valid(build) &&
        (!strcmp(build,"28cadccf0e71") || !strcmp(build,"33b04057db86"));
}
static size_t path_length(const char *path) {
    if(!span(path,KUI_UPDATE_PATH_BYTES))return SIZE_MAX;
    for(size_t i=0;i<KUI_UPDATE_PATH_BYTES;i++)if(!path[i])return i;
    return SIZE_MAX;
}
static bool raw_helper(const char *path) {
    return !strcmp(path,"apps/games/smash-vc2-vmu-helper.bin") ||
        !strcmp(path,"apps/games/smash-swirl-vmu-helper.bin") || !strcmp(path,"apps/games/vc2-vmu-helper.bin");
}
static bool lower_alnum(unsigned char c) {return (c>='a' && c<='z') || (c>='0' && c<='9');}
static bool legacy_path_valid(const char *path) {
    size_t bytes=path_length(path);if(bytes==SIZE_MAX || !bytes)return false;
    if(!strcmp(path,"runtime.kui") || raw_helper(path))return true;
    size_t prefix=bytes>11u && !memcmp(path,"apps/games/",11u)?11u:
        bytes>10u && !memcmp(path,"apps/disc/",10u)?10u:0u;
    if(!prefix || bytes-prefix<5u || memcmp(path+bytes-4u,".kui",4u))return false;
    const char *base=path+prefix;
    if(!lower_alnum((unsigned char)base[0]) || strstr(base,".."))return false;
    for(size_t i=0;i<bytes-prefix;i++)if(!lower_alnum((unsigned char)base[i]) &&
        base[i]!='.' && base[i]!='_' && base[i]!='-')return false;
    return true;
}
static bool safe_component(const char *p,size_t bytes) {
    if(!bytes || !lower_alnum((unsigned char)p[0]) || p[bytes-1u]=='.')return false;
    for(size_t i=0u;i<bytes;i++) {
        unsigned char c=(unsigned char)p[i];
        if(!lower_alnum(c) && c!='.' && c!='_' && c!='-')return false;
        if(c=='.' && i+1u<bytes && p[i+1u]=='.')return false;
    }
    return true;
}
bool kui_update_module_path_valid(const char *path) {
    size_t bytes=path_length(path);if(bytes==SIZE_MAX || !bytes)return false;
    if(!strcmp(path,"runtime.kui") || !strcmp(path,"tools.kui") || !strcmp(path,"recovery.kui") ||
       !strcmp(path,"redump.db") || !strcmp(path,"tosec.db") || !strcmp(path,"apps/games/probe.dat"))return true;
    size_t prefix=bytes>11u && !memcmp(path,"apps/games/",11u)?11u:
        bytes>10u && !memcmp(path,"apps/disc/",10u)?10u:
        bytes>11u && !memcmp(path,"apps/music/",11u)?11u:0u;
    if(prefix)return bytes-prefix>=5u &&
        (!memcmp(path+bytes-4u,".kui",4u) || !memcmp(path+bytes-4u,".bin",4u) ||
         (prefix==11u && !memcmp(path,"apps/music/",11u) &&
          (!memcmp(path+bytes-4u,".ogg",4u) || !memcmp(path+bytes-4u,".wav",4u)))) &&
        safe_component(path+prefix,bytes-prefix);
    if(bytes<=7u || memcmp(path,"assets/",7u))return false;
    size_t start=7u;unsigned components=0u;
    for(size_t i=start;i<=bytes;i++)if(i==bytes || path[i]=='/') {
        if(++components>4u || !safe_component(path+start,i-start))return false;
        start=i+1u;
    }
    return true;
}
bool kui_update_bank_path(const char *build,const char *relative,char *out,size_t bytes) {
    if(!span(out,bytes) || !bytes)return false;
    if((build && overlap(build,KUI_UPDATE_BUILD_BYTES,out,bytes)) ||
       (relative && overlap(relative,KUI_UPDATE_PATH_BYTES,out,bytes)))return false;
    out[0]=0;
    if(!kui_update_build_valid(build) || !kui_update_module_path_valid(relative))return false;
    size_t root=sizeof(KUI_UPDATE_SYSTEM)-1u,path=strlen(relative);
    size_t needed=root+1u+12u+1u+path+1u;
    if(bytes<needed)return false;
    memcpy(out,KUI_UPDATE_SYSTEM,root);out[root]='/';memcpy(out+root+1u,build,12u);
    out[root+13u]='/';memcpy(out+root+14u,relative,path+1u);return true;
}
uint32_t kui_update_package_version(const struct kui_update_package *p) {return p && p->version?p->version:1u;}
size_t kui_update_entry_bytes(const struct kui_update_package *p) {
    uint32_t version=kui_update_package_version(p);
    return version==1u?KUI_UPDATE_ENTRY_BYTES:version==2u?KUI_UPDATE_ENTRY_V2_BYTES:0u;
}
static uint32_t entry_limit(uint32_t version) {
    return version==1u?KUI_UPDATE_V1_MAX_ENTRIES:version==2u?KUI_UPDATE_MAX_ENTRIES:0u;
}
static bool flags_valid(uint32_t version,uint8_t flags) {
    return version==1u?!flags:version==2u && !(flags&~KUI_UPDATE_FULL);
}
static bool scalar_valid(const struct kui_update_package *p) {
    size_t entry_bytes=kui_update_entry_bytes(p);
    return entry_bytes && flags_valid(kui_update_package_version(p),p->flags) && kui_update_build_valid(p->build) &&
        (kui_update_package_version(p)!=1u || !kui_update_build_withdrawn(p->build)) && p->channel<=1u && p->count &&
        p->count<=entry_limit(kui_update_package_version(p)) && p->payload_bytes<=KUI_UPDATE_MAX_PAYLOAD &&
        p->file_bytes==KUI_UPDATE_HEADER_BYTES+(uint64_t)p->count*entry_bytes+p->payload_bytes;
}
enum kui_update_result kui_update_header_decode(const uint8_t header[128],uint64_t file_bytes,
    struct kui_update_package *out) {
    if(!span(out,sizeof(*out)))return KUI_UPDATE_ARGUMENT;
    if(header && overlap(header,KUI_UPDATE_HEADER_BYTES,out,sizeof(*out)))return KUI_UPDATE_ARGUMENT;
    memset(out,0,sizeof(*out));
    if(!span(header,KUI_UPDATE_HEADER_BYTES))return KUI_UPDATE_ARGUMENT;
    uint32_t version=get32(header+8u);
    size_t entry_bytes=version==1u?KUI_UPDATE_ENTRY_BYTES:version==2u?KUI_UPDATE_ENTRY_V2_BYTES:0u;
    if(!entry_bytes || memcmp(header,version==1u?"KUIUPD1\0":"KUIUPD2\0",8u) || get32(header+12u)!=128u ||
       !flags_valid(version,header[45u]) || !zero(header+46u,18u) || !zero(header+96u,28u))return KUI_UPDATE_FORMAT;
    if(get32(header+124u)!=kui_crc32(0u,header,124u))return KUI_UPDATE_CHECKSUM;
    uint32_t count=get32(header+16u);
    if(version==2u && count>KUI_UPDATE_MAX_ENTRIES)return KUI_UPDATE_LIMIT;
    if(!count || count>entry_limit(version) || get32(header+20u)!=count*entry_bytes ||
       get64(header+24u)>KUI_UPDATE_MAX_PAYLOAD || header[44u]>1u)return KUI_UPDATE_FORMAT;
    char build[KUI_UPDATE_BUILD_BYTES]={0};memcpy(build,header+32u,12u);
    if(!kui_update_build_valid(build) || file_bytes!=128u+(uint64_t)count*entry_bytes+get64(header+24u))return KUI_UPDATE_FORMAT;
    if(version==1u && kui_update_build_withdrawn(build))return KUI_UPDATE_COHORT;
    memcpy(out->build,build,sizeof(build));out->channel=header[44u];out->flags=header[45u];out->count=count;
    out->version=version;out->payload_bytes=get64(header+24u);out->file_bytes=file_bytes;memcpy(out->manifest_sha256,header+64u,32u);
    return KUI_UPDATE_OK;
}
static bool canonical_path(const char *field,uint32_t version) {
    if(!(version==1u?legacy_path_valid(field):kui_update_module_path_valid(field)))return false;
    size_t n=strlen(field);
    return zero((const uint8_t *)field+n+1u,KUI_UPDATE_PATH_BYTES-n-1u);
}
static bool entry_valid(const struct kui_update_package *p,const struct kui_update_entry *entry) {
    if(kui_update_package_version(p)==2u) {
        if((p->flags&KUI_UPDATE_FULL) && entry->policy==KUI_UPDATE_KEEP)return false;
        if(entry->policy==KUI_UPDATE_KEEP || entry->policy==KUI_UPDATE_REMOVE)
            return (entry->policy!=KUI_UPDATE_REMOVE || strcmp(entry->path,"runtime.kui")) &&
                !entry->bytes && zero(entry->sha256,32u) && zero(entry->build,sizeof(entry->build));
        size_t bytes=strlen(entry->path);
        bool kui=bytes>=4u && !memcmp(entry->path+bytes-4u,".kui",4u);
        if(entry->policy==KUI_UPDATE_SELF_KUI)return kui && entry->bytes &&
            kui_update_build_valid(entry->build) &&
            (strcmp(entry->path,"runtime.kui") || !kui_update_build_withdrawn(entry->build));
        return entry->policy==KUI_UPDATE_RAW && !kui && !zero(entry->sha256,32u) &&
            zero(entry->build,sizeof(entry->build));
    }
    if(!zero(entry->build,sizeof(entry->build)))return false;
    if(entry->policy==KUI_UPDATE_INHERIT_ROOT)
        return strcmp(entry->path,"runtime.kui") && !entry->bytes && zero(entry->sha256,32u);
    return !raw_helper(entry->path) && entry->bytes && kui_update_entry_build(p->build,entry);
}
static bool full_gui_valid(const struct kui_update_package *p) {
    if(kui_update_package_version(p)!=2u || !(p->flags&KUI_UPDATE_FULL))return true;
    for(uint32_t i=0u;i<p->count;i++)if(!strcmp(p->entries[i].path,"runtime.kui"))
        return p->entries[i].policy==KUI_UPDATE_SELF_KUI;
    return false;
}
enum kui_update_result kui_update_manifest_decode(struct kui_update_package *package,const void *manifest,size_t bytes) {
    if(!span(package,sizeof(*package)))return KUI_UPDATE_ARGUMENT;
    if(manifest && overlap(manifest,bytes,package,sizeof(*package)))return KUI_UPDATE_ARGUMENT;
    if(!span(manifest,bytes)) {memset(package,0,sizeof(*package));return KUI_UPDATE_ARGUMENT;}
    struct kui_update_package *decoded=malloc(sizeof(*decoded));
    if(!decoded) {memset(package,0,sizeof(*package));return KUI_UPDATE_MEMORY;}
    memcpy(decoded,package,sizeof(*decoded));memset(package,0,sizeof(*package));
    memset(decoded->entries,0,sizeof(decoded->entries));enum kui_update_result result=KUI_UPDATE_FORMAT;
    size_t entry_bytes=kui_update_entry_bytes(decoded);uint32_t version=kui_update_package_version(decoded);
    if(!scalar_valid(decoded) || bytes!=(size_t)decoded->count*entry_bytes)goto done;
    uint8_t sha[32];digest(manifest,bytes,sha);
    if(memcmp(sha,decoded->manifest_sha256,32u)) {result=KUI_UPDATE_CHECKSUM;goto done;}
    uint64_t offset=128u+(uint64_t)bytes,total=0u;
    for(uint32_t i=0;i<decoded->count;i++) {
        const uint8_t *wire=(const uint8_t *)manifest+(size_t)i*entry_bytes;
        if(!canonical_path((const char *)wire,version) ||
           (version==1u && !i && strcmp((const char *)wire,"runtime.kui"))) {result=KUI_UPDATE_COHORT;goto done;}
        for(uint32_t j=0u;j<i;j++)if(!strcmp((const char *)wire,decoded->entries[j].path)) {result=KUI_UPDATE_COHORT;goto done;}
        uint64_t size=get64(wire+80u);
        if(wire[120u]>(version==1u?KUI_UPDATE_INHERIT_ROOT:KUI_UPDATE_REMOVE) ||
           !zero(wire+121u,7u) || (version==2u && !zero(wire+140u,20u)) || size>KUI_UPDATE_MAX_PAYLOAD-total)goto done;
        struct kui_update_entry *entry=decoded->entries+i;
        memcpy(entry->path,wire,strlen((const char *)wire)+1u);entry->bytes=size;
        memcpy(entry->sha256,wire+88u,32u);entry->payload_offset=offset;offset+=size;total+=size;
        entry->policy=wire[120u];
        if(version==2u)memcpy(entry->build,wire+128u,12u);
        if(!entry_valid(decoded,entry)) {result=KUI_UPDATE_COHORT;goto done;}
    }
    if(total!=decoded->payload_bytes || offset!=decoded->file_bytes)goto done;
    if(!full_gui_valid(decoded)) {result=KUI_UPDATE_RUNTIME;goto done;}
    memcpy(package,decoded,sizeof(*package));result=KUI_UPDATE_OK;
done:
    free(decoded);return result;
}
static enum kui_update_result full_valid(const struct kui_update_package *p) {
    if(!scalar_valid(p))return KUI_UPDATE_FORMAT;
    uint64_t total=0u;uint32_t version=kui_update_package_version(p);
    for(uint32_t i=0;i<p->count;i++) {
        if(!canonical_path(p->entries[i].path,version) ||
           (version==1u && !i && strcmp(p->entries[i].path,"runtime.kui")))return KUI_UPDATE_COHORT;
        for(uint32_t j=0u;j<i;j++)if(!strcmp(p->entries[i].path,p->entries[j].path))return KUI_UPDATE_COHORT;
        if(p->entries[i].bytes>KUI_UPDATE_MAX_PAYLOAD-total)return KUI_UPDATE_FORMAT;
        if(!entry_valid(p,p->entries+i))return KUI_UPDATE_COHORT;
        total+=p->entries[i].bytes;
    }
    if(total!=p->payload_bytes)return KUI_UPDATE_FORMAT;
    return full_gui_valid(p)?KUI_UPDATE_OK:KUI_UPDATE_RUNTIME;
}
static void entry_encode(const struct kui_update_package *p,const struct kui_update_entry *entry,uint8_t wire[160]) {
    memset(wire,0,KUI_UPDATE_ENTRY_V2_BYTES);memcpy(wire,entry->path,strlen(entry->path));
    put64(wire+80u,entry->bytes);memcpy(wire+88u,entry->sha256,32u);wire[120u]=entry->policy;
    if(kui_update_package_version(p)==2u)memcpy(wire+128u,entry->build,12u);
}
static void manifest_digest(const struct kui_update_package *p,uint8_t out[32]) {
    uint8_t wire[KUI_UPDATE_ENTRY_V2_BYTES];struct kui_sha256 hash;kui_sha256_init(&hash);
    for(uint32_t i=0u;i<p->count;i++) {
        entry_encode(p,p->entries+i,wire);kui_sha256_update(&hash,wire,kui_update_entry_bytes(p));
    }
    kui_sha256_digest(&hash,out);
}
enum kui_update_result kui_update_package_seal(struct kui_update_package *package) {
    if(!span(package,sizeof(*package)))return KUI_UPDATE_ARGUMENT;
    size_t entry_bytes=kui_update_entry_bytes(package);
    uint32_t version=kui_update_package_version(package);
    if(version==2u && package->count>KUI_UPDATE_MAX_ENTRIES)return KUI_UPDATE_LIMIT;
    if(!entry_bytes || !package->count || package->count>entry_limit(version))return KUI_UPDATE_FORMAT;
    struct kui_update_package *sealed=malloc(sizeof(*sealed));if(!sealed)return KUI_UPDATE_MEMORY;
    *sealed=*package;sealed->payload_bytes=0u;
    uint64_t offset=KUI_UPDATE_HEADER_BYTES+(uint64_t)sealed->count*entry_bytes;
    enum kui_update_result result=KUI_UPDATE_FORMAT;
    for(uint32_t i=0u;i<sealed->count;i++) {
        if(sealed->entries[i].bytes>KUI_UPDATE_MAX_PAYLOAD-sealed->payload_bytes)goto done;
        sealed->entries[i].payload_offset=offset;offset+=sealed->entries[i].bytes;
        sealed->payload_bytes+=sealed->entries[i].bytes;
    }
    sealed->file_bytes=offset;result=full_valid(sealed);
    if(result==KUI_UPDATE_OK) {manifest_digest(sealed,sealed->manifest_sha256);*package=*sealed;}
done:
    free(sealed);return result;
}
enum kui_update_result kui_update_receipt_encode(const struct kui_update_package *package,void *wire,
    size_t capacity,size_t *written) {
    if(!span(written,sizeof(*written)) || !span(package,sizeof(*package)) || !span(wire,capacity) ||
       overlap(written,sizeof(*written),package,sizeof(*package)) || overlap(written,sizeof(*written),wire,capacity) ||
       overlap(package,sizeof(*package),wire,capacity))return KUI_UPDATE_ARGUMENT;
    *written=0u;enum kui_update_result result=full_valid(package);if(result!=KUI_UPDATE_OK)return result;
    size_t entry_bytes=kui_update_entry_bytes(package);
    size_t bytes=128u+(size_t)package->count*entry_bytes;if(capacity<bytes)return KUI_UPDATE_ARGUMENT;
    uint8_t *encoded=calloc(1u,bytes);if(!encoded)return KUI_UPDATE_MEMORY;
    for(uint32_t i=0;i<package->count;i++) {
        uint8_t entry[KUI_UPDATE_ENTRY_V2_BYTES];entry_encode(package,package->entries+i,entry);
        memcpy(encoded+128u+(size_t)i*entry_bytes,entry,entry_bytes);
    }
    uint8_t sha[32];digest(encoded+128u,bytes-128u,sha);
    if(memcmp(sha,package->manifest_sha256,32u)) {free(encoded);return KUI_UPDATE_CHECKSUM;}
    uint32_t version=kui_update_package_version(package);
    memcpy(encoded,version==1u?"KUIUPD1\0":"KUIUPD2\0",8u);put32(encoded+8u,version);put32(encoded+12u,128u);
    put32(encoded+16u,package->count);put32(encoded+20u,(uint32_t)(bytes-128u));put64(encoded+24u,package->payload_bytes);
    memcpy(encoded+32u,package->build,12u);encoded[44u]=package->channel;encoded[45u]=package->flags;
    memcpy(encoded+64u,sha,32u);
    put32(encoded+124u,kui_crc32(0u,encoded,124u));memcpy(wire,encoded,bytes);*written=bytes;free(encoded);return KUI_UPDATE_OK;
}
void kui_update_package_pin(const struct kui_update_package *package,struct kui_update_pin *out) {
    if(!span(out,sizeof(*out)) || (package && overlap(package,sizeof(*package),out,sizeof(*out))))return;
    memset(out,0,sizeof(*out));if(!span(package,sizeof(*package)) || !scalar_valid(package))return;
    memcpy(out->build,package->build,sizeof(out->build));out->channel=package->channel;out->flags=package->flags;
    memcpy(out->manifest_sha256,package->manifest_sha256,32u);out->file_bytes=package->file_bytes;
    out->version=kui_update_package_version(package);
}
bool kui_update_pin_matches(const struct kui_update_pin *pin,const struct kui_update_package *package) {
    return span(pin,sizeof(*pin)) && span(package,sizeof(*package)) && scalar_valid(package) &&
        kui_update_build_valid(pin->build) && pin->channel<=1u && !strcmp(pin->build,package->build) &&
        (pin->version?pin->version:1u)==kui_update_package_version(package) &&
        pin->channel==package->channel && pin->flags==package->flags && pin->file_bytes==package->file_bytes &&
        !memcmp(pin->manifest_sha256,package->manifest_sha256,32u);
}
const struct kui_update_entry *kui_update_package_find(const struct kui_update_package *package,const char *relative) {
    if(!span(package,sizeof(*package)) || !kui_update_module_path_valid(relative) || full_valid(package)!=KUI_UPDATE_OK)return NULL;
    for(uint32_t i=0u;i<package->count;i++)if(!strcmp(package->entries[i].path,relative))return package->entries+i;
    return NULL;
}
bool kui_update_package_member(const struct kui_update_package *package,const char *relative,
    struct kui_update_entry *out) {
    if(!span(out,sizeof(*out)) || !span(package,sizeof(*package)) ||
       overlap(out,sizeof(*out),package,sizeof(*package)) ||
       (relative && overlap(out,sizeof(*out),relative,KUI_UPDATE_PATH_BYTES)))return false;
    memset(out,0,sizeof(*out));
    if(!kui_update_module_path_valid(relative) || full_valid(package)!=KUI_UPDATE_OK)return false;
    for(uint32_t i=0u;i<package->count;i++)if(!strcmp(package->entries[i].path,relative)) {
        *out=package->entries[i];return true;
    }
    /* A FULL snapshot must never expose software omitted from that release.
     * Overlay GUI omission is materialized by the installer from effective current. */
    memcpy(out->path,relative,strlen(relative)+1u);
    out->policy=kui_update_package_version(package)==2u && (package->flags&KUI_UPDATE_FULL)?
        KUI_UPDATE_REMOVE:KUI_UPDATE_INHERIT_ROOT;
    return true;
}
bool kui_update_capability_valid(const void *payload,size_t bytes) {
    if(!span(payload,bytes) || bytes<KUI_UPDATE_CAPABILITY_BYTES)return false;
    const uint8_t *p=payload;unsigned found=0u;
    for(size_t i=0;i<=bytes-KUI_UPDATE_CAPABILITY_BYTES;i++)
        if(!memcmp(p+i,"KUIBANK1",8u) && get32(p+i+8u)==1u && get32(p+i+12u)==UINT32_C(0xfffffffe))
            if(++found!=1u)return false;
    return found==1u;
}
unsigned kui_update_engine_level(const void *payload,size_t bytes) {
    if(!span(payload,bytes) || bytes<KUI_UPDATE_ENGINE_BYTES)return 0u;
    const uint8_t *p=payload;unsigned found=0u;
    for(size_t i=0u;i<=bytes-KUI_UPDATE_ENGINE_BYTES;i++)
        if(!memcmp(p+i,"KUIENG2!",8u) && get32(p+i+8u)==2u && get32(p+i+12u)==UINT32_C(0xfffffffd))
            if(++found!=1u)return 0u;
    return found==1u?2u:0u;
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
        "Update checksum mismatch","Update module list incompatible","Invalid runtime module","Update storage I/O failed",
        "Update stopped","Not enough free space","Update changed; inspect it again","Selected or previous bank is protected",
        "Not enough memory","Activation sequence exhausted","Upgrade the root K-UI updater engine first",
        "Too many software files for this updater"};
    return (unsigned)result<sizeof(names)/sizeof(names[0])?names[result]:"Unknown update result";
}
