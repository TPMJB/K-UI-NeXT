/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_WRITE_IRQ_H
#define KUI_TEST_WRITE_IRQ_H
#include <stdint.h>
typedef uint32_t irq_mask_t;
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t);
#endif
