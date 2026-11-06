/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)

/* Cold transport/verification is not an SDK call. The owner is retired and
 * the caller has already published dirty RAM. Enter through P2 before any
 * CCR write; ICE accelerates P1 callback code, while OCE remains disabled.
 * Unknown live cache/MMU profiles preserve their original call verbatim. */
    .text
    .align 2
    .global C(kui_cold_load_call3)
    .type C(kui_cold_load_call3),@function
C(kui_cold_load_call3):
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    sts.l pr,@-r15
    mov r4,r8
    mov #0,r10
    mov.l .Lscope_p2,r0
    mov.l .Lp2,r1
    or r1,r0
    jmp @r0
    nop
.Lscope_uncached:
    mov.l .Lccr,r1
    mov.l @r1,r9
    mov.l .Lmmu,r1
    mov.l @r1,r0
    mov #1,r2
    tst r2,r0
    bf .Lcall
    mov r9,r0
    mov.l .Lunsupported,r1
    and r1,r0
    tst r0,r0
    bf .Lcall
    mov r8,r0
    mov.l .Larea,r1
    and r1,r0
    mov.l .Lcached_area,r1
    cmp/eq r1,r0
    bt .Lentry_ram
    mov.l .Luncached_area,r1
    cmp/eq r1,r0
    bf .Lcall
.Lentry_ram:
    mov r8,r0
    mov.l .Lphysical,r1
    and r1,r0
    mov.l .Lram,r1
    cmp/hs r1,r0
    bf .Lcall
    mov.l .Lramend,r1
    cmp/hs r1,r0
    bt .Lcall
    mov #1,r1
    tst r1,r0
    bf .Lcall
    mov.l .Lp1,r0
    and r0,r8
    mov #1,r10
    mov.l .Lccr,r1
    mov.l .Lice,r0
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Lcall:
    mov r5,r4
    mov r6,r5
    mov r7,r6
    jsr @r8
    nop
    mov r0,r8
    tst r10,r10
    bt .Ldone
    mov.l .Linvalidate,r0
    or r9,r0
    mov.l .Lccr,r1
    mov.l r0,@r1
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
.Ldone:
    mov r8,r0
    lds.l @r15+,pr
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    rts
    nop
    .align 2
.Lscope_p2: .long .Lscope_uncached
.Lp2: .long 0x20000000
.Lccr: .long 0xff00001c
.Lmmu: .long 0xff000010
.Lunsupported: .long 0xfffffeff
.Larea: .long 0xe0000000
.Lcached_area: .long 0x80000000
.Luncached_area: .long 0xa0000000
.Lphysical: .long 0x1fffffff
.Lram: .long 0x0c000000
.Lramend: .long 0x0d000000
.Lp1: .long 0xdfffffff
.Lice: .long 0x00000908
.Linvalidate: .long 0x00000808
    .size C(kui_cold_load_call3),.-C(kui_cold_load_call3)
