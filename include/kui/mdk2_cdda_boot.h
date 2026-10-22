/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MDK2_CDDA_BOOT_H
#define KUI_MDK2_CDDA_BOOT_H
/* Scalar contracts for the admitted MDK2 executable. The shared low control
 * ABI is declared by toy_pilot_boot.h; none of these addresses admit another
 * title or grant ownership before the real allocator returns its lease. */
#define KUI_TOY_BOOT_HEAP_INIT 0x8c0a990c
#define KUI_TOY_BOOT_HEAP_POOL 0x8c0692d8
#define KUI_TOY_BOOT_HEAP_BEGIN_VALUE 0x8c4fe800
#define KUI_TOY_BOOT_HEAP_BYTES_VALUE 0x00b01800
#define KUI_TOY_BOOT_LEASE_BEGIN 0x8cfd0000
#define KUI_TOY_BOOT_LEASE_BYTES 0x00030000
#define KUI_TOY_BOOT_WORKER_LIMIT 0x8cfe0000
#define KUI_MDK2_BOOT_HEAP_ALLOCATE 0x8c0a99ba
#define KUI_MDK2_BOOT_HEAP_SENTINEL 0x8c4af714
#define KUI_MDK2_BOOT_HEAP_CURSOR 0x8c4af734
#define KUI_MDK2_BOOT_HEAP_BEGIN 0x8c4af738
#define KUI_MDK2_BOOT_HEAP_BYTES 0x8c4af73c
#define KUI_MDK2_BOOT_HEAP_BASE_COPY 0x8c4af740
#define KUI_MDK2_BOOT_CACHE_POLICY_POOL 0x8c0c5e40
#define KUI_MDK2_BOOT_CACHE_POLICY_NATIVE 0x00000105
#endif
