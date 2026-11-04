/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/report_stage_cache.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
static struct kui_report_stage_cache cache;
static struct {
    uint64_t card,first,end;
    struct kui_vmu_fs_range protected[3];size_t protected_count;
    uint32_t calls,sectors,last_lba,last_count;
    unsigned generation;bool fail;
} mock;
static uint8_t pattern(uint32_t lba,unsigned byte) {return (uint8_t)(lba*13u+byte*7u+mock.generation);}
static int read_multi(void *context,uint32_t lba,uint32_t count,uint8_t *out) {
    assert(context==&mock && count && count<=8u && (uint64_t)lba+count<=mock.card);
    assert(lba?lba>=mock.first && (uint64_t)lba+count<=mock.end:count==1u);
    for(size_t i=0;i<mock.protected_count;i++)assert((uint64_t)lba+count<=mock.protected[i].lba ||
        (uint64_t)lba>=mock.protected[i].lba+mock.protected[i].blocks);
    ++mock.calls;mock.sectors+=count;mock.last_lba=lba;mock.last_count=count;
    for(uint32_t sector=0;sector<count;sector++)for(unsigned b=0;b<512u;b++)
        out[sector*512u+b]=pattern(lba+sector,b);
    return mock.fail?-1:0;
}
static void initialize(uint64_t card,uint64_t first,uint64_t end) {
    mock.card=card;mock.first=first;mock.end=end;
    assert(!kui_report_stage_cache_init(&cache,card,first,end,mock.protected,
        mock.protected_count,read_multi,&mock));
    assert(!cache.requests && !cache.wire_calls && !cache.physical_sectors && !cache.hits && !cache.poisoned);
}
static void checked(uint32_t lba,uint8_t out[512]) {
    assert(!kui_report_stage_cache_read(&cache,lba,out));
    for(unsigned b=0;b<512u;b++)assert(out[b]==pattern(lba,b));
}
int main(void) {
    uint8_t out[512],saved[512];memset(&mock,0,sizeof(mock));initialize(50000u,1u,49999u);
    /* Independent mirrored FAT streams alternate on every logical sector. */
    for(uint32_t i=0;i<128u;i++){checked(100u+i,out);checked(10000u+i,out);}
    assert(mock.calls==32u && mock.sectors==256u && cache.hits==224u);
    assert(cache.requests==256u && cache.wire_calls==32u && cache.physical_sectors==256u);
    memset(&mock,0,sizeof(mock));mock.protected[0]=(struct kui_vmu_fs_range){20u,10u};mock.protected_count=1u;
    initialize(100u,5u,95u);checked(17u,out);assert(mock.last_count==3u);
    checked(18u,out);assert(cache.hits==1u && mock.calls==1u);
    checked(30u,out);assert(mock.last_count==8u);checked(94u,out);assert(mock.last_count==1u);
    checked(0u,out);assert(mock.last_count==1u);memcpy(saved,out,512u);
    unsigned calls=mock.calls;assert(kui_report_stage_cache_read(&cache,20u,out));
    assert(cache.poisoned && !cache.count[0] && !cache.count[1] && !memcmp(out,saved,512u));
    assert(kui_report_stage_cache_read(&cache,18u,out) && mock.calls==calls);
    initialize(100u,5u,95u);memset(out,0x69u,512u);memcpy(saved,out,512u);
    assert(kui_report_stage_cache_read(&cache,4u,out) && !memcmp(out,saved,512u));
    initialize(100u,5u,95u);assert(kui_report_stage_cache_read(&cache,95u,out));
    memset(&mock,0,sizeof(mock));initialize(UINT64_C(1)<<32,1u,UINT64_C(1)<<32);
    checked(UINT32_MAX,out);assert(mock.last_count==1u && mock.last_lba==UINT32_MAX);
    memset(&mock,0,sizeof(mock));initialize(100u,1u,100u);checked(10u,out);checked(30u,out);
    memcpy(saved,out,512u);mock.fail=true;calls=mock.calls;
    assert(kui_report_stage_cache_read(&cache,50u,out));
    assert(mock.calls==calls+1u && cache.poisoned && !cache.count[0] && !cache.count[1]);
    assert(!memcmp(out,saved,512u));calls=mock.calls;
    assert(kui_report_stage_cache_read(&cache,10u,out) && mock.calls==calls && !memcmp(out,saved,512u));
    mock.fail=false;++mock.generation;initialize(100u,1u,100u);checked(10u,out);
    assert(mock.calls==calls+1u); /* Fresh admission never trusts the old cache. */
    memset(&mock,0,sizeof(mock));initialize(1000u,1u,1000u);
    for(unsigned i=0;i<KUI_VMU_FS_MAX_READS/8u;i++)checked(1u+(i%3u)*32u,out);
    assert(mock.sectors==KUI_VMU_FS_MAX_READS && cache.physical_sectors==KUI_VMU_FS_MAX_READS);
    calls=mock.calls;memcpy(saved,out,512u);
    assert(kui_report_stage_cache_read(&cache,500u,out) && mock.calls==calls && !memcmp(out,saved,512u));
    memset(&mock,0,sizeof(mock));initialize(100u,1u,100u);
    for(unsigned i=0;i<KUI_VMU_FS_MAX_READS;i++)checked(10u,out);
    calls=mock.calls;assert(calls==1u && cache.hits==KUI_VMU_FS_MAX_READS-1u);
    assert(kui_report_stage_cache_read(&cache,10u,out) && mock.calls==calls);
    assert(kui_report_stage_cache_init(&cache,0u,0u,1u,NULL,0u,read_multi,&mock));
    assert(cache.poisoned && kui_report_stage_cache_read(&cache,0u,out));
    assert(kui_report_stage_cache_init(&cache,100u,50u,50u,NULL,0u,read_multi,&mock));
    assert(kui_report_stage_cache_init(&cache,100u,1u,100u,NULL,1u,read_multi,&mock));
    mock.protected[0]=(struct kui_vmu_fs_range){99u,2u};
    assert(kui_report_stage_cache_init(&cache,100u,1u,100u,mock.protected,1u,read_multi,&mock));
    puts("stage metadata read window: mirrored LRU, strict boundaries, fresh proof and finite budgets passed");
    return 0;
}
