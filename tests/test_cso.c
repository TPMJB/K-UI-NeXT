/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/zso.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Independently authored CISO v1 fixture: raw sectors and RFC1951 stored
 * streams. No production compressor prepares the bytes being validated. */
struct source {uint8_t bytes[16384];uint32_t size,calls,fail_on;enum kui_zso_result fault;};
static struct source source;
static uint8_t expected[3][2048];
static void put(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(n>>(8u*i));}
static uint32_t aligned(uint32_t n,unsigned shift) {uint32_t mask=(1u<<shift)-1u;return (n+mask)&~mask;}
static enum kui_zso_result read_source(void *ctx,uint32_t offset,void *out,size_t bytes) {
    struct source *s=ctx;assert(offset<=s->size && bytes<=s->size-offset);++s->calls;
    if(s->calls==s->fail_on)return s->fault;
    memcpy(out,s->bytes+offset,bytes);return KUI_ZSO_OK;
}
static uint32_t stored(uint8_t *p,const uint8_t *data,unsigned count,bool final) {
    p[0]=(uint8_t)final;p[1]=(uint8_t)count;p[2]=(uint8_t)(count>>8);
    p[3]=(uint8_t)~count;p[4]=(uint8_t)(~count>>8);memcpy(p+5,data,count);return count+5u;
}
static struct kui_zso_info fixture(unsigned shift) {
    memset(&source,0,sizeof(source));memset(source.bytes,0xad,sizeof(source.bytes));
    memset(source.bytes,0,24);memcpy(source.bytes,"CISO",4);put(source.bytes+4,24);
    put(source.bytes+8,3u*2048u);put(source.bytes+16,2048);source.bytes[20]=1;source.bytes[21]=(uint8_t)shift;
    uint32_t at=aligned(40,shift);
    for(unsigned block=0;block<3;block++) {
        for(unsigned i=0;i<2048;i++)expected[block][i]=(uint8_t)(i*37u+block*31u);
        put(source.bytes+24+4u*block,(at>>shift)|(block?0:UINT32_C(0x80000000)));
        if(!block){memcpy(source.bytes+at,expected[block],2048);at+=2048;}
        else if(block==1)at+=stored(source.bytes+at,expected[block],2048,true);
        else {at+=stored(source.bytes+at,expected[block],1024,false);at+=stored(source.bytes+at,expected[block]+1024,1024,true);}
        at=aligned(at,shift);
    }
    put(source.bytes+36,at>>shift);source.size=at;
    struct kui_zso_info info;
    assert(kui_zso_header_parse_codec(source.bytes,source.size,KUI_ZSO_DEFLATE,&info)==KUI_ZSO_OK);
    return info;
}
static void headers(void) {
    struct kui_zso_info info=fixture(0),unchanged;uint8_t good[24];memcpy(good,source.bytes,24);
    const unsigned offsets[]={0,4,8,8,12,16,20,20,21,22,23};
    const uint32_t values[]={'Z',0,0,2049,1,4096,0,2,10,1,1};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        uint8_t bad[24];memcpy(bad,good,24);
        if(offsets[i]<20)put(bad+offsets[i],values[i]);else bad[offsets[i]]=(uint8_t)values[i];
        memset(&info,0x5a,sizeof(info));unchanged=info;
        assert(kui_zso_header_parse_codec(bad,source.size,KUI_ZSO_DEFLATE,&info)!=KUI_ZSO_OK);
        assert(!memcmp(&info,&unchanged,sizeof(info)));
    }
    assert(kui_zso_header_parse(good,source.size,&info)==KUI_ZSO_UNSUPPORTED);
    assert(kui_zso_header_parse_codec(good,source.size,KUI_ZSO_LZO1X,&info)==KUI_ZSO_UNSUPPORTED);
    assert(kui_zso_header_parse_codec(good,39,KUI_ZSO_DEFLATE,&info)==KUI_ZSO_RANGE);
    assert(kui_zso_header_parse_codec(good,UINT64_C(0x100000000),KUI_ZSO_DEFLATE,&info)==KUI_ZSO_RANGE);
    assert(kui_zso_header_parse_codec(NULL,source.size,KUI_ZSO_DEFLATE,&info)==KUI_ZSO_INVALID);
    assert(kui_zso_header_parse_codec(good,source.size,(enum kui_zso_codec)3,&info)==KUI_ZSO_UNSUPPORTED);
    puts("PASS CSO header: CISO v1/header24/block2048/reserved-zero, codec separation, bounds and unchanged errors");
}
static void blocks(void) {
    const unsigned shifts[]={0,1,4,9};
    for(unsigned variant=0;variant<sizeof(shifts)/sizeof(shifts[0]);variant++) {
        struct kui_zso_info info=fixture(shifts[variant]);
        assert(info.codec==KUI_ZSO_DEFLATE && kui_zso_validate_index(&info,read_source,&source)==KUI_ZSO_OK);
        for(unsigned block=0;block<3;block++) {
            struct {uint8_t before[19],data[2048],after[17];} out;memset(&out,0x6a,sizeof(out));source.calls=0;
            assert(kui_zso_read_block(&info,block,read_source,&source,out.data)==KUI_ZSO_OK);
            assert(!memcmp(out.data,expected[block],2048));unsigned calls=source.calls;
            for(unsigned fail=1;fail<=calls;fail++)for(unsigned cancel=0;cancel<2;cancel++) {
                source.calls=0;source.fail_on=fail;source.fault=cancel?KUI_ZSO_CANCELLED:KUI_ZSO_IO;
                assert(kui_zso_read_block(&info,block,read_source,&source,out.data)==source.fault);
                for(unsigned i=0;i<sizeof(out.before);i++)assert(out.before[i]==0x6a);
                for(unsigned i=0;i<sizeof(out.after);i++)assert(out.after[i]==0x6a);
            }
            source.fail_on=0;
        }
        uint8_t out[2048];source.calls=0;
        assert(kui_zso_read_block(&info,3,read_source,&source,out)==KUI_ZSO_RANGE && !source.calls);
        /* Revalidation rejects changed index locations and excess final tail. */
        put(source.bytes+28,24);assert(kui_zso_validate_index(&info,read_source,&source)!=KUI_ZSO_OK);
        info=fixture(shifts[variant]);source.size+=1u<<info.shift;info.file_bytes=source.size;
        assert(kui_zso_validate_index(&info,read_source,&source)==KUI_ZSO_RANGE);
    }
    puts("PASS CSO blocks: raw/stored/multiblock streams, alignment0..9, guarded output, every IO/cancel boundary and index changes");
}
int main(void) {headers();blocks();return 0;}
