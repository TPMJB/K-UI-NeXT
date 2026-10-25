/* SPDX-License-Identifier: GPL-3.0-only */
#define C_SYMBOL(x) _##x
    .section .native_ra_exports,"a"
    .balign 32
    .global C_SYMBOL(kui_native_readahead_aux_exports)
    .type C_SYMBOL(kui_native_readahead_aux_exports),@object
C_SYMBOL(kui_native_readahead_aux_exports):
    .long 0x4b524131,1,64
    .long C_SYMBOL(kui_native_readahead_aux_visit)
    .long C_SYMBOL(kui_native_readahead_aux_init)
    .long C_SYMBOL(kui_native_readahead_aux_context)
    .long __native_ra_image_end,__native_ra_stack_bottom,__native_ra_stack_top,__native_ra_memory_end
    .long 0,0,0,0,0,0
    .size C_SYMBOL(kui_native_readahead_aux_exports),64
    .global __kui_native_readahead_auxiliary
    .set __kui_native_readahead_auxiliary,1

/* Standard C call: preserve SR, PR, SP and every ABI callee-saved register.
 * The low GD or VMU frame remains parked on its original stack. Reject busy
 * or damaged private storage before writing either frame or changing SP. */
    .section .text.native_ra_entry,"ax"
    .balign 4
    .global C_SYMBOL(kui_native_readahead_aux_visit)
    .type C_SYMBOL(kui_native_readahead_aux_visit),@function
C_SYMBOL(kui_native_readahead_aux_visit):
    stc sr,r0
    mov.l .Lmask,r1
    or r0,r1
    ldc r1,sr
    mov.l .Lcontext,r1
    mov.l @r1,r2
    tst r2,r2
    bf .Lreject
    mov.l .Lbottom,r1
    mov.l @r1,r2
    mov.l .Lcanary,r3
    cmp/eq r2,r3
    bf .Lreject
    mov.l .Lhigh,r1
    mov.l @r1,r2
    cmp/eq r2,r3
    bf .Lreject
    mov.l r8,@-r15
    mov.l r9,@-r15
    mov.l r10,@-r15
    mov.l r11,@-r15
    sts.l pr,@-r15
    mov r15,r8
    mov r0,r9
    mov.l .Lcontext,r10
    mov r4,r11
    mov #1,r0
    mov.l r0,@r10
    mov.l .Lstack,r15
    mov.l .Ldispatch,r0
    jsr @r0
    nop
    mov.l .Lbottom,r1
    mov.l @r1,r2
    mov.l .Lcanary,r3
    cmp/eq r2,r3
    bf .Lafter_fault
    mov.l .Lhigh,r1
    mov.l @r1,r2
    cmp/eq r2,r3
    bt .Lrestore
.Lafter_fault:
    mov r10,r1
    add #64,r1
    mov.l @r1,r2
    add #1,r2
    mov.l r2,@r1
    mov #1,r0
    mov #3,r1
    cmp/eq r11,r1
    bf .Lrestore
    mov #0,r0
.Lrestore:
    mov #0,r1
    mov.l r1,@r10
    mov r8,r15
    mov r9,r1
    lds.l @r15+,pr
    mov.l @r15+,r11
    mov.l @r15+,r10
    mov.l @r15+,r9
    mov.l @r15+,r8
    ldc r1,sr
    rts
    nop
.Lreject:
    mov r0,r3
    mov.l .Lcontext,r1
    add #64,r1
    mov.l @r1,r2
    add #1,r2
    mov.l r2,@r1
    mov #0,r0
    mov #1,r1
    cmp/eq r4,r1
    bf .Lreject_restore
    mov #1,r0
.Lreject_restore:
    ldc r3,sr
    rts
    nop
    .balign 4
.Lmask: .long 0x100000f0
.Lcontext: .long C_SYMBOL(kui_native_readahead_aux_context)
.Lbottom: .long __native_ra_stack_bottom
.Lhigh: .long __native_ra_stack_top-16
.Lstack: .long __native_ra_stack_top-32
.Lcanary: .long 0x4b524147
.Ldispatch: .long C_SYMBOL(kui_native_readahead_aux_dispatch)
    .size C_SYMBOL(kui_native_readahead_aux_visit),.-C_SYMBOL(kui_native_readahead_aux_visit)
