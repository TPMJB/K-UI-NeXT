/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_TEXTURE_PROBE_H
#define KUI_VC2_TEXTURE_PROBE_H
#define KUI_VC2_TEXTURE_PROBE_ENTRY 0xacfce100
#define KUI_VC2_TEXTURE_PROBE_PRE 0x80000000
#ifndef __ASSEMBLER__
#include <stdint.h>
#include "../../src/loader/retail_display.h"
enum kui_vc2_texture_probe_reason {
    KUI_VC2_TEXTURE_MAP=1u,KUI_VC2_TEXTURE_MISMATCH=2u,
    KUI_VC2_TEXTURE_VIRTUAL_VRAM=4u,KUI_VC2_TEXTURE_TARGET_TAIL=8u,
    KUI_VC2_TEXTURE_LIMIT=16u,KUI_VC2_TEXTURE_PREPARE=32u,
    KUI_VC2_TEXTURE_SOURCE_CHANGED=64u,KUI_VC2_TEXTURE_EARLY_LATE_DIFF=128u,
    KUI_VC2_TEXTURE_PRESTATE=256u
};
struct kui_vc2_texture_fold {
    uint32_t source_hash,destination_hash,first_offset,expected,actual;
};
struct kui_vc2_texture_probe_snapshot {
    struct retail_display_state display;
    uint32_t stop[5]; /* reason flags, calls, compared bytes, target chunks, map failures */
    uint32_t chunk[5]; /* source, mapped/original destination, bytes, stream offset, FAD */
    struct kui_vc2_texture_fold fold;
    struct kui_vc2_texture_fold late,source,source_late;
    uint32_t pre_state[5]; /* status, source, mapped destination, bytes, stream offset */
    uint32_t dma_pre[4],dma_post[4]; /* DMA1 SAR,DAR,TCR,CHCR; read-only */
    uint32_t eviction_reads; /* current chunk's deliberate immutable P1 reads */
    uint8_t saved_source[512];
    uint32_t context[16];
    uint32_t cpu[5]; /* SR, MMUCR, TTB, CCR, PTEH */
    uint32_t tlb_first[6],tlb_last[6]; /* matches,index,address,data,physical,page mask */
    uint32_t coverage[5]; /* main-RAM, direct-VRAM, virtual, matched-virtual, unknown */
    uint32_t memory[8]; /* RAMFREE,RAMEND,effective,summary bounds,count,pointer,valid */
    uint32_t descriptors[3][5];
    uint32_t reader[16],prepare[5];
};
/* Readback uses aligned volatile16-bit destination reads, including odd
 * fragments. No destination byte access or cache operation is performed. */
void kui_vc2_texture_probe_fold(const uint8_t *,const void *,uint32_t,
    struct kui_vc2_texture_fold *);
/* Pure SH4 UTLB matcher. Returns1 only for one supplied valid entry under
 * its current ASID/SV/shared rule; callers count matches independently. */
uint32_t kui_vc2_texture_probe_utlb(uint32_t,uint32_t,uint32_t,uint32_t,
    uint32_t,uint32_t,uint32_t *);
/* Called only after the low preparer admitted this immutable helper and
 * set its service ownership marker. PRE in count snapshots <=512 source
 * bytes and returns; ordinary count compares after the unchanged low copy.
 * NULL source reports a map failure. Offset is within the GD request. */
void kui_vc2_texture_probe_run(const uint8_t *,void *,uint32_t,uint32_t);
void kui_vc2_texture_probe_entry(const uint8_t *,uint32_t,uint32_t,uint32_t);
void kui_vc2_ce_vmu_texture_context(uint32_t out[16]);
#ifdef KUI_VC2_TEXTURE_PROBE_HOST
uint32_t kui_vc2_texture_probe_host_read32(uint32_t);
uint32_t kui_vc2_texture_probe_host_sr(void);
void kui_vc2_texture_probe_host_stop(const struct kui_vc2_texture_probe_snapshot *)
    __attribute__((noreturn));
void kui_vc2_texture_probe_host_reset(void);
#endif
#endif
#endif
