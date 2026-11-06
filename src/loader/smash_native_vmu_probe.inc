/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_vmu_probe.h"
/* This worker is embedded only in the exact-owner native child module.
 * Its stage has verified the entire MDE source before arming this worker. */
__asm__(".global __kui_smash_native_vmu_probe_worker\n"
        ".set __kui_smash_native_vmu_probe_worker,1\n");
static struct kui_smash_native_vmu_probe smash_vmu_probe;
static bool smash_vmu_probe_read(void *context,uint32_t address,unsigned width,uint32_t *out) {
    (void)context;
    /* These application records are ordinary cached P1 data. Read the
     * original SDK alias so an unflushed connection callback stays visible. */
    return read_value(NULL,address,width,out);
}
static uint32_t __attribute__((noipa,unused)) smash_vmu_probe_value(uint32_t address,unsigned width) {
    uint32_t value=UINT32_MAX;(void)smash_vmu_probe_read(NULL,address,width,&value);return value;
}
static void smash_vmu_probe_report(void) __attribute__((noreturn));
static void smash_vmu_probe_report(void) {
#if defined(KUI_VMU_NATIVE_PASSIVE_HOST_TEST) && !defined(KUI_VMU_NATIVE_REPORT_HOST_RENDER)
    kui_vmu_native_passive_test_terminal(16u,state.error);
#else
    /* No SD operation or packet modification occurs in this terminal. */
    struct retail_display_state display;memcpy(display.regs,config.display,sizeof(display.regs));
    retail_display_restore(&display);
    retail_display_line("SMASH RPG VMU CHECK / ONE PAGE");
    retail_display_values("CHECK APP ERROR FREE",(uint32_t[5]){
        state.error,smash_vmu_probe_value(KUI_SMASH_BUP_APP,2u),
        smash_vmu_probe_value(KUI_SMASH_BUP_APP+4u,4u),smash_vmu_probe_value(KUI_SMASH_BUP_APP+58u,2u)},4u);
    uint32_t root=smash_vmu_probe_value(KUI_SMASH_SDK_ROOT_CELL,4u);
    bool valid=ram(root,0x1100u,4u);
    retail_display_values("SDK STATUS FUNCTION ROOT",(uint32_t[5]){
        smash_vmu_probe_value(KUI_SMASH_SDK_STATUS,4u),
        valid?smash_vmu_probe_value(root+0x4c4u,4u):UINT32_MAX,
        root},3u);
    retail_display_values("PRESENT PREV ADD REMOVE MASK",(uint32_t[5]){
        smash_vmu_probe_value(UINT32_C(0x8c5f9bdc),4u),smash_vmu_probe_value(UINT32_C(0x8c5f9bd8),4u),
        smash_vmu_probe_value(UINT32_C(0x8c5f9be0),4u),smash_vmu_probe_value(UINT32_C(0x8c5f9be4),4u),
        smash_vmu_probe_value(KUI_SMASH_BUP_MASK,4u)},5u);
    retail_display_values("JOB STATE BUSY RESULT CALLBACK",(uint32_t[5]){
        smash_vmu_probe_value(KUI_SMASH_BUP_JOB,4u),smash_vmu_probe_value(KUI_SMASH_BUP_JOB+4u,4u),
        smash_vmu_probe_value(KUI_SMASH_BUP_JOB+8u,4u),
        smash_vmu_probe_value(KUI_SMASH_BUP_JOB+116u,4u)},4u);
    retail_display_values("ADVERT INFO ALL MEDIA READ",(uint32_t[5]){
        overlay.stats.advertisements,overlay.stats.devinfo,overlay.stats.allinfo,overlay.stats.media,overlay.stats.reads},5u);
    retail_display_values("SUB DONE REQUEST REPLY LOAN",(uint32_t[5]){
        state.submitted,state.completed,overlay.stats.last_request,overlay.stats.last_reply,
        (uint32_t)durable_card.last_lease_result},5u);
    retail_display_line("PHOTO THIS PAGE THEN POWER OFF");
    for(;;)__asm__ volatile("nop");
#endif
}
static void __attribute__((unused)) smash_vmu_probe_complete(void) {
    if(kui_smash_native_vmu_probe_tick(&smash_vmu_probe,smash_vmu_probe_read,NULL)) {
        state.error=KUI_SMASH_VMU_PROBE_DETAIL;smash_vmu_probe_report();
    }
}
