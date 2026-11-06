/* SPDX-License-Identifier: GPL-3.0-only */
/* Complete original predicate from2aac9af; independent negative control. */
static bool __attribute__((noipa)) reference_sdk_coherence_admit(void) {
    if(native_driver.family==KUI_VMU_NATIVE_FAMILY_EARLY_V1) return true;
    if(native_driver.family==KUI_VMU_NATIVE_FAMILY_EARLY_P2_V1 &&
       !kui_vmu_native_early_p2_calls_valid((const void *)(uintptr_t)KUI_RETAIL_EXEC_ADDRESS,
           exec_bytes,&native_driver,native_call.call,passive_layout.initializer_call)) return false;
    uint32_t slot;return native_sdk_coherence_slot(&slot);
}
