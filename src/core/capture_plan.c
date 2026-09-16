/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture.h"
#include <string.h>
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i)); }
bool kui_plan_tracks(const struct kui_toc sessions[2], struct kui_capture_plan *out) {
    if(!sessions || !out || !sessions[0].count || !sessions[1].count ||
       sessions[0].count>99 || sessions[1].count>99 ||
       sessions[0].count+sessions[1].count>99) return false;
    struct kui_capture_plan p={0};
    if(sessions[0].tracks[0].start!=150 || sessions[1].tracks[0].start!=45150 ||
       sessions[1].tracks[0].control!=4 ||
       sessions[0].tracks[sessions[0].count-1].end>45150) return false;
    for(unsigned area=0;area<2;area++) for(unsigned i=0;i<sessions[area].count;i++) {
        const struct kui_track *t=&sessions[area].tracks[i];
        if(t->number!=p.count+1 || (t->control!=0 && t->control!=4) ||
           t->start<150 || t->end<=t->start || t->end>0xffffff ||
           (i && sessions[area].tracks[i-1].end!=t->start)) return false;
        /* Declared classic GDI convention, not a measurement of INDEX 00:
         * exclude 150 before an intra-session change in data/audio type.
         * Same-type tracks and session leadouts retain the whole TOC range.
         * See docs/capture-format.md for reference and exclusions. */
        uint32_t gap=(i+1<sessions[area].count && t->control!=sessions[area].tracks[i+1].control)?150:0;
        if(t->end-t->start<=gap) return false;
        uint32_t end=t->end-gap;
        uint64_t bytes=(uint64_t)(end-t->start)*KUI_RAW_BYTES;
        if(bytes>UINT32_MAX) return false; /* One raw file also fits FAT32. */
        p.tracks[p.count++]=(struct kui_capture_track){t->number,t->control,area,t->start,end,t->end};
        p.bytes+=bytes;
    }
    *out=p; return true;
}
void kui_checkpoint_encode(const struct kui_checkpoint *s,uint8_t b[KUI_CHECKPOINT_BYTES]) {
    memset(b,0,KUI_CHECKPOINT_BYTES);memcpy(b,"KUICKP1",7);
    put32(b+8,1);put32(b+12,KUI_CHECKPOINT_BYTES);
    put32(b+16,(uint32_t)s->sequence);put32(b+20,(uint32_t)(s->sequence>>32));
    memcpy(b+24,s->identity,32);put32(b+56,s->count);put32(b+60,s->retries);
    memcpy(b+64,s->build,12);
    for(unsigned i=0;i<s->count && i<99;i++) {
        uint8_t *p=b+96+i*40;
        put32(p,s->track[i].sectors);put32(p+4,s->track[i].crc32);memcpy(p+8,s->track[i].sha256,32);
    }
    put32(b+4092,kui_crc32(0,b,4092));
}
bool kui_checkpoint_decode(const uint8_t b[KUI_CHECKPOINT_BYTES],const struct kui_capture_plan *p,
    const uint8_t identity[32],struct kui_checkpoint *out) {
    if(!p || !identity || !out || !p->count || p->count>99 || memcmp(b,"KUICKP1\0",8) ||
       get32(b+8)!=1 || get32(b+12)!=KUI_CHECKPOINT_BYTES || get32(b+56)!=p->count ||
       memcmp(b+24,identity,32) || get32(b+4092)!=kui_crc32(0,b,4092)) return false;
    for(unsigned i=76;i<96;i++) if(b[i]) return false;
    for(unsigned i=96+p->count*40;i<4092;i++) if(b[i]) return false;
    struct kui_checkpoint s={.count=p->count,.retries=get32(b+60)};
    s.sequence=(uint64_t)get32(b+16)|((uint64_t)get32(b+20)<<32);
    if(!s.sequence || s.sequence==UINT64_MAX) return false;
    for(unsigned i=0;i<12;i++) if(!((b[64+i]>='0'&&b[64+i]<='9') ||
                                   (b[64+i]>='a'&&b[64+i]<='f'))) return false;
    memcpy(s.build,b+64,12);memcpy(s.identity,identity,32);
    bool incomplete=false;
    for(unsigned i=0;i<s.count;i++) {
        const uint8_t *entry=b+96+i*40;
        uint32_t count=get32(entry),expected=p->tracks[i].end-p->tracks[i].start;
        if(count>expected || (incomplete && count)) return false;
        if(count<expected) incomplete=true;
        if(!count) for(unsigned j=4;j<40;j++) if(entry[j]) return false;
        s.track[i].sectors=count;s.track[i].crc32=get32(entry+4);memcpy(s.track[i].sha256,entry+8,32);
    }
    *out=s;return true;
}
