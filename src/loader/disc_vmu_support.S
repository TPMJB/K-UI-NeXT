/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)

    .section .disc_vmu_support_exports,"a"
    .align 2
    .global C(kui_disc_vmu_support_exports)
    .type C(kui_disc_vmu_support_exports),@object
C(kui_disc_vmu_support_exports):
    .long 0x44565331,1,64
    .long C(kui_disc_vmu_support_bind),C(kui_disc_vmu_support_sd_lease)
    .long C(kui_disc_vmu_support_terminal)
    .long __disc_vmu_support_bss_begin,__disc_vmu_support_bss_end
    .long __disc_vmu_support_end
    .long 0,0,0,0,0,0,0
    .size C(kui_disc_vmu_support_exports),.-C(kui_disc_vmu_support_exports)

    .section .text.disc_vmu_support_terminal,"ax"
    .align 2
    .global C(kui_disc_vmu_support_terminal)
    .type C(kui_disc_vmu_support_terminal),@function
C(kui_disc_vmu_support_terminal):
    /* No owner-stack access. Mask before inspecting resident/worker state. */
    stc sr,r0
    mov.l .Lmask,r1
    or r1,r0
    ldc r0,sr
    mov.l .Lstate,r1
    mov.l @r1,r0
    mov.l .Lready,r2
    cmp/eq r2,r0
    bf .Lstopped
    mov.l @(4,r1),r3
    mov.l @(8,r1),r4
    mov.l .Lexports,r1
    cmp/eq r1,r4
    bf .Lstopped
    mov.l .Lworker_bytes,r2
    sub r2,r1
    mov.l @r1,r0
    mov.l .Lworker_magic,r2
    cmp/eq r2,r0
    bf .Lstopped
    mov.l @(4,r1),r0
    cmp/eq #4,r0
    bf .Lstopped
    mov.l @(8,r1),r0
    cmp/eq #80,r0
    bf .Lstopped
    mov.l @(24,r1),r0
    cmp/eq r3,r0
    bf .Lstopped
    mov.l @(40,r1),r0
    cmp/eq r4,r0
    bf .Lstopped
    mov.l @(44,r1),r0
    cmp/eq r4,r0
    bf .Lstopped
    mov.l .Lalias,r2
    sub r2,r1
    add #80,r1
    cmp/hs r1,r3
    bf .Lstopped
    mov r4,r1
    sub r2,r1
    mov.l .Lstack_bytes,r0
    sub r0,r1
    cmp/hs r1,r3
    bt .Lstopped
    mov r3,r0
    tst #1,r0
    bf .Lstopped
    mov r4,r15
    sub r2,r15
    jmp @r3
    nop
.Lstopped:
    bra .Lstopped
    nop
    .align 2
.Lmask: .long 0x100000f0
.Lstate: .long C(kui_disc_vmu_support_state)
.Lready: .long 0x44565342
.Lexports: .long C(kui_disc_vmu_support_exports)
.Lworker_bytes: .long 0x12000
.Lworker_magic: .long 0x4b4e5031
.Lalias: .long 0x20000000
.Lstack_bytes: .long 4096
    .size C(kui_disc_vmu_support_terminal),.-C(kui_disc_vmu_support_terminal)
