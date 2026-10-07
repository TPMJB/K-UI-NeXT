/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)

    .text
    .align 2
    .global C(kui_cdda_bios_vector_entry)
    .type C(kui_cdda_bios_vector_entry),@function
C(kui_cdda_bios_vector_entry):
    /* Native vector args r4/r5/r6/r7 already match the four-integer C ABI.
     * Save the caller's frame on its current stack. The C dispatcher guards
     * before any worker-stack switch; this entry never resets worker SP. */
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    mov.l .Lnative_dispatch,r0
    jsr @r0
    nop
    /* Keep the signed r0 result unchanged while restoring the caller. */
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    lds.l @r15+,pr
    rts
    nop
    .size C(kui_cdda_bios_vector_entry),.-C(kui_cdda_bios_vector_entry)
    .align 2
.Lnative_dispatch:
    .long C(cdda_bios_native_dispatch)
    .section .note.GNU-stack,"",@progbits
