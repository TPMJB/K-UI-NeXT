/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture.h"
#include <string.h>
#include <stdarg.h>
#include <stdio.h>
static uint32_t get32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(uint8_t *p,uint32_t n) { for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i)); }
const char *kui_bench_fad_note(const struct kui_toc sessions[2], uint32_t fad, bool audio) {
    if(!sessions) return NULL;
    for(unsigned area=0;area<2;area++) {
        unsigned count=sessions[area].count>99?99:sessions[area].count;
        for(unsigned i=0;i<count;i++) {
            const struct kui_track *t=&sessions[area].tracks[i];
            if(t->end<=t->start || fad<t->start || fad>=t->end) continue;
            bool data=(t->control&4)!=0;
            if(data==!audio) return NULL;   /* the track is the type the bench asked for */
            return data
                ? "capture_fad is inside a DATA track but capture_type=audio: this measures the "
                  "audio code path (no EDC) on data sectors, not how fast audio tracks read"
                : "capture_fad is inside an AUDIO track but capture_type=data: every sector will "
                  "fail the EDC check";
        }
    }
    return "capture_fad is not inside any track on this disc: the capture section will fail";
}

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
    put32(b+76,(s->crc_only?1u:0u)|((uint32_t)s->format<<1u));
    for(unsigned i=0;i<s->count && i<99;i++) {
        uint8_t *p=b+96+i*40;
        put32(p,s->track[i].sectors);put32(p+4,s->track[i].crc32);memcpy(p+8,s->track[i].sha256,32);
        if(s->format!=KUI_CAPTURE_FORMAT_GDI && s->track[i].sectors && s->track[i].sector_mode==2u)
            b[80u+i/8u]|=(uint8_t)(1u<<(i%8u));
    }
    put32(b+4092,kui_crc32(0,b,4092));
}
bool kui_checkpoint_decode(const uint8_t b[KUI_CHECKPOINT_BYTES],const struct kui_capture_plan *p,
    const uint8_t identity[32],struct kui_checkpoint *out) {
    if(!p || !identity || !out || !p->count || p->count>99 || memcmp(b,"KUICKP1\0",8) ||
       get32(b+8)!=1 || get32(b+12)!=KUI_CHECKPOINT_BYTES || get32(b+56)!=p->count ||
       memcmp(b+24,identity,32) || get32(b+4092)!=kui_crc32(0,b,4092)) return false;
    uint32_t flags=get32(b+76);
    if((flags&~15u) || (flags>>1u)>=KUI_CAPTURE_FORMAT_COUNT) return false;
    for(unsigned i=(flags>>1u)?93u:80u;i<96;i++) if(b[i]) return false;
    if((flags>>1u) && (b[92]&0xf8u)) return false;
    for(unsigned i=96+p->count*40;i<4092;i++) if(b[i]) return false;
    struct kui_checkpoint s={.count=p->count,.retries=get32(b+60)};
    s.sequence=(uint64_t)get32(b+16)|((uint64_t)get32(b+20)<<32);
    if(!s.sequence || s.sequence==UINT64_MAX) return false;
    for(unsigned i=0;i<12;i++) if(!((b[64+i]>='0'&&b[64+i]<='9') ||
                                   (b[64+i]>='a'&&b[64+i]<='f'))) return false;
    memcpy(s.build,b+64,12);memcpy(s.identity,identity,32);s.crc_only=flags&1u;
    s.format=(enum kui_capture_format)(flags>>1u);
    bool incomplete=false;
    for(unsigned i=0;i<s.count;i++) {
        const uint8_t *entry=b+96+i*40;
        uint32_t count=get32(entry),expected=p->tracks[i].end-p->tracks[i].start;
        if(count>expected || (incomplete && count)) return false;
        if(count<expected) incomplete=true;
        if(!count) for(unsigned j=4;j<40;j++) if(entry[j]) return false;
        if(s.crc_only) for(unsigned j=8;j<40;j++) if(entry[j]) return false;   /* no SHA to record */
        bool mode2=(b[80u+i/8u]&(1u<<(i%8u)))!=0;
        if(mode2 && (!count || p->tracks[i].control!=4)) return false;
        s.track[i].sectors=count;s.track[i].crc32=get32(entry+4);memcpy(s.track[i].sha256,entry+8,32);
        s.track[i].sector_mode=s.format!=KUI_CAPTURE_FORMAT_GDI && count && p->tracks[i].control==4?
            (mode2?2u:1u):0u;
    }
    if(s.format!=KUI_CAPTURE_FORMAT_GDI)
        for(unsigned i=s.count;i<99;i++) if(b[80u+i/8u]&(1u<<(i%8u))) return false;
    *out=s;return true;
}
static bool cue_append(char *out,size_t capacity,size_t *used,const char *format,...) {
    if(*used>=capacity) return false;
    va_list args;va_start(args,format);
    int n=vsnprintf(out+*used,capacity-*used,format,args);va_end(args);
    if(n<0 || (size_t)n>=capacity-*used) return false;
    *used+=(size_t)n;return true;
}
bool kui_capture_cue_encode(const struct kui_capture_plan *plan,const uint8_t modes[99],
    char *out,size_t capacity,size_t *bytes) {
    if(!plan || !modes || !out || !bytes || !plan->count || plan->count>99u) return false;
    size_t used=0;bool high=false;
    if(!cue_append(out,capacity,&used,"REM CAPTURE_PROFILE %s\n",KUI_CAPTURE_PROFILE)) return false;
    for(unsigned i=0;i<plan->count;i++) {
        const struct kui_capture_track *t=&plan->tracks[i];
        if(t->number!=i+1u || t->session>1u || (t->control!=0u && t->control!=4u) ||
           t->start<150u || t->end<=t->start || t->toc_end<t->end ||
           (t->control==4u && modes[i]!=1u && modes[i]!=2u)) return false;
        uint32_t gap=0;
        if(!i) {
            if(t->session || t->start!=150u) return false;
            if(!cue_append(out,capacity,&used,"REM SINGLE-DENSITY AREA\n")) return false;
        } else {
            const struct kui_capture_track *previous=&plan->tracks[i-1u];
            if(t->session<previous->session || t->start<previous->toc_end) return false;
            if(t->session==previous->session) {
                if(t->start!=previous->toc_end) return false;
                gap=previous->toc_end-previous->end;
                if(gap!=(t->control!=previous->control?150u:0u)) return false;
            } else {
                if(t->start!=45150u || t->control!=4u || previous->end!=previous->toc_end) return false;
                high=true;
                if(!cue_append(out,capacity,&used,"REM HIGH-DENSITY AREA\n")) return false;
            }
        }
        if(!cue_append(out,capacity,&used,"FILE \"track%02u.%s\" BINARY\n  TRACK %02u ",
            i+1u,t->control==4u?"bin":"raw",i+1u)) return false;
        if(t->control==4u) {
            if(!cue_append(out,capacity,&used,"MODE%u/2352\n",modes[i])) return false;
        } else if(!cue_append(out,capacity,&used,"AUDIO\n")) return false;
        if(gap && !cue_append(out,capacity,&used,"    PREGAP 00:02:00\n")) return false;
        if(!cue_append(out,capacity,&used,"    INDEX 01 00:00:00\n")) return false;
    }
    if(!high || plan->tracks[plan->count-1u].end!=plan->tracks[plan->count-1u].toc_end) return false;
    *bytes=used;return true;
}
