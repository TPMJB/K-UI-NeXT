/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent device oracle: writes can remain volatile until sync, persist
 * early, or tear at ANY byte boundary on a failing write/barrier. No store
 * internals or CRC helper decide expected saved bytes. */
#include "kui/vmu_store.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

#define CARD_SECTORS 512u
#define SECTOR_BYTES 512u
struct device {
    uint8_t durable[CARD_SECTORS][SECTOR_BYTES];
    uint8_t cache[CARD_SECTORS][SECTOR_BYTES];
    uint8_t dirty[CARD_SECTORS];
    unsigned calls,reads,writes,syncs,fail_at,cut;
    int mode,offline;
};
static struct device device;
static uint8_t snapshot[CARD_SECTORS][SECTOR_BYTES];
static const struct kui_vmu_store_extent image_map[]={{30,77},{300,99},{160,80}};
static const struct kui_vmu_store_extent journal_map[]={{7,1},{501,1}};
static const uint8_t id[16]={0x4b,0x55,0x49,0x56,0x4d,0x55,1,2,3,4,5,6,7,8,9,10};
static struct kui_vmu_store_config config(void) {
    struct kui_vmu_store_config c={.card_sectors=CARD_SECTORS,
        .image_extents=image_map,.image_extent_count=3,
        .journal_extents=journal_map,.journal_extent_count=2};
    memcpy(c.image_id,id,sizeof(id)); return c;
}
static unsigned image_lba(unsigned block) {
    assert(block<256);
    if(block<77) return 30+block;
    if(block<176) return 300+block-77;
    return 160+block-176;
}
static int allowed(unsigned lba) {
    return (lba>=30&&lba<107)||(lba>=300&&lba<399)||
           (lba>=160&&lba<240)||lba==7||lba==501;
}
static int should_fail(struct device *d) {
    assert(!d->offline); ++d->calls;
    return d->fail_at&&d->calls==d->fail_at;
}
static void persist_dirty(struct device *d,unsigned count) {
    for(unsigned i=0;i<CARD_SECTORS;i++) if(d->dirty[i])
        memcpy(d->durable[i],d->cache[i],count);
}
static void cut_power(struct device *d) {
    d->offline=1;
    memcpy(d->cache,d->durable,sizeof(d->cache));
    memset(d->dirty,0,sizeof(d->dirty));
}
static int read_sector(void *ctx,uint64_t lba,void *sector) {
    struct device *d=ctx; assert(lba<CARD_SECTORS&&allowed((unsigned)lba));
    ++d->reads;
    if(should_fail(d)) { if(d->mode!=3) cut_power(d); return -1; }
    memcpy(sector,d->cache[lba],SECTOR_BYTES); return 0;
}
static int write_sector(void *ctx,uint64_t lba,const void *sector) {
    struct device *d=ctx; assert(lba<CARD_SECTORS&&allowed((unsigned)lba));
    ++d->writes;
    if(should_fail(d)) {
        if(d->mode==3) {
            memcpy(d->cache[lba],sector,SECTOR_BYTES); d->dirty[lba]=1; return -1;
        }
        if(d->mode==1) memcpy(d->durable[lba],sector,SECTOR_BYTES);
        if(d->mode==2) memcpy(d->durable[lba],sector,d->cut);
        cut_power(d); return -1;
    }
    memcpy(d->cache[lba],sector,SECTOR_BYTES); d->dirty[lba]=1; return 0;
}
static int sync_device(void *ctx) {
    struct device *d=ctx; ++d->syncs;
    if(should_fail(d)) {
        if(d->mode==3) return -1;
        if(d->mode==1) persist_dirty(d,SECTOR_BYTES);
        if(d->mode==2) persist_dirty(d,d->cut);
        cut_power(d); return -1;
    }
    persist_dirty(d,SECTOR_BYTES); memset(d->dirty,0,sizeof(d->dirty)); return 0;
}
static struct kui_vmu_store_io io(void) {
    struct kui_vmu_store_io i={&device,read_sector,write_sector,sync_device}; return i;
}
static void restart(void) {
    device.offline=0; device.calls=device.reads=device.writes=device.syncs=0;
    device.fail_at=0; device.mode=0; device.cut=0;
    memset(device.dirty,0,sizeof(device.dirty));
    memcpy(device.cache,device.durable,sizeof(device.cache));
}
static void pattern(uint8_t sector[512],unsigned block,unsigned generation) {
    for(unsigned i=0;i<512;i++) sector[i]=(uint8_t)(block*37u+i*19u+generation*71u+(i>>7));
}
static void initialize(void) {
    memset(&device,0,sizeof(device)); memset(device.durable,0xa6,sizeof(device.durable));
    for(unsigned block=0;block<256;block++) pattern(device.durable[image_lba(block)],block,0);
    memset(device.durable[7],0,512); memset(device.durable[501],0,512); restart();
}
static void open_ok(struct kui_vmu_store *s) {
    struct kui_vmu_store_io i=io(); struct kui_vmu_store_config c=config();
    assert(kui_vmu_store_open(s,&i,&c)==0);
}
static void check_image(struct kui_vmu_store *s,unsigned changed,unsigned gen) {
    uint8_t expected[512],actual[512];
    for(unsigned b=0;b<256;b++) {
        unsigned g=b==37?1:0; if(b==changed) g=gen;
        pattern(expected,b,g);
        assert(kui_vmu_store_read(s,b,actual)==0&&!memcmp(actual,expected,512));
    }
    for(unsigned lba=0;lba<CARD_SECTORS;lba++) if(!allowed(lba))
        for(unsigned j=0;j<512;j++) assert(device.durable[lba][j]==0xa6);
}
static void success_and_mapping_copy(void) {
    struct kui_vmu_store s; uint8_t sector[512],out[512];
    initialize(); open_ok(&s);
    for(unsigned b=0;b<256;b++) {
        pattern(sector,b,1); assert(kui_vmu_store_write(&s,b,sector)==0);
        memset(out,0,sizeof(out)); assert(kui_vmu_store_read(&s,b,out)==0);
        assert(!memcmp(out,sector,512));
    }
    restart(); open_ok(&s);
    for(unsigned b=0;b<256;b++) {
        pattern(sector,b,1); assert(kui_vmu_store_read(&s,b,out)==0&&!memcmp(out,sector,512));
    }
    assert(kui_vmu_store_sync(&s)==0);
    /* A prepared launch can release its input descriptor after open. */
    struct kui_vmu_store_extent e[3]; memcpy(e,image_map,sizeof(e));
    struct kui_vmu_store_config c=config(); struct kui_vmu_store_io i=io();
    c.image_extents=e; assert(kui_vmu_store_open(&s,&i,&c)==0);
    e[0].first_lba=0; memset(c.image_id,0,16);
    pattern(sector,0,2); assert(kui_vmu_store_write(&s,0,sector)==0);
    assert(!memcmp(device.durable[image_lba(0)],sector,512));
}
static void power_loss_matrix(void) {
    struct kui_vmu_store s; uint8_t next[512],old[512],out[512];
    initialize(); open_ok(&s); pattern(next,37,1);
    assert(kui_vmu_store_write(&s,37,next)==0);
    memcpy(snapshot,device.durable,sizeof(snapshot));
    unsigned cases=0;
    for(unsigned fail=1;fail<=8;fail++) for(int mode=0;mode<3;mode++) {
        unsigned cuts=mode==2?513:1;
        for(unsigned cut=0;cut<cuts;cut++) {
            memcpy(device.durable,snapshot,sizeof(snapshot)); restart(); open_ok(&s);
            device.calls=0; device.fail_at=fail; device.mode=mode; device.cut=cut;
            pattern(next,199,2); pattern(old,199,0);
            assert(kui_vmu_store_write(&s,199,next)==KUI_VMU_STORE_IO);
            unsigned calls=device.calls;
            assert(kui_vmu_store_read(&s,199,out)==KUI_VMU_STORE_BLOCKED);
            assert(kui_vmu_store_write(&s,199,next)==KUI_VMU_STORE_BLOCKED);
            assert(kui_vmu_store_sync(&s)==KUI_VMU_STORE_BLOCKED&&device.calls==calls);
            restart(); open_ok(&s);
            assert(kui_vmu_store_read(&s,199,out)==0);
            unsigned generation=!memcmp(out,next,512)?2:0;
            assert(generation==2||!memcmp(out,old,512));
            if(fail>=7) assert(generation==2); /* durable commit already exists */
            if(fail<=4) assert(generation==0); /* marker not yet committed */
            check_image(&s,199,generation);
            /* Recovery and retained-marker reuse must themselves be durable. */
            restart(); open_ok(&s); check_image(&s,199,generation);
            pattern(next,199,3); assert(kui_vmu_store_write(&s,199,next)==0);
            restart(); open_ok(&s); check_image(&s,199,3); ++cases;
        }
    }
    printf("PASS VMU store %u interrupted write/barrier cases (all tear boundaries)\n",cases);
}
static void clean_checkpoint(void) {
    struct kui_vmu_store s; uint8_t sector[512],out[512];
    initialize(); open_ok(&s); pattern(sector,37,1);
    assert(kui_vmu_store_write(&s,37,sector)==0);
    assert(kui_vmu_store_checkpoint(&s)==0);
    for(unsigned n=0;n<512;n++) assert(device.durable[501][n]==0);
    restart(); open_ok(&s); check_image(&s,256,0);
    /* The base can now be managed as a normal VMD. An out-of-band import
     * following a successful checkpoint cannot be undone by stale redo. */
    pattern(sector,37,4); memcpy(device.durable[image_lba(37)],sector,512);
    restart(); open_ok(&s);
    assert(kui_vmu_store_read(&s,37,out)==0&&!memcmp(out,sector,512));
    for(unsigned fail=1;fail<=3;fail++) for(unsigned cut=0;cut<=512;cut++) {
        initialize(); open_ok(&s); pattern(sector,37,1);
        assert(kui_vmu_store_write(&s,37,sector)==0);
        device.calls=0; device.fail_at=fail; device.mode=2; device.cut=cut;
        assert(kui_vmu_store_checkpoint(&s)==KUI_VMU_STORE_IO);
        restart(); open_ok(&s); check_image(&s,256,0);
    }
    assert(kui_vmu_store_checkpoint(NULL)==KUI_VMU_STORE_ARGUMENT);
}
static uint32_t oracle_crc(const uint8_t *p,size_t n) {
    uint32_t c=UINT32_MAX;
    while(n--) { c^=*p++; for(unsigned b=0;b<8;b++) c=(c>>1)^((c&1)?0xedb88320u:0); }
    return c^UINT32_MAX;
}
static void put32(uint8_t *p,uint32_t v) {
    for(unsigned n=0;n<4;n++) p[n]=(uint8_t)(v>>(8*n));
}
static void bad_maps(void) {
    struct kui_vmu_store s; struct kui_vmu_store_io i=io();
    struct kui_vmu_store_config c; struct kui_vmu_store_extent e[3],j[2];
    initialize();
#define BAD_MAP(change) do { c=config(); memcpy(e,image_map,sizeof(e)); memcpy(j,journal_map,sizeof(j)); \
    c.image_extents=e; c.journal_extents=j; change; unsigned before=device.calls; \
    assert(kui_vmu_store_open(&s,&i,&c)==KUI_VMU_STORE_MAPPING&&device.calls==before); \
    assert(kui_vmu_store_sync(&s)==KUI_VMU_STORE_BLOCKED); } while(0)
    BAD_MAP(c.card_sectors=0);
    BAD_MAP(c.card_sectors=501);
    BAD_MAP(memset(c.image_id,0,16));
    BAD_MAP(c.image_extents=NULL);
    BAD_MAP(c.image_extent_count=0);
    BAD_MAP(c.image_extent_count=KUI_VMU_STORE_MAX_EXTENTS+1);
    BAD_MAP(c.journal_extents=NULL);
    BAD_MAP(c.journal_extent_count=0);
    BAD_MAP(c.journal_extent_count=KUI_VMU_STORE_MAX_EXTENTS+1);
    BAD_MAP(e[0].sectors=0);
    BAD_MAP(e[0].sectors=76);
    BAD_MAP(e[0].sectors=78);
    BAD_MAP(e[1].first_lba=31);
    BAD_MAP(e[1].first_lba=UINT64_MAX-2);
    BAD_MAP(e[1].sectors=UINT32_MAX);
    BAD_MAP(j[1].first_lba=7);
    BAD_MAP(j[1].first_lba=30);
    BAD_MAP(j[0].sectors=2);
    BAD_MAP(j[1].first_lba=UINT64_MAX);
#undef BAD_MAP
    assert(kui_vmu_store_open(NULL,&i,&c)==KUI_VMU_STORE_ARGUMENT);
    assert(kui_vmu_store_open(&s,NULL,&c)==KUI_VMU_STORE_ARGUMENT);
    assert(kui_vmu_store_open(&s,&i,NULL)==KUI_VMU_STORE_ARGUMENT);
    i.sync=NULL; c=config(); assert(kui_vmu_store_open(&s,&i,&c)==KUI_VMU_STORE_ARGUMENT);
    i=io(); open_ok(&s); unsigned before=device.calls; uint8_t b[512];
    assert(kui_vmu_store_read(&s,256,b)==KUI_VMU_STORE_ARGUMENT);
    assert(kui_vmu_store_write(&s,256,b)==KUI_VMU_STORE_ARGUMENT);
    assert(kui_vmu_store_read(&s,0,NULL)==KUI_VMU_STORE_ARGUMENT);
    assert(kui_vmu_store_write(&s,0,NULL)==KUI_VMU_STORE_ARGUMENT&&device.calls==before);
}
static void corruption_and_recovery_errors(void) {
    struct kui_vmu_store s; struct kui_vmu_store_io i=io();
    struct kui_vmu_store_config c=config(); uint8_t b[512];
    initialize(); open_ok(&s); pattern(b,37,1); assert(kui_vmu_store_write(&s,37,b)==0);
    memcpy(snapshot,device.durable,sizeof(snapshot));
    assert(oracle_crc((const uint8_t *)"123456789",9)==0xcbf43926u);
    assert(oracle_crc(snapshot[501],508)==
        ((uint32_t)snapshot[501][508]|(uint32_t)snapshot[501][509]<<8|
         (uint32_t)snapshot[501][510]<<16|(uint32_t)snapshot[501][511]<<24));
    for(unsigned kind=0;kind<6;kind++) {
        struct kui_vmu_store_extent reordered[3]={image_map[1],image_map[0],image_map[2]};
        memcpy(device.durable,snapshot,sizeof(snapshot));
        if(kind==0) device.durable[7][123]^=1;
        if(kind==1) c.image_id[0]^=1;
        if(kind==2) c.card_sectors++;
        if(kind==5) c.image_extents=reordered;
        if(kind==3||kind==4) {
            put32(device.durable[501]+(kind==3?8:44),kind==3?2:256);
            put32(device.durable[501]+508,oracle_crc(device.durable[501],508));
        }
        restart(); unsigned writes=device.writes;
        int result=kui_vmu_store_open(&s,&i,&c);
        assert(result==(kind==1||kind==2||kind==5?KUI_VMU_STORE_IDENTITY:KUI_VMU_STORE_CORRUPT));
        assert(device.writes==writes&&kui_vmu_store_read(&s,37,b)==KUI_VMU_STORE_BLOCKED);
        c=config();
    }
    /* Every read/write/barrier in recovery may fail; each restart repeats the
     * same committed redo, even if the preceding recovery tore its base. */
    for(unsigned fail=1;fail<=5;fail++) for(int mode=0;mode<3;mode++) {
        memcpy(device.durable,snapshot,sizeof(snapshot)); restart();
        device.fail_at=fail; device.mode=mode; device.cut=273;
        assert(kui_vmu_store_open(&s,&i,&c)==KUI_VMU_STORE_IO);
        restart(); open_ok(&s); check_image(&s,256,0);
    }
    memcpy(device.durable,snapshot,sizeof(snapshot)); restart(); open_ok(&s);
    device.calls=0; device.fail_at=1;
    assert(kui_vmu_store_read(&s,0,b)==KUI_VMU_STORE_IO);
    assert(kui_vmu_store_sync(&s)==KUI_VMU_STORE_BLOCKED);
    restart(); open_ok(&s); device.calls=0; device.fail_at=1;
    assert(kui_vmu_store_sync(&s)==KUI_VMU_STORE_IO);
    assert(kui_vmu_store_read(&s,0,b)==KUI_VMU_STORE_BLOCKED);
}
static void reopen_with_volatile_commit(void) {
    struct kui_vmu_store s;
    struct kui_vmu_store_io i=io(); struct kui_vmu_store_config c=config();
    uint8_t next[512],out[512],old[512];
    /* Marker is fully in a write-back cache but its sync reports failure.
     * Reopening the store in the same session must establish a redo barrier
     * before issuing even one base write. Fail that new barrier with a tear. */
    for(unsigned cut=0;cut<=512;cut++) {
        initialize(); open_ok(&s); pattern(next,37,1);
        assert(kui_vmu_store_write(&s,37,next)==0);
        device.calls=0; device.fail_at=6; device.mode=3;
        pattern(next,199,2); pattern(old,199,0);
        assert(kui_vmu_store_write(&s,199,next)==KUI_VMU_STORE_IO);
        assert(!device.offline&&memcmp(device.cache[501],device.durable[501],512));
        unsigned writes=device.writes;
        device.calls=0; device.fail_at=3; device.mode=2; device.cut=cut;
        assert(kui_vmu_store_open(&s,&i,&c)==KUI_VMU_STORE_IO);
        assert(device.writes==writes&&!memcmp(device.durable[image_lba(199)],old,512));
        restart(); open_ok(&s); assert(kui_vmu_store_read(&s,199,out)==0);
        assert(!memcmp(out,old,512)||!memcmp(out,next,512));
        check_image(&s,199,!memcmp(out,next,512)?2:0);
    }
    /* Every ambiguous transaction failure can also be retried without a
     * device reset. A later acknowledged write must survive a real reset. */
    for(unsigned fail=1;fail<=8;fail++) {
        initialize(); open_ok(&s); pattern(next,37,1);
        assert(kui_vmu_store_write(&s,37,next)==0);
        device.calls=0; device.fail_at=fail; device.mode=3; pattern(next,199,2);
        assert(kui_vmu_store_write(&s,199,next)==KUI_VMU_STORE_IO&&!device.offline);
        device.fail_at=0; assert(kui_vmu_store_open(&s,&i,&c)==0);
        pattern(next,199,3); assert(kui_vmu_store_write(&s,199,next)==0);
        restart(); open_ok(&s); check_image(&s,199,3);
    }
}
struct wide_device { uint64_t last_lba; unsigned writes; };
static int wide_read(void *ctx,uint64_t lba,void *sector) {
    struct wide_device *w=ctx; w->last_lba=lba; memset(sector,0,512); return 0;
}
static int wide_write(void *ctx,uint64_t lba,const void *sector) {
    struct wide_device *w=ctx; (void)sector; w->last_lba=lba; ++w->writes; return 0;
}
static int wide_sync(void *ctx) { (void)ctx; return 0; }
static void capacity_edges(void) {
    struct kui_vmu_store s; struct wide_device w={0}; uint8_t b[512]={0};
    struct kui_vmu_store_io i={&w,wide_read,wide_write,wide_sync};
    struct kui_vmu_store_extent e={UINT64_MAX-256,256},j={1,2};
    struct kui_vmu_store_config c=config(); c.card_sectors=UINT64_MAX;
    c.image_extents=&e; c.image_extent_count=1; c.journal_extents=&j; c.journal_extent_count=1;
    assert(kui_vmu_store_open(&s,&i,&c)==0);
    assert(kui_vmu_store_write(&s,255,b)==0&&w.last_lba==UINT64_MAX-1);
    assert(kui_vmu_store_read(&s,0,b)==0&&w.last_lba==UINT64_MAX-256);
    ++e.first_lba; unsigned writes=w.writes;
    assert(kui_vmu_store_open(&s,&i,&c)==KUI_VMU_STORE_MAPPING&&w.writes==writes);
    /* Exact low-card endpoint is legal; one past the endpoint is not. */
    c.card_sectors=258; e.first_lba=2; j.first_lba=0;
    assert(kui_vmu_store_open(&s,&i,&c)==0);
    assert(kui_vmu_store_write(&s,255,b)==0&&w.last_lba==257);
    c.card_sectors=257; assert(kui_vmu_store_open(&s,&i,&c)==KUI_VMU_STORE_MAPPING);
}
int main(void) {
    success_and_mapping_copy(); bad_maps(); capacity_edges(); clean_checkpoint();
    corruption_and_recovery_errors(); reopen_with_volatile_commit(); power_loss_matrix();
    printf("PASS VMU fixed-extent store (%zu bytes RAM, plain128KiB image, 1KiB journal)\n",sizeof(struct kui_vmu_store));
    return 0;
}
