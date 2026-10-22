/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/runtime.h"
#include "kui/boot_image.h"
#include "kui/media.h"
#include <arch/exec.h>
#include <kos/thread.h>
#include <kos/timer.h>
#include <inttypes.h>
#include <stdlib.h>

/* Only called by the boot menu with exclusive storage ownership and no active
 * I/O worker. Failed/cancelled calls release their state for insertion/retry;
 * KOS shuts down the remaining kernel services at successful handoff. */
static enum kui_runtime_result bootstrap_load(unsigned transport_filter,
    enum kui_boot_mode mode,kui_cancel_fn cancelled,bool execute) {
    struct kui_runtime_image image = {0};
    enum kui_runtime_result result = KUI_RUNTIME_IO;
    unsigned chosen_transport=KUI_STORAGE_AUTO;
    if(!cancelled || transport_filter>KUI_STORAGE_AUTO || !kui_boot_image_mode_path(mode,0))
        return KUI_RUNTIME_IO;
    /* A present but empty SCIF card must not hide a runtime on SCI/IDE.
     * Only validated images choose a source; probing never writes the card. */
    for(unsigned attempt=0;;attempt++) {
        unsigned candidate=kui_boot_transport_at(transport_filter,attempt);
        if(candidate==KUI_STORAGE_AUTO) break;
        if(cancelled()) {result=KUI_RUNTIME_CANCELLED;break;}
        result=KUI_RUNTIME_IO;
        kui_sd_set_params(candidate,true);
        uint64_t init_start=timer_us_gettime64();
        bool connected=kui_sd_connect();
        uint64_t init_us=timer_us_gettime64()-init_start;
        if(!connected) {
            kui_sd_disconnect();
            kui_log("%s init: %" PRIu64 " ms; unavailable",kui_storage_name(candidate),init_us/1000u);
            continue;
        }
        struct kui_media_ops raw;
        uint64_t load_start=timer_us_gettime64();
        if(kui_sd_raw_read_ops(&raw))
            result=kui_boot_image_read_mode(&raw,candidate,mode,&image,kui_log,cancelled);
        uint64_t load_us=timer_us_gettime64()-load_start;
        kui_sd_disconnect();
        kui_log("%s: init %" PRIu64 " ms; load/check %" PRIu64 " ms",
            kui_storage_name(candidate),init_us/1000u,load_us/1000u);
        if(result==KUI_RUNTIME_OK) {
            /* File/partition reads, CRCs, marker validation and any UI work
             * called by that path are included. This is not raw bus speed. */
            uint64_t rate10=load_us ? (uint64_t)image.info.payload_bytes*UINT64_C(10000000)/(load_us*1024u) : 0;
            kui_log("%" PRIu32 " bytes; %" PRIu64 ".%" PRIu64 " KiB/s including checks/UI",
                image.info.payload_bytes,rate10/10u,rate10%10u);
        }
        kui_log("Image load result: %s",kui_runtime_result_name(result));
        /* The portable reader validates checksums, chooses normal/recovery
         * images, and patches the transport before returning success. */
        if(result==KUI_RUNTIME_OK) {chosen_transport=candidate;break;}
        kui_runtime_free(&image);
        if(result==KUI_RUNTIME_CANCELLED || result==KUI_RUNTIME_MEMORY) break;
    }
    kui_media_set(NULL);
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    /* A hardware connection can fail while B is pressed, including on the
     * last explicitly selected transport. Preserve cancellation in that case. */
    if(cancelled()) result = KUI_RUNTIME_CANCELLED;
    if(result != KUI_RUNTIME_OK) {
        kui_log("Card image not started: %s", kui_runtime_result_name(result));
        kui_runtime_free(&image);
        return result;
    }
    if(!execute) {
        kui_log("Measured %s build %s; no image started",kui_storage_name(chosen_transport),image.info.build);
        kui_runtime_free(&image);
        return KUI_RUNTIME_OK;
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
        kui_log("Card image not started: unsafe staging/stack addresses");
        kui_runtime_free(&image);
        return KUI_RUNTIME_ADDRESS_ERROR;
    }
    kui_log("Image validated. Starting %s build %s",kui_storage_name(chosen_transport),image.info.build);
    if(cancelled()) { kui_runtime_free(&image); return KUI_RUNTIME_CANCELLED; }
    thd_sleep(250);
    if(cancelled()) { kui_runtime_free(&image); return KUI_RUNTIME_CANCELLED; }
    arch_exec(image.data, image.info.payload_bytes);
}
enum kui_runtime_result kui_bootstrap_start(unsigned transport_filter,
    enum kui_boot_mode mode,kui_cancel_fn cancelled) {
    return bootstrap_load(transport_filter,mode,cancelled,true);
}
enum kui_runtime_result kui_bootstrap_measure(unsigned transport_filter,
    kui_cancel_fn cancelled) {
    return bootstrap_load(transport_filter,KUI_BOOT_MODE_NORMAL,cancelled,false);
}
void kui_bootstrap_load(kui_cancel_fn cancelled,bool recovery_only) {
    (void)kui_bootstrap_start(KUI_STORAGE_AUTO,
        recovery_only ? KUI_BOOT_MODE_RECOVERY : KUI_BOOT_MODE_NORMAL,cancelled);
}
