/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently authored, bounded completion-context observation only. */
    .section .bridge,"ax"
    .align 1
    .global _kui_armada_ce_maple_bridge
_kui_armada_ce_maple_bridge:
    mov.l r0,@-r15
    mov.l r1,@-r15
    mov.l r2,@-r15
    mov.l r3,@-r15
    mov.l r4,@-r15
    mov.l r5,@-r15
    mov.l r6,@-r15
    mov.l r7,@-r15
    sts.l pr,@-r15
    sts.l mach,@-r15
    sts.l macl,@-r15
    stc.l gbr,@-r15
    mov #1,r4
    bsr _kui_armada_ce_maple_bridge+(0x6374-0x67a4)
    nop
    mov.l r0,@-r15
    mov.l @(32,r15),r4
    mov r15,r5
    add #52,r5
    mov.l @(16,r15),r6
    mov.l @r15,r7
    mov.l .Lhelper,r0
    jsr @r0
    nop
    mov.l @r15+,r4
    bsr _kui_armada_ce_maple_bridge+(0x6374-0x67a4)
    nop
    ldc.l @r15+,gbr
    lds.l @r15+,macl
    lds.l @r15+,mach
    lds.l @r15+,pr
    mov.l @r15+,r7
    mov.l @r15+,r6
    mov.l @r15+,r5
    mov.l @r15+,r4
    mov.l @r15+,r3
    mov.l @r15+,r2
    mov.l @r15+,r1
    mov.l @r15+,r0
    mov.l r8,@-r15
    mov.l r9,@-r15
    bra _kui_armada_ce_maple_bridge+(0x5b10-0x67a4)
    nop
    .align 2
#if KUI_ARMADA_CE_MAPLE_TRAFFIC
.Lhelper: .long 0xacfce000
#else
.Lhelper: .long 0xacfcf000
#endif
    .global __kui_maple_bridge_end
__kui_maple_bridge_end:

#if !KUI_ARMADA_CE_MAPLE_TRAFFIC
    .section .hook,"ax"
    .align 2
    .global _kui_armada_ce_maple_hook
_kui_armada_ce_maple_hook:
    /* r4=original extension, r5=original SP, r6=original PR, r7=old mode.
     * No operand is dereferenced; stackless and no native calls. */
    stc sr,r3
    mov r3,r0
    or #0xf0,r0
    ldc r0,sr
    mov.l .Llog,r0
    mov.l @(4,r0),r1
    mov #-1,r2
    cmp/eq r2,r1
    bt .Lvisits_ready
    add #1,r1
    mov.l r1,@(4,r0)
.Lvisits_ready:
    mov.l @(8,r0),r1
    mov #32,r2
    cmp/hs r2,r1
    bt .Ldone
    add #1,r1
    mov.l r1,@(8,r0)
    mov.l .Lmagic,r2
    mov.l r2,@r0
    mov #32,r2
    mov.l r2,@(12,r0)
    mov.l r7,@(16,r0)
    mov #1,r2
    cmp/eq r2,r1
    bt .Lfirst
    add #60,r0
    bra .Lstore
    mov #1,r2
.Lfirst:
    add #20,r0
    mov #2,r2
.Lstore:
    mov.l r4,@r0
    mov.l r5,@(4,r0)
    mov.l r6,@(8,r0)
    mov.l r3,@(12,r0)
    mov.l r7,@(16,r0)
    mov.l .Lttb,r1
    mov.l @r1,r1
    mov.l r1,@(20,r0)
    mov.l .Lpteh,r1
    mov.l @r1,r1
    mov.l r1,@(24,r0)
    mov.l .Lccr,r1
    mov.l @r1,r1
    mov.l r1,@(28,r0)
    mov.l .Lprocess,r1
    mov.l @r1,r1
    mov.l r1,@(32,r0)
    mov #0,r1
    mov.l r1,@(36,r0)
    add #40,r0
    dt r2
    bf .Lstore
.Ldone:
    ldc r3,sr
    rts
    nop
    .align 2
.Llog: .long 0xacfcfe00
.Lmagic: .long 0x314b484d
.Lttb: .long 0xff000008
.Lpteh: .long 0xff000000
.Lccr: .long 0xff00001c
.Lprocess: .long 0x8c145890
    .global __kui_maple_hook_binary_end
__kui_maple_hook_binary_end:
    .section .log,"aw",@nobits
    .align 2
    .global _kui_armada_ce_maple_hook_log
_kui_armada_ce_maple_hook_log:
    .space 120

#endif /* !TRAFFIC: separate helper owns .hook and private data. */
