/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_SD_H
#define KUI_RETAIL_SD_H

#include "sd_reader.h"

/* Synchronous serial-SD ownership for the retail resident. The caller masks
 * interrupts across acquire/read/release and must serialize every use. These
 * functions never change the CPU interrupt mask or any timer register.
 *
 * Initialization borrows the pins, runs the existing read-only SD protocol and
 * returns the pins, retaining card.ready and the card's protocol state. Later
 * calls bracket kui_loader_sd_read() with acquire/release. A successful acquire
 * must always be paired with release, including after an SD error. Release is
 * idempotent. This singleton must be linked into the resident itself; none of
 * its function pointers or saved state may belong to the retired launcher.
 *
 * Active serial I/O, serial interrupts and queued FIFO bytes cause acquisition
 * to fail without writes. The pins cannot simultaneously serve a serial device.
 * Protocol timeout values become finite byte-work budgets, NOT elapsed time;
 * no hardware timer is borrowed from the game. */
enum kui_loader_sd_result kui_retail_sd_init(struct kui_loader_sd *card);
enum kui_loader_sd_result kui_retail_sd_acquire(void);
void kui_retail_sd_release(void);

#endif
