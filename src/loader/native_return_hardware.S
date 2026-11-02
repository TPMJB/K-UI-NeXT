/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_I(a,b) a##b
#define JOIN(a,b) JOIN_I(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)
    .text
    .align 2
    .global C(kui_native_return_cache_off)
    .type C(kui_native_return_cache_off),@function
C(kui_native_return_cache_off):
    mova .Lcache_p2,r0
    mov.l .Lp2_alias,r1
    or r1,r0
    jmp @r0
    nop
    .align 2
.Lcache_p2:
    /* Publish every dirty line, including the live P1 stack, before
     * discarding cached game aliases of the incoming P2 runtime payload. */
    mov.l .Ltags,r0
    mov.l .Ltag_count,r1
    mov #-3,r3
.Lwriteback:
    mov.l @r0,r2
    and r3,r2
    mov.l r2,@r0
    add #32,r0
    dt r1
    bf .Lwriteback
    mov.l .Lccr,r0
    mov.l .Lcache_disable,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rts
    nop
    .align 2
.Lp2_alias: .long 0x20000000
.Ltags: .long 0xf4000008
.Ltag_count: .long 512
.Lccr: .long 0xff00001c
.Lcache_disable: .long 0x00000808
    .size C(kui_native_return_cache_off),.-C(kui_native_return_cache_off)

    .align 2
    .global C(kui_native_return_enter)
    .type C(kui_native_return_enter),@function
C(kui_native_return_enter):
    mova .Lenter_p2,r0
    mov.l .Lenter_alias,r1
    or r1,r0
    jmp @r0
    nop
    .align 2
.Lenter_p2:
    mov.l .Linitial_sr,r0
    ldc r0,sr
    mov.l .Lmmucr,r0
    mov #4,r1
    mov.l r1,@r0
    mov.l .Lentry_ccr,r0
    mov.l .Lentry_cache,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    mov.l .Linitial_vbr,r0
    ldc r0,vbr
    mov.l .Linitial_sp,r15
    mov #0,r0
    lds r0,pr
    ldc r0,gbr
    mov.l .Lruntime_entry,r0
    jmp @r0
    nop
    .align 2
.Lenter_alias: .long 0x20000000
.Linitial_sr: .long 0x500000f0
.Lmmucr: .long 0xff000010
.Lentry_ccr: .long 0xff00001c
.Lentry_cache: .long 0x00000808
.Linitial_vbr: .long 0x8c000000
.Linitial_sp: .long 0x8cffff00
.Lruntime_entry: .long 0xac010000
    .size C(kui_native_return_enter),.-C(kui_native_return_enter)

    /* Firmware entry restoration runs after the storage loan has closed.
     * CCR writes execute from P2 and preserve the game's cache mode. */
    .align 2
    .global C(kui_native_session_invalidate)
    .type C(kui_native_session_invalidate),@function
C(kui_native_session_invalidate):
    mova .Lsession_cache_p2,r0
    mov.l .Lsession_alias,r1
    or r1,r0
    jmp @r0
    nop
    .align 2
.Lsession_cache_p2:
    mov.l .Lsession_ccr,r0
    mov.l @r0,r1
    mov.l .Lsession_ici,r2
    or r2,r1
    mov.l r1,@r0
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    rts
    nop
    .align 2
.Lsession_alias: .long 0x20000000
.Lsession_ccr: .long 0xff00001c
.Lsession_ici: .long 0x00000800
    .size C(kui_native_session_invalidate),.-C(kui_native_session_invalidate)
