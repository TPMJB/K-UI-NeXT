/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual clone, VMU validator and SHA/CRC implementations; only logical
 * source/destination block callbacks are replaced by bounded memory media. */
#include "kui/vmu_image_clone.h"
#include "kui/hash.h"
#include "kui/retail_image.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct kui_vmu_image_clone work;
static struct {
    uint8_t source[256][512],destination[256][512],before[256][512];
    unsigned source_reads,reads,writes,syncs,source_writes;
    unsigned fail_source,fail_read,fail_write,fail_sync;
    unsigned mutate_source_at,corrupt_destination_at,reenter_at;
    unsigned body_synced,root_written,post_root_synced;
} media;
static struct kui_vmu_image_io source,destination;
static int source_write_forbidden(void *context,uint16_t block,const void *data) {
    (void)context;(void)block;(void)data;++media.source_writes;return -1;
}
static int read_source(void *context,uint16_t block,void *data) {
    assert(context==&media && block<256u && data);++media.source_reads;
    if(media.source_reads==media.fail_source) return -1;
    if(media.source_reads==media.mutate_source_at) media.source[255][100]^=1u;
    if(media.source_reads==media.reenter_at)
        assert(kui_vmu_image_clone_verify_source(&work)<0);
    memcpy(data,media.source[block],512u);return 0;
}
static int read_destination(void *context,uint16_t block,void *data) {
    assert(context==&media && block<256u && data);++media.reads;
    if(media.reads==media.fail_read) return -1;
    if(media.reads==media.corrupt_destination_at) media.destination[block][100]^=1u;
    memcpy(data,media.destination[block],512u);return 0;
}
static int write_destination(void *context,uint16_t block,const void *data) {
    assert(context==&media && block<256u && data);++media.writes;
    if(block==255u) {
        assert(media.writes==256u && media.body_synced && !media.root_written);
        media.root_written=1u;
    } else {
        assert(!media.root_written && block==media.writes-1u);
        for(unsigned i=0;i<512u;i++) assert(!media.destination[255][i]);
    }
    if(media.writes==media.fail_write) return -1;
    memcpy(media.destination[block],data,512u);return 0;
}
static int sync_destination(void *context) {
    assert(context==&media);++media.syncs;
    if(media.syncs==media.fail_sync) return -1;
    if(!media.root_written) {assert(media.writes==255u);media.body_synced=1u;}
    else {assert(media.writes==256u && media.body_synced);media.post_root_synced=1u;}
    return 0;
}
static void put16(uint8_t *p,unsigned value) {p[0]=(uint8_t)value;p[1]=(uint8_t)(value>>8);}
static void setup(void) {
    memset(&work,0,sizeof(work));memset(&media,0,sizeof(media));
    for(unsigned block=0;block<256u;block++) {
        assert(!kui_vmu_image_blank_block((uint16_t)block,media.source[block]));
        if(block<241u) for(unsigned i=0;i<512u;i++) media.source[block][i]=(uint8_t)(block*7u+i*13u);
    }
    uint8_t *entry=media.source[253];entry[0]=0x33u;entry[1]=0xffu;
    put16(entry+2u,198u);memcpy(entry+4u,"PROTECTED",9u);put16(entry+24u,2u);
    put16(media.source[254]+198u*2u,197u);put16(media.source[254]+197u*2u,0xfffau);
    /* A physical card's opaque root metadata need not match our formatter.
     * Include every unknown byte in the exact-copy and source-change checks. */
    for(unsigned i=0x40u;i<512u;i++) {
        if(i>=0x46u && i<0x52u) continue;
        media.source[255][i]=(uint8_t)(i*29u+7u);
    }
    memcpy(media.before,media.source,sizeof(media.before));
    source=(struct kui_vmu_image_io){&media,read_source,NULL,NULL};
    destination=(struct kui_vmu_image_io){&media,read_destination,write_destination,sync_destination};
}
static void capture(void) {
    assert(!kui_vmu_image_clone_capture(&work,&source,KUI_VMU_IMAGE_BYTES));
    assert(work.captured && !work.failed && !work.active && !work.completed);
    assert(work.summary.files==1u && work.summary.used_blocks==2u && work.summary.free_blocks==198u);
    assert(!memcmp(work.image.names[0],"PROTECTED",9u));
    assert(!media.reads && !media.writes && !media.syncs && !media.source_writes);
}
static void source_unchanged(void) {
    assert(!memcmp(media.source,media.before,sizeof(media.source)) && !media.source_writes);
}
static void no_reuse(void) {
    unsigned sr=media.source_reads,dr=media.reads,dw=media.writes,ds=media.syncs;
    assert(work.failed && !work.captured && !work.completed && !work.active);
    assert(kui_vmu_image_clone_verify_source(&work)<0);
    assert(kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES)<0);
    assert(sr==media.source_reads && dr==media.reads && dw==media.writes && ds==media.syncs);
}
static void success(void) {
    setup();capture();assert(!kui_vmu_image_clone_verify_source(&work));
    assert(!kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES));
    assert(work.completed && !work.failed && !work.active && work.phase==KUI_VMU_CLONE_DONE);
    assert(media.writes==256u && media.syncs==2u && media.post_root_synced);
    assert(!memcmp(media.source,media.destination,sizeof(media.source)));source_unchanged();
    assert(media.destination[253][1]==0xffu); /* Original protected flag retained. */
    struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);
    kui_sha256_update(&hash,media.source,sizeof(media.source));kui_sha256_digest(&hash,digest);
    assert(!memcmp(digest,work.sha256,32u));
    unsigned writes=media.writes;
    assert(kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES)<0 && media.writes==writes);
}
static void invalid_source(void) {
    for(unsigned kind=0;kind<5u;kind++) {
        setup();
        if(kind==0u) media.source[255][0]=0u;
        if(kind==1u) put16(media.source[255]+80u,201u);
        if(kind==2u) put16(media.source[254]+198u*2u,198u); /* Cycle. */
        if(kind==3u) media.source[253][28]=1u;
        if(kind==4u) put16(media.source[254]+196u*2u,0xfffau); /* Orphan. */
        assert(kui_vmu_image_clone_capture(&work,&source,KUI_VMU_IMAGE_BYTES)<0);
        assert(!media.writes && !media.syncs);no_reuse();
    }
    setup();source.write_block=source_write_forbidden;
    assert(kui_vmu_image_clone_capture(&work,&source,KUI_VMU_IMAGE_BYTES)==KUI_VMU_CLONE_ARGUMENT);
    assert(!media.source_reads && !media.source_writes);no_reuse();
    setup();assert(kui_vmu_image_clone_capture(&work,&source,KUI_VMU_IMAGE_BYTES-1u)<0);
    assert(!media.source_reads);no_reuse();
}
static void changed_source(void) {
    setup();capture();media.source[42][12]^=1u;
    assert(kui_vmu_image_clone_verify_source(&work)==KUI_VMU_CLONE_CHANGED);
    assert(!media.writes && !media.reads);no_reuse();
    setup();capture();media.mutate_source_at=media.source_reads+256u+42u;
    assert(kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES)==KUI_VMU_CLONE_CHANGED);
    assert(media.writes==255u && !media.root_written && !media.syncs);no_reuse();
    /* A change during the pinned semantic pass is refused before any copy. */
    setup();media.mutate_source_at=257u;
    assert(kui_vmu_image_clone_capture(&work,&source,KUI_VMU_IMAGE_BYTES)==KUI_VMU_CLONE_CHANGED);
    assert(!media.writes);no_reuse();
}
static void nonzero_and_aliases(void) {
    setup();capture();media.destination[255][511]=1u;
    assert(kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES)==KUI_VMU_CLONE_NOT_ZERO);
    assert(!media.writes && !media.syncs);source_unchanged();no_reuse();
    setup();capture();destination.read_block=read_source;
    assert(kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES)==KUI_VMU_CLONE_ARGUMENT);
    assert(!media.writes);no_reuse();
    setup();capture();
    struct kui_vmu_image_clone moved=work;
    assert(kui_vmu_image_clone_verify_source(&moved)==KUI_VMU_CLONE_ARGUMENT);
    assert(work.captured && !work.failed);
    assert(kui_vmu_image_clone_copy(&work,(const struct kui_vmu_image_io *)(const void *)&work.buffer,
        KUI_VMU_IMAGE_BYTES)==KUI_VMU_CLONE_ARGUMENT);no_reuse();
    setup();assert(kui_vmu_image_clone_capture(&work,(const struct kui_vmu_image_io *)(const void *)&work,
        KUI_VMU_IMAGE_BYTES)==KUI_VMU_CLONE_ARGUMENT);
    assert(!media.source_reads);no_reuse();
}
static void callback_failures(void) {
    setup();capture();unsigned capture_reads=media.source_reads;
    assert(!kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES));
    unsigned copy_source=media.source_reads-capture_reads,copy_reads=media.reads;
    assert(copy_source==512u && copy_reads>512u);
    for(unsigned at=1u;at<=capture_reads;at++) {
        setup();media.fail_source=at;
        assert(kui_vmu_image_clone_capture(&work,&source,KUI_VMU_IMAGE_BYTES)<0);
        assert(!media.writes);source_unchanged();no_reuse();
    }
    /* Exercise every callback boundary of the complete copy, not just the
     * happy path or an implementation-shaped subset. */
    for(unsigned kind=0;kind<4u;kind++) {
        unsigned count=kind==0u?copy_source:kind==1u?copy_reads:kind==2u?256u:2u;
        for(unsigned at=1u;at<=count;at++) {
            setup();capture();
            if(kind==0u) media.fail_source=media.source_reads+at;
            if(kind==1u) media.fail_read=at;
            if(kind==2u) media.fail_write=at;
            if(kind==3u) media.fail_sync=at;
            assert(kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES)<0);
            if(kind==0u || (kind==1u && at<=256u) || (kind==2u && at<256u) ||
               (kind==3u && at==1u)) assert(!media.root_written);
            source_unchanged();no_reuse();
        }
    }
    setup();capture();media.corrupt_destination_at=257u;
    assert(kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES)==KUI_VMU_CLONE_VERIFY);
    assert(media.root_written && media.post_root_synced);source_unchanged();no_reuse();
    setup();capture();media.reenter_at=media.source_reads+1u;
    assert(kui_vmu_image_clone_copy(&work,&destination,KUI_VMU_IMAGE_BYTES)==KUI_VMU_CLONE_STATE);
    assert(!media.writes);no_reuse();
}
int main(void) {
    success();invalid_source();changed_source();nonzero_and_aliases();callback_failures();
    printf("VMU clone: readonly pinned source, root-last exact copy, protected metadata, all callback cuts and fault latch passed (workspace %zu)\n",sizeof(work));
    return 0;
}
