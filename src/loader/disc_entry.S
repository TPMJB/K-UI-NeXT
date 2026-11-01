/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_STAGE_BIN
#define KUI_DISC_STAGE_BIN "build/disc-native/stage.bin"
#endif
    .section .entry,"ax"
    .align 2
    .global _start
_start:
    mov.l .Lmasked,r0
    ldc r0,sr
    mov.l .Lsource,r0
    mov.l .Ldestination,r1
    mov.l .Lwords,r2
.Lcopy:
    mov.l @r0+,r3
    mov.l r3,@r1
    add #4,r1
    dt r2
    bf .Lcopy
    mov.l .Lstack,r15
    mov.l .Ldestination,r0
    jmp @r0
    nop
    .align 2
.Lmasked: .long 0x500000f0
.Lsource: .long __disc_stage_blob_start
.Ldestination: .long 0x8ce00000
.Lwords: .long (__disc_stage_blob_end-__disc_stage_blob_start)/4
.Lstack: .long 0x8cff0000

    .section .package_header,"a"
    .ascii "KUIDISC1"
    .long 1,64,0x1000,512,0x8ce00000
    .long __disc_stage_blob_end-__disc_stage_blob_start
    .long 0x8ce00000,0x8c010000,0x400000,0x8cff0000,0x2000
#if KUI_DISC_VMU
    .long 3,0,0
#else
    .long 1,0,0
#endif
    .section .disc_config,"a"
    .space 512
    .section .stage_blob,"a"
    .align 2
    .global __disc_stage_blob_start,__disc_stage_blob_end
__disc_stage_blob_start:
    .incbin KUI_DISC_STAGE_BIN
    .align 2
__disc_stage_blob_end:
