/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_NATIVE_STARTUP_STACK_H
#define KUI_VMU_NATIVE_STARTUP_STACK_H
#include "kui/retail_loader_layout.h"
/* The complete retained native reader and its private stack end below IP.
 * Original startup functions may put ordinary frames in the lower IP area;
 * Rayman's original startup reserves 16 KiB before its allocator call. Keep
 * 256 bytes above IP's lower boundary for every inherited 52-byte save frame,
 * while retaining the original upper bound at the boot VBR. No heap/SDK or
 * caller-identity check is changed by this physical stack interval. */
#define KUI_VMU_NATIVE_OWNER_STACK_LOW ((KUI_RETAIL_IP_ADDRESS & 0x1fffffff) + 0x100)
#define KUI_VMU_NATIVE_OWNER_STACK_HIGH (KUI_RETAIL_BOOT_VBR & 0x1fffffff)
#endif
