/* SPDX-License-Identifier: GPL-3.0-only */
/* Native foreground DMA: cold adoption code retires into SCI receive RAM.
 * The reservation, entry, manifest slots and private stack limits remain
 * unchanged. The region's engine/shared state is always in zeroed BSS. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { resident PT_LOAD FLAGS(7); }
SECTIONS
{
    . = 0x8c004000;
    .entry : { KEEP(*(.entry)) } :resident
    .text : { *(.text .text.*) } :resident
    .rodata : { *(.rodata .rodata.*) . = ALIGN(4); } :resident
    .data : { *(.data .data.*) } :resident
    . = ALIGN(32);
    __native_dma_cold_begin = .;
    _standalone_reader_region = ABSOLUTE(.);
    __native_dma_region_bytes = 0x640;
    .native_dma_cold_init : {
        KEEP(*(.native_dma_cold_init))
        . = ALIGN(4);
    } :resident
    __native_dma_cold_end = .;
    __retail_resident_binary_end = .;
    .bss (NOLOAD) : {
        __retail_resident_bss_begin = .;
        /* Only the cold code is exempt from startup clearing. All actual
         * engine/shared fields start beyond the two 0x220-byte buffers. */
        . += 0x640 - SIZEOF(.native_dma_cold_init);
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __retail_resident_bss_end = .;
    } :resident
    __retail_hook_stack_bottom = DEFINED(__kui_native_gd_vram_dma) ? 0x8c007ea0 : 0x8c007ba0;
    __retail_hook_stack = __retail_hook_stack_bottom + 352;
    ASSERT(_start == 0x8c004000, "native entry moved")
    ASSERT(_standalone_reader_region == __native_dma_cold_begin,
           "private SCI storage must start at the cold receive-buffer span")
    ASSERT(SIZEOF(.native_dma_cold_init) <= 0x440,
           "cold init would overlap live SCI engine/state")
    ASSERT(_kui_retail_resident_init >= __native_dma_cold_begin &&
           _kui_retail_resident_init < __native_dma_cold_end,
           "cold adoption function escaped its receive-buffer lifetime")
    ASSERT(__retail_resident_bss_end <= __retail_hook_stack_bottom,
           "retail resident overlaps its guarded service stack")
    ASSERT(__retail_hook_stack <= 0x8c008000,
           "native VRAM stack overlaps owner IP/bootstrap")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
