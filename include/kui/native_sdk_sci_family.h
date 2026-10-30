/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_SDK_SCI_FAMILY_H
#define KUI_NATIVE_SDK_SCI_FAMILY_H
#include <stddef.h>
#include <stdint.h>
/* Roles of the one reviewed full/compact-FPU/round-toward-zero SDK interrupt family in an
 * immutable executable. vector is the source vector's canonical P1 address
 * in the loaded image; normal, fpu and original keep the raw pointer values
 * that the copied vector and the SDK callback slots hold (P1 or U0 RAM). */
struct kui_native_sdk_sci_family {uint32_t vector,normal,fpu,original;};
_Static_assert(sizeof(struct kui_native_sdk_sci_family)==16u,"SDK SCI family roles");
/* The image-only half of kui_native_sdk_sci_route_discover(): the same
 * unique vector/common/default/table-initializer family, decoded literal
 * relationships and exact normalized fingerprints, with no live read and no
 * title identity. It grants nothing by itself: the copied vector, both still
 * default callback slots and the owner VBR must be checked live before any
 * publication. Returns 1 found, 0 no family, -1 malformed, ambiguous or
 * invalid input. Out clears on every result except overlap with image. */
int kui_native_sdk_sci_family_find(const void *image,size_t image_bytes,uint32_t load,
    struct kui_native_sdk_sci_family *out);
#endif
