/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_vmu_runtime.h"
#include "kui/retail_image.h"
#if KUI_VC2_VMU_PERSIST
#include "kui/vc2_ce_vmu_loan.h"
#endif

static int service_address(uint32_t p) {
#if KUI_VC2_VMU_PERSIST
    return p==(uint32_t)(uintptr_t)kui_vc2_ce_vmu_loan_begin ||
        p==(uint32_t)(uintptr_t)kui_vc2_ce_vmu_loan_end;
#else
    return !(p&1u) && p>=UINT32_C(0x8c008300) && p<UINT32_C(0x8c00d800);
#endif
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
