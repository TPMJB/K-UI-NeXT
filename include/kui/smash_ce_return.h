/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CE_RETURN_H
#define KUI_SMASH_CE_RETURN_H
#define KUI_SMASH_CE_RETURN_MAGIC 0x31524353
#define KUI_SMASH_CE_RETURN_ABI 1
#define KUI_SMASH_CE_RETURN_CONTEXT 0x0001
#define KUI_SMASH_CE_RETURN_SOURCE 0x0002
#define KUI_SMASH_CE_RETURN_MMU 0x0004
#define KUI_SMASH_CE_RETURN_RUNTIME 0x0008
#define KUI_SMASH_CE_RETURN_PROTOCOL 0x0010
#define KUI_SMASH_CE_RETURN_MAILBOX 0x0020
#define KUI_SMASH_CE_RETURN_SD 0x0040
#define KUI_SMASH_CE_RETURN_LOAN 0x0080
#define KUI_SMASH_CE_RETURN_GD 0x0100
#define KUI_SMASH_CE_RETURN_MAPLE 0x0200
#define KUI_SMASH_CE_RETURN_DMA 0x0400
#define KUI_SMASH_CE_RETURN_RETIRED 0x0800
#define KUI_SMASH_CE_RETURN_GATE 0x1000
#define KUI_SMASH_CE_RETURN_UNQUALIFIED 0x2000
#ifndef __ASSEMBLER__
#include <stdint.h>
/* Source-bound terminal diagnostics only. No CE worker, retirement or cold
 * return authority is provided by these scalar observations. The low probe
 * always includes UNQUALIFIED and never resumes or overwrites the CE owner. */
struct kui_smash_ce_return_witness {
    uint32_t magic,abi,attempts,refused;
    uint32_t command,caller,entry_sr,entry_sp;
    uint32_t actual_sr,mmucr,ttb,ccr;
    uint32_t thread,process,runtime,loan;
    uint32_t maple,mden_before,mdst_before,mden_after;
    uint32_t mdst_after,retired,virtual_address,reserved;
};
_Static_assert(sizeof(struct kui_smash_ce_return_witness)==96u,"CE return witness ABI");
#endif
#endif
