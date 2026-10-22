/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .vmu_native_worker_exports,"a"
    .align 2
    .global C(kui_vmu_native_worker_exports)
    .type C(kui_vmu_native_worker_exports),@object
C(kui_vmu_native_worker_exports):
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    .long 0x4b4e5031,1,80,C(kui_vmu_native_passive_arm)
#else
    .long 0x4b4e5031,1,64,C(kui_vmu_native_passive_arm)
#endif
    .long C(kui_vmu_native_passive_submit_hook),C(kui_vmu_native_passive_complete_hook)
    .long C(kui_vmu_native_passive_terminal)
    .long __vmu_native_worker_bss_begin,__vmu_native_worker_bss_end
    .long __vmu_native_worker_stack_bottom,__vmu_native_worker_stack_top
    .long __vmu_native_worker_end
    .long C(kui_vmu_native_passive_resume),C(kui_vmu_native_passive_consumer)
#if KUI_VMU_NATIVE_READONLY_PROBE
    .long C(kui_vmu_native_readonly_release_hook),C(kui_vmu_native_readonly_release_resume)
#else
    .long 0,0
#endif
#if KUI_VMU_NATIVE_PERSISTENT_PROBE
    .long C(kui_vmu_native_persistent_sd_transaction),0,0,0
#endif
    .size C(kui_vmu_native_worker_exports),.-C(kui_vmu_native_worker_exports)
