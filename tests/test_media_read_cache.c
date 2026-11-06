/* SPDX-License-Identifier: GPL-3.0-only */
/* Production disk adapter over byte-backed media. Device callbacks count
 * actual adapter requests and inject read, write and sync failures. */
#include "kui/media.h"
#include "ff.h"
#include "diskio.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define SECTORS 128u
#define PARTITION_FIRST 4u
#define PARTITION_COUNT 120u
#define CACHE_BYTES (64u*(512u+4u))
static uint8_t image[SECTORS][512];
static unsigned reads,writes,syncs;
static bool fail_read,fail_write,fail_sync;
static _Alignas(32) uint8_t cache[CACHE_BYTES],nested_cache[CACHE_BYTES];

uint32_t kui_clock_fattime(void) {return 0;}
static uint64_t blocks(void *context) {(void)context;return SECTORS;}
static int read_blocks(void *context,uint32_t block,size_t count,uint8_t *out) {
    (void)context;++reads;assert(block<SECTORS && count<=SECTORS-block);
    if(fail_read)return -1;
    memcpy(out,image[block],count*512u);return 0;
}
static int write_blocks(void *context,uint32_t block,size_t count,const uint8_t *in) {
    (void)context;++writes;assert(block<SECTORS && count<=SECTORS-block);
    if(fail_write)return -1;
    memcpy(image[block],in,count*512u);return 0;
}
static int sync_blocks(void *context) {(void)context;++syncs;return fail_sync?-1:0;}
static const struct kui_media_ops ops={NULL,blocks,read_blocks,write_blocks,sync_blocks};
static void fixture(void) {
    kui_media_read_cache_end();
    fail_read=fail_write=fail_sync=false;
    for(unsigned i=0;i<SECTORS;i++)memset(image[i],(int)i,512u);
    memset(image[0],0,512u);image[0][510]=0x55;image[0][511]=0xaa;
    image[0][446u+4u]=0x0c;image[0][446u+8u]=PARTITION_FIRST;
    image[0][446u+12u]=PARTITION_COUNT;
    kui_media_set(&ops);assert(disk_initialize(0u)==0u);
    assert(kui_media_volume()->start==PARTITION_FIRST);
    reads=writes=syncs=0u;
}
static void first_read_and_scope(void) {
    uint8_t out[1024];fixture();
    assert(!kui_media_read_cache_begin(NULL,sizeof(cache)));
    assert(!kui_media_read_cache_begin(cache,1u));
    assert(!kui_media_read_cache_begin(cache+1u,sizeof(cache)-1u));
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==1u && out[0]==7u);
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==1u && out[511]==7u);
    assert(!kui_media_read_cache_begin(nested_cache,sizeof(nested_cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==1u);
    /* Multi-sector requests always reach the SD adapter, and never populate
     * the single-sector metadata cache with unchecked partial results. */
    assert(disk_read(0u,out,8u,2u)==RES_OK && reads==2u && out[512]==13u);
    assert(disk_read(0u,out,8u,2u)==RES_OK && reads==3u);
    assert(disk_read(0u,out,8u,1u)==RES_OK && reads==4u);
    assert(disk_read(0u,out,8u,1u)==RES_OK && reads==4u);
    assert(disk_ioctl(0u,CTRL_SYNC,NULL)==RES_OK && syncs==1u);
    assert(disk_read(0u,out,8u,1u)==RES_OK && reads==4u);
    kui_media_read_cache_end();image[7][0]=99u;
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==5u && out[0]==99u);
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==6u && out[0]==99u);
    kui_media_read_cache_end();
}
static void bounded_storage(void) {
    uint8_t out[512];fixture();
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    /* More distinct metadata sectors than the scratch can retain must still
     * return the actual sector and stay within the caller's allocation. */
    for(unsigned direction=0u;direction<2u;direction++)
        for(unsigned n=0u;n<80u;n++) {
            unsigned sector=direction?79u-n:n;
            assert(disk_read(0u,out,sector,1u)==RES_OK);
            assert(out[0]==sector+PARTITION_FIRST && out[511]==out[0]);
        }
    kui_media_read_cache_end();
}
static void writes_disable_scope(void) {
    uint8_t out[512],in[512];memset(in,0x9a,sizeof(in));fixture();
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==1u);
    assert(disk_write(0u,in,3u,1u)==RES_OK && writes==1u);
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==2u && out[0]==0x9au);
    /* A write elsewhere also ends this read-only operation's cache scope. */
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==3u);
    assert(disk_write(0u,in,9u,1u)==RES_OK && writes==2u);
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==4u);
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==5u);
    assert(disk_write(1u,in,9u,1u)==RES_PARERR && writes==2u);
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==6u);
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==7u);
    assert(disk_write(0u,NULL,9u,1u)==RES_PARERR && writes==2u);
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==8u);
}
static void failure_invalidates(unsigned failure) {
    uint8_t out[512],in[512]={0};fixture();
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==1u);
    if(failure==0u) {fail_read=true;assert(disk_read(0u,out,4u,1u)==RES_ERROR);}
    else if(failure==1u) {fail_write=true;assert(disk_write(0u,in,4u,1u)==RES_ERROR);}
    else {fail_sync=true;assert(disk_ioctl(0u,CTRL_SYNC,NULL)==RES_ERROR);}
    unsigned before=reads;
    assert((disk_status(0u)&STA_NOINIT)!=0u);
    assert(disk_read(0u,out,3u,1u)==RES_PARERR && reads==before);
    assert((disk_initialize(0u)&STA_NOINIT)!=0u);
    /* Failed media never silently recovers, and a fresh binding has no keys
     * pointing into the preceding operation's caller-owned scratch. */
    fail_read=fail_write=fail_sync=false;kui_media_set(&ops);
    assert(disk_initialize(0u)==0u);before=reads;image[7][0]=0x81u;
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==before+1u && out[0]==0x81u);
    kui_media_read_cache_end();
}
static void bounds_and_rebind(void) {
    uint8_t out[512];fixture();
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==1u);
    assert(disk_read(1u,out,3u,1u)==RES_PARERR && reads==1u);
    assert(disk_read(0u,NULL,3u,1u)==RES_PARERR && reads==1u);
    assert(disk_read(0u,out,3u,0u)==RES_PARERR && reads==1u);
    assert(disk_read(0u,out,PARTITION_COUNT,1u)==RES_PARERR && reads==1u);
    /* Reset and a differently based partition must not retain relative-LBA
     * entries from the previous media binding. */
    kui_media_set(NULL);assert(disk_read(0u,out,3u,1u)==RES_PARERR && reads==1u);
    image[0][446u+8u]=8u;image[0][446u+12u]=116u;
    kui_media_set(&ops);assert(disk_initialize(0u)==0u);unsigned before=reads;
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==before+1u && out[0]==11u);
    kui_media_read_cache_end();
    struct kui_volume boot={.start=16u,.count=96u};
    assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK);
    assert(kui_media_boot_view(&ops,&boot));assert(disk_initialize(0u)==STA_PROTECT);
    before=reads;assert(kui_media_read_cache_begin(cache,sizeof(cache)));
    assert(disk_read(0u,out,3u,1u)==RES_OK && reads==before+1u && out[0]==19u);
    kui_media_read_cache_end();
}
int main(void) {
    first_read_and_scope();bounded_storage();writes_disable_scope();
    for(unsigned i=0u;i<3u;i++)failure_invalidates(i);
    bounds_and_rebind();kui_media_read_cache_end();kui_media_set(NULL);
    puts("Media read cache: first-read I/O, scope/nesting, direct multi-sector I/O, write/error/reset invalidation and bounds passed");
    return 0;
}
