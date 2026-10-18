/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent sparse FAT32 fixture: whole-cluster ownership, not file-sector
 * bounds, is the oracle. No FatFs formatter or owner fixture is reused. */
#include "kui/vmu_fs_admission.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PARTITION 2048u
#define RESERVED 32u
#define FAT_SECTORS 513u
#define CLUSTERS 65536u
#define SPC 8u
#define VOLUME_SECTORS (RESERVED+FAT_SECTORS+CLUSTERS*SPC)
#define DATA_FIRST (PARTITION+RESERVED+FAT_SECTORS)
#define BASE_FIRST 6u
#define BASE_LAST 37u
#define JOURNAL 38u
#define IDENTITY 39u
#define EOC UINT32_C(0x0fffffff)

static struct {
    struct kui_vmu_fs_input input;
    struct kui_vmu_fs_workspace work;
    struct kui_vmu_persist_profile profile;
    uint32_t fat[CLUSTERS+2u];
    uint8_t boot[512],dirs[4][512],identity[512];
    unsigned reads,outside;
} disk;

static void put16(uint8_t *p,uint16_t value) {
    p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);
}
static void put32(uint8_t *p,uint32_t value) {
    put16(p,(uint16_t)value);put16(p+2u,(uint16_t)(value>>16));
}
static uint32_t cluster_lba(uint32_t cluster) {return DATA_FIRST+(cluster-2u)*SPC;}

static int read_sector(void *context,uint32_t lba,uint8_t out[512]) {
    assert(context==&disk);
    ++disk.reads;
    if(!lba) {
        memset(out,0,512u);out[446u+4u]=0x0cu;
        put32(out+446u+8u,PARTITION);put32(out+446u+12u,VOLUME_SECTORS);
        put16(out+510u,0xaa55u);return 0;
    }
    if(lba<PARTITION || lba>=PARTITION+VOLUME_SECTORS) {
        ++disk.outside;return -1;
    }
    memset(out,0,512u);
    if(lba==PARTITION || lba==PARTITION+6u) memcpy(out,disk.boot,512u);
    else if(lba==PARTITION+1u || lba==PARTITION+7u) {
        put32(out,UINT32_C(0x41615252));put32(out+484u,UINT32_C(0x61417272));
        put32(out+488u,UINT32_MAX);put32(out+492u,UINT32_MAX);
        put32(out+508u,UINT32_C(0xaa550000));
    } else if(lba>=PARTITION+RESERVED && lba<PARTITION+RESERVED+FAT_SECTORS) {
        size_t first=(size_t)(lba-PARTITION-RESERVED)*128u;
        for(size_t i=0;i<128u && first+i<CLUSTERS+2u;i++) put32(out+4u*i,disk.fat[first+i]);
    } else {
        for(unsigned i=0;i<4u;i++)
            if(lba==cluster_lba(2u+i)) memcpy(out,disk.dirs[i],512u);
        if(lba==cluster_lba(IDENTITY)) memcpy(out,disk.identity,512u);
    }
    return 0;
}

static void short_entry(uint8_t *entry,const char name[11],unsigned attributes,
    uint32_t first,uint32_t bytes) {
    memset(entry,0,32u);memcpy(entry,name,11u);entry[11]=(uint8_t)attributes;
    put16(entry+20u,(uint16_t)(first>>16));put16(entry+26u,(uint16_t)first);
    put32(entry+28u,bytes);
}
static uint8_t short_checksum(const uint8_t name[11]) {
    uint8_t sum=0;
    for(unsigned i=0;i<11u;i++) sum=(uint8_t)((sum>>1)|(sum<<7))+name[i];
    return sum;
}
static void journal_entries(uint8_t *lfn) {
    static const unsigned offsets[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    static const char name[]="card.journal";
    short_entry(lfn+32u,"CARD~1  JOU",0x20u,JOURNAL,1024u);
    memset(lfn,0xff,32u);lfn[0]=0x41u;lfn[11]=0x0fu;lfn[12]=0;
    lfn[13]=short_checksum(lfn+32u);put16(lfn+26u,0);
    for(unsigned i=0;i<13u;i++) put16(lfn+offsets[i],(uint16_t)(unsigned char)name[i]);
}
static void fresh(void) {
    memset(&disk,0,sizeof(disk));
    disk.input=(struct kui_vmu_fs_input){.read_sector=read_sector,.context=&disk,
        .card_sectors=PARTITION+VOLUME_SECTORS,.partition_first=PARTITION,
        .partition_end=PARTITION+VOLUME_SECTORS,.high_capacity=1u,
        .pair_path="/KUI/vmu-test/p0001"};
    for(unsigned i=0;i<16u;i++) {disk.input.cid[i]=(uint8_t)(0x31u+i);disk.input.image_id[i]=(uint8_t)(0x91u+i);}
    memcpy(disk.identity,disk.input.image_id,16u);
    uint8_t *b=disk.boot;b[0]=0xebu;b[1]=0x58u;b[2]=0x90u;
    memcpy(b+3u,"MSDOS5.0",8u);put16(b+11u,512u);b[13]=SPC;put16(b+14u,RESERVED);
    b[16]=1u;b[21]=0xf8u;put16(b+24u,63u);put16(b+26u,255u);put32(b+28u,PARTITION);
    put32(b+32u,VOLUME_SECTORS);put32(b+36u,FAT_SECTORS);put32(b+44u,2u);
    put16(b+48u,1u);put16(b+50u,6u);b[64]=0x80u;b[66]=0x29u;
    put32(b+67u,UINT32_C(0xa1b2c3d4));memcpy(b+71u,"NO NAME    ",11u);
    memcpy(b+82u,"FAT32   ",8u);put16(b+510u,0xaa55u);
    disk.fat[0]=UINT32_C(0x0ffffff8);disk.fat[1]=EOC;
    for(unsigned i=2u;i<=IDENTITY;i++) disk.fat[i]=EOC;
    for(unsigned i=BASE_FIRST;i<BASE_LAST;i++) disk.fat[i]=i+1u;
    short_entry(disk.dirs[0],"KUI        ",0x10u,3u,0u);
    short_entry(disk.dirs[1],".          ",0x10u,3u,0u);
    short_entry(disk.dirs[1]+32u,"..         ",0x10u,0u,0u);
    short_entry(disk.dirs[1]+64u,"VMU-TEST   ",0x10u,4u,0u);
    short_entry(disk.dirs[2],".          ",0x10u,4u,0u);
    short_entry(disk.dirs[2]+32u,"..         ",0x10u,3u,0u);
    short_entry(disk.dirs[2]+64u,"P0001      ",0x10u,5u,0u);
    short_entry(disk.dirs[3],".          ",0x10u,5u,0u);
    short_entry(disk.dirs[3]+32u,"..         ",0x10u,4u,0u);
    short_entry(disk.dirs[3]+64u,"CARD    VMD",0x20u,BASE_FIRST,131072u);
    journal_entries(disk.dirs[3]+96u);
    short_entry(disk.dirs[3]+160u,"IDENTITYBIN",0x20u,IDENTITY,16u);
}
static int collect(void) {return kui_vmu_fs_collect(&disk.input,&disk.profile,&disk.work);}
static void refused(void) {
    memset(&disk.profile,0xa5,sizeof(disk.profile));
    assert(collect()!=KUI_VMU_FS_OK);
    const uint8_t *bytes=(const uint8_t *)&disk.profile;
    for(size_t i=0;i<sizeof(disk.profile);i++) assert(!bytes[i]);
    assert(disk.reads<=KUI_VMU_FS_MAX_READS && !disk.outside);
}

static void valid_whole_cluster_binding(void) {
    fresh();assert(collect()==KUI_VMU_FS_OK);
    const struct kui_vmu_persist_file *journal=&disk.profile.files[KUI_VMU_PERSIST_JOURNAL];
    const struct kui_vmu_persist_file *identity=&disk.profile.files[KUI_VMU_PERSIST_IDENTITY];
    assert(journal->useful_count==1u && journal->useful[0].blocks==2u);
    assert(journal->allocated_count==1u && journal->allocated[0].blocks==SPC);
    assert(identity->file_bytes==16u && identity->useful[0].blocks==1u);
    assert(identity->allocated[0].blocks==SPC);
    assert(!disk.outside && disk.reads<=KUI_VMU_FS_MAX_READS);
    struct kui_vmu_persist_profile previous=disk.profile;
    /* An unrelated file can appear without invalidating unchanged ownership. */
    short_entry(disk.dirs[0]+32u,"OTHER   BIN",0x20u,100u,17u);disk.fat[100u]=EOC;
    assert(kui_vmu_fs_admit(&disk.input,&previous,&disk.work)==KUI_VMU_FS_OK);
}
static void shared_heads_and_incoming_edges(void) {
    fresh();short_entry(disk.dirs[0]+32u,"ALIAS   BIN",0x20u,BASE_FIRST,17u);refused();
    fresh();short_entry(disk.dirs[0]+32u,"ALIAS   BIN",0x20u,BASE_FIRST+3u,17u);refused();
    fresh();disk.fat[100u]=BASE_FIRST;refused(); /* Unnamed incoming edge. */
    fresh();disk.fat[100u]=JOURNAL;refused();
    fresh();disk.fat[BASE_LAST]=40u;disk.fat[40u]=EOC;refused(); /* Extra allocation tail. */
}
static void slack_and_regular_file_attributes(void) {
    fresh();struct kui_vmu_fs_range slack={cluster_lba(JOURNAL)+2u,1u};
    disk.input.forbidden=&slack;disk.input.forbidden_count=1u;refused();
    fresh();slack=(struct kui_vmu_fs_range){cluster_lba(IDENTITY)+7u,1u};
    disk.input.forbidden=&slack;disk.input.forbidden_count=1u;refused();
    const unsigned bad[]={1u,4u,0x10u};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        fresh();disk.dirs[3][64u+11u]|=(uint8_t)bad[i];refused();
        fresh();disk.dirs[3][128u+11u]|=(uint8_t)bad[i];refused();
    }
    fresh();disk.dirs[3][160u+11u]|=0x10u;refused();
}
static void malformed_geometry_and_target_metadata(void) {
    fresh();disk.boot[13]=3u;refused();
    fresh();put32(disk.boot+36u,UINT32_MAX);refused();
    fresh();put32(disk.boot+44u,CLUSTERS+2u);refused();
    fresh();put16(disk.boot+11u,4096u);refused();
    fresh();disk.dirs[3][96u+13u]^=1u;refused(); /* Target LFN checksum. */
    fresh();put32(disk.dirs[3]+128u+28u,1023u);refused();
    fresh();disk.identity[3]^=1u;refused();
    fresh();disk.fat[BASE_FIRST]=BASE_FIRST;refused();
}
static void short_name_padding_and_fresh_attributes(void) {
    fresh();disk.dirs[3][64u+5u]='X';refused(); /* CARD X must not truncate to CARD. */
    fresh();disk.dirs[0][4u]='X';refused(); /* The same applies to path components. */
    fresh();short_entry(disk.dirs[0]+32u,"OTHER   B N",0x20u,100u,17u);
    disk.fat[100u]=EOC;refused(); /* Extension padding cannot hide a later byte. */
    fresh();disk.dirs[3][64u+12u]=1u;refused(); /* Reserved short-name case bits. */
    fresh();assert(collect()==KUI_VMU_FS_OK);
    struct kui_vmu_persist_profile previous=disk.profile;
    disk.dirs[3][128u+11u]|=1u;
    assert(kui_vmu_fs_admit(&disk.input,&previous,&disk.work)!=KUI_VMU_FS_OK);
    fresh();assert(collect()==KUI_VMU_FS_OK);previous=disk.profile;
    disk.dirs[3][64u+11u]|=4u;
    assert(kui_vmu_fs_admit(&disk.input,&previous,&disk.work)!=KUI_VMU_FS_OK);
}
static void short_paths_refuse_before_reads(void) {
    static const char full[]="/KUI/vmu-test/p0001";
    for(size_t bytes=0;bytes<sizeof(full)-1u;bytes++) {
        fresh();char *short_path=malloc(bytes+1u);assert(short_path);
        memcpy(short_path,full,bytes);short_path[bytes]=0;
        disk.input.pair_path=short_path;
        assert(collect()==KUI_VMU_FS_ARGUMENT);
        assert(!disk.reads && !disk.work.stats.reads);
        free(short_path);
    }
}
int main(void) {
    valid_whole_cluster_binding();shared_heads_and_incoming_edges();
    slack_and_regular_file_attributes();malformed_geometry_and_target_metadata();
    short_name_padding_and_fresh_attributes();short_paths_refuse_before_reads();
    puts("Independent FAT32 admission: shared heads, incoming links, slack, attributes and malformed metadata passed");
    return 0;
}
