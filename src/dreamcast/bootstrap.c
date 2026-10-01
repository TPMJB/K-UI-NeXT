/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/runtime.h"
#include "kui/boot_image.h"
#include "kui/media.h"
#include <arch/exec.h>
#include <kos/thread.h>
#include <stdlib.h>

/* Only called by main before the I/O worker is created. Mount/read/unmount
 * have one owner; KOS shuts down the remaining kernel services at handoff. */
void kui_bootstrap_load(kui_cancel_fn cancelled,bool recovery_only) {
    struct kui_runtime_image image = {0};
    enum kui_runtime_result result = KUI_RUNTIME_IO;
    unsigned chosen_transport=KUI_STORAGE_AUTO;
    /* A present but empty SCIF card must not hide a runtime on SCI/IDE.
     * Only validated images choose a source; probing never writes the card. */
    for(unsigned candidate=KUI_STORAGE_SCIF;candidate<KUI_STORAGE_AUTO;candidate++) {
        if(cancelled()) {result=KUI_RUNTIME_CANCELLED;break;}
        result=KUI_RUNTIME_IO;
        kui_sd_set_params(candidate,true);
        if(!kui_sd_connect()) continue;
        struct kui_media_ops raw;
        if(kui_sd_raw_read_ops(&raw))
            result=kui_boot_image_read(&raw,candidate,recovery_only,&image,kui_log,cancelled);
        kui_sd_disconnect();
        /* The portable reader validates checksums, chooses normal/recovery
         * images, and patches the transport before returning success. */
        if(result==KUI_RUNTIME_OK) {chosen_transport=candidate;break;}
        kui_runtime_free(&image);
        if(result==KUI_RUNTIME_CANCELLED || result==KUI_RUNTIME_MEMORY) break;
    }
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
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
    kui_log("Runtime validated. Starting %s build %s",kui_storage_name(chosen_transport),image.info.build);
    if(cancelled()) { kui_runtime_free(&image); return; }
    thd_sleep(250);
    if(cancelled()) { kui_runtime_free(&image); return; }
    arch_exec(image.data, image.info.payload_bytes);
}
