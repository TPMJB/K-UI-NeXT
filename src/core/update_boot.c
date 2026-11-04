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

void kui_update_boot_reset(void) {
    free(running);running=NULL;resolver=ROOT_MODULES;
}
static void block_modules(void) {
    kui_update_boot_reset();resolver=BLOCK_MODULES;
}
void kui_update_boot_block(void) {block_modules();}
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
    const struct kui_update_entry *entry=kui_update_package_find(running,relative);
    if(!entry || !kui_update_bank_path(running->build,entry->path,resolved,capacity))return false;
    if(stream) {
        stream->pinned=true;stream->bytes=entry->bytes;
        memcpy(stream->build,running->build,sizeof(stream->build));
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
    struct kui_runtime_image *out,struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    /* Root files are the deliberately retained baseline. A running bank may
     * not continue as baseline merely because a selection record disappeared. */
    kui_update_boot_reset();
    struct kui_runtime_info info;
    enum kui_runtime_result r=root_header(&info,cancel);
    if(r==KUI_RUNTIME_OK && !strcmp(compiled,info.build)) {
        /* The bootstrap already validated the running root GUI. Do not read
         * its multi-MiB payload a second time on every ordinary startup. */
        memcpy(view->build,info.build,sizeof(view->build));view->bank[0]=0;
        resolver=ROOT_MODULES;return KUI_UPDATE_BOOT_BASELINE;
    }
    if(r==KUI_RUNTIME_OK)r=kui_runtime_read(KUI_RUNTIME_PATH,out,log,cancel);
    if(r!=KUI_RUNTIME_OK)return problem(out,view,log,
        r==KUI_RUNTIME_CANCELLED?"Update boot selection stopped":"Root baseline runtime is unavailable; use Recovery");
    memcpy(view->build,out->info.build,sizeof(view->build));view->bank[0]=0;
    if(!strcmp(compiled,out->info.build)) {
        kui_runtime_free(out);resolver=ROOT_MODULES;return KUI_UPDATE_BOOT_BASELINE;
    }
    if(!patch(out,transport,true))return problem(out,view,log,"Root baseline lacks the selected storage handoff");
    resolver=BLOCK_MODULES;return KUI_UPDATE_BOOT_CHAIN;
}
enum kui_update_boot_result kui_update_boot_baseline(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
    if(!start(compiled,transport,out,view,log,cancel))return KUI_UPDATE_BOOT_ERROR;
    return baseline(compiled,transport,out,view,log,cancel);
}
static bool same_activation(const struct kui_update_activation *a,const struct kui_update_activation *b) {
    return a->sequence==b->sequence && !memcmp(a->selected,b->selected,sizeof(a->selected)) &&
        !memcmp(a->previous,b->previous,sizeof(a->previous)) &&
        !memcmp(a->manifest_sha256,b->manifest_sha256,sizeof(a->manifest_sha256));
}
static bool load_bank(const struct kui_update_activation *selected,struct kui_runtime_image *out,
    kui_log_fn log,kui_cancel_fn cancel) {
    block_modules();running=calloc(1,sizeof(*running));
    if(!running)return false;
    enum kui_update_result r=kui_update_bank_receipt_read(selected->selected,running,cancel);
    if(r!=KUI_UPDATE_OK || strcmp(running->build,selected->selected) ||
       memcmp(running->manifest_sha256,selected->manifest_sha256,32u))return false;
    resolver=BANK_MODULES;
    enum kui_runtime_result loaded=kui_runtime_read(KUI_RUNTIME_PATH,out,log,cancel);
    if(loaded!=KUI_RUNTIME_OK || !kui_update_capability_valid(out->data,out->info.payload_bytes))return false;
    return true;
}
enum kui_update_boot_result kui_update_boot_read(const char *compiled,unsigned transport,
    struct kui_runtime_image *out,struct kui_update_boot_view *view,kui_log_fn log,kui_cancel_fn cancel) {
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
        if(!selected->selected[0]) {
            if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Invalid update selection; using the retained baseline");
            /* An explicit rollback to baseline is authoritative. If its
             * root file is unavailable, do not reactivate an older bank. */
            return baseline(compiled,transport,out,view,log,cancel);
        }
        if(load_bank(selected,out,log,cancel)) {
            memcpy(view->bank,selected->selected,sizeof(view->bank));
            memcpy(view->build,out->info.build,sizeof(view->build));
            if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Newer update unavailable; using an older checked selection");
            if(!strcmp(compiled,out->info.build)) {
                kui_runtime_free(out);return KUI_UPDATE_BOOT_PINNED;
            }
            if(patch(out,transport,false)) {
                block_modules();return KUI_UPDATE_BOOT_CHAIN;
            }
            log("Updates: bank runtime lacks a unique storage handoff");
        }
        kui_runtime_free(out);block_modules();
        if(cancel())return problem(out,view,log,"Update boot selection stopped");
        view->fallback=true;log("Updates: bank %s refused; checking the retained selection",selected->selected);
    }
    if(view->fallback)snprintf(view->notice,sizeof(view->notice),"Update unavailable; using the retained root baseline");
    return baseline(compiled,transport,out,view,log,cancel);
}
