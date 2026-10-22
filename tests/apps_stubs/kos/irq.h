#ifndef KUI_TEST_APPS_IRQ_H
#define KUI_TEST_APPS_IRQ_H
typedef unsigned irq_mask_t;
irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t state);
#endif
