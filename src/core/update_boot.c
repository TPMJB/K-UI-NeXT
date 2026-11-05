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
static struct {bool active;enum resolver_mode mode;struct kui_update_package *package;} reload_previous;

void kui_update_boot_reset(void) {
    free(running);running=NULL;resolver=ROOT_MODULES;
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
bool kui_update_runtime_resolve(const char *logical,char *resolved,size_t capacity,
    struct kui_update_runtime_stream *stream) {
    if(stream)memset(stream,0,sizeof(*stream));
    if(!logical || !resolved || !capacity || resolver==BLOCK_MODULES)return false;
    if(resolver==ROOT_MODULES)return copy_path(logical,resolved,capacity);
    static const char prefix[]="0:/KUI/";
    if(!running || strncmp(logical,prefix,sizeof(prefix)-1u))return false;
    const char *relative=logical+sizeof(prefix)-1u;
    struct kui_update_entry member;
    if(!kui_update_package_member(running,relative,&member))return false;
    const struct kui_update_entry *entry=&member;
    /* Deliberate inheritance preserves the retained module inventory,
     * including absence. Replacement failures never take this branch. */
    if(entry && entry->policy==KUI_UPDATE_INHERIT_ROOT)return copy_path(logical,resolved,capacity);
    const char *build=entry?kui_update_entry_build(running->build,entry):NULL;
    if(!build || !kui_update_bank_path(running->build,entry->path,resolved,capacity))return false;
    if(stream) {
        stream->pinned=true;stream->bytes=entry->bytes;
        memcpy(stream->build,build,sizeof(stream->build));
        memcpy(stream->expected,entry->sha256,sizeof(stream->expected));
        kui_sha256_init(&stream->hash);stream->update=stream_update;stream->finish=stream_finish;
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
    image->info.crc32=kui_crc32(0,image->data,image->info.payload_bytes);return true;
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
    memcpy(view->build,out->info.build,sizeof(view->build));view->bank[0]=0;
    if(!force && !strcmp(compiled,out->info.build)) {
        kui_runtime_free(out);resolver=ROOT_MODULES;return KUI_UPDATE_BOOT_BASELINE;
    }
    if(!patch(out,transport,true))return problem(out,view,log,"Root baseline lacks the selected storage handoff");
    resolver=BLOCK_MODULES;return KUI_UPDATE_BOOT_CHAIN;
}
enum kui_update_boot_result kui_update_boot_baseline(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    if(!start(compiled,transport,out,view,log,cancel))return KUI_UPDATE_BOOT_ERROR;
    return baseline(compiled,transport,out,view,log,cancel,false);
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
    if(!context || !kui_update_handoff_decode(context->wire,&h) ||
       strcmp(compiled,h.build) || h.transport!=transport || h.payload_offset!=context->payload_offset ||
       h.sequence!=selected->sequence || strcmp(h.build,selected->selected) ||
       memcmp(h.manifest_sha256,selected->manifest_sha256,32u) || cancel())return false;
    const struct kui_update_entry *entry=kui_update_package_find(running,"runtime.kui");
    if(!entry || h.file_bytes!=entry->bytes || memcmp(h.runtime_sha256,entry->sha256,32u))return false;
    char path[KUI_UPDATE_BOOT_PATH_BYTES];
    if(!kui_update_bank_path(running->build,entry->path,path,sizeof(path)))return false;
    FIL file;if(f_open(&file,path,FA_READ)!=FR_OK)return false;
    uint8_t header[64],slot[KUI_UPDATE_HANDOFF_BYTES];UINT got=0;struct kui_runtime_info info;
    bool ok=f_size(&file)==h.file_bytes && f_read(&file,header,sizeof(header),&got)==FR_OK && got==sizeof(header) &&
        kui_runtime_header(header,f_size(&file),&info)==KUI_RUNTIME_OK && !strcmp(info.build,h.build) &&
        info.crc32==h.payload_crc32 && info.memory_bytes==h.memory_bytes &&
        h.payload_offset<=info.payload_bytes-KUI_UPDATE_HANDOFF_BYTES && !cancel();
    if(ok)ok=f_lseek(&file,64u+h.payload_offset)==FR_OK &&
        f_read(&file,slot,sizeof(slot),&got)==FR_OK && got==sizeof(slot) &&
        kui_update_handoff_empty(slot,sizeof(slot),NULL);
    if(f_close(&file)!=FR_OK)ok=false;
    return ok && !cancel();
}
static bool load_runtime(struct kui_runtime_image *out,kui_log_fn log,kui_cancel_fn cancel) {
    enum kui_runtime_result loaded=kui_runtime_read(KUI_RUNTIME_PATH,out,log,cancel);
    return loaded==KUI_RUNTIME_OK && kui_update_capability_valid(out->data,out->info.payload_bytes) &&
        kui_update_handoff_empty(out->data,out->info.payload_bytes,NULL);
}
static bool stamp(struct kui_runtime_image *image,const struct kui_update_activation *selected,unsigned transport) {
    const struct kui_update_entry *entry=kui_update_package_find(running,"runtime.kui");
    struct kui_update_handoff h={.transport=transport,.sequence=selected->sequence,
        .file_bytes=entry?entry->bytes:0u,.payload_crc32=image->info.crc32,.memory_bytes=image->info.memory_bytes};
    if(!entry || !kui_update_handoff_empty(image->data,image->info.payload_bytes,&h.payload_offset))return false;
    memcpy(h.build,selected->selected,sizeof(h.build));
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
    image->info.crc32=kui_crc32(0,image->data,image->info.payload_bytes);return true;
}
static enum kui_update_boot_result read_selected(const char *compiled,unsigned transport,
    const struct kui_update_handoff_context *context,struct kui_runtime_image *out,
    struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel,bool force) {
    if(!start(compiled,transport,out,view,log,cancel))return KUI_UPDATE_BOOT_ERROR;
    struct kui_update_activation slots[2]={{0}};
    enum kui_update_result result[2];
    for(unsigned i=0;i<2u;i++) {
        result[i]=kui_update_activation_read(i,slots+i,cancel);
        if(result[i]==KUI_UPDATE_CANCELLED || cancel())return problem(out,view,log,"Update boot selection stopped");
        if(result[i]!=KUI_UPDATE_OK && result[i]!=KUI_UPDATE_NOT_FOUND)view->fallback=true;
    }
    bool valid[2]={result[0]==KUI_UPDATE_OK,result[1]==KUI_UPDATE_OK};
    if(valid[0] && valid[1] && slots[0].sequence==slots[1].sequence && !same_activation(slots,slots+1u)) {
        valid[0]=valid[1]=false;view->fallback=true;
        log("Updates: conflicting activation records; using the root baseline");
    }
    unsigned first=valid[1] && (!valid[0] || slots[1].sequence>slots[0].sequence)?1u:0u;
    for(unsigned attempt=0;attempt<2u;attempt++) {
        unsigned at=first^attempt;
        if(!valid[at])continue;
        const struct kui_update_activation *selected=slots+at;
        if(kui_update_build_withdrawn(selected->selected)) {
            view->fallback=true;log("Updates: skipping withdrawn GUI %s",selected->selected);continue;
        }
        if(!selected->selected[0]) {
            if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Invalid update selection; using the retained baseline");
            /* An explicit rollback to baseline is authoritative. If its
             * root file is unavailable, do not reactivate an older bank. */
            return baseline(compiled,transport,out,view,log,cancel,force);
        }
        if(load_receipt(selected,cancel)) {
            if(!force && adopt(compiled,transport,selected,context,cancel)) {
                memcpy(view->bank,selected->selected,sizeof(view->bank));
                memcpy(view->build,compiled,sizeof(view->build));
                if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Newer update unavailable; using an older checked selection");
                log("Updates: using the checked RAM bank handoff");return KUI_UPDATE_BOOT_PINNED;
            }
        }
        if(running && resolver==BANK_MODULES && !cancel() && load_runtime(out,log,cancel)) {
            memcpy(view->bank,selected->selected,sizeof(view->bank));
            memcpy(view->build,out->info.build,sizeof(view->build));
            if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Newer update unavailable; using an older checked selection");
            if(!force && !strcmp(compiled,out->info.build)) {
                kui_runtime_free(out);return KUI_UPDATE_BOOT_PINNED;
            }
            if(stamp(out,selected,transport)) {
                block_modules();return KUI_UPDATE_BOOT_CHAIN;
            }
            log("Updates: bank runtime lacks a unique storage handoff");
        }
        kui_runtime_free(out);block_modules();
        if(cancel())return problem(out,view,log,"Update boot selection stopped");
        view->fallback=true;log("Updates: bank %s refused; checking the retained selection",selected->selected);
    }
    if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Update unavailable; using the retained root baseline");
    return baseline(compiled,transport,out,view,log,cancel,force);
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
