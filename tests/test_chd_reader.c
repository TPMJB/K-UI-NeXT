/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/chd.h"
#include "kui/core.h"
#include "kui/hash.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct source {
    uint8_t *data;size_t size;unsigned calls,fail_on,cancel_calls,cancel_on;
    enum kui_chd_result fault;
};
static enum kui_chd_result read_source(void *context,uint64_t at,void *out,size_t n) {
    struct source *s=context;s->calls++;
    assert(at<=s->size && n<=s->size-at);
    if(s->fail_on && s->calls==s->fail_on) {
        memcpy(out,s->data+at,n/2u);return s->fault;
    }
    memcpy(out,s->data+at,n);return KUI_CHD_OK;
}
static int cancelled(void *context) {
    struct source *s=context;s->cancel_calls++;return s->cancel_on && s->cancel_calls>=s->cancel_on;
}
static uint32_t be32(const uint8_t *p) {return (uint32_t)p[0]<<24|(uint32_t)p[1]<<16|(uint32_t)p[2]<<8|p[3];}
static uint64_t be64(const uint8_t *p) {return (uint64_t)be32(p)<<32|be32(p+4);}
static void put32(uint8_t *p,uint32_t v) {p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;}
static void put64(uint8_t *p,uint64_t v) {put32(p,(uint32_t)(v>>32));put32(p+4,(uint32_t)v);}
static void reset(struct source *s) {s->calls=s->fail_on=s->cancel_calls=s->cancel_on=0;s->fault=KUI_CHD_IO;}
static void reject(struct source *s) {
    struct kui_chd_io io={s,read_source,cancelled};struct kui_chd_info before,out;
    memset(&before,0xa5,sizeof(before));out=before;reset(s);
    assert(kui_chd_open(&io,s->size,&out)!=KUI_CHD_OK);assert(!memcmp(&before,&out,sizeof(out)));
}
static void digest(const char *kind,unsigned index,const struct kui_sha256 *sha) {
    uint8_t result[32];kui_sha256_digest(sha,result);printf("%s %u ",kind,index);
    for(unsigned i=0;i<32u;i++) printf("%02x",result[i]);
    putchar('\n');
}
static void guards(const uint8_t *p,size_t bytes) {
    for(unsigned i=0;i<16u;i++) assert(p[i]==0xa5 && p[16u+bytes+i]==0xa5);
}
int main(int argc,char **argv) {
    assert(argc==2);FILE *fp=fopen(argv[1],"rb");assert(fp);
    assert(!fseek(fp,0,SEEK_END));long length=ftell(fp);assert(length>0 && !fseek(fp,0,SEEK_SET));
    struct source s={0};s.size=(size_t)length;s.data=malloc(s.size);assert(s.data && fread(s.data,1,s.size,fp)==s.size);assert(!fclose(fp));
    uint8_t *original=malloc(s.size);assert(original);memcpy(original,s.data,s.size);
    struct kui_chd_io io={&s,read_source,cancelled};struct kui_chd_info info;reset(&s);
    assert(kui_chd_open(&io,s.size,&info)==KUI_CHD_OK && info.track_count==5u);
    const uint32_t starts[]={0,160,45000,45163,45318},counts[]={10,7,13,5,9};
    for(unsigned i=0;i<5u;i++) {
        assert(info.tracks[i].disc_lba==starts[i]);
        assert(info.tracks[i].frames-info.tracks[i].pad==counts[i]);
    }
    size_t work_bytes=kui_chd_workspace_bytes();void *work=malloc(work_bytes);assert(work);
    assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);
    assert(kui_chd_workspace_init(work,work_bytes-1u)==KUI_CHD_INVALID);
    assert(kui_chd_workspace_init((uint8_t *)work+1u,work_bytes-1u)==KUI_CHD_INVALID);
    printf("workspace %zu alignment %zu\n",work_bytes,kui_chd_workspace_alignment());
    enum { CHUNK=13u*KUI_CHD_FRAME_BYTES };
    uint8_t *guard=malloc(CHUNK+32u);assert(guard);uint8_t *out=guard+16u;
    struct kui_sha256 sha;kui_sha256_init(&sha);uint32_t frames=(uint32_t)(info.logical_bytes/KUI_CHD_FRAME_BYTES);
    for(uint32_t first=0;first<frames;) {
        uint32_t count=frames-first;if(count>13u)count=13u;size_t bytes=(size_t)count*KUI_CHD_FRAME_BYTES;
        memset(guard,0xa5,CHUNK+32u);
        assert(kui_chd_read_frames(&info,&io,work,work_bytes,first,count,out,bytes)==KUI_CHD_OK);
        guards(guard,bytes);kui_sha256_update(&sha,out,bytes);first+=count;
    }
    digest("canonical",0,&sha);
    for(unsigned t=0;t<5u;t++) {
        memset(guard,0xa5,CHUNK+32u);size_t bytes=(size_t)counts[t]*2352u;
        assert(kui_chd_read_sectors(&info,&io,work,work_bytes,starts[t],counts[t],out,bytes)==KUI_CHD_OK);
        guards(guard,bytes);kui_sha256_init(&sha);kui_sha256_update(&sha,out,bytes);digest("track",t,&sha);
    }
    /* Repeated and reverse seeks cross padded tracks/hunks. Cache hits perform
     * no physical read but still honour cancellation. */
    assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);reset(&s);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,1,1,out,CHUNK)==KUI_CHD_OK);
    unsigned calls=s.calls;assert(calls);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,1,out,CHUNK)==KUI_CHD_OK && s.calls==calls);
    s.cancel_on=s.cancel_calls+1u;
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,1,out,CHUNK)==KUI_CHD_CANCELLED);
    reset(&s);assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,1,out,CHUNK)==KUI_CHD_OK && s.calls);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,frames-1u,1,out,CHUNK)==KUI_CHD_OK);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,7u,3,out,CHUNK)==KUI_CHD_OK);
    memset(guard,0xa5,CHUNK+32u);
    assert(kui_chd_read_sectors(&info,&io,work,work_bytes,9u,2,out,CHUNK)==KUI_CHD_GAP);
    for(unsigned b=0;b<CHUNK+32u;b++)assert(guard[b]==0xa5);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,frames,1,out,CHUNK)==KUI_CHD_RANGE);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,1,out,KUI_CHD_FRAME_BYTES-1u)==KUI_CHD_RANGE);
    assert(kui_chd_read_sectors(&info,&io,work,work_bytes,UINT32_MAX,2,out,CHUNK)==KUI_CHD_RANGE);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,0,out,CHUNK)==KUI_CHD_INVALID);
    /* Header/version/parent/geometry limits and strict map/metadata profile. */
    static const unsigned positions[]={0,8,12,16,20,24,28,36,44,68};
    for(unsigned i=0;i<sizeof(positions)/sizeof(positions[0]);i++) {
        s.data[positions[i]]^=0x80;reject(&s);memcpy(s.data,original,s.size);
    }
    size_t full=s.size;
    const size_t truncations[]={0,7,107,108,123,124};
    for(unsigned i=0;i<sizeof(truncations)/sizeof(truncations[0]);i++) {s.size=truncations[i];reject(&s);}
    s.size=full-1u;reject(&s);s.size=full;
    unsigned maptypes[]={0,3,4,5,6,0x81};
    for(unsigned i=0;i<sizeof(maptypes)/sizeof(maptypes[0]);i++) {s.data[123]=(uint8_t)maptypes[i];reject(&s);memcpy(s.data,original,s.size);}
    memset(s.data+120,0,3);reject(&s);memcpy(s.data,original,s.size);
    put64(s.data+108,108);reject(&s);memcpy(s.data,original,s.size);
    put64(s.data+108,info.metadata_offset);reject(&s);memcpy(s.data,original,s.size);
    s.data[108u+16u*info.hunk_count]^=1;reject(&s);memcpy(s.data,original,s.size);
    uint64_t meta=info.metadata_offset;
    s.data[meta]='X';reject(&s);memcpy(s.data,original,s.size);
    put64(s.data+meta+8u,meta);reject(&s);memcpy(s.data,original,s.size);
    put64(s.data+meta+8u,UINT64_MAX);reject(&s);memcpy(s.data,original,s.size);
    put32(s.data+meta+4u,0x01ffffffu);reject(&s);memcpy(s.data,original,s.size);
    s.data[meta+16u+be32(s.data+meta+4u)%0x1000000u-1u]='x';reject(&s);memcpy(s.data,original,s.size);
    char *text=(char *)s.data+meta+16u;
    char *where=strstr(text,"SUBTYPE:NONE");assert(where);where[8]='R';reject(&s);memcpy(s.data,original,s.size);
    where=strstr(text,"TRACK:1");assert(where);where[6]='2';reject(&s);memcpy(s.data,original,s.size);
    where=strstr(text,"PREGAP:0");assert(where);where[7]='1';reject(&s);memcpy(s.data,original,s.size);
    where=strstr(text,"FRAMES:");assert(where);where[7]='9';reject(&s);memcpy(s.data,original,s.size);
    /* Callback failures and cancellation preserve unpublished descriptors. */
    struct kui_chd_info before;memset(&before,0xa5,sizeof(before));struct kui_chd_info candidate=before;
    reset(&s);s.fail_on=3;s.fault=KUI_CHD_IO;
    assert(kui_chd_open(&io,s.size,&candidate)==KUI_CHD_IO && !memcmp(&candidate,&before,sizeof(before)));
    reset(&s);s.cancel_on=2;
    assert(kui_chd_open(&io,s.size,&candidate)==KUI_CHD_CANCELLED && !memcmp(&candidate,&before,sizeof(before)));
    reset(&s);assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);s.fail_on=2;
    memset(guard,0xa5,CHUNK+32u);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,1,out,CHUNK)==KUI_CHD_IO);
    for(unsigned b=0;b<CHUNK+32u;b++)assert(guard[b]==0xa5);
    reset(&s);assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,1,out,CHUNK)==KUI_CHD_OK && s.calls);
    /* Raw hunk CRC, zero subchannels, sector mode; output remains untouched
     * until a complete checked hunk exists. */
    uint64_t payload=be64(s.data+108);assert(s.data[123]==2u);
    s.data[payload]^=1;assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);
    memset(guard,0xa5,CHUNK+32u);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,1,out,CHUNK)==KUI_CHD_CRC);
    for(unsigned b=0;b<CHUNK+32u;b++)assert(guard[b]==0xa5);
    memcpy(s.data,original,s.size);s.data[payload+2352u]=1;
    put32(s.data+116,kui_crc32(0,s.data+payload,KUI_CHD_HUNK_BYTES));
    assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,1,out,CHUNK)==KUI_CHD_UNSUPPORTED);
    memcpy(s.data,original,s.size);s.data[payload+15u]=2;
    put32(s.data+116,kui_crc32(0,s.data+payload,KUI_CHD_HUNK_BYTES));
    assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,0,1,out,CHUNK)==KUI_CHD_INVALID);
    memcpy(s.data,original,s.size);
    uint32_t compressed=0;while(compressed<info.hunk_count && s.data[108u+16u*compressed+15u]!=1u)compressed++;
    assert(compressed<info.hunk_count);uint8_t *map=s.data+108u+16u*compressed;payload=be64(map);s.data[payload]^=0xff;
    assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,compressed*8u,1,out,CHUNK)!=KUI_CHD_OK);
    memcpy(s.data,original,s.size);map=s.data+108u+16u*compressed;payload=be64(map);
    uint32_t packed=(uint32_t)map[12]<<8|map[13]|(uint32_t)map[14]<<16;assert(packed>2u);
    uint32_t cut=packed-1u;map[12]=(uint8_t)(cut>>8);map[13]=(uint8_t)cut;map[14]=(uint8_t)(cut>>16);
    assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,compressed*8u,1,out,CHUNK)!=KUI_CHD_OK);
    memcpy(s.data,original,s.size);map=s.data+108u+16u*compressed;cut=packed+1u;
    map[12]=(uint8_t)(cut>>8);map[13]=(uint8_t)cut;map[14]=(uint8_t)(cut>>16);
    assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,compressed*8u,1,out,CHUNK)==KUI_CHD_INVALID);
    memcpy(s.data,original,s.size);map=s.data+108u+16u*compressed;
    map[12]=0;map[13]=2;map[14]=0;s.data[payload]=3;s.data[payload+1u]=0; /* Empty raw DEFLATE stream. */
    assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,compressed*8u,1,out,CHUNK)==KUI_CHD_INVALID);
    memcpy(s.data,original,s.size);reset(&s);s.fail_on=3;
    assert(kui_chd_workspace_init(work,work_bytes)==KUI_CHD_OK);
    assert(kui_chd_read_frames(&info,&io,work,work_bytes,compressed*8u,1,out,CHUNK)==KUI_CHD_IO);
    reset(&s);assert(kui_chd_read_frames(&info,&io,work,work_bytes,compressed*8u,1,out,CHUNK)==KUI_CHD_OK && s.calls);
    puts("CHD reader checks passed");free(guard);free(work);free(original);free(s.data);return 0;
}
