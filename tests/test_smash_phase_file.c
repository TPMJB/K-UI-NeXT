/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/smash_phase_file.h"
#include "kui/vmu_backing.h"
#include "retail_storage.h"
#include "sci_sd_bus.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static FILE *image;
static const uint32_t card_blocks=96u*1024u*1024u/512u;
static const char build[]="0123456789ab";
struct metadata {
    struct kui_vmu_persist_profile profile;
    struct kui_smash_menu_capsule_runs report;
    uint32_t sectors[8],clusters[8],directory_cluster;
};
static struct metadata meta;
static void direct_read(uint32_t lba,unsigned count,void *out) {
    assert(lba<card_blocks && count && count<=card_blocks-lba);
    assert(!fseeko(image,(off_t)lba*512,SEEK_SET));assert(fread(out,512u,count,image)==count);
}
static void direct_write(uint32_t lba,unsigned count,const void *data) {
    assert(lba<card_blocks && count && count<=card_blocks-lba);
    assert(!fseeko(image,(off_t)lba*512,SEEK_SET));assert(fwrite(data,512u,count,image)==count);assert(!fflush(image));
}
static void text(char out[4096],const char *body) {
    memset(out,' ',4096u);
    char first[256];int n=snprintf(first,sizeof(first),KUI_SMASH_PHASE_FILE_PREFIX "%s\n%s\n",build,body);
    assert(n>0 && n<(int)sizeof(first));memcpy(out,first,(size_t)n);
}

#ifdef KUI_PHASE_FILE_SEED
DSTATUS disk_initialize(BYTE drive) {return drive?STA_NOINIT:0u;}
DSTATUS disk_status(BYTE drive) {return disk_initialize(drive);}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t lba,UINT count) {
    if(drive)return RES_PARERR;
    direct_read(lba,count,out);return RES_OK;
}
DRESULT disk_write(BYTE drive,const BYTE *in,LBA_t lba,UINT count) {
    if(drive)return RES_PARERR;
    direct_write(lba,count,in);return RES_OK;
}
DRESULT disk_ioctl(BYTE drive,BYTE command,void *out) {
    if(drive)return RES_PARERR;
    if(command==CTRL_SYNC)return RES_OK;
    if(command==GET_SECTOR_COUNT) {*(LBA_t *)out=card_blocks;return RES_OK;}
    if(command==GET_SECTOR_SIZE) {*(WORD *)out=512u;return RES_OK;}
    if(command==GET_BLOCK_SIZE) {*(DWORD *)out=1u;return RES_OK;}
    return RES_PARERR;
}
DWORD get_fattime(void) {return UINT32_C(0x5d052345);}
int main(int argc,char **argv) {
    assert(argc==4);image=fopen(argv[1],"r+b");assert(image);
    FATFS fs;assert(f_mount(&fs,"0:",1u)==FR_OK);
    assert(f_mkdir("0:/KUI")==FR_OK);assert(f_mkdir("0:/KUI/apps")==FR_OK);
    assert(f_mkdir("0:/KUI/apps/games")==FR_OK);
    DIR dir;assert(f_opendir(&dir,"0:/KUI/apps/games")==FR_OK);meta.directory_cluster=dir.obj.sclust;
    assert(f_closedir(&dir)==FR_OK);
    char blank[4096];text(blank,"Pending console timing");
    FIL file;UINT put;
    assert(f_open(&file,KUI_SMASH_PHASE_FILE_PATH,FA_CREATE_ALWAYS|FA_WRITE|FA_READ)==FR_OK);
    if(!strcmp(argv[3],"two-runs")) {
        assert(fs.fs_type==FS_FAT32 && fs.csize==1u);
        assert(f_write(&file,blank,2048u,&put)==FR_OK && put==2048u);assert(f_sync(&file)==FR_OK);
        FIL spacer;assert(f_open(&spacer,"0:/KUI/apps/games/spacer.bin",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
        assert(f_write(&spacer,blank,4096u,&put)==FR_OK && put==4096u);assert(f_close(&spacer)==FR_OK);
        assert(f_write(&file,blank+2048u,2048u,&put)==FR_OK && put==2048u);assert(f_sync(&file)==FR_OK);
    } else {
        assert(!strcmp(argv[3],"one-run"));assert(f_expand(&file,sizeof(blank),1u)==FR_OK);
        assert(f_write(&file,blank,sizeof(blank),&put)==FR_OK && put==sizeof(blank));assert(f_sync(&file)==FR_OK);
    }
    for(unsigned i=0;i<8u;i++) {
        assert(f_lseek(&file,512u*i+1u)==FR_OK);meta.sectors[i]=(uint32_t)file.sect;meta.clusters[i]=file.clust;
    }
    uint32_t table[18]={18u};file.cltbl=table;assert(f_lseek(&file,CREATE_LINKMAP)==FR_OK);file.cltbl=NULL;
    uint32_t allocated=0u;for(unsigned i=1u;i+1u<table[0];i+=2u)allocated+=table[i]*fs.csize;
    assert(kui_smash_menu_capsule_map(table,table[0],(uint32_t)(fs.database-fs.volbase),fs.csize,
        fs.n_fatent,(uint32_t)fs.volbase,card_blocks-(uint32_t)fs.volbase,allocated,&meta.report));
    assert(!!meta.report.second_lba==!strcmp(argv[3],"two-runs"));
    meta.profile.card_sectors=card_blocks;meta.profile.partition_first=fs.volbase;meta.profile.partition_end=card_blocks;
    meta.profile.high_capacity=1u;meta.profile.transport=1u;
    for(unsigned i=0;i<16u;i++)meta.profile.cid[i]=(uint8_t)(13u*i+7u);
    meta.profile.geometry=(struct kui_vmu_persist_geometry){.fs_type=fs.fs_type==FS_EXFAT?KUI_VMU_PERSIST_EXFAT:KUI_VMU_PERSIST_FAT32,
        .sector_bytes=512u,.sectors_per_cluster=fs.csize,.fat_count=fs.n_fats,.fat_first_lba=fs.fatbase,
        .fat_sectors=fs.fsize,.data_first_lba=fs.database,.cluster_count=fs.n_fatent-2u,.root_cluster=(uint32_t)fs.dirbase};
    assert(f_close(&file)==FR_OK);assert(f_mount(NULL,"0:",0u)==FR_OK);
    assert(!fclose(image));FILE *out=fopen(argv[2],"wb");assert(out);assert(fwrite(&meta,sizeof(meta),1u,out)==1u);assert(!fclose(out));
    puts("seeded actual filesystem report and complete allocation map");return 0;
}
#else
static struct kui_retail_storage storage;
static struct kui_retail_manifest manifest;
static unsigned reads,writes,identities,acquires,releases,syncs;
static unsigned bad_identity,fail_read,fail_write,fail_sync,fail_release,corrupt_readback;
static bool masked=true,leased,healthy=true,fault;
static enum kui_loader_sd_result acquire_result;
static char wanted[4096],blank[4096];
static uint64_t other_bytes_hash(void) {
    uint64_t hash=UINT64_C(1469598103934665603);uint8_t bytes[512];
    for(uint32_t lba=0u;lba<card_blocks;lba++) {
        bool report=false;for(unsigned i=0u;i<8u;i++)if(meta.sectors[i]==lba)report=true;
        if(report)continue;
        direct_read(lba,1u,bytes);
        for(unsigned i=0u;i<512u;i++)hash=(hash^bytes[i])*UINT64_C(1099511628211);
    }
    return hash;
}
bool kui_smash_phase_file_test_masked(void) {return masked;}
bool kui_sci_sd_healthy(void) {return leased && healthy;}
bool kui_sci_sd_faulted(void) {return fault;}
enum kui_loader_sd_result kui_retail_storage_acquire(struct kui_retail_storage *s) {
    assert(s==&storage && !leased);++acquires;if(acquire_result)return acquire_result;
    leased=true;return KUI_LOADER_SD_OK;
}
void kui_retail_storage_release(struct kui_retail_storage *s) {
    assert(s==&storage && leased);leased=false;++releases;if(fail_release) {fault=true;healthy=false;}
}
enum kui_loader_sd_result kui_loader_sd_read(struct kui_loader_sd *card,uint32_t lba,uint32_t count,void *out) {
    assert(card==&storage.device.sd && leased && count==1u && !((uintptr_t)out&31u));
    ++reads;if(reads==fail_read) {card->ready=false;return KUI_LOADER_SD_CRC;}
    direct_read(lba,count,out);
    if(corrupt_readback && writes==8u && lba==meta.sectors[0])((uint8_t *)out)[50]^=1u;
    return KUI_LOADER_SD_OK;
}
int kui_vmu_sd_identity_read(struct kui_loader_sd *card,struct kui_vmu_sd_identity *out) {
    assert(card==&storage.device.sd && leased);++identities;
    memcpy(out->cid,meta.profile.cid,sizeof(out->cid));out->sectors=card_blocks;out->high_capacity=true;
    if(identities==bad_identity)out->cid[3]^=1u;
    return KUI_VMU_BACKING_OK;
}
enum kui_loader_sd_result kui_sci_sd_write(struct kui_loader_sd *card,uint32_t lba,uint32_t count,const uint8_t *in) {
    assert(card==&storage.device.sd && leased && !card->bus.transfer_block && count==1u);
    assert(writes<8u && lba==meta.sectors[writes]);++writes;
    if(writes==fail_write) {card->ready=false;return KUI_LOADER_SD_TIMEOUT;}
    direct_write(lba,count,in);return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_sci_sd_sync(struct kui_loader_sd *card) {
    assert(card==&storage.device.sd && leased);++syncs;
    if(fail_sync) {card->ready=false;return KUI_LOADER_SD_TIMEOUT;}return KUI_LOADER_SD_OK;
}
static bool unused_block(void *a,const uint8_t *b,uint8_t *c,size_t d,bool e,uint16_t *f) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;abort();
}
static void reset(void) {
    assert(!leased);
    storage=(struct kui_retail_storage){.transport=KUI_STORAGE_SCI};
    storage.device.sd=(struct kui_loader_sd){.ready=true,.blocks=card_blocks,.high_capacity=true};
    storage.device.sd.bus.transfer_block=unused_block;
    manifest=(struct kui_retail_manifest){.card_sectors=card_blocks,.partition_start=(uint32_t)meta.profile.partition_first,
        .partition_end=(uint32_t)meta.profile.partition_end};
    reads=writes=identities=acquires=releases=syncs=0u;
    bad_identity=fail_read=fail_write=fail_sync=fail_release=corrupt_readback=0u;
    masked=healthy=true;fault=false;acquire_result=KUI_LOADER_SD_OK;
    for(unsigned i=0u;i<8u;i++)direct_write(meta.sectors[i],1u,blank+512u*i);
}
static enum kui_smash_phase_file_result run(const struct kui_vmu_persist_profile *p,
    const struct kui_smash_menu_capsule_runs *forbidden,size_t n) {
    struct kui_smash_phase_file_stats stats;
    enum kui_smash_phase_file_result r=kui_smash_phase_file_write(&storage,&manifest,p,forbidden,n,build,wanted,&stats);
    assert(!leased && stats.writes==writes && stats.identities==identities);
    assert(storage.device.sd.bus.transfer_block==unused_block);
    if(r==KUI_SMASH_PHASE_FILE_CARD_FAULT)assert(!storage.device.sd.ready);
    else assert(storage.device.sd.ready);
    assert(releases<=acquires && acquires-releases<=1u);
    return r;
}
int main(int argc,char **argv) {
    assert(argc==3);image=fopen(argv[1],"r+b");assert(image);
    FILE *in=fopen(argv[2],"rb");assert(in);assert(fread(&meta,sizeof(meta),1u,in)==1u);assert(!fclose(in));
    text(blank,"Pending console timing");text(wanted,"GIN 111 ms\nFS OWNER 222 ms\nMDE ENTRY 333 ms\nNo elapsed hardware claim in host fixture.");
    uint64_t before=other_bytes_hash();
    reset();assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_OK);
    assert(writes==8u && identities==3u && syncs==1u && acquires==1u && releases==1u);
    char check[4096];for(unsigned i=0u;i<8u;i++)direct_read(meta.sectors[i],1u,check+512u*i);
    assert(!memcmp(check,wanted,sizeof(check)));assert(disk_write(0,(const BYTE *)wanted,meta.sectors[0],1u)==RES_WRPRT);

    reset();masked=false;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!reads && !writes);
    reset();storage.stream.active=true;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!reads && !writes);
    reset();wanted[50]=0;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!reads && !writes);text(wanted,"new timing");
    reset();char wrong[512];memcpy(wrong,blank,sizeof(wrong));wrong[31]='f';direct_write(meta.sectors[0],1u,wrong);
    assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!writes && identities==1u);
    reset();struct kui_vmu_persist_profile p=meta.profile;p.geometry.sectors_per_cluster*=2u;
    assert(run(&p,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!writes);
    reset();manifest.extent_count=1u;manifest.slots[0].extent=(struct kui_retail_extent){.card_lba=meta.sectors[0],.blocks=1u};
    assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!writes);
    reset();p=meta.profile;p.files[0].allocated_count=1u;p.files[0].allocated[0]=(struct kui_vmu_persist_extent){0u,meta.sectors[7],1u};
    assert(run(&p,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!writes);
    reset();assert(run(&meta.profile,&meta.report,1u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!writes);
    reset();acquire_result=KUI_LOADER_SD_NOT_READY;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!reads && !writes);
    reset();acquire_result=KUI_LOADER_SD_TIMEOUT;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_CARD_FAULT);assert(!reads && !writes);
    for(unsigned i=1u;i<=3u;i++) {
        reset();bad_identity=i;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_CARD_FAULT);assert(writes==(i==3u?8u:0u));
    }
    reset();fail_read=1u;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_CARD_FAULT);assert(!writes);
    for(unsigned i=1u;i<=8u;i++) {
        reset();fail_write=i;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_CARD_FAULT);assert(writes==i);
    }
    reset();fail_sync=1u;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_CARD_FAULT);assert(writes==8u);
    reset();corrupt_readback=1u;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_CARD_FAULT);assert(writes==8u);
    reset();fail_release=1u;assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_CARD_FAULT);assert(writes==8u);
    if(meta.profile.geometry.fs_type==KUI_VMU_PERSIST_FAT32 && meta.profile.geometry.sectors_per_cluster==1u) {
        /* Cyclic data chain must not hang CREATE_LINKMAP: bounded seeks
         * identify the repeated report cluster before any writer is called. */
        reset();uint8_t fat[512],saved[512];uint32_t cluster=meta.clusters[1];
        uint32_t fat_lba=(uint32_t)meta.profile.geometry.fat_first_lba+cluster/128u;
        direct_read(fat_lba,1u,fat);memcpy(saved,fat,sizeof(fat));
        unsigned at=(cluster%128u)*4u;uint32_t previous=meta.clusters[0];
        for(unsigned j=0u;j<4u;j++)fat[at+j]=(uint8_t)(previous>>(8u*j));
        direct_write(fat_lba,1u,fat);
        assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!writes);
        direct_write(fat_lba,1u,saved);

        /* An unterminated directory cycling through its FAT has fresh disk
         * reads; the independent sector budget stops lookup safely. */
        reset();cluster=meta.directory_cluster;
        fat_lba=(uint32_t)meta.profile.geometry.fat_first_lba+cluster/128u;
        direct_read(fat_lba,1u,fat);memcpy(saved,fat,sizeof(fat));at=(cluster%128u)*4u;
        for(unsigned j=0u;j<4u;j++)fat[at+j]=(uint8_t)(cluster>>(8u*j));
        uint32_t directory=(uint32_t)meta.profile.geometry.data_first_lba+cluster-2u;
        uint8_t old_directory[512],deleted[512];direct_read(directory,1u,old_directory);memset(deleted,0xe5,sizeof(deleted));
        direct_write(fat_lba,1u,fat);direct_write(directory,1u,deleted);
        assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_UNAVAILABLE);assert(!writes && reads<=256u);
        direct_write(fat_lba,1u,saved);direct_write(directory,1u,old_directory);
    }
    reset();assert(run(&meta.profile,NULL,0u)==KUI_SMASH_PHASE_FILE_OK);
    assert(other_bytes_hash()==before);
    assert(!fclose(image));
    puts("PASS actual FatFs diagnostic mapping: eight report-data writes only; protected maps, build, identity, mask, CRC read, every write, sync, readback and release refusal");
    return 0;
}
#endif
