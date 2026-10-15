/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/games_retail.h"
#include "kui/game_launch_report.h"
#include "kui/game_launch_settings.h"
#include "kui/version.h"
#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "unknown"
#endif
#include "kui/ce_load_plan.h"
#include "kui/armada_ce_excerpt_pool.h"
#include "kui/armada_ce_stream_seed.h"
#include "kui/games.h"
#include "kui/game_metadata.h"
#include "kui/retail_image.h"
#include "kui/retail_loader_layout.h"
#include "kui/media.h"
#include "../core/games_cdda_profile.h"
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
                         (limit==KUI_RETAIL_LOW_STANDARD_LIMIT ||
                          limit==KUI_MDK2_CDDA_LOW_LIMIT));
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
#define LEGACY_MAP_SLOTS 160u
#define LINK_MAP_WORDS (2u+2u*(LEGACY_MAP_SLOTS+1u))
#define CDDA_LINK_MAP_MAX_WORDS (2u+2u*8192u)
static DWORD link_map[LINK_MAP_WORDS]; /* One I/O worker prepares at a time. */
enum map_result {MAP_OK,MAP_FULL,MAP_FAILED};
enum map_reason {MAP_REASON_NONE,MAP_REASON_FILE,MAP_REASON_CANCEL,
    MAP_REASON_LINK_LIMIT,MAP_REASON_MEMORY,MAP_REASON_CHAIN,
    MAP_REASON_BOUNDS,MAP_REASON_CLOSE};
struct map_stats {
    uint32_t required_extents,required_slots,max_track_extents,largest_track,bad_track;
    uint32_t link_map_words,max_link_map_words;
    enum map_reason reason;
    bool complete;
};
static void map_failure(struct map_stats *stats,unsigned index,enum map_reason reason) {
    if(!stats) return;
    stats->complete=false;
    if(!stats->bad_track) {stats->bad_track=index+1u;stats->reason=reason;}
}
static const char *map_reason_name(enum map_reason reason) {
    switch(reason) {
    case MAP_REASON_FILE:return "missing or changed file";
    case MAP_REASON_CANCEL:return "cancelled";
    case MAP_REASON_LINK_LIMIT:return "allocation chain exceeds the bounded link table";
    case MAP_REASON_MEMORY:return "insufficient memory for allocation chain";
    case MAP_REASON_CHAIN:return "allocation-chain lookup failed";
    case MAP_REASON_BOUNDS:return "allocation chain outside file or card bounds";
    case MAP_REASON_CLOSE:return "cannot close mapped file";
    default:return "mapping capacity";
    }
}
/* Lists track index in map's slots and appends normalized physical runs.
 * A CDDA caller supplies stats to finish counting after its table fills;
 * ordinary callers retain the prior early refusal and audio fallback. */
static enum map_result map_track(struct files *files,FATFS *fs,const struct kui_volume *volume,
    const struct kui_game_image_track *track,struct kui_retail_manifest *map,unsigned index,
    uint32_t capacity,bool mapped,struct map_stats *stats) {
    uint32_t used=map->track_count+map->extent_count;
    struct kui_retail_track *t=&map->slots[index].track;
    *t=(struct kui_retail_track){.start_lba=track->start_lba,.end_lba=track->end_lba,
        .first_extent=(uint16_t)((used<256u?used:0u) |
            ((track->file_offset & 255u) << 8)),
        .control=(uint8_t)(track->control |
            (track->sector_bytes==KUI_GAME_DATA_BYTES?KUI_RETAIL_TRACK_COOKED:0u) |
            (track->sector_mode==2 && track->sector_bytes!=KUI_GAME_DATA_BYTES?KUI_RETAIL_TRACK_MODE2:0u) |
            (track->sector_bytes==2336u?KUI_RETAIL_TRACK_2336:0u) |
            (track->sector_bytes==2448u?KUI_RETAIL_TRACK_2448:0u) |
            (track->file_offset & 256u?KUI_RETAIL_TRACK_OFFSET_HIGH:0u))};
    if(!mapped) return stopped(files)?MAP_FAILED:MAP_OK;
    char path[KUI_GAMES_FILE_CAP+3];FIL file;
    if(stopped(files)) {map_failure(stats,index,MAP_REASON_CANCEL);return MAP_FAILED;}
    if(!join(files,track->name,path) || f_open(&file,path,FA_READ)!=FR_OK) {
        map_failure(stats,index,MAP_REASON_FILE);return MAP_FAILED;
    }
    enum map_result result=f_size(&file)==track->file_bytes && fs->csize?MAP_OK:MAP_FAILED;
    if(result==MAP_FAILED) map_failure(stats,index,MAP_REASON_FILE);
    uint64_t track_bytes=(uint64_t)(track->end_lba-track->start_lba)*track->sector_bytes;
    uint64_t skip=track->file_offset/512u;
    uint64_t total64=((track->file_offset & 511u)+track_bytes+511u)/512u;
    if(total64>UINT32_MAX) {result=MAP_FAILED;map_failure(stats,index,MAP_REASON_BOUNDS);}
    uint32_t total=(uint32_t)total64;
    DWORD *table=link_map,*allocated=NULL;
    uint32_t table_words=LINK_MAP_WORDS;
    table[0]=table_words;file.cltbl=table;
    if(result==MAP_OK) {
        FRESULT seek=f_lseek(&file,CREATE_LINKMAP);
        uint32_t required=(seek==FR_OK || seek==FR_NOT_ENOUGH_CORE)?table[0]:0u;
        if(stats) {
            stats->link_map_words=required;
            if(required>stats->max_link_map_words) stats->max_link_map_words=required;
        }
        /* FatFs reports the exact required word count even when its first
         * table is too small. Only CDDA expands, to a finite read-only heap
         * allocation; no track payload or FAT allocation is modified. */
        if(seek==FR_NOT_ENOUGH_CORE && stats && !stopped(files)) {
            if(required<4u || (required&1u) || required>CDDA_LINK_MAP_MAX_WORDS) {
                map_failure(stats,index,MAP_REASON_LINK_LIMIT);
            } else if(!(allocated=malloc((size_t)required*sizeof(*allocated)))) {
                map_failure(stats,index,MAP_REASON_MEMORY);
            } else {
                table=allocated;table_words=required;table[0]=required;file.cltbl=table;
                seek=f_lseek(&file,CREATE_LINKMAP);
                if(seek==FR_OK || seek==FR_NOT_ENOUGH_CORE) {
                    if(table[0]>stats->max_link_map_words) stats->max_link_map_words=table[0];
                    stats->link_map_words=table[0];
                }
            }
        }
        if(stopped(files)) {result=MAP_FAILED;map_failure(stats,index,MAP_REASON_CANCEL);}
        else if(seek==FR_NOT_ENOUGH_CORE) {
            result=stats?MAP_FAILED:MAP_FULL;
            map_failure(stats,index,allocated?MAP_REASON_CHAIN:MAP_REASON_LINK_LIMIT);
        } else if(seek!=FR_OK) {result=MAP_FAILED;map_failure(stats,index,MAP_REASON_CHAIN);}
        else if(table[0]<2u || (table[0]&1u) || table[0]>table_words) {
            result=MAP_FAILED;map_failure(stats,index,MAP_REASON_CHAIN);
        }
    }
    uint32_t block=0,normal_runs=0,last_stored=UINT32_MAX;
    uint64_t last_end=0;
    bool store=true;
    const DWORD *run=table+1,*table_end=table+table_words;
    while((result==MAP_OK || (stats && result==MAP_FULL)) && block<total) {
        if(run+1>=table_end || !run[0] || run[1]<2u || run[1]>=fs->n_fatent) {
            result=MAP_FAILED;map_failure(stats,index,MAP_REASON_CHAIN);break;
        }
        uint64_t sect=(uint64_t)fs->database+(uint64_t)(run[1]-2u)*fs->csize;
        uint64_t count=(uint64_t)run[0]*fs->csize;
        run+=2;
        if(skip>=count) {skip-=count;continue;}
        sect+=skip;count-=skip;skip=0;
        if(count>total-block) count=total-block;
        if(sect<fs->database || sect>=volume->count || sect+count>volume->count ||
           (uint64_t)volume->start+sect>UINT32_MAX ||
           (uint64_t)volume->start+sect+count>UINT64_C(0x100000000)) {
            result=MAP_FAILED;map_failure(stats,index,MAP_REASON_BOUNDS);break;
        }
        uint32_t card=volume->start+(uint32_t)sect;
        bool adjacent=normal_runs && last_end==card;
        if(!adjacent) ++normal_runs;
        if(store) {
            used=map->track_count+map->extent_count;
            if(adjacent && last_stored!=UINT32_MAX) map->slots[last_stored].extent.blocks+=(uint32_t)count;
            else if(used>=capacity || used>=KUI_RETAIL_MANIFEST_SLOTS || used>=256u ||
                    t->extent_count==UINT8_MAX) {
                result=MAP_FULL;store=false;
                if(!stats) break;
            } else {
                map->slots[used].extent=(struct kui_retail_extent){block,card,(uint32_t)count};
                last_stored=used;++map->extent_count;++t->extent_count;
            }
        }
        last_end=(uint64_t)card+count;
        block+=(uint32_t)count;
        if(stopped(files)) {result=MAP_FAILED;map_failure(stats,index,MAP_REASON_CANCEL);break;}
    }
    if(stats) {
        stats->required_extents+=normal_runs;
        stats->required_slots=map->track_count+stats->required_extents;
        if(normal_runs>stats->max_track_extents) {
            stats->max_track_extents=normal_runs;stats->largest_track=index+1u;
        }
    }
    file.cltbl=NULL;
    if(f_close(&file)!=FR_OK) {result=MAP_FAILED;map_failure(stats,index,MAP_REASON_CLOSE);}
    free(allocated);
    return result;
}
static enum map_result map_cdda_tracks(struct files *files,FATFS *fs,const struct kui_volume *volume,
    const struct kui_game_image *image,struct kui_retail_manifest *map,uint32_t capacity,
    struct map_stats *stats) {
    *stats=(struct map_stats){.required_slots=image->count,.complete=true};
    memset(map->slots,0,sizeof(map->slots));map->extent_count=0;
    enum map_result result=image->count>capacity?MAP_FULL:MAP_OK;
    for(unsigned i=0;i<image->count;i++) {
        enum map_result one=map_track(files,fs,volume,&image->tracks[i],map,i,capacity,true,stats);
        if(one==MAP_FAILED) return MAP_FAILED;
        if(one==MAP_FULL) result=MAP_FULL;
    }
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
                with_audio || image->tracks[i].control,NULL);
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
static enum kui_wince_cdda_profile ce_music_profile(const struct kui_game_image *image,
        const struct kui_game_metadata *metadata) {
    if(image->format!=KUI_GAME_IMAGE_GDI || image->cd_image || image->scrambled ||
       !metadata->windows_ce || strcmp(metadata->bootfile,"0WINCEOS.BIN")) return KUI_WINCE_CDDA_NONE;
    for(enum kui_wince_cdda_profile profile=KUI_WINCE_CDDA_ARMADA;
            profile<=KUI_WINCE_CDDA_WORMS_ARMAGEDDON;profile++) {
        const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(profile);
        if(image->count!=disc->track_count || metadata->boot_bytes!=disc->kernel_bytes ||
           (profile!=KUI_WINCE_CDDA_ARMADA && metadata->boot_lba!=disc->boot_lba) ||
           strcmp(metadata->product,disc->product)) continue;
        unsigned i=0;
        for(;i<disc->track_count;i++)
            if(image->tracks[i].start_lba!=disc->starts[i] || image->tracks[i].file_offset ||
               (image->tracks[i].sector_bytes!=2352u &&
                !(image->tracks[i].control==4u && image->tracks[i].sector_bytes==2048u &&
                  !image->tracks[i].data_offset)) ||
               image->tracks[i].control!=kui_wince_cdda_track_control(profile,i)) break;
        if(i==disc->track_count && (profile!=KUI_WINCE_CDDA_ARMADA ||
           image->tracks[3].end_lba>=KUI_ARMADA_CE_EXCERPT_START_LBA+KUI_ARMADA_CE_EXCERPT_SECTORS))
            return profile;
    }
    return KUI_WINCE_CDDA_NONE;
}
static bool armada_excerpt_seed(struct kui_runtime_image *package,
        const struct kui_game_image *image,kui_log_fn log) {
    uint8_t *data=package->data;size_t found=SIZE_MAX;
    const size_t bytes=KUI_ARMADA_CE_EXCERPT_SEED_BYTES+KUI_ARMADA_CE_EXCERPT_PCM_BYTES;
    /* Locate the one typed, initially empty seed in this trusted package.
     * Do not confuse an instruction literal with a writable PCM seed. */
    for(size_t at=KUI_RETAIL_STAGE_BLOB_OFFSET;at<=package->info.payload_bytes &&
            bytes<=package->info.payload_bytes-at;at+=4u) {
        const uint32_t *words=(const uint32_t *)(const void *)(data+at);
        if(!kui_armada_ce_excerpt_seed_valid(words) || words[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]) continue;
        if(found!=SIZE_MAX) return false;
        found=at;
    }
    if(found==SIZE_MAX) return false;
    uint8_t *pcm=data+found+KUI_ARMADA_CE_EXCERPT_SEED_BYTES;
    for(size_t i=0;i<KUI_ARMADA_CE_EXCERPT_PCM_BYTES;i++) if(pcm[i]) return false;
    enum kui_game_result result=kui_game_image_read(image,KUI_ARMADA_CE_EXCERPT_START_LBA,
        KUI_ARMADA_CE_EXCERPT_SECTORS,KUI_GAME_SECTOR_RAW,pcm,KUI_ARMADA_CE_EXCERPT_PCM_BYTES);
    if(result!=KUI_GAME_OK) {log("CE music excerpt read failed: %s",kui_game_result_name(result));return false;}
    uint32_t *seed=(uint32_t *)(void *)(data+found);
    seed[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]=kui_armada_ce_excerpt_crc32(pcm,KUI_ARMADA_CE_EXCERPT_PCM_BYTES);
    log("CE music test: preloaded seconds 2..4 of track 4 (%u bytes), CRC32=%08x",
        KUI_ARMADA_CE_EXCERPT_PCM_BYTES,seed[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]);
    return true;
}
/* A unique typed blank CEX2 seed identifies the full-stream package. A
 * populated or malformed template cannot silently use the excerpt route. */
static size_t armada_stream_seed_find(const struct kui_runtime_image *package,
        enum kui_wince_cdda_profile profile) {
    size_t found=SIZE_MAX;
    if(!package || !package->data) return SIZE_MAX;
    const uint8_t *data=package->data;
    for(size_t at=KUI_RETAIL_STAGE_BLOB_OFFSET;at<=package->info.payload_bytes &&
            sizeof(struct kui_armada_ce_stream_seed)<=package->info.payload_bytes-at;at+=4u) {
        const uint32_t *words=(const uint32_t *)(const void *)(data+at);
        if(!kui_wince_cdda_seed_header_valid(words,profile)) continue;
        if(found!=SIZE_MAX) return SIZE_MAX;
        found=at;
    }
    return found;
}
static bool armada_stream_seed_fill(struct kui_runtime_image *package,size_t at,
        const struct kui_retail_manifest *map,enum kui_wince_cdda_profile profile,kui_log_fn log) {
    if(at==SIZE_MAX || at>package->info.payload_bytes ||
       sizeof(struct kui_armada_ce_stream_seed)>package->info.payload_bytes-at ||
       !kui_wince_cdda_manifest_valid(map,profile)) return false;
    struct kui_armada_ce_stream_seed *seed=(void *)((uint8_t *)package->data+at);
    if(!kui_wince_cdda_seed_header_valid(seed->words,profile) ||
       seed->words[KUI_ARMADA_CE_STREAM_MAP_CRC_WORD]) return false;
    for(unsigned i=0;i<sizeof(seed->wire);i++) if(seed->wire[i]) return false;
    if(kui_retail_manifest_encode(map,seed->wire)!=KUI_GAME_OK) return false;
    seed->words[KUI_ARMADA_CE_STREAM_MAP_CRC_WORD]=le32(seed->wire+16u);
    log("CE music: complete source map, %u tracks, %u extents, CRC32=%08x",
        map->track_count,map->extent_count,seed->words[KUI_ARMADA_CE_STREAM_MAP_CRC_WORD]);
    return true;
}
static bool fill_ram_patches(struct kui_runtime_image *package,
        const struct kui_game_ram_patches *patches,uint32_t exec_bytes) {
    if(!kui_game_ram_patches_enabled(patches)) return true;
    size_t found=SIZE_MAX;
    for(size_t at=KUI_RETAIL_STAGE_BLOB_OFFSET;
        at<=package->info.payload_bytes && KUI_GAME_RAM_PATCH_SEED_BYTES<=package->info.payload_bytes-at;at+=4u) {
        uint8_t *seed=(uint8_t *)package->data+at;
        /* SH literal pools can contain adjacent magic comparison words.
         * Select the complete typed header, not an eight-byte coincidence. */
        if(le32(seed)!=KUI_GAME_RAM_PATCH_SEED_MAGIC0 || le32(seed+4)!=KUI_GAME_RAM_PATCH_SEED_MAGIC1 ||
           le32(seed+8)!=KUI_GAME_RAM_PATCH_SEED_VERSION || le32(seed+12)!=KUI_GAME_RAM_PATCH_SEED_BYTES) continue;
        if(found!=SIZE_MAX || !kui_game_ram_patch_seed_blank(seed,KUI_GAME_RAM_PATCH_SEED_BYTES)) return false;
        found=at;
    }
    return found!=SIZE_MAX && kui_game_ram_patch_seed_encode((uint8_t *)package->data+found,
        KUI_GAME_RAM_PATCH_SEED_BYTES,patches,exec_bytes);
}
static bool prepare_launch(const char *path,uint32_t reader,const struct kui_game_ram_patches *patches,
    bool reporting,struct kui_runtime_image *package,kui_log_fn log,kui_cancel_fn cancel) {
    if(!package) return false;
    *package=(struct kui_runtime_image){0};
    if(!log || !cancel) return false;
    const struct kui_game_ram_patches off={0};
    if(!patches) patches=&off;
    if(!kui_game_ram_patches_valid(patches)) {log("RAM patches: invalid address or duplicate target");return false;}
    const bool patched=kui_game_ram_patches_enabled(patches);
    struct kui_game_ram_patches applied_patches={0};
    const bool ce=(reader&KUI_GAMES_RETAIL_CE_PROBE)!=0;
    const bool descramble=(reader&KUI_GAMES_RETAIL_DESCRAMBLE)!=0;
    const bool plain=(reader&KUI_GAMES_RETAIL_BOOT_PLAIN)!=0;
    const bool cdda=(reader&KUI_GAMES_RETAIL_CDDA)!=0;
    const bool ce_excerpt=(reader&KUI_GAMES_RETAIL_CE_EXCERPT)!=0;
    if(patched && (ce || cdda || ce_excerpt)) {
        log("RAM patches require a native game with CD music Off");return false;
    }
    reader&=~(KUI_GAMES_RETAIL_CE_PROBE | KUI_GAMES_RETAIL_DESCRAMBLE |
        KUI_GAMES_RETAIL_BOOT_PLAIN | KUI_GAMES_RETAIL_CDDA | KUI_GAMES_RETAIL_CE_EXCERPT);
    if(ce_excerpt && (!ce || cdda || descramble || plain || reader!=KUI_RETAIL_READER_ASYNC)) {
        log("CE CD music requires its separate background profile");return false;
    }
    if(cdda && (ce || descramble || plain || reader!=KUI_RETAIL_READER_STANDARD)) {
        log("CDDA launch: unsupported profile; use native GDI, standard reader and original boot encoding");
        return false;
    }
    if((ce && (descramble || plain)) || (descramble && plain)) {
        log("Retail boot: invalid boot encoding choice");return false;
    }
    struct files files={.cancel=cancel};char name[KUI_GAME_NAME_CAP];
    if(!split(path,&files,name) || stopped(&files)) {log("Retail boot: invalid path or cancelled");return false;}
    if(!kui_sd_connect()) {log("Retail boot: storage unavailable");return false;}
    FATFS fs;bool ok=false;const char *problem="cannot mount storage";
    struct kui_retail_manifest *map=NULL;struct kui_game_image *image=NULL;uint8_t *gdi=NULL;
    if(!kui_mount(&fs,log)) goto done;
    enum kui_wince_cdda_profile music_profile=KUI_WINCE_CDDA_NONE;
    const char *launch_package=ce?KUI_GAMES_RETAIL_CE_PACKAGE:KUI_GAMES_RETAIL_PACKAGE;
    enum kui_runtime_result rr=KUI_RUNTIME_OK;
    if(!cdda && !ce_excerpt) {
        rr=kui_runtime_read(launch_package,package,log,cancel);
        if(rr!=KUI_RUNTIME_OK) {problem=kui_runtime_result_name(rr);goto done;}
        if(!layout(package,ce)) {problem="unsupported game-launch package layout";goto done;}
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
    if(cdda && (metadata.windows_ce || !metadata.native_gd ||
        image->format!=KUI_GAME_IMAGE_GDI || image->cd_image || image->scrambled ||
        map->storage_transport!=KUI_STORAGE_SCI)) {
        problem="unsupported CDDA profile; supported SCI/native GDI required";goto done;
    }
    if(ce_excerpt) {
        music_profile=ce_music_profile(image,&metadata);
        if(music_profile==KUI_WINCE_CDDA_NONE || map->storage_transport!=KUI_STORAGE_SCI) {
            problem="CD music has no compatible WinCE runtime adapter for this image; turn CD music Off to launch";goto done;
        }
        launch_package=music_profile==KUI_WINCE_CDDA_WORMS_ARMAGEDDON?
            KUI_GAMES_RETAIL_CE_WORMS_PACKAGE:music_profile==KUI_WINCE_CDDA_BUST_A_MOVE_4?
            KUI_GAMES_RETAIL_CE_BAM4_PACKAGE:KUI_GAMES_RETAIL_CE_EXCERPT_PACKAGE;
        log("CE CD music: %s profile",kui_wince_cdda_disc(music_profile)->name);
        if(!close_reader(&files)) {problem="cannot close metadata reader";goto done;}
        rr=kui_runtime_read(launch_package,package,log,cancel);
        if(rr!=KUI_RUNTIME_OK) {problem=kui_runtime_result_name(rr);goto done;}
        if(!layout(package,ce)) {
            problem="unsupported CE CD music package layout";
            log("Replace %s with the selected game-launch package",launch_package+2);goto done;
        }
    }
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
    uint32_t patch_offsets[KUI_GAME_RAM_PATCHES_COUNT];
    if(!kui_game_ram_patches_plan(patches,map->boot_bytes,patch_offsets)) {
        problem="RAM patch target lies outside the selected boot executable";goto done;
    }
    for(unsigned i=0;i<KUI_GAME_RAM_PATCHES_COUNT;i++) if(patches->entries[i].enabled)
        applied_patches.entries[i]=(struct kui_game_ram_patch){
            KUI_GAME_RAM_PATCH_EXEC_ADDRESS+patch_offsets[i],patches->entries[i].value,true};
    snprintf(map->title,sizeof(map->title),"%.127s",metadata.title[0]?metadata.title:"Untitled game");
    snprintf(map->product,sizeof(map->product),"%s",metadata.product);
    snprintf(map->region,sizeof(map->region),"%s",metadata.region);
    snprintf(map->bootfile,sizeof(map->bootfile),"%s",metadata.bootfile);
    log("Retail boot: %.100s; product=%s version=%s region=%s",map->title,metadata.product,
        metadata.version,metadata.region);
    struct kui_game_audio_info audio_info;
    r=kui_game_image_audio_info(image,&audio_info);
    if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
    if(cdda && (audio_info.classification!=KUI_GAME_AUDIO_GD_HIGH_DENSITY ||
        !audio_info.candidate_tracks || audio_info.candidate_tracks>KUI_GAME_TRACK_MAX)) {
        problem="unsupported CDDA profile; no known playable high-density audio tracks";goto done;
    }
    if(!cdda && !ce_excerpt && audio_info.candidate_tracks)
        log("Retail warning: CD audio playback is unsupported (%u candidate tracks); commands are acknowledged silently, so music using them is absent",
            audio_info.candidate_tracks);
    else if(!cdda && !ce_excerpt && audio_info.classification==KUI_GAME_AUDIO_UNKNOWN)
        log("Retail audio: bare ISO/raw image has no original audio-track inventory; CD audio playback remains unsupported");
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
    if(reporting || patched || cdda || cooked_boot || image->format!=KUI_GAME_IMAGE_GDI) {
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
    const unsigned cdda_capacity=kui_games_cdda_mapping_capacity(image,map);
    struct map_stats cdda_stats={0};
    if(cdda) {
        /* CDDA requires every audio file. Capacity belongs to the selected
         * executable adapter; the Toy payload still has its original limit. */
        log("CDDA map capacity: %u slots (%u available for file extents)",
            cdda_capacity,cdda_capacity>image->count?cdda_capacity-image->count:0u);
        mapped=map_cdda_tracks(&files,&fs,&volume,image,map,cdda_capacity,&cdda_stats);
        if(cdda_stats.complete)
            log("CDDA complete map: %u tracks need %u file extents, %u slots; largest T%02u has %u extents",
                image->count,cdda_stats.required_extents,cdda_stats.required_slots,
                cdda_stats.largest_track,cdda_stats.max_track_extents);
        else
            log("CDDA mapping incomplete at T%02u: %s; CLMT reported %u words (limit %u), %u extents counted",
                cdda_stats.bad_track,map_reason_name(cdda_stats.reason),
                cdda_stats.link_map_words,CDDA_LINK_MAP_MAX_WORDS,cdda_stats.required_extents);
    } else if(background) {
        mapped=map_tracks(&files,&fs,&volume,image,map,KUI_RETAIL_ASYNC_SLOTS,&audio);
        if(mapped==MAP_FULL) {
            if(ce) {problem="Windows CE background map exceeds 64 slots; defragment or use cooked tracks";goto done;}
            log("Retail boot: the map needs more than the background reader's %u slots; using the standard reader",
                KUI_RETAIL_ASYNC_SLOTS);
            background=false;
        }
    }
    if(!cdda && !background && mapped!=MAP_FAILED)
        mapped=map_tracks(&files,&fs,&volume,image,map,LEGACY_MAP_SLOTS,&audio);
    if(mapped!=MAP_OK) {
        if(cdda && mapped==MAP_FULL) {
            log("CDDA map requires %u slots (%u file extents); this adapter holds %u",
                cdda_stats.required_slots,cdda_stats.required_extents,cdda_capacity);
            problem="CDDA track map exceeds this adapter's capacity";
        } else if(cdda) {
            problem=map_reason_name(cdda_stats.reason);
        } else problem=mapped==MAP_FULL?"track files too fragmented for the launch map (160 slots)":
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
    if(cdda) {
        enum kui_games_cdda_profile profile=kui_games_cdda_profile(image,map);
        const char *package_path=kui_games_cdda_package(profile);
        if(!package_path) {problem="unsupported CDDA profile; supported image identity required";goto done;}
        struct kui_runtime_image candidate={0};
        rr=kui_runtime_read(package_path,&candidate,log,cancel);
        if(rr!=KUI_RUNTIME_OK) {problem=kui_runtime_result_name(rr);goto done;}
        if(!layout(&candidate,false) || !kui_games_cdda_package_admit(profile,&candidate)) {
            kui_runtime_free(&candidate);
            problem="CDDA package identity or layout mismatch; reinstall the selected package";goto done;
        }
        /* Publish only the verified allocation; failures never expose a
         * partial replacement or silently use the ordinary loader. */
        kui_runtime_free(package);*package=candidate;launch_package=package_path;
        log("CDDA launch: exact %s %s profile; loaded executable SHA, driver and hook guards remain mandatory",
            profile==KUI_GAMES_CDDA_MDK2_RAW?"MDK2":"Toy Commander",
            profile==KUI_GAMES_CDDA_TOY_COOKED?"cooked DATA":"raw GDI");
    }
    if(ce_excerpt) {
        size_t stream_seed=armada_stream_seed_find(package,music_profile);
        if(stream_seed!=SIZE_MAX) {
            struct kui_retail_manifest *full=malloc(sizeof(*full));
            if(!full) {problem="not enough memory for the complete CE audio map";goto done;}
            *full=*map;full->reader=KUI_RETAIL_READER_STANDARD;
            struct map_stats stats;
            enum map_result complete=map_cdda_tracks(&files,&fs,&volume,image,full,
                KUI_RETAIL_MANIFEST_SLOTS,&stats);
            bool admitted=complete==MAP_OK && stats.complete &&
                armada_stream_seed_fill(package,stream_seed,full,music_profile,log);
            if(!admitted) log("CE audio map: %u slots required; %s",stats.required_slots,
                complete==MAP_FULL?"mapping capacity exceeded":map_reason_name(stats.reason));
            free(full);
            if(!admitted) {problem="complete CE audio source map is unavailable";goto done;}
        } else if(music_profile!=KUI_WINCE_CDDA_ARMADA || !armada_excerpt_seed(package,image,log)) {
            problem="CE music seed is missing, invalid, stopped or unreadable";goto done;
        }
    }
    r=kui_retail_manifest_encode(map,(uint8_t *)package->data+KUI_RETAIL_MAP_OFFSET);
    if(r!=KUI_GAME_OK) {problem=kui_game_result_name(r);goto done;}
    if(!fill_ram_patches(package,&applied_patches,map->boot_bytes)) {
        problem="RAM patches need a matching ordinary native loader; reinstall the game-launch package";goto done;
    }
    if(patched) log("RAM patches: validated one-time writes inside %u executable bytes",map->boot_bytes);
    char report_root[KUI_GAME_LAUNCH_SETTINGS_PATH_CAP];
    /* Track names are sibling components. Keeping all source files outside
     * the report directory also excludes FAT short-name aliases of either
     * report filename, without guessing which spelling refers to a file. */
    bool report_safe=!strcmp(files.root,"/") ||
        (kui_game_launch_settings_normalize(report_root,files.root) && strcmp(report_root,"/kui/apps/games"));
    if(reporting && !report_safe)
        log("Launch report skipped: selected image shares the reserved report directory");
    if(reporting && report_safe && !stopped(&files)) {
        struct kui_game_launch_report report={
            .version=KUI_VERSION,.build_id=KUI_BUILD_ID,.image_path=path,
            .reader=map->reader==KUI_RETAIL_READER_ASYNC?"Background":
                map->reader==KUI_RETAIL_READER_ASYNC_EAGER?"Background (more work)":"Standard",
            .storage=map->storage_transport==KUI_STORAGE_SCIF?"SCIF microSD":
                map->storage_transport==KUI_STORAGE_SCI?"SCI microSD":"IDE / CF",
            .encoding=ce?"Windows CE prefix + body":(map->flags&KUI_RETAIL_IMAGE_SCRAMBLED)?"Scrambled (decode)":"Plain",
            .cdda=cdda || ce_excerpt,.cdda_profile=ce_excerpt?kui_wince_cdda_disc(music_profile)->name:
                cdda?"Native admitted profile":"Off",.package_path=launch_package,
            .product=map->product,.title=map->title,.bootfile=map->bootfile,
            .ip_crc32=map->ip_crc32,.ip_bytes=KUI_RETAIL_IP_BYTES,
            .executable_crc32=map->boot_crc32,.executable_bytes=map->boot_bytes,.patches=&applied_patches};
        if(!kui_game_launch_report_save(&report,log,cancel))
            log("Launch report unavailable; continuing without a new saved report");
    }
    log("%s prepared: %u tracks, %u extents; IP CRC32=%08x, %s=%u bytes checked at load",
        ce_excerpt?"Windows CE CD music":ce?"Windows CE boot test":"Retail boot",
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

bool kui_games_retail_prepare_reader(const char *path,uint32_t reader,
    struct kui_runtime_image *image,kui_log_fn log,kui_cancel_fn cancelled) {
    return prepare_launch(path,reader,NULL,false,image,log,cancelled);
}
bool kui_games_retail_prepare_launch(const char *path,uint32_t reader,
    const struct kui_game_ram_patches *patches,struct kui_runtime_image *image,
    kui_log_fn log,kui_cancel_fn cancelled) {
    return prepare_launch(path,reader,patches,true,image,log,cancelled);
}
