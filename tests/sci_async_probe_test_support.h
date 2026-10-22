/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SCI_ASYNC_PROBE_TEST_SUPPORT_H
#define KUI_SCI_ASYNC_PROBE_TEST_SUPPORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* A deliberately small KOS surface for the isolated host hardware model. */
typedef unsigned irq_t;
typedef unsigned irq_mask_t;
typedef struct { uint32_t pc, sr; } irq_context_t;
typedef void (*irq_hdl_t)(irq_t, irq_context_t *, void *);
typedef struct { irq_hdl_t hdl; void *data; } irq_cb_t;
typedef enum { IRQ_SRC_SCI1, IRQ_SRC_DMAC } irq_src_t;
enum { EXC_SCI_ERI = 0x4e0, EXC_SCI_RXI = 0x500,
       EXC_DMAC_DMTE1 = 0x660 };

irq_mask_t irq_disable(void);
void irq_restore(irq_mask_t mask);
bool irq_inside_int(void);
irq_cb_t irq_get_handler(irq_t source);
int irq_set_handler(irq_t source, irq_hdl_t handler, void *data);
unsigned irq_get_priority(irq_src_t source);
void irq_set_priority(irq_src_t source, unsigned priority);
uint64_t timer_us_gettime64(void);
/* KOS's uptime as seconds and TMU2 ticks (arch/timer.h); the model's TMU2
 * runs at 2 MHz. */
typedef struct { uint32_t secs, ticks; } timer_val_t;
timer_val_t kui_sci_async_test_ticks(void);

uint32_t kui_sci_async_test_read(uint32_t address, unsigned width);
void kui_sci_async_test_write(uint32_t address, uint32_t value, unsigned width);
void kui_sci_async_test_delay(uint32_t count);
uint32_t kui_sci_async_test_dma_address(const void *buffer, size_t count);
void kui_sci_async_test_cache_purge(void *buffer, size_t count);
void kui_sci_async_test_cache_invalidate(void *buffer, size_t count);
void kui_sci_async_test_work_tick(void);
void kui_sci_async_test_bus_fence(void);
#endif
