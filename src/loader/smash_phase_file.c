/* SPDX-License-Identifier: GPL-3.0-only */
/* Private cold-stage file mapper. Never linked in a resident or worker. */
#include "kui/smash_phase_file.h"
#include "kui/smash_session_file.h"
#include "kui/vmu_backing.h"
#include "retail_storage.h"
#include "sci_sd_bus.h"
#include "../dreamcast/sci_sd_storage.h"
#include "ff.h"
#include "diskio.h"
#include <stdbool.h>
#include <string.h>

struct phase_file_work {
    struct kui_retail_storage *storage;
    const struct kui_retail_manifest *map;
    const struct kui_vmu_persist_profile *profile;
    struct kui_smash_phase_file_stats stats;
    FATFS fs;
    FIL file;
    _Alignas(32) uint8_t sector[512];
    uint32_t sectors[8],clusters[8],cluster_count;
    struct kui_smash_menu_capsule_runs allocated;
    bool active,leased,mounted,opened,io_fault;
#if KUI_SMASH_CARD_SESSION
    uint32_t read_limit,session_sectors[KUI_VMU_FS_WITNESS_FILE_BYTES/512u];
    _Alignas(32) uint8_t run[KUI_VMU_FS_WITNESS_SCRATCH_BYTES];
    bool borrowed;
#endif
};
static struct phase_file_work work;

static bool phase_healthy(void) {
    return work.storage && work.storage->transport==KUI_STORAGE_SCI &&
        work.storage->device.sd.ready && !work.storage->stream.active &&
        !kui_sci_sd_faulted() && (!work.leased || kui_sci_sd_healthy());
}
static bool phase_masked(void) {
#ifdef KUI_SMASH_PHASE_FILE_HOST_TEST
    extern bool kui_smash_phase_file_test_masked(void);
    return kui_smash_phase_file_test_masked();
#else
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr));
    return (sr&UINT32_C(0xf0))==UINT32_C(0xf0);
#endif
}
static bool phase_identity(void) {
    struct kui_vmu_sd_identity identity;
    ++work.stats.identities;
    if(!phase_healthy() || kui_vmu_sd_identity_read(&work.storage->device.sd,&identity)) {
        work.io_fault=true;return false;
    }
    bool same=identity.sectors==work.profile->card_sectors &&
        identity.high_capacity==!!work.profile->high_capacity &&
        !memcmp(identity.cid,work.profile->cid,sizeof(identity.cid));
    if(!same)work.io_fault=true;
    return same;
}
static bool phase_range(uint64_t lba,uint32_t count) {
    return count && lba>=work.profile->partition_first &&
        lba<work.profile->partition_end && count<=work.profile->partition_end-lba &&
        lba<work.profile->card_sectors && count<=work.profile->card_sectors-lba &&
        lba<=UINT32_MAX && (uint64_t)count<=UINT64_C(0x100000000)-lba;
}

/* These names are owned by the private cold-stage FatFs link. The mapper never
 * offers a filesystem writer, including after mount or on an error path. */
DSTATUS disk_status(BYTE drive) {
    return drive || !work.active || !work.leased || !phase_healthy()?
        (STA_NOINIT|STA_PROTECT):STA_PROTECT;
}
DSTATUS disk_initialize(BYTE drive) {return disk_status(drive);}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t lba,UINT count) {
    uint32_t limit=KUI_SMASH_PHASE_FILE_READ_LIMIT;
#if KUI_SMASH_CARD_SESSION
    if(work.read_limit)limit=work.read_limit;
#endif
    if(drive || !out || !work.active || !work.leased || !phase_healthy() ||
       !count || work.stats.reads>limit || count>limit-work.stats.reads ||
       !((lba==0u && count==1u) || phase_range(lba,count)))return RES_PARERR;
    work.stats.reads+=count;
    /* Aligned private storage retains the DMA-capable ordinary CRC path even
     * when FatFs supplies an unaligned directory or short-file receiver. */
    for(UINT i=0u;i<count;i++) {
        if(kui_loader_sd_read(&work.storage->device.sd,(uint32_t)lba+i,1u,work.sector)!=KUI_LOADER_SD_OK ||
           !phase_healthy()) {work.io_fault=true;return RES_ERROR;}
        if(out+512u*i!=work.sector)memcpy(out+512u*i,work.sector,512u);
    }
    return RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *data,LBA_t lba,UINT count) {
    (void)drive;(void)data;(void)lba;(void)count;return RES_WRPRT;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *out) {
    if(disk_status(drive)&STA_NOINIT)return RES_NOTRDY;
    if(command==CTRL_SYNC)return RES_OK; /* No filesystem write can be pending. */
    if(!out)return RES_PARERR;
    if(command==GET_SECTOR_COUNT) {*(LBA_t *)out=(LBA_t)work.profile->card_sectors;return RES_OK;}
    if(command==GET_SECTOR_SIZE) {*(WORD *)out=512u;return RES_OK;}
    if(command==GET_BLOCK_SIZE) {*(DWORD *)out=1u;return RES_OK;}
    return RES_PARERR;
}
/* Read-only f_open/f_lseek/f_close cannot need a timestamp. Its presence is
 * required only because this is the same pinned FatFs configuration as GUI. */
DWORD get_fattime(void) {return 0u;}

static bool phase_geometry(void) {
    const struct kui_vmu_persist_geometry *g=&work.profile->geometry;
    const FATFS *fs=&work.fs;
    uint64_t selected_fat=g->fat_first_lba+g->fat_sectors*g->active_fat;
    return ((fs->fs_type==FS_FAT32 && g->fs_type==KUI_VMU_PERSIST_FAT32) ||
            (fs->fs_type==FS_EXFAT && g->fs_type==KUI_VMU_PERSIST_EXFAT)) &&
        g->sector_bytes==512u && fs->volbase==work.profile->partition_first &&
        fs->csize==g->sectors_per_cluster && fs->n_fatent>=2u &&
        fs->n_fatent-2u==g->cluster_count && fs->n_fats==g->fat_count &&
        fs->fsize==g->fat_sectors && fs->fatbase==selected_fat &&
        fs->database==g->data_first_lba && fs->dirbase==g->root_cluster;
}
static bool phase_header(const char *data,const char build[13]) {
    static const char prefix[]=KUI_SMASH_PHASE_FILE_PREFIX;
    if(!data || !build || build[12])return false;
    for(unsigned i=0u;i<12u;i++)
        if(!((build[i]>='0' && build[i]<='9') || (build[i]>='a' && build[i]<='f')))return false;
    return !memcmp(data,prefix,sizeof(prefix)-1u) &&
        !memcmp(data+sizeof(prefix)-1u,build,12u) && data[KUI_SMASH_PHASE_FILE_HEADER_BYTES-1u]=='\n';
}
static bool phase_ascii(const char *text) {
    for(unsigned i=0u;i<KUI_SMASH_PHASE_FILE_BYTES;i++) {
        unsigned char c=(unsigned char)text[i];
        if(c!='\n' && c!='\r' && c!='\t' && (c<32u || c>126u))return false;
    }
    return true;
}
static bool phase_add_cluster(uint32_t cluster) {
    if(cluster<2u || cluster>=work.fs.n_fatent)return false;
    if(work.cluster_count && work.clusters[work.cluster_count-1u]==cluster)return true;
    for(unsigned i=0u;i<work.cluster_count;i++)if(work.clusters[i]==cluster)return false;
    if(work.cluster_count==8u)return false;
    work.clusters[work.cluster_count++]=cluster;
    return true;
}
static bool phase_map(void) {
    /* A seek just inside each logical sector fills fp.sect and follows at
     * most the eight required clusters. CREATE_LINKMAP instead walks to EOC
     * without a cycle bound, even if every read is a FatFs window-cache hit. */
    for(unsigned i=0u;i<8u;i++) {
        FSIZE_t offset=(FSIZE_t)i*512u+1u;
        if(f_lseek(&work.file,offset)!=FR_OK || f_tell(&work.file)!=offset ||
           !phase_add_cluster(work.file.clust) || !phase_range(work.file.sect,1u))return false;
        work.sectors[i]=(uint32_t)work.file.sect;
        uint64_t expected=(uint64_t)work.fs.database+
            (uint64_t)(work.file.clust-2u)*work.fs.csize+(i%work.fs.csize);
        if(expected!=work.file.sect)return false;
        for(unsigned j=0u;j<i;j++)if(work.sectors[i]==work.sectors[j])return false;
    }
    /* The ordinary capsule mapper validates and clips this bounded table.
     * It also refuses more than two physical runs, matching capsule policy. */
    uint32_t table[18]={18u};unsigned at=1u;
    for(unsigned i=0u;i<work.cluster_count;i++) {
        if(i && work.clusters[i]==work.clusters[i-1u]+1u)++table[at-2u];
        else {table[at++]=1u;table[at++]=work.clusters[i];}
    }
    table[0]=at+1u;table[at]=0u;
    uint64_t allocated=(uint64_t)work.cluster_count*work.fs.csize;
    if(allocated>UINT32_MAX || work.fs.database<work.fs.volbase)return false;
    return kui_smash_menu_capsule_map(table,at+1u,(uint32_t)(work.fs.database-work.fs.volbase),
        work.fs.csize,work.fs.n_fatent,(uint32_t)work.profile->partition_first,
        (uint32_t)(work.profile->partition_end-work.profile->partition_first),
        (uint32_t)allocated,&work.allocated);
}

enum kui_smash_phase_file_result kui_smash_phase_file_write(
    struct kui_retail_storage *storage,const struct kui_retail_manifest *map,
    const struct kui_vmu_persist_profile *profile,
    const struct kui_smash_menu_capsule_runs *forbidden,size_t forbidden_count,
    const char build[13],const char text[KUI_SMASH_PHASE_FILE_BYTES],
    struct kui_smash_phase_file_stats *stats) {
    if(stats)memset(stats,0,sizeof(*stats));
    if(work.active || !storage || !map || !profile || !text || !phase_header(text,build) ||
       !phase_ascii(text) || (forbidden_count && !forbidden) || forbidden_count>8u ||
       storage->transport!=KUI_STORAGE_SCI || storage->stream.active || !phase_masked() ||
       !profile->card_sectors || profile->card_sectors>UINT32_MAX ||
       profile->partition_first>=profile->partition_end || profile->partition_end>profile->card_sectors ||
       map->card_sectors!=profile->card_sectors || map->partition_start!=profile->partition_first ||
       map->partition_end!=profile->partition_end)return KUI_SMASH_PHASE_FILE_UNAVAILABLE;
    memset(&work,0,sizeof(work));work.active=true;work.storage=storage;work.map=map;work.profile=profile;
    enum kui_smash_phase_file_result result=KUI_SMASH_PHASE_FILE_UNAVAILABLE;
    work.stats.detail=1u;
    if(!phase_healthy()) {work.io_fault=true;goto done;}
    enum kui_loader_sd_result acquired=kui_retail_storage_acquire(storage);
    if(acquired!=KUI_LOADER_SD_OK) {
        if(acquired!=KUI_LOADER_SD_NOT_READY && acquired!=KUI_LOADER_SD_UNSUPPORTED)work.io_fault=true;
        goto done;
    }
    work.leased=true;
    work.stats.detail=2u;if(!phase_identity())goto done;
    work.stats.detail=3u;
    work.mounted=true;
    if(f_mount(&work.fs,"0:",1u)!=FR_OK || !phase_geometry())goto done;
    work.stats.detail=4u;
    if(f_open(&work.file,KUI_SMASH_PHASE_FILE_PATH,FA_READ)!=FR_OK)goto done;
    work.opened=true;
    UINT got=0u;
    if(f_size(&work.file)!=KUI_SMASH_PHASE_FILE_BYTES ||
       f_read(&work.file,work.sector,KUI_SMASH_PHASE_FILE_HEADER_BYTES,&got)!=FR_OK ||
       got!=KUI_SMASH_PHASE_FILE_HEADER_BYTES || !phase_header((const char *)work.sector,build))goto done;
    work.stats.detail=5u;
    if(!phase_map() || !kui_smash_menu_capsule_disjoint(&work.allocated,map,profile))goto done;
    for(size_t i=0u;i<forbidden_count;i++)
        if(!kui_smash_menu_capsule_runs_disjoint(&work.allocated,&forbidden[i]))goto done;
    work.stats.detail=6u;
    FRESULT closed=f_close(&work.file);
    work.opened=false;
    if(closed!=FR_OK)goto done;
    if(f_mount(NULL,"0:",0u)!=FR_OK)goto done;
    work.mounted=false;
    if(!phase_identity())goto done;
    work.stats.detail=7u;
    for(unsigned i=0u;i<8u;i++) {
        memcpy(work.sector,text+512u*i,512u);
        bool (*block)(void *,const uint8_t *,uint8_t *,size_t,bool,uint16_t *)=
            storage->device.sd.bus.transfer_block;
        storage->device.sd.bus.transfer_block=NULL;
        enum kui_loader_sd_result written=kui_sci_sd_write(&storage->device.sd,work.sectors[i],1u,work.sector);
        storage->device.sd.bus.transfer_block=block;
        ++work.stats.writes;
        if(written!=KUI_LOADER_SD_OK || !phase_healthy()) {work.io_fault=true;goto done;}
    }
    work.stats.detail=8u;
    if(kui_sci_sd_sync(&storage->device.sd)!=KUI_LOADER_SD_OK || !phase_healthy()) {
        work.io_fault=true;goto done;
    }
    work.stats.detail=9u;
    for(unsigned i=0u;i<8u;i++) {
        ++work.stats.reads;
        if(kui_loader_sd_read(&storage->device.sd,work.sectors[i],1u,work.sector)!=KUI_LOADER_SD_OK ||
           !phase_healthy() || memcmp(work.sector,text+512u*i,512u)) {work.io_fault=true;goto done;}
    }
    work.stats.detail=10u;if(!phase_identity())goto done;
    work.stats.detail=0u;result=KUI_SMASH_PHASE_FILE_OK;
done:
    if(work.opened && f_close(&work.file)!=FR_OK)result=KUI_SMASH_PHASE_FILE_UNAVAILABLE;
    if(work.mounted && f_mount(NULL,"0:",0u)!=FR_OK)result=KUI_SMASH_PHASE_FILE_UNAVAILABLE;
    if(work.leased) {
        kui_retail_storage_release(storage);
        work.leased=false;
        if(!phase_healthy())work.io_fault=true;
    }
    if(work.io_fault) {storage->device.sd.ready=false;result=KUI_SMASH_PHASE_FILE_CARD_FAULT;}
    if(stats)*stats=work.stats;
    work.active=false;work.storage=NULL;work.map=NULL;work.profile=NULL;
    return result;
}

#if KUI_SMASH_CARD_SESSION
#include "kui/smash_ce_profile.h"
#define SESSION_BLOCKS (KUI_VMU_FS_WITNESS_FILE_BYTES/512u)
#define SESSION_CAPSULE_BLOCK_LIMIT 16384u
static const char *const session_capsules[6]={
    KUI_SMASH_MENU_CAPSULE_PATH,KUI_SMASH_MENU_OUTER_CAPSULE_PATH,
    KUI_SMASH_MENU_VC2_CAPSULE_PATH,KUI_SMASH_MENU_SWIRL_CAPSULE_PATH,
    KUI_SMASH_CE_VC2_SESSION,KUI_SMASH_CE_SWIRL_SESSION};
static uint32_t session_u32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static bool session_prefix(const uint8_t *p) {
    return !memcmp(p,"KUIFSW01",8u) && session_u32(p+8u)==1u && session_u32(p+12u)==512u;
}
static bool session_close(void) {
    if(!work.opened)return true;
    FRESULT result=f_close(&work.file);work.opened=false;return result==FR_OK;
}
static bool session_open(const char *path) {
    /* A missing/malformed directory cannot spend the whole capsule-map budget. */
    work.read_limit=work.stats.reads+KUI_SMASH_PHASE_FILE_READ_LIMIT;
    if(f_open(&work.file,path,FA_READ)!=FR_OK)return false;
    work.opened=true;return true;
}
static bool session_tail(uint32_t cluster) {
    /* NoFatChain exFAT files have no meaningful FAT tail. All other exact
     * preallocations must end here, rather than hiding an unvisited suffix. */
    if(work.fs.fs_type==FS_EXFAT && work.file.obj.stat==2u)return true;
    uint64_t lba=(uint64_t)work.fs.fatbase+cluster/128u;
    if(!phase_range(lba,1u) || disk_read(0u,work.sector,lba,1u)!=RES_OK)return false;
    uint32_t next=session_u32(work.sector+4u*(cluster%128u));
    if(work.fs.fs_type==FS_FAT32)next&=UINT32_C(0x0fffffff);
    return next>=(work.fs.fs_type==FS_FAT32?UINT32_C(0x0ffffff8):UINT32_C(0xfffffff8));
}
static bool session_map(uint32_t blocks,bool target,
    struct kui_smash_menu_capsule_runs *data,struct kui_smash_menu_capsule_runs *allocated) {
    if(!blocks || blocks>SESSION_CAPSULE_BLOCK_LIMIT || !work.fs.csize)return false;
    uint32_t clusters=(blocks+work.fs.csize-1u)/work.fs.csize;
    /* One data-sector fill and at most two metadata fills per cluster, plus
     * fixed tail/short-entry overhead. Every seek advances by one cluster. */
    work.read_limit=work.stats.reads+3u*clusters+16u;
    uint32_t table[6]={0},run_count=0u,last=0u;
    for(uint32_t i=0u;i<clusters;i++) {
        FSIZE_t offset=(FSIZE_t)i*work.fs.csize*512u+1u;
        if(f_lseek(&work.file,offset)!=FR_OK || f_tell(&work.file)!=offset)return false;
        uint32_t cluster=work.file.clust;
        uint64_t lba=(uint64_t)work.fs.database+(uint64_t)(cluster-2u)*work.fs.csize;
        if(cluster<2u || cluster>=work.fs.n_fatent || work.file.sect!=lba ||
           !phase_range(lba,work.fs.csize))return false;
        for(uint32_t run=0u;run<run_count;run++)
            if(cluster>=table[2u+2u*run] &&
               cluster-table[2u+2u*run]<table[1u+2u*run])return false;
        if(i && cluster==last+1u)++table[1u+2u*(run_count-1u)];
        else {
            if(run_count==2u)return false;
            table[1u+2u*run_count]=1u;table[2u+2u*run_count]=cluster;++run_count;
        }
        if(target) {
            uint32_t first=i*work.fs.csize,n=blocks-first;
            if(n>work.fs.csize)n=work.fs.csize;
            for(uint32_t j=0u;j<n;j++)work.session_sectors[first+j]=(uint32_t)lba+j;
        }
        last=cluster;
    }
    if(!session_tail(last))return false;
    uint32_t words=2u+2u*run_count;table[0]=words;
    uint64_t full=(uint64_t)clusters*work.fs.csize;
    if(full>UINT32_MAX || work.fs.database<work.fs.volbase)return false;
    return kui_smash_menu_capsule_map(table,words,(uint32_t)(work.fs.database-work.fs.volbase),
        work.fs.csize,work.fs.n_fatent,(uint32_t)work.profile->partition_first,
        (uint32_t)(work.profile->partition_end-work.profile->partition_first),blocks,data) &&
        kui_smash_menu_capsule_map(table,words,(uint32_t)(work.fs.database-work.fs.volbase),
        work.fs.csize,work.fs.n_fatent,(uint32_t)work.profile->partition_first,
        (uint32_t)(work.profile->partition_end-work.profile->partition_first),(uint32_t)full,allocated);
}
static bool session_prepare(const struct kui_smash_card_config *config) {
    work.stats.detail=3u;work.read_limit=KUI_SMASH_PHASE_FILE_READ_LIMIT;work.mounted=true;
    if(f_mount(&work.fs,"0:",1u)!=FR_OK || !phase_geometry())return false;
    work.stats.detail=4u;
    if(!session_open(KUI_SMASH_CARD_WITNESS_PATH) || f_size(&work.file)!=KUI_VMU_FS_WITNESS_FILE_BYTES)return false;
    UINT got=0u;
    if(f_read(&work.file,work.sector,16u,&got)!=FR_OK || got!=16u || !session_prefix(work.sector))return false;
    struct kui_smash_menu_capsule_runs data,allocated;
    work.stats.detail=5u;
    if(!session_map(SESSION_BLOCKS,true,&data,&allocated) ||
       memcmp(&data,&config->data,sizeof(data)) || memcmp(&allocated,&config->allocated,sizeof(allocated)) ||
       !kui_smash_menu_capsule_disjoint(&allocated,work.map,work.profile) || !session_close())return false;
    for(unsigned i=0u;i<6u;i++) {
        if(!session_open(session_capsules[i]))return false;
        FSIZE_t bytes=f_size(&work.file);
        if(!bytes || bytes>(FSIZE_t)SESSION_CAPSULE_BLOCK_LIMIT*512u ||
           !session_map((uint32_t)((bytes+511u)/512u),false,&data,&work.allocated) ||
           !kui_smash_menu_capsule_runs_disjoint(&allocated,&work.allocated) || !session_close())return false;
    }
    work.stats.detail=6u;
    if(f_mount(NULL,"0:",0u)!=FR_OK)return false;
    work.mounted=false;return phase_identity();
}
static void session_fill(const uint8_t header[512],const struct kui_vmu_fs_witness_record *records,
    uint32_t record_bytes,uint32_t block) {
    if(!block) {memcpy(work.sector,header,512u);return;}
    uint32_t offset=(block-1u)*512u,n=record_bytes-offset;
    if(n>512u)n=512u;
    memset(work.sector,0,512u);memcpy(work.sector,(const uint8_t *)records+offset,n);
}
static bool session_read(uint32_t first,uint32_t blocks) {
    if(!blocks || blocks>KUI_VMU_FS_WITNESS_RUN_BLOCKS || first>=SESSION_BLOCKS || blocks>SESSION_BLOCKS-first)return false;
    enum kui_loader_sd_result result=blocks==1u?
        kui_loader_sd_read(&work.storage->device.sd,work.session_sectors[first],1u,work.run):
        kui_loader_sd_read_multi(&work.storage->device.sd,work.session_sectors[first],blocks,work.run);
    work.stats.reads+=blocks;
    if(result!=KUI_LOADER_SD_OK || !phase_healthy()) {work.io_fault=true;return false;}
    return true;
}
static uint32_t session_run(uint32_t first,uint32_t total) {
    uint32_t n=1u;
    while(n<KUI_VMU_FS_WITNESS_RUN_BLOCKS && n<total-first &&
        work.session_sectors[first+n]==work.session_sectors[first]+n)++n;
    return n;
}
static enum kui_smash_session_file_result session_io(
    struct kui_retail_storage *storage,const struct kui_retail_manifest *map,
    const struct kui_vmu_persist_profile *profile,const struct kui_smash_card_config *config,
    bool borrowed,bool store,uint8_t *header,struct kui_vmu_fs_witness_record *records,
    size_t capacity,uint32_t *count,struct kui_smash_phase_file_stats *stats) {
    uint32_t requested=count?*count:0u;
    if(count)*count=0u;
    if(stats)memset(stats,0,sizeof(*stats));
    if(work.active || !storage || !map || !profile || !config || !header || !records || !count ||
       !capacity || capacity>KUI_VMU_FS_WITNESS_MAX_RECORDS ||
       !kui_smash_card_config_valid(config) ||
       config->witness_bytes!=KUI_VMU_FS_WITNESS_FILE_BYTES ||
       storage->transport!=KUI_STORAGE_SCI || storage->stream.active || !phase_masked() ||
       !profile->card_sectors || profile->card_sectors>UINT32_MAX ||
       profile->partition_first>=profile->partition_end || profile->partition_end>profile->card_sectors ||
       map->card_sectors!=profile->card_sectors || map->partition_start!=profile->partition_first ||
       map->partition_end!=profile->partition_end)return KUI_SMASH_SESSION_FILE_UNAVAILABLE;
    uint32_t parsed=0u;
    if(store && (kui_vmu_fs_witness_header_count(header,&parsed) || parsed!=requested || requested>capacity))
        return KUI_SMASH_SESSION_FILE_UNAVAILABLE;
    memset(&work,0,sizeof(work));work.active=true;work.storage=storage;work.map=map;work.profile=profile;
    work.borrowed=borrowed;work.leased=borrowed;
    enum kui_smash_session_file_result result=KUI_SMASH_SESSION_FILE_UNAVAILABLE;
    work.stats.detail=1u;
    if(!phase_healthy()) {work.io_fault=true;goto done;}
    if(!borrowed) {
        enum kui_loader_sd_result acquired=kui_retail_storage_acquire(storage);
        if(acquired!=KUI_LOADER_SD_OK) {
            if(acquired!=KUI_LOADER_SD_NOT_READY && acquired!=KUI_LOADER_SD_UNSUPPORTED)work.io_fault=true;
            goto done;
        }
        work.leased=true;
    }
    work.stats.detail=2u;if(!phase_identity() || !session_prepare(config))goto done;
    if(!store) {
        if(!session_read(0u,1u))goto done;
        memcpy(header,work.run,512u);
        if(kui_vmu_fs_witness_header_count(header,&parsed) || parsed>capacity)goto done;
        requested=parsed;
    }
    uint32_t bytes=requested*KUI_VMU_FS_WITNESS_RECORD_BYTES,total=1u+(bytes+511u)/512u;
    work.stats.detail=7u;
    if(store) {
        for(uint32_t block=0u;block<total;block++) {
            session_fill(header,records,bytes,block);
            bool (*callback)(void *,const uint8_t *,uint8_t *,size_t,bool,uint16_t *)=storage->device.sd.bus.transfer_block;
            storage->device.sd.bus.transfer_block=NULL;
            enum kui_loader_sd_result written=kui_sci_sd_write(&storage->device.sd,work.session_sectors[block],1u,work.sector);
            storage->device.sd.bus.transfer_block=callback;++work.stats.writes;
            if(written!=KUI_LOADER_SD_OK || !phase_healthy()) {work.io_fault=true;goto done;}
        }
        work.stats.detail=8u;
        if(kui_sci_sd_sync(&storage->device.sd)!=KUI_LOADER_SD_OK || !phase_healthy()) {work.io_fault=true;goto done;}
    }
    work.stats.detail=9u;
    for(uint32_t first=store?0u:1u;first<total;) {
        uint32_t n=session_run(first,total);if(!session_read(first,n))goto done;
        for(uint32_t j=0u;j<n;j++) {
            uint32_t block=first+j;const uint8_t *sector=work.run+512u*j;
            if(store) {
                session_fill(header,records,bytes,block);
                if(memcmp(sector,work.sector,512u)) {work.io_fault=true;goto done;}
            } else {
                uint32_t offset=(block-1u)*512u,length=bytes-offset;
                if(length>512u)length=512u;
                memcpy((uint8_t *)records+offset,sector,length);
                for(uint32_t k=length;k<512u;k++)if(sector[k])goto done;
            }
        }
        first+=n;
    }
    work.stats.detail=10u;if(!phase_identity())goto done;
    work.stats.detail=0u;*count=requested;result=KUI_SMASH_SESSION_FILE_OK;
done:
    if(!session_close())result=KUI_SMASH_SESSION_FILE_UNAVAILABLE;
    if(work.mounted && f_mount(NULL,"0:",0u)!=FR_OK)result=KUI_SMASH_SESSION_FILE_UNAVAILABLE;
    if(work.leased && !phase_healthy())work.io_fault=true;
    if(work.leased && !work.borrowed) {
        kui_retail_storage_release(storage);work.leased=false;
        if(!phase_healthy())work.io_fault=true;
    }
    if(work.io_fault) {storage->device.sd.ready=false;result=KUI_SMASH_SESSION_FILE_CARD_FAULT;}
    if(result!=KUI_SMASH_SESSION_FILE_OK)*count=0u;
    if(stats)*stats=work.stats;
    work.active=false;work.storage=NULL;work.map=NULL;work.profile=NULL;
    return result;
}
enum kui_smash_session_file_result kui_smash_session_file_store(
    struct kui_retail_storage *storage,const struct kui_retail_manifest *map,
    const struct kui_vmu_persist_profile *profile,const struct kui_smash_card_config *config,
    bool borrowed,const uint8_t header[512],const struct kui_vmu_fs_witness_record *records,
    uint32_t count,struct kui_smash_phase_file_stats *stats) {
    return session_io(storage,map,profile,config,borrowed,true,(uint8_t *)(uintptr_t)header,
        (struct kui_vmu_fs_witness_record *)(uintptr_t)records,count,&count,stats);
}
enum kui_smash_session_file_result kui_smash_session_file_load(
    struct kui_retail_storage *storage,const struct kui_retail_manifest *map,
    const struct kui_vmu_persist_profile *profile,const struct kui_smash_card_config *config,
    bool borrowed,uint8_t header[512],struct kui_vmu_fs_witness_record *records,
    size_t capacity,uint32_t *count,struct kui_smash_phase_file_stats *stats) {
    if(count)*count=0u;
    return session_io(storage,map,profile,config,borrowed,false,header,records,capacity,count,stats);
}
#endif
