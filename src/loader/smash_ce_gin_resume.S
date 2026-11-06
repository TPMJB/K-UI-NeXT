/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_ce_gin.h"
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .text
    .align 2
    .global C(kui_smash_ce_gin_resume)
    .type C(kui_smash_ce_gin_resume),@function
C(kui_smash_ce_gin_resume):
/* Called through P2, with R4 naming the restored original P1 frame and R5
 * carrying the captured CCR. SR's RB bit remains 1 for this entire routine.
 * Purge while the current normal-index cache profile is still active; this
 * includes the restored frame and all writes made by backend adoption. */
    mov.l .Lram,r0
    mov.l .Lramend,r1
.Lpurge:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lpurge
    mov.l .Lflush,r0
    or r5,r0
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
    mov r4,r15
    lds.l @r15+,macl
    lds.l @r15+,mach
    ldc.l @r15+,gbr
    ldc.l @r15+,vbr
    ldc.l @r15+,ssr
    lds.l @r15+,pr
    mov.l .Ltarget,r0
    ldc r0,spc
    mov.l @r15+,r14
    mov.l @r15+,r13
    mov.l @r15+,r12
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    mov.l @r15+,r7
    mov.l @r15+,r6
    mov.l @r15+,r5
    mov.l @r15+,r4
    mov.l @r15+,r3
    mov.l @r15+,r2
    mov.l @r15+,r1
    mov.l @r15+,r0
    rte
    nop
    .align 2
.Lram: .long 0x8c000000
.Lramend: .long 0x8d000000
.Lflush: .long 0x808
.Lccr: .long 0xff00001c
.Ltarget: .long KUI_SMASH_CE_GIN_TARGET_P2
    .size C(kui_smash_ce_gin_resume),.-C(kui_smash_ce_gin_resume)
