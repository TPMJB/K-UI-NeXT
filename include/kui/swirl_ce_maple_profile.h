/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SWIRL_CE_MAPLE_PROFILE_H
#define KUI_SWIRL_CE_MAPLE_PROFILE_H
/* Compile-time Sega Swirl source profile. The shared nested-CE implementation
 * consumes its existing KUI_VC2_* ABI; selecting this file changes no profile
 * at runtime. Exact source: SWIRL/0WINCEOS.BIN SHA256
 * a19080f2e243c04e01a3a473c7a3caa36fb85a5a8fbb2163c8f7a1b2c8ed5ea8.
 * Derivation and scalar decoder checks are in the Swirl source audits. */
/* Source-specific facts from the exact owner kernel. These constants do not
 * establish a live reservation, a mapped callback or guest packet ownership.
 * The bridge below wraps the ordinary worker call; it does not replace the
 * completion prologue or import an Armada driver offset. */
#define KUI_VC2_CE_KERNEL_FILE_BYTES       0x001be800
#define KUI_VC2_CE_KERNEL_PREFIX_BYTES     0x00000800
#define KUI_VC2_CE_KERNEL_BODY_BYTES       0x001be000
#define KUI_VC2_CE_KERNEL_BODY_P1          0x8c010000
#define KUI_VC2_CE_KERNEL_BODY_CRC32       0x14a27ac2
#define KUI_VC2_CE_ROMHDR_P1               0x8c1cd61c
#define KUI_VC2_CE_ROMHDR_BYTES            84
#define KUI_VC2_CE_ROMHDR_CRC32            0x83b75bbc
#define KUI_VC2_CE_RAMEND_WORD_P1          0x8c1cd638
#define KUI_VC2_CE_ORIGINAL_RAMEND         0x8cfd0000

#define KUI_VC2_CE_MAPLE_NOMINAL_BASE      0x01ce0000
#define KUI_VC2_CE_MAPLE_TEXT_RECORD_P1    0x8c0a6fb4
#define KUI_VC2_CE_MAPLE_TEXT_RECORD_CRC32 0x295e08ba
#define KUI_VC2_CE_MAPLE_TEXT_VSIZE        0x00006c62
#define KUI_VC2_CE_MAPLE_TEXT_EXTENDED     0x00006e00
#define KUI_VC2_CE_MAPLE_TEXT_RVA          0x00001000
#define KUI_VC2_CE_MAPLE_TEXT_P1           0x8c0b1000
#define KUI_VC2_CE_MAPLE_WORKER_PATCH_RVA  0x00007270
#define KUI_VC2_CE_MAPLE_WORKER_PATCH_P1   0x8c0b7270
#define KUI_VC2_CE_MAPLE_WORKER_SOURCE_CRC 0xae094759
/* Authored NOP; BSR bridge. The unchanged third halfword remains the
 * source worker's argument-setup delay instruction. */
#define KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD 0xb4f70009
#define KUI_VC2_CE_MAPLE_WORKER_RESUME_RVA 0x00007276
#define KUI_VC2_CE_MAPLE_COMPLETION_RVA    0x00006f98
#define KUI_VC2_CE_MAPLE_SETKMODE_RVA      0x0000783c
#define KUI_VC2_CE_MAPLE_SETKMODE_ORDINAL  630
#define KUI_VC2_CE_MAPLE_ROOT_RVA          0x000082b8
#define KUI_VC2_CE_MAPLE_WAIT_RVA          0x000077dc
#define KUI_VC2_CE_MAPLE_WAIT_IAT_RVA      0x00008024
#define KUI_VC2_CE_MAPLE_WAIT_ORDINAL      497
/* The ordinary worker waits on the DMA event at +178. Its CreateThread
 * HANDLE is separately stored at +180; only that self-thread handle is an
 * admissible one-ms wait that cannot consume a Maple completion event. */
#define KUI_VC2_CE_MAPLE_DMA_EVENT_OFFSET  0x00000178
#define KUI_VC2_CE_MAPLE_THREAD_HANDLE_OFFSET 0x00000180

#define KUI_VC2_CE_MAPLE_PADDING_RVA       0x00007c64
#define KUI_VC2_CE_MAPLE_PADDING_P1        0x8c0b7c64
#define KUI_VC2_CE_MAPLE_PADDING_BYTES     412
#define KUI_VC2_CE_MAPLE_BRIDGE_RVA        KUI_VC2_CE_MAPLE_PADDING_RVA
#define KUI_VC2_CE_MAPLE_BRIDGE_BYTES      100
#define KUI_VC2_CE_MAPLE_BRIDGE_CALLBACK_OFFSET 0x54
#define KUI_VC2_CE_MAPLE_BRIDGE_PRE_RETURN_RVA 0x00007c88
#define KUI_VC2_CE_MAPLE_BRIDGE_END_RVA    0x00007cc8
#define KUI_VC2_CE_MAPLE_RESERVE_P1        0x8cfc4000
#define KUI_VC2_CE_MAPLE_RESERVE_END_P1    0x8cfd0000

/* Verified NK and extension geometry. The scalar audit is independent of
 * the live repeated mapping/cache/owner checks required by a sampler. */
#define KUI_VC2_CE_CURRENT_PROCESS_P1     0x8c1d1890
#define KUI_VC2_CE_CURRENT_THREAD_P1      0x8c1d1894
#define KUI_VC2_CE_PROCESS_TABLE_P1       0x8c1da6e0
#define KUI_VC2_CE_PROCESS_STRIDE         0x0000009c
#define KUI_VC2_CE_PROCESS_COUNT          32
#define KUI_VC2_CE_MAPLE_EXTENSION_BYTES  0x000001a0

/* Swirl packs other modules' metadata immediately after true Maple VSIZE.
 * Never write the apparent stored tail in the original kernel. Reserve a
 * separate32KiB source, copy meaningful text and zero only its private tail.
 * The nominal CE addresses and original O32 real-address field stay fixed. */
#define KUI_SWIRL_CE_MAPLE_RELOCATE_P1 0x8cfb8000
#define KUI_SWIRL_CE_MAPLE_RELOCATE_P2 0xacfb8000
#define KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES 0x8000

#endif
