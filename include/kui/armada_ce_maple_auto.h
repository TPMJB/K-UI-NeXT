/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_AUTO_H
#define KUI_ARMADA_CE_MAPLE_AUTO_H
#include <stdint.h>

/* Isolated A2 diagnostic only. These five words follow the unchanged
 * 120-byte A2 snapshot inside the existing pre-NK reservation. The hook
 * publishes the reason last, immediately before its terminal transfer.
 * A terminal PRE capture may deliberately retain a held Maple DMA fence;
 * neither phase represents a watchdog or a path that resumes the game. */
#define KUI_ARMADA_CE_MAPLE_AUTO_REASON_ADDRESS UINT32_C(0xacfcff78)
#define KUI_ARMADA_CE_MAPLE_AUTO_PHASE_ADDRESS UINT32_C(0xacfcff7c)
#define KUI_ARMADA_CE_MAPLE_AUTO_MENU_ADDRESS UINT32_C(0xacfcff80)
#define KUI_ARMADA_CE_MAPLE_PROOF_INITIAL_ADDRESS UINT32_C(0xacfcff84)
#define KUI_ARMADA_CE_MAPLE_PROOF_FINAL_ADDRESS UINT32_C(0xacfcff88)
#ifndef KUI_ARMADA_CE_MAPLE_DIAGNOSTIC_MODE
#define KUI_ARMADA_CE_MAPLE_DIAGNOSTIC_MODE 0
#endif
enum kui_armada_ce_maple_auto_reason {
    KUI_ARMADA_CE_MAPLE_AUTO_NONE,
    KUI_ARMADA_CE_MAPLE_AUTO_ADVERTISEMENT,
    KUI_ARMADA_CE_MAPLE_AUTO_INFO,
    KUI_ARMADA_CE_MAPLE_AUTO_READ,
    KUI_ARMADA_CE_MAPLE_AUTO_FINALIZE,
    KUI_ARMADA_CE_MAPLE_AUTO_FAULT,
    KUI_ARMADA_CE_MAPLE_AUTO_VISIT_LIMIT
};
enum kui_armada_ce_maple_auto_phase {
    KUI_ARMADA_CE_MAPLE_AUTO_BEFORE_ORIGINAL=1,
    KUI_ARMADA_CE_MAPLE_AUTO_AFTER_ORIGINAL=2
};

/* Checkpoints use the corresponding reason number. Counters are cumulative
 * since cold boot; a positive count means the requested first event has
 * happened. Historical faults take priority over successful checkpoints.
 * The caller chooses PRE/POST and an independent bounded-visit fallback. */
static inline unsigned kui_armada_ce_maple_auto_decide(unsigned checkpoint,
    uint32_t advertisements,uint32_t info,uint32_t reads,uint32_t finalizations,
    uint32_t fault) {
    if(fault) return KUI_ARMADA_CE_MAPLE_AUTO_FAULT;
    switch(checkpoint) {
    case KUI_ARMADA_CE_MAPLE_AUTO_ADVERTISEMENT:
        return advertisements?checkpoint:KUI_ARMADA_CE_MAPLE_AUTO_NONE;
    case KUI_ARMADA_CE_MAPLE_AUTO_INFO:
        return info?checkpoint:KUI_ARMADA_CE_MAPLE_AUTO_NONE;
    case KUI_ARMADA_CE_MAPLE_AUTO_READ:
        return reads?checkpoint:KUI_ARMADA_CE_MAPLE_AUTO_NONE;
    case KUI_ARMADA_CE_MAPLE_AUTO_FINALIZE:
        return finalizations?checkpoint:KUI_ARMADA_CE_MAPLE_AUTO_NONE;
    default:
        return KUI_ARMADA_CE_MAPLE_AUTO_NONE;
    }
}
#endif
