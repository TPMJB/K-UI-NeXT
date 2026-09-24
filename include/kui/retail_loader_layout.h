/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_LOADER_LAYOUT_H
#define KUI_RETAIL_LOADER_LAYOUT_H

/* First native-GD launch experiment. A temporary high stage loads the owner's
 * IP and executable. The resident replaces the unused lower IP area before
 * entering owner bootstrap2 with explicitly initialized retail CPU state.
 * Firmware, IP metadata/TOC, upper bootstrap/VBR/stack and all normal game RAM
 * are outside the resident reservation. This is not a universal SDK promise. */
#define KUI_RETAIL_PACKAGE_MAGIC "KUIRBT01"
#define KUI_RETAIL_PACKAGE_VERSION 1
#define KUI_RETAIL_HEADER_OFFSET 0x100
#define KUI_RETAIL_HEADER_BYTES 64
#define KUI_RETAIL_MAP_OFFSET 0x1000
#define KUI_RETAIL_MAP_BYTES 0x1000
#define KUI_RETAIL_STAGE_BLOB_OFFSET 0x2000
#define KUI_RETAIL_STAGE_ADDRESS 0x8ce00000
#define KUI_RETAIL_STAGE_MAX_BYTES 0x100000
#define KUI_RETAIL_STAGE_MEMORY_END 0x8cfe0000
#define KUI_RETAIL_STAGE_STACK 0x8cff0000
#define KUI_RETAIL_IP_ADDRESS 0x8c008000
#define KUI_RETAIL_IP_BYTES 0x8000
#define KUI_RETAIL_BOOT2_ADDRESS 0x8c00e000
#define KUI_RETAIL_BOOT_VBR 0x8c00f400
#define KUI_RETAIL_EXEC_ADDRESS 0x8c010000
#define KUI_RETAIL_EXEC_MAX_BYTES 0xc00000
#define KUI_RETAIL_TRAMPOLINE_BYTES 128
#define KUI_RETAIL_RESIDENT_ADDRESS 0x8c008300
#define KUI_RETAIL_RESIDENT_LIMIT 0x8c00d000
#define KUI_RETAIL_HOOK_STACK_BOTTOM 0x8c00d000
#define KUI_RETAIL_HOOK_STACK 0x8c00e000
#define KUI_RETAIL_RAM_END 0x8d000000

#endif
