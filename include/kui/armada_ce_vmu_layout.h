/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_VMU_LAYOUT_H
#define KUI_ARMADA_CE_VMU_LAYOUT_H

/* C/assembler/linker-preprocessor scalars. The default is the accepted
 * 24 KiB readonly reservation. The separate experimental 48 KiB profile
 * reserves space only: it grants no WRITE, storage or worker capability.
 * Both are installed before NK; OEM globals still begin at 0x8cfd0000. */
#ifndef KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT
#define KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT 0
#endif
#if KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT != 0 && KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT != 1
#error "CE VMU WRITE layout must be explicitly disabled or enabled"
#endif
#if KUI_ARMADA_CE_MAPLE_WRITE_LAYOUT
#define KUI_ARMADA_CE_VMU_RESERVE_BEGIN 0x8cfc4000
#else
#define KUI_ARMADA_CE_VMU_RESERVE_BEGIN 0x8cfca000
#endif
#define KUI_ARMADA_CE_VMU_RESERVE_END 0x8cfd0000
#define KUI_ARMADA_CE_VMU_RESERVE_BYTES (KUI_ARMADA_CE_VMU_RESERVE_END-KUI_ARMADA_CE_VMU_RESERVE_BEGIN)
#define KUI_ARMADA_CE_VMU_HOOK_P2 (KUI_ARMADA_CE_VMU_RESERVE_BEGIN+0x20000000)
#define KUI_ARMADA_CE_VMU_STACK_BOTTOM 0xacfcf800
#define KUI_ARMADA_CE_VMU_STACK_TOP 0xacfcfe00
#define KUI_ARMADA_CE_VMU_HANDOFF_P2 0xacfce200
#define KUI_ARMADA_CE_VMU_HANDOFF_BYTES 1272
#define KUI_ARMADA_CE_VMU_SERVICES_ADDRESS 0xacfce700
#define KUI_ARMADA_CE_VMU_SERVICES_SIZE 32
#define KUI_ARMADA_CE_VMU_WORKER_STACK_BOTTOM 0xacfce800
#define KUI_ARMADA_CE_VMU_WORKER_STACK_TOP 0xacfcf800
#endif
