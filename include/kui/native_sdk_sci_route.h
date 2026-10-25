/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_SDK_SCI_ROUTE_H
#define KUI_NATIVE_SDK_SCI_ROUTE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KUI_NATIVE_SDK_SCI_ROUTE_VERSION 1u
#define KUI_NATIVE_SDK_SCI_ROUTE_LOAD UINT32_C(0x8c010000)
#define KUI_NATIVE_SDK_SCI_ROUTE_IMAGE_BYTES UINT32_C(4024288)
#define KUI_NATIVE_SDK_SCI_ROUTE_VBR UINT32_C(0x8c00f400)
#define KUI_NATIVE_SDK_SCI_ROUTE_DEFAULT UINT32_C(0x8c1add0c)
#define KUI_NATIVE_SDK_SCI_ROUTE_ERI UINT32_C(0x8c00f65c)
#define KUI_NATIVE_SDK_SCI_ROUTE_RXI UINT32_C(0x8c00f660)
/* The existing constants above retain the Evolution2 reviewed profile.
 * Evolution1 uses the same independently checked copied-vector/callback
 * layout, with its own exact program identity and original code windows. */
#define KUI_NATIVE_SDK_SCI_EVO1_IMAGE_BYTES UINT32_C(2143652)
#define KUI_NATIVE_SDK_SCI_EVO1_VECTOR UINT32_C(0x8c1da080)
#define KUI_NATIVE_SDK_SCI_EVO1_NORMAL UINT32_C(0x8c1da10c)
#define KUI_NATIVE_SDK_SCI_EVO1_FPU UINT32_C(0x8c1da274)
#define KUI_NATIVE_SDK_SCI_EVO1_DEFAULT UINT32_C(0x8c1d9f50)
#define KUI_NATIVE_SDK_SCI_EVO1_INIT UINT32_C(0x8c1b0e56)

typedef bool (*kui_native_sdk_sci_route_read)(void *,uint32_t,uint32_t *);
struct kui_native_sdk_sci_route_plan {
    uint32_t version,bytes,vbr,common,eri_slot,rxi_slot,callback,previous;
};

/* Exact reviewed Evolution1/Evolution2 owner admission: 1 is admitted, 0 is another owner,
 * -1 is malformed/changed context. This helper performs no writes or MMIO.
 * verified_sha256 must be the caller's already completed normalized-program
 * identity proof, before legitimate runtime patches; it is not a cache hint.
 * Fresh immutable code fingerprints and the live copied SDK vector/common
 * are checked again. The source vector's selected-common literal may have
 * been updated by its original initializer. Only the live vector's R0 scratch
 * word is ignored; all executable bytes and other literals are required.
 *
 * Caller additionally owns the retained map/worker and proves no pending GD,
 * SCI reception, loan, callback or release frame. Under IMASK15 it publishes
 * only the two planned callback words, verifies both, and then advertises
 * readiness. The callback is a separately proved canonical low P1 RTS entry,
 * never the resident's RTE interrupt entry. No saved SDK frame is patched.
 * Plan addresses and previous/default callback belong to the selected owner;
 * no caller substitutes one owner's layout or common handler for another.
 * The two default slots must still be unowned; this is first-install admission.
 * Out clears on refusal except overlap with image/digest, which is untouched.
 */
int kui_native_sdk_sci_route_admit(const void *image,size_t image_bytes,uint32_t load,
    const uint8_t verified_sha256[32],uint32_t current_vbr,uint32_t current_sr,
    uint32_t callback,kui_native_sdk_sci_route_read read,void *context,
    struct kui_native_sdk_sci_route_plan *out);
#endif
