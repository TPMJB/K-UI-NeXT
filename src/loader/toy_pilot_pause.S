/* SPDX-License-Identifier: GPL-3.0-only */
#define C_SYMBOL(name) _ ## name
    .text
    .align 2
    .global C_SYMBOL(kui_toy_pilot_pause_hook)
    .type C_SYMBOL(kui_toy_pilot_pause_hook),@function
C_SYMBOL(kui_toy_pilot_pause_hook):
    /* The native zero-argument pause attempt retains its original caller
     * stack and ABI. Only the authored follow-up uses the shared private
     * worker stack. In particular, a skipped/reentrant bridge must not turn
     * the native -13 retry result into apparent success. */
    sts.l pr,@-r15
    mov.l .Loriginal_pause,r0
    jsr @r0
    nop
    mov.l r0,@-r15
    mov r0,r4
    mov.l .Lafter_pause,r0
    mov.l .Lworker_bridge,r1
    jsr @r1
    nop
    mov.l @r15+,r0
    lds.l @r15+,pr
    rts
    nop
    .align 2
.Loriginal_pause: .long 0x8c0b3200
.Lafter_pause: .long C_SYMBOL(kui_toy_pilot_pause_after)
.Lworker_bridge: .long C_SYMBOL(kui_toy_pilot_worker_bridge)
    .size C_SYMBOL(kui_toy_pilot_pause_hook),.-C_SYMBOL(kui_toy_pilot_pause_hook)

    .align 2
    .global C_SYMBOL(kui_toy_pilot_native_pump)
    .type C_SYMBOL(kui_toy_pilot_native_pump),@function
C_SYMBOL(kui_toy_pilot_native_pump):
    /* C validated this bridge anchor and the full 192-byte native call
     * reservation under IMASK15. Keep private restoration state on the
     * worker stack; only native SDK scratch uses the suspended game stack.
     * The saved caller SP is below the bridge's preserved integer frame. */
    sts.l pr,@-r15
    mov.l r8,@-r15
    mov r15,r8
    mov.l .Lbridge_anchor,r0
    mov.l @r0,r15
    mov.l .Lnative_guard_pump,r0
    jsr @r0
    nop
    mov r8,r15
    mov.l @r15+,r8
    lds.l @r15+,pr
    rts
    nop
    .align 2
.Lbridge_anchor: .long __toy_pilot_stack_top-16
.Lnative_guard_pump: .long 0x8c0b1704
    .size C_SYMBOL(kui_toy_pilot_native_pump),.-C_SYMBOL(kui_toy_pilot_native_pump)
