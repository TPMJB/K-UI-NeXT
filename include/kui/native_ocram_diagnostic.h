/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_OCRAM_DIAGNOSTIC_H
#define KUI_NATIVE_OCRAM_DIAGNOSTIC_H
#define KUI_OC_TRACE_WORDS 16
#define KUI_OC_TRACE_HISTORY 64
#define KUI_OC_TRACE_COPYBACK 144
#define KUI_OC_TRACE_BYTES 148
#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>
struct kui_native_ocram_diagnostic {
    /* count,R4,R5,R6,FN,callerPR,callerSP,callerSR,source,assemblyR0,
     * lockBefore,MMUCR,CCR,VBR,mappedPointer,mapReason */
    uint32_t entry[KUI_OC_TRACE_WORDS];
    /* Four completed gateway calls: FN,R4,R5,assemblyR0,CCR. */
    uint32_t history[4][5];
    uint32_t copyback_ccr;
};
enum kui_native_ocram_map_reason {
    KUI_OC_MAP_NOT_CALLED=0, KUI_OC_MAP_OCRAM=1, KUI_OC_MAP_READ_DESTINATION=2,
    KUI_OC_MAP_TRANSLATION=3, KUI_OC_MAP_CACHE_MODE=4, KUI_OC_MAP_SPAN=5,
    KUI_OC_MAP_MAIN_RAM=6, KUI_OC_MAP_MAIN_RAM_RANGE=7,
    KUI_OC_MAP_CPU_VIRTUAL=8, KUI_OC_MAP_VIRTUAL_RANGE=9,
    KUI_OC_MAP_VIRTUAL_PAYLOAD=10, KUI_OC_MAP_CE_EVENT_BINDING=11,
    KUI_OC_MAP_CPU_VIRTUAL_PAYLOAD=12
};
_Static_assert(offsetof(struct kui_native_ocram_diagnostic,history)==KUI_OC_TRACE_HISTORY,
    "OCRAM diagnostic history ABI");
_Static_assert(offsetof(struct kui_native_ocram_diagnostic,copyback_ccr)==KUI_OC_TRACE_COPYBACK,
    "OCRAM diagnostic cache-mode ABI");
_Static_assert(sizeof(struct kui_native_ocram_diagnostic)==KUI_OC_TRACE_BYTES,
    "OCRAM diagnostic assembly ABI");
extern volatile struct kui_native_ocram_diagnostic kui_native_ocram_trace;
#endif
#endif
