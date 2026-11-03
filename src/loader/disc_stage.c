/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include "retail_display.h"
#include "stage_fast_crc32.h"
#include <string.h>

extern uint8_t __disc_stage_binary_end[] __asm__("__disc_stage_binary_end");
static struct retail_display_state display;
static struct kui_disc_launch_plan plan;
void kui_disc_stage_enter(void) __attribute__((noreturn));
static void stopped(const char *message,uint32_t detail) __attribute__((noreturn));
static void stopped(const char *message,uint32_t detail) {
    retail_display_restore(&display);
    retail_display_line("PHYSICAL DISC BOOT TEST");
    retail_display_line(message);retail_display_hex("DETAIL",detail);
    retail_display_line("PHOTO THEN POWER OFF");
    for(;;) __asm__ volatile("nop");
}
static bool firmware_target(uint32_t pointer) {
    uint32_t area=pointer&UINT32_C(0xe0000000),physical=pointer&UINT32_C(0x1fffffff);
    return !(pointer&1u) && (area==0u || area==UINT32_C(0x80000000) || area==UINT32_C(0xa0000000)) &&
        ((physical>=UINT32_C(0x100) && physical<UINT32_C(0x200000)) ||
         (physical>=UINT32_C(0x0c000100) && physical<UINT32_C(0x0c008000)));
}
void kui_disc_stage_main(void) {
    retail_display_capture(&display);retail_display_restore(&display);
    retail_display_line("PHYSICAL DISC BOOT TEST");
    retail_display_line("ORIGINAL DRIVE / NATIVE GD / NO VMU");
    const uint8_t *payload=(const uint8_t *)(uintptr_t)KUI_DISC_LAUNCH_LOAD_ADDRESS;
    const uint8_t *packet=payload+KUI_DISC_LAUNCH_CONFIG_OFFSET;
    if(!kui_disc_launch_decode(packet,&plan) || plan.flags!=KUI_DISC_LAUNCH_FLAGS)
        stopped("INVALID DISC BOOT PACKET",1u);
    uint32_t actual_stage=(uint32_t)(uintptr_t)__disc_stage_binary_end-KUI_DISC_LAUNCH_STAGE_ADDRESS;
    if(plan.module_bytes!=KUI_DISC_LAUNCH_STAGE_OFFSET+actual_stage ||
       !kui_disc_launch_validate(payload,plan.payload_bytes,&plan))
        stopped("DISC BOOT HASH OR LAYOUT REFUSED",2u);
    /* The real GD firmware entry must survive the destination copies. KOS's
     * public syscall interface names this vector; no custom GD implementation
     * or command map is installed. An unexpected target needs investigation. */
    uint32_t gd=*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xac0000bc);
    if(!firmware_target(gd)) stopped("ORIGINAL GD FIRMWARE VECTOR REFUSED",gd);
    uint32_t low_crc=kui_stage_crc32(0u,(const void *)(uintptr_t)UINT32_C(0x8c000000),0x8000u);
    /* IP ends exactly where the executable begins. Both sources are above
     * those destinations, and all stage code/state/stack live above the whole
     * appended payload. Capture every plan field before moving game bytes. */
    const uint8_t *source_ip=payload+plan.ip_offset,*source_exec=payload+plan.exec_offset;
    uint32_t executable_bytes=plan.exec_bytes;
    memmove((void *)(uintptr_t)KUI_DISC_LAUNCH_IP_ADDRESS,source_ip,KUI_DISC_LAUNCH_IP_BYTES);
    memmove((void *)(uintptr_t)KUI_DISC_LAUNCH_EXEC_ADDRESS,source_exec,executable_bytes);
    if(*(volatile const uint32_t *)(uintptr_t)UINT32_C(0xac0000bc)!=gd ||
       kui_stage_crc32(0u,(const void *)(uintptr_t)UINT32_C(0x8c000000),0x8000u)!=low_crc)
        stopped("LOW FIRMWARE RAM CHANGED",3u);
    /* Existing K-UI native bootstrap handoff clears this one RAM boot flag.
     * Disc bytes and the checked source fingerprints remain unchanged. */
    ((uint8_t *)(uintptr_t)KUI_DISC_LAUNCH_IP_ADDRESS)[0xfcu]&=(uint8_t)~0x20u;
    retail_display_line("IP AND EXECUTABLE HASHES PASSED");
    retail_display_hex("BOOT BYTES",executable_bytes);
    retail_display_hex("ORIGINAL GD VECTOR",gd);
    retail_display_line("ENTERING DISC BOOTSTRAP 2");
    retail_display_pause(30u);
    kui_disc_stage_enter();
}
void kui_disc_stage_returned(void) {
    stopped("DISC BOOTSTRAP RETURNED",4u);
}
