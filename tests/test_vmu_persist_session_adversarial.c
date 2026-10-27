/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent sparse physical-card oracle. Metadata is never a writable
 * device slot; writes outside the exact256+2 useful sectors are assertions. */
#include "kui/vmu_fs_admission.h"
#include "kui/vmu_image.h"
#include "kui/vmu_store.h"
#include "kui/vmu_persist_session.h"
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
#define SLOTS 258u

static struct {
    struct kui_vmu_fs_input input;
    struct kui_vmu_fs_workspace work;
    struct kui_vmu_persist_profile profile;
    uint32_t fat[CLUSTERS+2u];
    uint8_t boot[512],dirs[4][512],identity[512];
    uint8_t durable[SLOTS][512],cache[SLOTS][512],dirty[SLOTS];
    struct kui_vmu_persist_session_identity actual_identity;
    const struct kui_vmu_persist_session *active_session;
    unsigned phase_events[7],failure_phase,matching_events;
    unsigned reads,writes,syncs,calls,fail_at,cut,mode,offline;
} disk;
static uint8_t before[SLOTS][512];

static void put16(uint8_t *p,uint16_t value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void put32(uint8_t *p,uint32_t value) {
    put16(p,(uint16_t)value);put16(p+2u,(uint16_t)(value>>16));
}
static uint32_t cluster_lba(uint32_t cluster) {return DATA_FIRST+(cluster-2u)*SPC;}
static int physical_slot(uint64_t lba) {
    uint32_t base=cluster_lba(BASE_FIRST),journal=cluster_lba(JOURNAL);
    if(lba>=base && lba-base<256u) return (int)(lba-base);
    if(lba>=journal && lba-journal<2u) return (int)(256u+lba-journal);
    return -1;
}
static void power_cut(void) {
    disk.offline=1u;memcpy(disk.cache,disk.durable,sizeof(disk.cache));
    memset(disk.dirty,0,sizeof(disk.dirty));
}
static int should_fail(void) {
    assert(!disk.offline);++disk.calls;
    unsigned phase=disk.active_session?disk.active_session->phase:0u;
    if(phase<7u) ++disk.phase_events[phase];
    if(disk.failure_phase) {
        if(phase!=disk.failure_phase) return 0;
        ++disk.matching_events;
        return disk.fail_at && disk.matching_events==disk.fail_at;
    }
    return disk.fail_at && disk.calls==disk.fail_at;
}
static int card_read(void *context,uint32_t lba,uint8_t out[512]) {
    assert(context==&disk && lba<PARTITION+VOLUME_SECTORS);++disk.reads;
    if(should_fail()) {power_cut();return -1;}
    memset(out,0,512u);
    if(!lba) {
        out[446u+4u]=0x0cu;put32(out+446u+8u,PARTITION);
        put32(out+446u+12u,VOLUME_SECTORS);put16(out+510u,0xaa55u);return 0;
    }
    assert(lba>=PARTITION);
    int slot=physical_slot(lba);
    if(slot>=0) memcpy(out,disk.cache[slot],512u);
    else if(lba==PARTITION || lba==PARTITION+6u) memcpy(out,disk.boot,512u);
    else if(lba==PARTITION+1u || lba==PARTITION+7u) {
        put32(out,UINT32_C(0x41615252));put32(out+484u,UINT32_C(0x61417272));
        put32(out+488u,UINT32_MAX);put32(out+492u,UINT32_MAX);
        put32(out+508u,UINT32_C(0xaa550000));
    } else if(lba>=PARTITION+RESERVED && lba<PARTITION+RESERVED+FAT_SECTORS) {
        size_t first=(size_t)(lba-PARTITION-RESERVED)*128u;
        for(size_t i=0;i<128u && first+i<CLUSTERS+2u;i++) put32(out+4u*i,disk.fat[first+i]);
    } else {
        for(unsigned i=0;i<4u;i++) if(lba==cluster_lba(2u+i)) memcpy(out,disk.dirs[i],512u);
        if(lba==cluster_lba(IDENTITY)) memcpy(out,disk.identity,512u);
    }
    return 0;
}
static int card_write(void *context,uint64_t lba,const void *bytes) {
    assert(context==&disk);int slot=physical_slot(lba);assert(slot>=0);++disk.writes;
    if(should_fail()) {
        unsigned n=disk.mode==1u?disk.cut:disk.mode==2u?512u:0u;
        memcpy(disk.durable[slot],bytes,n);power_cut();return -1;
    }
    memcpy(disk.cache[slot],bytes,512u);disk.dirty[slot]=1u;return 0;
}
static int card_sync(void *context) {
    assert(context==&disk);++disk.syncs;
    if(should_fail()) {
        unsigned n=disk.mode==1u?disk.cut:disk.mode==2u?512u:0u;
        for(unsigned i=0;i<SLOTS;i++) if(disk.dirty[i]) memcpy(disk.durable[i],disk.cache[i],n);
        power_cut();return -1;
    }
    for(unsigned i=0;i<SLOTS;i++) if(disk.dirty[i]) memcpy(disk.durable[i],disk.cache[i],512u);
    memset(disk.dirty,0,sizeof(disk.dirty));return 0;
}
static void short_entry(uint8_t *entry,const char name[11],unsigned attributes,
    uint32_t first,uint32_t bytes) {
    memset(entry,0,32u);memcpy(entry,name,11u);entry[11]=(uint8_t)attributes;
    put16(entry+20u,(uint16_t)(first>>16));put16(entry+26u,(uint16_t)first);put32(entry+28u,bytes);
}
static uint8_t short_checksum(const uint8_t name[11]) {
    uint8_t sum=0;for(unsigned i=0;i<11u;i++) sum=(uint8_t)((sum>>1)|(sum<<7))+name[i];return sum;
}
static void journal_entries(uint8_t *lfn) {
    static const unsigned offsets[13]={1,3,5,7,9,14,16,18,20,22,24,28,30};
    static const char name[]="card.journal";
    short_entry(lfn+32u,"CARD~1  JOU",0x20u,JOURNAL,1024u);
    memset(lfn,0xff,32u);lfn[0]=0x41u;lfn[11]=0x0fu;lfn[12]=0;
    lfn[13]=short_checksum(lfn+32u);put16(lfn+26u,0);
    for(unsigned i=0;i<13u;i++) put16(lfn+offsets[i],(uint16_t)(unsigned char)name[i]);
}
static int snapshot_read(void *context,unsigned role,uint32_t logical,uint8_t out[512]) {
    assert(context==&disk);uint32_t lba;
    if(kui_vmu_persist_profile_map_lba(&disk.profile,role,logical,&lba)) return -1;
    return card_read(context,lba,out);
}
static void restart(void) {
    disk.offline=disk.reads=disk.writes=disk.syncs=disk.calls=disk.fail_at=0u;
    disk.cut=disk.mode=0u;memcpy(disk.cache,disk.durable,sizeof(disk.cache));
    memset(disk.dirty,0,sizeof(disk.dirty));
    disk.active_session=NULL;disk.failure_phase=disk.matching_events=0u;
    memset(disk.phase_events,0,sizeof(disk.phase_events));
}
static void fresh(void) {
    memset(&disk,0,sizeof(disk));
    disk.input=(struct kui_vmu_fs_input){.read_sector=card_read,.context=&disk,
        .card_sectors=PARTITION+VOLUME_SECTORS,.partition_first=PARTITION,
        .partition_end=PARTITION+VOLUME_SECTORS,.high_capacity=1u,
        .pair_path="/KUI/vmu-test/p0001"};
    for(unsigned i=0;i<16u;i++) {disk.input.cid[i]=(uint8_t)(0x51u+i);disk.input.image_id[i]=(uint8_t)(0xa1u+i);}
    memcpy(disk.identity,disk.input.image_id,16u);
    memcpy(disk.actual_identity.cid,disk.input.cid,16u);
    disk.actual_identity.card_sectors=disk.input.card_sectors;
    disk.actual_identity.high_capacity=disk.input.high_capacity;
    uint8_t *b=disk.boot;b[0]=0xebu;b[1]=0x58u;b[2]=0x90u;
    memcpy(b+3u,"MSDOS5.0",8u);put16(b+11u,512u);b[13]=SPC;put16(b+14u,RESERVED);
    b[16]=1u;b[21]=0xf8u;put16(b+24u,63u);put16(b+26u,255u);put32(b+28u,PARTITION);
    put32(b+32u,VOLUME_SECTORS);put32(b+36u,FAT_SECTORS);put32(b+44u,2u);
    put16(b+48u,1u);put16(b+50u,6u);b[64]=0x80u;b[66]=0x29u;
    put32(b+67u,UINT32_C(0xa5b6c7d8));memcpy(b+71u,"NO NAME    ",11u);
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
    for(unsigned block=0;block<256u;block++) assert(!kui_vmu_image_blank_block((uint16_t)block,disk.durable[block]));
    restart();assert(!kui_vmu_fs_collect(&disk.input,&disk.profile,&disk.work));
    assert(!kui_vmu_persist_profile_collect_snapshot(&disk.profile,snapshot_read,&disk));
    assert(disk.profile.files[0].useful[0].lba==cluster_lba(BASE_FIRST));
    assert(disk.profile.files[1].useful[0].lba==cluster_lba(JOURNAL));
    restart();memcpy(before,disk.durable,sizeof(before));
}

static int raw_read(void *context,uint64_t lba,void *out) {
    assert(lba<=UINT32_MAX);return card_read(context,(uint32_t)lba,out);
}
static int raw_identity(void *context,struct kui_vmu_persist_session_identity *out) {
    assert(context==&disk);
    if(should_fail()) {power_cut();return -1;}
    *out=disk.actual_identity;return 0;
}
static bool raw_healthy(void *context) {assert(context==&disk);return !disk.offline;}
static struct kui_vmu_persist_session_ops operations(void) {
    struct kui_vmu_persist_session_ops ops={&disk,raw_identity,raw_read,card_write,card_sync,raw_healthy};
    return ops;
}
static struct kui_vmu_persist_session *new_session(int *result) {
    struct kui_vmu_persist_session *s=calloc(1,sizeof(*s));assert(s);
    disk.active_session=s;
    struct kui_vmu_persist_session_ops ops=operations();
    struct kui_vmu_image_workspace image_work;
    *result=kui_vmu_persist_session_init(s,&disk.profile,&disk.input,&ops,&disk.work,&image_work);
    return s;
}
static struct kui_vmu_persist_session *open_ok(void) {
    int result;struct kui_vmu_persist_session *s=new_session(&result);assert(result==0);
    assert(s->ready && s->admitted && s->snapshot_valid);return s;
}
static void count_from_zero(void) {disk.calls=disk.reads=disk.writes=disk.syncs=0u;}
static void next_block(uint8_t out[512]) {
    for(unsigned i=0;i<512u;i++) out[i]=(uint8_t)(0x35u+17u*i+(i>>3));
}
static void check_other_image_blocks(unsigned except) {
    for(unsigned block=0;block<256u;block++) if(block!=except)
        assert(!memcmp(disk.durable[block],before[block],512u));
}
static int forbidden_fs_read(void *context,uint32_t lba,uint8_t out[512]) {
    (void)context;(void)lba;(void)out;
    assert(!"session admission must use the same raw backend as recovery");return -1;
}
static void actual_maps_and_mutable_provenance(void) {
    fresh();disk.input.read_sector=forbidden_fs_read;disk.input.context=NULL;
    struct kui_vmu_persist_session *s=open_ok();uint8_t next[512],out[512];next_block(next);
    count_from_zero();assert(!kui_vmu_persist_session_commit(s,199u,next));
    assert(disk.writes==4u && !memcmp(disk.durable[199u],next,512u));
    assert(!s->snapshot_valid && !kui_vmu_persist_session_read(s,199u,out) && !memcmp(out,next,512u));
    assert(memcmp(disk.durable[257u],before[257u],512u)); /* Valid redo remains. */
    check_other_image_blocks(199u);free(s);power_cut();restart();
    s=open_ok(); /* The original P2 hashes deliberately no longer match. */
    assert(disk.writes==1u && !kui_vmu_persist_session_read(s,199u,out));
    assert(!memcmp(out,next,512u));check_other_image_blocks(199u);
    assert(!kui_vmu_persist_session_checkpoint(s));
    for(unsigned i=0;i<512u;i++) assert(!disk.durable[257u][i]);
    free(s);
}
static int synthetic_read(void *context,uint64_t lba,void *out) {
    assert(lba<SLOTS);return raw_read(context,lba<256u?cluster_lba(BASE_FIRST)+lba:cluster_lba(JOURNAL)+lba-256u,out);
}
static int synthetic_write(void *context,uint64_t lba,const void *data) {
    assert(lba<SLOTS);return card_write(context,lba<256u?cluster_lba(BASE_FIRST)+lba:cluster_lba(JOURNAL)+lba-256u,data);
}
static void seed_pending(unsigned synthetic,unsigned bad_identity,unsigned damaged_base) {
    struct kui_vmu_store store;
    struct kui_vmu_store_extent image={synthetic?0u:cluster_lba(BASE_FIRST),256u};
    struct kui_vmu_store_extent journal={synthetic?256u:cluster_lba(JOURNAL),2u};
    struct kui_vmu_store_config config={.card_sectors=synthetic?258u:disk.input.card_sectors,
        .image_extents=&image,.image_extent_count=1u,.journal_extents=&journal,.journal_extent_count=1u};
    memcpy(config.image_id,disk.input.image_id,16u);if(bad_identity) config.image_id[0]^=1u;
    struct kui_vmu_store_io io={&disk,synthetic?synthetic_read:raw_read,
        synthetic?synthetic_write:card_write,card_sync};
    assert(!kui_vmu_store_open(&store,&io,&config));
    uint8_t root[512];memcpy(root,disk.durable[255u],512u);root[0x30u]=0x21u;
    assert(!kui_vmu_store_write(&store,255u,root));
    if(damaged_base) disk.durable[255u][0]=0u; /* A torn base needs redo before validation. */
    restart();memcpy(before,disk.durable,sizeof(before));
}
static void recovery_precedes_vmu_validation(void) {
    fresh();seed_pending(0u,0u,1u);struct kui_vmu_persist_session *s=open_ok();
    assert(disk.writes==1u && disk.durable[255u][0]==0x55u && disk.durable[255u][0x30u]==0x21u);
    check_other_image_blocks(255u);free(s);
    for(unsigned kind=0;kind<3u;kind++) {
        fresh();seed_pending(kind==0u,kind==1u,1u);
        if(kind==2u) {disk.durable[256u][17u]^=1u;restart();memcpy(before,disk.durable,sizeof(before));}
        int result;s=new_session(&result);assert(result && !disk.writes);
        assert(!memcmp(before,disk.durable,sizeof(before)) && !s->ready);free(s);
    }
}
static void failed_admission_never_replays(void) {
    for(unsigned kind=0;kind<7u;kind++) {
        fresh();seed_pending(0u,0u,1u);
        struct kui_vmu_fs_range range={cluster_lba(JOURNAL)+7u,1u};
        if(kind==0u) disk.actual_identity.cid[0]^=1u;
        if(kind==1u) disk.actual_identity.card_sectors--;
        if(kind==2u) disk.dirs[3][128u+11u]|=1u;
        if(kind==3u) disk.fat[100u]=JOURNAL;
        if(kind==4u) disk.identity[4u]^=1u;
        if(kind==5u) {disk.input.forbidden=&range;disk.input.forbidden_count=1u;}
        if(kind==6u) short_entry(disk.dirs[0]+32u,"ALIAS   BIN",0x20u,BASE_FIRST+4u,512u);
        int result;struct kui_vmu_persist_session *s=new_session(&result);
        assert(result && !disk.writes && !memcmp(before,disk.durable,sizeof(before)));
        assert(!s->ready);free(s);
    }
}
static void every_commit_failure_recovers_exact_old_or_new(void) {
    fresh();struct kui_vmu_persist_session *s=open_ok();uint8_t next[512],out[512];next_block(next);
    count_from_zero();assert(!kui_vmu_persist_session_commit(s,199u,next));
    unsigned events=disk.calls;free(s);assert(events>=8u);
    static const unsigned cuts[]={0u,1u,70u,257u,511u,512u};unsigned cases=0u;
    for(unsigned fail=1u;fail<=events;fail++) for(unsigned mode=0u;mode<3u;mode++)
        for(unsigned cut=0u;cut<(mode==1u?sizeof(cuts)/sizeof(cuts[0]):1u);cut++) {
            fresh();s=open_ok();count_from_zero();disk.fail_at=fail;disk.mode=mode;disk.cut=cuts[cut];
            assert(kui_vmu_persist_session_commit(s,199u,next)!=0);
            assert(s->faulted && !s->ready);unsigned calls=disk.calls;
            memset(out,0xa5,sizeof(out));uint8_t unchanged[512];memcpy(unchanged,out,sizeof(out));
            assert(kui_vmu_persist_session_read(s,199u,out)!=0 && !memcmp(out,unchanged,sizeof(out)));
            assert(kui_vmu_persist_session_commit(s,199u,next)!=0 && disk.calls==calls);
            free(s);power_cut();restart();s=open_ok();
            assert(!kui_vmu_persist_session_read(s,199u,out));
            assert(!memcmp(out,before[199u],512u) || !memcmp(out,next,512u));
            check_other_image_blocks(199u);free(s);++cases;
        }
    printf("Independent physical session: %u callback/tear failures recovered exact old/new blocks\n",cases);
}
static void every_recovery_failure_retains_replay(void) {
    fresh();seed_pending(0u,0u,1u);struct kui_vmu_persist_session *s=open_ok();
    unsigned events=disk.phase_events[KUI_VMU_PERSIST_SESSION_RECOVERY];free(s);
    assert(events>=5u);static const unsigned cuts[]={0u,1u,70u,257u,511u,512u};unsigned cases=0u;
    for(unsigned fail=1u;fail<=events;fail++) for(unsigned mode=0u;mode<3u;mode++)
        for(unsigned cut=0u;cut<(mode==1u?sizeof(cuts)/sizeof(cuts[0]):1u);cut++) {
            fresh();seed_pending(0u,0u,1u);disk.failure_phase=KUI_VMU_PERSIST_SESSION_RECOVERY;
            disk.fail_at=fail;disk.mode=mode;disk.cut=cuts[cut];
            int result;s=new_session(&result);assert(result && s->faulted && !s->ready);
            unsigned calls=disk.calls;uint8_t out[512];memset(out,0x6a,sizeof(out));
            assert(kui_vmu_persist_session_read(s,255u,out)!=0 && disk.calls==calls);
            for(unsigned i=0;i<512u;i++) assert(out[i]==0x6au);
            free(s);power_cut();restart();s=open_ok();
            assert(!memcmp(disk.durable[255u],disk.durable[256u],512u));
            assert(disk.durable[255u][0]==0x55u && disk.durable[255u][0x30u]==0x21u);
            check_other_image_blocks(255u);free(s);++cases;
        }
    printf("Independent physical session: %u interrupted recovery callbacks retained exact replay\n",cases);
}
int main(void) {
    actual_maps_and_mutable_provenance();recovery_precedes_vmu_validation();
    failed_admission_never_replays();every_commit_failure_recovers_exact_old_or_new();
    every_recovery_failure_retains_replay();
    puts("Independent physical session: fresh admission, real/synthetic journal binding, recovery order and write confinement passed");
    return 0;
}
