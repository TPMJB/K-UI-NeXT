/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef SCI_ASYNC_HEARTBEAT_TEST_SUPPORT_H
#define SCI_ASYNC_HEARTBEAT_TEST_SUPPORT_H
#include <stdint.h>
typedef unsigned irq_t;
typedef unsigned irq_mask_t;
typedef struct { uint32_t selected_context; } irq_context_t;
typedef void (*irq_hdl_t)(irq_t,irq_context_t *,void *);
typedef struct { irq_hdl_t hdl; void *data; } irq_cb_t;
#define EXC_TMU0_TUNI0 0x400u
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t mask);
irq_cb_t irq_get_handler(irq_t source);
int irq_set_handler(irq_t source,irq_hdl_t handler,void *data);
uint64_t timer_us_gettime64(void);
void kui_sci_async_heartbeat_test_reset(void);
#endif
