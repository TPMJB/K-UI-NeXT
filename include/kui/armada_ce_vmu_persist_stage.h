/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_VMU_PERSIST_STAGE_H
#define KUI_ARMADA_CE_VMU_PERSIST_STAGE_H
#include "kui/vmu_persist_stage.h"
#include "kui/armada_ce_vmu_runtime.h"

/* High-stage-only, before NK and before a background reader exists. The
 * caller has already admitted the exact CE owner and exclusively owns the
 * initialized storage device. Keep all executable/audio extents unchanged;
 * only a private copy's reader selector is normalized for the shared stage
 * scanner, whose actual I/O is supplied through the serialized ops. */
struct kui_armada_ce_vmu_persist_stage_workspace {
    struct kui_retail_manifest serialized;
    struct kui_vmu_persist_stage_workspace core;
    uint32_t started;
};
int kui_armada_ce_vmu_persist_stage_prepare(const struct kui_vmu_persist_profile *,
    const struct kui_retail_manifest *,const struct kui_vmu_persist_session_ops *,
    struct kui_armada_ce_vmu_persist_stage_workspace *,struct kui_vmu_durable_handoff *);
/* Copy only the source-owned fixed resident descriptor. Admission verifies
 * its complete native blob separately before following either entrypoint. */
int kui_armada_ce_vmu_persist_services_prepare(const uint8_t *resident,size_t bytes,
    bool stream,struct kui_armada_ce_vmu_runtime_services *);
#endif
