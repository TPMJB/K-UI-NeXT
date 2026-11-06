/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/smash_card_session.h"
#include "kui/smash_session_file.h"
#include "kui/retail_image.h"
#include "kui/smash_ce_profile.h"
#include "kui/vmu_backing.h"
#include "retail_storage.h"
#include "sci_sd_bus.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SESSION_BLOCKS (KUI_VMU_FS_WITNESS_FILE_BYTES/512u)
#define CAPSULES 6u
#ifdef KUI_SESSION_FILE_SEED
static const char *const capsule_paths[CAPSULES]={
    KUI_SMASH_MENU_CAPSULE_PATH,KUI_SMASH_MENU_VC2_CAPSULE_PATH,
    KUI_SMASH_MENU_SWIRL_CAPSULE_PATH,KUI_SMASH_MENU_OUTER_CAPSULE_PATH,
    KUI_SMASH_CE_VC2_SESSION,KUI_SMASH_CE_SWIRL_SESSION};
#endif
static FILE *image;
static const uint32_t card_blocks=96u*1024u*1024u/512u;
static const uint8_t file_prefix[16]={'K','U','I','F','S','W','0','1',1u,0u,0u,0u,0u,2u,0u,0u};
struct metadata {
    struct kui_vmu_persist_profile profile;
    struct kui_smash_menu_capsule_runs data,allocated,capsules[CAPSULES];
    uint32_t sectors[SESSION_BLOCKS],clusters[SESSION_BLOCKS],directory_cluster;
    uint32_t capsule_dir_lba,capsule_dir_offset;
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
#ifdef KUI_SESSION_FILE_SEED
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
static void map_file(FATFS *fs,FIL *file,struct kui_smash_menu_capsule_runs *data,
                     struct kui_smash_menu_capsule_runs *allocated) {
    uint32_t table[18]={18u};file->cltbl=table;assert(f_lseek(file,CREATE_LINKMAP)==FR_OK);file->cltbl=NULL;
    uint32_t blocks=0u;for(unsigned i=1u;i+1u<table[0];i+=2u)blocks+=table[i]*fs->csize;
    assert(kui_smash_menu_capsule_map(table,table[0],(uint32_t)(fs->database-fs->volbase),fs->csize,
        fs->n_fatent,(uint32_t)fs->volbase,card_blocks-(uint32_t)fs->volbase,blocks,allocated));
    assert(kui_smash_menu_capsule_map(table,table[0],(uint32_t)(fs->database-fs->volbase),fs->csize,
        fs->n_fatent,(uint32_t)fs->volbase,card_blocks-(uint32_t)fs->volbase,(uint32_t)((f_size(file)+511u)/512u),data));
}
int main(int argc,char **argv) {
    assert(argc==4);image=fopen(argv[1],"r+b");assert(image);
    FATFS fs;assert(f_mount(&fs,"0:",1u)==FR_OK);
    assert(f_mkdir("0:/KUI")==FR_OK);assert(f_mkdir("0:/KUI/apps")==FR_OK);
    assert(f_mkdir("0:/KUI/apps/games")==FR_OK);
    DIR dir;assert(f_opendir(&dir,"0:/KUI/apps/games")==FR_OK);meta.directory_cluster=dir.obj.sclust;
    assert(f_closedir(&dir)==FR_OK);
    uint8_t *blank=calloc(1u,KUI_VMU_FS_WITNESS_FILE_BYTES);assert(blank);
    memcpy(blank,file_prefix,sizeof(file_prefix));
    FIL file;UINT put;
    assert(f_open(&file,KUI_SMASH_CARD_WITNESS_PATH,FA_CREATE_ALWAYS|FA_WRITE|FA_READ)==FR_OK);
    if(!strcmp(argv[3],"two-runs")) {
        assert(fs.fs_type==FS_FAT32 && fs.csize==1u);
        const UINT first=288u*512u;
        assert(f_write(&file,blank,first,&put)==FR_OK && put==first);assert(f_sync(&file)==FR_OK);
        FIL spacer;assert(f_open(&spacer,"0:/KUI/apps/games/spacer.bin",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
        assert(f_write(&spacer,blank,4096u,&put)==FR_OK && put==4096u);assert(f_close(&spacer)==FR_OK);
        assert(f_write(&file,blank+first,KUI_VMU_FS_WITNESS_FILE_BYTES-first,&put)==FR_OK && put==KUI_VMU_FS_WITNESS_FILE_BYTES-first);
    } else {
        assert(!strcmp(argv[3],"one-run"));assert(f_expand(&file,KUI_VMU_FS_WITNESS_FILE_BYTES,1u)==FR_OK);
        assert(f_write(&file,blank,KUI_VMU_FS_WITNESS_FILE_BYTES,&put)==FR_OK && put==KUI_VMU_FS_WITNESS_FILE_BYTES);
    }
    assert(f_sync(&file)==FR_OK);
    for(unsigned i=0;i<SESSION_BLOCKS;i++) {
        assert(f_lseek(&file,512u*i+1u)==FR_OK);meta.sectors[i]=(uint32_t)file.sect;meta.clusters[i]=file.clust;
    }
    map_file(&fs,&file,&meta.data,&meta.allocated);
    assert(!!meta.data.second_lba==!strcmp(argv[3],"two-runs"));assert(meta.data.blocks==SESSION_BLOCKS);
    assert(f_close(&file)==FR_OK);
    for(unsigned i=0u;i<CAPSULES;i++) {
        assert(f_open(&file,capsule_paths[i],FA_CREATE_ALWAYS|FA_WRITE|FA_READ)==FR_OK);
        assert(f_expand(&file,4096u,1u)==FR_OK);
        memset(blank,(int)('A'+i),4096u);
        assert(f_write(&file,blank,4096u,&put)==FR_OK && put==4096u);assert(f_sync(&file)==FR_OK);
        struct kui_smash_menu_capsule_runs useful;
        map_file(&fs,&file,&useful,&meta.capsules[i]);
        if(!i && fs.fs_type==FS_FAT32) {
            meta.capsule_dir_lba=(uint32_t)file.dir_sect;
            meta.capsule_dir_offset=(uint32_t)(file.dir_ptr-fs.win);
            assert(meta.capsule_dir_offset<=480u);
        }
        assert(f_close(&file)==FR_OK);
    }
    meta.profile.card_sectors=card_blocks;meta.profile.partition_first=fs.volbase;meta.profile.partition_end=card_blocks;
    meta.profile.high_capacity=1u;meta.profile.transport=1u;
    for(unsigned i=0;i<16u;i++)meta.profile.cid[i]=(uint8_t)(13u*i+7u);
    meta.profile.geometry=(struct kui_vmu_persist_geometry){.fs_type=fs.fs_type==FS_EXFAT?KUI_VMU_PERSIST_EXFAT:KUI_VMU_PERSIST_FAT32,
        .sector_bytes=512u,.sectors_per_cluster=fs.csize,.fat_count=fs.n_fats,.fat_first_lba=fs.fatbase,
        .fat_sectors=fs.fsize,.data_first_lba=fs.database,.cluster_count=fs.n_fatent-2u,.root_cluster=(uint32_t)fs.dirbase};
    assert(f_mount(NULL,"0:",0u)==FR_OK);free(blank);
    assert(!fclose(image));FILE *out=fopen(argv[2],"wb");assert(out);assert(fwrite(&meta,sizeof(meta),1u,out)==1u);assert(!fclose(out));
    puts("seeded actual filesystem session and six protected capsule allocations");return 0;
}
#else
static struct kui_retail_storage storage;
static struct kui_retail_manifest manifest;
static struct kui_smash_card_config config;
static unsigned reads,writes,identities,acquires,releases,syncs,multi_reads;
static unsigned bad_identity,fail_read,fail_write,fail_sync,fail_release,corrupt_readback;
static bool masked=true,leased,healthy=true,fault,fail_multi;
static enum kui_loader_sd_result acquire_result;
static struct kui_vmu_fs_witness_capture capture;
static uint8_t header[512],loaded_header[512];
static struct kui_vmu_fs_witness_record loaded_records[KUI_VMU_FS_WITNESS_MAX_RECORDS];
static unsigned meaningful_blocks(uint32_t count) {return 1u+(count*36u+511u)/512u;}
static uint64_t other_bytes_hash(void) {
    uint64_t hash=UINT64_C(1469598103934665603);uint8_t bytes[512];
    for(uint32_t lba=0u;lba<card_blocks;lba++) {
        bool session=false;
        for(unsigned i=0u;i<SESSION_BLOCKS;i++)if(meta.sectors[i]==lba) {session=true;break;}
        if(session)continue;
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
    assert(card==&storage.device.sd && leased && count && count<=32u && !((uintptr_t)out&31u));
    unsigned first=reads+1u;reads+=count;
    if(fail_read>=first && fail_read<=reads) {card->ready=false;return KUI_LOADER_SD_CRC;}
    direct_read(lba,count,out);
    if(corrupt_readback && writes && lba==meta.sectors[0])((uint8_t *)out)[50]^=1u;
    return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_loader_sd_read_multi(struct kui_loader_sd *card,uint32_t lba,uint32_t count,void *out) {
    assert(count && count<=32u);unsigned logical=SESSION_BLOCKS;
    for(unsigned i=0u;i<SESSION_BLOCKS;i++)if(meta.sectors[i]==lba) {logical=i;break;}
    assert(logical<SESSION_BLOCKS && count<=SESSION_BLOCKS-logical);
    for(unsigned i=0u;i<count;i++)assert(meta.sectors[logical+i]==lba+i);
    ++multi_reads;
    if(fail_multi) {card->ready=false;return KUI_LOADER_SD_CRC;}
    return kui_loader_sd_read(card,lba,count,out);
}
int kui_vmu_sd_identity_read(struct kui_loader_sd *card,struct kui_vmu_sd_identity *out) {
    assert(card==&storage.device.sd && leased);++identities;
    memcpy(out->cid,meta.profile.cid,sizeof(out->cid));out->sectors=card_blocks;out->high_capacity=true;
    if(identities==bad_identity)out->cid[3]^=1u;
    return KUI_VMU_BACKING_OK;
}
enum kui_loader_sd_result kui_sci_sd_write(struct kui_loader_sd *card,uint32_t lba,uint32_t count,const uint8_t *in) {
    assert(card==&storage.device.sd && leased && !card->bus.transfer_block && count && count<=32u);
    assert(writes+count<=SESSION_BLOCKS);
    for(unsigned i=0u;i<count;i++)assert(lba+i==meta.sectors[writes+i]);
    unsigned first=writes+1u;writes+=count;
    if(fail_write>=first && fail_write<=writes) {card->ready=false;return KUI_LOADER_SD_TIMEOUT;}
    direct_write(lba,count,in);return KUI_LOADER_SD_OK;
}
enum kui_loader_sd_result kui_sci_sd_sync(struct kui_loader_sd *card) {
    assert(card==&storage.device.sd && leased);++syncs;
    if(fail_sync) {card->ready=false;return KUI_LOADER_SD_TIMEOUT;}return KUI_LOADER_SD_OK;
}
static bool unused_block(void *a,const uint8_t *b,uint8_t *c,size_t d,bool e,uint16_t *f) {
    (void)a;(void)b;(void)c;(void)d;(void)e;(void)f;abort();
}
static void seal_config(void) {
    config.crc32=0u;
    config.crc32=kui_retail_crc32(0u,&config,sizeof(config));
    assert(kui_smash_card_config_valid(&config));
}
static void reset(void) {
    assert(!leased);
    storage=(struct kui_retail_storage){.transport=KUI_STORAGE_SCI};
    storage.device.sd=(struct kui_loader_sd){.ready=true,.blocks=card_blocks,.high_capacity=true};
    storage.device.sd.bus.transfer_block=unused_block;
    manifest=(struct kui_retail_manifest){.card_sectors=card_blocks,.partition_start=(uint32_t)meta.profile.partition_first,
        .partition_end=(uint32_t)meta.profile.partition_end};
    config=(struct kui_smash_card_config){.magic0=KUI_SMASH_CARD_CONFIG_MAGIC0,.magic1=KUI_SMASH_CARD_CONFIG_MAGIC1,
        .version=1u,.bytes=sizeof(config),.role=KUI_SMASH_CARD_ROLE_NATIVE,.generation=7u,
        .witness_bytes=KUI_VMU_FS_WITNESS_FILE_BYTES,.data=meta.data,.allocated=meta.allocated};
    memcpy(config.build,"0123456789ab",12u);
    memset(config.ownership_sha256,0x83,sizeof(config.ownership_sha256));seal_config();
    reads=writes=identities=acquires=releases=syncs=multi_reads=0u;
    bad_identity=fail_read=fail_write=fail_sync=fail_release=corrupt_readback=0u;
    masked=healthy=true;fault=fail_multi=false;acquire_result=KUI_LOADER_SD_OK;
    uint8_t blank[512];memset(blank,0xcc,sizeof(blank));
    for(unsigned i=0u;i<SESSION_BLOCKS;i++) {
        if(!i)memcpy(blank,file_prefix,sizeof(file_prefix));
        direct_write(meta.sectors[i],1u,blank);
        if(!i)memset(blank,0xcc,sizeof(blank));
    }
}
static void check_result(enum kui_smash_session_file_result result,bool borrowed,
    const struct kui_smash_phase_file_stats *stats) {
    assert(leased==borrowed && stats->writes==writes && stats->identities==identities);
    assert(storage.device.sd.bus.transfer_block==unused_block);
    if(result==KUI_SMASH_SESSION_FILE_CARD_FAULT)assert(!storage.device.sd.ready);
    else assert(storage.device.sd.ready);
    if(borrowed)assert(!acquires && !releases);
    else assert(releases<=acquires && acquires-releases<=1u);
    leased=false; /* The test owner, not the engine, ends a borrowed lease. */
}
static enum kui_smash_session_file_result store(const struct kui_vmu_persist_profile *p,uint32_t count,bool borrowed) {
    assert(!leased);leased=borrowed;struct kui_smash_phase_file_stats stats;
    enum kui_smash_session_file_result result=kui_smash_session_file_store(&storage,&manifest,p,&config,
        borrowed,header,capture.records,count,&stats);
    check_result(result,borrowed,&stats);return result;
}
static enum kui_smash_session_file_result load(size_t capacity,uint32_t *count,bool borrowed) {
    assert(!leased);leased=borrowed;struct kui_smash_phase_file_stats stats;
    enum kui_smash_session_file_result result=kui_smash_session_file_load(&storage,&manifest,&meta.profile,&config,
        borrowed,loaded_header,loaded_records,capacity,count,&stats);
    check_result(result,borrowed,&stats);if(result)assert(!*count);return result;
}
static int capture_read(void *ctx,uint32_t lba,uint8_t out[512]) {
    (void)ctx;direct_read(lba,1u,out);return 0;
}
static void make_witness(uint32_t count) {
    struct kui_vmu_fs_witness_binding binding={.card_sectors=card_blocks,
        .partition_first=meta.profile.partition_first,.partition_end=meta.profile.partition_end,
        .high_capacity=1u,.generation=7u};
    memcpy(binding.cid,meta.profile.cid,16u);memset(binding.image_id,0x59,16u);memset(binding.binding_sha256,0x83,32u);
    assert(kui_vmu_fs_witness_capture_begin(&capture,&binding,capture_read,NULL)==0);
    uint8_t sector[512],digest[32];
    for(uint32_t i=0u;i<count;i++)assert(kui_vmu_fs_witness_capture_read(&capture,card_blocks-9000u+i,sector)==0);
    assert(kui_vmu_fs_witness_capture_finish(&capture,0,header,digest)==0 && capture.count==count);
    assert(kui_vmu_fs_witness_validate(header,capture.records,count*sizeof(capture.records[0]),&binding,digest)==0);
}
static void verify_written(uint32_t count) {
    uint8_t sector[512];unsigned blocks=meaningful_blocks(count);size_t record_bytes=count*36u;
    for(unsigned i=0u;i<SESSION_BLOCKS;i++) {
        direct_read(meta.sectors[i],1u,sector);
        if(!i)assert(!memcmp(sector,header,512u));
        else if(i<blocks)for(unsigned j=0u;j<512u;j++) {
            size_t at=(size_t)(i-1u)*512u+j;
            assert(sector[j]==(at<record_bytes?((const uint8_t *)capture.records)[at]:0u));
        } else for(unsigned j=0u;j<512u;j++)assert(sector[j]==0xccu);
    }
}
int main(int argc,char **argv) {
    assert(argc==3);image=fopen(argv[1],"r+b");assert(image);
    FILE *in=fopen(argv[2],"rb");assert(in);assert(fread(&meta,sizeof(meta),1u,in)==1u);assert(!fclose(in));
    uint64_t before=other_bytes_hash();uint32_t count;
    reset();make_witness(15u);assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_OK);
    assert(writes==3u && syncs==1u && identities>=2u && acquires==1u && releases==1u);verify_written(15u);
    writes=reads=identities=acquires=releases=syncs=0u;
    assert(load(15u,&count,false)==KUI_SMASH_SESSION_FILE_OK && count==15u);
    assert(!writes && !syncs && !memcmp(header,loaded_header,512u));
    assert(!memcmp(capture.records,loaded_records,15u*36u));
    assert(disk_write(0,header,meta.sectors[0],1u)==RES_WRPRT);
    uint8_t bad_padding[512];direct_read(meta.sectors[2],1u,bad_padding);bad_padding[28]=0x55u;
    direct_write(meta.sectors[2],1u,bad_padding);writes=reads=identities=acquires=releases=syncs=0u;
    assert(load(15u,&count,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
    reset();make_witness(15u);assert(store(&meta.profile,15u,true)==KUI_SMASH_SESSION_FILE_OK);verify_written(15u);
    writes=reads=identities=acquires=releases=syncs=0u;
    assert(load(15u,&count,true)==KUI_SMASH_SESSION_FILE_OK && count==15u);
    reset();make_witness(KUI_VMU_FS_WITNESS_MAX_RECORDS);
    assert(store(&meta.profile,KUI_VMU_FS_WITNESS_MAX_RECORDS,false)==KUI_SMASH_SESSION_FILE_OK);
    assert(writes==SESSION_BLOCKS && multi_reads);verify_written(KUI_VMU_FS_WITNESS_MAX_RECORDS);
    writes=reads=identities=acquires=releases=syncs=0u;
    assert(load(KUI_VMU_FS_WITNESS_MAX_RECORDS,&count,false)==KUI_SMASH_SESSION_FILE_OK && count==KUI_VMU_FS_WITNESS_MAX_RECORDS);
    assert(!memcmp(capture.records,loaded_records,sizeof(loaded_records)));
    reset();make_witness(15u);config.data.first_lba++;config.allocated.first_lba++;seal_config();
    assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
    reset();config.data.first_lba++;config.allocated.first_lba++;seal_config();
    assert(store(&meta.profile,15u,true)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
    reset();config.allocated.blocks++;
    if(!config.allocated.second_lba)config.allocated.first_blocks++;
    seal_config();
    assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
    reset();manifest.extent_count=1u;manifest.slots[0].extent=(struct kui_retail_extent){.card_lba=meta.sectors[0],.blocks=1u};
    assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
    reset();struct kui_vmu_persist_profile p=meta.profile;p.files[0].allocated_count=1u;
    uint32_t last_allocated=meta.allocated.second_lba?
        meta.allocated.second_lba+meta.allocated.blocks-meta.allocated.first_blocks-1u:
        meta.allocated.first_lba+meta.allocated.blocks-1u;
    p.files[0].allocated[0]=(struct kui_vmu_persist_extent){0u,last_allocated,1u};
    assert(store(&p,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
    reset();masked=false;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !reads && !writes);
    reset();storage.stream.active=true;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !reads && !writes);
    reset();assert(store(&meta.profile,14u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !reads && !writes);
    reset();header[0]^=1u;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !reads && !writes);header[0]^=1u;
    reset();uint8_t first[512];direct_read(meta.sectors[0],1u,first);first[0]^=1u;direct_write(meta.sectors[0],1u,first);
    assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
    reset();p=meta.profile;p.geometry.sectors_per_cluster*=2u;
    assert(store(&p,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
    reset();acquire_result=KUI_LOADER_SD_NOT_READY;
    assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !reads && !writes);
    reset();bad_identity=1u;assert(store(&meta.profile,15u,true)==KUI_SMASH_SESSION_FILE_CARD_FAULT && !writes);
    reset();bad_identity=2u;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_CARD_FAULT && !writes);
    reset();bad_identity=3u;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_CARD_FAULT && writes==3u);
    reset();fail_read=1u;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_CARD_FAULT && !writes);
    reset();fail_write=2u;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_CARD_FAULT && writes>=2u);
    reset();fail_multi=true;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_CARD_FAULT && writes==3u);
    reset();fail_sync=1u;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_CARD_FAULT && writes==3u);
    reset();corrupt_readback=1u;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_CARD_FAULT && writes==3u);
    reset();fail_release=1u;assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_CARD_FAULT && writes==3u);
    reset();assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_OK);
    writes=reads=identities=acquires=releases=syncs=0u;
    assert(load(14u,&count,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
    if(meta.profile.geometry.fs_type==KUI_VMU_PERSIST_FAT32 && meta.profile.geometry.sectors_per_cluster==1u) {
        reset();uint8_t fat[512],saved[512];uint32_t cluster=meta.clusters[1];
        uint32_t fat_lba=(uint32_t)meta.profile.geometry.fat_first_lba+cluster/128u;
        direct_read(fat_lba,1u,fat);memcpy(saved,fat,512u);unsigned at=(cluster%128u)*4u;
        for(unsigned i=0u;i<4u;i++)fat[at+i]=(uint8_t)(meta.clusters[0]>>(8u*i));
        direct_write(fat_lba,1u,fat);
        assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);direct_write(fat_lba,1u,saved);
        reset();cluster=meta.clusters[SESSION_BLOCKS-1u];
        fat_lba=(uint32_t)meta.profile.geometry.fat_first_lba+cluster/128u;
        direct_read(fat_lba,1u,fat);memcpy(saved,fat,512u);at=(cluster%128u)*4u;
        for(unsigned i=0u;i<4u;i++)fat[at+i]=(uint8_t)(meta.clusters[0]>>(8u*i));
        direct_write(fat_lba,1u,fat);
        assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);direct_write(fat_lba,1u,saved);
        reset();uint8_t old_entry[512],entry[512];direct_read(meta.capsule_dir_lba,1u,entry);memcpy(old_entry,entry,512u);
        at=meta.capsule_dir_offset;cluster=meta.clusters[0];
        entry[at+20u]=(uint8_t)(cluster>>16);entry[at+21u]=(uint8_t)(cluster>>24);
        entry[at+26u]=(uint8_t)cluster;entry[at+27u]=(uint8_t)(cluster>>8);
        direct_write(meta.capsule_dir_lba,1u,entry);
        assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);direct_write(meta.capsule_dir_lba,1u,old_entry);
        reset();cluster=meta.directory_cluster;fat_lba=(uint32_t)meta.profile.geometry.fat_first_lba+cluster/128u;
        direct_read(fat_lba,1u,fat);memcpy(saved,fat,512u);at=(cluster%128u)*4u;
        for(unsigned i=0u;i<4u;i++)fat[at+i]=(uint8_t)(cluster>>(8u*i));
        uint32_t directory=(uint32_t)meta.profile.geometry.data_first_lba+cluster-2u;
        uint8_t old_directory[512],deleted[512];direct_read(directory,1u,old_directory);memset(deleted,0xe5,512u);
        direct_write(fat_lba,1u,fat);direct_write(directory,1u,deleted);
        assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_UNAVAILABLE && !writes);
        assert(reads<=8192u);direct_write(fat_lba,1u,saved);direct_write(directory,1u,old_directory);
    }
    reset();assert(store(&meta.profile,15u,false)==KUI_SMASH_SESSION_FILE_OK);verify_written(15u);
    assert(other_bytes_hash()==before);assert(!fclose(image));
    puts("PASS real FatFs session file: maps, six capsules, protected allocations, cycles/budget, borrowed leases, partial/full records and raw-only writes");
    return 0;
}
#endif
