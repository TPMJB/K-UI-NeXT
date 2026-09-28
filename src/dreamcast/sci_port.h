/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SCI_PORT_H
#define KUI_SCI_PORT_H
#include <stdbool.h>

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
#endif
