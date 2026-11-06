/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/runtime_module_bundle.h"
#include "kui/hash.h"
#include "miniz.h"
#include <stdlib.h>
#include <string.h>
extern const uint8_t __kui_runtime_module_bundle_start[]
    __asm__("__kui_runtime_module_bundle_start") __attribute__((weak));
extern const uint8_t __kui_runtime_module_bundle_end[]
    __asm__("__kui_runtime_module_bundle_end") __attribute__((weak));
enum {HEADER=96,ENTRY=128,MAX_BUNDLE=4*1024*1024};
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool zeros(const uint8_t *p,size_t n) {while(n--)if(*p++)return false;return true;}
static void digest(const void *data,size_t n,uint8_t out[32]) {
    struct kui_sha256 sha;kui_sha256_init(&sha);kui_sha256_update(&sha,data,n);kui_sha256_digest(&sha,out);
}
static bool path_valid(const char *path) {
    size_t n=0u;while(n<80u && path[n])++n;
    if(n==80u || n<15u || memcmp(path+n-4u,".kui",4u) || strstr(path,".."))return false;
    size_t prefix=!strncmp(path,"apps/games/",11u)?11u:!strncmp(path,"apps/disc/",10u)?10u:0u;
    if(!prefix || n<=prefix+4u || !((path[prefix]>='a' && path[prefix]<='z') ||
       (path[prefix]>='0' && path[prefix]<='9')))return false;
    for(size_t i=prefix;i<n;i++)if(!((path[i]>='a' && path[i]<='z') ||
       (path[i]>='0' && path[i]<='9') || path[i]=='_' || path[i]=='-' || path[i]=='.'))return false;
    return zeros((const uint8_t *)path+n+1u,79u-n);
}
/* Returns -1 for damage, 0 absent, 1 a unique canonical member. */
static int find(const uint8_t *p,size_t n,const char *path,const uint8_t **entry) {
    if(!p || n<HEADER || n>MAX_BUNDLE || memcmp(p,"KUIMB01\0",8u) ||
       word(p+8u)!=1u || word(p+12u)!=HEADER || !word(p+16u) || word(p+16u)>64u ||
       word(p+20u)!=ENTRY || word(p+24u)!=n || word(p+28u) || !zeros(p+44u,20u))return -1;
    for(unsigned i=32u;i<44u;i++)if(!((p[i]>='0' && p[i]<='9') || (p[i]>='a' && p[i]<='f')))return -1;
    uint32_t count=word(p+16u);size_t offset=HEADER+(size_t)count*ENTRY;
    if(offset>n)return -1;
    uint8_t sha[32];digest(p+HEADER,(size_t)count*ENTRY,sha);
    if(memcmp(sha,p+64u,32u))return -1;
    const char *relative=path && !strncmp(path,"0:/KUI/",7u)?path+7u:NULL;
    int found=0;const uint8_t *previous=NULL;
    for(uint32_t i=0u;i<count;i++) {
        const uint8_t *e=p+HEADER+(size_t)i*ENTRY;
        uint32_t raw=word(e+80u),compressed=word(e+84u);
        if(!path_valid((const char *)e) || (previous && strcmp((const char *)previous,(const char *)e)>=0) ||
           raw<68u || raw>KUI_RUNTIME_HEADER_BYTES+KUI_RUNTIME_MAX_BYTES || !compressed ||
           word(e+88u)!=offset || word(e+92u) || compressed>n-offset)return -1;
        if(relative && !strcmp(relative,(const char *)e)) {*entry=e;found=1;}
        offset+=compressed;previous=e;
    }
    return offset==n?found:-1;
}
static bool linked(const uint8_t **p,size_t *n) {
    uintptr_t begin=(uintptr_t)__kui_runtime_module_bundle_start,end=(uintptr_t)__kui_runtime_module_bundle_end;
    if(!begin || end<=begin)return false;
    *p=(const uint8_t *)begin;*n=end-begin;return true;
}
bool kui_runtime_module_bundle_contains(const char *path) {
    const uint8_t *p,*entry=NULL;size_t n;
    return linked(&p,&n) && find(p,n,path,&entry)!=0;
}
bool kui_runtime_module_bundle_claims_view(const void *bundle,size_t bytes,const char *path) {
    const uint8_t *entry=NULL;
    return find(bundle,bytes,path,&entry)!=0;
}
enum kui_runtime_result kui_runtime_module_bundle_read_view(const void *bundle,size_t bytes,
    const char *path,struct kui_runtime_image *out,kui_cancel_fn cancel,bool *handled) {
    if(!out || !handled)return KUI_RUNTIME_IO;
    *out=(struct kui_runtime_image){0};*handled=false;
    const uint8_t *p=bundle,*entry=NULL;int found=find(p,bytes,path,&entry);
    if(!found)return KUI_RUNTIME_IO;
    *handled=true;
    if(found<0)return KUI_RUNTIME_CHECKSUM;
    if(cancel && cancel())return KUI_RUNTIME_CANCELLED;
    size_t raw=word(entry+80u),compressed=word(entry+84u),source=compressed,destination=raw;
    uint8_t *data=malloc(raw);tinfl_decompressor *state=malloc(sizeof(*state));
    if(!data || !state) {free(state);free(data);return KUI_RUNTIME_MEMORY;}
    tinfl_init(state);
    tinfl_status status=tinfl_decompress(state,p+word(entry+88u),&source,data,data,&destination,
        TINFL_FLAG_PARSE_ZLIB_HEADER|TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    free(state);enum kui_runtime_result result=KUI_RUNTIME_CHECKSUM;
    if(status!=TINFL_STATUS_DONE || source!=compressed || destination!=raw)goto done;
    uint8_t sha[32];digest(data,raw,sha);
    if(memcmp(sha,entry+96u,32u))goto done;
    result=kui_runtime_header(data,raw,&out->info);
    if(result!=KUI_RUNTIME_OK)goto done;
    if(memcmp(out->info.build,p+32u,12u) || kui_crc32(0u,data+64u,raw-64u)!=out->info.crc32) {
        result=KUI_RUNTIME_CHECKSUM;goto done;
    }
    if(cancel && cancel()) {result=KUI_RUNTIME_CANCELLED;goto done;}
    memmove(data,data+64u,raw-64u);out->data=data;return KUI_RUNTIME_OK;
done:
    free(data);memset(out,0,sizeof(*out));return result;
}
enum kui_runtime_result kui_runtime_module_bundle_read(const char *path,
    struct kui_runtime_image *out,kui_cancel_fn cancel,bool *handled) {
    if(!out || !handled)return KUI_RUNTIME_IO;
    *out=(struct kui_runtime_image){0};*handled=false;
    const uint8_t *p;size_t n;
    if(!linked(&p,&n))return KUI_RUNTIME_IO;
    return kui_runtime_module_bundle_read_view(p,n,path,out,cancel,handled);
}
