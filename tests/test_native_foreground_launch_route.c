/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual foreground envelope plus the production helpers used by Games.
 * No FatFs frontend, runtime execution or hardware is modeled here. */
#include "kui/native_standard_route.h"
#include "kui/runtime.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if !KUI_VMU_GAME_CARDS
#error "This regression exercises the VMU-toggle-enabled GUI route"
#endif
static unsigned checks,mutations,manifest_refusals;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"foreground launch route failed line %u: %s\n",__LINE__,#x);exit(1); \
} } while(0)
static void put(uint8_t *p,uint32_t v) {
    for(unsigned i=0u;i<4u;++i)p[i]=(uint8_t)(v>>(8u*i));
}
static void words(uint8_t *p,const uint32_t values[8]) {
    for(unsigned i=0u;i<8u;++i)put(p+4u*i,values[i]);
}
static const uint32_t physical[8]={UINT32_C(0x5049554b),UINT32_C(0x31594850),1u,32u,0u,0u,0u,0u};
static const uint32_t standalone[8]={UINT32_C(0x3153444b),UINT32_C(0x454e4f4c),1u,32u,1u,0u,0u,0u};
static const uint32_t dual[8]={UINT32_C(0x3152444b),UINT32_C(0x4c415544),1u,32u,1u,2u,UINT32_C(0x47000),0u};
static bool envelope_valid(const uint8_t *p,size_t n,struct kui_runtime_info *out) {
    return n>=64u && kui_runtime_header(p,n,out)==KUI_RUNTIME_OK &&
        kui_crc32(0,p+64u,out->payload_bytes)==out->crc32;
}
static uint8_t *actual_envelope(const char *path,size_t *bytes,struct kui_runtime_info *info) {
    FILE *f=fopen(path,"rb");CHECK(f!=NULL);
    CHECK(!fseek(f,0,SEEK_END));long size=ftell(f);CHECK(size>=64 && size<=KUI_RUNTIME_MAX_BYTES+64u);
    CHECK(!fseek(f,0,SEEK_SET));*bytes=(size_t)size;
    uint8_t *blob=malloc(*bytes);CHECK(blob!=NULL);
    CHECK(fread(blob,1u,*bytes,f)==*bytes);CHECK(fgetc(f)==EOF);CHECK(!fclose(f));
    CHECK(envelope_valid(blob,*bytes,info));
    return blob;
}
static void classifier(const uint8_t *payload,size_t n) {
    size_t at=SIZE_MAX;
    CHECK(kui_native_foreground_descriptor_find(payload,n,&at)==1);
    CHECK(at<=n-32u);
    CHECK(kui_native_standard_module_find(payload,n)==KUI_NATIVE_STANDARD_FOREGROUND);
    CHECK(kui_native_physical_descriptor_find(payload,n,NULL)==0);
    CHECK(kui_native_standalone_descriptor_find(payload,n,NULL)==0);
    CHECK(kui_native_dual_descriptor_find(payload,n,NULL)==0);
    CHECK(kui_native_standard_module_find(NULL,n)==KUI_NATIVE_STANDARD_REFUSED);
    uint8_t *copy=malloc(n+80u),*before=malloc(n+80u);CHECK(copy && before);
    memcpy(copy,payload,n);memcpy(before,copy,n);
    CHECK(kui_native_standard_module_find(copy,n)==KUI_NATIVE_STANDARD_FOREGROUND);
    CHECK(!memcmp(copy,before,n));
    for(unsigned word=0u;word<8u;++word)for(unsigned bit=0u;bit<32u;++bit) {
        memcpy(copy,payload,n);copy[at+4u*word+bit/8u]^=(uint8_t)(1u<<(bit%8u));
        memcpy(before,copy,n);
        CHECK(kui_native_standard_module_find(copy,n)==KUI_NATIVE_STANDARD_REFUSED);
        CHECK(!memcmp(copy,before,n));++mutations;
    }
    /* Duplicates are copied from the actual descriptor at an unaligned
     * payload append, rather than synthesizing the foreground positive. */
    memcpy(copy,payload,n);memcpy(copy+n+1u,payload+at,32u);copy[n]=0;
    CHECK(kui_native_standard_module_find(copy,n+33u)==KUI_NATIVE_STANDARD_REFUSED);
    const uint32_t *other[]={physical,standalone,dual};
    for(unsigned i=0u;i<3u;++i) {
        memcpy(copy,payload,n);copy[n]=0;words(copy+n+1u,other[i]);
        memcpy(before,copy,n+33u);
        CHECK(kui_native_standard_module_find(copy,n+33u)==KUI_NATIVE_STANDARD_REFUSED);
        CHECK(!memcmp(copy,before,n+33u));
    }
    /* This is an authored ordinary-physical descriptor fixture, not an
     * assertion that the foreground machine code became a physical module. */
    memset(copy,0,n+80u);words(copy+1u,physical);
    memcpy(before,copy,n+80u);
    CHECK(kui_native_standard_module_find(copy,n+80u)==KUI_NATIVE_STANDARD_PHYSICAL);
    CHECK(!memcmp(copy,before,n+80u));
    CHECK(kui_native_standard_module_find(copy,32u)==KUI_NATIVE_STANDARD_REFUSED);
    words(copy+50u,physical);
    CHECK(kui_native_standard_module_find(copy,n+80u)==KUI_NATIVE_STANDARD_REFUSED);
    /* No foreground record: retain the ordinary physical classifier's
     * existing policy even if another recognized capability is present. */
    for(unsigned i=1u;i<3u;++i) {
        memset(copy,0,n+80u);words(copy+1u,physical);words(copy+50u,other[i]);
        memcpy(before,copy,n+80u);
        CHECK(kui_native_standard_module_find(copy,n+80u)==KUI_NATIVE_STANDARD_PHYSICAL);
        CHECK(!memcmp(copy,before,n+80u));
    }
    free(before);free(copy);
}
static struct kui_retail_manifest valid_manifest(void) {
    struct kui_retail_manifest m;memset(&m,0x5a,sizeof(m));
    m.reader=KUI_RETAIL_READER_STANDARD;m.storage_transport=KUI_STORAGE_SCI;
    m.track_count=3u;m.extent_count=3u;m.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    return m;
}
static void manifest_check(struct kui_retail_manifest m,bool expected) {
    const struct kui_retail_manifest before=m;
    CHECK(kui_native_foreground_manifest_valid(&m)==expected);
    CHECK(!memcmp(&m,&before,sizeof(m)));
    if(!expected)++manifest_refusals;
}
static void manifests(void) {
    CHECK(!kui_native_foreground_manifest_valid(NULL));
    const uint32_t counts[][2]={{1u,1u},{1u,63u},{63u,1u},{32u,32u},
        {0u,1u},{1u,0u},{64u,1u},{1u,64u},{33u,32u},{UINT32_MAX,1u},
        {1u,UINT32_MAX},{UINT32_MAX,UINT32_MAX}};
    for(unsigned i=0u;i<sizeof(counts)/sizeof(counts[0]);++i) {
        struct kui_retail_manifest m=valid_manifest();m.track_count=counts[i][0];m.extent_count=counts[i][1];
        manifest_check(m,i<4u);
    }
    struct kui_retail_manifest m=valid_manifest();m.flags=0u;manifest_check(m,true);
    for(unsigned transport=0u;transport<8u;++transport) {
        m=valid_manifest();m.storage_transport=transport;
        manifest_check(m,transport==KUI_STORAGE_SCI);
    }
    for(unsigned reader=0u;reader<8u;++reader) {
        m=valid_manifest();m.reader=reader;
        manifest_check(m,reader==KUI_RETAIL_READER_STANDARD);
    }
    for(unsigned bit=0u;bit<32u;++bit) {
        m=valid_manifest();m.flags=UINT32_C(1)<<bit;
        manifest_check(m,m.flags==KUI_RETAIL_IMAGE_BOOT_CRC);
    }
    /* Existing physical Standard transport admission stays broad; the
     * foreground-only restrictions must not replace this older helper. */
    for(unsigned transport=0u;transport<3u;++transport)
        CHECK(kui_native_physical_launch_valid(KUI_RETAIL_READER_STANDARD,transport));
    CHECK(!kui_native_physical_launch_valid(KUI_RETAIL_READER_ASYNC,KUI_STORAGE_SCI));
}
int main(int argc,char **argv) {
    CHECK(argc==2);
    CHECK(!strcmp(kui_native_standard_package(false),"0:/KUI/apps/games/retail-physical.kui"));
    CHECK(!strcmp(kui_native_standard_package(true),"0:/KUI/apps/games/retail-boot.kui"));
    size_t n;struct kui_runtime_info info;
    uint8_t *blob=actual_envelope(argv[1],&n,&info);
    classifier(blob+64u,info.payload_bytes);manifests();
    blob[0]^=1u;CHECK(!envelope_valid(blob,n,&info));blob[0]^=1u;
    blob[n-1u]^=1u;CHECK(!envelope_valid(blob,n,&info));blob[n-1u]^=1u;
    CHECK(envelope_valid(blob,n,&info));free(blob);
    printf("FOREGROUND_LAUNCH_ROUTE_JSON {\"checks\":%u,\"actual_payload_bytes\":%u,"
        "\"foreground_field_mutations\":%u,\"manifest_refusals\":%u,"
        "\"off_path\":\"0:/KUI/apps/games/retail-physical.kui\","
        "\"on_path\":\"0:/KUI/apps/games/retail-boot.kui\","
        "\"actual_foreground_envelope\":true,\"physical_positive_is_authored_fixture\":true,"
        "\"full_frontend_executed\":false,\"hardware_executed\":false}\n",
        checks,info.payload_bytes,mutations,manifest_refusals);
    return 0;
}
