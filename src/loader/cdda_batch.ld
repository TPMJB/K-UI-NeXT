/* SPDX-License-Identifier: GPL-3.0-only */
OUTPUT_FORMAT("elf32-shl")
OUTPUT_ARCH(sh)
ENTRY(_start)
PHDRS { image PT_LOAD FLAGS(5); data PT_LOAD FLAGS(6); stack PT_LOAD FLAGS(6);
 service_stack PT_LOAD FLAGS(6); client PT_LOAD FLAGS(5); client_stack PT_LOAD FLAGS(6); }
SECTIONS {
 . = 0x8c010000;
 __cdda_engine_begin = .;
 .entry : { KEEP(*(.entry)) } :image
 .text : { EXCLUDE_FILE (*cdda_batch_client.o) *(.text .text.*) } :image
 .rodata : { EXCLUDE_FILE (*cdda_batch_client.o) *(.rodata .rodata.*) } :image
 . = ALIGN(32); __cdda_engine_end = .;
 __cdda_state_begin = .;
 .data : { KEEP(*(.cdda_boot_marker)) EXCLUDE_FILE (*cdda_batch_client.o) *(.data .data.* .sdata .sdata.*) } :data
 . = ALIGN(32); __cdda_bss_begin = .;
 .bss (NOLOAD) : { EXCLUDE_FILE (*cdda_batch_client.o) *(.bss .bss.* .sbss .sbss.*) EXCLUDE_FILE (*cdda_batch_client.o) *(COMMON) . = ALIGN(32); } :data
 __cdda_bss_end = .;
 ASSERT(__cdda_bss_end <= 0x8c200000,"CDDA engine overlaps stack")
 . = 0x8c200000;
 .stack (NOLOAD) : { __cdda_stack_bottom = .; . += 0x10000; __cdda_stack_top = .; } :stack
 . = 0x8c220000;
 .service_stack (NOLOAD) : { __cdda_service_stack_bottom = .; . += 0x10000; __cdda_service_stack_top = .; } :service_stack
 . = 0x8c300000; __cdda_client_begin = .;
 .client : { KEEP(*cdda_batch_client.o(.text .text.*)) *cdda_batch_client.o(.rodata .rodata.* .data .data.* .sdata .sdata.*) . = ALIGN(32); } :client
 __cdda_client_end = .; __cdda_client_bss_begin = .;
 .client_bss (NOLOAD) : { *cdda_batch_client.o(.bss .bss.* .sbss .sbss.* COMMON) . = ALIGN(32); } :client
 __cdda_client_bss_end = .;
 ASSERT(__cdda_client_bss_end <= 0x8c310000,"CDDA client overlaps stack")
 . = 0x8c310000;
 .client_stack (NOLOAD) : { __cdda_client_stack_bottom = .; . += 0x10000; __cdda_client_stack_top = .; } :client_stack
 ASSERT(__cdda_client_stack_top == 0x8c320000,"CDDA client stack changed")
 /DISCARD/ : { *(.comment) *(.note*) *(.eh_frame*) }
}
