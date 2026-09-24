/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/image_loader_layout.h"

/* arch_exec has disabled and invalidated I/D caches and has shut KOS down.
 * Keep caches disabled throughout this correctness probe. The first entry is
 * reached through P2; subsequent P1 accesses are coherent with caches off. */
    .section .entry,"ax"
    .align 2
    .global _start
_start:
    mov.l .Lsr,r0
    ldc r0,sr
    mov.l .Lfpscr,r0
    lds r0,fpscr
    mov.l .Lsource,r0
    mov.l .Ltarget,r1
    mov.l .Llength,r2
    shlr2 r2
.Lcopy:
    mov.l @r0+,r3
    mov.l r3,@r1
    add #4,r1
    dt r2
    bf .Lcopy
    mov.l .Lstack,r15
    mov.l .Lmanifest,r4
    mov.l .Ltarget,r0
    jmp @r0
    nop
    .align 2
.Lsr:       .long 0x500000f0
.Lfpscr:    .long 0x00040000
.Lsource:   .long __resident_blob_start
.Ltarget:   .long KUI_IMAGE_RESIDENT_ADDRESS
.Llength:   .long __resident_blob_end-__resident_blob_start
.Lstack:    .long KUI_IMAGE_RESIDENT_STACK
.Lmanifest: .long __loader_manifest

    .section .package_header,"a"
    .ascii KUI_IMAGE_PACKAGE_MAGIC
    .long KUI_IMAGE_PACKAGE_VERSION
    .long KUI_IMAGE_PACKAGE_HEADER_BYTES
    .long KUI_IMAGE_MANIFEST_OFFSET
    .long KUI_IMAGE_MANIFEST_BYTES
    .long KUI_IMAGE_RESIDENT_ADDRESS
    .long __resident_blob_end-__resident_blob_start
    .long KUI_IMAGE_RESIDENT_ADDRESS
    .long KUI_IMAGE_CLIENT_ADDRESS
    .long KUI_IMAGE_CLIENT_STACK
    .long KUI_IMAGE_RESIDENT_STACK
    .long KUI_IMAGE_RESIDENT_BLOB_OFFSET
    .long KUI_IMAGE_CLIENT_MAX_BYTES
    .long 0
    .long 0

    .section .loader_manifest,"a"
    .global __loader_manifest
__loader_manifest:
    .space KUI_IMAGE_MANIFEST_BYTES

    .section .resident_blob,"a"
    .align 2
    .global __resident_blob_start
    .global __resident_blob_end
__resident_blob_start:
    .incbin "build/loader/image_resident.bin"
    .align 2
__resident_blob_end:
