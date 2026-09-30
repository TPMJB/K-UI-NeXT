/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SCI_PORT_H
#define KUI_SCI_PORT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* The SH-4's SCI port as an SPI bus (KOS dc/sci.h) for the network
 * adapters wired to it (docs/sci-connector.md), with one chip select at a
 * time: GPIO7 (port A pin 7, the usual W5500 point, which KOS's SCI driver
 * drives) or GPIO6 (the network connector's select, driven here). The Wi-Fi
 * board's READY line is GPIO5. Only the storage worker uses it, and the SD
 * card stays on SCIF. */
#define KUI_SCI_RATES 4u
/* rate: 0 (12.5 MHz) to 3 (1.5625 MHz); select: 6 or 7. */
bool kui_sci_open(unsigned rate, unsigned select);
void kui_sci_close(void);
bool kui_sci_running(void);
void kui_sci_select(bool active);
/* GPIO5's level. */
bool kui_sci_ready(void);

/* A transfer inside a selected frame with DMA channel 1 taking in the bytes
 * after the first (see sci_port.c), full duplex; out NULL clocks out 0xff.
 * Worth it from KUI_SCI_DMA_MIN bytes. It returns false when a piece did not
 * finish in time or ended with a receive error; the port is then ready for
 * a programmed transfer again, and what the device made of the frame is its
 * caller's business. kui_sci_dma_ready says whether the DMA controller can
 * be used at all. */
#define KUI_SCI_DMA_MIN 16u
bool kui_sci_dma_ready(void);
bool kui_sci_dma_transfer(const uint8_t *out, uint8_t *in, size_t bytes);

/* An async frame (kui_w5500_bus.frame_async): the header by a programmed
 * transfer, then `bytes` (at most KUI_SCI_ASYNC_MAX) written from `out` or
 * read into `in` by DMA channel 1 with no help from the CPU; `done` runs
 * from channel 1's transfer-end interrupt once the chip select is
 * released. Cancel gives up on one that has not ended (no `done`). */
#define KUI_SCI_ASYNC_MAX 4096u
bool kui_sci_async(const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes,
                   void (*done)(void *arg, bool ok), void *arg);
void kui_sci_async_cancel(void);
#endif
