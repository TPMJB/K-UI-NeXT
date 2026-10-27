/* SPDX-License-Identifier: GPL-3.0-only */
/* Fixed-width linker-owned header. Every pointer is an explicit DIR32
 * relocation; the extraction tool must never guess pointer-looking words. */
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .vmu_native_worker_exports,"a"
    .align 2
    .global C(kui_vmu_native_worker_exports)
    .type C(kui_vmu_native_worker_exports),@object
C(kui_vmu_native_worker_exports):
    .long 0x4b4e5731,1,64
    .long C(kui_vmu_native_worker_probe)
    .long __vmu_native_worker_bss_begin,__vmu_native_worker_bss_end
    .long __vmu_native_worker_stack_bottom,__vmu_native_worker_stack_top
    .long __vmu_native_worker_end
    .long 0,0,0,0,0,0,0
    .size C(kui_vmu_native_worker_exports),.-C(kui_vmu_native_worker_exports)
