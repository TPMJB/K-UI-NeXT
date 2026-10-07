/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_retail.h"
#include "kui/ce_load_plan.h"
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

_Static_assert(KUI_GAME_TRACK_MAX <= KUI_RETAIL_IMAGE_TRACKS, "a launch map holds every GDI's tracks");
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
    if(!kui_game_image_name_supported(slash+1)) return false;
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
/* ce: the Windows CE probe package, with its own magic and higher stage. */
static bool layout(const struct kui_runtime_image *image,bool ce) {
    const uint32_t stage=ce?KUI_RETAIL_CE_STAGE_ADDRESS:KUI_RETAIL_STAGE_ADDRESS;
    const uint32_t begin=KUI_RETAIL_STAGE_BLOB_OFFSET,max=KUI_RETAIL_STAGE_MAX_BYTES;
    if(!image->data || image->info.payload_bytes<begin+4 || image->info.payload_bytes>begin+max ||
        image->info.memory_bytes!=image->info.payload_bytes) return false;
    const uint8_t *h=(const uint8_t *)image->data+KUI_RETAIL_HEADER_OFFSET;
    uint32_t n=le32(h+28);
    const uint32_t resident=le32(h+52),limit=le32(h+56);
    bool known_resident=(resident==KUI_RETAIL_LEGACY_RESIDENT_ADDRESS &&
                         limit==KUI_RETAIL_LEGACY_STANDARD_LIMIT) ||
                        (!ce && resident==KUI_RETAIL_LOW_RESIDENT_ADDRESS &&
                         limit==KUI_RETAIL_LOW_STANDARD_LIMIT);
    if(memcmp(h,ce?KUI_RETAIL_CE_PACKAGE_MAGIC:KUI_RETAIL_PACKAGE_MAGIC,8) ||
        le32(h+8)!=KUI_RETAIL_PACKAGE_VERSION ||
        le32(h+12)!=KUI_RETAIL_HEADER_BYTES || le32(h+16)!=KUI_RETAIL_MAP_OFFSET ||
        le32(h+20)!=KUI_RETAIL_IMAGE_WIRE_BYTES || le32(h+24)!=stage ||
        !n || n%4 || n>max || begin+n!=image->info.payload_bytes ||
        le32(h+32)!=stage || le32(h+36)!=KUI_RETAIL_EXEC_ADDRESS ||
        le32(h+40)!=KUI_RETAIL_EXEC_MAX_BYTES || le32(h+44)!=KUI_RETAIL_STAGE_STACK ||
        le32(h+48)!=begin || !known_resident || le32(h+60)) return false;
    for(unsigned i=0;i<KUI_RETAIL_IMAGE_WIRE_BYTES;i++)
        if(((const uint8_t *)image->data)[KUI_RETAIL_MAP_OFFSET+i]) return false;
    return true;
}
/* FatFs fast-seek link map: the table size, then (cluster count, first
 * cluster) pairs ending in zero. Room for more runs than a map can hold, so
 * an over-fragmented track fails on the slot limit, not on this table. */
#define LINK_MAP_WORDS (2u+2u*(KUI_RETAIL_IMAGE_SLOTS+1u))
static DWORD link_map[LINK_MAP_WORDS]; /* One I/O worker prepares at a time. */
enum map_result {MAP_OK,MAP_FULL,MAP_FAILED};
/* Lists track index in map's slots and, when mapped, appends its file's
 * extents, using at most capacity slots in all. An audio track not mapped
 * keeps no extents (the readers refuse its sectors). */
static enum map_result map_track(struct files *files,FATFS *fs,const struct kui_volume *volume,
    const struct kui_game_image_track *track,struct kui_retail_manifest *map,unsigned index,
    uint32_t capacity,bool mapped) {
    struct kui_retail_track *t=&map->slots[index].track;
    *t=(struct kui_retail_track){.start_lba=track->start_lba,.end_lba=track->end_lba,
        .first_extent=(uint16_t)((map->track_count+map->extent_count) |
            ((track->file_offset & 255u) << 8)),
        .control=(uint8_t)(track->control |
            (track->sector_bytes==KUI_GAME_DATA_BYTES?KUI_RETAIL_TRACK_COOKED:0u) |
            (track->sector_mode==2 && track->sector_bytes!=KUI_GAME_DATA_BYTES?KUI_RETAIL_TRACK_MODE2:0u) |
            (track->sector_bytes==2336u?KUI_RETAIL_TRACK_2336:0u) |
            (track->sector_bytes==2448u?KUI_RETAIL_TRACK_2448:0u) |
            (track->file_offset & 256u?KUI_RETAIL_TRACK_OFFSET_HIGH:0u))};
    if(!mapped) return stopped(files)?MAP_FAILED:MAP_OK;
    char path[KUI_GAMES_FILE_CAP+3];FIL file;
    if(stopped(files) || !join(files,track->name,path) || f_open(&file,path,FA_READ)!=FR_OK) return MAP_FAILED;
    enum map_result result=f_size(&file)==track->file_bytes && fs->csize?MAP_OK:MAP_FAILED;
    uint64_t track_bytes=(uint64_t)(track->end_lba-track->start_lba)*track->sector_bytes;
    uint64_t skip=track->file_offset/512u;
    uint32_t total=(uint32_t)(((track->file_offset & 511u)+track_bytes+511u)/512u);
    /* The allocation table alone lists the file's contiguous cluster runs:
     * no track data is read (one data read per cluster took seconds). */
    link_map[0]=LINK_MAP_WORDS;file.cltbl=link_map;
    if(result==MAP_OK) {
        FRESULT seek=f_lseek(&file,CREATE_LINKMAP);
        result=seek==FR_NOT_ENOUGH_CORE?MAP_FULL:seek!=FR_OK || stopped(files)?MAP_FAILED:MAP_OK;
    }
    uint32_t block=0;
    for(const DWORD *run=link_map+1;result==MAP_OK && block<total;run+=2) {
        if(!run[0] || run[1]<2u || run[1]>=fs->n_fatent) {result=MAP_FAILED;break;}
        uint64_t sect=(uint64_t)fs->database+(uint64_t)(run[1]-2u)*fs->csize;
        uint64_t count=(uint64_t)run[0]*fs->csize;
        if(skip>=count) {skip-=count;continue;}
        sect+=skip;count-=skip;skip=0;
        if(count>total-block) count=total-block;
        if(sect<fs->database || sect>=volume->count || sect+count>volume->count) {result=MAP_FAILED;break;}
        uint32_t card=volume->start+(uint32_t)sect,used=map->track_count+map->extent_count;
        struct kui_retail_extent *last=t->extent_count?&map->slots[used-1].extent:NULL;
        if(last && (uint64_t)last->card_lba+last->blocks==card) last->blocks+=(uint32_t)count;
        else {
            if(used>=capacity || t->extent_count==UINT8_MAX) {result=MAP_FULL;break;}
            map->slots[used].extent=(struct kui_retail_extent){block,card,(uint32_t)count};
            ++map->extent_count;++t->extent_count;
        }
        block+=(uint32_t)count;
    }
    file.cltbl=NULL;
    if(f_close(&file)!=FR_OK) result=MAP_FAILED;
    return result;
}
/* Maps every track's file within capacity slots when they fit; otherwise
 * lists audio tracks without their files: K-UI plays no disc audio, and
 * games read audio tracks only to play them. *audio says which. */
static enum map_result map_tracks(struct files *files,FATFS *fs,const struct kui_volume *volume,
    const struct kui_game_image *image,struct kui_retail_manifest *map,uint32_t capacity,bool *audio) {
    enum map_result result=MAP_FULL;*audio=false;
    if(image->count>capacity) return MAP_FULL;
    for(int with_audio=1;with_audio>=0 && result==MAP_FULL;--with_audio) {
        memset(map->slots,0,sizeof(map->slots));map->extent_count=0;result=MAP_OK;
        for(unsigned i=0;i<image->count && result==MAP_OK;i++)
            result=map_track(files,fs,volume,&image->tracks[i],map,i,capacity,
                with_audio || image->tracks[i].control);
        *audio=with_audio;
    }
    return result;
}
/* Hash exact logical bytes. Raw boot tracks retain their address checks in
 * the stage; a cooked boot has no sector headers, so its expected executable
 * CRC is prepared here and checked against the detached physical read. */
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
    return kui_games_retail_prepare_reader(path,KUI_RETAIL_READER_STANDARD,package,log,cancel);
}
bool kui_games_retail_prepare_reader(const char *path,uint32_t reader,
    struct kui_runtime_image *package,kui_log_fn log,kui_cancel_fn cancel) {
    if(!package) return false;
    *package=(struct kui_runtime_image){0};
    if(!log || !cancel) return false;
    const bool ce=(reader&KUI_GAMES_RETAIL_CE_PROBE)!=0;
    const bool descramble=(reader&KUI_GAMES_RETAIL_DESCRAMBLE)!=0;
    const bool plain=(reader&KUI_GAMES_RETAIL_BOOT_PLAIN)!=0;
    reader&=~(KUI_GAMES_RETAIL_CE_PROBE | KUI_GAMES_RETAIL_DESCRAMBLE | KUI_GAMES_RETAIL_BOOT_PLAIN);
    if((ce && (descramble || plain)) || (descramble && plain)) {
        log("Retail boot: invalid boot encoding choice");return false;
    }
    struct files files={.cancel=cancel};char name[KUI_GAME_NAME_CAP];
    if(!split(path,&files,name) || stopped(&files)) {log("Retail boot: invalid path or cancelled");return false;}
    if(!kui_sd_connect()) {log("Retail boot: storage unavailable");return false;}
    FATFS fs;bool ok=false;const char *problem="cannot mount storage";
    struct kui_retail_manifest *map=NULL;struct kui_game_image *image=NULL;uint8_t *gdi=NULL;
    if(!kui_mount(&fs,log)) goto done;
    enum kui_runtime_result rr=kui_runtime_read(ce?KUI_GAMES_RETAIL_CE_PACKAGE:KUI_GAMES_RETAIL_PACKAGE,
        package,log,cancel);
    if(rr!=KUI_RUNTIME_OK) {problem=kui_runtime_result_name(rr);goto done;}
    if(!layout(package,ce)) {
        problem=ce?"unsupported Windows CE probe package layout":"unsupported retail-boot package layout";
        goto done;
    }
    uint64_t size=0;
    if(stat_file(&files,name,&size)!=KUI_GAME_OK || !size) {
        problem="selected image missing or unreadable";goto done;
    }
    size_t fingerprint_bytes=size>KUI_GAME_GDI_LIMIT?KUI_GAME_GDI_LIMIT:(size_t)size;
    map=calloc(1,sizeof(*map));image=malloc(sizeof(*image));gdi=malloc(fingerprint_bytes);
    if(!map || !image || !gdi) {problem="insufficient memory to prepare selected image";goto done;}
    if(read_file(&files,name,0,gdi,fingerprint_bytes)!=KUI_GAME_OK) {problem="cannot read image header";goto done;}
    struct kui_game_file_ops file_ops={&files,stat_file,read_file};
    enum kui_game_result r=kui_game_image_open_named(name,&file_ops,image);
    if(r!=KUI_GAME_OK) {
        problem=r==KUI_GAME_UNSUPPORTED?
            "unsupported image layout; compressed images need the import tool":kui_game_result_name(r);
        goto done;
    }
    map->storage_transport=kui_storage_active();
    if(map->storage_transport>KUI_STORAGE_IDE) {problem="storage transport not selected";goto done;}
    log("Retail boot storage: %s",map->storage_transport==KUI_STORAGE_SCIF?"SCIF microSD":
        map->storage_transport==KUI_STORAGE_SCI?"SCI microSD":"IDE / CF");
    map->track_count=image->count;map->gdi_crc32=kui_retail_crc32(0,gdi,fingerprint_bytes);
    map->session_lba=image->data_lba;
    if(image->cd_image)
        map->flags|=KUI_RETAIL_IMAGE_CD;
    log("Retail image: %s, session LBA %u",kui_game_image_format_name(image->format),map->session_lba);
    struct kui_game_metadata metadata;struct kui_game_metadata_ops metadata_ops={image,metadata_read,metadata_range};
    enum kui_game_metadata_status ms=kui_game_metadata_read(&metadata_ops,map->session_lba,&metadata);
    if(ms!=KUI_GAME_METADATA_OK) {problem=kui_game_metadata_status_text(ms);goto done;}
    /* The owner's IP/ISO metadata selects the executable. Titles and boot
     * filenames are not compatibility gates; native GD bytes stay verbatim. */
    if(ce) {
        /* The CE boot test: the boot file is a 2048-byte load prefix
         * followed by the kernel body. The stage checks the rest. */
        if(!metadata.windows_ce) {problem="the Windows CE boot test needs a Windows CE image";goto done;}
        /* Only the SCI resident is built CE-safe (see Makefile.dc). */
        if(map->storage_transport!=KUI_STORAGE_SCI) {problem="the Windows CE boot test needs SCI microSD";goto done;}
        if(metadata.boot_bytes<=KUI_CE_LOAD_PREFIX_BYTES) {
            problem="Windows CE boot file must be larger than its 2048-byte prefix";goto done;
        }
    } else {
        if(metadata.windows_ce) {problem="Windows CE game launching is not supported";goto done;}
        if(!metadata.native_gd && !metadata.native_cd) {problem="valid native IP peripheral flags required";goto done;}
        if((image->scrambled && !plain) || descramble) {
            if(!(map->flags & KUI_RETAIL_IMAGE_CD)) {problem="descrambling requires a CD image";goto done;}
            map->flags|=KUI_RETAIL_IMAGE_SCRAMBLED;
        }
    }
    if(metadata.boot_bytes<KUI_RETAIL_TRAMPOLINE_BYTES ||
        metadata.boot_bytes>KUI_RETAIL_EXEC_MAX_BYTES ||
        metadata.boot_bytes>KUI_RETAIL_IMAGE_BOOT_MAX) {
        problem="boot executable must be 128 bytes to 12 MiB";goto done;
    }
    if(metadata.boot_lba<map->session_lba+16u ||
        kui_game_image_check(image,map->session_lba,16,KUI_GAME_SECTOR_MODE1)!=KUI_GAME_OK) {
        problem="boot executable and full IP must be in the selected data session";goto done;
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
    const uint32_t boot_end=map->boot_lba+(map->boot_bytes+2047u)/2048u;
    bool cooked_boot=false;
    for(unsigned i=0;i<image->count;i++)
        if(image->tracks[i].sector_bytes==KUI_GAME_DATA_BYTES &&
            image->tracks[i].start_lba<boot_end && image->tracks[i].end_lba>map->boot_lba)
            cooked_boot=true;
    if(cooked_boot || image->format!=KUI_GAME_IMAGE_GDI) {
        map->flags|=KUI_RETAIL_IMAGE_BOOT_CRC;
        log("Retail boot: checking exact executable CRC before detached launch");
        r=extent_crc(image,map->boot_lba,map->boot_bytes,&map->boot_crc32);
        if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
        log("Retail boot: executable CRC32=%08x",map->boot_crc32);
    }
    if(!close_reader(&files)) {problem="cannot close image reader";goto done;}
    const struct kui_volume volume=*kui_media_volume();
    if(!volume.count || (uint64_t)volume.start+volume.count>UINT32_MAX) {problem="invalid partition bounds";goto done;}
    map->partition_start=volume.start;map->partition_end=(uint64_t)volume.start+volume.count;
    map->card_sectors=map->partition_end; /* detached stage checks actual capacity */
    log("Retail boot: mapping %u tracks",image->count);
    bool background=reader!=KUI_RETAIL_READER_STANDARD,audio=true;
    if(background && map->storage_transport!=KUI_STORAGE_SCI) {
        log("Retail boot: the background reader needs SCI microSD; using the standard reader");
        background=false;
    }
    enum map_result mapped=MAP_FULL;
    if(background) {
        mapped=map_tracks(&files,&fs,&volume,image,map,KUI_RETAIL_ASYNC_SLOTS,&audio);
        if(mapped==MAP_FULL) {
            if(ce) {problem="Windows CE background map exceeds 64 slots; defragment or use cooked tracks";goto done;}
            log("Retail boot: the map needs more than the background reader's %u slots; using the standard reader",
                KUI_RETAIL_ASYNC_SLOTS);
            background=false;
        }
    }
    if(!background && mapped!=MAP_FAILED) mapped=map_tracks(&files,&fs,&volume,image,map,KUI_RETAIL_IMAGE_SLOTS,&audio);
    if(mapped!=MAP_OK) {
        problem=mapped==MAP_FULL?"track files too fragmented for the launch map (160 slots)":
            "track map failed: changed file, I/O or bounds";
        goto done;
    }
    for(unsigned i=0;i<image->count;i++) if(map->slots[i].track.extent_count)
        log("Retail map T%02u: %u-byte sectors, %u extents",image->tracks[i].number,
            image->tracks[i].sector_bytes,map->slots[i].track.extent_count);
    if(!audio)
        log("Retail boot: audio tracks listed without their files to fit %u tracks; their sectors cannot be read",
            image->count);
    if(background) {
        map->reader=reader;
        if(ce) log("Retail boot reader: background SCI stream (test, Windows CE interrupts)");
        else log("Retail boot reader: background SCI stream (test, %s)",
            reader==KUI_RETAIL_READER_ASYNC_EAGER?"25 blocks per call":"20 blocks per call");
    }
    r=kui_retail_manifest_encode(map,(uint8_t *)package->data+KUI_RETAIL_MAP_OFFSET);
    if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
    log("%s prepared: %u tracks, %u extents; IP CRC32=%08x, %s=%u bytes checked at load",
        ce?"Windows CE boot test":"Retail boot",
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
