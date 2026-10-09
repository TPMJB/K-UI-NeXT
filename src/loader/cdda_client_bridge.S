/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)

    .text
    .align 2
    .global C(kui_cdda_client_call)
    .type C(kui_cdda_client_call),@function
C(kui_cdda_client_call):
    /* r4=entry, r5=context, r6=aligned destination stack top. */
    mov r4,r1
    mov r5,r4
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    mov r15,r0
    mov r6,r15
    add #-16,r15
    /* Keep the suspended caller-frame SP in destination memory, independent of
     * the callee-saved registers that the controlled probe will exercise. */
    mov.l r0,@r15
    jsr @r1
    nop
    /* Preserve entry's r0 return value while restoring the caller frame. */
    mov.l @r15,r1
    mov r1,r15
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
    .size C(kui_cdda_client_call),.-C(kui_cdda_client_call)

    .align 2
    .global C(kui_cdda_bridge_probe)
    .type C(kui_cdda_bridge_probe),@function
C(kui_cdda_bridge_probe):
    /* r4=service, r5=context. Save the probe caller's complete integer
     * callee-saved set before seeding registers for the service call. */
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    mov.l r12,@-r15
    mov.l r13,@-r15
    mov.l r14,@-r15
    mov r4,r0
    mov r5,r4
    mov #8,r8
    mov #9,r9
    mov #10,r10
    mov #11,r11
    mov #12,r12
    mov #13,r13
    mov #14,r14
    jsr @r0
    nop
.Lprobe_service_return:
    mov #0,r0
    mov #8,r1
    cmp/eq r1,r8
    bt .Lprobe_r9
    or #1,r0
.Lprobe_r9:
    mov #9,r1
    cmp/eq r1,r9
    bt .Lprobe_r10
    or #2,r0
.Lprobe_r10:
    mov #10,r1
    cmp/eq r1,r10
    bt .Lprobe_r11
    or #4,r0
.Lprobe_r11:
    mov #11,r1
    cmp/eq r1,r11
    bt .Lprobe_r12
    or #8,r0
.Lprobe_r12:
    mov #12,r1
    cmp/eq r1,r12
    bt .Lprobe_r13
    or #16,r0
.Lprobe_r13:
    mov #13,r1
    cmp/eq r1,r13
    bt .Lprobe_r14
    or #32,r0
.Lprobe_r14:
    mov #14,r1
    cmp/eq r1,r14
    bt .Lprobe_pr
    or #64,r0
.Lprobe_pr:
    sts pr,r2
    mov.l .Lprobe_return_address,r1
    cmp/eq r1,r2
    bt .Lprobe_restore
    or #128,r0
.Lprobe_restore:
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
    .size C(kui_cdda_bridge_probe),.-C(kui_cdda_bridge_probe)
    .align 2
.Lprobe_return_address:
    .long .Lprobe_service_return
    .section .note.GNU-stack,"",@progbits
