/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/chd_cache.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct file {uint8_t *data;uint64_t size,written;bool open,synced;};
struct fixture {
    struct kui_chd_cache_plan plan;
    struct file output[KUI_CHD_CACHE_TRACK_MAX];
    unsigned calls,fail_at,cancel_at,aborts,publishes,finishes,creates;
    bool corrupt,committed,progress_seen,verify_seen;
    char descriptor[KUI_CHD_CACHE_GDI_BYTES];
    char previous_cache[24];
    struct kui_chd_cache_report published;
};
static uint8_t source_byte(unsigned track,uint64_t offset) {
    return (uint8_t)((offset*37u+(offset>>7u)*11u+track*53u)^((offset>>12u)*19u));
}
static bool event(struct fixture *f) {return ++f->calls!=f->fail_at;}
static bool cancelled(void *arg) {struct fixture *f=arg;return f->cancel_at && f->calls>=f->cancel_at;}
static bool source(void *arg,unsigned track,uint32_t first,uint32_t count,void *data,size_t bytes) {
    struct fixture *f=arg;
    assert(track<f->plan.count && count && count<=KUI_CHD_CACHE_CHUNK_SECTORS);
    const struct kui_chd_cache_track *t=&f->plan.tracks[track];
    assert(first<t->frames && count<=t->frames-first && bytes==(size_t)count*t->sector_bytes);
    if(!event(f))return false;
    uint8_t *p=data;uint64_t offset=(uint64_t)first*t->sector_bytes;
    for(size_t i=0;i<bytes;i++)p[i]=source_byte(track,offset+i);
    return true;
}
static bool create(void *arg,unsigned track,const char *name,uint64_t bytes) {
    struct fixture *f=arg;char expected[32];
    assert(track<f->plan.count && !f->output[track].data);
    snprintf(expected,sizeof(expected),"track%02u.bin",track+1u);assert(!strcmp(name,expected));
    assert(bytes==(uint64_t)f->plan.tracks[track].frames*f->plan.tracks[track].sector_bytes);
    if(!event(f))return false;
    struct file *out=&f->output[track];out->data=malloc((size_t)bytes+32u);assert(out->data);
    memset(out->data,0xa5,(size_t)bytes+32u);out->size=bytes;out->open=true;f->creates++;
    return true;
}
static bool write_track(void *arg,unsigned track,uint64_t offset,const void *data,size_t bytes) {
    struct fixture *f=arg;struct file *out=&f->output[track];
    assert(out->open && offset==out->written && bytes && offset<=out->size && bytes<=out->size-offset);
    assert(bytes<=KUI_CHD_CACHE_CHUNK_SECTORS*2352u);
    if(!event(f))return false;
    memcpy(out->data+offset,data,bytes);out->written+=bytes;return true;
}
static bool read_output(void *arg,unsigned track,uint64_t offset,void *data,size_t bytes) {
    struct fixture *f=arg;struct file *out=&f->output[track];
    assert(out->open && out->synced && out->written==out->size && bytes);
    assert(offset<=out->size && bytes<=out->size-offset && bytes<=KUI_CHD_CACHE_CHUNK_SECTORS*2352u);
    if(!event(f))return false;
    memcpy(data,out->data+offset,bytes);if(f->corrupt)((uint8_t *)data)[bytes-1u]^=1u;return true;
}
static bool sync_track(void *arg,unsigned track) {
    struct fixture *f=arg;struct file *out=&f->output[track];assert(out->open && out->written==out->size);
    if(!event(f))return false;
    out->synced=true;return true;
}
static bool finish(void *arg,unsigned track,uint64_t expected) {
    struct fixture *f=arg;struct file *out=&f->output[track];assert(out->open && expected==out->size);
    out->open=false;f->finishes++;
    bool ok=event(f);return ok && out->written==expected;
}
static bool publish(void *arg,const char *gdi,size_t bytes,const struct kui_chd_cache_report *report) {
    struct fixture *f=arg;
    assert(bytes<sizeof(f->descriptor) && bytes==strlen(gdi) && report->count==f->plan.count);
    assert(!f->committed && !f->aborts && !cancelled(f));
    for(unsigned i=0;i<f->plan.count;i++)assert(f->output[i].data && !f->output[i].open && f->output[i].synced);
    if(!event(f))return false;
    memcpy(f->descriptor,gdi,bytes+1u);f->published=*report;f->committed=true;f->publishes++;return true;
}
static void abort_export(void *arg) {
    struct fixture *f=arg;assert(!f->committed);f->aborts++;
    for(unsigned i=0;i<f->plan.count;i++) {
        assert(!f->output[i].open);free(f->output[i].data);f->output[i]=(struct file){0};
    }
}
static void progress(void *arg,uint64_t done,uint64_t total,bool verify) {
    struct fixture *f=arg;assert(done<=total && total);f->progress_seen=true;if(verify)f->verify_seen=true;
}
static struct kui_chd_cache_io io(struct fixture *f) {
    return (struct kui_chd_cache_io){.ctx=f,.read_track=source,.create_track=create,
        .write_track=write_track,.read_output=read_output,.sync_track=sync_track,.finish_track=finish,
        .publish=publish,.abort=abort_export,.cancelled=cancelled,.progress=progress};
}
static void init(struct fixture *f) {
    memset(f,0,sizeof(*f));strcpy(f->previous_cache,"accepted old cache");
    f->plan.count=3u;
    f->plan.tracks[0]=(struct kui_chd_cache_track){.disc_lba=0u,.frames=32u,.sector_bytes=2352u,.control=4u};
    f->plan.tracks[1]=(struct kui_chd_cache_track){.disc_lba=182u,.frames=17u,.sector_bytes=2352u,.control=0u};
    f->plan.tracks[2]=(struct kui_chd_cache_track){.disc_lba=45000u,.frames=33u,.sector_bytes=2048u,.control=4u};
}
static void clear(struct fixture *f) {
    assert(!strcmp(f->previous_cache,"accepted old cache"));
    for(unsigned i=0;i<f->plan.count && i<KUI_CHD_CACHE_TRACK_MAX;i++) {
        struct file *out=&f->output[i];assert(!out->open);
        if(out->data)for(unsigned n=0;n<32u;n++)assert(out->data[out->size+n]==0xa5u);
        free(out->data);out->data=NULL;
    }
}
static bool zero(const void *data,size_t bytes) {
    const uint8_t *p=data;for(size_t i=0;i<bytes;i++)if(p[i])return false;return true;
}
static void verify_source(struct fixture *f) {
    for(unsigned i=0;i<f->plan.count;i++) {
        const struct file *out=&f->output[i];
        for(uint64_t n=0;n<out->size;n++)assert(out->data[n]==source_byte(i,n));
    }
}
static void invalid_plans(void) {
    struct fixture f;init(&f);uint64_t bytes=12345u;
    assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_OK && bytes>0u);
    struct kui_chd_cache_plan original=f.plan;
    f.plan.count=0;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_INVALID);
    f.plan.count=100;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_INVALID);
    f.plan=original;f.plan.tracks[0].frames=0;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_INVALID);
    f.plan=original;f.plan.tracks[0].control=1;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_INVALID);
    f.plan=original;f.plan.tracks[0].sector_bytes=2336;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_INVALID);
    f.plan=original;f.plan.tracks[1].sector_bytes=2048;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_INVALID);
    f.plan=original;f.plan.tracks[1].disc_lba=31;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_RANGE);
    f.plan=original;f.plan.tracks[2].disc_lba=UINT32_MAX;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_RANGE);
    f.plan=original;f.plan.tracks[2].frames=UINT32_MAX;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_RANGE);
    f.plan=original;f.plan.tracks[2].disc_lba=719849u;f.plan.tracks[2].frames=1;
    assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_OK);
    f.plan.tracks[2].frames=2;assert(kui_chd_cache_bound(&f.plan,&bytes)==KUI_CHD_CACHE_RANGE);
    assert(kui_chd_cache_bound(NULL,&bytes)==KUI_CHD_CACHE_INVALID);
    assert(kui_chd_cache_bound(&original,NULL)==KUI_CHD_CACHE_INVALID);
}
int main(void) {
    invalid_plans();
    const size_t bytes=kui_chd_cache_workspace_bytes(),alignment=kui_chd_cache_workspace_alignment();
    assert(alignment && !(alignment&(alignment-1u)));
    uint8_t *allocation=malloc(bytes+alignment+64u);assert(allocation);
    memset(allocation,0xa7,bytes+alignment+64u);
    uint8_t *workspace=(uint8_t *)(((uintptr_t)(allocation+32u)+alignment-1u)&~(uintptr_t)(alignment-1u));
    uint8_t source_id[32];for(unsigned i=0;i<32u;i++)source_id[i]=(uint8_t)(i*7u);
    struct kui_chd_cache_report report;
    struct fixture f;init(&f);struct kui_chd_cache_io callbacks=io(&f);
    assert(kui_chd_cache_export(&f.plan,source_id,&callbacks,workspace,bytes,&report)==KUI_CHD_CACHE_OK);
    unsigned calls=f.calls;
    assert(f.committed && f.publishes==1 && !f.aborts && f.progress_seen && f.verify_seen);
    assert(f.creates==3 && f.finishes==3 && !memcmp(report.source_id,source_id,sizeof(source_id)));
    assert(!memcmp(&report,&f.published,sizeof(report)));verify_source(&f);
    const char *expected="3\n1 0 4 2352 track01.bin 0\n2 182 0 2352 track02.bin 0\n3 45000 4 2048 track03.bin 0\n";
    assert(!strcmp(f.descriptor,expected));
    uint64_t bound=0;assert(kui_chd_cache_bound(&f.plan,&bound)==KUI_CHD_CACHE_OK);
    assert(bound==report.payload_bytes+strlen(expected) && report.descriptor_bytes==strlen(expected));
    char hex[65];
    for(unsigned i=0;i<3u;i++){kui_hex(report.track_sha256[i],32u,hex);printf("track %u %s\n",i,hex);}
    kui_hex(report.descriptor_sha256,32u,hex);printf("gdi %s\n",hex);clear(&f);
    for(unsigned fail=1;fail<=calls;fail++) {
        init(&f);f.fail_at=fail;callbacks=io(&f);memset(&report,0x79,sizeof(report));
        assert(kui_chd_cache_export(&f.plan,source_id,&callbacks,workspace,bytes,&report)==KUI_CHD_CACHE_IO);
        assert(!f.committed && !f.publishes && f.aborts==1 && zero(&report,sizeof(report)));clear(&f);
    }
    for(unsigned stop=1;stop<calls;stop++) {
        init(&f);f.cancel_at=stop;callbacks=io(&f);
        assert(kui_chd_cache_export(&f.plan,source_id,&callbacks,workspace,bytes,&report)==KUI_CHD_CACHE_CANCELLED);
        assert(!f.committed && !f.publishes && f.aborts==1 && zero(&report,sizeof(report)));clear(&f);
    }
    init(&f);f.cancel_at=1;f.calls=1;callbacks=io(&f);
    assert(kui_chd_cache_export(&f.plan,source_id,&callbacks,workspace,bytes,&report)==KUI_CHD_CACHE_CANCELLED);
    assert(!f.creates && f.aborts==1);clear(&f);
    init(&f);f.corrupt=true;callbacks=io(&f);
    assert(kui_chd_cache_export(&f.plan,source_id,&callbacks,workspace,bytes,&report)==KUI_CHD_CACHE_VERIFY);
    assert(!f.committed && f.aborts==1 && zero(&report,sizeof(report)));clear(&f);
    init(&f);callbacks=io(&f);
    assert(kui_chd_cache_export(&f.plan,source_id,&callbacks,workspace,bytes-1u,&report)==KUI_CHD_CACHE_INVALID);
    if(alignment>1u)assert(kui_chd_cache_export(&f.plan,source_id,&callbacks,workspace+1u,bytes,&report)==KUI_CHD_CACHE_INVALID);
    callbacks.read_track=NULL;
    assert(kui_chd_cache_export(&f.plan,source_id,&callbacks,workspace,bytes,&report)==KUI_CHD_CACHE_INVALID);
    assert(!f.calls && !f.aborts);clear(&f);
    init(&f);f.plan.count=99u;
    for(unsigned i=0;i<f.plan.count;i++)f.plan.tracks[i]=(struct kui_chd_cache_track){.disc_lba=i*2u,.frames=1u,.sector_bytes=2352u,.control=4u};
    callbacks=io(&f);callbacks.cancelled=NULL;callbacks.progress=NULL;
    memcpy(report.source_id,source_id,sizeof(source_id));
    assert(kui_chd_cache_export(&f.plan,report.source_id,&callbacks,workspace,bytes,&report)==KUI_CHD_CACHE_OK);
    assert(!memcmp(report.source_id,source_id,sizeof(source_id)) && strstr(f.descriptor,"99 196 4 2352 track99.bin 0\n"));
    verify_source(&f);clear(&f);
    for(uint8_t *p=allocation;p<workspace;p++)assert(*p==0xa7u);
    for(uint8_t *p=workspace+bytes;p<allocation+bytes+alignment+64u;p++)assert(*p==0xa7u);
    printf("CHD cache checks passed: %u injected callback failures and %u cancellation boundaries\n",calls,calls-1u);
    free(allocation);return 0;
}
