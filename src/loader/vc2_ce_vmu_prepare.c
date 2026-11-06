/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vc2_ce_vmu_prepare.h"
#include "kui/vc2_ce_maple_profile.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include <string.h>
#if KUI_SMASH_SWIRL_VMU && !defined(KUI_VC2_CE_VMU_PREPARE_HOST)
__asm__(".global __kui_smash_swirl_ce_vmu\n.set __kui_smash_swirl_ce_vmu,1\n");
#endif

static uint32_t read_word(const volatile uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}
static void store_word(uint8_t *p,uint32_t value) {
    /* All three installation words are aligned and reached through P2 on
     * hardware. Keep one whole-word publication, including the final hook. */
    *(volatile uint32_t *)(void *)p=value;
}
static uint32_t refuse(struct kui_vc2_ce_vmu_prepare_report *r,
    uint32_t why,uint32_t actual,uint32_t expected) {
    r->reason=why;r->actual=actual;r->expected=expected;return why;
}
#if KUI_SMASH_SWIRL_VMU
static bool disjoint(const void *left,size_t ln,const void *right,size_t rn) {
    uintptr_t l=(uintptr_t)left,r=(uintptr_t)right;
    return ln<=UINTPTR_MAX-l && rn<=UINTPTR_MAX-r && (l+ln<=r || r+rn<=l);
}
static bool zero_bytes(const uint8_t *p,size_t n) {
    while(n--)if(*p++)return false;
    return true;
}
static bool relocated_valid(const uint8_t *out,const uint8_t *source,
    const uint8_t *bridge,size_t bytes) {
    const size_t worker=KUI_VC2_CE_MAPLE_WORKER_PATCH_RVA-KUI_VC2_CE_MAPLE_TEXT_RVA;
    const size_t cave=KUI_VC2_CE_MAPLE_BRIDGE_RVA-KUI_VC2_CE_MAPLE_TEXT_RVA;
    const size_t end=cave+KUI_VC2_CE_MAPLE_BRIDGE_BYTES;
    return !memcmp(out,source,worker) &&
        read_word(out+worker)==KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD &&
        !memcmp(out+worker+4u,source+worker+4u,KUI_VC2_CE_MAPLE_TEXT_VSIZE-worker-4u) &&
        zero_bytes(out+KUI_VC2_CE_MAPLE_TEXT_VSIZE,cave-KUI_VC2_CE_MAPLE_TEXT_VSIZE) &&
        !memcmp(out+cave,bridge,KUI_VC2_CE_MAPLE_BRIDGE_BYTES) && zero_bytes(out+end,bytes-end);
}
uint32_t kui_vc2_ce_vmu_install_body_relocated(uint8_t *body,size_t bytes,
    const uint8_t *bridge,size_t bridge_bytes,uint8_t *out,size_t out_bytes,
    kui_vc2_ce_vmu_publish publish,void *context,
    struct kui_vc2_ce_vmu_prepare_report *report,uint32_t mode) {
    if(!report)return KUI_VC2_PREPARE_ARGUMENT;
    memset(report,0,sizeof(*report));report->magic=UINT32_C(0x31504356);
    if(!body || ((uintptr_t)body&3u) || bytes!=KUI_VC2_CE_KERNEL_BODY_BYTES ||
       !bridge || bridge_bytes!=KUI_VC2_CE_MAPLE_BRIDGE_BYTES || !publish ||
       !out || ((uintptr_t)out&3u) || out_bytes!=KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES ||
       !disjoint(body,bytes,out,out_bytes) || !disjoint(bridge,bridge_bytes,out,out_bytes) ||
       !disjoint(report,sizeof(*report),out,out_bytes))
        return refuse(report,KUI_VC2_PREPARE_ARGUMENT,(uint32_t)out_bytes,
                      KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES);
    report->phase=1u;report->kernel_crc=kui_vc2_ce_maple_crc32(body,bytes);
    if(report->kernel_crc!=KUI_VC2_CE_KERNEL_BODY_CRC32 ||
       !kui_vc2_ce_maple_source_valid(body,bytes))
        return refuse(report,KUI_VC2_PREPARE_KERNEL,report->kernel_crc,KUI_VC2_CE_KERNEL_BODY_CRC32);
    uint32_t callback=read_word(bridge+0x54u);
    if((callback&3u) || callback<KUI_VC2_VMU_RESERVE_P2 ||
       callback>=KUI_VC2_VMU_RESERVE_P2+KUI_VC2_VMU_RESERVE_BYTES)
        return refuse(report,KUI_VC2_PREPARE_BRIDGE,callback,KUI_VC2_VMU_RESERVE_P2);
    const size_t ram=KUI_VC2_CE_RAMEND_WORD_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    const size_t text=KUI_VC2_CE_MAPLE_TEXT_RECORD_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    const size_t source_at=KUI_VC2_CE_MAPLE_TEXT_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    const size_t worker=KUI_VC2_CE_MAPLE_WORKER_PATCH_RVA-KUI_VC2_CE_MAPLE_TEXT_RVA;
    const size_t cave=KUI_VC2_CE_MAPLE_BRIDGE_RVA-KUI_VC2_CE_MAPLE_TEXT_RVA;
    const uint8_t *source=body+source_at;
    const uint32_t original_worker=read_word(source+worker);
    const unsigned reserve_only=mode==4u;
    report->phase=2u;
    if(!reserve_only) {
        memset(out,0,out_bytes);
        memcpy(out,source,KUI_VC2_CE_MAPLE_TEXT_VSIZE);
        memcpy(out+cave,bridge,bridge_bytes);
        store_word(out+worker,KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD);
        if(!relocated_valid(out,source,bridge,out_bytes))goto rollback;
    }
    store_word(body+ram,KUI_VC2_VMU_RAMEND_P1);
    /* Publish complete relocated code before the original O32 source pointer
     * can name it. Original Maple text and overlapping metadata are untouched. */
    publish(context);
    if(read_word(body+ram)!=KUI_VC2_VMU_RAMEND_P1 ||
       (!reserve_only && !relocated_valid(out,source,bridge,out_bytes)))goto rollback;
    if(!reserve_only) {
        store_word(body+text,KUI_VC2_CE_MAPLE_TEXT_EXTENDED);
        store_word(body+text+12u,KUI_SWIRL_CE_MAPLE_RELOCATE_P1);
        publish(context);
        if(read_word(body+ram)!=KUI_VC2_VMU_RAMEND_P1 ||
           read_word(body+text)!=KUI_VC2_CE_MAPLE_TEXT_EXTENDED ||
           read_word(body+text+12u)!=KUI_SWIRL_CE_MAPLE_RELOCATE_P1 ||
           !relocated_valid(out,source,bridge,out_bytes))goto rollback;
    }
    report->phase=3u;report->ram_end=read_word(body+ram);
    report->worker_word=reserve_only?original_worker:read_word(out+worker);
    return 0u;
rollback:
    store_word(body+text+12u,KUI_VC2_CE_MAPLE_TEXT_P1);
    store_word(body+text,KUI_VC2_CE_MAPLE_TEXT_VSIZE);
    store_word(body+ram,KUI_VC2_CE_ORIGINAL_RAMEND);
    if(!reserve_only)memset(out,0,out_bytes);
    publish(context);
    return refuse(report,KUI_VC2_PREPARE_VERIFY,read_word(source+worker),original_worker);
}
uint32_t kui_vc2_ce_vmu_install_body_mode(uint8_t *body,size_t bytes,
    const uint8_t *bridge,size_t bridge_bytes,kui_vc2_ce_vmu_publish publish,
    void *context,struct kui_vc2_ce_vmu_prepare_report *report,uint32_t mode) {
    return kui_vc2_ce_vmu_install_body_relocated(body,bytes,bridge,bridge_bytes,
        (uint8_t *)(uintptr_t)KUI_SWIRL_CE_MAPLE_RELOCATE_P2,KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES,
        publish,context,report,mode);
}
#else
uint32_t kui_vc2_ce_vmu_install_body_mode(uint8_t *body,size_t bytes,
    const uint8_t *bridge,size_t bridge_bytes,kui_vc2_ce_vmu_publish publish,
    void *context,struct kui_vc2_ce_vmu_prepare_report *report,uint32_t mode) {
    if(!report)return KUI_VC2_PREPARE_ARGUMENT;
    memset(report,0,sizeof(*report));report->magic=UINT32_C(0x31504356); /* VCP1 */
    if(!body || ((uintptr_t)body&3u) || bytes!=KUI_VC2_CE_KERNEL_BODY_BYTES ||
       !bridge || bridge_bytes!=KUI_VC2_CE_MAPLE_BRIDGE_BYTES || !publish)
        return refuse(report,KUI_VC2_PREPARE_ARGUMENT,(uint32_t)bytes,
                      KUI_VC2_CE_KERNEL_BODY_BYTES);
    report->phase=1u;
    report->kernel_crc=kui_vc2_ce_maple_crc32(body,bytes);
    if(report->kernel_crc!=KUI_VC2_CE_KERNEL_BODY_CRC32 ||
       !kui_vc2_ce_maple_source_valid(body,bytes))
        return refuse(report,KUI_VC2_PREPARE_KERNEL,report->kernel_crc,
                      KUI_VC2_CE_KERNEL_BODY_CRC32);
    /* The bridge is independently linked authored code within the admitted
     * helper blob. Its final literal must name that same reserved helper;
     * it must not become an arbitrary indirect-call publication. */
    const uint32_t callback=read_word(bridge+0x54u);
    if((callback&3u) || callback<KUI_VC2_VMU_RESERVE_P2 ||
       callback>=KUI_VC2_VMU_RESERVE_P2+KUI_VC2_VMU_RESERVE_BYTES)
        return refuse(report,KUI_VC2_PREPARE_BRIDGE,callback,KUI_VC2_VMU_RESERVE_P2);
    const size_t ram=KUI_VC2_CE_RAMEND_WORD_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    const size_t text=KUI_VC2_CE_MAPLE_TEXT_RECORD_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    const size_t worker=KUI_VC2_CE_MAPLE_WORKER_PATCH_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    const size_t padding=KUI_VC2_CE_MAPLE_PADDING_P1-KUI_VC2_CE_KERNEL_BODY_P1;
    const uint32_t original_worker=read_word(body+worker);
    const unsigned reserve_only=mode==4u;
    report->phase=2u;
    store_word(body+ram,KUI_VC2_VMU_RAMEND_P1);
    if(!reserve_only) {
        store_word(body+text,KUI_VC2_CE_MAPLE_TEXT_EXTENDED);
        memcpy(body+padding,bridge,bridge_bytes);
        if(read_word(body+ram)!=KUI_VC2_VMU_RAMEND_P1 ||
           read_word(body+text)!=KUI_VC2_CE_MAPLE_TEXT_EXTENDED ||
           memcmp(body+padding,bridge,bridge_bytes))goto rollback;
        /* Flush/invalidate code before making the original worker reach it. */
        publish(context);
        store_word(body+worker,KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD);
    }
    publish(context);
    if(read_word(body+ram)!=KUI_VC2_VMU_RAMEND_P1 ||
       read_word(body+worker)!=(reserve_only?original_worker:
                                KUI_VC2_CE_MAPLE_WORKER_PATCH_WORD))goto rollback;
    report->phase=3u;report->ram_end=read_word(body+ram);
    report->worker_word=read_word(body+worker);return 0u;
rollback:
    if(!reserve_only) {
        store_word(body+worker,original_worker);
        memset(body+padding,0,bridge_bytes);
        store_word(body+text,KUI_VC2_CE_MAPLE_TEXT_VSIZE);
    }
    store_word(body+ram,KUI_VC2_CE_ORIGINAL_RAMEND);
    publish(context);
    return refuse(report,KUI_VC2_PREPARE_VERIFY,read_word(body+worker),original_worker);
}
#endif
uint32_t kui_vc2_ce_vmu_install_body(uint8_t *body,size_t bytes,
    const uint8_t *bridge,size_t bridge_bytes,kui_vc2_ce_vmu_publish publish,
    void *context,struct kui_vc2_ce_vmu_prepare_report *report) {
    return kui_vc2_ce_vmu_install_body_mode(body,bytes,bridge,bridge_bytes,
                                         publish,context,report,0u);
}

#ifndef KUI_VC2_CE_VMU_PREPARE_HOST
#if KUI_VC2_VMU_WRITE_PROBE
__asm__(".global __kui_vc2_vmu_write_probe\n.set __kui_vc2_vmu_write_probe,1\n");
#endif
#if KUI_VC2_VMU_PERSIST
__asm__(".global __kui_vc2_vmu_persist\n.set __kui_vc2_vmu_persist,1\n");
#endif
#include "kui/vc2_ce_vmu_diagnostic.h"
extern const uint8_t __vc2_maple_bridge_blob_start[] __asm__("__vc2_maple_bridge_blob_start");
extern const uint8_t __vc2_maple_bridge_blob_end[] __asm__("__vc2_maple_bridge_blob_end");
extern const volatile uint32_t kui_armada_ce_maple_diagnostic_mode;
/* Written only after the low gate verified the immutable helper and its P2
 * entry initialized the entire mutable tail before saving into that stack.
 * Every high object and all kernel stores use P2; MAINANA later discards the
 * native cache tags before entering NK. No dirty P1 state may be retained. */
struct kui_vc2_ce_vmu_prepare_report kui_vc2_ce_vmu_prepare_report;
extern uint32_t kui_vc2_ce_vmu_service_validate(void *,uint32_t);
static void __attribute__((section(".text.vc2_prepare_publish"))) publish_kernel(void *unused) {
    (void)unused;
#if KUI_SMASH_SWIRL_VMU
    const uint32_t lines[]={KUI_VC2_CE_RAMEND_WORD_P1&~31u,KUI_VC2_CE_MAPLE_TEXT_RECORD_P1&~31u};
    for(unsigned i=0u;i<sizeof(lines)/sizeof(lines[0]);i++)
        __asm__ volatile("ocbi @%0"::"r"(lines[i]):"memory");
    for(uint32_t line=KUI_SWIRL_CE_MAPLE_RELOCATE_P1;
        line<KUI_SWIRL_CE_MAPLE_RELOCATE_P1+KUI_SWIRL_CE_MAPLE_RELOCATE_BYTES;line+=32u)
        __asm__ volatile("ocbi @%0"::"r"(line):"memory");
#else
    const uint32_t lines[]={KUI_VC2_CE_RAMEND_WORD_P1&~31u,
        KUI_VC2_CE_MAPLE_TEXT_RECORD_P1&~31u,KUI_VC2_CE_MAPLE_WORKER_PATCH_P1&~31u};
    for(unsigned i=0u;i<sizeof(lines)/sizeof(lines[0]);i++)
        __asm__ volatile("ocbi @%0"::"r"(lines[i]):"memory");
    for(uint32_t line=KUI_VC2_CE_MAPLE_PADDING_P1&~31u;
        line<KUI_VC2_CE_MAPLE_PADDING_P1+KUI_VC2_CE_MAPLE_BRIDGE_BYTES;line+=32u)
        __asm__ volatile("ocbi @%0"::"r"(line):"memory");
#endif
    volatile uint32_t *ccr=(volatile uint32_t *)(uintptr_t)UINT32_C(0xff00001c);
    *ccr|=UINT32_C(0x800); /* ICI; preserve enable, index and operand-RAM mode. */
    __asm__ volatile("nop\nnop\nnop\nnop\nnop\nnop\nnop\nnop":::"memory");
}
uint32_t kui_vc2_ce_vmu_prepare_run(void *region,uint32_t token,volatile void *trace) {
    uint32_t status=kui_vc2_ce_vmu_service_validate(region,token);
    if(status)return kui_vc2_ce_vmu_service_refuse(region,trace,status);
    /* Pre-NK has no virtual aliases. Publish native P1 data before examining
     * the physical kernel; stale dirty lines must cause a source refusal,
     * never a later write over freshly installed ROM metadata or bridge. */
    for(uint32_t at=KUI_VC2_CE_KERNEL_BODY_P1;
        at<KUI_VC2_CE_KERNEL_BODY_P1+KUI_VC2_CE_KERNEL_BODY_BYTES;at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    for(uint32_t at=UINT32_C(0x8ced4000);at<UINT32_C(0x8ced4800);at+=32u)
        __asm__ volatile("ocbp @%0"::"r"(at):"memory");
    const uint32_t prefix=kui_vc2_ce_vmu_crc32(
        (const volatile void *)(uintptr_t)UINT32_C(0xaced4000),0x800u);
    if(prefix!=KUI_VC2_VMU_PREFIX_CRC32) {
        refuse(&kui_vc2_ce_vmu_prepare_report,KUI_VC2_PREPARE_KERNEL,
               prefix,KUI_VC2_VMU_PREFIX_CRC32);
        kui_vc2_ce_vmu_diagnostic_stop(KUI_VC2_PREPARE_KERNEL,0u);
    }
    status=kui_vc2_ce_vmu_install_body_mode(
        (uint8_t *)(uintptr_t)(KUI_VC2_CE_KERNEL_BODY_P1|UINT32_C(0x20000000)),
        KUI_VC2_CE_KERNEL_BODY_BYTES,__vc2_maple_bridge_blob_start,
        (size_t)(__vc2_maple_bridge_blob_end-__vc2_maple_bridge_blob_start),
        publish_kernel,NULL,&kui_vc2_ce_vmu_prepare_report,
        kui_armada_ce_maple_diagnostic_mode);
    if(status)kui_vc2_ce_vmu_diagnostic_stop(status,0u);
    status=kui_vc2_ce_vmu_service_own();
    if(status)kui_vc2_ce_vmu_diagnostic_stop(status,0u);
    return status;
}
#endif
