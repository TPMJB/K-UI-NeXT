/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_runtime.h"
#include "kui/retail_image.h"

static int service_address(uint32_t p) {
    return !(p&1u) && p>=UINT32_C(0x8c008300) && p<UINT32_C(0x8c00d800);
}
static int service_shape(const struct kui_armada_ce_vmu_runtime_services *s) {
    return s && service_address(s->loan_begin) && service_address(s->loan_end) &&
        s->loan_begin!=s->loan_end && !s->reserved[0] && !s->reserved[1];
}
int kui_armada_ce_vmu_runtime_services_seal(struct kui_armada_ce_vmu_runtime_services *s) {
    if(!s) return -1;
    s->seal=0u;
    if(!service_shape(s)) return -1;
    s->magic=KUI_ARMADA_CE_VMU_SERVICES_MAGIC;
    s->version=KUI_ARMADA_CE_VMU_SERVICES_VERSION;
    s->bytes=sizeof(*s);
    s->seal=kui_retail_crc32(0,s,offsetof(struct kui_armada_ce_vmu_runtime_services,seal));
    return 0;
}
int kui_armada_ce_vmu_runtime_services_validate(const struct kui_armada_ce_vmu_runtime_services *s) {
    return !service_shape(s) || s->magic!=KUI_ARMADA_CE_VMU_SERVICES_MAGIC ||
        s->version!=KUI_ARMADA_CE_VMU_SERVICES_VERSION || s->bytes!=sizeof(*s) ||
        s->seal!=kui_retail_crc32(0,s,offsetof(struct kui_armada_ce_vmu_runtime_services,seal))?-1:0;
}
