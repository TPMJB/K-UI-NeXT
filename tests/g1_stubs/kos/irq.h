/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_G1_IRQ_H
#define KUI_TEST_G1_IRQ_H
typedef unsigned irq_mask_t;
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t);
#endif
