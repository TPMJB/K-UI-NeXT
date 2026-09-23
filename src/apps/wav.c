/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/wav.h"
#include <string.h>
static uint16_t le16(const uint8_t *p) {return (uint16_t)(p[0]|((uint16_t)p[1]<<8));}
static uint32_t le32(const uint8_t *p) {return p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
bool kui_wav_parse(const void *file,size_t size,struct kui_wav *out) {
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!file || size<12) return false;
    const uint8_t *p=file;
    if(memcmp(p,"RIFF",4) || memcmp(p+8,"WAVE",4) || (uint64_t)le32(p+4)+8!=size) return false;
    struct kui_wav parsed={0};bool fmt=false,data=false;
    for(size_t at=12;at<size;) {
        if(size-at<8) return false;
        uint32_t bytes=le32(p+at+4);size_t body=at+8;
        if(bytes>size-body) return false;
        if(!memcmp(p+at,"fmt ",4)) {
            if(fmt || bytes<16 || le16(p+body)!=1 || le16(p+body+14)!=16) return false;
            parsed.channels=le16(p+body+2);parsed.rate=le32(p+body+4);
            parsed.frame_bytes=parsed.channels*2u;
            if((parsed.channels!=1 && parsed.channels!=2) || parsed.rate<8000 || parsed.rate>44100 ||
               le16(p+body+12)!=parsed.frame_bytes || le32(p+body+8)!=parsed.rate*parsed.frame_bytes) return false;
            fmt=true;
        } else if(!memcmp(p+at,"data",4)) {
            if(data || !bytes) return false;
            parsed.offset=body;parsed.bytes=bytes;data=true;
        }
        at=body+bytes;
        if(bytes&1u) {if(at==size) return false;++at;}
    }
    if(!fmt || !data || parsed.bytes%parsed.frame_bytes) return false;
    *out=parsed;return true;
}
bool kui_pcm_loop_init(struct kui_pcm_loop *loop,const void *data,size_t bytes,unsigned frame_bytes) {
    if(!loop) return false;
    memset(loop,0,sizeof(*loop));
    if(!data || !bytes || (frame_bytes!=2 && frame_bytes!=4) || bytes%frame_bytes) return false;
    loop->data=data;loop->bytes=bytes;loop->frame_bytes=frame_bytes;return true;
}
size_t kui_pcm_loop_fill(struct kui_pcm_loop *loop,void *out,size_t bytes) {
    if(!loop || !out || !loop->data || !loop->bytes || !loop->frame_bytes ||
       loop->position>=loop->bytes || bytes%loop->frame_bytes) return 0;
    uint8_t *p=out;size_t remaining=bytes;
    while(remaining) {
        size_t count=loop->bytes-loop->position;if(count>remaining) count=remaining;
        memcpy(p,loop->data+loop->position,count);p+=count;remaining-=count;
        loop->position+=count;if(loop->position==loop->bytes) loop->position=0;
    }
    return bytes;
}
unsigned kui_music_aica_volume(unsigned percent) {return (percent>100?100:percent)*255u/100u;}
