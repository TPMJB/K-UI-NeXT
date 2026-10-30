/* SPDX-License-Identifier: GPL-3.0-only */
/* Included by vmu.c so collection capture uses the existing guarded physical
 * reader. Physical source callbacks expose no write or filesystem operation. */
#include "kui/vmu_image_pack.h"
struct collection_source {
    struct session *session;
    uint32_t functions;
    maple_devinfo_t info;
};
static bool collection_same_source(struct collection_source *source) {
    struct session *s=source->session;
    if(!same_device(s)) return false;
    if(s->device->info.functions!=source->functions ||
       memcmp(&s->device->info,&source->info,sizeof(source->info)))
        return fail(s,"Source VMU identity changed; refresh and preview again");
    return true;
}
static int collection_source_read(void *context,uint16_t block,void *out) {
    struct collection_source *source=context;
    if(!collection_same_source(source) || !read_block(source->session,block,out) ||
       !collection_same_source(source)) {
        source->session->log("VMU source read rejected at block %u",(unsigned)block);
        return -1;
    }
    return 0;
}
static void collection_digest(const void *bytes,size_t length,uint8_t out[32]) {
    struct kui_sha256 hash;kui_sha256_init(&hash);
    kui_sha256_update(&hash,bytes,length);kui_sha256_digest(&hash,out);
}
static void collection_root_diagnostic(struct session *s,const struct snapshot *snap) {
    const uint8_t *root=snap->root;
    s->log("VMU physical root: FAT %u+%u dir %u+%u icon %u user %u custom %u",
        (unsigned)le16(root+0x46),(unsigned)le16(root+0x48),
        (unsigned)le16(root+0x4a),(unsigned)le16(root+0x4c),
        (unsigned)le16(root+0x4e),(unsigned)le16(root+0x50),(unsigned)root[16]);
    if(!strcmp(s->out->status.message,"Unsupported or damaged VMU filesystem layout"))
        status(s,"Source layout rejected: FAT %u+%u dir %u+%u user %u",
            (unsigned)le16(root+0x46),(unsigned)le16(root+0x48),
            (unsigned)le16(root+0x4a),(unsigned)le16(root+0x4c),(unsigned)le16(root+0x50));
}
static bool collection_unlock(struct session *s,bool ok) {
    if(vmufs_mutex_unlock()==0) return ok;
    if(!s->out->status.errors && !s->out->status.stopped)
        fail(s,"Cannot unlock source VMU reader");
    else s->log("VMU: Cannot unlock source VMU reader");
    return false;
}
/* Follow the physical card's own root and check the pinned device information
 * around every read. Source system bytes are never treated as a virtual card. */
static bool collection_snapshot(struct collection_source *source,struct snapshot *snap) {
    struct session *s=source->session;memset(snap,0,sizeof(*snap));
    if(vmufs_mutex_lock()!=0) return fail(s,"Cannot lock source VMU metadata");
    bool ok=!collection_source_read(source,255u,snap->root) && snapshot_layout(s,snap);
    if(ok) ok=!collection_source_read(source,le16(snap->root+70),snap->fat);
    for(unsigned i=0;ok && i<snap->directory_blocks;i++)
        ok=!collection_source_read(source,(uint16_t)(snap->directory_start-i),snap->directory+i*BLOCK_BYTES);
    if(ok) ok=validate_snapshot(s,snap);
    if(!ok) collection_root_diagnostic(s,snap);
    return collection_unlock(s,ok);
}
static bool collection_check_snapshot(struct collection_source *source,
    const struct snapshot *original,struct snapshot *check) {
    if(!collection_snapshot(source,check)) return false;
    if(check->fingerprint!=original->fingerprint || check->directory_blocks!=original->directory_blocks ||
       memcmp(check->root,original->root,BLOCK_BYTES) || memcmp(check->fat,original->fat,BLOCK_BYTES) ||
       memcmp(check->directory,original->directory,original->directory_blocks*BLOCK_BYTES))
        return fail(source->session,"Source VMU metadata changed; refresh and preview again");
    return true;
}
static bool collection_payloads(struct collection_source *source,const struct snapshot *snap,
    const struct kui_vmu_image_file *files,unsigned count,bool compare) {
    struct session *s=source->session;
    if(vmufs_mutex_lock()!=0) return fail(s,"Cannot lock source VMU saves");
    bool ok=true;_Alignas(32) uint8_t scratch[BLOCK_BYTES];
    for(unsigned i=0;ok && i<count;i++) {
        unsigned at=le16(files[i].entry+2),blocks=le16(files[i].entry+24);
        for(unsigned n=0;ok && n<blocks;n++) {
            uint8_t *data=(uint8_t *)files[i].payload+n*BLOCK_BYTES;
            if(collection_source_read(source,(uint16_t)at,compare?scratch:data)) {ok=false;break;}
            if(compare && memcmp(scratch,data,BLOCK_BYTES)) {
                char message[128];snprintf(message,sizeof(message),
                    "Source rejected: save %u changed at block %u",i+1u,at);
                ok=fail(s,message);break;
            }
            at=le16(snap->fat+at*2u);
        }
    }
    return collection_unlock(s,ok);
}
/* Import logical saves into a separate, canonical card. No physical root,
 * system FAT entry, inactive directory slot or free flash is transplanted. */
static bool collection_capture(struct collection_source *source,
    const struct snapshot *snap,struct snapshot *check,bool all,unsigned row,
    uint8_t image[IMAGE_BYTES],struct kui_vmu_image_workspace *workspace,
    struct kui_vmu_image_summary *summary,bool strict_metadata) {
    struct session *s=source->session;unsigned count=all?snap->count:1u,blocks=0;
    if(!healthy_metadata(s,snap)) return false;
    for(unsigned i=0;i<count;i++)
        blocks+=le16(snap->directory+snap->entries[all?i:row]*32u+24u);
    if(blocks>200u || count>KUI_VMU_IMAGE_DIRECTORY_ENTRIES) {
        char message[128];snprintf(message,sizeof(message),
            "Source saves need %u blocks; a new virtual card holds 200",blocks);
        return fail(s,message);
    }
    struct kui_vmu_image_file *files=calloc(count,sizeof(*files));
    uint8_t *payload=malloc((size_t)blocks*BLOCK_BYTES);bool ok=false;
    if(!files || !payload) {fail(s,"Not enough RAM for checked source saves");goto done;}
    size_t offset=0;
    for(unsigned i=0;i<count;i++) {
        const uint8_t *entry=snap->directory+snap->entries[all?i:row]*32u;
        if(strict_metadata && ((entry[1]!=0 && entry[1]!=0xff) || entry[28] || entry[29] || entry[30] || entry[31])) {
            char message[128];snprintf(message,sizeof(message),
                "Source entry %u rejected: protection %02x reserved %02x%02x%02x%02x",
                (unsigned)snap->entries[all?i:row],entry[1],entry[28],entry[29],entry[30],entry[31]);
            fail(s,message);goto done;
        }
        unsigned name_bytes=12u;
        while(name_bytes && (!entry[4u+name_bytes-1u] || entry[4u+name_bytes-1u]==' ')) --name_bytes;
        bool valid_name=name_bytes!=0u;
        for(unsigned n=0;n<name_bytes;n++)
            if(entry[4u+n]<32u || entry[4u+n]==127u) valid_name=false;
        memset(workspace->names[i],0,12u);memcpy(workspace->names[i],entry+4u,name_bytes);
        for(unsigned n=0;n<i;n++)
            if(!memcmp(workspace->names[n],workspace->names[i],12u)) valid_name=false;
        if(!valid_name) {
            char message[128];snprintf(message,sizeof(message),
                "Source entry %u rejected: invalid or duplicate filename",
                (unsigned)snap->entries[all?i:row]);
            fail(s,message);goto done;
        }
        memcpy(files[i].entry,entry,32u);files[i].bytes=(size_t)le16(entry+24)*BLOCK_BYTES;
        files[i].payload=payload+offset;offset+=files[i].bytes;
    }
    if(!collection_check_snapshot(source,snap,check) ||
       !collection_payloads(source,snap,files,count,false) ||
       !collection_check_snapshot(source,snap,check) ||
       !collection_payloads(source,snap,files,count,true) ||
       !collection_check_snapshot(source,snap,check)) goto done;
    int result=kui_vmu_image_pack(files,count,image,IMAGE_BYTES,workspace,summary);
    if(result) {
        char message[128];snprintf(message,sizeof(message),
            "Source saves rejected: image %d, %u files, %u blocks",result,count,blocks);
        fail(s,message);goto done;
    }
    ok=collection_same_source(source);
done:
    free(payload);free(files);return ok;
}

bool kui_vmu_capture_run(unsigned source_slot,void *image,size_t bytes,
    struct kui_vmu_image_summary *summary,struct kui_vmu_view *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return false;
    memset(out,0,sizeof(*out));out->slot=source_slot;
    struct session source={out,log?log:quiet_log,cancel,progress,NULL,source_slot/2u,source_slot%2u+1u,false};
    struct snapshot *snap=NULL,*check=NULL;
    struct kui_vmu_image_workspace *workspace=NULL;
    struct collection_source physical={.session=&source};
    bool ok=false;
    if(!image || !summary || bytes!=IMAGE_BYTES || source_slot>=8u) {
        fail(&source,"Choose a valid physical VMU and capture buffer");goto done;
    }
    memset(summary,0,sizeof(*summary));
    if(cancelled(&source)) goto done;
    source.device=maple_enum_dev((int)source.port,(int)source.unit);
    if(!source.device || !source.device->valid || !(source.device->info.functions&MAPLE_FUNC_MEMCARD)) {
        fail(&source,"No physical VMU in the selected source slot");goto done;
    }
    physical.functions=source.device->info.functions;
    memcpy(&physical.info,&source.device->info,sizeof(physical.info));
    snap=malloc(sizeof(*snap));check=malloc(sizeof(*check));workspace=malloc(sizeof(*workspace));
    if(!snap || !check || !workspace) {fail(&source,"Not enough RAM for checked source capture");goto done;}
    if(!collection_snapshot(&physical,snap)) goto done;
    fill_view(&source,snap,0);out->present=true;
    if(!snap->count) {fail(&source,"Source VMU has no saves to import");goto done;}
    status(&source,"Reading and checking all source saves...");
    if(!collection_capture(&physical,snap,check,true,0,image,workspace,summary,false)) goto done;
    out->bulk_total=out->bulk_completed=summary->files;
    out->free_blocks=summary->free_blocks;
    out->status.done=out->status.total=(uint64_t)summary->used_blocks*BLOCK_BYTES;
    status(&source,"Captured %u saves using %u of 200 blocks",summary->files,summary->used_blocks);
    ok=true;
done:
    free(workspace);free(check);free(snap);
    if(!ok && summary) memset(summary,0,sizeof(*summary));
    out->status.complete=ok;out->status.passed=ok;
    if(progress) progress(&out->status);
    return ok;
}

static struct {
    maple_device_t *device;
    uint32_t functions,fingerprint;
    maple_devinfo_t info;
    uint8_t sha256[32];
    unsigned slot,page,selected;
    bool valid,all;
} collection_preview;

void kui_vmu_collection_copy_run(unsigned source_slot,unsigned page,unsigned selected,
    bool all,bool commit,struct kui_vmu_view *out,kui_log_fn log,kui_cancel_fn cancel,
    kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));out->slot=source_slot;
    struct session source={out,log?log:quiet_log,cancel,progress,NULL,source_slot/2u,source_slot%2u+1u,false};
    struct snapshot *snap=NULL,*check=NULL;
    uint8_t *image=NULL;struct kui_vmu_image_workspace *workspace=NULL;
    struct kui_vmu_image_summary summary={0};uint8_t sha256[32];
    FATFS fs;bool connected=false,mounted=false,ok=false;
    struct collection_source physical={.session=&source};
    struct kui_vmu_collection_card card;
    if(source_slot>=8u) {collection_preview.valid=false;fail(&source,"Choose a valid physical source VMU");goto done;}
    if(commit) {
        bool matches=collection_preview.valid && collection_preview.slot==source_slot &&
            collection_preview.page==page && collection_preview.selected==selected && collection_preview.all==all;
        collection_preview.valid=false; /* Every confirmation is consumed. */
        if(!matches) {fail(&source,"Preview this virtual-card copy before confirming");goto done;}
    } else collection_preview.valid=false;
    if(cancelled(&source)) goto done;
    if(!listed[source_slot].valid) {fail(&source,"Refresh the physical source VMU before copying");goto done;}
    source.device=maple_enum_dev((int)source.port,(int)source.unit);
    if(!source.device || !source.device->valid || !(source.device->info.functions&MAPLE_FUNC_MEMCARD)) {
        listed[source_slot].valid=false;fail(&source,"No physical VMU in the selected source slot");goto done;
    }
    physical.functions=source.device->info.functions;
    memcpy(&physical.info,&source.device->info,sizeof(physical.info));
    snap=malloc(sizeof(*snap));check=malloc(sizeof(*check));image=malloc(IMAGE_BYTES);
    workspace=malloc(sizeof(*workspace));
    if(!snap || !check || !image || !workspace) {fail(&source,"Not enough RAM for virtual-card save capture");goto done;}
    if(!collection_snapshot(&physical,snap)) goto done;
    if(listed[source_slot].device!=source.device || listed[source_slot].fingerprint!=snap->fingerprint) {
        listed[source_slot].valid=false;fail(&source,"Source VMU changed since listing; refresh before copying");goto done;
    }
    fill_view(&source,snap,page);out->present=true;
    if(!snap->count) {fail(&source,"Source VMU has no saves to copy");goto done;}
    if(!all && (out->page!=page || selected>=out->count)) {
        fail(&source,"Selected physical save is no longer available");goto done;
    }
    status(&source,"Capturing checked source saves; physical VMU stays read only...");
    unsigned row=all?0u:out->page*KUI_VMU_ROWS+selected;
    if(!collection_capture(&physical,snap,check,all,row,image,workspace,&summary,true)) goto done;
    collection_digest(image,IMAGE_BYTES,sha256);
    if(commit && (collection_preview.device!=source.device ||
       collection_preview.functions!=physical.functions || collection_preview.fingerprint!=snap->fingerprint ||
       memcmp(&collection_preview.info,&physical.info,sizeof(physical.info)) ||
       memcmp(collection_preview.sha256,sha256,sizeof(sha256)))) {
        fail(&source,"Source VMU or save bytes changed since preview; preview again");goto done;
    }
    char label[16];
    if(all) snprintf(label,sizeof(label),"%c%u save set",'A'+source.port,source.unit);
    else {
        const uint8_t *entry=snap->directory+snap->entries[row]*32u;
        display_name(label,entry);
        out->count=out->total=1u;memset(out->entries,0,sizeof(out->entries));
        display_name(out->entries[0].name,entry);out->entries[0].bytes=(uint32_t)le16(entry+24)*BLOCK_BYTES;
    }
    out->bulk_total=out->bulk_remaining=summary.files;out->free_blocks=summary.free_blocks;
    out->status.total=(uint64_t)summary.used_blocks*BLOCK_BYTES;
    if(!commit) {
        collection_preview.device=source.device;collection_preview.functions=physical.functions;
        collection_preview.fingerprint=snap->fingerprint;
        memcpy(&collection_preview.info,&physical.info,sizeof(physical.info));
        memcpy(collection_preview.sha256,sha256,sizeof(sha256));
        collection_preview.slot=source_slot;collection_preview.page=page;collection_preview.selected=selected;
        collection_preview.all=all;collection_preview.valid=true;out->copy_ready=true;
        if(all) status(&source,"Copy all %u saves together to a new 200-block virtual card?",summary.files);
        else status(&source,"Copy %s to a new virtual card? Companion files are not inferred.",label);
        ok=true;goto done;
    }
    if(cancelled(&source)) goto done;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(!kui_sd_connect()) {fail(&source,"Cannot connect SD for a new virtual card");goto done;}
    connected=true;
    if(!kui_mount(&fs,source.log)) {fail(&source,"Cannot mount SD for a new virtual card");goto done;}
    mounted=true;
    if(!collection_same_source(&physical) || !collection_check_snapshot(&physical,snap,check) || cancelled(&source)) goto done;
    status(&source,"Creating and verifying a separate virtual save-set card...");
    int result=kui_vmu_collection_publish(image,IMAGE_BYTES,label,&card,source.log,cancel);
    if(result) {
        if(!cancelled(&source)) fail(&source,"Virtual-card creation failed; existing cards are preserved");
        goto done;
    }
    if(strlen(card.path)>=sizeof(out->backup_path)) {fail(&source,"Created virtual-card path is too long");goto done;}
    strcpy(out->backup_path,card.path);out->bulk_completed=summary.files;out->bulk_remaining=0u;
    out->status.done=out->status.total;out->free_blocks=card.summary.free_blocks;
    status(&source,"Stored %u saves; use Assign games in Virtual VMUs",summary.files);
    source.log("VMU collection verified v%04u: %u saves, %u free blocks; source %c%u read only; assign games from Virtual VMUs",
        card.id,summary.files,card.summary.free_blocks,'A'+source.port,source.unit);
    ok=true;
done:
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(&source,"Cannot unmount virtual-card filesystem");ok=false;}
    if(connected) kui_sd_disconnect();
    free(workspace);free(image);free(check);free(snap);
    if(!ok) {collection_preview.valid=false;out->copy_ready=false;}
    out->page=page;out->status.complete=out->status.passed=ok;
    if(progress) progress(&out->status);
}

#include "kui/vmu_library.h"

void kui_vmu_collection_run(unsigned page,struct kui_vmu_backup_view *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress) {
    if(!out) return;
    memset(out,0,sizeof(*out));out->page=page;
    struct kui_vmu_view view={0};
    struct session s={&view,log?log:quiet_log,cancel,progress,NULL,0,1,true};
    FATFS fs;bool connected=false,mounted=false,ok=false;
    if(cancelled(&s)) goto done;
    kui_sd_set_params(KUI_STORAGE_AUTO,true);
    if(!kui_sd_connect()) {fail(&s,"Cannot connect SD for virtual VMUs");goto done;}
    connected=true;
    if(!kui_mount(&fs,s.log)) {fail(&s,"Cannot mount SD for virtual VMUs");goto done;}
    mounted=true;status(&s,"Reading virtual cards and game assignments...");
    ok=kui_vmu_library_list(page,out,s.log,cancel)==KUI_VMU_LIBRARY_OK;
    view.status=out->status;
 done:
    if(mounted && f_mount(NULL,"0:",0)!=FR_OK) {fail(&s,"Cannot unmount virtual-card filesystem");ok=false;}
    if(connected) kui_sd_disconnect();
    view.status.complete=true;view.status.passed=ok;out->status=view.status;
    if(progress) progress(&out->status);
}
