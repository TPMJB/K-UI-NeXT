/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .entry,"ax"
    .align 2
    .global _start
_start:
    /* arch_exec removed KOS and invalidated/disabled I/D caches. Use P2
     * until memory initialization and the CCR handoff are complete. */
    mov.l .Lp2,r0
    jmp @r0
    nop
.Luncached:
    mov.l .Lsr,r0
    ldc r0,sr
    mov.l .Lfpscr,r0
    lds r0,fpscr
    mov.l .Lstack_bottom,r0
    mov.l .Lstack_top,r1
    mov.l .Lwatermark,r2
.Lfill:
    mov.l r2,@r0
    add #4,r0
    cmp/hs r1,r0
    bf .Lfill
    mov.l .Lstack_bottom,r0
    mov.l .Lcanary,r2
    mov #16,r3
.Lguard:
    mov.l r2,@r0
    add #4,r0
    dt r3
    bf .Lguard
    mov.l .Lbss,r0
    mov.l .Lbss_end,r1
    mov #0,r2
.Lzero:
    cmp/hs r1,r0
    bt .Lcache
    mov.l r2,@r0
    add #4,r0
    bra .Lzero
    nop
.Lcache:
    /* Writing CCR from P2 avoids cached instruction-fetch hazards. 0x909
     * enables normal I/D caches and invalidates both; wait eight instructions
     * before the first cached fetch. SCI DMA handles its own cache lines. */
    mov.l .Lccr,r0
    mov.l .Lcache_mode,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l .Lstack_top,r15
    mov.l .Lmain,r0
    jmp @r0
    nop
    .align 2
.Lp2: .long .Luncached+0x20000000
.Lsr: .long 0x500000f0
.Lfpscr: .long 0x00040000
.Lstack_bottom: .long __cdda_stack_bottom
.Lstack_top: .long __cdda_stack_top
.Lwatermark: .long 0xa5a5a5a5
.Lcanary: .long 0x43444441
.Lbss: .long __cdda_bss_begin
.Lbss_end: .long __cdda_bss_end
.Lccr: .long 0xff00001c
.Lcache_mode: .long 0x00000909
.Lmain: .long C(cdda_main)
