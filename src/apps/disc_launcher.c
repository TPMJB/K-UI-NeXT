/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/disc_launcher.h"
#include "kui/disc_boot.h"
#include "kui/disc_launch.h"
#include "kui/hash.h"
#include "platform.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct optical_source {
    kui_log_fn log;
    kui_cancel_fn cancel;
    kui_app_progress_fn progress;
    struct kui_app_status *status;
    uint32_t reads;
    bool io_failed;
};
static void publish(struct optical_source *s) {
    if(s->progress) s->progress(s->status);
}
static bool stopped(void *context) {
    struct optical_source *s=context;
    return s->cancel && s->cancel();
}
static enum kui_disc_boot_io_result read_sector(void *context,uint32_t fad,uint8_t data[2048]) {
    struct optical_source *s=context;
    uint8_t raw[KUI_RAW_BYTES];
    if(stopped(s)) return KUI_DISC_BOOT_IO_CANCELLED;
    enum kui_read_result result=kui_disc_read_raw(NULL,fad,1u,raw);
    if(result!=KUI_READ_OK) {
        s->io_failed=true;
        s->log("Physical disc: read refused at FAD %" PRIu32 ", result %u",fad,(unsigned)result);
        return KUI_DISC_BOOT_IO_ERROR;
    }
    int offset=kui_data_offset(raw);
    if(offset!=16 || raw[15]!=1u || !kui_sector_edc_valid_at(raw,fad)) {
        s->io_failed=true;
        s->log("Physical disc: sector address/EDC refused at FAD %" PRIu32,fad);
        return KUI_DISC_BOOT_IO_ERROR;
    }
    memcpy(data,raw+(unsigned)offset,2048u);
    ++s->reads;s->status->done=(uint64_t)s->reads*2048u;
    if(!(s->reads&127u)) publish(s);
    return KUI_DISC_BOOT_IO_OK;
}
static bool source_open(struct optical_source *s,struct kui_disc_boot_source *source) {
    struct kui_toc toc[2];
    bool is_cd=true;
    if(stopped(s) || !kui_disc_prepare_capture(toc,&is_cd)) return false;
    if(is_cd || !toc[1].count || toc[1].count>99u) return false;
    /* Only a positively identified GD high-density data track supplies bounds.
     * No audio/gap or later extent is folded into this first boot prototype. */
    for(unsigned i=0;i<toc[1].count;i++) {
        const struct kui_track *t=&toc[1].tracks[i];
        if(!(t->control&4u)) continue;
        if(t->start<45150u || t->end<=t->start) return false;
        *source=(struct kui_disc_boot_source){.context=s,.read_sector=read_sector,
            .cancelled=stopped,.disc_type=KUI_DISC_BOOT_GD_TYPE,
            .first_fad=t->start,.end_fad=t->end};
        return true;
    }
    return false;
}
static bool fail(struct optical_source *s,const char *message) {
    s->status->passed=false;s->status->complete=true;
    s->status->stopped=!s->io_failed && stopped(s);s->status->errors=s->status->stopped?0u:1u;
    snprintf(s->status->message,sizeof(s->status->message),"%s",message);
    s->log("Physical disc: %s",message);publish(s);return false;
}
static void describe(struct optical_source *s,const struct kui_disc_boot_plan *plan) {
    struct kui_app_status *out=s->status;
    out->line_count=4u;
    snprintf(out->lines[0],KUI_APP_LINE_CAP,"%.70s",plan->metadata.title);
    snprintf(out->lines[1],KUI_APP_LINE_CAP,"Product %.10s / region %.8s",plan->metadata.product,plan->metadata.region);
    snprintf(out->lines[2],KUI_APP_LINE_CAP,"%.16s: %" PRIu32 " bytes",plan->metadata.bootfile,plan->metadata.boot_bytes);
    snprintf(out->lines[3],KUI_APP_LINE_CAP,"IP %08" PRIX32 " / executable %08" PRIX32,plan->ip_crc32,plan->boot_crc32);
    s->log("Physical disc: %s; product %s; region %s; %s",plan->metadata.title,
        plan->metadata.product,plan->metadata.region,plan->metadata.bootfile);
    s->log("Physical disc: data FAD %" PRIu32 "..%" PRIu32 "; boot FAD %" PRIu32 "; %" PRIu32 " bytes",
        plan->first_fad,plan->end_fad,plan->boot_fad,plan->metadata.boot_bytes);
    s->log("Physical disc: full IP CRC32 %08" PRIx32 "; exact executable CRC32 %08" PRIx32,
        plan->ip_crc32,plan->boot_crc32);
}
static bool inspect(struct optical_source *s,struct kui_disc_boot_source *source,
    struct kui_disc_boot_plan *plan) {
    snprintf(s->status->message,sizeof(s->status->message),"Checking the physical disc's native executable...");
    publish(s);
    if(!source_open(s,source)) return fail(s,"Native GD data track unavailable; check the disc and drive log.");
    enum kui_disc_boot_status result=kui_disc_boot_prepare(source,plan);
    if(result!=KUI_DISC_BOOT_OK) return fail(s,kui_disc_boot_status_text(result));
    describe(s,plan);return true;
}
void kui_disc_launcher_check(struct kui_app_status *out,kui_log_fn log,
    kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out || !log || !cancel) return;
    *out=(struct kui_app_status){0};
    struct optical_source s={log,cancel,progress,out,0u,false};
    struct kui_disc_boot_source source;struct kui_disc_boot_plan plan;
    if(!inspect(&s,&source,&plan)) return;
    out->complete=out->passed=true;
    snprintf(out->message,sizeof(out->message),"Native GD boot bytes checked. X checks again; Y tests direct boot.");
    publish(&s);
}
bool kui_disc_launcher_prepare(struct kui_runtime_image *image,
    struct kui_app_status *out,kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!image || !out || !log || !cancel) return false;
    *image=(struct kui_runtime_image){0};*out=(struct kui_app_status){0};
    struct optical_source s={log,cancel,progress,out,0u,false};
    if(stopped(&s)) return fail(&s,"Direct disc boot stopped.");
    /* A physical-GD baseline must never read any virtual-card catalogue,
     * assignment, image, journal or preference, even when VMU was On in Games. */
    if(!kui_sd_connect()) return fail(&s,"SD unavailable for the direct-disc bootstrap.");
    FATFS fs;bool mounted=kui_mount(&fs,log),loaded=false;
    if(mounted) loaded=kui_runtime_read(KUI_DISC_LAUNCH_PACKAGE,image,log,cancel)==KUI_RUNTIME_OK;
    bool released=f_mount(NULL,"0:",0)==FR_OK;
    kui_sd_disconnect();
    if(!mounted || !loaded || !released) {
        kui_runtime_free(image);return fail(&s,"Direct-disc bootstrap unavailable; copy the complete test KUI folder.");
    }
    struct kui_disc_launch_plan layout;
    if(!kui_disc_launch_layout(image->data,image->info.payload_bytes,4096u,&layout)) {
        kui_runtime_free(image);return fail(&s,"Direct-disc bootstrap template refused.");
    }
    struct kui_disc_boot_source source;struct kui_disc_boot_plan plan;
    if(!inspect(&s,&source,&plan)) {kui_runtime_free(image);return false;}
    if(!kui_disc_launch_layout(image->data,image->info.payload_bytes,plan.metadata.boot_bytes,&layout)) {
        kui_runtime_free(image);return fail(&s,"This first direct-boot test supports native executables up to 4 MiB.");
    }
    void *grown=realloc(image->data,layout.payload_bytes);
    if(!grown) {kui_runtime_free(image);return fail(&s,"Not enough contiguous RAM to stage this disc executable.");}
    image->data=grown;
    memset((uint8_t *)grown+layout.module_bytes,0,layout.payload_bytes-layout.module_bytes);
    snprintf(out->message,sizeof(out->message),"Staging checked physical-disc boot bytes...");publish(&s);
    uint8_t sector[2048];
    for(unsigned which=0;which<2u;which++) {
        uint32_t bytes=which?layout.exec_bytes:KUI_DISC_BOOT_IP_BYTES;
        uint32_t fad=which?plan.boot_fad:plan.first_fad;
        uint8_t *destination=(uint8_t *)grown+(which?layout.exec_offset:layout.ip_offset);
        for(uint32_t done=0;done<bytes;) {
            if(read_sector(&s,fad++,sector)!=KUI_DISC_BOOT_IO_OK) {
                kui_runtime_free(image);return fail(&s,"Physical-disc read stopped or failed while staging boot bytes.");
            }
            uint32_t n=bytes-done;if(n>sizeof(sector)) n=sizeof(sector);
            memcpy(destination+done,sector,n);done+=n;
        }
    }
    layout.ip_crc32=plan.ip_crc32;layout.exec_crc32=plan.boot_crc32;
    memcpy(layout.ip_sha256,plan.ip_sha256,32u);memcpy(layout.exec_sha256,plan.boot_sha256,32u);
    struct kui_disc_launch_plan checked;
    if(stopped(&s) || !kui_disc_launch_encode((uint8_t *)grown+KUI_DISC_LAUNCH_CONFIG_OFFSET,&layout) ||
       !kui_disc_launch_validate(grown,layout.payload_bytes,&checked)) {
        kui_runtime_free(image);return fail(&s,"Physical-disc boot bytes changed or handoff was stopped.");
    }
    if(!kui_disc_native_handoff(plan.first_fad,(const uint8_t *)grown+layout.ip_offset)) {
        kui_runtime_free(image);return fail(&s,"Could not restore checked 2048-byte drive reads for the game.");
    }
    image->info.payload_bytes=image->info.memory_bytes=layout.payload_bytes;
    image->info.crc32=kui_crc32(0,grown,layout.payload_bytes);
    out->complete=out->passed=true;
    snprintf(out->message,sizeof(out->message),"Direct-disc boot prepared. Virtual VMU is disabled for this test.");
    log("Physical disc: direct native boot prepared; build %s; no VMU/card lookup",image->info.build);
    publish(&s);return true;
}
