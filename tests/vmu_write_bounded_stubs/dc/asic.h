/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_WRITE_ASIC_H
#define KUI_TEST_WRITE_ASIC_H
#include <stdint.h>
#define ASIC_EVT_MAPLE_DMA UINT16_C(0x000c)
typedef void (*asic_evt_handler)(uint32_t,void *);
typedef struct {asic_evt_handler hdl;void *data;} asic_evt_handler_entry_t;
asic_evt_handler_entry_t asic_evt_set_handler(uint16_t,asic_evt_handler,void *);
#endif
