/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/retail_loader_layout.h"
    .section .entry,"ax"
    .align 2
    .global _start
_start:
    mov.l .Lsr,r0
    ldc r0,sr
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
    mov.l .Lmap,r4
    mov.l .Ltarget,r0
    jmp @r0
    nop
    .align 2
.Lsr: .long 0x500000f0
.Lsource: .long __retail_stage_blob_start
.Ltarget: .long KUI_RETAIL_STAGE_ADDRESS
.Llength: .long __retail_stage_blob_end-__retail_stage_blob_start
.Lstack: .long KUI_RETAIL_STAGE_STACK
.Lmap: .long __retail_map
    .section .package_header,"a"
    .ascii KUI_RETAIL_PACKAGE_MAGIC
    .long KUI_RETAIL_PACKAGE_VERSION
    .long KUI_RETAIL_HEADER_BYTES
    .long KUI_RETAIL_MAP_OFFSET
    .long KUI_RETAIL_MAP_BYTES
    .long KUI_RETAIL_STAGE_ADDRESS
    .long __retail_stage_blob_end-__retail_stage_blob_start
    .long KUI_RETAIL_STAGE_ADDRESS
    .long KUI_RETAIL_EXEC_ADDRESS
    .long KUI_RETAIL_EXEC_MAX_BYTES
    .long KUI_RETAIL_STAGE_STACK
    .long KUI_RETAIL_STAGE_BLOB_OFFSET
    .long KUI_RETAIL_RESIDENT_ADDRESS
    .long KUI_RETAIL_RESIDENT_LIMIT
    .long 0
    .section .retail_map,"a"
    .global __retail_map
__retail_map:
    .space KUI_RETAIL_MAP_BYTES
    .section .stage_blob,"a"
    .align 2
    .global __retail_stage_blob_start,__retail_stage_blob_end
__retail_stage_blob_start:
    .incbin "build/retail/stage.bin"
    .align 2
__retail_stage_blob_end:
