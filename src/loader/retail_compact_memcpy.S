/* SPDX-License-Identifier: GPL-3.0-only */
#define JOIN_(a,b) a##b
#define JOIN(a,b) JOIN_(a,b)
#define C(name) JOIN(__USER_LABEL_PREFIX__,name)

/* Shared copy code for size-constrained retail residents. The current
 * rollout enables it only in the Smash outer resident; CE helpers and native
 * workers retain their existing implementations. Aligned source/destination
 * pairs use integer longwords, with the exact byte fallback and tail for all
 * other cases. R7 is caller-saved and retains the original return pointer.
 * This leaf has no stack, FPU, store-queue or cache-control accesses. */
    .global __kui_retail_compact_word_copy
    .set __kui_retail_compact_word_copy,1
    .section .text.retail_compact_memcpy,"ax"
    .align 2
    .global C(memcpy)
    .type C(memcpy),@function
C(memcpy):
    .cfi_startproc
    mov r4,r7
    mov r4,r0
    or r5,r0
    tst #3,r0
    bf .Lbytes
    mov #4,r1
    cmp/hs r1,r6
    bf .Lbytes
.Lwords:
    mov.l @r5+,r2
    mov.l r2,@r4
    add #4,r4
    add #-4,r6
    cmp/hs r1,r6
    bt .Lwords
.Lbytes:
    tst r6,r6
    bt .Ldone
.Lbyte_loop:
    mov.b @r5+,r2
    mov.b r2,@r4
    add #1,r4
    dt r6
    bf .Lbyte_loop
.Ldone:
    rts
    mov r7,r0
    .cfi_endproc
    .size C(memcpy),.-C(memcpy)
