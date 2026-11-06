/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VC2_CE_VMU_HANDOFF_H
#define KUI_VC2_CE_VMU_HANDOFF_H
#ifndef KUI_VC2_VMU_LATE
#define KUI_VC2_VMU_LATE 0
#endif
#ifndef KUI_VC2_VMU_PERSIST
#define KUI_VC2_VMU_PERSIST 0
#endif

#ifndef KUI_SMASH_SWIRL_VMU
#define KUI_SMASH_SWIRL_VMU 0
#endif
#if KUI_SMASH_SWIRL_VMU != 0 && KUI_SMASH_SWIRL_VMU != 1
#error "Swirl VMU source profile must be explicitly zero or one"
#endif

/* The nested VC2 experiment admits this prepared page only after MAINANA
 * has delivered the complete exact CE kernel. Before then these bytes are
 * unowned staging, not a reservation. The low gate verifies every initialized
 * immutable byte before its fixed P2 entry clears the entire working tail. */
#define KUI_VC2_VMU_RESERVE_P1 0x8cfc4000
#define KUI_VC2_VMU_RESERVE_P2 0xacfc4000
#define KUI_VC2_VMU_RESERVE_END 0x8cfd0000
#define KUI_VC2_VMU_RESERVE_BYTES 0xc000
#if KUI_SMASH_SWIRL_VMU
#define KUI_VC2_VMU_RAMEND_P1 0x8cfb8000
#elif KUI_VC2_VMU_PERSIST
#define KUI_VC2_VMU_RAMEND_P1 0x8cfc0000
#else
#define KUI_VC2_VMU_RAMEND_P1 KUI_VC2_VMU_RESERVE_P1
#endif
#define KUI_VC2_VMU_INIT_P2 KUI_VC2_VMU_RESERVE_P2
#define KUI_VC2_VMU_GD_WAIT_P2 0xacfce000
#if KUI_SMASH_SWIRL_VMU
#define KUI_VC2_VMU_KERNEL_FAD 493127
#define KUI_VC2_VMU_KERNEL_SECTORS 893
#define KUI_VC2_VMU_KERNEL_BYTES 0x1be800
#define KUI_VC2_VMU_BODY_BYTES 0x1be000
#define KUI_VC2_VMU_BODY_CRC32 0x14a27ac2
#define KUI_VC2_VMU_PREFIX_CRC32 0x7e29bc00
#else
#define KUI_VC2_VMU_KERNEL_FAD 529805
#define KUI_VC2_VMU_KERNEL_SECTORS 899
#define KUI_VC2_VMU_KERNEL_BYTES 0x1c1800
#define KUI_VC2_VMU_BODY_BYTES 0x1c1000
#define KUI_VC2_VMU_BODY_CRC32 0x6d9c21a3
#define KUI_VC2_VMU_PREFIX_CRC32 0xa253bfef
#endif
#define KUI_VC2_VMU_ABORT_PR 0x8ce09362

#ifndef __ASSEMBLER__
#include <stddef.h>
#include <stdint.h>

/* Rich reader diagnostics put the measured value in VALUE and one of these
 * reasons in WHY. No failure code authorizes a kernel or hook write. */
enum kui_vc2_ce_vmu_gate_reason {
    KUI_VC2_VMU_GATE_OK = 0,
    KUI_VC2_VMU_GATE_CALLER_SR = 0x201,
    KUI_VC2_VMU_GATE_MMU = 0x202,
    KUI_VC2_VMU_GATE_STREAM = 0x203,
    KUI_VC2_VMU_GATE_SCI = 0x204,
    /* For the compact low gate, AUX is the exact offending MMIO address
     * and VALUE is its contents. These identify MMU/CCR and every DMA
     * channel without spending low code on a second per-register label. */
    KUI_VC2_VMU_GATE_HARDWARE = 0x205,
    KUI_VC2_VMU_GATE_CPU_DMA = KUI_VC2_VMU_GATE_HARDWARE,
    KUI_VC2_VMU_GATE_G1_DMA = 0x206,
    KUI_VC2_VMU_GATE_G2_DMA = 0x207,
    KUI_VC2_VMU_GATE_MAPLE_DMA = 0x208,
    KUI_VC2_VMU_GATE_PVR_DMA = 0x209,
    KUI_VC2_VMU_GATE_PREPARED_CRC = 0x20a,
    KUI_VC2_VMU_GATE_INITIALIZER = 0x20b,
    KUI_VC2_VMU_GATE_CACHE_MODE = 0x20c,
    KUI_VC2_VMU_GATE_OWNERSHIP = 0x20d,
    KUI_VC2_VMU_GATE_LOAD = 0x20f,
    KUI_VC2_VMU_GATE_RAW_CODE = 0x210
};

typedef uint32_t (*kui_vc2_ce_vmu_initializer)(void *,uint32_t,volatile void *);
uint32_t kui_vc2_ce_vmu_service_validate(void *,uint32_t);
uint32_t kui_vc2_ce_vmu_service_refuse(void *,volatile void *,uint32_t);
uint32_t kui_vc2_ce_vmu_service_own(void);
struct retail_display_state;
const struct retail_display_state *kui_vc2_ce_vmu_saved_display(void);
/* token,command,rawFAD,request,completed,priorPieces,error,status,pending,
 * active,opened,SCIacquired,SCIfault,SCIstate,initialized,source-region. */
void kui_vc2_ce_vmu_service_snapshot(uint32_t out[16]);

/* Both independently compiled low/high consumers invoke these. They are
 * compile-time ABI contracts, not data occupying the constrained resident. */
#ifndef KUI_RETAIL_ASYNC_TEST
#define KUI_VC2_VMU_NATIVE_LAYOUT_ASSERTS() \
    _Static_assert(sizeof(kui_retail_async_region_type)==0x640u,"VC2 native region ABI"); \
    _Static_assert(offsetof(kui_retail_async_region_type,engine)==0x440u,"VC2 native engine ABI"); \
    _Static_assert(offsetof(kui_retail_async_region_type,shared)==0x4acu,"VC2 native shared ABI"); \
    _Static_assert(sizeof(struct kui_retail_async)==108u,"VC2 native engine size ABI"); \
    _Static_assert(sizeof(struct kui_retail_gd)==168u,"VC2 native GD size ABI"); \
    _Static_assert(offsetof(struct kui_retail_gd,token)==44u,"VC2 native token ABI"); \
    _Static_assert(offsetof(struct kui_retail_gd,completed_bytes)==72u,"VC2 native completed ABI"); \
    _Static_assert(offsetof(struct kui_retail_gd,outputs)==116u,"VC2 native prior piece ABI"); \
    _Static_assert(offsetof(struct kui_retail_gd,status)==136u,"VC2 native status ABI"); \
    _Static_assert(offsetof(struct kui_retail_async_shared,stream)==296u,"VC2 native SCI state ABI"); \
    _Static_assert(offsetof(struct kui_retail_async_shared,port)==380u,"VC2 native SCI port ABI"); \
    _Static_assert(offsetof(struct kui_retail_manifest,title)==68u,"VC2 native trace overlay ABI")
#else
#define KUI_VC2_VMU_NATIVE_LAYOUT_ASSERTS() \
    _Static_assert(1,"Host field model; console ABI checked separately")
#endif

/* IEEE CRC32, deliberately table-free so the trusted low gate needs no
 * 1 KiB lookup table. Volatile reads make the P2 validation explicit. */
static inline uint32_t kui_vc2_ce_vmu_crc32(const volatile void *data,size_t bytes) {
    const volatile uint8_t *p=(const volatile uint8_t *)data;
    uint32_t crc=UINT32_MAX;
    while(bytes--) {
        crc^=*p++;
        for(unsigned bit=0u;bit<8u;++bit)
            crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
}

/* The immutable helper is word-aligned and its length is a multiple
 * of four. Folding each little-endian word through32 shifts is the same
 * IEEE CRC as four sequential bytes, with a smaller trusted low loop. */
static inline uint32_t kui_vc2_ce_vmu_crc32_words(const volatile void *data,size_t words) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    const volatile uint32_t *p=(const volatile uint32_t *)data;
    uint32_t crc=UINT32_MAX;
    while(words--) {
        crc^=*p++;
        for(unsigned bit=0u;bit<32u;++bit)
            crc=(crc>>1)^(UINT32_C(0xedb88320)&(0u-(crc&1u)));
    }
    return ~crc;
#else
    return kui_vc2_ce_vmu_crc32(data,words*4u);
#endif
}
#endif
#endif
