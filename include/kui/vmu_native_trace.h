/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_TRACE_H
#define KUI_VMU_NATIVE_TRACE_H
#include "kui/vmu_native_passive.h"
#define KUI_VMU_TRACE_WAIT_LIMIT UINT32_C(0x00100000)
#if defined(KUI_VMU_NATIVE_PASSIVE_HOST_TEST) && !defined(KUI_VMU_NATIVE_TRACE_HOST_TEST)
static inline void kui_vmu_native_trace_init(const struct kui_vmu_native_passive_config *c) {(void)c;}
static inline void kui_vmu_native_trace_point(uint32_t c,const struct kui_vmu_native_passive_frame *f,uint32_t s,uint32_t n) {(void)c;(void)f;(void)s;(void)n;}
static inline int kui_vmu_native_trace_enabled(void) {return 0;}
static inline void kui_vmu_native_trace_report(uint32_t c,uint32_t v,uint32_t a,uint32_t b) {(void)c;(void)v;(void)a;(void)b;__builtin_trap();}
#else
bool kui_vmu_native_trace_image(const struct kui_vmu_native_discovery *);
void kui_vmu_native_trace_init(const struct kui_vmu_native_passive_config *);
void kui_vmu_native_trace_point(uint32_t,const struct kui_vmu_native_passive_frame *,uint32_t,uint32_t);
void kui_vmu_native_trace_game(uint32_t,const struct kui_vmu_native_passive_frame *);
void kui_vmu_native_trace_report(uint32_t,uint32_t,uint32_t,uint32_t) __attribute__((noreturn));
int kui_vmu_native_trace_enabled(void);
#endif
#endif
