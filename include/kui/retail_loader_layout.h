/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_LOADER_LAYOUT_H
#define KUI_RETAIL_LOADER_LAYOUT_H

/* Native-GD launch layout. A temporary high stage loads the owner's
 * IP and executable. The ordinary resident replaces the lower IP area before
 * entering owner bootstrap2 with explicitly initialized retail CPU state.
 * Firmware, IP metadata/TOC, upper bootstrap/VBR/stack and all normal game RAM
 * are outside that ordinary reservation; opt-in placement is described below.
 * This is not a universal SDK promise. */
#define KUI_RETAIL_PACKAGE_MAGIC "KUIRBT01"
#define KUI_RETAIL_PACKAGE_VERSION 1
#define KUI_RETAIL_HEADER_OFFSET 0x100
#define KUI_RETAIL_HEADER_BYTES 64
#define KUI_RETAIL_MAP_OFFSET 0x1000
#define KUI_RETAIL_MAP_BYTES 0x1000
#define KUI_RETAIL_STAGE_BLOB_OFFSET 0x2000
#define KUI_RETAIL_STAGE_ADDRESS 0x8ce00000
/* The Windows CE placement probe package: the same stage built with
 * KUI_RETAIL_CE and linked 64 KiB higher, leaving CE's 2 KiB boot prefix
 * destination (KUI_CE_LOAD_PREFIX_ADDRESS, 0x8ce01000) outside it. */
#define KUI_RETAIL_CE_PACKAGE_MAGIC "KUIRCE01"
#define KUI_RETAIL_CE_STAGE_ADDRESS 0x8ce10000
#define KUI_RETAIL_STAGE_MAX_BYTES 0x100000
#define KUI_RETAIL_STAGE_MEMORY_END 0x8cfe0000
#define KUI_RETAIL_STAGE_STACK 0x8cff0000
#define KUI_RETAIL_IP_ADDRESS 0x8c008000
#define KUI_RETAIL_IP_BYTES 0x8000
#define KUI_RETAIL_BOOT2_ADDRESS 0x8c00e000
#define KUI_RETAIL_BOOT_VBR 0x8c00f400
#define KUI_RETAIL_EXEC_ADDRESS 0x8c010000
/* Includes room for rounding the final sector without reaching the temporary
 * stage at 0x8ce00000. This bounds memory use, not a particular title's size. */
#define KUI_RETAIL_EXEC_MAX_BYTES 0xc00000
#define KUI_RETAIL_TRAMPOLINE_BYTES 128
/* Hardware-confirmed native firmware-area placement (normal retail build). Keep the complete code/data/stack
 * reservation below IP.BIN so ordinary owner startup frames can use the
 * lower IP area. The stage checks retained firmware vectors before copying.
 * Hardware confirmation covers the owner console and reported titles,
 * not every firmware or multidisc path. */
#ifndef KUI_RETAIL_LOW_RESIDENT
#define KUI_RETAIL_LOW_RESIDENT 0
#endif
#if KUI_RETAIL_LOW_RESIDENT != 0 && KUI_RETAIL_LOW_RESIDENT != 1
#error "KUI_RETAIL_LOW_RESIDENT must be 0 or 1"
#endif
#if KUI_RETAIL_LOW_RESIDENT && ((defined(KUI_RETAIL_SONIC_STACK_TEST) && KUI_RETAIL_SONIC_STACK_TEST) || (defined(KUI_RETAIL_STARTUP_TRACE) && KUI_RETAIL_STARTUP_TRACE))
#error "Low resident placement must run without owner RAM trace/stack patches"
#endif
#define KUI_RETAIL_LOW_RESIDENT_ADDRESS 0x8c004000
#define KUI_RETAIL_LOW_STANDARD_LIMIT 0x8c007800
#define KUI_RETAIL_LOW_ASYNC_LIMIT 0x8c007ba0
#define KUI_RETAIL_LOW_HOOK_STACK 0x8c007d00
#define KUI_RETAIL_LEGACY_RESIDENT_ADDRESS 0x8c008300
#define KUI_RETAIL_LEGACY_STANDARD_LIMIT 0x8c00bb00
#define KUI_RETAIL_LEGACY_ASYNC_LIMIT 0x8c00bea0
#define KUI_RETAIL_LEGACY_HOOK_STACK 0x8c00c000
#define KUI_RETAIL_RESIDENT_ADDRESS 0x8c008300
/* DOA2 T3601N V1.100 fills C000..F3FF with its startup stack marker.
 * Both resident state and the guarded service stack must stay below C000.
 * The native build checks the linked end and conservative .su stack sum. */
#define KUI_RETAIL_STANDARD_LIMIT 0x8c00bb00
/* The background SCI reader's resident (built with KUI_RETAIL_ASYNC) has its
 * worst-case stack proven from the compiler's call graph
 * (tools/check_retail_stack.py) instead of summing every frame, so its
 * guarded stack is 352 bytes and its image may extend to here. */
#define KUI_RETAIL_ASYNC_LIMIT 0x8c00bea0
/* The Windows CE boot test's reader (built with KUI_RETAIL_CE). CE enters
 * through bootstrap 2 at KUI_RETAIL_BOOT2_ADDRESS and never returns to the
 * IP's lower bootstrap area, so this reader may fill everything below C800
 * and keep a 2 KiB guarded stack above it, all below bootstrap 2. */
#define KUI_RETAIL_CE_LIMIT 0x8c00c800
#define KUI_RETAIL_CE_HOOK_STACK 0x8c00d000
/* The Windows CE background reader (KUI_RETAIL_CE and KUI_RETAIL_ASYNC).
 * Once booted, CE uses nothing below its kernel image at 0x8c010000 (its RAM
 * lies above that), so this reader may fill everything below D800 and keep a
 * 2 KiB guarded stack above it, ending where bootstrap 2 begins. */
#define KUI_RETAIL_CE_ASYNC_LIMIT 0x8c00d800
#define KUI_RETAIL_CE_ASYNC_HOOK_STACK 0x8c00e000
/* Four words in the CE readers' entry section the stage fills in: the
 * addresses of CE's pending-interrupt mask, interrupt ring head, reschedule
 * flag and interrupt handler table (retail_resident.S, kui_retail_ce_kernel). */
#define KUI_RETAIL_CE_KERNEL 0x8c008324
#define KUI_RETAIL_CE_KERNEL_WORDS 4
#if KUI_RETAIL_LOW_RESIDENT && !defined(KUI_RETAIL_CE)
#undef KUI_RETAIL_RESIDENT_ADDRESS
#undef KUI_RETAIL_STANDARD_LIMIT
#undef KUI_RETAIL_ASYNC_LIMIT
#define KUI_RETAIL_RESIDENT_ADDRESS KUI_RETAIL_LOW_RESIDENT_ADDRESS
#define KUI_RETAIL_STANDARD_LIMIT KUI_RETAIL_LOW_STANDARD_LIMIT
#define KUI_RETAIL_ASYNC_LIMIT KUI_RETAIL_LOW_ASYNC_LIMIT
#endif
#if defined(KUI_RETAIL_ASYNC) && defined(KUI_RETAIL_CE)
#define KUI_RETAIL_RESIDENT_LIMIT KUI_RETAIL_CE_ASYNC_LIMIT
#elif defined(KUI_RETAIL_ASYNC)
#define KUI_RETAIL_RESIDENT_LIMIT KUI_RETAIL_ASYNC_LIMIT
#elif defined(KUI_RETAIL_CE)
#define KUI_RETAIL_RESIDENT_LIMIT KUI_RETAIL_CE_LIMIT
#else
#define KUI_RETAIL_RESIDENT_LIMIT KUI_RETAIL_STANDARD_LIMIT
#endif
#define KUI_RETAIL_HOOK_STACK_BOTTOM KUI_RETAIL_RESIDENT_LIMIT
#ifndef KUI_RETAIL_CE
#if KUI_RETAIL_LOW_RESIDENT
#define KUI_RETAIL_HOOK_STACK KUI_RETAIL_LOW_HOOK_STACK
#else
#define KUI_RETAIL_HOOK_STACK 0x8c00c000
#endif
#elif defined(KUI_RETAIL_ASYNC)
#define KUI_RETAIL_HOOK_STACK KUI_RETAIL_CE_ASYNC_HOOK_STACK
#else
#define KUI_RETAIL_HOOK_STACK KUI_RETAIL_CE_HOOK_STACK
#endif
#define KUI_RETAIL_RAM_END 0x8d000000

#endif
