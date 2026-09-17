/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/runtime.h"
#include <arch/exec.h>
#include <kos/thread.h>
#include <stdlib.h>

/* Only called by main before the I/O worker is created. Mount/read/unmount
 * have one owner; KOS shuts down the remaining kernel services at handoff. */
void kui_bootstrap_load(kui_cancel_fn cancelled) {
    FATFS fs;
    struct kui_runtime_image image = {0};
    if(cancelled() || !kui_sd_connect()) return;
    enum kui_runtime_result result = KUI_RUNTIME_IO;
    if(kui_mount(&fs, kui_log))
        result = kui_runtime_read(KUI_RUNTIME_PATH, &image, kui_log, cancelled);
    if(f_mount(NULL, "0:", 0) != FR_OK && result == KUI_RUNTIME_OK)
        result = KUI_RUNTIME_IO;
    kui_sd_disconnect();
    if(result == KUI_RUNTIME_OK && cancelled()) result = KUI_RUNTIME_CANCELLED;
    if(result != KUI_RUNTIME_OK) {
        kui_log("SD runtime not started: %s", kui_runtime_result_name(result));
        kui_runtime_free(&image);
        return;
    }
    /* arch_exec copies forward from a staged, word-aligned buffer. Require
     * its physical address to be above the destination and below the main
     * stack, and keep the destination away from the on-stack trampoline. */
    uintptr_t stack;
    __asm__ __volatile__("mov r15,%0" : "=r"(stack));
    uintptr_t source = (uintptr_t)image.data;
    if((source & 3) || source < KUI_RUNTIME_ADDRESS ||
       source >= 0x8d000000u || image.info.payload_bytes > 0x8d000000u - source ||
       stack < KUI_RUNTIME_ADDRESS + KUI_RUNTIME_MAX_BYTES + 65536u ||
       source + image.info.payload_bytes > stack - 65536u) {
        kui_log("SD runtime not started: unsafe staging/stack addresses");
        kui_runtime_free(&image);
        return;
    }
    kui_log("Runtime validated. Starting SD build %s", image.info.build);
    if(cancelled()) { kui_runtime_free(&image); return; }
    thd_sleep(250);
    if(cancelled()) { kui_runtime_free(&image); return; }
    arch_exec(image.data, image.info.payload_bytes);
}
