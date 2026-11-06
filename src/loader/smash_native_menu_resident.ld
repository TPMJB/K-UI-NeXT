/* SPDX-License-Identifier: GPL-3.0-only */
/* Native shared-VMU reader plus a separately hydrated retired-BOOT1 raw gate. */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { resident PT_LOAD FLAGS(7); raw PT_LOAD FLAGS(5); }
SECTIONS
{
    . = 0x8c004000;
    .entry : { KEEP(*(.entry)) } :resident
    .text : { *(.text .text.*) } :resident
    .vmu_sd_lease : {
        __vmu_sd_lease_begin = .;
        KEEP(*(.vmu_sd_lease))
        __vmu_sd_lease_end = .;
    } :resident
    .rodata : { *(.rodata .rodata.*) } :resident
    .data : { *(.data .data.*) } :resident
    . = ALIGN(4);
    __retail_resident_binary_end = .;
    .bss (NOLOAD) : {
        __retail_resident_bss_begin = .;
        *(.bss .bss.* COMMON)
        . = ALIGN(4);
        __retail_resident_bss_end = .;
    } :resident
    __retail_hook_stack_bottom = 0x8c007800;
    __retail_hook_stack = 0x8c007d00;
    ASSERT(_start == 0x8c004000, "resident entry moved")
    ASSERT(__vmu_sd_lease_begin < __vmu_sd_lease_end,
           "SD proof requires its isolated lease gateway")
    ASSERT(__retail_resident_bss_end <= 0x8c007800,
           "retail resident overlaps its guarded service stack")
    .smash_return_raw 0xac008100 : AT(0x8c008100) {
        KEEP(*(.smash_return_raw))
        . = ALIGN(4);
    } :raw
    ASSERT(_kui_smash_menu_return_read_card == 0xac008100,
           "Smash return raw reader fixed P2 entry moved")
    ASSERT(SIZEOF(.smash_return_raw) > 0 && SIZEOF(.smash_return_raw) <= 512,
           "Smash return raw reader exceeds discarded bootstrap slot")
    /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
