/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef KUI_SCI_SD_BUS_H
#define KUI_SCI_SD_BUS_H
#include "sd_reader.h"

/* Retail Dreamcast SCI: TXD1=MOSI, RXD1=MISO, SCK1=clock, PA7=CS.
 * One device only. No DMA, interrupts, heap or timer ownership. The caller
 * serializes access; snapshot/claim/release must run with IRQs masked.
 * Active SCI or unread data cause a no-write rejection. Every successful
 * claim must be released, even after protocol errors. This bus is shared by
 * the runtime and the freestanding game reader, linked independently. */
enum kui_loader_sd_result kui_sci_sd_acquire(void);
void kui_sci_sd_release(void);
const struct kui_loader_sd_bus *kui_sci_sd_bus(void);
bool kui_sci_sd_healthy(void);
#endif
