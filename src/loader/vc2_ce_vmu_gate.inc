/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_handoff.h"
#if KUI_SMASH_CE_RETURN
#include "kui/smash_ce_transport.h"
extern void kui_retail_menu_hook(void);
#endif
#if KUI_VC2_VMU_LATE
#include "kui/vc2_ce_vmu_late.h"
#include "kui/vc2_ce_vmu_late_seed.h"
#ifndef KUI_VC2_VMU_GATE_LOAD
#define KUI_VC2_VMU_GATE_LOAD(failed) \
    kui_vc2_vmu_late_read(RETAIL_MANIFEST->boot_lba,RETAIL_MANIFEST->gdi_crc32, \
        RETAIL_MANIFEST->boot_bytes,(uint8_t *)(uintptr_t)KUI_VC2_VMU_RESERVE_P2, \
        kui_vc2_ce_vmu_read_card,failed)
#endif
#endif
#ifndef KUI_VC2_VMU_BLOB_CRC32
#include "vc2_ce_vmu_symbols.h"
#endif

#ifndef KUI_VC2_VMU_GATE_READ32
#define KUI_VC2_VMU_GATE_READ32(address) \
    (*(volatile const uint32_t *)(uintptr_t)(address))
#endif
#ifndef KUI_VC2_VMU_GATE_PURGE
#define KUI_VC2_VMU_GATE_PURGE(address) \
    __asm__ volatile("ocbp @%0"::"r"(address):"memory")
#endif
#if KUI_VC2_VMU_LATE
#ifndef KUI_VC2_VMU_GATE_RAW_PURGE
#define KUI_VC2_VMU_GATE_RAW_PURGE() do { \
    for(uint32_t at=KUI_VC2_LATE_RAW_P1; \
        at<KUI_VC2_LATE_RAW_P1+KUI_VC2_LATE_RAW_BYTES;at+=32u) \
        KUI_VC2_VMU_GATE_PURGE(at); \
} while(0)
#endif
#ifndef KUI_VC2_VMU_GATE_RAW_CRC
#define KUI_VC2_VMU_GATE_RAW_CRC() \
    kui_vc2_ce_vmu_crc32_words((const volatile void *)(uintptr_t) \
        KUI_VC2_LATE_RAW_P2,KUI_VC2_LATE_RAW_BYTES/4u)
#endif
#endif
#ifndef KUI_VC2_VMU_GATE_BLOB_CRC
#if KUI_VC2_VMU_LATE
#define KUI_VC2_VMU_GATE_BLOB_CRC() \
    kui_vc2_ce_vmu_crc32_words((const volatile void *)(uintptr_t) \
        KUI_VC2_VMU_RESERVE_P2,KUI_VC2_VMU_RESERVE_BYTES/4u)
#else
#define KUI_VC2_VMU_GATE_BLOB_CRC() \
    kui_vc2_ce_vmu_crc32_words((const volatile void *)(uintptr_t) \
        KUI_VC2_VMU_RESERVE_P2,KUI_VC2_VMU_BLOB_BYTES/4u)
#endif
#endif
#if KUI_VC2_VMU_LATE
#define KUI_VC2_VMU_GATE_EXPECTED_CRC KUI_VC2_VMU_PREPARED_CRC32
#else
#define KUI_VC2_VMU_GATE_EXPECTED_CRC KUI_VC2_VMU_BLOB_CRC32
#endif
#ifndef KUI_VC2_VMU_GATE_INITIALIZE
#define KUI_VC2_VMU_GATE_INITIALIZE(region,token,trace) \
    ((kui_vc2_ce_vmu_initializer)(uintptr_t)KUI_VC2_VMU_INIT_P2)(region,token,trace)
#endif
KUI_VC2_VMU_NATIVE_LAYOUT_ASSERTS();

/* Include after report_fault/reader/service exist. Call immediately before
 * the GD core handles FN8: ABORT itself retires the legacy-stream fields.
 * Return zero for unrelated calls or successful preparation; a nonzero
 * result is a terminal experimental admission refusal with VALUE/WHY set.
 * No old native IRQ may execute between this point and original NK entry,
 * so the *caller's* IMASK15 is required, not merely our gateway's mask. */
static uint32_t vc2_ce_vmu_gate(uint32_t token,uint32_t function) {
    if(function!=KUI_GD_ABORT ||
       kui_native_ocram_trace.entry[5]!=KUI_VC2_VMU_ABORT_PR)return 0u;
    uint32_t address=0u;
    uint32_t value=kui_native_ocram_trace.entry[7];
    uint32_t why=KUI_VC2_VMU_GATE_CALLER_SR;
    if((value&UINT32_C(0xf0))!=UINT32_C(0xf0))goto refused;
    address=UINT32_C(0xff000010);
    value=KUI_VC2_VMU_GATE_READ32(address);
    why=KUI_VC2_VMU_GATE_HARDWARE;
    if(value&1u)goto refused;
    address=UINT32_C(0xff00001c);
    value=KUI_VC2_VMU_GATE_READ32(address);
    if(value&UINT32_C(0x80))goto refused;
    /* Hardware producers remain independent of IMASK. Refuse active or
     * armed producers rather than disabling an unowned native transfer. */
    for(unsigned i=0u;i<4u;++i) {
        address=UINT32_C(0xffa0000c)+16u*i;
        value=KUI_VC2_VMU_GATE_READ32(address);
        if((value&3u)==1u)goto refused;
    }
    address=UINT32_C(0xa05f7418);
    value=KUI_VC2_VMU_GATE_READ32(address);
    if(value&1u)goto refused;
    for(unsigned i=0u;i<4u;++i) {
        uint32_t base=UINT32_C(0xa05f7800)+32u*i;
        address=base+20u;
        value=KUI_VC2_VMU_GATE_READ32(address);
        if(value&1u)goto refused;
        address=base+24u;
        value=KUI_VC2_VMU_GATE_READ32(address);
        if(value&1u)goto refused;
    }
    address=UINT32_C(0xa05f6c14);
    value=KUI_VC2_VMU_GATE_READ32(address);
    if(value&1u)goto refused;
    address=UINT32_C(0xa05f6c18);
    value=KUI_VC2_VMU_GATE_READ32(address);
    if(value&1u)goto refused;
    address=UINT32_C(0xa05f6808);
    value=KUI_VC2_VMU_GATE_READ32(address);
    if(value&1u)goto refused;
    /* Publish any native dirty P1 lines before checking P2. Otherwise a
     * later eviction could overwrite a seemingly valid immutable helper.
     * MMU and index mode are admitted above. ORA may halve cache capacity;
     * it does not turn these canonical main-RAM operands into OCRAM.
     * The helper's first entry executes through P2. CE source-code
     * publication/cache handling belongs to its admitted initializer. */
    for(uint32_t at=KUI_VC2_VMU_RAMEND_P1;at<KUI_VC2_VMU_RESERVE_END;at+=32u)
        KUI_VC2_VMU_GATE_PURGE(at);
#if KUI_VC2_VMU_LATE
    /* Only this bounded P2 raw-card shim survives the native phase. Native
     * writes, including dirty aliases, must be detected before its first
     * instruction. It never contains the VMU helper or game track state. */
    KUI_VC2_VMU_GATE_RAW_PURGE();
    why=KUI_VC2_VMU_GATE_RAW_CODE;
    address=KUI_VC2_LATE_RAW_P2;
    value=KUI_VC2_VMU_GATE_RAW_CRC();
    if(value!=RETAIL_MANIFEST->ip_crc32)goto refused;
    why=KUI_VC2_VMU_GATE_LOAD;
    value=KUI_VC2_VMU_GATE_LOAD(&address);
    if(value)goto refused;
#endif
    value=KUI_VC2_VMU_GATE_BLOB_CRC();
    why=KUI_VC2_VMU_GATE_PREPARED_CRC;
    address=KUI_VC2_VMU_RESERVE_P2;
    if(!value || value!=
#if KUI_SMASH_CE_RETURN
       *(volatile const uint32_t *)((uintptr_t)RETAIL_MANIFEST+KUI_SMASH_CE_TRANSPORT_SEAL_OFFSET)
#else
       KUI_VC2_VMU_GATE_EXPECTED_CRC
#endif
       ) {
        /* WHY identifies this fixed staging address. Retain its first word
         * instead, to distinguish a missing entry from later code damage. */
        address=KUI_VC2_VMU_GATE_READ32(KUI_VC2_VMU_RESERVE_P2);
        goto refused;
    }
#if KUI_SMASH_CE_RETURN
    /* The exact current native stage sealed every transported byte only
     * after overwriting/syncing/readback of this launch's runtime. No stale
     * on-card CRC or runtime can supply this live RAM authority. The copy
     * leaf enters through P2 and publishes its low copy before erasing the
     * original helper padding; no old BOOT1 fetch can follow this ABORT. */
    address=KUI_SMASH_CE_RETURN_COPY_HIGH_P2;
    value=((uint32_t (*)(uint32_t))(uintptr_t)address)(
        (uint32_t)(uintptr_t)kui_retail_menu_hook);
    if(value)goto refused;
#endif
    /* The entire entry, its clear-loop bounds, cached C and bridge source
     * are now verified. The entry initializes all mutable tail bytes via P2
     * before its first stack save; native scratch contents are never trusted. */
    /* Pass the trusted low symbol itself, not a game-supplied pointer. A
     * high preparation refusal publishes the same AUX/VALUE/WHY tuple before
     * returning. This lets the low gate tail-return through the unchanged
     * initializer wrapper without retaining another diagnostic frame. */
    return KUI_VC2_VMU_GATE_INITIALIZE(&kui_retail_async_region,token,
        &kui_native_ocram_trace);
refused:
    kui_native_ocram_trace.entry[3]=address;
    kui_native_ocram_trace.entry[14]=value;
    kui_native_ocram_trace.entry[15]=why;
    return why;
}
