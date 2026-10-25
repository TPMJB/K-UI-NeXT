/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/native_readahead_cache.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned checks;
#define CHECK(test) do { ++checks; assert(test); } while(0)
static _Alignas(32) uint8_t arena[KUI_NATIVE_RA_CACHE_BYTES+64u];
static uint8_t block[512];
static struct kui_retail_manifest manifest;

static uint8_t byte_at(uint32_t lba,unsigned i) {
    return (uint8_t)((lba*UINT32_C(0x9e3779b1))^(lba>>8)^(i*29u)^(i>>3));
}
static void fill(uint32_t lba) {
    for(unsigned i=0;i<512u;i++)block[i]=byte_at(lba,i);
}
static void contents(const uint8_t *p,uint32_t lba) {
    CHECK(p!=NULL);
    for(unsigned i=0;i<512u;i++)CHECK(p[i]==byte_at(lba,i));
}
static void append(struct kui_native_ra_cache *c,uint32_t lba) {
    fill(lba);CHECK(kui_native_ra_cache_append(c,lba,block));
}
static void sentinels(void) {
    for(unsigned i=0;i<32u;i++)CHECK(arena[i]==0xa7 && arena[32u+KUI_NATIVE_RA_CACHE_BYTES+i]==0xa7);
}
static void ring(void) {
    memset(arena,0xa7,sizeof(arena));
    struct kui_native_ra_cache c;
    CHECK(kui_native_ra_cache_init(&c,arena+32u,KUI_NATIVE_RA_CACHE_BYTES));
    uint32_t next=0x12345678u;
    CHECK(!kui_native_ra_cache_next(&c,&next) && next==0x12345678u);
    CHECK(kui_native_ra_cache_rebase(&c,100u,10000u));
    for(uint32_t i=0;i<KUI_NATIVE_RA_CACHE_BLOCKS;i++) {
        CHECK(kui_native_ra_cache_next(&c,&next) && next==100u+i);
        append(&c,next);
    }
    CHECK(!kui_native_ra_cache_next(&c,&next));
    CHECK(c.count==384u);
    struct kui_native_ra_cache before=c;
    CHECK(!kui_native_ra_cache_lookup(&c,99u) && !memcmp(&before,&c,sizeof(c)));
    CHECK(!kui_native_ra_cache_lookup(&c,484u) && !memcmp(&before,&c,sizeof(c)));
    CHECK(!kui_native_ra_cache_append(&c,484u,block) && !memcmp(&before,&c,sizeof(c)));
    /* Repeated lookups retain the current boundary block and its address. */
    const uint8_t *p=kui_native_ra_cache_lookup(&c,237u);
    contents(p,237u);
    for(unsigned i=0;i<12u;i++)CHECK(kui_native_ra_cache_lookup(&c,237u)==p);
    CHECK(c.base==237u && c.count==247u && c.head==137u);
    for(uint32_t i=484u;i<621u;i++)append(&c,i);
    CHECK(c.count==384u);
    /* Walk all entries after wrapping, then refill and repeat many cycles. */
    for(unsigned cycle=0;cycle<7u;cycle++) {
        uint32_t base=c.base;
        for(uint32_t i=0;i<384u;i++)contents(kui_native_ra_cache_lookup(&c,base+i),base+i);
        CHECK(c.count==1u && c.base==base+383u);
        for(uint32_t i=1;i<384u;i++)append(&c,c.base+i);
        CHECK(c.count==384u);
    }
    sentinels();
    /* Forward seek invalidates only when the caller explicitly rebases. */
    CHECK(kui_native_ra_cache_rebase(&c,3u,7u));
    for(uint32_t i=3u;i<7u;i++)append(&c,i);
    CHECK(!kui_native_ra_cache_next(&c,&next));
    contents(kui_native_ra_cache_lookup(&c,6u),6u);
    CHECK(c.count==1u && !kui_native_ra_cache_next(&c,&next));
    CHECK(kui_native_ra_cache_rebase(&c,UINT32_MAX-2u,UINT32_MAX));
    append(&c,UINT32_MAX-2u);append(&c,UINT32_MAX-1u);
    CHECK(!kui_native_ra_cache_next(&c,&next));
    contents(kui_native_ra_cache_lookup(&c,UINT32_MAX-1u),UINT32_MAX-1u);
    sentinels();
}
static void invalid(void) {
    struct kui_native_ra_cache c={0};
    CHECK(kui_native_ra_cache_init(&c,arena+32u,KUI_NATIVE_RA_CACHE_BYTES));
    CHECK(kui_native_ra_cache_rebase(&c,100u,1000u));append(&c,100u);
    struct kui_native_ra_cache before=c;
    CHECK(!kui_native_ra_cache_init(NULL,arena+32u,KUI_NATIVE_RA_CACHE_BYTES));
    CHECK(!kui_native_ra_cache_init(&c,NULL,KUI_NATIVE_RA_CACHE_BYTES));
    CHECK(!memcmp(&before,&c,sizeof(c)));
    CHECK(!kui_native_ra_cache_init(&c,arena+33u,KUI_NATIVE_RA_CACHE_BYTES));
    CHECK(!kui_native_ra_cache_init(&c,arena+32u,KUI_NATIVE_RA_CACHE_BYTES-1u));
    CHECK(!kui_native_ra_cache_init(&c,arena+32u,KUI_NATIVE_RA_CACHE_BYTES+1u));
    CHECK(!kui_native_ra_cache_init(&c,(void *)(UINTPTR_MAX&~(uintptr_t)31u),KUI_NATIVE_RA_CACHE_BYTES));
    CHECK(!memcmp(&before,&c,sizeof(c)));
    CHECK(!kui_native_ra_cache_init((void *)(arena+64u),arena+32u,KUI_NATIVE_RA_CACHE_BYTES));
    CHECK(!kui_native_ra_cache_rebase(&c,100u,100u));
    CHECK(!kui_native_ra_cache_rebase(&c,101u,100u));
    CHECK(!kui_native_ra_cache_rebase(NULL,1u,2u));
    CHECK(!kui_native_ra_cache_append(&c,101u,NULL));
    CHECK(!kui_native_ra_cache_append(&c,100u,block));
    CHECK(!kui_native_ra_cache_append(&c,102u,block));
    CHECK(!memcmp(&before,&c,sizeof(c)));
    union { struct kui_native_ra_cache cache; uint8_t bytes[512]; } overlap;
    CHECK(kui_native_ra_cache_init(&overlap.cache,arena+32u,KUI_NATIVE_RA_CACHE_BYTES));
    CHECK(kui_native_ra_cache_rebase(&overlap.cache,100u,1000u));
    CHECK(!kui_native_ra_cache_append(&overlap.cache,100u,overlap.bytes));
    CHECK(!kui_native_ra_cache_lookup(NULL,100u));
    CHECK(!kui_native_ra_cache_next(&c,NULL));
    CHECK(!kui_native_ra_cache_next(NULL,&c.base));
    for(unsigned kind=0;kind<5u;kind++) {
        struct kui_native_ra_cache broken=c;
        if(kind==0u)broken.head=384u;
        if(kind==1u)broken.count=385u;
        if(kind==2u)broken.base=1001u;
        if(kind==3u)broken.end=100u;
        if(kind==4u)broken.data++;
        struct kui_native_ra_cache snapshot=broken;
        uint32_t out=0x12345678u;
        CHECK(!kui_native_ra_cache_lookup(&broken,100u));
        CHECK(!kui_native_ra_cache_next(&broken,&out) && out==0x12345678u);
        CHECK(!kui_native_ra_cache_append(&broken,101u,block));
        CHECK(!kui_native_ra_cache_rebase(&broken,101u,102u));
        CHECK(!memcmp(&snapshot,&broken,sizeof(broken)));
    }
}
static void aliases(void) {
    struct kui_native_ra_cache c;
    CHECK(kui_native_ra_cache_init(&c,arena+32u,KUI_NATIVE_RA_CACHE_BYTES));
    CHECK(kui_native_ra_cache_rebase(&c,100u,1000u));append(&c,100u);
    const uint8_t *p=kui_native_ra_cache_lookup(&c,100u);
    CHECK(kui_native_ra_cache_append(&c,101u,p));
    CHECK(!memcmp(kui_native_ra_cache_lookup(&c,101u),p,512u));
    /* A caller may supply an overlapping byte range; publication is still
     * after a memmove-equivalent copy, never partially valid. */
    CHECK(kui_native_ra_cache_rebase(&c,3u,8u));
    for(unsigned i=0;i<544u;i++)c.data[i]=(uint8_t)i;
    uint8_t expected[512];memcpy(expected,c.data+16u,512u);
    CHECK(kui_native_ra_cache_append(&c,3u,c.data+16u));
    CHECK(!memcmp(kui_native_ra_cache_lookup(&c,3u),expected,512u));
}
static uint32_t rng=UINT32_C(0x13579bdf);
static uint32_t random_word(void) {rng^=rng<<13;rng^=rng>>17;rng^=rng<<5;return rng;}
static void randomized(void) {
    struct kui_native_ra_cache c;
    CHECK(kui_native_ra_cache_init(&c,arena+32u,KUI_NATIVE_RA_CACHE_BYTES));
    uint32_t base=300u,end=1000000u,count=0;
    CHECK(kui_native_ra_cache_rebase(&c,base,end));
    for(unsigned i=0;i<16000u;i++) {
        uint32_t kind=random_word()%16u;
        if(kind<9u) {
            uint32_t lba=base+count;
            fill(lba);
            bool accepted=count<384u && lba<end;
            CHECK(kui_native_ra_cache_append(&c,lba,block)==accepted);
            if(accepted)++count;
        } else if(kind<14u && count) {
            uint32_t skip=random_word()%count;
            uint32_t lba=base+skip;
            contents(kui_native_ra_cache_lookup(&c,lba),lba);
            base=lba;count-=skip;
        } else if(kind==14u) {
            struct kui_native_ra_cache before=c;
            CHECK(!kui_native_ra_cache_lookup(&c,base+count+1u));
            CHECK(!memcmp(&before,&c,sizeof(c)));
        } else {
            base=random_word()%900000u;end=base+1u+random_word()%1000u;count=0;
            CHECK(kui_native_ra_cache_rebase(&c,base,end));
        }
        CHECK(c.base==base && c.count==count && c.end==end);
    }
}

static void fixture(uint8_t control,uint32_t offset,uint32_t sectors,unsigned fragments) {
    memset(&manifest,0,sizeof(manifest));
    manifest.card_sectors=100000u;manifest.partition_start=20u;manifest.partition_end=90000u;
    manifest.track_count=1u;manifest.extent_count=fragments;
    manifest.flags=KUI_RETAIL_IMAGE_CD;manifest.session_lba=45000u;
    manifest.boot_lba=45000u;manifest.boot_bytes=2048u;
    manifest.storage_transport=KUI_STORAGE_SCI;manifest.reader=KUI_RETAIL_READER_ASYNC;
    strcpy(manifest.title,"Read-ahead bounded physical cache");strcpy(manifest.bootfile,"1ST_READ.BIN");
    struct kui_retail_track *t=&manifest.slots[0].track;
    *t=(struct kui_retail_track){45000u,45000u+sectors,
        (uint16_t)(1u|((offset&255u)<<8)),(uint8_t)fragments,
        (uint8_t)(control|((offset&256u)?KUI_RETAIL_TRACK_OFFSET_HIGH:0u))};
    uint32_t blocks=(offset+sectors*kui_retail_track_sector_bytes(t)+511u)/512u;
    uint32_t at=0;
    for(unsigned i=0;i<fragments;i++) {
        uint32_t take=(blocks-at)/(fragments-i);
        manifest.slots[1u+i].extent=(struct kui_retail_extent){at,1000u+600u*i,take};
        at+=take;
    }
    CHECK(at==blocks && kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);
}
static void extents(void) {
    const uint8_t controls[]={4u,4u|KUI_RETAIL_TRACK_MODE2,
        4u|KUI_RETAIL_TRACK_MODE2|KUI_RETAIL_TRACK_2336,
        4u|KUI_RETAIL_TRACK_2448,4u|KUI_RETAIL_TRACK_MODE2|KUI_RETAIL_TRACK_2448,
        4u|KUI_RETAIL_TRACK_COOKED};
    const uint32_t offsets[]={0u,1u,16u,255u,256u,511u};
    for(unsigned c=0;c<sizeof(controls);c++)for(unsigned o=0;o<sizeof(offsets)/sizeof(offsets[0]);o++)
        for(unsigned fragments=1;fragments<=3u;fragments++) {
            fixture(controls[c],offsets[o],75u,fragments);
            for(unsigned i=0;i<fragments;i++) {
                const struct kui_retail_extent *e=&manifest.slots[1u+i].extent;
                for(uint32_t n=0;n<e->blocks;n++)CHECK(kui_native_ra_extent_end(&manifest,e->card_lba+n)==e->card_lba+e->blocks);
                CHECK(!kui_native_ra_extent_end(&manifest,e->card_lba-1u));
                CHECK(!kui_native_ra_extent_end(&manifest,e->card_lba+e->blocks));
            }
        }
    fixture(4u|KUI_RETAIL_TRACK_COOKED,1u,1u,1u);
    struct kui_retail_track first=manifest.slots[0].track;
    struct kui_retail_extent extent=manifest.slots[1].extent;
    /* Adjacent packed tracks share one boundary card block. */
    manifest.track_count=2u;manifest.extent_count=2u;
    first.start_lba=45000u;first.end_lba=45001u;first.first_extent=(uint16_t)(2u|(1u<<8));
    manifest.slots[0].track=first;
    first.start_lba=45001u;first.end_lba=45002u;first.first_extent=(uint16_t)(3u|(1u<<8));
    manifest.slots[1].track=first;
    manifest.slots[2].extent=extent;
    manifest.slots[3].extent=(struct kui_retail_extent){0u,extent.card_lba+4u,5u};
    CHECK(kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);
    CHECK(kui_native_ra_extent_end(&manifest,1004u)==1005u);
    CHECK(kui_native_ra_extent_end(&manifest,1005u)==1009u);
    manifest.slots[1].track.first_extent=3u; /* used bytes now overlap */
    CHECK(!kui_native_ra_extent_end(&manifest,1000u));
    /* Unmapped audio remains listed but authorizes no physical reads. */
    fixture(4u,0u,75u,1u);first=manifest.slots[0].track;extent=manifest.slots[1].extent;
    manifest.track_count=2u;manifest.slots[0].track=(struct kui_retail_track){0u,3u,2u,0u,0u};
    first.first_extent=2u;manifest.slots[1].track=first;manifest.slots[2].extent=extent;
    CHECK(kui_retail_manifest_validate(&manifest)==KUI_GAME_OK);
    CHECK(kui_native_ra_extent_end(&manifest,1000u)==1000u+extent.blocks);
}
static void extent_faults(void) {
    CHECK(!kui_native_ra_extent_end(NULL,1000u));
    for(unsigned kind=0;kind<22u;kind++) {
        fixture(4u,0u,75u,1u);
        struct kui_retail_track *t=&manifest.slots[0].track;
        struct kui_retail_extent *e=&manifest.slots[1].extent;
        if(kind==0u)manifest.track_count=0;
        if(kind==1u)manifest.track_count=65u;
        if(kind==2u)manifest.extent_count=64u;
        if(kind==3u)manifest.extent_count=0;
        if(kind==4u)manifest.card_sectors=0;
        if(kind==5u)manifest.card_sectors=UINT64_C(0x100000001);
        if(kind==6u)manifest.partition_start=manifest.partition_end;
        if(kind==7u)manifest.partition_end=manifest.card_sectors+1u;
        if(kind==8u)t->start_lba=t->end_lba;
        if(kind==9u)t->control|=1u;
        if(kind==10u)t->control=KUI_RETAIL_TRACK_COOKED;
        if(kind==11u)t->control|=KUI_RETAIL_TRACK_COOKED|KUI_RETAIL_TRACK_2336;
        if(kind==12u)t->control|=KUI_RETAIL_TRACK_2336;
        if(kind==13u)t->first_extent=0;
        if(kind==14u)t->first_extent=63u;
        if(kind==15u)t->extent_count=2u;
        if(kind==16u)t->extent_count=0u;
        if(kind==17u)e->file_block=1u;
        if(kind==18u)e->blocks=0;
        if(kind==19u)e->blocks++;
        if(kind==20u)e->card_lba=19u;
        if(kind==21u)e->card_lba=UINT32_MAX-3u;
        CHECK(!kui_native_ra_extent_end(&manifest,1000u));
    }
    fixture(4u|KUI_RETAIL_TRACK_COOKED,0u,1u,1u);
    manifest.card_sectors=manifest.partition_end=UINT64_C(0x100000000);
    manifest.slots[1].extent.card_lba=UINT32_MAX-3u;
    CHECK(!kui_native_ra_extent_end(&manifest,UINT32_MAX));
    manifest.slots[1].extent.card_lba=UINT32_MAX-4u;
    CHECK(kui_native_ra_extent_end(&manifest,UINT32_MAX-1u)==UINT32_MAX);
}
int main(void) {
    ring();invalid();aliases();randomized();extents();extent_faults();
    printf("native read-ahead cache: %u checks;384-slot wrap, overlap, seeks, publication, bounded extents and invalid geometry passed\n",checks);
    return 0;
}
