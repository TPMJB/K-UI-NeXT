/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/update_boot.h"
#include "kui/update.h"
#include "kui/storage_policy.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum resolver_mode { ROOT_MODULES, BANK_MODULES, BLOCK_MODULES };
static enum resolver_mode resolver;
static struct kui_update_package *running;
static unsigned required_engine;
static bool context_known;
static struct {bool active;enum resolver_mode mode;struct kui_update_package *package;} reload_previous;

void kui_update_boot_reset(void) {
    free(running);running=NULL;resolver=ROOT_MODULES;
}
void kui_update_boot_require_engine(unsigned level) {
    if(level>required_engine)required_engine=level;
    context_known=true;
}
int kui_update_effective_snapshot(char bank[13],uint8_t manifest_sha256[32]) {
    if(!bank || !manifest_sha256)return -1;
    memset(bank,0,13u);memset(manifest_sha256,0,32u);
    if(!context_known)return 0;
    if(resolver==BLOCK_MODULES)return -1;
    if(resolver==ROOT_MODULES)return 1;
    if(!running)return -1;
    memcpy(bank,running->build,13u);memcpy(manifest_sha256,running->manifest_sha256,32u);
    return 1;
}
static void block_modules(void) {
    kui_update_boot_reset();resolver=BLOCK_MODULES;
}
void kui_update_boot_block(void) {block_modules();}
void kui_update_boot_reload_abort(void) {
    if(!reload_previous.active)return;
    block_modules();running=reload_previous.package;resolver=reload_previous.mode;
    memset(&reload_previous,0,sizeof(reload_previous));
}
static void stream_update(struct kui_update_runtime_stream *s,const void *data,size_t bytes) {
    kui_sha256_update(&s->hash,data,bytes);
}
static bool stream_finish(const struct kui_update_runtime_stream *s) {
    uint8_t digest[32];kui_sha256_digest(&s->hash,digest);
    return !memcmp(digest,s->expected,sizeof(digest));
}
static bool copy_path(const char *path,char *out,size_t capacity) {
    if(!path || !out || !capacity)return false;
    size_t n=strlen(path);
    if(n>=capacity)return false;
    memmove(out,path,n+1u);return true;
}
static bool bank_member(const char *logical,struct kui_update_entry *entry) {
    static const char prefix[]="0:/KUI/";
    return resolver==BANK_MODULES && running && logical &&
        !strncmp(logical,prefix,sizeof(prefix)-1u) &&
        kui_update_package_member(running,logical+sizeof(prefix)-1u,entry);
}
bool kui_update_runtime_absent(const char *logical) {
    struct kui_update_entry member;
    return bank_member(logical,&member) && member.policy==KUI_UPDATE_REMOVE;
}
bool kui_update_runtime_resolve(const char *logical,char *resolved,size_t capacity,
    struct kui_update_runtime_stream *stream) {
    if(stream)memset(stream,0,sizeof(*stream));
    if(!logical || !resolved || !capacity || resolver==BLOCK_MODULES)return false;
    if(resolver==ROOT_MODULES)return copy_path(logical,resolved,capacity);
    struct kui_update_entry member;
    if(!bank_member(logical,&member))return false;
    const struct kui_update_entry *entry=&member;
    /* Deliberate inheritance preserves the retained module inventory,
     * including absence. Replacement failures never take this branch. */
    if(entry && entry->policy==KUI_UPDATE_INHERIT_ROOT)return copy_path(logical,resolved,capacity);
    if(entry->policy==KUI_UPDATE_REMOVE)return false;
    const char *build=entry?kui_update_entry_build(running->build,entry):NULL;
    bool raw=entry->policy==KUI_UPDATE_RAW;
    if((!raw && !build) || !kui_update_bank_path(running->build,entry->path,resolved,capacity))return false;
    if(stream) {
        stream->pinned=true;stream->raw=raw;stream->bytes=entry->bytes;
        if(build)memcpy(stream->build,build,sizeof(stream->build));
        memcpy(stream->expected,entry->sha256,sizeof(stream->expected));
        kui_sha256_init(&stream->hash);stream->crc32=kui_crc32_capture;
        stream->update=stream_update;stream->finish=stream_finish;
    }
    return true;
}
static enum kui_update_boot_result problem(struct kui_runtime_image *out,
    struct kui_update_boot_view *view,kui_log_fn log,const char *message) {
    kui_runtime_free(out);block_modules();
    snprintf(view->notice,sizeof(view->notice),"%s",message);log("Updates: %s",message);
    return KUI_UPDATE_BOOT_ERROR;
}
static bool start(const char *compiled,unsigned transport,struct kui_runtime_image *out,
    struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    if(out)memset(out,0,sizeof(*out));
    if(view)memset(view,0,sizeof(*view));
    block_modules();
    return out && view && log && cancel && kui_update_build_valid(compiled) &&
        transport<KUI_STORAGE_AUTO;
}
static bool patch(struct kui_runtime_image *image,unsigned transport,bool legacy) {
    if(!kui_storage_patch_boot(image->data,image->info.payload_bytes,transport) &&
       (!legacy || transport!=KUI_STORAGE_SCIF))return false;
    image->info.crc32=kui_crc32_capture(0,image->data,image->info.payload_bytes);return true;
}
static enum kui_runtime_result root_header(struct kui_runtime_info *info,kui_cancel_fn cancel) {
    if(cancel())return KUI_RUNTIME_CANCELLED;
    FIL file;UINT got=0;uint8_t header[KUI_RUNTIME_HEADER_BYTES];
    if(f_open(&file,KUI_RUNTIME_PATH,FA_READ)!=FR_OK)return KUI_RUNTIME_IO;
    enum kui_runtime_result r=KUI_RUNTIME_IO;
    if(f_read(&file,header,sizeof(header),&got)==FR_OK && got==sizeof(header))
        r=kui_runtime_header(header,f_size(&file),info);
    if(f_close(&file)!=FR_OK)r=KUI_RUNTIME_IO;
    return cancel()?KUI_RUNTIME_CANCELLED:r;
}
static enum kui_update_boot_result baseline(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel,bool force) {
    /* Root files are the deliberately retained baseline. A running bank may
     * not continue as baseline merely because a selection record disappeared. */
    kui_update_boot_reset();
    struct kui_runtime_info info;
    enum kui_runtime_result r=root_header(&info,cancel);
    if(r==KUI_RUNTIME_OK && kui_update_build_withdrawn(info.build))
        return problem(out,view,log,"Retained GUI was withdrawn; copy the updater add-on runtime before restoring");
    if(!force && r==KUI_RUNTIME_OK && !strcmp(compiled,info.build)) {
        /* The bootstrap already validated the running root GUI. Do not read
         * its multi-MiB payload a second time on every ordinary startup. */
        memcpy(view->build,info.build,sizeof(view->build));view->bank[0]=0;
        resolver=ROOT_MODULES;return KUI_UPDATE_BOOT_BASELINE;
    }
    if(r==KUI_RUNTIME_OK)r=kui_runtime_read(KUI_RUNTIME_PATH,out,log,cancel);
    if(r!=KUI_RUNTIME_OK)return problem(out,view,log,
        r==KUI_RUNTIME_CANCELLED?"Update boot selection stopped":"Root baseline runtime is unavailable; use Recovery");
    if(kui_update_build_withdrawn(out->info.build))
        return problem(out,view,log,"Retained GUI was withdrawn; copy the updater add-on runtime before restoring");
    if(kui_update_engine_level(out->data,out->info.payload_bytes)<required_engine)
        return problem(out,view,log,"Retained GUI needs the current updater engine; replace the root runtime");
    memcpy(view->build,out->info.build,sizeof(view->build));view->bank[0]=0;
    if(!force && !strcmp(compiled,out->info.build)) {
        kui_runtime_free(out);resolver=ROOT_MODULES;return KUI_UPDATE_BOOT_BASELINE;
    }
    if(!patch(out,transport,true))return problem(out,view,log,"Root baseline lacks the selected storage handoff");
    resolver=BLOCK_MODULES;return KUI_UPDATE_BOOT_CHAIN;
}
static bool chained_context(const struct kui_update_handoff_context *context) {
    /* An ordinary root GUI has its initialized EMPTY slot. A previous bank
     * may have the same source build but different configuration, so even a
     * damaged nonempty context cannot authorize continuing as root. Root
     * images are chained with EMPTY slots, which ends this extra hop. */
    return context && !kui_update_handoff_empty(context->wire,sizeof(context->wire),NULL);
}
enum kui_update_boot_result kui_update_boot_baseline(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    if(!start(compiled,transport,out,view,log,cancel))return KUI_UPDATE_BOOT_ERROR;
    return baseline(compiled,transport,out,view,log,cancel,false);
}
enum kui_update_boot_result kui_update_boot_baseline_handoff(const char *compiled,unsigned transport,
    const struct kui_update_handoff_context *context,struct kui_runtime_image *out,
    struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    if(!start(compiled,transport,out,view,log,cancel))return KUI_UPDATE_BOOT_ERROR;
    return baseline(compiled,transport,out,view,log,cancel,chained_context(context));
}
static bool same_activation(const struct kui_update_activation *a,const struct kui_update_activation *b) {
    return a->sequence==b->sequence && !memcmp(a->selected,b->selected,sizeof(a->selected)) &&
        !memcmp(a->previous,b->previous,sizeof(a->previous)) &&
        !memcmp(a->manifest_sha256,b->manifest_sha256,sizeof(a->manifest_sha256));
}
static bool load_receipt(const struct kui_update_activation *selected,kui_cancel_fn cancel) {
    block_modules();running=calloc(1,sizeof(*running));
    if(!running)return false;
    enum kui_update_result r=kui_update_bank_receipt_read(selected->selected,running,cancel);
    if(r!=KUI_UPDATE_OK || strcmp(running->build,selected->selected) ||
       memcmp(running->manifest_sha256,selected->manifest_sha256,32u))return false;
    resolver=BANK_MODULES;return true;
}
/* The first GUI checked the full immutable source. Rebind that RAM transfer to
 * the freshly selected receipt and the source's still-EMPTY slot. No filled
 * on-disk marker is accepted as proof; a direct boot has no RAM context and
 * always takes the full SHA/CRC path. */
static bool adopt(const char *compiled,unsigned transport,const struct kui_update_activation *selected,
    const struct kui_update_handoff_context *context,kui_cancel_fn cancel) {
    struct kui_update_handoff h;
    /* V1 transfers predate an engine floor. Once the production GUI opts in,
     * reread those sources rather than adopting a legacy GUI1 transfer. V2
     * always checks engine2 before creating a handoff; future floors require
     * a fresh read as well. */
    if((required_engine && kui_update_package_version(running)<2u) || required_engine>2u ||
       !context || !kui_update_handoff_decode(context->wire,&h) ||
       strcmp(compiled,h.build) || h.transport!=transport || h.payload_offset!=context->payload_offset ||
       h.sequence!=selected->sequence ||
       memcmp(h.manifest_sha256,selected->manifest_sha256,32u) || cancel())return false;
    const struct kui_update_entry *entry=kui_update_package_find(running,"runtime.kui");
    const char *build=entry?kui_update_entry_build(running->build,entry):NULL;
    if(!build || strcmp(build,h.build) || h.file_bytes!=entry->bytes ||
       memcmp(h.runtime_sha256,entry->sha256,32u))return false;
    char path[KUI_UPDATE_BOOT_PATH_BYTES];
    if(!kui_update_bank_path(running->build,entry->path,path,sizeof(path)))return false;
    FIL file;if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    uint8_t header[64],slot[KUI_UPDATE_HANDOFF_BYTES];UINT got=0;struct kui_runtime_info info;
    bool ok=f_size(&file)==h.file_bytes && f_read(&file,header,sizeof(header),&got)==FR_OK && got==sizeof(header) &&
        kui_runtime_header(header,f_size(&file),&info)==KUI_RUNTIME_OK && !strcmp(info.build,h.build) &&
        info.crc32==h.payload_crc32 && info.memory_bytes==h.memory_bytes &&
        info.payload_bytes>=KUI_UPDATE_HANDOFF_BYTES &&
        h.payload_offset<=info.payload_bytes-KUI_UPDATE_HANDOFF_BYTES && !cancel();
    if(ok)ok=f_lseek(&file,64u+h.payload_offset)==FR_OK &&
        f_read(&file,slot,sizeof(slot),&got)==FR_OK && got==sizeof(slot) &&
        kui_update_handoff_empty(slot,sizeof(slot),NULL);
    if(f_close(&file)!=FR_OK)ok=false;
    return ok && !cancel();
}
static bool load_runtime(struct kui_runtime_image *out,uint32_t *handoff_offset,kui_log_fn log,kui_cancel_fn cancel) {
    enum kui_runtime_result loaded=kui_runtime_read(KUI_RUNTIME_PATH,out,log,cancel);
    unsigned minimum=kui_update_package_version(running)>=2u && required_engine<2u?2u:required_engine;
    return loaded==KUI_RUNTIME_OK && kui_update_capability_valid(out->data,out->info.payload_bytes) &&
        kui_update_engine_level(out->data,out->info.payload_bytes)>=minimum &&
        kui_update_handoff_empty(out->data,out->info.payload_bytes,handoff_offset);
}
static bool stamp(struct kui_runtime_image *image,uint32_t handoff_offset,
    const struct kui_update_activation *selected,unsigned transport) {
    const struct kui_update_entry *entry=kui_update_package_find(running,"runtime.kui");
    struct kui_update_handoff h={.transport=transport,.sequence=selected->sequence,
        .file_bytes=entry?entry->bytes:0u,.payload_crc32=image->info.crc32,.memory_bytes=image->info.memory_bytes};
    /* load_runtime already checked uniqueness across these immutable bytes.
     * Recheck the bounded slot before mutation without rescanning the GUI. */
    if(!entry || handoff_offset>image->info.payload_bytes ||
       image->info.payload_bytes-handoff_offset<KUI_UPDATE_HANDOFF_BYTES ||
       !kui_update_handoff_empty((const uint8_t *)image->data+handoff_offset,KUI_UPDATE_HANDOFF_BYTES,NULL))return false;
    h.payload_offset=handoff_offset;
    memcpy(h.build,image->info.build,sizeof(h.build));
    memcpy(h.manifest_sha256,selected->manifest_sha256,32u);memcpy(h.runtime_sha256,entry->sha256,32u);
    /* Coherence token, never a secret or an authentication nonce. It varies
     * between attempted transfers and cannot replace any source validation. */
    static uint64_t serial;uintptr_t address=(uintptr_t)image->data;uint8_t digest[32];
    struct kui_sha256 hash;kui_sha256_init(&hash);++serial;
    kui_sha256_update(&hash,&serial,sizeof(serial));kui_sha256_update(&hash,&address,sizeof(address));
    kui_sha256_update(&hash,&h.sequence,sizeof(h.sequence));kui_sha256_update(&hash,h.runtime_sha256,32u);
    kui_sha256_update(&hash,h.manifest_sha256,32u);kui_sha256_digest(&hash,digest);
    memcpy(h.nonce,digest,sizeof(h.nonce));h.nonce[0]|=1u;
    if(!kui_storage_patch_boot(image->data,image->info.payload_bytes,transport) ||
       !kui_update_handoff_encode((uint8_t *)image->data+h.payload_offset,&h))return false;
    image->info.crc32=kui_crc32_capture(0,image->data,image->info.payload_bytes);return true;
}
static enum kui_update_boot_result read_selected(const char *compiled,unsigned transport,
    const struct kui_update_handoff_context *context,struct kui_runtime_image *out,
    struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel,bool force) {
    if(!start(compiled,transport,out,view,log,cancel))return KUI_UPDATE_BOOT_ERROR;
    bool force_root=force || chained_context(context);
    struct kui_update_activation slots[2]={{0}};
    enum kui_update_result result[2];
    for(unsigned i=0;i<2u;i++) {
        result[i]=kui_update_activation_read(i,slots+i,cancel);
        if(result[i]==KUI_UPDATE_CANCELLED || cancel())return problem(out,view,log,"Update boot selection stopped");
        if(result[i]!=KUI_UPDATE_OK && result[i]!=KUI_UPDATE_NOT_FOUND)view->fallback=true;
    }
    bool valid[2]={result[0]==KUI_UPDATE_OK,result[1]==KUI_UPDATE_OK};
    struct {bool present;char bank[13];uint8_t manifest_sha256[32];} failed_source={0};
    if(valid[0] && valid[1] && slots[0].sequence==slots[1].sequence && !same_activation(slots,slots+1u)) {
        valid[0]=valid[1]=false;view->fallback=true;
        log("Updates: conflicting activation records; using the root baseline");
    }
    unsigned first=valid[1] && (!valid[0] || slots[1].sequence>slots[0].sequence)?1u:0u;
    for(unsigned attempt=0;attempt<2u;attempt++) {
        unsigned at=first^attempt;
        if(!valid[at])continue;
        const struct kui_update_activation *selected=slots+at;
        if(!selected->selected[0]) {
            if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Invalid update selection; using the retained baseline");
            /* An explicit rollback to baseline is authoritative. If its
             * root file is unavailable, do not reactivate an older bank. */
            return baseline(compiled,transport,out,view,log,cancel,force_root);
        }
        if(load_receipt(selected,cancel)) {
            const struct kui_update_entry *gui=kui_update_package_find(running,"runtime.kui");
            const char *gui_build=gui?kui_update_entry_build(running->build,gui):NULL;
            /* A v2 bank ID describes a complete configuration. Withdrawal
             * applies to the GUI's actual source build, never that bank ID. */
            if(!gui_build || kui_update_build_withdrawn(gui_build)) {
                view->fallback=true;log("Updates: skipping an unavailable or withdrawn bank GUI");
                block_modules();continue;
            }
            if(!force && adopt(compiled,transport,selected,context,cancel)) {
                memcpy(view->bank,selected->selected,sizeof(view->bank));
                memcpy(view->build,compiled,sizeof(view->build));
                if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Newer update unavailable; using an older checked selection");
                log("Updates: using the checked RAM bank handoff");return KUI_UPDATE_BOOT_PINNED;
            }
        }
        bool loaded=false;uint32_t handoff_offset=UINT32_MAX;
        if(running && resolver==BANK_MODULES && !cancel()) {
            /* Adoption above remains sequence-specific. A failed immutable
             * source, however, cannot become valid by rereading the same bank
             * and receipt for the older activation slot. */
            bool repeated=failed_source.present && !strcmp(failed_source.bank,selected->selected) &&
                !memcmp(failed_source.manifest_sha256,selected->manifest_sha256,32u);
            if(repeated)log("Updates: skipping an already refused GUI source");
            else {
                loaded=load_runtime(out,&handoff_offset,log,cancel);
                if(!loaded) {
                    failed_source.present=true;memcpy(failed_source.bank,selected->selected,sizeof(failed_source.bank));
                    memcpy(failed_source.manifest_sha256,selected->manifest_sha256,32u);
                }
            }
        }
        if(loaded) {
            memcpy(view->bank,selected->selected,sizeof(view->bank));
            memcpy(view->build,out->info.build,sizeof(view->build));
            if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Newer update unavailable; using an older checked selection");
            if(!force && kui_update_package_version(running)<2u && !strcmp(compiled,out->info.build)) {
                kui_runtime_free(out);return KUI_UPDATE_BOOT_PINNED;
            }
            if(stamp(out,handoff_offset,selected,transport)) {
                block_modules();return KUI_UPDATE_BOOT_CHAIN;
            }
            log("Updates: bank runtime lacks a unique storage handoff");
        }
        kui_runtime_free(out);block_modules();
        if(cancel())return problem(out,view,log,"Update boot selection stopped");
        view->fallback=true;log("Updates: bank %s refused; checking the retained selection",selected->selected);
    }
    if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Update unavailable; using the retained root baseline");
    return baseline(compiled,transport,out,view,log,cancel,force_root);
}
enum kui_update_boot_result kui_update_boot_read(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    return kui_update_boot_read_handoff(compiled,transport,NULL,out,view,log,cancel);
}
enum kui_update_boot_result kui_update_boot_read_handoff(const char *compiled,unsigned transport,
    const struct kui_update_handoff_context *context,struct kui_runtime_image *out,
    struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    return read_selected(compiled,transport,context,out,view,log,cancel,false);
}
enum kui_update_boot_result kui_update_boot_reload(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    kui_update_boot_reload_abort();
    reload_previous.active=true;reload_previous.mode=resolver;reload_previous.package=running;
    running=NULL;resolver=BLOCK_MODULES;
    enum kui_update_boot_result r=read_selected(compiled,transport,NULL,out,view,log,cancel,true);
    if(r!=KUI_UPDATE_BOOT_CHAIN)kui_update_boot_reload_abort();
    return r;
}
