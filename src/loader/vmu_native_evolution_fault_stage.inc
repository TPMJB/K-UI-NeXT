/* SPDX-License-Identifier: GPL-3.0-only */
/* Temporary, exact-revision diagnosis for Evolution's existing fatal malloc
 * path. This does not wrap allocations, alter a successful return, or expand
 * the generic native discovery/hook admission rules. Include in the temporary
 * installer only; no image pointer or installer callback survives the launch.
 *
 * The caller has independently bound driver->image_sha256 to the immutable
 * executable before installing its normal hooks. Those hooks and the SDK's
 * first initializer have since changed live RAM, so rehashing that live image
 * would not be an immutable-image check. The exact failure-only window is
 * independently witnessed again immediately before this additional mutation.
 * Call under IMASK15 after the worker target has passed passive_entry().
 */
#include "kui/vmu_native_discovery.h"
#include "kui/vmu_native_patch.h"
#include <string.h>

#define KUI_NATIVE_EVOLUTION_FAULT_IMAGE_BYTES UINT32_C(2143652)
#define KUI_NATIVE_EVOLUTION_FAULT_LOAD UINT32_C(0x8c010000)
#define KUI_NATIVE_EVOLUTION_FAULT_ENTRY UINT32_C(0x8c0431e8)

/* 0: another immutable image, untouched; 1: installed and published;
 * -1: selected revision refused before any mutation; -2: readback failed.
 * publish must synchronously publish the exact P1 code span, preserving CCR.
 * image is the currently loaded executable corresponding to load, and may be
 * a host buffer in the proof test. Hook range ownership is checked by caller.
 */
static int native_evolution_fault_install(const struct kui_vmu_native_discovery *driver,
    void *image,size_t bytes,uint32_t load,uint32_t allocation_failure_hook,
    void (*publish)(uint32_t,uint32_t)) {
    static const uint8_t digest[32]={
        0xff,0xb5,0xa8,0xe5,0xff,0x55,0x47,0x02,0x6d,0x93,0x64,0x9e,0x1f,0x90,0x58,0xb7,
        0xee,0x5e,0x29,0xd8,0xd0,0xff,0x8a,0x89,0x33,0x57,0x01,0xf7,0xe9,0x64,0x7f,0x35};
    /* Original twelve bytes belong exclusively to the malloc==NULL arm:
     * it records failure then falls into the existing self-loop at +12.
     * The replacement is nonreturning and receives the untouched failure ABI.
     */
    static const uint8_t witness[12]={
        0xc3,0x63,0xdc,0x33,0x33,0x51,0x31,0xd2,0x2b,0x21,0x13,0x13};
    const size_t offset=KUI_NATIVE_EVOLUTION_FAULT_ENTRY-KUI_NATIVE_EVOLUTION_FAULT_LOAD;
    uint16_t patch[6];
    if(!driver) return -1;
    if(memcmp(driver->image_sha256,digest,sizeof(digest))) return 0;
    if(!image || !publish || bytes!=KUI_NATIVE_EVOLUTION_FAULT_IMAGE_BYTES ||
       driver->version!=KUI_VMU_NATIVE_DISCOVERY_VERSION || driver->bytes!=sizeof(*driver) ||
       driver->family!=KUI_VMU_NATIVE_FAMILY_UNIFORM_V1 ||
       driver->requirements!=KUI_VMU_NATIVE_REQUIREMENTS || driver->image_bytes!=bytes ||
       load!=KUI_NATIVE_EVOLUTION_FAULT_LOAD || driver->load_address!=load ||
       bytes>UINTPTR_MAX-(uintptr_t)image || offset>bytes || sizeof(witness)>bytes-offset ||
       memcmp((const uint8_t *)image+offset,witness,sizeof(witness)) ||
       !kui_vmu_native_patch_no_interior(image,bytes,load,
            KUI_NATIVE_EVOLUTION_FAULT_ENTRY,sizeof(witness)) ||
       !kui_vmu_native_patch_encode(KUI_NATIVE_EVOLUTION_FAULT_ENTRY,allocation_failure_hook,patch))
        return -1;
    memcpy((uint8_t *)image+offset,patch,sizeof(patch));
    publish(KUI_NATIVE_EVOLUTION_FAULT_ENTRY,KUI_NATIVE_EVOLUTION_FAULT_ENTRY+sizeof(patch));
    if(memcmp((const uint8_t *)image+offset,patch,sizeof(patch))) return -2;
    return 1;
}
