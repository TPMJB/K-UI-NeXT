/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_vmu_probe.h"
#include "kui/vmu_native_program.h"
#include "kui/hash.h"
#include <string.h>

static bool ram_span(uintptr_t address,size_t bytes,uint32_t *physical) {
#if UINTPTR_MAX>UINT32_MAX
    if(address>UINT32_MAX) return false;
#endif
    uint32_t a=(uint32_t)address,area=a&UINT32_C(0xe0000000);
    *physical=a&UINT32_C(0x1fffffff);
    return (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        *physical>=UINT32_C(0x0c000000) && *physical<UINT32_C(0x0d000000) &&
        bytes<=UINT32_C(0x0d000000)-*physical;
}
static bool disjoint(const void *image,size_t bytes,const void *out,size_t out_bytes) {
    uintptr_t a=(uintptr_t)image,b=(uintptr_t)out;
    if(bytes>UINTPTR_MAX-a || out_bytes>UINTPTR_MAX-b ||
       (a<b+out_bytes && b<a+bytes)) return false;
    uint32_t ap,bp;
    return !(ram_span(a,bytes,&ap) && ram_span(b,out_bytes,&bp) &&
        ap<bp+out_bytes && bp<ap+bytes);
}
static bool image_arguments(const void *image,size_t bytes,uint32_t load) {
    uint32_t p;
    return image && bytes>=0x300u && bytes<=UINT32_C(0xc00000) &&
        bytes<=UINTPTR_MAX-(uintptr_t)image && !(load&3u) &&
        ram_span(load,bytes,&p) && p>=UINT32_C(0x0c010000);
}
bool kui_disc_vmu_probe_run(const void *image,size_t file_bytes,uint32_t load,
    struct kui_disc_vmu_probe *out) {
    if(!out || ((uintptr_t)out&3u) ||
       !disjoint(image,file_bytes,out,sizeof(*out))) return false;
    memset(out,0,sizeof(*out));
    out->version=KUI_DISC_VMU_PROBE_VERSION;out->bytes=sizeof(*out);
    out->driver_result=KUI_VMU_NATIVE_INVALID;out->heap_result=KUI_VMU_NATIVE_HEAP_INVALID;
    out->stage=KUI_DISC_VMU_PROBE_ARGUMENT;
    if(!image_arguments(image,file_bytes,load)) return false;
    out->file_bytes=(uint32_t)file_bytes;out->file_crc32=kui_crc32(0u,image,file_bytes);
    out->stage=KUI_DISC_VMU_PROBE_EXTENT;
    size_t program_bytes;
    if(!kui_vmu_native_program_extent(image,file_bytes,load,&program_bytes)) return false;
    out->program_bytes=(uint32_t)program_bytes;
    if(!kui_vmu_native_discover_layout(image,program_bytes,load,&out->driver,&out->layout)) {
        /* The combined finder deliberately clears both outputs on failure.
         * A diagnostic-only replay distinguishes a missing/ambiguous driver
         * from a valid driver whose immutable buffer layout is unsupported.
         * Successful launches keep the single combined discovery pass. */
        out->driver_result=kui_vmu_native_discover(image,program_bytes,load,&out->driver);
        out->stage=out->driver_result==KUI_VMU_NATIVE_RESOLVED?
            KUI_DISC_VMU_PROBE_MAPLE_LAYOUT:KUI_DISC_VMU_PROBE_MAPLE_DRIVER;
        return false;
    }
    out->driver_result=KUI_VMU_NATIVE_RESOLVED;
    out->stage=KUI_DISC_VMU_PROBE_HEAP_DISCOVERY;
    out->heap_result=kui_vmu_native_heap_discover(image,program_bytes,load,&out->heap);
    if(out->heap_result!=KUI_VMU_NATIVE_HEAP_FOUND) return false;
    out->stage=KUI_DISC_VMU_PROBE_HEAP_SLOT;
    if(!out->heap.initializer_slot) return false;
    out->stage=KUI_DISC_VMU_PROBE_HEAP_CALL;
    if(!kui_vmu_native_heap_call_discover(image,program_bytes,&out->heap,&out->call) ||
       (out->driver.family==KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1 &&
        !kui_vmu_native_early_p2_calls_valid(image,program_bytes,&out->driver,out->call.call,out->layout.initializer_call))) return false;
    out->stage=KUI_DISC_VMU_PROBE_STARTUP_CLEAR;
    struct kui_vmu_native_heap_preclear clear;
    if(kui_vmu_native_heap_preclear_plan(image,program_bytes,out->driver.image_sha256,
       &out->heap,&out->call,&clear)!=0) return false;
    out->stage=KUI_DISC_VMU_PROBE_READY;return true;
}
const char *kui_disc_vmu_probe_stage_text(uint32_t stage) {
    switch(stage) {
    case KUI_DISC_VMU_PROBE_ARGUMENT:return "Invalid executable probe arguments";
    case KUI_DISC_VMU_PROBE_EXTENT:return "Native executable extent is unsupported";
    case KUI_DISC_VMU_PROBE_MAPLE_DRIVER:return "Native Maple driver is unsupported";
    case KUI_DISC_VMU_PROBE_MAPLE_LAYOUT:return "Native Maple buffer layout is unsupported";
    case KUI_DISC_VMU_PROBE_HEAP_DISCOVERY:return "Native allocator is unsupported";
    case KUI_DISC_VMU_PROBE_HEAP_SLOT:return "Native allocator startup pointer is unavailable";
    case KUI_DISC_VMU_PROBE_HEAP_CALL:return "Native allocator startup call is unsupported";
    case KUI_DISC_VMU_PROBE_STARTUP_CLEAR:return "Startup RAM clear is unsafe for the physical-disc VMU installer";
    case KUI_DISC_VMU_PROBE_READY:return "Immutable VMU probe passed";
    default:return "Unknown native VMU probe stage";
    }
}
