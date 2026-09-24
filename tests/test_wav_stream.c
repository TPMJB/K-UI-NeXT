/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/wav.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct source {uint8_t bytes[160];uint64_t size;unsigned calls,fail_call;};
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i));}
static void reset(struct source *s,uint32_t pcm,unsigned channels) {
    memset(s,0,sizeof(*s));s->size=(uint64_t)pcm+44;
    memcpy(s->bytes,"RIFF",4);put32(s->bytes+4,pcm+36);memcpy(s->bytes+8,"WAVEfmt ",8);
    put32(s->bytes+16,16);put16(s->bytes+20,1);put16(s->bytes+22,channels);put32(s->bytes+24,44100);
    put32(s->bytes+28,44100*channels*2);put16(s->bytes+32,channels*2);put16(s->bytes+34,16);
    memcpy(s->bytes+36,"data",4);put32(s->bytes+40,pcm);
}
static bool read_at(void *ctx,uint64_t at,void *out,size_t bytes) {
    struct source *s=ctx;++s->calls;
    assert(bytes<=16 && at<=s->size && bytes<=s->size-at);
    if(s->fail_call && s->calls==s->fail_call) return false;
    /* Header-only parser must not read the payload of the virtual 1GiB file. */
    assert(at+bytes<=sizeof(s->bytes));memcpy(out,s->bytes+(size_t)at,bytes);return true;
}
int main(void) {
    struct source s;struct kui_wav out,other;
    for(unsigned channels=1;channels<=2;channels++) {
        reset(&s,1073741824u,channels);assert(kui_wav_read(read_at,&s,s.size,&out));
        assert(out.bytes==1073741824u && out.offset==44 && out.channels==channels && s.calls==4);
        for(unsigned fail=1;fail<=4;fail++) {
            reset(&s,8,channels);s.fail_call=fail;assert(!kui_wav_read(read_at,&s,s.size,&out));
        }
    }
    for(unsigned byte=0;byte<44;byte++) {
        reset(&s,8,2);s.bytes[byte]^=0x80;
        bool memory=kui_wav_parse(s.bytes,52,&other);
        bool stream=kui_wav_read(read_at,&s,52,&out);
        assert(memory==stream);
    }
    reset(&s,8,1);
    memmove(s.bytes+46,s.bytes+36,16);memcpy(s.bytes+36,"JUNK",4);
    put32(s.bytes+40,1);s.bytes[44]=123;s.bytes[45]=0;s.size=62;put32(s.bytes+4,54);
    assert(kui_wav_read(read_at,&s,s.size,&out) && out.offset==54);
    reset(&s,8,1);memcpy(s.bytes+52,s.bytes+36,16);s.size=68;put32(s.bytes+4,60);
    assert(!kui_wav_read(read_at,&s,s.size,&out));
    reset(&s,8,1);put32(s.bytes+40,UINT32_MAX);assert(!kui_wav_read(read_at,&s,s.size,&out));
    assert(!kui_wav_read(NULL,&s,52,&out));assert(!kui_wav_read(read_at,&s,52,NULL));
    assert(!kui_wav_read(read_at,&s,11,&out));
    puts("PASS streaming WAV: 1GiB payload skipped, exact bounded headers, faults and malformed format");return 0;
}
