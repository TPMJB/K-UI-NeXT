/* SPDX-License-Identifier: GPL-3.0-only */
/* Independent payload/count oracles for the common filesystem image reader. */
#include "kui/game_image.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned allocations,releases,live,deny_allocation;
static size_t maximum_allocation;
static void *batch_malloc(size_t bytes) {
    ++allocations;if(bytes>maximum_allocation)maximum_allocation=bytes;
    if(deny_allocation)return NULL;
    void *p=malloc(bytes);if(p)++live;return p;
}
static void batch_free(void *p) {if(p) {assert(live);--live;++releases;}free(p);}
#define malloc batch_malloc
#define free batch_free
#include "../src/core/game_image.c"
#undef malloc
#undef free

#define SECTORS 257u
#define FILE_BYTES (SECTORS*2448u+2048u)
static uint8_t files[2][FILE_BYTES],actual[SECTORS*2352u+2u],oracle[SECTORS*2352u+2u];
struct request {unsigned file;uint64_t offset;size_t bytes;uintptr_t destination;};
static struct request requests[SECTORS];
static struct kui_game_image image;
static unsigned reads,fail_at,nested_reads;
static enum kui_game_result failure;
static bool nested,inside;

static uint8_t payload(unsigned track,unsigned sector,unsigned byte) {
    return (uint8_t)(track*89u+sector*37u+byte*13u+(byte>>8));
}
static enum kui_game_result unused_stat(void *context,const char *name,uint64_t *bytes) {
    (void)context;(void)name;(void)bytes;assert(!"Read path must not restat files");return KUI_GAME_IO;
}
static unsigned file_index(const char *name) {
    assert(!strcmp(name,"a.bin") || !strcmp(name,"b.bin"));return name[0]=='b';
}
static enum kui_game_result read_file(void *context,const char *name,uint64_t offset,void *out,size_t bytes) {
    assert(context==&image && out && reads<SECTORS);unsigned file=file_index(name);
    assert(offset<=FILE_BYTES && bytes<=FILE_BYTES-offset);
    bool bounded=false;
    for(unsigned i=0;i<image.count;i++) {
        const struct kui_game_image_track *t=&image.tracks[i];
        uint64_t end=t->file_offset+(uint64_t)(t->end_lba-t->start_lba)*t->sector_bytes;
        if(file_index(t->name)==file && offset>=t->file_offset && offset+bytes<=end)bounded=true;
    }
    assert(bounded);requests[reads++]=(struct request){file,offset,bytes,(uintptr_t)out};
    if(fail_at && reads==fail_at) {memset(out,0xddu,bytes);return failure;}
    memcpy(out,files[file]+offset,bytes);
    if(nested && !inside) {
        inside=true;uint8_t inner[4096];
        assert(kui_game_image_read(&image,image.tracks[0].start_lba,2u,KUI_GAME_SECTOR_MODE1,inner,sizeof(inner))==KUI_GAME_OK);
        for(unsigned i=0;i<4096u;i++)assert(inner[i]==payload(0u,i/2048u,i%2048u));
        ++nested_reads;inside=false;
    }
    return KUI_GAME_OK;
}
static void track(unsigned index,unsigned file,uint32_t first,uint32_t count,unsigned stride,
    unsigned mode,uint64_t offset) {
    struct kui_game_image_track *t=&image.tracks[index];
    *t=(struct kui_game_image_track){.start_lba=first,.end_lba=first+count,.number=index+1u,
        .control=mode?4u:0u,.sector_bytes=stride,.sector_mode=mode,
        .data_offset=stride==2048u?0u:mode==1u?16u:stride==2336u?8u:mode==2u?24u:0u,
        .file_bytes=FILE_BYTES,.file_offset=offset};
    strcpy(t->name,file?"b.bin":"a.bin");
    assert(offset+(uint64_t)count*stride<=FILE_BYTES);
    for(unsigned n=0;n<count;n++) {
        uint8_t *raw=files[file]+offset+(size_t)n*stride;
        memset(raw,0xe3u,stride);
        if(mode && stride!=2048u && stride!=2336u) {
            raw[0]=raw[11]=0u;memset(raw+1u,255u,10u);raw[15]=(uint8_t)mode;
        }
        if(mode==2u) {
            uint8_t *sub=raw+(stride==2336u?0u:16u);
            const uint8_t xa[4]={1u,7u,8u,3u};memcpy(sub,xa,4u);memcpy(sub+4u,xa,4u);
        }
        unsigned user=mode?2048u:2352u;
        for(unsigned b=0;b<user;b++)raw[t->data_offset+b]=payload(index,n,b);
    }
}
static void fresh(unsigned stride,unsigned mode,unsigned count,unsigned offset) {
    assert(!live);allocations=releases=deny_allocation=0u;maximum_allocation=0u;
    reads=fail_at=nested_reads=0u;failure=KUI_GAME_IO;nested=inside=false;
    memset(&image,0,sizeof(image));memset(files,0xa9u,sizeof(files));
    image.files=(struct kui_game_file_ops){&image,unused_stat,read_file};
    image.count=1u;image.format=KUI_GAME_IMAGE_GDI;image.data_lba=45000u;
    track(0u,0u,45000u,count,stride,mode,offset);
}
static void reset_reads(void) {reads=0u;memset(requests,0,sizeof(requests));}
static void expected(uint32_t first,uint32_t count,enum kui_game_sector_format format) {
    uint8_t *p=oracle;
    for(uint32_t n=0;n<count;n++) {
        unsigned t=0u;while(image.tracks[t].end_lba<=first+n)++t;
        const struct kui_game_image_track *track=&image.tracks[t];
        unsigned length=format==KUI_GAME_SECTOR_RAW?2352u:2048u;
        uint64_t at=track->file_offset+(uint64_t)(first+n-track->start_lba)*track->sector_bytes+
            (format==KUI_GAME_SECTOR_RAW?0u:track->data_offset);
        memcpy(p,files[file_index(track->name)]+at,length);p+=length;
    }
}
static void verify(uint32_t first,uint32_t count,enum kui_game_sector_format format,unsigned expected_reads) {
    unsigned width=format==KUI_GAME_SECTOR_RAW?2352u:2048u;
    expected(first,count,format);memset(actual,0x77u,sizeof(actual));reset_reads();
    assert(kui_game_image_read(&image,first,count,format,actual+1u,(size_t)count*width)==KUI_GAME_OK);
    assert(reads==expected_reads && !memcmp(actual+1u,oracle,(size_t)count*width));
    assert(actual[0]==0x77u && actual[1u+(size_t)count*width]==0x77u && !live);
}
static void verify_alignment(void) {
    for(unsigned i=0u;i<reads;i++)
        assert((requests[i].destination&31u)==(requests[i].offset&31u));
}
static void formats_and_counts(void) {
    static const struct {unsigned stride,mode;enum kui_game_sector_format format;bool buffered;} cases[]={
        {2352,1,KUI_GAME_SECTOR_MODE1,true},{2352,2,KUI_GAME_SECTOR_MODE1,true},
        {2336,2,KUI_GAME_SECTOR_MODE1,true},{2448,1,KUI_GAME_SECTOR_MODE1,true},
        {2448,2,KUI_GAME_SECTOR_MODE1,true},{2448,0,KUI_GAME_SECTOR_RAW,true},
        {2448,1,KUI_GAME_SECTOR_RAW,true},{2352,1,KUI_GAME_SECTOR_RAW,false},
        {2352,0,KUI_GAME_SECTOR_RAW,false},{2048,1,KUI_GAME_SECTOR_MODE1,false}};
    static const unsigned counts[]={1u,2u,15u,16u,17u,33u,257u};
    for(unsigned kind=0;kind<sizeof(cases)/sizeof(cases[0]);kind++)
    for(unsigned n=0;n<sizeof(counts)/sizeof(counts[0]);n++)
    for(unsigned format=KUI_GAME_IMAGE_GDI;format<=KUI_GAME_IMAGE_RAW;format++) {
        fresh(cases[kind].stride,cases[kind].mode,counts[n],497u);image.format=(enum kui_game_image_format)format;
        bool batch=cases[kind].buffered && counts[n]>1u;
        unsigned calls=cases[kind].buffered?(counts[n]+15u)/16u:1u;
        verify(45000u,counts[n],cases[kind].format,calls);
        assert(allocations==(unsigned)batch && releases==allocations && maximum_allocation<=16u*2448u+31u);
        if(cases[kind].buffered)verify_alignment();
        if(batch)for(unsigned i=0;i<reads;i++) {
            assert(requests[i].bytes && requests[i].bytes<=16u*cases[kind].stride);
            assert(requests[i].bytes%cases[kind].stride==0u);
        }
    }
    puts("Batch matrix:10physical-layout/output cases x7request sizes x5container labels;257raw sectors use17 bounded reads");
}
static void conversion_alignment(void) {
    static const unsigned strides[]={2336u,2352u,2448u};
    for(unsigned kind=0u;kind<sizeof(strides)/sizeof(strides[0]);kind++)
    for(unsigned offset=0u;offset<512u;offset++)
    for(unsigned denied=0u;denied<2u;denied++) {
        unsigned mode=strides[kind]==2336u?2u:1u;
        fresh(strides[kind],mode,35u,offset);deny_allocation=denied;
        verify(45001u,33u,KUI_GAME_SECTOR_MODE1,denied?33u:3u);
        verify_alignment();
        fresh(strides[kind],mode,1u,offset);
        verify(45000u,1u,KUI_GAME_SECTOR_MODE1,1u);verify_alignment();
    }
    puts("Conversion alignment: all512filesystem offsets,2336/2352/2448strides, oddsector start, batching and single-sector/OOMfallback");
}
static void boundaries_and_fallback(void) {
    fresh(2352u,1u,65u,497u);verify(45003u,17u,KUI_GAME_SECTOR_MODE1,2u);
    assert(requests[0].offset==497u+3u*2352u && requests[0].bytes==16u*2352u);
    assert(requests[1].offset==497u+19u*2352u && requests[1].bytes==2352u);
    for(unsigned shared=0;shared<2u;shared++) {
        fresh(2352u,1u,17u,511u);image.count=2u;
        track(1u,shared?0u:1u,45017u,33u,2448u,2u,shared?511u+17u*2352u+113u:123u);
        verify(45000u,50u,KUI_GAME_SECTOR_MODE1,5u);
        for(unsigned i=0;i<reads;i++)assert(requests[i].bytes<=16u*2448u);
    }
    fresh(2352u,1u,33u,491u);deny_allocation=1u;
    verify(45000u,33u,KUI_GAME_SECTOR_MODE1,33u);
    assert(allocations==1u && !releases && !live);
    /* Reentrant callbacks must not borrow a global batch buffer. */
    fresh(2352u,1u,33u,1u);nested=true;
    expected(45000u,33u,KUI_GAME_SECTOR_MODE1);memset(actual,0x77u,sizeof(actual));
    assert(kui_game_image_read(&image,45000u,33u,KUI_GAME_SECTOR_MODE1,actual,33u*2048u)==KUI_GAME_OK);
    assert(!memcmp(actual,oracle,33u*2048u) && nested_reads==3u && reads==6u && !live);
    assert(allocations==4u && releases==4u);
}
static void failure_and_preflight(void) {
    for(unsigned kind=0;kind<2u;kind++)for(unsigned at=1u;at<=3u;at++) {
        fresh(2352u,1u,33u,255u);fail_at=at;failure=kind?KUI_GAME_CANCELLED:KUI_GAME_IO;
        memset(actual,0x77u,sizeof(actual));
        assert(kui_game_image_read(&image,45000u,33u,KUI_GAME_SECTOR_MODE1,actual,33u*2048u)==failure);
        assert(reads==at && !live && allocations==releases);
        for(size_t i=(size_t)(at-1u)*16u*2048u;i<33u*2048u;i++)assert(actual[i]==0x77u);
        fail_at=0u;verify(45000u,33u,KUI_GAME_SECTOR_MODE1,3u);
    }
    for(unsigned mode=1u;mode<=2u;mode++)for(unsigned bad=0u;bad<3u;bad++) {
        fresh(mode==1u?2352u:2336u,mode,33u,256u);
        struct kui_game_image_track *t=&image.tracks[0];
        uint8_t *p=files[0]+t->file_offset+17u*t->sector_bytes;
        if(mode==1u)p[bad==0u?5u:bad==1u?11u:15u]^=1u;
        else if(bad==0u)p[4]^=1u;
        else p[2]=p[6]=0x20u;
        memset(actual,0x77u,sizeof(actual));
        assert(kui_game_image_read(&image,45000u,33u,KUI_GAME_SECTOR_MODE1,actual,33u*2048u)==KUI_GAME_MODE);
        assert(reads==2u && !live && allocations==releases);
        for(size_t i=18u*2048u;i<33u*2048u;i++)assert(actual[i]==0x77u);
    }
    fresh(2352u,1u,33u,0u);memset(actual,0x77u,sizeof(actual));
    assert(kui_game_image_read(&image,45000u,34u,KUI_GAME_SECTOR_MODE1,actual,sizeof(actual))==KUI_GAME_RANGE);
    assert(kui_game_image_read(&image,45000u,33u,KUI_GAME_SECTOR_MODE1,actual,33u*2048u-1u)==KUI_GAME_INVALID);
    image.count=2u;track(1u,1u,45035u,1u,2352u,1u,0u);
    assert(kui_game_image_read(&image,45032u,4u,KUI_GAME_SECTOR_MODE1,actual,sizeof(actual))==KUI_GAME_GAP);
    image.tracks[1].start_lba=45033u;image.tracks[1].end_lba=45034u;image.tracks[1].control=0u;
    image.tracks[1].sector_mode=0u;image.tracks[1].data_offset=0u;
    assert(kui_game_image_read(&image,45032u,2u,KUI_GAME_SECTOR_MODE1,actual,sizeof(actual))==KUI_GAME_AUDIO);
    assert(!reads && !allocations && !live);
    for(size_t i=0;i<sizeof(actual);i++)assert(actual[i]==0x77u);
}
int main(void) {
    formats_and_counts();conversion_alignment();boundaries_and_fallback();failure_and_preflight();
    puts("PASS common image batch: byte-identical layouts,16sector bound, track/offset boundaries, OOMfallback, reentry, cancel/poisonedI/O andeveryheader validation");
    return 0;
}
