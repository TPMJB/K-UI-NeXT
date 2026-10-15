/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture_export.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BLOCKS 16u
static struct {
    uint8_t source[3][BLOCKS*KUI_RAW_BYTES],output[128u*1024u];
    size_t bytes;unsigned writes,reads,blocks;
    bool readonly,fail_write,fail_read,corrupt,stop_write,stop_verify,stop;
    struct kui_capture_plan plan;
    struct kui_checkpoint state;
} test;
static unsigned checks;
#define CHECK(c) do {++checks;assert(c);} while(0)
static uint32_t random_word(uint32_t *s) {
    uint32_t x=*s;x^=x<<13;x^=x>>17;x^=x<<5;return *s=x;
}
static void put32(uint8_t *p,uint32_t value) {
    for(unsigned i=0;i<4u;i++) p[i]=(uint8_t)(value>>(8u*i));
}
static void setup(enum kui_capture_format format) {
    memset(&test,0,sizeof(test));
    test.plan=(struct kui_capture_plan){.tracks={{1,4,0,150,154,304},{2,0,0,304,308,308},
        {3,4,1,45150,45150+BLOCKS,45150+BLOCKS}},.count=3};
    test.state.count=3;test.state.format=format;uint32_t prng=0x74b83921u;
    for(unsigned t=0;t<3u;t++) {
        unsigned sectors=t==2u?BLOCKS:4u;test.state.track[t].sectors=sectors;
        struct kui_sha256 hash;kui_sha256_init(&hash);uint32_t crc=0;
        for(unsigned s=0;s<sectors;s++) {
            uint8_t *raw=test.source[t]+s*KUI_RAW_BYTES;
            for(unsigned j=0;j<KUI_RAW_BYTES;j++) raw[j]=s<sectors/2u?(uint8_t)s:(uint8_t)random_word(&prng);
            if(t!=1u) {
                unsigned mode=s&1u?2u:1u,offset=mode==1u?16u:24u;
                raw[0]=raw[11]=0;memset(raw+1,255,10);raw[12]=raw[13]=raw[14]=0;raw[15]=(uint8_t)mode;
                if(mode==2u) memset(raw+16,0,8);
                put32(raw+offset+KUI_DATA_BYTES,kui_cd_edc(raw+(mode==1u?0u:16u),offset+KUI_DATA_BYTES-(mode==1u?0u:16u)));
                CHECK(kui_sector_edc_valid(raw));
            }
            crc=kui_crc32(crc,raw,KUI_RAW_BYTES);kui_sha256_update(&hash,raw,KUI_RAW_BYTES);
        }
        test.state.track[t].crc32=crc;kui_sha256_digest(&hash,test.state.track[t].sha256);
        test.plan.bytes+=(uint64_t)sectors*KUI_RAW_BYTES;
    }
}
static bool source_read(void *ctx,unsigned track,uint64_t offset,void *out,size_t bytes) {
    (void)ctx;if(track>=3u || offset>test.state.track[track].sectors*KUI_RAW_BYTES ||
        bytes>test.state.track[track].sectors*KUI_RAW_BYTES-offset) return false;
    memcpy(out,test.source[track]+offset,bytes);return true;
}
static bool output_read(void *ctx,uint64_t offset,void *out,size_t bytes) {
    (void)ctx;++test.reads;
    if(test.fail_read || offset>test.bytes || bytes>test.bytes-offset) return false;
    memcpy(out,test.output+offset,bytes);return true;
}
static bool output_write(void *ctx,uint64_t offset,const void *data,size_t bytes) {
    (void)ctx;
    if(test.readonly) return true;
    ++test.writes;if(test.fail_write || offset>sizeof(test.output) || bytes>sizeof(test.output)-offset) return false;
    memcpy(test.output+offset,data,bytes);
    if(offset+bytes>test.bytes) test.bytes=(size_t)offset+bytes;
    return true;
}
static bool sync_output(void *ctx) {
    (void)ctx;
    if(test.corrupt) {test.output[24]^=1u;test.corrupt=false;}
    return true;
}
static bool cancelled(void *ctx) {(void)ctx;return test.stop;}
static void progress(void *ctx,uint64_t done,uint64_t total,bool verifying) {
    (void)ctx;CHECK(done<=total);++test.blocks;
    if((verifying && test.stop_verify) || (!verifying && test.stop_write)) test.stop=true;
}
static struct kui_capture_export_io io(void) {
    return (struct kui_capture_export_io){NULL,source_read,output_read,output_write,sync_output,cancelled,progress};
}
static enum kui_capture_export_result run(struct kui_capture_export_report *report) {
    struct kui_capture_export_io ops=io();
    return kui_capture_export_write(&test.plan,&test.state,test.state.format,&ops,report);
}
static void save(const char *dir,enum kui_capture_format format) {
    char path[1024];int n=snprintf(path,sizeof(path),"%s/test%s",dir,kui_capture_format_extension(format));
    CHECK(n>0 && (size_t)n<sizeof(path));FILE *file=fopen(path,"wb");CHECK(file);
    CHECK(fwrite(test.output,1,test.bytes,file)==test.bytes);CHECK(fclose(file)==0);
    n=snprintf(path,sizeof(path),"%s/expected.iso",dir);CHECK(n>0 && (size_t)n<sizeof(path));
    file=fopen(path,"wb");CHECK(file);
    for(unsigned s=0;s<BLOCKS;s++) {
        const uint8_t *raw=test.source[2]+s*KUI_RAW_BYTES;int offset=kui_data_offset(raw);CHECK(offset>=0);
        CHECK(fwrite(raw+offset,1,KUI_DATA_BYTES,file)==KUI_DATA_BYTES);
    }
    CHECK(fclose(file)==0);
}
int main(int argc,char **argv) {
    struct kui_capture_export_report report;
    for(enum kui_capture_format format=KUI_CAPTURE_FORMAT_CSO;format<=KUI_CAPTURE_FORMAT_ZSO;format++) {
        setup(format);uint64_t bound=0;CHECK(kui_capture_export_bound(&test.plan,format,&bound));
        CHECK(run(&report)==KUI_EXPORT_COMPLETE);CHECK(report.bytes==test.bytes && report.bytes<=bound);
        CHECK(report.logical_bytes==BLOCKS*KUI_DATA_BYTES && report.data_track==3u);
        CHECK(test.reads && test.writes);CHECK(report.crc32==kui_crc32(0,test.output,test.bytes));
        struct kui_sha256 hash;uint8_t digest[32];kui_sha256_init(&hash);kui_sha256_update(&hash,test.output,test.bytes);
        kui_sha256_digest(&hash,digest);CHECK(!memcmp(digest,report.sha256,32));
        if(argc==2) save(argv[1],format);
        /* Strict read-only reconstruction must still decode and verify the
         * existing image, but issue no real output write. */
        unsigned writes=test.writes;test.readonly=true;
        CHECK(run(&report)==KUI_EXPORT_COMPLETE);CHECK(test.writes==writes);
        test.output[0]^=1u;CHECK(run(&report)==KUI_EXPORT_FAILED);CHECK(test.writes==writes);
        setup(format);test.fail_write=true;CHECK(run(&report)==KUI_EXPORT_FAILED);CHECK(!report.bytes);
        setup(format);test.fail_read=true;CHECK(run(&report)==KUI_EXPORT_FAILED);CHECK(!report.bytes);
        setup(format);test.corrupt=true;CHECK(run(&report)==KUI_EXPORT_FAILED);CHECK(!report.bytes);
        setup(format);test.stop_write=true;CHECK(run(&report)==KUI_EXPORT_STOPPED);CHECK(!report.bytes);
        setup(format);test.stop_verify=true;CHECK(run(&report)==KUI_EXPORT_STOPPED);CHECK(!report.bytes);
        setup(format);test.state.track[2].crc32^=1u;CHECK(run(&report)==KUI_EXPORT_FAILED);CHECK(!report.bytes);
        setup(format);test.state.track[2].sha256[0]^=1u;CHECK(run(&report)==KUI_EXPORT_FAILED);CHECK(!report.bytes);
        setup(format);test.state.crc_only=true;memset(test.state.track[2].sha256,0,32);CHECK(run(&report)==KUI_EXPORT_COMPLETE);
        setup(format);--test.state.track[2].sectors;CHECK(run(&report)==KUI_EXPORT_FAILED);CHECK(!test.writes);
        setup(format);test.plan.tracks[1].session=1u;test.plan.tracks[1].control=4u;
        char reason[192];CHECK(!kui_capture_export_preflight(&test.plan,&test.state,format,reason,sizeof(reason)));
        CHECK(reason[0]);CHECK(run(&report)==KUI_EXPORT_FAILED);CHECK(!test.writes);
        /* Layout preflight is usable before NEW has captured any sectors. */
        setup(format);memset(test.state.track,0,sizeof(test.state.track));
        CHECK(kui_capture_export_preflight(&test.plan,&test.state,format,reason,sizeof(reason)));
    }
    printf("capture export: %u checks passed\n",checks);return 0;
}
