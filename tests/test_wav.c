/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/wav.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(i*8));}
static void wav(uint8_t p[64],unsigned channels) {
    memset(p,0,64);memcpy(p,"RIFF",4);put32(p+4,44);memcpy(p+8,"WAVEfmt ",8);
    put32(p+16,16);put16(p+20,1);put16(p+22,channels);put32(p+24,22050);
    put32(p+28,22050*2*channels);put16(p+32,2*channels);put16(p+34,16);
    memcpy(p+36,"data",4);put32(p+40,8);
    for(unsigned i=0;i<8;i++) p[44+i]=(uint8_t)(i+1);
}
int main(void) {
    uint8_t p[96],original[64];struct kui_wav view;
    for(unsigned channels=1;channels<=2;channels++) {
        wav(p,channels);assert(kui_wav_parse(p,52,&view));
        assert(view.rate==22050 && view.channels==channels && view.frame_bytes==channels*2);
        assert(view.offset==44 && view.bytes==8);
        for(size_t size=0;size<52;size++) assert(!kui_wav_parse(p,size,&view));
    }
    wav(original,1);
    const unsigned corrupt[]={0,4,8,16,20,22,24,28,32,34,40};
    for(unsigned i=0;i<sizeof(corrupt)/sizeof(corrupt[0]);i++) {
        memcpy(p,original,64);p[corrupt[i]]^=0x80;assert(!kui_wav_parse(p,52,&view));
    }
    memcpy(p,original,64);put32(p+40,7);put32(p+4,43);assert(!kui_wav_parse(p,51,&view));
    memcpy(p,original,64);put32(p+40,0);put32(p+4,36);assert(!kui_wav_parse(p,44,&view));
    /* Unknown, odd-sized RIFF chunks must have their padding accounted for. */
    memcpy(p,original,36);memcpy(p+36,"JUNK",4);put32(p+40,1);p[44]=123;p[45]=0;
    memcpy(p+46,original+36,16);put32(p+4,54);assert(kui_wav_parse(p,62,&view));assert(view.offset==54);
    /* Data before fmt is legal; duplicate format or data is ambiguous. */
    memcpy(p,original,12);memcpy(p+12,original+36,16);memcpy(p+28,original+12,24);
    assert(kui_wav_parse(p,52,&view));assert(view.offset==20);
    memcpy(p,original,52);memcpy(p+52,original+36,16);put32(p+4,60);assert(!kui_wav_parse(p,68,&view));
    memcpy(p,original,52);memcpy(p+52,original+12,24);put32(p+4,68);assert(!kui_wav_parse(p,76,&view));
    memcpy(p,original,64);put32(p+4,UINT32_MAX);assert(!kui_wav_parse(p,52,&view));
    assert(!kui_wav_parse(NULL,52,&view));assert(!kui_wav_parse(original,52,NULL));
    uint8_t guarded[40];memset(guarded,0xa5,sizeof(guarded));struct kui_pcm_loop loop;
    assert(kui_pcm_loop_init(&loop,original+44,8,2));
    assert(kui_pcm_loop_fill(&loop,guarded+4,28)==28);
    for(unsigned i=0;i<28;i++) assert(guarded[4+i]==original[44+i%8]);
    for(unsigned i=0;i<4;i++) assert(guarded[i]==0xa5 && guarded[32+i]==0xa5);
    assert(loop.position==4);assert(kui_pcm_loop_fill(&loop,guarded,4)==4);
    assert(!memcmp(guarded,original+48,4) && loop.position==0);
    assert(!kui_pcm_loop_fill(&loop,guarded,3) && loop.position==0);
    assert(!kui_pcm_loop_init(&loop,original,7,4));
    assert(!kui_pcm_loop_init(&loop,original,0,2));
    assert(!kui_pcm_loop_init(&loop,NULL,8,2));
    assert(kui_pcm_loop_init(&loop,original+44,8,4));assert(kui_pcm_loop_fill(&loop,guarded,32)==32);
    assert(kui_music_aica_volume(0)==0 && kui_music_aica_volume(100)==255 && kui_music_aica_volume(101)==255);
    unsigned last=0;for(unsigned i=0;i<=100;i++) {unsigned v=kui_music_aica_volume(i);assert(v>=last && v<=255);last=v;}
    puts("PASS WAV: strict bounds/format, padded chunks, duplicate rejection, mono/stereo loops and volume");return 0;
}
