/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/zso.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* Independently authored container fixtures follow maxcso README_ZSO.md and
 * the LZ4 block-format specification. No production encoder/decoder creates
 * their expected bytes. Both random raw data and expanded literal-only LZ4
 * are included; compressed data need not be smaller than an output block. */
#define CAP 16384u
struct source {
    uint8_t bytes[CAP];uint32_t size,calls,fail_on;
    size_t max_read;enum kui_zso_result fault;
};
static struct source source;
static uint8_t expected[3][KUI_ZSO_BLOCK_BYTES];
static void put32(uint8_t *out,uint32_t value) {for(unsigned i=0;i<4;i++) out[i]=(uint8_t)(value>>(8*i));}
static uint32_t get32(const uint8_t *in) {
    return (uint32_t)in[0]|(uint32_t)in[1]<<8|(uint32_t)in[2]<<16|(uint32_t)in[3]<<24;
}
static uint32_t aligned(uint32_t value,unsigned shift) {
    uint32_t mask=(1u<<shift)-1u;return (value+mask)&~mask;
}
static void header(uint8_t *out,uint32_t total,unsigned shift) {
    memset(out,0,24);memcpy(out,"ZISO",4);put32(out+4,24);put32(out+8,total);
    put32(out+16,2048);out[20]=1;out[21]=(uint8_t)shift;
}
static enum kui_zso_result read_source(void *context,uint32_t offset,void *out,size_t size) {
    struct source *s=context;++s->calls;if(size>s->max_read) s->max_read=size;
    assert(offset<=s->size && size<=s->size-offset); /* Codec must preflight every physical request. */
    if(s->fail_on && s->calls==s->fail_on) {
        memcpy(out,s->bytes+offset,size/2u);return s->fault;
    }
    memcpy(out,s->bytes+offset,size);return KUI_ZSO_OK;
}
static size_t extension(uint8_t *out,unsigned value) {
    size_t used=0;while(value>=255u) {out[used++]=255;value-=255u;}out[used++]=(uint8_t)value;return used;
}
static size_t repeat_block(uint8_t *out,unsigned final_literals) {
    /* One literal, a distance-one overlapping match, and a final literal
     * sequence. The final5 variant is a canonical 2048-byte all-'A' block. */
    size_t at=0;out[at++]=0x1f;out[at++]='A';out[at++]=1;out[at++]=0;
    at+=extension(out+at,2048u-1u-final_literals-19u);
    if(final_literals) {out[at++]=(uint8_t)(final_literals<<4);memset(out+at,'A',final_literals);at+=final_literals;}
    return at;
}
static struct kui_zso_info fixture(unsigned shift) {
    memset(&source,0,sizeof(source));memset(source.bytes,0xd3,sizeof(source.bytes));
    header(source.bytes,3u*2048u,shift);uint32_t at=aligned(24u+4u*4u,shift);
    for(unsigned block=0;block<3;block++) {
        put32(source.bytes+24u+block*4u,(at>>shift)|(block==0?UINT32_C(0x80000000):0));
        size_t bytes;
        if(block==0) {
            for(unsigned i=0;i<2048;i++) expected[0][i]=(uint8_t)(i*37u+(i>>8)*13u);
            memcpy(source.bytes+at,expected[0],2048);bytes=2048;
        } else if(block==1) {
            memset(expected[1],'A',2048);bytes=repeat_block(source.bytes+at,5);
        } else {
            for(unsigned i=0;i<2048;i++) expected[2][i]=(uint8_t)(i^0xa5u);
            source.bytes[at]=0xf0;bytes=1+extension(source.bytes+at+1,2048u-15u);
            memcpy(source.bytes+at+bytes,expected[2],2048);bytes+=2048;
        }
        at=aligned(at+(uint32_t)bytes,shift);
    }
    put32(source.bytes+36,at>>shift);source.size=at;
    struct kui_zso_info info;
    assert(kui_zso_header_parse(source.bytes,source.size,&info)==KUI_ZSO_OK);
    return info;
}
static struct kui_zso_info single(const uint8_t *payload,size_t size,unsigned shift,unsigned extra_units) {
    memset(&source,0,sizeof(source));memset(source.bytes,0xb7,sizeof(source.bytes));
    header(source.bytes,2048,shift);uint32_t start=aligned(32,shift);
    uint32_t end=aligned(start+(uint32_t)size,shift)+(extra_units<<shift);
    assert(end<=CAP);put32(source.bytes+24,start>>shift);put32(source.bytes+28,end>>shift);
    memcpy(source.bytes+start,payload,size);source.size=end;
    struct kui_zso_info info;assert(kui_zso_header_parse(source.bytes,source.size,&info)==KUI_ZSO_OK);return info;
}
static void header_reject(const uint8_t *bytes,uint64_t size) {
    struct kui_zso_info info,before;memset(&info,0xa5,sizeof(info));memcpy(&before,&info,sizeof(info));
    assert(kui_zso_header_parse(bytes,size,&info)!=KUI_ZSO_OK);assert(!memcmp(&info,&before,sizeof(info)));
}
static void headers(void) {
    struct kui_zso_info info=fixture(0);uint8_t original[24],damaged[24];memcpy(original,source.bytes,24);
    assert(info.total_bytes==6144 && info.block_count==3 && info.index_bytes==16 && !info.shift);
    const unsigned offsets[]={0,4,8,8,12,16,20,20,21,22,23};
    const uint32_t values[]={'C',0,0,2049,1,4096,0,2,10,1,1};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        memcpy(damaged,original,24);
        if(offsets[i]<20) put32(damaged+offsets[i],values[i]);else damaged[offsets[i]]=(uint8_t)values[i];
        header_reject(damaged,source.size);
    }
    header_reject(original,39);header_reject(original,UINT64_C(0x100000000));
    header_reject(NULL,source.size);assert(kui_zso_header_parse(original,source.size,NULL)!=KUI_ZSO_OK);
    memcpy(damaged,original,24);put32(damaged+8,KUI_ZSO_MAX_BYTES);
    assert(kui_zso_header_parse(damaged,UINT32_MAX,&info)==KUI_ZSO_OK);
    assert(info.block_count==1048576u && info.index_bytes==4194308u);
    puts("PASS ZSO headers: strict standard-LZ4 geometry, endian/size bounds and unchanged failure output");
}
static void mixed_reads_and_faults(void) {
    const unsigned shifts[]={0,1,4,9};
    for(unsigned sh=0;sh<sizeof(shifts)/sizeof(shifts[0]);sh++) {
        struct kui_zso_info info=fixture(shifts[sh]);
        assert(kui_zso_validate_index(&info,read_source,&source)==KUI_ZSO_OK);
        /* ZSO v1's terminal flag is ignored, matching its offset-only role. */
        uint32_t terminal=get32(source.bytes+36);put32(source.bytes+36,terminal|UINT32_C(0x80000000));
        assert(kui_zso_validate_index(&info,read_source,&source)==KUI_ZSO_OK);
        for(unsigned block=0;block<3;block++) {
            struct {uint8_t before[17],data[2048],after[19];} out;
            memset(&out,0x6d,sizeof(out));source.calls=0;
            assert(kui_zso_read_block(&info,block,read_source,&source,out.data)==KUI_ZSO_OK);
            assert(!memcmp(out.data,expected[block],2048));unsigned calls=source.calls;
            for(unsigned i=0;i<17;i++) assert(out.before[i]==0x6d);
            for(unsigned i=0;i<19;i++) assert(out.after[i]==0x6d);
            for(unsigned error=0;error<2;error++) for(unsigned request=1;request<=calls;request++) {
                source.calls=0;source.fail_on=request;source.fault=error?KUI_ZSO_CANCELLED:KUI_ZSO_IO;
                assert(kui_zso_read_block(&info,block,read_source,&source,out.data)==source.fault);
                for(unsigned i=0;i<17;i++) assert(out.before[i]==0x6d);
                for(unsigned i=0;i<19;i++) assert(out.after[i]==0x6d);
            }
            source.fail_on=0;
        }
        unsigned calls=source.calls;source.calls=0;
        assert(kui_zso_read_block(&info,3,read_source,&source,expected[0])==KUI_ZSO_RANGE && !source.calls);
        assert(calls && source.max_read<=2048);
    }
    puts("PASS ZSO reads: independent raw/overlap/literal LZ4 fixtures, nonzero alignment padding and every callback IO/cancel boundary");
}
struct large_index {uint32_t blocks,start,size,calls,fail_on;size_t max_read;bool corrupt;enum kui_zso_result fault;};
static enum kui_zso_result virtual_index(void *context,uint32_t offset,void *out,size_t size) {
    struct large_index *s=context;++s->calls;if(size>s->max_read) s->max_read=size;
    assert(offset>=24 && size%4u==0 && offset+size<=s->start);
    if(s->fail_on && s->calls==s->fail_on)return s->fault;
    uint8_t *bytes=out;
    for(size_t i=0;i<size;i+=4) {
        uint32_t entry=(offset-24u+(uint32_t)i)/4u;
        uint32_t position=s->start+entry*2048u;
        if(s->corrupt && entry==s->blocks) position-=2048u;
        put32(bytes+i,position|(entry<s->blocks?UINT32_C(0x80000000):0));
    }
    return KUI_ZSO_OK;
}
static void indices_and_changes(void) {
    struct kui_zso_info info=fixture(0);
    uint8_t pristine[CAP];memcpy(pristine,source.bytes,sizeof(pristine));uint32_t file_bytes=source.size;
    const unsigned offsets[]={24,28,28,28,32,36};
    const uint32_t values[]={28,40,39,2087,UINT32_C(0x7fffffff),UINT32_C(0x7fffffff)};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++) {
        memcpy(source.bytes,pristine,sizeof(pristine));put32(source.bytes+offsets[i],values[i]);source.calls=0;
        assert(kui_zso_validate_index(&info,read_source,&source)!=KUI_ZSO_OK);
    }
    memcpy(source.bytes,pristine,sizeof(pristine));source.size=++info.file_bytes;
    assert(kui_zso_validate_index(&info,read_source,&source)!=KUI_ZSO_OK); /* tail1 is not legal at shift0 */
    info.file_bytes=file_bytes;source.size=file_bytes;
    assert(kui_zso_validate_index(&info,read_source,&source)==KUI_ZSO_OK);
    put32(source.bytes+28,40);uint8_t out[2048];
    assert(kui_zso_read_block(&info,0,read_source,&source,out)!=KUI_ZSO_OK); /* changed index after admission */
    info=fixture(9);put32(source.bytes+24,UINT32_C(0x7fffffff));
    assert(kui_zso_validate_index(&info,read_source,&source)==KUI_ZSO_RANGE); /* shift in uint64, no wrap */
    info=fixture(9);source.size+=511;info.file_bytes+=511;
    assert(kui_zso_validate_index(&info,read_source,&source)==KUI_ZSO_OK);
    source.size++;info.file_bytes++;
    assert(kui_zso_validate_index(&info,read_source,&source)!=KUI_ZSO_OK);
    struct large_index large={.blocks=1100,.start=24+1101*4,.size=24+1101*4+1100*2048};
    uint8_t raw_header[24];header(raw_header,large.blocks*2048u,0);
    assert(kui_zso_header_parse(raw_header,large.size,&info)==KUI_ZSO_OK);
    assert(kui_zso_validate_index(&info,virtual_index,&large)==KUI_ZSO_OK);
    assert(large.calls>1 && large.max_read<=128);
    for(unsigned error=0;error<2;error++) {
        large.calls=0;large.fail_on=3;large.fault=error?KUI_ZSO_CANCELLED:KUI_ZSO_IO;
        assert(kui_zso_validate_index(&info,virtual_index,&large)==large.fault && large.calls==3);
    }
    large.fail_on=0;
    large.corrupt=true;large.calls=0;
    assert(kui_zso_validate_index(&info,virtual_index,&large)!=KUI_ZSO_OK && large.calls>1);
    puts("PASS ZSO index: full bounded-page validation, strict extents/tails, shifted overflow and rechecked selected-pair changes");
}
static void bad_payload(const uint8_t *payload,size_t bytes,unsigned shift,unsigned extra_units) {
    struct kui_zso_info info=single(payload,bytes,shift,extra_units);
    struct {uint8_t before[11],data[2048],after[13];} out;memset(&out,0x93,sizeof(out));
    assert(kui_zso_read_block(&info,0,read_source,&source,out.data)!=KUI_ZSO_OK);
    for(unsigned i=0;i<11;i++) assert(out.before[i]==0x93);
    for(unsigned i=0;i<13;i++) assert(out.after[i]==0x93);
}
static void lz4_rejections(void) {
    uint8_t encoded[4096];size_t bytes=repeat_block(encoded,5);
    for(size_t truncated=0;truncated<bytes;truncated++) bad_payload(encoded,truncated,0,0);
    bad_payload(encoded,bytes,0,1);bad_payload(encoded,bytes,9,1); /* excessive container padding */
    bytes=repeat_block(encoded,4);bad_payload(encoded,bytes,0,0); /* less than5 final literals */
    bytes=repeat_block(encoded,0);bad_payload(encoded,bytes,0,0); /* ends in a match */
    static const uint8_t offset_zero[]={0x10,'A',0,0};bad_payload(offset_zero,sizeof(offset_zero),0,0);
    static const uint8_t offset_before_start[]={0x10,'A',2,0};bad_payload(offset_before_start,sizeof(offset_before_start),0,0);
    static const uint8_t short_offset[]={0x10,'A',1};bad_payload(short_offset,sizeof(short_offset),0,0);
    static const uint8_t short_literals[]={0xf0,0};bad_payload(short_literals,sizeof(short_literals),0,0);
    memset(encoded,255,16);encoded[0]=0xf0;bad_payload(encoded,16,0,0); /* literal length overflow */
    encoded[0]=0x1f;encoded[1]='A';encoded[2]=1;encoded[3]=0;memset(encoded+4,255,16);
    bad_payload(encoded,20,0,0); /* match length overflow */
    encoded[0]=0xf2;bytes=1+extension(encoded+1,2037u-15u);memset(encoded+bytes,'A',2037);bytes+=2037;
    encoded[bytes++]=1;encoded[bytes++]=0;encoded[bytes++]=0x50;memset(encoded+bytes,'A',5);bytes+=5;
    bad_payload(encoded,bytes,0,0); /* final match starts only11 bytes before the output end */
    /* Fixed independent minilzo 2.10 LZO1X-1 output for2048 'L' bytes, the
     * historical alternate ZSO dialect. It must not trigger codec fallback. */
    static const uint8_t lzo[]={0x02,0x4c,0x4c,0x4c,0x4c,0x4c,0x20,0,0,0,0,0,0,0,0xd2,0x10,0,0x0c,
        0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x4c,0x11,0,0};
    bad_payload(lzo,sizeof(lzo),0,0);
    puts("PASS ZSO LZ4: every truncation, illegal offsets/runs/end conditions/padding, bounded output and no LZO fallback");
}
int main(void) {headers();mixed_reads_and_faults();indices_and_changes();lz4_rejections();return 0;}
