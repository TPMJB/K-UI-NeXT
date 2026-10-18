/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .section .entry,"ax"
    .align 2
    .global _start
_start:
    /* This test owns the first MiB of guest RAM. The original bootstrap2
     * enters through the normal K-UI retail relay before changing stacks. */
    mov.l .Lstack,r15
    mov.l .Lbss,r0
    mov.l .Lend,r1
    mov #0,r2
.Lzero:
    cmp/hs r1,r0
    bt .Lready
    mov.l r2,@r0
    add #4,r0
    bra .Lzero
    nop
.Lready:
    mov.l .Lmain,r0
    jsr @r0
    nop
.Lhalt:
    bra .Lhalt
    nop
    .align 2
.Lstack: .long 0x8c100000
.Lbss: .long __bss_start
.Lend: .long __bss_end
.Lmain: .long C(kui_chd_console_main)

    .text
    .align 2
    .global C(kui_chd_console_gd_call)
C(kui_chd_console_gd_call):
    /* C arguments -> documented Dreamcast GD BIOS vector ABI. */
    sts.l pr,@-r15
    mov r4,r7
    mov r5,r4
    mov r6,r5
    mov #0,r6
    mov.l .Lvector,r0
    mov.l @r0,r0
    jsr @r0
    nop
    lds.l @r15+,pr
    rts
    nop
    .align 2
.Lvector: .long 0x8c0000bc

    /* Entirely original fixture bootstrap, placed in the generated IP.BIN
     * at offset 0x6000. K-UI has already loaded 1ST_READ.BIN. */
    .section .bootstrap2,"ax"
    .align 2
    mov.l .Lentry,r0
    jmp @r0
    nop
    nop
.Lentry: .long 0xac010000
