/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_G1_ASIC_H
#define KUI_TEST_G1_ASIC_H
#include <stdint.h>
#define ASIC_EVT_GD_COMMAND 0x0100
#define ASIC_ACK_A UINT32_C(0xa05f6900)
#define ASIC_ACK_B UINT32_C(0xa05f6904)
#define ASIC_ACK_C UINT32_C(0xa05f6908)
#define ASIC_IRQD_B UINT32_C(0xa05f6914)
#define ASIC_IRQB_B UINT32_C(0xa05f6924)
#define ASIC_IRQ9_B UINT32_C(0xa05f6934)
typedef void (*asic_evt_handler)(uint32_t,void *);
typedef struct {asic_evt_handler hdl;void *data;} asic_evt_handler_entry_t;
asic_evt_handler_entry_t asic_evt_set_handler(uint16_t,asic_evt_handler,void *);
#endif
