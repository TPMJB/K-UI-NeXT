/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_image_probe.h"
#include "kui/games.h"
#include "kui/game_metadata.h"
#include "kui/resident_image.h"
#include "kui/image_loader_layout.h"
#include "kui/media.h"
#include "platform.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct files {char root[KUI_DEST_ROOT_CAP];kui_cancel_fn cancel;};
static bool stopped(const struct files *f) {return f->cancel && f->cancel();}
static bool join(const struct files *f,const char *name,char out[KUI_GAMES_FILE_CAP+3]) {
    if(!kui_destination_name_valid(name)) return false;
    int n=snprintf(out,KUI_GAMES_FILE_CAP+3,"0:%s%s%s",f->root,
        strcmp(f->root,"/")?"/":"",name);
    return n>0 && n<(int)KUI_GAMES_FILE_CAP+3;
}
static bool split(const char *path,struct files *files,char name[KUI_GAME_NAME_CAP]) {
    if(!path || path[0]!='/' || strlen(path)>=KUI_GAMES_FILE_CAP) return false;
    const char *slash=strrchr(path,'/');size_t n=(size_t)(slash-path),len=strlen(slash+1);
    if(n>=sizeof(files->root) || len<=4 || len>=KUI_GAME_NAME_CAP) return false;
    const char *ext=slash+1+len-4;
    if(ext[0]!='.' || (ext[1]!='g'&&ext[1]!='G') ||
        (ext[2]!='d'&&ext[2]!='D') || (ext[3]!='i'&&ext[3]!='I')) return false;
    char root[KUI_DEST_ROOT_CAP];
    if(!n) strcpy(root,"/");else {memcpy(root,path,n);root[n]=0;}
    if(!kui_destination_normalize(files->root,root) || strcmp(root,files->root) ||
        !kui_destination_name_valid(slash+1)) return false;
    strcpy(name,slash+1);return true;
}
static enum kui_game_result stat_file(void *ctx,const char *name,uint64_t *bytes) {
    struct files *f=ctx;char path[KUI_GAMES_FILE_CAP+3];FILINFO info;
    if(stopped(f)) return KUI_GAME_CANCELLED;
    if(!join(f,name,path)) return KUI_GAME_INVALID;
    FRESULT r=f_stat(path,&info);
    if(r!=FR_OK) return r==FR_NO_FILE || r==FR_NO_PATH?KUI_GAME_NOT_FOUND:KUI_GAME_IO;
    if(info.fattrib&AM_DIR) return KUI_GAME_FILE_SIZE;
    *bytes=info.fsize;return KUI_GAME_OK;
}
static enum kui_game_result read_file(void *ctx,const char *name,uint64_t offset,
    void *out,size_t bytes) {
    struct files *f=ctx;char path[KUI_GAMES_FILE_CAP+3];FIL file;
    if(stopped(f)) return KUI_GAME_CANCELLED;
    if(!join(f,name,path) || bytes>UINT_MAX || (uint64_t)(FSIZE_t)offset!=offset) return KUI_GAME_INVALID;
    if(f_open(&file,path,FA_READ)!=FR_OK) return KUI_GAME_IO;
    bool ok=offset<=f_size(&file) && bytes<=f_size(&file)-offset;
    UINT got=0;
    if(ok) ok=f_lseek(&file,(FSIZE_t)offset)==FR_OK && f_tell(&file)==offset;
    if(ok) ok=f_read(&file,out,(UINT)bytes,&got)==FR_OK && got==bytes;
    if(f_close(&file)!=FR_OK) ok=false;
    return stopped(f)?KUI_GAME_CANCELLED:ok?KUI_GAME_OK:KUI_GAME_IO;
}
static enum kui_game_metadata_io_result metadata_read(void *ctx,uint32_t lba,uint8_t out[2048]) {
    enum kui_game_result r=kui_game_image_read(ctx,lba,1,KUI_GAME_SECTOR_MODE1,out,2048);
    return r==KUI_GAME_OK?KUI_GAME_METADATA_IO_OK:
        r==KUI_GAME_CANCELLED?KUI_GAME_METADATA_IO_CANCELLED:KUI_GAME_METADATA_IO_ERROR;
}
static bool metadata_range(void *ctx,uint32_t lba,uint32_t count) {
    return kui_game_image_check(ctx,lba,count,KUI_GAME_SECTOR_MODE1)==KUI_GAME_OK;
}
static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool layout(const struct kui_runtime_image *image) {
    const uint32_t begin=KUI_IMAGE_RESIDENT_BLOB_OFFSET,max=KUI_IMAGE_RESIDENT_MAX_BYTES;
    if(image->info.payload_bytes<begin+4 || image->info.payload_bytes>begin+max ||
        image->info.memory_bytes!=image->info.payload_bytes) return false;
    const uint8_t *h=(const uint8_t *)image->data+KUI_IMAGE_PACKAGE_HEADER_OFFSET;
    uint32_t n=le32(h+28);
    if(memcmp(h,KUI_IMAGE_PACKAGE_MAGIC,8) || le32(h+8)!=1 || le32(h+12)!=64 ||
        le32(h+16)!=KUI_IMAGE_MANIFEST_OFFSET || le32(h+20)!=KUI_RESIDENT_IMAGE_WIRE_BYTES ||
        le32(h+24)!=KUI_IMAGE_RESIDENT_ADDRESS || !n || n%4 || n>max || begin+n!=image->info.payload_bytes ||
        le32(h+32)!=KUI_IMAGE_RESIDENT_ADDRESS || le32(h+36)!=KUI_IMAGE_CLIENT_ADDRESS ||
        le32(h+40)!=KUI_IMAGE_CLIENT_STACK || le32(h+44)!=KUI_IMAGE_RESIDENT_STACK ||
        le32(h+48)!=begin || le32(h+52)!=KUI_IMAGE_CLIENT_MAX_BYTES || le32(h+56) || le32(h+60)) return false;
    for(unsigned i=0;i<KUI_RESIDENT_IMAGE_WIRE_BYTES;i++)
        if(((const uint8_t *)image->data)[KUI_IMAGE_MANIFEST_OFFSET+i]) return false;
    return true;
}
static bool map_track(struct files *files,FATFS *fs,const struct kui_volume *volume,
    const struct kui_game_image_track *track,struct kui_resident_manifest *map,unsigned index) {
    char path[KUI_GAMES_FILE_CAP+3];FIL file;
    if(stopped(files) || !join(files,track->name,path) || f_open(&file,path,FA_READ)!=FR_OK) return false;
    bool ok=f_size(&file)==track->file_bytes && fs->csize;
    struct kui_resident_track *t=&map->tracks[index];
    *t=(struct kui_resident_track){track->number,track->start_lba,track->end_lba,track->control,map->extent_count,0};
    uint32_t total=(uint32_t)((track->file_bytes+511u)/512u);
    for(uint32_t block=0;ok && block<total;) {
        if(stopped(files)) {ok=false;break;}
        FSIZE_t offset=(FSIZE_t)block*512u;uint8_t byte;UINT got=0;
        /* One cache read per allocation cluster, not one per track sector.
         * FatFs direct full-sector reads do not update FIL.sect reliably. */
        if(f_lseek(&file,offset)!=FR_OK || f_tell(&file)!=offset ||
            f_read(&file,&byte,1,&got)!=FR_OK || got!=1) {ok=false;break;}
        uint32_t count=fs->csize-block%fs->csize;
        if(count>total-block) count=total-block;
        if(file.sect<fs->database || file.sect>=volume->count ||
            (uint64_t)file.sect+count>volume->count) {ok=false;break;}
        uint32_t card=volume->start+(uint32_t)file.sect;
        struct kui_resident_extent *last=t->extent_count?&map->extents[map->extent_count-1]:NULL;
        if(last && (uint64_t)last->card_lba+last->blocks==card) last->blocks+=count;
        else {
            if(map->extent_count==KUI_RESIDENT_IMAGE_EXTENTS) {ok=false;break;}
            map->extents[map->extent_count++]=(struct kui_resident_extent){block,card,count};
            ++t->extent_count;
        }
        block+=count;
    }
    if(f_close(&file)!=FR_OK) ok=false;
    return ok;
}
static void sample(struct kui_resident_manifest *m,const struct kui_game_image *image,
    uint32_t lba,uint32_t count,enum kui_game_sector_format format) {
    if(m->sample_count==KUI_RESIDENT_IMAGE_SAMPLES ||
        kui_game_image_check(image,lba,count,format)!=KUI_GAME_OK) return;
    for(unsigned i=0;i<m->sample_count;i++) if(m->samples[i].lba==lba &&
        m->samples[i].count==count && m->samples[i].format==(unsigned)format) return;
    m->samples[m->sample_count++]=(struct kui_resident_sample){lba,count,format,0};
}
bool kui_games_image_probe_prepare(const char *path,struct kui_runtime_image *package,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!package || !log || !cancel) return false;
    *package=(struct kui_runtime_image){0};struct files files={.cancel=cancel};char name[KUI_GAME_NAME_CAP];
    if(!split(path,&files,name) || stopped(&files)) {log("Image probe: invalid path or cancelled");return false;}
    if(!kui_sd_connect()) {log("Image probe: SD unavailable");return false;}
    FATFS fs;bool ok=false;const char *problem="cannot mount SD";
    struct kui_resident_manifest *map=NULL;struct kui_game_image *image=NULL;uint8_t *gdi=NULL;
    if(!kui_mount(&fs,log)) goto done;
    enum kui_runtime_result rr=kui_runtime_read(KUI_GAMES_IMAGE_PROBE_PACKAGE,package,log,cancel);
    if(rr!=KUI_RUNTIME_OK) {problem=kui_runtime_result_name(rr);goto done;}
    if(!layout(package)) {problem="unsupported image-probe package layout";goto done;}
    uint64_t size=0;
    if(stat_file(&files,name,&size)!=KUI_GAME_OK || !size || size>KUI_GAME_GDI_LIMIT) {
        problem="GDI missing, unreadable or too large";goto done;
    }
    map=calloc(1,sizeof(*map));image=malloc(sizeof(*image));gdi=malloc((size_t)size);
    if(!map || !image || !gdi) {problem="insufficient memory to prepare selected image";goto done;}
    if(read_file(&files,name,0,gdi,(size_t)size)!=KUI_GAME_OK) {problem="cannot read GDI";goto done;}
    struct kui_game_file_ops file_ops={&files,stat_file,read_file};
    enum kui_game_result r=kui_game_image_open(gdi,(size_t)size,&file_ops,image);
    if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
    map->track_count=image->count;map->gdi_crc32=kui_crc32(0,gdi,(size_t)size);
    for(unsigned i=0;i<image->count;i++) if(image->tracks[i].control==4 && image->tracks[i].start_lba>=45000) {
        map->session_lba=image->tracks[i].start_lba;break;
    }
    struct kui_game_metadata metadata;struct kui_game_metadata_ops metadata_ops={image,metadata_read,metadata_range};
    enum kui_game_metadata_status ms=kui_game_metadata_read(&metadata_ops,map->session_lba,&metadata);
    if(ms!=KUI_GAME_METADATA_OK) {problem=kui_game_metadata_status_text(ms);goto done;}
    map->boot_lba=metadata.boot_lba;map->boot_bytes=metadata.boot_bytes;
    snprintf(map->title,sizeof(map->title),"%.127s",metadata.title);
    snprintf(map->product,sizeof(map->product),"%s",metadata.product);
    snprintf(map->region,sizeof(map->region),"%s",metadata.region);
    snprintf(map->bootfile,sizeof(map->bootfile),"%s",metadata.bootfile);
    const struct kui_volume volume=*kui_media_volume();
    if(!volume.count || (uint64_t)volume.start+volume.count>UINT32_MAX) {problem="invalid partition bounds";goto done;}
    map->partition_start=volume.start;map->partition_end=(uint64_t)volume.start+volume.count;
    map->card_sectors=map->partition_end; /* resident checks this upper bound fits actual capacity */
    log("Image probe: mapping %u tracks for %.100s",image->count,map->title);
    for(unsigned i=0;i<image->count;i++) {
        if(!map_track(&files,&fs,&volume,&image->tracks[i],map,i)) {
            problem="track map failed: changed file, I/O, bounds or fragmentation limit (4096 extents)";goto done;
        }
        log("Image map T%02u: %u extents",image->tracks[i].number,map->tracks[i].extent_count);
    }
    sample(map,image,map->boot_lba,1,KUI_GAME_SECTOR_MODE1);
    sample(map,image,map->boot_lba+(map->boot_bytes-1)/2048u,1,KUI_GAME_SECTOR_MODE1);
    sample(map,image,map->session_lba,1,KUI_GAME_SECTOR_MODE1);
    for(unsigned i=0;i<image->count;i++) if(image->tracks[i].control==0) {
        sample(map,image,image->tracks[i].start_lba,1,KUI_GAME_SECTOR_RAW);break;
    }
    for(unsigned i=1;i<image->count;i++) if(image->tracks[i-1].end_lba==image->tracks[i].start_lba) {
        sample(map,image,image->tracks[i].start_lba-1,2,KUI_GAME_SECTOR_RAW);break;
    }
    unsigned stride=(image->count+7)/8;
    for(unsigned i=0;i<image->count;i+=stride) sample(map,image,image->tracks[i].start_lba,1,KUI_GAME_SECTOR_RAW);
    sample(map,image,image->tracks[image->count-1].end_lba-1,1,KUI_GAME_SECTOR_RAW);
    uint8_t bytes[2*KUI_GAME_RAW_BYTES];
    for(unsigned i=0;i<map->sample_count;i++) {
        struct kui_resident_sample *s=&map->samples[i];
        size_t n=s->count*(s->format==KUI_GAME_SECTOR_RAW?2352u:2048u);
        r=kui_game_image_read(image,s->lba,s->count,(enum kui_game_sector_format)s->format,bytes,sizeof(bytes));
        if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
        s->crc32=kui_crc32(0,bytes,n);
    }
    r=kui_resident_manifest_encode(map,(uint8_t *)package->data+KUI_IMAGE_MANIFEST_OFFSET);
    if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
    log("Image probe prepared: %u tracks, %u extents, %u reference samples; boot %.24s (not executed)",
        map->track_count,map->extent_count,map->sample_count,map->bootfile);ok=true;
done:
    if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="cannot release filesystem";}
    kui_sd_disconnect();free(gdi);free(image);free(map);
    if(stopped(&files)) {ok=false;problem="cancelled before image handoff";}
    if(!ok) {kui_runtime_free(package);log("Image probe: %s",problem);}
    return ok;
}
