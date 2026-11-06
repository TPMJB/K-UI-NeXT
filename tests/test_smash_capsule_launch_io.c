/* SPDX-License-Identifier: GPL-3.0-only */
/* Real FatFs six-file preparation. Only the optional scratch allocator is
 * controlled; allocation maps, comparisons, writes and readback are production. */
#include "ff.h"
#include "diskio.h"
#include "kui/core.h"
#include "kui/hash.h"
#include "kui/media.h"
#include "kui/runtime.h"
#include "kui/smash_menu_capsule.h"
#include "kui/smash_ce_profile.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do {if(!(x)){fprintf(stderr,"%s:%d: %s (%s)\n",__FILE__,__LINE__,#x,problem?problem:"");exit(1);}} while(0)
#define LINK_MAP_WORDS 32u
static DWORD link_map[LINK_MAP_WORDS];
static bool wide;
static void *scratch_allocate(size_t bytes) {return wide?malloc(bytes):NULL;}
#define malloc scratch_allocate
#include "../src/apps/smash_menu_capsule_file_prepare.inc"
#undef malloc

enum { FILES=6 };
static const char *const paths[FILES]={KUI_SMASH_MENU_CAPSULE_PATH,KUI_SMASH_MENU_VC2_CAPSULE_PATH,
    KUI_SMASH_MENU_SWIRL_CAPSULE_PATH,KUI_SMASH_MENU_OUTER_CAPSULE_PATH,
    KUI_SMASH_CE_VC2_SESSION,KUI_SMASH_CE_SWIRL_SESSION};
static const uint32_t sizes[FILES]={246272u,94720u,94720u,63488u,50688u,50688u};
static uint8_t *disk,*payload[FILES];
static size_t disk_bytes;
static FATFS fs;
static struct kui_volume volume;
static const char *problem;
static bool observing;
static uint64_t reads,read_sectors,writes,write_sectors,syncs;
static struct kui_smash_menu_capsule_runs payload_runs[FILES];
static uint64_t payload_reads,payload_read_sectors,payload_writes,payload_write_sectors;

static bool payload_span(uint32_t lba,size_t blocks) {
    for(unsigned i=0;i<FILES;i++) {
        const struct kui_smash_menu_capsule_runs *r=&payload_runs[i];
        if(r->first_blocks && lba>=r->first_lba && (uint64_t)lba+blocks<=(uint64_t)r->first_lba+r->first_blocks)return true;
        if(r->blocks>r->first_blocks && lba>=r->second_lba &&
           (uint64_t)lba+blocks<=(uint64_t)r->second_lba+r->blocks-r->first_blocks)return true;
    }
    return false;
}
static uint64_t device_blocks(void *context) {(void)context;return disk_bytes/512u;}
static int device_read(void *context,uint32_t lba,size_t blocks,uint8_t *out) {
    (void)context;if((uint64_t)lba+blocks>disk_bytes/512u)return -1;
    if(observing) {++reads;read_sectors+=blocks;
        if(payload_span(lba,blocks)){++payload_reads;payload_read_sectors+=blocks;}}
    memcpy(out,disk+(size_t)lba*512u,blocks*512u);return 0;
}
static int device_write(void *context,uint32_t lba,size_t blocks,const uint8_t *data) {
    (void)context;if((uint64_t)lba+blocks>disk_bytes/512u)return -1;
    if(observing) {++writes;write_sectors+=blocks;
        if(payload_span(lba,blocks)){++payload_writes;payload_write_sectors+=blocks;}}
    memcpy(disk+(size_t)lba*512u,data,blocks*512u);return 0;
}
static int device_sync(void *context) {(void)context;if(observing)++syncs;return 0;}
static const struct kui_media_ops media={NULL,device_blocks,device_read,device_write,device_sync};
static bool cancel(void) {return false;}
static void log_line(const char *format,...) {(void)format;}
static void put32(uint8_t *p,uint32_t v) {for(unsigned i=0;i<4u;i++)p[i]=(uint8_t)(v>>(i*8u));}
static void mount_volume(void) {
    kui_media_set(&media);CHECK(f_mount(&fs,"0:",1u)==FR_OK);
    volume=*kui_media_volume();CHECK(volume.partitioned && volume.start==2048u);
}
static void unmount_volume(void) {CHECK(f_mount(NULL,"0:",0u)==FR_OK);}
static void reset_counts(void) {
    reads=read_sectors=writes=write_sectors=syncs=0u;
    payload_reads=payload_read_sectors=payload_writes=payload_write_sectors=0u;
}
static void format_volume(BYTE kind,DWORD cluster) {
    disk_bytes=(2048u+131072u+2048u)*512u;disk=calloc(1u,disk_bytes);CHECK(disk);
    disk[446u+4u]=kind==FM_FAT32?0x0cu:7u;
    put32(disk+446u+8u,2048u);put32(disk+446u+12u,131072u);
    disk[510]=0x55;disk[511]=0xaa;kui_media_set(&media);
    MKFS_PARM parameters={.fmt=(BYTE)(kind|FM_SFD),.n_fat=1u,.align=1u,.n_root=512u,.au_size=cluster};
    uint8_t scratch[8192];CHECK(f_mkfs("0:",&parameters,scratch,sizeof(scratch))==FR_OK);
    if(kind==FM_FAT32) {
        uint8_t *boot=disk+2048u*512u;unsigned backup=boot[50u]|(unsigned)boot[51u]<<8;
        put32(boot+28u,2048u);CHECK(backup && backup<32u);put32(boot+(size_t)backup*512u+28u,2048u);
    }
    mount_volume();
    CHECK(fs.csize*512u==cluster && fs.fs_type==(kind==FM_FAT32?FS_FAT32:FS_EXFAT));
    CHECK(f_mkdir("0:/KUI")==FR_OK && f_mkdir("0:/KUI/apps")==FR_OK && f_mkdir("0:/KUI/apps/games")==FR_OK);
}
static void watch_payloads(void) {
    memset(payload_runs,0,sizeof(payload_runs));
    for(unsigned i=0;i<FILES;i++) {
        FIL file;CHECK(f_open(&file,paths[i],FA_READ)==FR_OK);
        CHECK(smash_menu_capsule_file_map(&file,&fs,&volume,sizes[i],&payload_runs[i]));
        CHECK(f_close(&file)==FR_OK);
    }
}
static void prepare_files(void) {
    struct kui_retail_manifest map={.partition_start=volume.start,
        .partition_end=(uint64_t)volume.start+volume.count};
    struct kui_vmu_persist_profile profile={0};
    struct kui_smash_menu_capsule_runs allocated[FILES]={{0}};
    uint8_t page[4096];
    for(unsigned i=0;i<FILES;i++) {
        CHECK(smash_menu_capsule_file_inspect(paths[i],&fs,&volume,&map,&profile,&allocated[i],&problem,cancel)>=0);
        for(unsigned j=0;j<i;j++)if(allocated[i].blocks && allocated[j].blocks)
            CHECK(kui_smash_menu_capsule_runs_disjoint(&allocated[i],&allocated[j]));
    }
    for(unsigned i=0;i<FILES;i++) {
        struct kui_smash_menu_capsule_runs forbidden[FILES-1u];size_t count=0u;
        for(unsigned j=0;j<FILES;j++)if(i!=j && allocated[j].blocks)forbidden[count++]=allocated[j];
        struct kui_smash_menu_capsule_prepared prepared={0};
        CHECK(smash_menu_capsule_file_prepare_many(&fs,&volume,&map,&profile,paths[i],payload[i],sizes[i],page,
            i>=4u?49152u:sizes[i],forbidden,count,&prepared,&problem,log_line,cancel));
        allocated[i]=prepared.allocated_runs;
    }
}
static void mutate_last_immutable_bytes(void) {
    for(unsigned i=0;i<FILES;i++) {
        FIL file;UINT put=0u;uint32_t at=(i>=4u?49152u:sizes[i])-1u;
        uint8_t changed=(uint8_t)(payload[i][at]^1u);
        CHECK(f_open(&file,paths[i],FA_WRITE)==FR_OK && f_lseek(&file,at)==FR_OK);
        CHECK(f_write(&file,&changed,1u,&put)==FR_OK && put==1u && f_sync(&file)==FR_OK && f_close(&file)==FR_OK);
    }
}
static void verify_payloads(void) {
    uint8_t *readback=malloc(sizes[0]);CHECK(readback);
    for(unsigned i=0;i<FILES;i++) {
        FIL file;UINT got=0u;CHECK(f_open(&file,paths[i],FA_READ)==FR_OK && f_size(&file)==sizes[i]);
        CHECK(f_read(&file,readback,sizes[i],&got)==FR_OK && got==sizes[i] && !memcmp(readback,payload[i],sizes[i]));
        CHECK(f_close(&file)==FR_OK);
    }
    free(readback);
}
static void report(BYTE kind,DWORD cluster,const char *scenario) {
    struct kui_sha256 hash;uint8_t digest[32];char hex[65];
    kui_sha256_init(&hash);kui_sha256_update(&hash,disk,disk_bytes);kui_sha256_digest(&hash,digest);kui_hex(digest,32u,hex);
    printf("{\"filesystem\":\"%s\",\"cluster_bytes\":%u,\"window_bytes\":%u,\"scenario\":\"%s\","
        "\"reads\":%" PRIu64 ",\"read_sectors\":%" PRIu64 ",\"writes\":%" PRIu64 ",\"write_sectors\":%" PRIu64
        ",\"syncs\":%" PRIu64 ",\"payload_reads\":%" PRIu64 ",\"payload_read_sectors\":%" PRIu64
        ",\"payload_writes\":%" PRIu64 ",\"payload_write_sectors\":%" PRIu64 ",\"disk_sha256\":\"%s\"}\n",
        kind==FM_FAT32?"FAT32":"exFAT",(unsigned)cluster,wide?32768u:4096u,scenario,
        reads,read_sectors,writes,write_sectors,syncs,payload_reads,payload_read_sectors,payload_writes,payload_write_sectors,hex);
}
static void trial(BYTE kind,DWORD cluster,bool use_wide) {
    wide=use_wide;memset(payload_runs,0,sizeof(payload_runs));format_volume(kind,cluster);
    const char *const scenarios[]={"fresh","reuse","changed"};
    for(unsigned i=0;i<3u;i++) {
        if(i==2u)mutate_last_immutable_bytes();
        if(i)watch_payloads();
        reset_counts();observing=true;prepare_files();observing=false;
        verify_payloads();report(kind,cluster,scenarios[i]);
        unmount_volume();mount_volume();
    }
    unmount_volume();free(disk);disk=NULL;
}
int main(void) {
    for(unsigned i=0;i<FILES;i++) {
        payload[i]=malloc(sizes[i]);CHECK(payload[i]);
        for(uint32_t at=0u;at<sizes[i];at++)payload[i][at]=(uint8_t)((at*37u)^(at>>7)^(i*0x5bu));
    }
    for(unsigned window=0u;window<2u;window++) {
        trial(FM_FAT32,512u,window!=0u);trial(FM_EXFAT,4096u,window!=0u);trial(FM_EXFAT,131072u,window!=0u);
    }
    for(unsigned i=0;i<FILES;i++)free(payload[i]);
    return 0;
}
