/* SPDX-License-Identifier: GPL-3.0-only */
/* Decode each shipped menu Ogg through the console wrapper and compare it
 * with its reproducible PCM16 WAV: same format and exact frame count, a close
 * waveform match, an exact repeat after the loop rewinds, and an identical
 * start after reopening in the arena. tests/test_music_assets.py drives it. */
#include "kui/music_ogg.h"
#include "kui/wav.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Production callbacks: a mono start fills two 32 KiB halves at a time. */
#define CHUNK 32768u
/* Measured 26.9-29.4 dB at q5; an unrelated or misaligned song is near 0 dB. */
#define MIN_SNR_DB 20.0
static _Alignas(16) unsigned char arena[KUI_OGG_WORKSPACE_BYTES];

static unsigned char *load(const char *path,size_t *bytes) {
    FILE *file=fopen(path,"rb");unsigned char *data=NULL;long size=-1;
    if(file && !fseek(file,0,SEEK_END) && (size=ftell(file))>0 && !fseek(file,0,SEEK_SET) &&
       (data=malloc((size_t)size)) && fread(data,1,(size_t)size,file)==(size_t)size) *bytes=(size_t)size;
    else {free(data);data=NULL;}
    if(file) fclose(file);
    return data;
}
static bool fill(struct kui_ogg *ogg,unsigned char *pcm,size_t bytes) {
    for(size_t at=0;at<bytes;) {
        size_t count=bytes-at<CHUNK?bytes-at:CHUNK;
        if(kui_ogg_fill(ogg,pcm+at,count)!=count) return false;
        at+=count;
    }
    return true;
}
static bool check(const char *ogg_path,const char *wav_path) {
    size_t ogg_bytes=0,wav_bytes=0;struct kui_wav wav;struct kui_ogg ogg={0};
    unsigned char *file=load(ogg_path,&ogg_bytes),*reference=load(wav_path,&wav_bytes),*pcm=NULL,*start=NULL;
    const char *problem=NULL;double signal=0,noise=0;
    if(!file || !reference || !kui_wav_parse(reference,wav_bytes,&wav)) problem="unreadable input";
    else if(!kui_ogg_open(&ogg,file,ogg_bytes,arena,sizeof(arena),NULL)) problem="rejected by the player's Ogg checks";
    else if(ogg.rate!=wav.rate || ogg.channels!=wav.channels || (size_t)ogg.frames*wav.frame_bytes!=wav.bytes)
        problem="format or frame count differs from the WAV";
    else if(!(pcm=malloc(2u*wav.bytes)) || !(start=malloc(CHUNK))) problem="out of memory";
    else if(!fill(&ogg,pcm,2u*wav.bytes)) problem="decode failed";
    else if(memcmp(pcm,pcm+wav.bytes,wav.bytes)) problem="second loop differs from the first";
    if(!problem) {
        const unsigned char *expected=reference+wav.offset;
        for(size_t i=0;i+1u<wav.bytes;i+=2u) {
            /* Both sides are little-endian PCM16 on the host. */
            double want=(double)(int16_t)(expected[i]|expected[i+1]<<8);
            double got=(double)(int16_t)(pcm[i]|pcm[i+1]<<8);
            signal+=want*want;noise+=(got-want)*(got-want);
        }
        kui_ogg_close(&ogg);
        if(noise>0 && 10.0*log10(signal/noise)<MIN_SNR_DB) problem="waveform does not match the WAV";
        else if(!kui_ogg_reopen(&ogg,file,ogg_bytes,arena,sizeof(arena)) || !fill(&ogg,start,CHUNK) ||
                memcmp(start,pcm,CHUNK)) problem="reopened decoder does not restart identically";
    }
    if(problem) fprintf(stderr,"FAIL %s: %s\n",ogg_path,problem);
    else printf("%s: %u frames at %u Hz, %.1f dB against the WAV, exact loop and reopen\n",
        ogg_path,(unsigned)ogg.frames,(unsigned)ogg.rate,noise>0?10.0*log10(signal/noise):INFINITY);
    kui_ogg_close(&ogg);free(start);free(pcm);free(reference);free(file);
    return !problem;
}
int main(int argc,char **argv) {
    if(argc<3 || !(argc&1)) {fprintf(stderr,"usage: %s OGG WAV [OGG WAV...]\n",argv[0]);return 2;}
    bool ok=true;
    for(int i=1;i<argc;i+=2) ok&=check(argv[i],argv[i+1]);
    return ok?0:1;
}
