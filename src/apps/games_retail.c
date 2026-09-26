/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_retail.h"
#include "kui/games.h"
#include "kui/game_metadata.h"
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"
#include "kui/media.h"
#include "platform.h"
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct files {
    char root[KUI_DEST_ROOT_CAP], open_name[KUI_GAME_NAME_CAP];
    kui_cancel_fn cancel;
    FIL reader;
    bool reader_open;
};
static bool close_reader(struct files *f) {
    if(!f->reader_open) return true;
    f->reader_open=false;
    return f_close(&f->reader)==FR_OK;
}
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
    struct files *f=ctx;char path[KUI_GAMES_FILE_CAP+3];
    if(stopped(f)) return KUI_GAME_CANCELLED;
    if(!join(f,name,path) || bytes>UINT_MAX || (uint64_t)(FSIZE_t)offset!=offset) return KUI_GAME_INVALID;
    /* Keep the current track open while hashing. Reopening for every 2352
     * bytes made FatFs walk its allocation chain from the start each time. */
    if(f->reader_open && strcmp(f->open_name,name) && !close_reader(f)) return KUI_GAME_IO;
    if(!f->reader_open) {
        if(f_open(&f->reader,path,FA_READ)!=FR_OK) return KUI_GAME_IO;
        f->reader_open=true;
        snprintf(f->open_name,sizeof(f->open_name),"%s",name);
    }
    FIL *file=&f->reader;
    bool ok=offset<=f_size(file) && bytes<=f_size(file)-offset;
    UINT got=0;
    if(ok && f_tell(file)!=offset) ok=f_lseek(file,(FSIZE_t)offset)==FR_OK && f_tell(file)==offset;
    if(ok) ok=f_read(file,out,(UINT)bytes,&got)==FR_OK && got==bytes;
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
    const uint32_t begin=KUI_RETAIL_STAGE_BLOB_OFFSET,max=KUI_RETAIL_STAGE_MAX_BYTES;
    if(!image->data || image->info.payload_bytes<begin+4 || image->info.payload_bytes>begin+max ||
        image->info.memory_bytes!=image->info.payload_bytes) return false;
    const uint8_t *h=(const uint8_t *)image->data+KUI_RETAIL_HEADER_OFFSET;
    uint32_t n=le32(h+28);
    if(memcmp(h,KUI_RETAIL_PACKAGE_MAGIC,8) || le32(h+8)!=KUI_RETAIL_PACKAGE_VERSION ||
        le32(h+12)!=KUI_RETAIL_HEADER_BYTES || le32(h+16)!=KUI_RETAIL_MAP_OFFSET ||
        le32(h+20)!=KUI_RETAIL_IMAGE_WIRE_BYTES || le32(h+24)!=KUI_RETAIL_STAGE_ADDRESS ||
        !n || n%4 || n>max || begin+n!=image->info.payload_bytes ||
        le32(h+32)!=KUI_RETAIL_STAGE_ADDRESS || le32(h+36)!=KUI_RETAIL_EXEC_ADDRESS ||
        le32(h+40)!=KUI_RETAIL_EXEC_MAX_BYTES || le32(h+44)!=KUI_RETAIL_STAGE_STACK ||
        le32(h+48)!=begin || le32(h+52)!=KUI_RETAIL_RESIDENT_ADDRESS ||
        le32(h+56)!=KUI_RETAIL_RESIDENT_LIMIT || le32(h+60)) return false;
    for(unsigned i=0;i<KUI_RETAIL_IMAGE_WIRE_BYTES;i++)
        if(((const uint8_t *)image->data)[KUI_RETAIL_MAP_OFFSET+i]) return false;
    return true;
}
/* FatFs fast-seek link map: the table size, then (cluster count, first
 * cluster) pairs ending in zero. Room for more runs than a manifest can hold,
 * so an over-fragmented track fails on the extent limit, not on this table. */
#define LINK_MAP_WORDS (2u+2u*(KUI_RETAIL_IMAGE_EXTENTS+1u))
static DWORD link_map[LINK_MAP_WORDS]; /* One I/O worker prepares at a time. */
static bool map_track(struct files *files,FATFS *fs,const struct kui_volume *volume,
    const struct kui_game_image_track *track,struct kui_retail_manifest *map,unsigned index) {
    char path[KUI_GAMES_FILE_CAP+3];FIL file;
    if(stopped(files) || !join(files,track->name,path) || f_open(&file,path,FA_READ)!=FR_OK) return false;
    bool ok=f_size(&file)==track->file_bytes && fs->csize;
    struct kui_retail_track *t=&map->tracks[index];
    *t=(struct kui_retail_track){track->number,track->start_lba,track->end_lba,track->control,map->extent_count,0};
    uint32_t total=(uint32_t)((track->file_bytes+511u)/512u);
    /* The allocation table alone lists the file's contiguous cluster runs:
     * no track data is read (one data read per cluster took seconds). */
    link_map[0]=LINK_MAP_WORDS;file.cltbl=link_map;
    if(ok) ok=f_lseek(&file,CREATE_LINKMAP)==FR_OK && !stopped(files);
    uint32_t block=0;
    for(const DWORD *run=link_map+1;ok && block<total;run+=2) {
        if(!run[0] || run[1]<2u || run[1]>=fs->n_fatent) {ok=false;break;}
        uint64_t sect=(uint64_t)fs->database+(uint64_t)(run[1]-2u)*fs->csize;
        uint64_t count=(uint64_t)run[0]*fs->csize;
        if(count>total-block) count=total-block;
        if(sect<fs->database || sect>=volume->count || sect+count>volume->count) {ok=false;break;}
        uint32_t card=volume->start+(uint32_t)sect;
        struct kui_retail_extent *last=t->extent_count?&map->extents[map->extent_count-1]:NULL;
        if(last && (uint64_t)last->card_lba+last->blocks==card) last->blocks+=(uint32_t)count;
        else {
            if(map->extent_count==KUI_RETAIL_IMAGE_EXTENTS) {ok=false;break;}
            map->extents[map->extent_count++]=(struct kui_retail_extent){block,card,(uint32_t)count};
            ++t->extent_count;
        }
        block+=(uint32_t)count;
    }
    file.cltbl=NULL;
    if(f_close(&file)!=FR_OK) ok=false;
    return ok;
}
/* Hash only the requested logical bytes of the IP. The boot executable is not
 * read here: the stage checks each of its sectors' headers as it loads them. */
static enum kui_game_result extent_crc(const struct kui_game_image *image,
    uint32_t lba,uint32_t bytes,uint32_t *crc) {
    if(!bytes) return KUI_GAME_INVALID;
    enum kui_game_result result=kui_game_image_check(image,lba,(bytes+2047u)/2048u,
        KUI_GAME_SECTOR_MODE1);
    if(result!=KUI_GAME_OK) return result;
    uint8_t sector[2048];uint32_t value=0;
    for(uint32_t done=0;done<bytes;) {
        result=kui_game_image_read(image,lba++,1,KUI_GAME_SECTOR_MODE1,sector,sizeof(sector));
        if(result!=KUI_GAME_OK) return result;
        uint32_t take=bytes-done;if(take>sizeof(sector)) take=sizeof(sector);
        value=kui_retail_crc32(value,sector,take);done+=take;
    }
    *crc=value;return KUI_GAME_OK;
}
bool kui_games_retail_prepare(const char *path,struct kui_runtime_image *package,
    kui_log_fn log,kui_cancel_fn cancel) {
    if(!package) return false;
    *package=(struct kui_runtime_image){0};
    if(!log || !cancel) return false;
    struct files files={.cancel=cancel};char name[KUI_GAME_NAME_CAP];
    if(!split(path,&files,name) || stopped(&files)) {log("Retail boot: invalid path or cancelled");return false;}
    if(!kui_sd_connect()) {log("Retail boot: SD unavailable");return false;}
    FATFS fs;bool ok=false;const char *problem="cannot mount SD";
    struct kui_retail_manifest *map=NULL;struct kui_game_image *image=NULL;uint8_t *gdi=NULL;
    if(!kui_mount(&fs,log)) goto done;
    enum kui_runtime_result rr=kui_runtime_read(KUI_GAMES_RETAIL_PACKAGE,package,log,cancel);
    if(rr!=KUI_RUNTIME_OK) {problem=kui_runtime_result_name(rr);goto done;}
    if(!layout(package)) {problem="unsupported retail-boot package layout";goto done;}
    uint64_t size=0;
    if(stat_file(&files,name,&size)!=KUI_GAME_OK || !size || size>KUI_GAME_GDI_LIMIT) {
        problem="GDI missing, unreadable or too large";goto done;
    }
    map=calloc(1,sizeof(*map));image=malloc(sizeof(*image));gdi=malloc((size_t)size);
    if(!map || !image || !gdi) {problem="insufficient memory to prepare selected image";goto done;}
    if(read_file(&files,name,0,gdi,(size_t)size)!=KUI_GAME_OK) {problem="cannot read GDI";goto done;}
    struct kui_game_file_ops file_ops={&files,stat_file,read_file};
    enum kui_game_result r=kui_game_image_open(gdi,(size_t)size,&file_ops,image);
    if(r!=KUI_GAME_OK) {
        problem=r==KUI_GAME_UNSUPPORTED?
            "raw 2352-byte GDI tracks with zero file offsets required":kui_game_result_name(r);
        goto done;
    }
    if(image->count>KUI_RETAIL_IMAGE_TRACKS) {problem="launch map supports at most 16 tracks";goto done;}
    map->track_count=image->count;map->gdi_crc32=kui_retail_crc32(0,gdi,(size_t)size);
    for(unsigned i=0;i<image->count;i++) if(image->tracks[i].control==4 && image->tracks[i].start_lba>=45000) {
        map->session_lba=image->tracks[i].start_lba;break;
    }
    if(!map->session_lba) {problem="raw GD-ROM high-density session required";goto done;}
    struct kui_game_metadata metadata;struct kui_game_metadata_ops metadata_ops={image,metadata_read,metadata_range};
    enum kui_game_metadata_status ms=kui_game_metadata_read(&metadata_ops,map->session_lba,&metadata);
    if(ms!=KUI_GAME_METADATA_OK) {problem=kui_game_metadata_status_text(ms);goto done;}
    /* The owner's IP/ISO metadata selects the executable. Titles and boot
     * filenames are not compatibility gates; native GD bytes stay verbatim. */
    if(metadata.windows_ce) {problem="Windows CE game launching is not supported";goto done;}
    if(!metadata.native_gd) {problem="native GD-ROM with valid IP peripheral flags required";goto done;}
    if(metadata.boot_bytes<KUI_RETAIL_TRAMPOLINE_BYTES ||
        metadata.boot_bytes>KUI_RETAIL_EXEC_MAX_BYTES ||
        metadata.boot_bytes>KUI_RETAIL_IMAGE_BOOT_MAX) {
        problem="boot executable must be 128 bytes to 12 MiB";goto done;
    }
    if(metadata.boot_lba<map->session_lba+16u ||
        kui_game_image_check(image,map->session_lba,16,KUI_GAME_SECTOR_MODE1)!=KUI_GAME_OK) {
        problem="boot executable and full IP must be in high-density data tracks";goto done;
    }
    map->boot_lba=metadata.boot_lba;map->boot_bytes=metadata.boot_bytes;
    snprintf(map->title,sizeof(map->title),"%.127s",metadata.title[0]?metadata.title:"Untitled game");
    snprintf(map->product,sizeof(map->product),"%s",metadata.product);
    snprintf(map->region,sizeof(map->region),"%s",metadata.region);
    snprintf(map->bootfile,sizeof(map->bootfile),"%s",metadata.bootfile);
    log("Retail boot: %.100s; product=%s version=%s region=%s",map->title,metadata.product,
        metadata.version,metadata.region);
    for(unsigned i=0;i<image->count;i++)
        if(image->tracks[i].control==0 && image->tracks[i].start_lba>=45000) {
            log("Retail warning: CD audio playback is unsupported; the reader accepts audio commands silently, so that music is absent");
            break;
        }
    r=extent_crc(image,map->session_lba,KUI_RETAIL_IP_BYTES,&map->ip_crc32);
    if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
    r=kui_game_image_check(image,map->boot_lba,(map->boot_bytes+2047u)/2048u,KUI_GAME_SECTOR_MODE1);
    if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
    if(!close_reader(&files)) {problem="cannot close image reader";goto done;}
    const struct kui_volume volume=*kui_media_volume();
    if(!volume.count || (uint64_t)volume.start+volume.count>UINT32_MAX) {problem="invalid partition bounds";goto done;}
    map->partition_start=volume.start;map->partition_end=(uint64_t)volume.start+volume.count;
    map->card_sectors=map->partition_end; /* detached stage checks actual capacity */
    log("Retail boot: mapping %u tracks",image->count);
    for(unsigned i=0;i<image->count;i++) {
        if(!map_track(&files,&fs,&volume,&image->tracks[i],map,i)) {
            problem="track map failed: changed file, I/O, bounds or fragmentation limit (128 extents)";goto done;
        }
        log("Retail map T%02u: %u extents",image->tracks[i].number,map->tracks[i].extent_count);
    }
    r=kui_retail_manifest_encode(map,(uint8_t *)package->data+KUI_RETAIL_MAP_OFFSET);
    if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
    log("Retail boot prepared: %u tracks, %u extents; IP CRC32=%08x, %s=%u bytes checked at load",
        map->track_count,map->extent_count,map->ip_crc32,map->bootfile,map->boot_bytes);
    ok=true;
done:
    if(!close_reader(&files)) {ok=false;problem="cannot close image reader";}
    if(f_mount(NULL,"0:",0)!=FR_OK) {ok=false;problem="cannot release filesystem";}
    kui_sd_disconnect();free(gdi);free(image);free(map);
    if(stopped(&files)) {ok=false;problem="cancelled before retail handoff";}
    if(!ok) {kui_runtime_free(package);log("Retail boot: %s",problem);}
    return ok;
}
