/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_launcher.h"
#include "kui/disc_launch.h"
#include "kui/disc_vmu_card.h"
#include "kui/disc_vmu_probe.h"
#include "kui/disc_region.h"
#include "kui/vmu_emulation_settings.h"
#include "kui/vmu_native_cache_seed.h"
#include "kui/vmu_native_resolution_cache.h"
#include "kui/vmu_native_program.h"
#include "platform.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../loader/stage_fast_crc32.h"

static bool preference(bool *enabled,unsigned *slot,kui_log_fn log,kui_cancel_fn cancel) {
    if(cancel && cancel()) return false;
    if(!kui_sd_connect()) return false;
    FATFS fs;
    bool mounted=kui_mount(&fs,log);
    bool loaded=mounted && kui_vmu_emulation_settings_load_full(enabled,slot,log);
    bool released=f_mount(NULL,"0:",0)==FR_OK;
    kui_sd_disconnect();
    return loaded && *slot<KUI_GAME_VMU_SLOT_COUNT && released && !(cancel && cancel());
}
static bool failure(struct kui_app_status *out,const char *message,kui_cancel_fn cancel,
    kui_app_progress_fn progress) {
    *out=(struct kui_app_status){.complete=true};
    out->stopped=cancel && cancel();out->errors=out->stopped?0u:1u;
    snprintf(out->message,sizeof(out->message),"%s",message);
    if(progress) progress(out);
    return false;
}
void kui_disc_launcher_vmu_info(struct kui_vmu_game_info *info,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!info || !out || !log || !cancel) return;
    memset(info,0,sizeof(*info));*out=(struct kui_app_status){0};
    bool enabled=false;
    unsigned slot=KUI_GAME_VMU_SLOT_A2;
    if(!preference(&enabled,&slot,log,cancel)) {
        snprintf(info->notice,sizeof(info->notice),"Cannot read Virtual VMU setting; check Diagnostics.");
        (void)failure(out,info->notice,cancel,progress);return;
    }
    /* Off does not identify a product, inspect a card catalogue, prepare a
     * profile or read any card/journal. The later boot uses the baseline. */
    if(!enabled) {
        info->known=true;
        snprintf(info->label,sizeof(info->label),"Physical VMUs");
        snprintf(info->notice,sizeof(info->notice),"Virtual VMU is Off. Direct boot uses physical VMUs.");
    } else {
        /* Preview needs the checked release ID, not whole executable hashes.
         * Identify afresh for every preview; actual launch still stages and
         * verifies the complete physical-disc file before VMU preflight. */
        struct kui_game_metadata metadata={0};uint8_t signature[32];
        if(!kui_disc_launcher_metadata(&metadata,signature,out,log,cancel,progress)) {
            snprintf(info->notice,sizeof(info->notice),"%.127s",out->message);return;
        }
        const char *problem=NULL;char key[KUI_VMU_GAME_CARD_KEY_BYTES];
        if(cancel()) problem="Disc card check stopped.";
        else if(!metadata.ip_valid || !metadata.boot_valid)
            problem="Physical-disc boot metadata is incomplete.";
        else if(metadata.windows_ce)
            problem="Windows CE needs its separate disc launch path.";
        else if(!metadata.native_gd || metadata.native_cd)
            problem="This launch path requires a native GD-ROM disc.";
        else if(metadata.boot_bytes<128u || metadata.boot_bytes>KUI_DISC_LAUNCH_EXEC_MAX_BYTES)
            problem="Physical disc boot data exceeds launch limits.";
        else if(!memchr(metadata.product,0,sizeof(metadata.product)) ||
                !kui_vmu_game_card_key(metadata.product,key))
            problem="Inserted disc has no usable release ID.";
        if(problem) {
            snprintf(info->notice,sizeof(info->notice),"%s",problem);
            (void)failure(out,problem,cancel,progress);return;
        }
        kui_vmu_game_info_run(metadata.product,true,info,log,cancel);
        if(cancel()) {(void)failure(out,"Disc card check stopped.",cancel,progress);return;}
        if(!info->known) {(void)failure(out,info->notice,cancel,progress);return;}
    }
    out->complete=out->passed=true;out->errors=0u;
    snprintf(out->message,sizeof(out->message),"%s",info->notice[0]?info->notice:info->label);
    if(progress) progress(out);
}
static const char *probe_refusal(uint32_t stage) {
    switch(stage) {
    case KUI_DISC_VMU_PROBE_MAPLE_DRIVER:
        return "Virtual VMU unsupported: Maple driver. R: Save Log; disable VMU in Options.";
    case KUI_DISC_VMU_PROBE_MAPLE_LAYOUT:
        return "Virtual VMU unsupported: Maple buffer layout. R: Save Log; disable VMU in Options.";
    case KUI_DISC_VMU_PROBE_HEAP_DISCOVERY:
        return "Virtual VMU unsupported: allocator. R: Save Log; disable VMU in Options.";
    case KUI_DISC_VMU_PROBE_HEAP_SLOT:
        return "Virtual VMU unsupported: allocator initializer. R: Save Log; disable VMU in Options.";
    case KUI_DISC_VMU_PROBE_HEAP_CALL:
        return "Virtual VMU unsupported: allocator startup call. R: Save Log; disable VMU in Options.";
    case KUI_DISC_VMU_PROBE_STARTUP_CLEAR:
        return "Virtual VMU unsupported: startup RAM clear. R: Save Log; disable VMU in Options.";
    default:
        return "Virtual VMU unsupported: executable format. R: Save Log; disable VMU in Options.";
    }
}
/* This fresh per-launch proof is never written to a cache file. The producer
 * runs while K-UI still owns the complete immutable disc executable and its
 * cached runtime. Only its value record is copied into the trusted module;
 * no launcher address or callback survives the handoff. */
struct disc_vmu_preflight {
    const struct kui_vmu_game_selection *selection;
    uint8_t wire[KUI_VMU_NATIVE_RESOLUTION_CACHE_BYTES];
    bool ready;
};
static bool selected_card(const char *product,const void *executable,uint32_t executable_bytes,
    uint8_t profile[4096],char refusal[KUI_DISC_LAUNCHER_REFUSAL_BYTES],void *context,
    kui_log_fn log,kui_cancel_fn cancel) {
    struct disc_vmu_preflight *preflight=context;
    const struct kui_vmu_game_selection *selection=preflight->selection;
    preflight->ready=false;
    memset(profile,0,4096u);
    if(selection && selection->product[0]) {
        char key[KUI_VMU_GAME_CARD_KEY_BYTES];
        if(!memchr(selection->product,0,sizeof(selection->product)) ||
           !kui_vmu_game_card_key(product,key) || memcmp(key,selection->product,sizeof(key))) {
            log("Physical disc virtual VMU: disc product changed after card review");
            return false;
        }
    }
    if(cancel()) return false;
    /* Produce the complete immutable proof once, including uniqueness and
     * guarded exit windows. The cold stage independently hashes the exact
     * post-bootstrap program, then rebinds live ownership before any hook. */
    struct kui_disc_vmu_probe probe={0};
    struct kui_vmu_native_resolution resolution;size_t program_bytes;
    bool compatible=kui_vmu_native_program_extent(executable,executable_bytes,
        KUI_DISC_LAUNCH_EXEC_ADDRESS,&program_bytes) &&
        kui_vmu_native_resolution_cache_build(executable,program_bytes,KUI_DISC_LAUNCH_EXEC_ADDRESS,
            preflight->wire,sizeof(preflight->wire),&resolution) &&
        kui_vmu_native_heap_metadata_valid(&resolution.heap) && resolution.heap.initializer_slot;
    if(compatible) {
        probe.version=KUI_DISC_VMU_PROBE_VERSION;probe.bytes=sizeof(probe);
        probe.stage=KUI_DISC_VMU_PROBE_READY;probe.file_bytes=executable_bytes;
        probe.program_bytes=(uint32_t)program_bytes;probe.file_crc32=kui_stage_crc32(0u,executable,executable_bytes);
        probe.driver_result=KUI_VMU_NATIVE_RESOLVED;probe.heap_result=KUI_VMU_NATIVE_HEAP_FOUND;
        probe.driver=resolution.driver;probe.layout=resolution.layout;probe.heap=resolution.heap;probe.call=resolution.call;
        struct kui_vmu_native_heap_preclear clear;
        if(kui_vmu_native_heap_preclear_plan(executable,program_bytes,resolution.driver.image_sha256,
           &resolution.heap,&resolution.call,&clear)!=0) {
            probe.stage=KUI_DISC_VMU_PROBE_STARTUP_CLEAR;compatible=false;
        } else preflight->ready=true;
    } else {
        /* Re-run partial discovery only on refusal, to retain the useful
         * Maple/layout/allocator diagnosis without slowing successful boots. */
        (void)kui_disc_vmu_probe_run(executable,executable_bytes,KUI_DISC_LAUNCH_EXEC_ADDRESS,&probe);
    }
    log("Physical disc VMU preflight: product %s; exact file CRC32 %08" PRIx32
        "; full bytes %" PRIu32 "; program bytes %" PRIu32,
        product,probe.file_crc32,probe.file_bytes,probe.program_bytes);
    log("Physical disc VMU preflight: %s; driver result %" PRId32 "; heap result %" PRId32,
        kui_disc_vmu_probe_stage_text(probe.stage),probe.driver_result,probe.heap_result);
    log("Physical disc VMU preflight: proven Maple submit %08" PRIx32 "; initializer %08" PRIx32
        "; layout initializer %08" PRIx32 "; TX %08" PRIx32 "; RX %08" PRIx32,
        probe.driver.submit,probe.driver.initializer,probe.layout.initializer,
        probe.layout.tx_base,probe.layout.rx_base);
    log("Physical disc VMU preflight: proven heap initializer %08" PRIx32 "; slot %08" PRIx32
        "; allocate %08" PRIx32 "; release %08" PRIx32,
        probe.heap.initializer,probe.heap.initializer_slot,probe.heap.allocate,probe.heap.release);
    log("Physical disc VMU preflight: proven heap load %08" PRIx32 "; call %08" PRIx32
        "; return %08" PRIx32,probe.call.load,probe.call.call,probe.call.return_pr);
    if(!compatible) {
        snprintf(refusal,KUI_DISC_LAUNCHER_REFUSAL_BYTES,"%s",probe.stage==KUI_DISC_VMU_PROBE_READY?
            "Virtual VMU unsupported: complete lifetime proof. R: Save Log; disable VMU in Options.":probe_refusal(probe.stage));
        return false;
    }
    if(cancel() || !kui_sd_connect()) return false;
    FATFS fs;bool mounted=kui_mount(&fs,log);
    struct kui_disc_vmu_card *card=calloc(1u,sizeof(*card));
    int result=KUI_DISC_VMU_CARD_FAILED;
    if(mounted && card) result=kui_disc_vmu_card_prepare(product,true,&fs,selection,card,log,cancel);
    bool released=f_mount(NULL,"0:",0)==FR_OK;
    kui_sd_disconnect();
    bool ok=result==KUI_DISC_VMU_CARD_READY && released && !cancel();
    if(ok) memcpy(profile,card->profile_wire,4096u);
    else if(card && card->choice.problem[0]) log("Physical disc virtual VMU: %s",card->choice.problem);
    if(card) {memset(card,0,sizeof(*card));free(card);}
    return ok;
}
static bool disc_vmu_seed_prepare(const struct disc_vmu_preflight *preflight,
    struct kui_runtime_image *image,struct kui_app_status *out,kui_log_fn log,
    kui_cancel_fn cancel,kui_app_progress_fn progress) {
    struct kui_disc_launch_plan plan;
    if(cancel() || !preflight->ready || !image->data ||
       image->info.payload_bytes<KUI_DISC_LAUNCH_CONFIG_OFFSET+KUI_DISC_LAUNCH_CONFIG_BYTES ||
       !kui_disc_launch_decode((const uint8_t *)image->data+KUI_DISC_LAUNCH_CONFIG_OFFSET,&plan) ||
       plan.flags!=KUI_DISC_LAUNCH_VMU_FLAGS || plan.payload_bytes!=image->info.payload_bytes ||
       plan.module_bytes>image->info.payload_bytes) {
        kui_runtime_free(image);
        return failure(out,"Disc VMU mapping handoff stopped or invalid; no game launched.",cancel,progress);
    }
    size_t at=kui_vmu_native_cache_seed_find(image->data,plan.module_bytes);
    if(at!=SIZE_MAX && kui_vmu_native_cache_seed_fill((uint8_t *)image->data+at,
        KUI_VMU_NATIVE_CACHE_SEED_BYTES,preflight->wire,sizeof(preflight->wire))) {
        image->info.crc32=kui_stage_crc32(0u,image->data,image->info.payload_bytes);
        log("Physical disc VMU: complete fresh preflight proof seeded; post-bootstrap image SHA and live ownership remain checked");
    } else {
        /* Older modules, a damaged slot or multiple markers get the existing
         * full cold discovery. A seed miss never skips structural checks. */
        log("Physical disc VMU: no unique valid mapping seed; retaining complete cold discovery");
    }
    return true;
}
bool kui_disc_launcher_prepare_vmu(const struct kui_vmu_game_selection *selection,
    struct kui_runtime_image *image,struct kui_app_status *out,kui_log_fn log,
    kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!image || !out || !log || !cancel) return false;
    *image=(struct kui_runtime_image){0};
    struct kui_disc_launcher_region region={0};
    if(!kui_disc_region_load(&region.region_free,log,cancel))
        return failure(out,"Region-free preference unavailable; see GD Play Options.",cancel,progress);
    if(!region.region_free) {
        region.console=kui_disc_region_console();
        if(region.console!='J' && region.console!='U' && region.console!='E')
            return failure(out,"Console region is unavailable; enable Region-free in GD Play Options.",cancel,progress);
    }
    bool enabled=false;
    unsigned slot=KUI_GAME_VMU_SLOT_A2;
    if(!preference(&enabled,&slot,log,cancel))
        return failure(out,"Virtual VMU preference unavailable; see Diagnostics.",cancel,progress);
    if(!enabled) return kui_disc_launcher_prepare_module(KUI_DISC_LAUNCH_PACKAGE,NULL,
        NULL,&region,image,out,log,cancel,progress);
    const char *package=slot==KUI_GAME_VMU_SLOT_A1?
        KUI_DISC_LAUNCH_VMU_A1_PACKAGE:KUI_DISC_LAUNCH_VMU_PACKAGE;
    struct disc_vmu_preflight preflight={.selection=selection};
    bool prepared=kui_disc_launcher_prepare_module(package,selected_card,
        &preflight,&region,image,out,log,cancel,progress);
    if(prepared) prepared=disc_vmu_seed_prepare(&preflight,image,out,log,cancel,progress);
    memset(&preflight,0,sizeof(preflight));return prepared;
}
