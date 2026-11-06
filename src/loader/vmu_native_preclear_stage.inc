/* SPDX-License-Identifier: GPL-3.0-only */
/* The exact reviewed startup clears its heap before the initializer hook.
 * Keep only this linked temporary stage alive across that clear. The low
 * installer tail wrapper completes the omitted interval after every high
 * callback returns; no game instruction resumes with partly cleared heap. */
__asm__(".global __kui_native_preheap_clear_stage\n"
        ".set __kui_native_preheap_clear_stage,1\n");
static struct kui_vmu_native_heap_preclear native_preclear;
static uint32_t native_preclear_begin,native_preclear_bytes;
/* P1 stage global whose value is the selected retained LOW tuple's P2
 * address. The installer tail's finish checker reads it before any erase. */
uint32_t kui_native_preclear_deferred;
extern void kui_native_preclear_stage_install(void);
#if KUI_NATIVE_DUAL_READERS
_Static_assert(KUI_VMU_NATIVE_HEAP_RESUME_OFFSET==LEGACY_KUI_VMU_NATIVE_HEAP_RESUME_OFFSET,
    "Both retained low readers must share the proved post-installer return offset");
#endif
static inline __attribute__((always_inline)) uint32_t native_preclear_installer(void) {
    return (uint32_t)(uintptr_t)(native_preclear.slot?
        kui_native_preclear_stage_install:kui_vmu_observe_stage_install);
}

static uint32_t __attribute__((noipa)) native_preclear_hook(
    uint32_t begin,uint32_t value,uint32_t bytes) {
    uint32_t pr,sr,sp;
    __asm__ volatile("sts pr,%0\nstc sr,%1\nmov r15,%2"
        :"=r"(pr),"=r"(sr),"=r"(sp)::"memory");
    uint32_t stack=sp&UINT32_C(0x1fffffff);
    volatile struct kui_vmu_observe_boot_control *c=control();
    if(kui_native_preclear_deferred!=NATIVE_LOW_HEAP_ORIGINAL+UINT32_C(0x20000000)+16u)
        native_report(2u,UINT32_C(0x51));
    volatile uint32_t *deferred=(void *)(uintptr_t)kui_native_preclear_deferred;
    if((pr&UINT32_C(0x1fffffff))!=(native_preclear.return_pr&UINT32_C(0x1fffffff)) ||
       ((sr>>4)&15u)!=15u || stack<KUI_VMU_NATIVE_OWNER_STACK_LOW ||
       stack>KUI_VMU_NATIVE_OWNER_STACK_HIGH || begin!=native_preclear.begin || value ||
       bytes!=native_preclear.bytes || c->status!=KUI_VMU_OBS_BOOT_ARMED ||
       native_word(native_preclear.slot)!=(uint32_t)(uintptr_t)native_preclear_hook ||
       deferred[0] || deferred[1] || deferred[2] || deferred[3])
        native_report(2u,UINT32_C(0x51));
    void *(*clear)(void *,int,uint32_t)=(void *)(uintptr_t)native_preclear.target;
    clear((void *)(uintptr_t)begin,0,native_preclear_begin-begin);
    uint32_t after=native_preclear_begin+native_preclear_bytes;
    clear((void *)(uintptr_t)after,0,begin+bytes-after);
    /* A later entry reaches this arena clear BEFORE the initializer's
     * terminal hook. Retire its unique literal to surviving LOW code so it
     * cannot erase the lease first. Only this first deferred clear keeps the
     * original helper in its pending tuple. The adjacent init stays hooked. */
    uint32_t terminal=NATIVE_LOW_RETURN_HOOK;
    *(volatile uint32_t *)(uintptr_t)native_preclear.slot=terminal;
    native_publish(native_preclear.slot);
    if(native_word(native_preclear.slot|UINT32_C(0x20000000))!=terminal ||
       c->status!=KUI_VMU_OBS_BOOT_ARMED)
        native_report(2u,UINT32_C(0x52));
    deferred[1]=native_preclear_begin;
    deferred[2]=native_preclear_bytes;
    deferred[3]=1u;
    __asm__ volatile("":::"memory");
    deferred[0]=native_preclear.target;
    /* The original nonempty zero loop returns R0=begin with T set. Its
     * argument/result ABI and the caller's reload-only path are preserved. */
    __asm__ volatile("sett":::"memory");
    return begin;
}

/* Called by the authored installer tail wrapper after the real installer
 * returns, while its saved LOW return PR is still available. No state is
 * consumed here: assembly restores that PR and its own high frame, snapshots
 * and clears all four LOW words, then tail-jumps the original stack-free leaf.
 * Its RTS lands directly on the existing LOW owner-frame restoration. */
uint32_t __attribute__((noipa)) kui_native_preclear_finish(uint32_t low_pr) {
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr));
    uint32_t first=KUI_RETAIL_STAGE_ADDRESS;
    uint32_t bytes=(uint32_t)(uintptr_t)__retail_stage_bss_end-first;
    uint32_t tuple=NATIVE_LOW_HEAP_ORIGINAL+UINT32_C(0x20000000)+16u;
    if(native_preclear.slot!=UINT32_C(0x8c0102d0) ||
       native_preclear.target!=UINT32_C(0x8c01c6e8) ||
       native_preclear.return_pr!=UINT32_C(0x8c010230) ||
       native_preclear.begin!=UINT32_C(0x8c3b0000) ||
       native_preclear.bytes!=UINT32_C(0x00c50000) ||
       native_preclear_begin!=first || native_preclear_bytes!=bytes ||
       kui_native_preclear_deferred!=tuple ||
       (sr&UINT32_C(0x100000f0))!=UINT32_C(0x100000f0) ||
       low_pr!=NATIVE_LOW_HEAP_HOOK+KUI_VMU_NATIVE_HEAP_RESUME_OFFSET)
        native_report(2u,UINT32_C(0x56));
    volatile struct kui_vmu_observe_boot_control *c=control();
    volatile const uint32_t *deferred=(void *)(uintptr_t)tuple;
    if(c->status!=KUI_VMU_OBS_BOOT_INSTALLED || c->installer || c->stage_stack ||
       deferred[0]!=native_preclear.target || deferred[1]!=first ||
       deferred[2]!=bytes || deferred[3]!=1u ||
       native_word(native_preclear.slot)!=NATIVE_LOW_RETURN_HOOK)
        native_report(2u,UINT32_C(0x57));
    return tuple;
}

static void __attribute__((noipa)) native_preclear_prepare(void) {
    struct kui_vmu_native_heap_preclear plan;
    int result=kui_vmu_native_heap_preclear_plan((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
        exec_bytes,native_driver.image_sha256,&native_heap,&native_call,&plan);
    if(result<0) native_report(2u,UINT32_C(0x53));
    memset(&native_preclear,0,sizeof(native_preclear));
    kui_native_preclear_deferred=0;
    if(!result) return;
    uint32_t first=KUI_RETAIL_STAGE_ADDRESS;
    uint32_t end=(uint32_t)(uintptr_t)__retail_stage_bss_end;
    /* Protect exactly the linked stage/code/BSS/installer stack, never the
     * retired relay stack or any extra game heap. All code/data that this
     * wrapper and installer need is within this one interval. */
    if(first<plan.begin || end<=first || end>plan.begin+plan.bytes ||
       ((first|end)&3u) || native_word(plan.slot)!=plan.target ||
       ((uintptr_t)native_preclear_hook&1u) ||
       (uintptr_t)native_preclear_hook<first || (uintptr_t)native_preclear_hook>=end)
        native_report(2u,UINT32_C(0x54));
    native_preclear=plan;native_preclear_begin=first;native_preclear_bytes=end-first;
    kui_native_preclear_deferred=NATIVE_LOW_HEAP_ORIGINAL+UINT32_C(0x20000000)+16u;
}
static void __attribute__((noipa)) native_preclear_arm(void) {
    if(!native_preclear.slot) return;
    if(control()->status!=KUI_VMU_OBS_BOOT_ARMED ||
       native_word(native_heap.initializer_slot)!=NATIVE_LOW_HEAP_HOOK ||
       native_word(native_preclear.slot)!=native_preclear.target)
        native_report(2u,UINT32_C(0x55));
    *(volatile uint32_t *)(uintptr_t)native_preclear.slot=(uint32_t)(uintptr_t)native_preclear_hook;
    native_publish(native_preclear.slot);
    if(native_word(native_preclear.slot|UINT32_C(0x20000000))!=(uint32_t)(uintptr_t)native_preclear_hook)
        native_report(2u,UINT32_C(0x55));
}
