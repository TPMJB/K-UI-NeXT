/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_PLATFORM_SCI_H
#define KUI_TEST_PLATFORM_SCI_H
#include <stddef.h>
#include <stdint.h>
#define SCI_OK 0
int sci_spi_rw_data(const uint8_t *out, uint8_t *in, size_t bytes);
#endif
