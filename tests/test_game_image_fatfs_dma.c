/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual FatFs and image conversion; the existing formatter is only a host
 * disk bridge. The SCI address predicate is copied verbatim by the runner. */
#define main formatter_main
#define disk_read fixture_disk_read
#include "format_fatfs_fixture.c"
#undef main
#undef disk_read
#include "kui/game_image.h"
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#define KUI_SCI_SD_TEST 1
static uint32_t kui_sci_sd_test_dma_address(const void *p,size_t bytes) {
    assert(p && !((uintptr_t)p&31u) && bytes==512u);return 0x0c100000u;
}
#include "sci-dma-address.inc"

#define CAPACITY 131072u
static uint8_t content[CAPACITY],output[33u*2048u+2u];
static FIL reader;
static bool tracing,deny;
static uintptr_t destination;
static size_t destination_bytes;
static void *allocated,*returned;
static unsigned file_reads,direct_blocks,eligible_blocks,rejected_blocks,single_requests,multi_requests;
static unsigned total_direct,total_eligible,total_rejected,total_file_reads,cases,file_extents;
void *__real_malloc(size_t);
void __real_free(void *);
void *__wrap_malloc(size_t bytes) {
    if(!tracing)return __real_malloc(bytes);
    assert(!allocated);
    if(deny)return NULL;
    allocated=__real_malloc(bytes+64u);assert(allocated);
    returned=(void *)(((uintptr_t)allocated+31u)&~(uintptr_t)31u);
    return returned;
}
void __wrap_free(void *p) {
    if(p && p==returned) {assert(allocated);__real_free(allocated);allocated=returned=NULL;}
    else __real_free(p);
}
DRESULT disk_read(BYTE drive,BYTE *out,LBA_t first,UINT count) {
    if(tracing && (uintptr_t)out>=destination &&
       (uintptr_t)out-destination<=destination_bytes &&
       (size_t)count*512u<=destination_bytes-((uintptr_t)out-destination)) {
        if(count==1u)++single_requests;else ++multi_requests;
        for(UINT i=0;i<count;i++) {
            ++direct_blocks;
            if(dma_address(out+(size_t)i*512u))++eligible_blocks;else ++rejected_blocks;
        }
    }
    return fixture_disk_read(drive,out,first,count);
}
static enum kui_game_result stat_source(void *ctx,const char *name,uint64_t *bytes) {
    (void)ctx;assert(!strcmp(name,"raw.bin"));*bytes=CAPACITY;return KUI_GAME_OK;
}
static enum kui_game_result read_source(void *ctx,const char *name,uint64_t offset,void *out,size_t bytes) {
    (void)ctx;assert(!strcmp(name,"raw.bin") && offset<=CAPACITY && bytes<=CAPACITY-offset);
    ++file_reads;
    if(f_tell(&reader)!=offset)assert(f_lseek(&reader,(FSIZE_t)offset)==FR_OK);
    destination=(uintptr_t)out;destination_bytes=bytes;
    UINT got=0;FRESULT r=f_read(&reader,out,(UINT)bytes,&got);
    destination=0;destination_bytes=0;return r==FR_OK && got==bytes?KUI_GAME_OK:KUI_GAME_IO;
}
static void prepare_file(bool fragmented) {
    FIL raw,pad;UINT got;
    assert(f_open(&raw,"0:/raw.bin",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    if(fragmented)assert(f_open(&pad,"0:/padding.bin",FA_CREATE_ALWAYS|FA_WRITE)==FR_OK);
    memset(content,0,sizeof(content));
    if(fragmented) {
        for(unsigned at=0;at<CAPACITY;at+=512u) {
            assert(f_write(&raw,content,512u,&got)==FR_OK && got==512u);
            assert(f_write(&pad,content,512u,&got)==FR_OK && got==512u);
        }
        assert(f_close(&pad)==FR_OK);
    } else assert(f_write(&raw,content,CAPACITY,&got)==FR_OK && got==CAPACITY);
    assert(f_close(&raw)==FR_OK);
    DWORD map[1024];map[0]=1024u;
    assert(f_open(&raw,"0:/raw.bin",FA_READ)==FR_OK);raw.cltbl=map;
    assert(f_lseek(&raw,CREATE_LINKMAP)==FR_OK);
    for(unsigned i=1u;map[i];i+=2u)++file_extents;
    assert(file_extents && (fragmented?file_extents>1u:file_extents==1u));
    assert(f_close(&raw)==FR_OK);
}
static void check(unsigned stride,unsigned residue,bool fallback) {
    const unsigned mode=stride==2336u?2u:1u;
    const unsigned data_offset=stride==2336u?8u:16u;
    const unsigned prefix=96u+residue;
    memset(content,0xa7,sizeof(content));
    for(unsigned sector=0;sector<40u;sector++) {
        uint8_t *p=content+prefix+(size_t)sector*stride;
        if(stride!=2336u) {p[0]=p[11]=0;memset(p+1u,255,10u);p[15]=(uint8_t)mode;}
        else {const uint8_t xa[4]={1,7,8,3};memcpy(p,xa,4);memcpy(p+4,xa,4);}
        for(unsigned byte=0;byte<2048u;byte++)p[data_offset+byte]=(uint8_t)(sector*37u+byte*13u);
    }
    FIL writer;UINT got;
    assert(f_open(&writer,"0:/raw.bin",FA_WRITE)==FR_OK);
    assert(f_write(&writer,content,sizeof(content),&got)==FR_OK && got==sizeof(content));
    assert(f_close(&writer)==FR_OK);
    assert(f_open(&reader,"0:/raw.bin",FA_READ)==FR_OK);
    struct kui_game_image game={.format=KUI_GAME_IMAGE_GDI,.count=1u,.data_lba=45000u,
        .files={NULL,stat_source,read_source}};
    game.tracks[0]=(struct kui_game_image_track){.start_lba=45000u,.end_lba=45040u,
        .number=1u,.control=4u,.sector_bytes=stride,.sector_mode=mode,.data_offset=data_offset,
        .file_bytes=CAPACITY,.file_offset=prefix};
    strcpy(game.tracks[0].name,"raw.bin");
    memset(output,0x5a,sizeof(output));
    file_reads=direct_blocks=eligible_blocks=rejected_blocks=single_requests=multi_requests=0;
    deny=fallback;tracing=true;
    assert(kui_game_image_read(&game,45001u,33u,KUI_GAME_SECTOR_MODE1,output+1u,sizeof(output)-2u)==KUI_GAME_OK);
    tracing=false;assert(!allocated && f_close(&reader)==FR_OK);
    assert(file_reads==(fallback?33u:3u) && direct_blocks>0u);
    assert(output[0]==0x5a && output[sizeof(output)-1u]==0x5a);
    for(unsigned sector=0;sector<33u;sector++)
        assert(!memcmp(output+1u+(size_t)sector*2048u,
            content+prefix+(size_t)(sector+1u)*stride+data_offset,2048u));
    total_direct+=direct_blocks;total_eligible+=eligible_blocks;total_rejected+=rejected_blocks;
    total_file_reads+=file_reads;++cases;
    if(EXPECT_ALIGNED)assert(!rejected_blocks && eligible_blocks==direct_blocks);
}
int main(int argc,char **argv) {
    assert(argc==4);
    char *formatter_args[]={argv[0],argv[1],argv[2]};
    assert(formatter_main(3,formatter_args)==0);
    image=fopen(argv[2],"r+b");assert(image);
    FATFS fs;assert(f_mount(&fs,"0:",1)==FR_OK);prepare_file(atoi(argv[3])!=0);
    const unsigned strides[]={2352u,2448u,2336u};
    for(unsigned i=0;i<3u;i++)for(unsigned residue=0;residue<32u;residue++)
        for(unsigned fallback=0;fallback<2u;fallback++)check(strides[i],residue,fallback!=0);
    assert(f_mount(NULL,"0:",0)==FR_OK && !fclose(image));image=NULL;
    if(!EXPECT_ALIGNED)assert(total_rejected>0);
    printf("RESULT {\"cases\":%u,\"file_extents\":%u,\"file_read_callbacks\":%u,\"direct_blocks\":%u,\"dma_address_admitted\":%u,\"pio_address_fallback\":%u,\"all_payloads_equal\":true}\n",
        cases,file_extents,total_file_reads,total_direct,total_eligible,total_rejected);return 0;
}
