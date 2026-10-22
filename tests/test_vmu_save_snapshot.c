/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_save_snapshot.h"
#include "kui/vmu_image_clone.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

enum { BYTES=131072, BLOCK=512, FREE=0xfffc, END=0xfffa };
static uint8_t physical_image[BYTES],before[BYTES],destination[BYTES],out[BLOCK];
static struct kui_vmu_save_snapshot snapshot;
static struct kui_vmu_image_clone clone;
static unsigned reads[256],physical_writes,physical_syncs,destination_writes;
static unsigned forbidden_free_reads;
static int change_block,change_at,change_mode,fail_block,fail_at,reenter;
static unsigned get16(const uint8_t *p) { return p[0]|((unsigned)p[1]<<8); }
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static uint8_t *entry(unsigned slot) {
    assert(slot<208u);return physical_image+(253u-slot/16u)*BLOCK+(slot%16u)*32u;
}
static int read_physical(void *context,uint16_t block,void *data) {
    assert(context==physical_image && block<256u);
    ++reads[block];
    if(block<200u && get16(physical_image+254u*BLOCK+block*2u)==FREE) {
        ++forbidden_free_reads;
        /* A changing unread free block represents the reported black VMU.
         * Reaching it is always a bug, regardless of returned bytes. */
        physical_image[block*BLOCK+12u]^=(uint8_t)reads[block];
        return -1;
    }
    if(reenter) {
        reenter=0;
        assert(kui_vmu_save_snapshot_read_block(&snapshot,block,out)==KUI_VMU_SAVE_SNAPSHOT_STATE);
    }
    if((int)block==fail_block && (int)reads[block]>=fail_at) return -1;
    if((int)block==change_block && (int)reads[block]>=change_at &&
       (change_mode==1 || (int)reads[block]==change_at))
        physical_image[block*BLOCK+12u]^=0x08u;
    memcpy(data,physical_image+block*BLOCK,BLOCK);return 0;
}
static int write_physical(void *context,uint16_t block,const void *data) {
    (void)context;(void)block;(void)data;++physical_writes;return -1;
}
static int sync_physical(void *context) {(void)context;++physical_syncs;return -1;}
static int read_destination(void *context,uint16_t block,void *data) {
    assert(context==destination && block<256u);
    memcpy(data,destination+block*BLOCK,BLOCK);return 0;
}
static int write_destination(void *context,uint16_t block,const void *data) {
    assert(context==destination && block<256u);++destination_writes;
    memcpy(destination+block*BLOCK,data,BLOCK);return 0;
}
static int sync_destination(void *context) {assert(context==destination);return 0;}
static const struct kui_vmu_image_io source={physical_image,read_physical,NULL,NULL};
static const struct kui_vmu_image_io target={destination,read_destination,write_destination,sync_destination};
static void reset(void) {
    memset(&snapshot,0,sizeof(snapshot));memset(&clone,0,sizeof(clone));
    memset(reads,0,sizeof(reads));memset(destination,0,sizeof(destination));
    physical_writes=physical_syncs=destination_writes=forbidden_free_reads=0;
    change_block=fail_block=-1;change_at=fail_at=0;change_mode=0;reenter=0;
    for(unsigned b=0;b<256u;b++)
        assert(!kui_vmu_image_blank_block((uint16_t)b,physical_image+b*BLOCK));
    for(unsigned b=0;b<241u;b++)
        for(unsigned i=0;i<BLOCK;i++) physical_image[b*BLOCK+i]=(uint8_t)(b*19u+i*7u+3u);
    /* Root opaque bytes and inactive-entry bytes must be preserved exactly. */
    physical_image[255u*BLOCK+0x40u]=0x81u;
    for(unsigned s=0;s<208u;s++) {memset(entry(s),0x49,28u);entry(s)[0]=0;}
    memset(out,0xa5,sizeof(out));
}
static void add(unsigned slot,const char *name,const unsigned *chain,unsigned count,unsigned game) {
    uint8_t *e=entry(slot);memset(e,0,32u);e[0]=game?0xcc:0x33;e[1]=0xff;
    put16(e+2,chain[0]);assert(strlen(name)<=12u);memcpy(e+4,name,strlen(name));
    for(unsigned i=16u;i<24u;i++) e[i]=(uint8_t)(slot+i*13u);
    put16(e+24,count);put16(e+26,count-1u);
    for(unsigned i=0;i<count;i++) {
        assert(chain[i]<200u);put16(physical_image+254u*BLOCK+chain[i]*2u,i+1u<count?chain[i+1u]:END);
        for(unsigned p=0;p<BLOCK;p++) physical_image[chain[i]*BLOCK+p]=(uint8_t)(slot*31u+i*23u+p);
    }
}
static void assert_zero(const void *bytes,size_t n) {
    const uint8_t *p=bytes;for(size_t i=0;i<n;i++) assert(!p[i]);
}
static void success(unsigned files,unsigned used,unsigned games) {
    memcpy(before,physical_image,sizeof(before));
    assert(!kui_vmu_save_snapshot_capture(&snapshot,&source,BYTES));
    assert(snapshot.ready && !snapshot.failed);
    assert(snapshot.summary.files==files && snapshot.summary.used_blocks==used &&
        snapshot.summary.game_files==games && snapshot.summary.free_blocks==200u-used);
    for(unsigned b=0;b<256u;b++) {
        assert(!kui_vmu_save_snapshot_read_block(&snapshot,(uint16_t)b,out));
        if(b<200u && get16(before+254u*BLOCK+b*2u)==FREE) {
            assert_zero(out,sizeof(out));assert(reads[b]==0u);
        } else assert(!memcmp(out,before+b*BLOCK,BLOCK));
    }
    assert(!forbidden_free_reads && !physical_writes && !physical_syncs);
    assert(!memcmp(before,physical_image,sizeof(before)));
}
static void free_churn_and_clone(void) {
    reset();const unsigned a[]={199,3,77},mini[]={0,1},z[]={91};
    add(0,"SAVE-OPAQUE",a,3u,0u);add(207,"MINIGAME",mini,2u,1u);add(16,"OTHER",z,1u,0u);
    success(3u,6u,1u);
    const struct kui_vmu_image_io view={&snapshot,kui_vmu_save_snapshot_read_block,NULL,NULL};
    assert(!kui_vmu_image_clone_capture(&clone,&view,BYTES));
    assert(!kui_vmu_image_clone_verify_source(&clone));
    assert(!kui_vmu_image_clone_copy(&clone,&target,BYTES));
    assert(clone.completed && destination_writes==256u);
    for(unsigned b=0;b<256u;b++) {
        if(b<200u && get16(before+254u*BLOCK+b*2u)==FREE) assert_zero(destination+b*BLOCK,BLOCK);
        else assert(!memcmp(destination+b*BLOCK,before+b*BLOCK,BLOCK));
    }
    assert(!forbidden_free_reads && !memcmp(before,physical_image,sizeof(before)));
    /* The troublesome free block may change between ANY two exported reads;
     * it remains invisible because neither capture nor clone requests it. */
    reset();const unsigned save[]={199,50};add(207,"BLACK-SAVE",save,2u,0u);
    success(1u,2u,0u);
    for(unsigned round=0;round<32u;round++) {
        physical_image[12u]^=(uint8_t)(round+1u);
        physical_image[18u]^=0x08u;
        assert(!kui_vmu_save_snapshot_read_block(&snapshot,0u,out));
        assert_zero(out,sizeof(out));assert(reads[0]==0u);
    }
    assert(!kui_vmu_image_clone_capture(&clone,&view,BYTES));
    physical_image[12u]^=0x40u;
    assert(!kui_vmu_image_clone_verify_source(&clone));
    assert(!kui_vmu_image_clone_copy(&clone,&target,BYTES));
    assert_zero(destination,BLOCK);assert(reads[0]==0u && !forbidden_free_reads);
}
static void all_slots_and_full_capacity(void) {
    for(unsigned slot=0;slot<208u;slot++) {
        reset();const unsigned a[]={199,50,3};add(slot,"BOUNDARY",a,3u,0u);success(1u,3u,0u);
    }
    reset();unsigned full[200];for(unsigned i=0;i<200u;i++) full[i]=199u-i;
    add(207,"FULL-CARD",full,200u,0u);success(1u,200u,0u);
    reset();for(unsigned i=0;i<200u;i++) {
        char name[13];(void)snprintf(name,sizeof(name),"FILE%03u",i);
        add(i+8u,name,&i,1u,0u);
    }
    success(200u,200u,0u);
}
static void capture_failure(int result) {
    assert(kui_vmu_save_snapshot_capture(&snapshot,&source,BYTES)==result);
    assert(snapshot.failed && !snapshot.ready);assert_zero(&snapshot.summary,sizeof(snapshot.summary));
    memset(out,0xa5,sizeof(out));assert(kui_vmu_save_snapshot_read_block(&snapshot,5u,out)==result);
    for(unsigned i=0;i<sizeof(out);i++) assert(out[i]==0xa5);
    assert(!forbidden_free_reads && !physical_writes && !physical_syncs);
}
static void changes_and_io_fail_closed(void) {
    const unsigned chain[]={5,6};
    /* Every retained class: allocated data, reserve bytes, directory, FAT,
     * root. Immediate mismatch and coherent later drift must both refuse. */
    const unsigned changed[]={5,200,241,253,254,255};
    for(unsigned i=0;i<sizeof(changed)/sizeof(changed[0]);i++) {
        reset();add(0,"SAVE",chain,2u,0u);change_block=(int)changed[i];change_at=2;change_mode=1;
        capture_failure(KUI_VMU_SAVE_SNAPSHOT_CHANGED);
        reset();add(0,"SAVE",chain,2u,0u);change_block=(int)changed[i];change_at=3;change_mode=2;
        /* On the second physical pair both reads change coherently. */
        capture_failure(KUI_VMU_SAVE_SNAPSHOT_CHANGED);
    }
    for(unsigned i=0;i<sizeof(changed)/sizeof(changed[0]);i++) {
        reset();add(0,"SAVE",chain,2u,0u);fail_block=(int)changed[i];fail_at=1;
        capture_failure(KUI_VMU_SAVE_SNAPSHOT_IO);
        reset();add(0,"SAVE",chain,2u,0u);fail_block=(int)changed[i];fail_at=2;
        capture_failure(KUI_VMU_SAVE_SNAPSHOT_IO);
    }
    reset();add(0,"SAVE",chain,2u,0u);success(1u,2u,0u);
    physical_image[5u*BLOCK+39u]^=1;memset(out,0xa5,sizeof(out));
    assert(kui_vmu_save_snapshot_read_block(&snapshot,5u,out)==KUI_VMU_SAVE_SNAPSHOT_CHANGED);
    for(unsigned i=0;i<sizeof(out);i++) assert(out[i]==0xa5);
    reset();add(0,"SAVE",chain,2u,0u);reenter=1;
    capture_failure(KUI_VMU_SAVE_SNAPSHOT_STATE);
}
static void invalid_filesystems_are_not_filtered(void) {
    reset();put16(physical_image+254u*BLOCK+99u*2u,END);capture_failure(KUI_VMU_SAVE_SNAPSHOT_IMAGE);
    reset();const unsigned chain[]={1,2};add(0,"BAD-MINI",chain,2u,1u);
    capture_failure(KUI_VMU_SAVE_SNAPSHOT_IMAGE);
    reset();add(0,"DUPLICATE",chain,2u,0u);add(207,"DUPLICATE",chain,2u,0u);
    capture_failure(KUI_VMU_SAVE_SNAPSHOT_IMAGE);
    reset();physical_image[255u*BLOCK+0x50u]=199;capture_failure(KUI_VMU_SAVE_SNAPSHOT_IMAGE);
    reset();entry(207)[31]=1;capture_failure(KUI_VMU_SAVE_SNAPSHOT_IMAGE);
    reset();add(0,"CYCLE",chain,2u,0u);put16(physical_image+254u*BLOCK+2u*2u,1u);
    capture_failure(KUI_VMU_SAVE_SNAPSHOT_IMAGE);
}
static void arguments_and_lifetime(void) {
    reset();assert(kui_vmu_save_snapshot_capture(NULL,&source,BYTES)==KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
    assert(kui_vmu_save_snapshot_read_block(NULL,0,out)==KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
    assert(kui_vmu_save_snapshot_read_block(&snapshot,0,out)==KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
    const struct kui_vmu_image_io writable={physical_image,read_physical,write_physical,sync_physical};
    assert(kui_vmu_save_snapshot_capture(&snapshot,&writable,BYTES)==KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
    assert(!physical_writes && !physical_syncs);
    reset();success(0u,0u,0u);
    assert(kui_vmu_save_snapshot_capture(&snapshot,&source,BYTES)==KUI_VMU_SAVE_SNAPSHOT_STATE);
    reset();success(0u,0u,0u);
    assert(kui_vmu_save_snapshot_read_block(&snapshot,256u,out)==KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
    reset();success(0u,0u,0u);
    assert(kui_vmu_save_snapshot_read_block(&snapshot,0,snapshot.first)==KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
    reset();assert(kui_vmu_save_snapshot_capture(&snapshot,&source,BYTES-1u)==KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
    reset();assert(kui_vmu_save_snapshot_capture(&snapshot,&snapshot.source,BYTES)==KUI_VMU_SAVE_SNAPSHOT_ARGUMENT);
}
int main(void) {
    free_churn_and_clone();all_slots_and_full_capacity();changes_and_io_fail_closed();
    invalid_filesystems_are_not_filtered();arguments_and_lifetime();
    printf("VMU save snapshot: free physical blocks never read; full save/metadata preservation, 208 slots, full capacity, clone, drift and refusal checks passed (workspace %zu bytes).\n",sizeof(snapshot));
    return 0;
}
