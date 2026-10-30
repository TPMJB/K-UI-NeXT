/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only structural diagnostic. The transport interpreter below executes
 * supplied original startup words; no retail bytes are embedded here. */
#define main native_layout_reference_tests
#include "../tests/test_vmu_native_layout.c"
#undef main
#include "kui/vmu_native_program.h"
#include "kui/vmu_native_resolution_cache.h"
#include "kui/vmu_native_stage_placement.h"
#include "kui/hash.h"
#include "kui/native_readahead_layout.h"

static void digest(const void *p,size_t bytes,char text[65],uint8_t out[32]) {
    static const char hex[]="0123456789abcdef";
    struct kui_sha256 state;
    kui_sha256_init(&state);kui_sha256_update(&state,p,bytes);kui_sha256_digest(&state,out);
    for(unsigned i=0;i<32u;i++) {text[2u*i]=hex[out[i]>>4];text[2u*i+1u]=hex[out[i]&15u];}
    text[64]=0;
}
static bool release_shape(const uint8_t *p,size_t n,uint32_t release) {
    uint32_t base=UINT32_C(0x8c010000),at=physical(release)-physical(base);
    if(physical(release)<physical(base) || at>n || n-at<20u)return false;
    uint16_t w[10];for(unsigned i=0;i<10u;i++)w[i]=(uint16_t)(p[at+2u*i]|(uint16_t)p[at+2u*i+1u]<<8);
    if(w[0]!=0x0002u || (w[1]&0xff00u)!=0x9300u || w[2]!=0x4009u || w[3]!=0x4009u ||
       w[4]!=0xc90fu || w[5]!=0x6703u || w[6]!=0x0002u || w[7]!=0x2039u || w[8]!=0xcbf0u || w[9]!=0x400eu)return false;
    size_t slot=(size_t)at+6u+2u*(w[1]&255u);
    return slot<=n && n-slot>=2u && p[slot]==0x0fu && p[slot+1u]==0xffu;
}

int main(int argc,char **argv) {
    if(argc!=4) {fprintf(stderr,"Usage: %s STARTUP STAGE_BEGIN STAGE_END\n",argv[0]);return 2;}
    char *last1,*last2;unsigned long first=strtoul(argv[2],&last1,0),last=strtoul(argv[3],&last2,0);
    if(*last1 || *last2 || first>UINT32_MAX || last>UINT32_MAX)return 2;
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    if(fseek(file,0,SEEK_END))return 2;
    long length=ftell(file);
    if(length<0x300 || length>0xc00000 || fseek(file,0,SEEK_SET))return 2;
    uint8_t *image=malloc((size_t)length);if(!image)return 2;
    if(fread(image,1,(size_t)length,file)!=(size_t)length || fclose(file))return 2;
    const uint32_t load=UINT32_C(0x8c010000),lease_bytes=KUI_NATIVE_RA_LEASE_BYTES;
    size_t bytes=(size_t)length,selected=0u;
    bool extent=kui_vmu_native_program_extent(image,bytes,load,&selected);
    size_t n=extent?selected:bytes;
    struct kui_vmu_native_discovery driver={0};struct kui_vmu_native_layout layout={0};
    struct kui_vmu_native_heap heap={0};struct kui_vmu_native_heap_call call={0};
    struct kui_vmu_native_exit exits={0},guarded_exits={0};
    struct kui_vmu_native_resolution resolution={0};uint8_t wire[1024]={0};
    int driver_result=kui_vmu_native_discover(image,n,load,&driver);
    bool found_layout=driver_result==KUI_VMU_NATIVE_RESOLVED && kui_vmu_native_layout_discover(image,n,&driver,&layout);
    int heap_result=kui_vmu_native_heap_discover(image,n,load,&heap);
    char source_hash[65],image_hash[65];uint8_t source_sha[32],image_sha[32];
    digest(image,bytes,source_hash,source_sha);digest(image,n,image_hash,image_sha);
    bool found_call=heap_result==KUI_VMU_NATIVE_HEAP_FOUND && kui_vmu_native_heap_call_discover(image,n,&heap,&call);
    /* A reviewed fixed arena stands in for the initializer slot and call,
     * for its exact image only. */
    bool fixed=heap_result==KUI_VMU_NATIVE_HEAP_FOUND && !heap.initializer_slot &&
        kui_vmu_native_heap_fixed_plan(image,n,image_sha,&heap,&call,NULL);
    bool found_exits=kui_vmu_native_exit_discover(image,n,load,&exits),found_guarded=false;
    bool aligned=heap_result==KUI_VMU_NATIVE_HEAP_FOUND && !((heap.initializer|heap.release)&3u);
    bool release=heap_result==KUI_VMU_NATIVE_HEAP_FOUND && release_shape(image,n,heap.release);
    if(driver_result==KUI_VMU_NATIVE_RESOLVED && heap_result==KUI_VMU_NATIVE_HEAP_FOUND) {
        const struct kui_vmu_native_patch_window windows[]={{driver.submit,12u},{heap.initializer,12u},{heap.release,20u}};
        found_guarded=kui_vmu_native_exit_discover_guarded(image,n,load,windows,3u,&guarded_exits);
    }
    bool complete=extent && kui_vmu_native_resolution_cache_build(image,n,load,wire,sizeof(wire),&resolution);
    bool placement=(found_call || fixed) && kui_vmu_native_stage_allocation_safe(&heap,(uint32_t)n,
        (uint32_t)first,(uint32_t)last,call.entry_begin,call.entry_bytes);
    uint32_t payload=found_call || fixed?physical(call.entry_begin)+call.entry_bytes-lease_bytes:0u;
    printf("{\"detector_epoch\":%u,\"source_bytes\":%zu,\"source_sha256\":\"%s\",\"extent\":%s,\"image_bytes\":%zu,"
        "\"image_sha256\":\"%s\",\"discarded_suffix_bytes\":%zu,\"driver\":%d,\"layout\":%s,"
        "\"heap\":%d,\"initializer_slot\":%s,\"initializer_call\":%s,\"fixed_arena\":%s,\"aligned_hook_entries\":%s,"
        "\"release_hook_shape\":%s,\"exits\":%s,\"guarded_exits\":%s,\"combined_resolution\":%s,"
        "\"placement_384k\":%s,\"lease_bytes\":%u,\"lease_payload\":%u,\"driver_submit\":%u,\"driver_initializer\":%u,"
        "\"completion_call\":%u,\"layout_initializer\":%u,\"layout_wrapper\":%u,\"layout_call\":%u,"
        "\"rx_layout\":%u,\"layout_version\":%u,\"heap_initializer\":%u,\"heap_allocate\":%u,"
        "\"heap_release\":%u,\"heap_slot\":%u,\"heap_begin\":%u,\"heap_bytes\":%u,\"exit_family\":%u",
        KUI_VMU_NATIVE_RESOLUTION_DETECTOR_EPOCH,bytes,source_hash,extent?"true":"false",n,image_hash,extent?bytes-n:0u,driver_result,found_layout?"true":"false",
        heap_result,heap.initializer_slot?"true":"false",found_call?"true":"false",fixed?"true":"false",aligned?"true":"false",
        release?"true":"false",found_exits?"true":"false",found_guarded?"true":"false",complete?"true":"false",
        placement?"true":"false",lease_bytes,payload,driver.submit,driver.initializer,driver.completion_call,layout.initializer,
        layout.initializer_wrapper,layout.initializer_call,layout.rx_layout,layout.version,heap.initializer,
        heap.allocate,heap.release,heap.initializer_slot,call.entry_begin,call.entry_bytes,exits.family);
    if(complete) {
        static const char hex[]="0123456789abcdef";printf(",\"record_hex\":\"");
        for(unsigned i=0;i<sizeof(wire);i++)printf("%c%c",hex[wire[i]>>4],hex[wire[i]&15u]);
        printf("\"");
    }
    printf("}\n");fflush(stdout);
    if(found_layout && (found_call || fixed) && placement) {
        memory=calloc(1u,0x1000000u);CHECK(memory);
        transport_model(image,n,&driver,&layout);
        struct kui_vmu_native_runtime_config runtime={.version=1u,.bytes=sizeof(runtime),
            .lease_address=payload|UINT32_C(0x80000000),.lease_bytes=lease_bytes,
            .lease_header_bytes=32u,.required_lease_bytes=lease_bytes};
        struct kui_vmu_native_layout_diagnostic why;
        bool bound=kui_vmu_native_layout_bind_checked(&layout,&driver,read_memory,NULL,&runtime,&why);
        printf("{\"transport_execution\":true,\"transport_checks\":%u,\"live_bind_384k\":%s,"
            "\"bind_diagnostic\":%u,\"bind_address\":%u,\"bind_value\":%u,\"bind_expected\":%u}\n",
            checks,bound?"true":"false",why.code,why.address,why.value,why.expected);
        free(memory);
    }
    free(image);return 0;
}
