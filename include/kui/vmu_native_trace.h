/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_TRACE_H
#define KUI_VMU_NATIVE_TRACE_H
#include "kui/vmu_native_passive.h"
#include "kui/vmu_volatile_chain.h"
#define KUI_VMU_TRACE_WAIT_LIMIT UINT32_C(0x00100000)
#define KUI_VMU_TRACE_BLANK_TICKS UINT32_C(1562500)
/* Terminal-only snapshot. Packet headers contain no captured save payload.
 * The caller remains stopped while the bounded private frame array is shown. */
struct kui_vmu_native_trace_failure {
 uint32_t check,address,value,expected;
 uint32_t source,source_bytes,target,capacity,frame_count,nops;
 const struct kui_vmu_volatile_chain_frame *frames;
};
#if defined(KUI_VMU_NATIVE_PASSIVE_HOST_TEST) && !defined(KUI_VMU_NATIVE_TRACE_HOST_TEST)
static inline void kui_vmu_native_trace_init(const struct kui_vmu_native_passive_config *c) {(void)c;}
static inline void kui_vmu_native_trace_point(uint32_t c,const struct kui_vmu_native_passive_frame *f,uint32_t s,uint32_t n) {(void)c;(void)f;(void)s;(void)n;}
static inline int kui_vmu_native_trace_enabled(void) {return 0;}
static inline void kui_vmu_native_trace_failure(const struct kui_vmu_native_trace_failure *f) {(void)f;}
static inline void kui_vmu_native_trace_report(uint32_t c,uint32_t v,uint32_t a,uint32_t b) {(void)c;(void)v;(void)a;(void)b;__builtin_trap();}
#elif KUI_NATIVE_SPLIT_CONTROLLER_GENERAL && !defined(KUI_VMU_NATIVE_TRACE_IMPLEMENTATION)
/* General admits NORMAL only. Keep optional Evolution sampling out of its
 * retained callback path; dedicated diagnostic profiles retain the sampler. */
static inline bool kui_vmu_native_trace_image(const struct kui_vmu_native_discovery *d) {(void)d;return false;}
static inline void kui_vmu_native_trace_init(const struct kui_vmu_native_passive_config *c) {(void)c;}
static inline void kui_vmu_native_trace_point(uint32_t c,const struct kui_vmu_native_passive_frame *f,uint32_t s,uint32_t n) {(void)c;(void)f;(void)s;(void)n;}
static inline void kui_vmu_native_trace_game(uint32_t c,const struct kui_vmu_native_passive_frame *f) {(void)c;(void)f;}
static inline int kui_vmu_native_trace_enabled(void) {return 0;}
static inline void kui_vmu_native_trace_failure(const struct kui_vmu_native_trace_failure *f) {(void)f;}
static inline __attribute__((noreturn)) void kui_vmu_native_trace_report(uint32_t c,uint32_t v,uint32_t a,uint32_t b) {kui_vmu_native_runtime_fault(c,v,a,b);}
#else
bool kui_vmu_native_trace_image(const struct kui_vmu_native_discovery *);
void kui_vmu_native_trace_init(const struct kui_vmu_native_passive_config *);
void kui_vmu_native_trace_point(uint32_t,const struct kui_vmu_native_passive_frame *,uint32_t,uint32_t);
void kui_vmu_native_trace_game(uint32_t,const struct kui_vmu_native_passive_frame *);
void kui_vmu_native_trace_failure(const struct kui_vmu_native_trace_failure *);
void kui_vmu_native_trace_report(uint32_t,uint32_t,uint32_t,uint32_t) __attribute__((noreturn));
int kui_vmu_native_trace_enabled(void);
#endif
#endif
