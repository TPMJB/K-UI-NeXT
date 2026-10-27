/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_MAPLE_PROBE_H
#define KUI_ARMADA_CE_MAPLE_PROBE_H
#include "kui/armada_ce_maple_observe.h"
#ifndef KUI_ARMADA_CE_MAPLE_PROOF
#define KUI_ARMADA_CE_MAPLE_PROOF 0
#endif
#ifndef KUI_ARMADA_CE_MAPLE_STATE
#define KUI_ARMADA_CE_MAPLE_STATE 0
#endif
#ifndef KUI_ARMADA_CE_MAPLE_HEADERS
#define KUI_ARMADA_CE_MAPLE_HEADERS 0
#endif
#ifndef KUI_ARMADA_CE_MAPLE_FENCE
#define KUI_ARMADA_CE_MAPLE_FENCE 0
#endif
#ifndef KUI_ARMADA_CE_MAPLE_A2
#define KUI_ARMADA_CE_MAPLE_A2 0
#endif
#if (KUI_ARMADA_CE_MAPLE_A2 != 0 && KUI_ARMADA_CE_MAPLE_A2 != 1) || \
    (KUI_ARMADA_CE_MAPLE_A2 && !KUI_ARMADA_CE_MAPLE_FENCE)
#error "CE virtual A2 requires its isolated DMA gate"
#endif
#if (KUI_ARMADA_CE_MAPLE_FENCE != 0 && KUI_ARMADA_CE_MAPLE_FENCE != 1) || \
    (KUI_ARMADA_CE_MAPLE_FENCE && !KUI_ARMADA_CE_MAPLE_TRAFFIC)
#error "CE Maple fence requires its isolated traffic build"
#endif
#ifndef KUI_ARMADA_CE_MAPLE_TRAFFIC
#define KUI_ARMADA_CE_MAPLE_TRAFFIC 0
#endif
#if (KUI_ARMADA_CE_MAPLE_TRAFFIC != 0 && KUI_ARMADA_CE_MAPLE_TRAFFIC != 1) || \
    (KUI_ARMADA_CE_MAPLE_TRAFFIC && !KUI_ARMADA_CE_MAPLE_HOOK)
#error "CE Maple traffic requires its completion hook"
#endif
#ifndef KUI_ARMADA_CE_MAPLE_HOOK
#define KUI_ARMADA_CE_MAPLE_HOOK 0
#endif
#if (KUI_ARMADA_CE_MAPLE_HOOK != 0 && KUI_ARMADA_CE_MAPLE_HOOK != 1) || \
    (KUI_ARMADA_CE_MAPLE_HOOK && (!KUI_ARMADA_CE_MAPLE_STATE || KUI_ARMADA_CE_MAPLE_HEADERS))
#error "CE Maple completion observation requires state proof and its own report"
#endif
#if (KUI_ARMADA_CE_MAPLE_HEADERS != 0 && KUI_ARMADA_CE_MAPLE_HEADERS != 1) || \
    (KUI_ARMADA_CE_MAPLE_HEADERS && !KUI_ARMADA_CE_MAPLE_STATE)
#error "CE Maple headers require the isolated driver-state proof"
#endif
#if (KUI_ARMADA_CE_MAPLE_STATE != 0 && KUI_ARMADA_CE_MAPLE_STATE != 1) || \
    (KUI_ARMADA_CE_MAPLE_STATE && !KUI_ARMADA_CE_MAPLE_PROOF)
#error "CE Maple state requires the isolated mapping proof"
#endif
#if KUI_ARMADA_CE_MAPLE_PROOF != 0 && KUI_ARMADA_CE_MAPLE_PROOF != 1
#error "CE Maple proof must be explicitly disabled or enabled"
#endif
#if KUI_ARMADA_CE_MAPLE_PROOF
#include "kui/armada_ce_maple_proof.h"
#endif
#if KUI_ARMADA_CE_MAPLE_STATE
#include "kui/armada_ce_maple_state.h"
#endif
#define KUI_ARMADA_CE_MAPLE_PROBE_ADDRESS UINT32_C(0x8c004000)
#define KUI_ARMADA_CE_MAPLE_PROBE_LIMIT UINT32_C(0x8c007c00)
#if KUI_ARMADA_CE_MAPLE_PROOF
#if KUI_ARMADA_CE_MAPLE_STATE
#if KUI_ARMADA_CE_MAPLE_A2
#define KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT UINT32_C(0x38504d43)
#define KUI_ARMADA_CE_MAPLE_PROBE_LABELS 36u
#elif KUI_ARMADA_CE_MAPLE_FENCE
#define KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT UINT32_C(0x37504d43)
#define KUI_ARMADA_CE_MAPLE_PROBE_LABELS 30u
#elif KUI_ARMADA_CE_MAPLE_TRAFFIC
#define KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT UINT32_C(0x36504d43)
#define KUI_ARMADA_CE_MAPLE_PROBE_LABELS 24u
#elif KUI_ARMADA_CE_MAPLE_HOOK
#define KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT UINT32_C(0x35504d43)
#define KUI_ARMADA_CE_MAPLE_PROBE_LABELS 24u
#elif KUI_ARMADA_CE_MAPLE_HEADERS
#define KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT UINT32_C(0x34504d43)
#define KUI_ARMADA_CE_MAPLE_PROBE_LABELS 21u
#else
#define KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT UINT32_C(0x33504d43)
#define KUI_ARMADA_CE_MAPLE_PROBE_LABELS 18u
#endif
#else
#define KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT UINT32_C(0x32504d43)
#define KUI_ARMADA_CE_MAPLE_PROBE_LABELS 12u
#endif
/* The historical entry returns the initial member. The isolated resident
 * may recover this containing report; ordinary metadata callers keep their
 * existing ABI. Labels stay here rather than expanding the low resident. */
struct kui_armada_ce_maple_probe_report {
    struct kui_armada_ce_maple_snapshot discovery;
    struct kui_armada_ce_maple_proof_snapshot proof;
#if KUI_ARMADA_CE_MAPLE_STATE
    struct kui_armada_ce_maple_state_snapshot state;
#if KUI_ARMADA_CE_MAPLE_HEADERS
    const struct kui_armada_ce_maple_headers_snapshot *headers;
#endif
#endif
    const char *labels[KUI_ARMADA_CE_MAPLE_PROBE_LABELS];
};
_Static_assert(offsetof(struct kui_armada_ce_maple_probe_report,discovery)==0u,
    "CE Maple discovery remains the initial report member");
#else
#define KUI_ARMADA_CE_MAPLE_PROBE_CONTRACT UINT32_C(0x31504d43)
#endif
extern const uint32_t kui_armada_ce_maple_probe_contract;
/* Existing isolated stage installer/entry symbol. Only accepted PLAY/PLAY2
 * with sample1 can sample. sample0 returns authored state without any reads.
 * The final snapshot ABI and the meaning of every word are in observe.h.
 * This collector calls no CE API and changes only its own metadata. The
 * separate HOOK variant installs its completion counter before NK starts;
 * that bridge's placement and mode API calls are audited separately.
 * Virtual VMU service and save-payload changes remain disabled. */
extern const struct kui_armada_ce_maple_snapshot *kui_armada_ce_bind_probe(
    uint32_t function,uint32_t command,int32_t result,uint32_t sample);
#if defined(KUI_ARMADA_CE_MAPLE_PROBE_HOST_TEST)
extern bool kui_armada_ce_maple_probe_test_read(void *,uint32_t,uint32_t *);
#if KUI_ARMADA_CE_MAPLE_PROOF
extern bool kui_armada_ce_maple_probe_test_cache(void *,uint32_t,uint32_t *);
#endif
void kui_armada_ce_maple_probe_test_reset(void);
#endif
#endif
