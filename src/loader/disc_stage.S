/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent physical-disc stage. CPU handoff values are the native IP
 * bootstrap interface already exercised by K-UI's own image launcher.
 * No DreamShell implementation or binary is linked into this program. */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .entry,"ax"
    .align 2
    .global _start
_start:
    mov.l .Lcache_setup,r0
    jmp @r0
    nop
.Lcache_setup_p2:
    mov.l .Lccr,r0
    mov.l .Lcache,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l .Lcached,r0
    jmp @r0
    nop
.Lcached_entry:
    mov.l .Lbss,r0
    mov.l .Lbss_end,r1
    mov #0,r2
.Lclear:
    cmp/hs r1,r0
    bt .Lready
    mov.l r2,@r0
    add #4,r0
    bra .Lclear
    nop
.Lready:
    mov.l .Lmain,r0
    jsr @r0
    nop
.Lstopped:
    bra .Lstopped
    nop
    .align 2
.Lcache_setup: .long .Lcache_setup_p2+0x20000000
.Lcached: .long .Lcached_entry
.Lccr: .long 0xff00001c
.Lcache: .long 0x00000909
.Lbss: .long __disc_stage_bss_begin
.Lbss_end: .long __disc_stage_bss_end
.Lmain: .long C(kui_disc_stage_main)

    .text
    .align 2
    .global C(kui_disc_stage_enter)
C(kui_disc_stage_enter):
    mov.l .Luncached,r0
    jmp @r0
    nop
.Lenter_p2:
    mov.l .Lram_begin,r0
    mov.l .Lram_end,r1
.Lpublish:
    ocbp @r0
    add #32,r0
    cmp/hs r1,r0
    bf .Lpublish
    mov.l .Lboot_ccr,r0
    mov.l .Lboot_cache,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l .Lsr,r0
    ldc r0,ssr
    ldc r0,sr
    mov.l .Lgbr,r0
    ldc r0,gbr
    mov.l .Lowner_stack,r15
    ldc r15,vbr
    mov.l .Lfpscr,r0
    /* Shared K-UI instruction-audit witness: this one-time initialization is
     * the only FPU-control instruction permitted in a bootstrap stage. */
    .global __retail_boot_fpscr_init
__retail_boot_fpscr_init:
    lds r0,fpscr
    mov.l .Lreturn,r0
    lds r0,pr
    mov.l .Lbootstrap,r14
    ldc r14,spc
    mov #0,r0
    mov #0,r1
    mov #0,r2
    mov #0,r3
    mov #0,r4
    mov #0,r5
    mov #0,r6
    mov #0,r7
    mov #0,r8
    mov #0,r9
    mov #0,r10
    mov #0,r11
    mov #0,r12
    mov #0,r13
    clrmac
    clrt
    jmp @r14
    mov #0,r14
.Lreturned:
    mov.l .Lhigh_stack,r15
    mov.l .Lreturn_handler,r0
    jmp @r0
    nop
    .align 2
.Luncached: .long .Lenter_p2+0x20000000
.Lram_begin: .long 0x8c000000
.Lram_end: .long 0x8d000000
.Lboot_ccr: .long 0xff00001c
.Lboot_cache: .long 0x00000909
.Lsr: .long 0x700000f0
.Lgbr: .long 0x8c000000
.Lowner_stack: .long 0x8c00f400
.Lfpscr: .long 0x00040001
.Lbootstrap: .long 0xac008300
.Lreturn: .long .Lreturned
.Lhigh_stack: .long 0x8cff0000
.Lreturn_handler: .long C(kui_disc_stage_returned)
