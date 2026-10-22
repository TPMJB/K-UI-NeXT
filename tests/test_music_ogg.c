/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music_ogg.h"
#include "fixtures/music_vorbis.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static _Alignas(16) unsigned char arena[KUI_OGG_WORKSPACE_BYTES];
static _Alignas(16) short pcm[65536],second[65536];
static bool stopped(void) {return true;}
static uint32_t read32(const unsigned char *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static void put32(unsigned char *p,uint32_t n) {
    for(unsigned i=0;i<4;i++) p[i]=(unsigned char)(n>>(8*i));
}
static void recheck(unsigned char *p,size_t bytes) {
    for(size_t at=0;at<bytes;) {
        assert(bytes-at>=27u);size_t header=27u+p[at+26],size=header;
        assert(header<=bytes-at);
        for(size_t i=27;i<header;i++) size+=p[at+i];
        assert(size<=bytes-at);uint32_t crc=0;
        for(size_t i=0;i<size;i++) {
            crc^=(uint32_t)((i>=22 && i<26)?0:p[at+i])<<24;
            for(unsigned bit=0;bit<8;bit++) crc=crc&0x80000000u?(crc<<1)^0x04c11db7u:crc<<1;
        }
        put32(p+at+22,crc);at+=size;
    }
}
static size_t next_page(const unsigned char *p,size_t at) {
    size_t size=27u+p[at+26];for(size_t i=27;i<27u+p[at+26];i++) size+=p[at+i];return at+size;
}
int main(void) {
    struct kui_ogg ogg;
    for(unsigned channels=1;channels<=2;channels++) {
        const unsigned char *file=channels==1?ogg_mono:ogg_stereo;
        size_t length=channels==1?sizeof(ogg_mono):sizeof(ogg_stereo);
        assert(kui_ogg_open(&ogg,file,length,arena,sizeof(arena),NULL));
        assert(ogg.channels==channels && ogg.rate==(channels==1?22050u:44100u));
        assert(ogg.frames==ogg.rate/2u);
        size_t period=ogg.frames*channels*2u;
        /* The decoded period wraps exactly and can span several copies inside
         * one callback without allocating or reading storage. */
        assert(kui_ogg_fill(&ogg,pcm,period)==period);
        assert(kui_ogg_fill(&ogg,second,period)==period);
        assert(!memcmp(pcm,second,period));
        bool nonzero=false;for(size_t i=0;i<period/2u;i++) nonzero|=pcm[i]!=0;
        assert(nonzero);
        for(unsigned i=0;i<20u;i++) assert(kui_ogg_fill(&ogg,pcm,sizeof(pcm))==sizeof(pcm));
        assert(!kui_ogg_fill(&ogg,pcm,3));
        kui_ogg_close(&ogg);assert(!ogg.decoder);
        assert(!kui_ogg_open(&ogg,file,length,arena,sizeof(arena),stopped));
        assert(!kui_ogg_open(&ogg,file,length,arena+1,sizeof(arena)-1,NULL));
        assert(!kui_ogg_open(&ogg,file,length,arena,1024,NULL));
        for(size_t cut=0;cut<length;cut+=31u) assert(!kui_ogg_open(&ogg,file,cut,arena,sizeof(arena),NULL));
        assert(!kui_ogg_open(&ogg,file,length-1u,arena,sizeof(arena),NULL));
    }
    unsigned char broken[sizeof(ogg_stereo)+1];
    for(size_t at=0;at<sizeof(ogg_stereo);at+=29u) {
        memcpy(broken,ogg_stereo,sizeof(ogg_stereo));broken[at]^=0x80u;
        assert(!kui_ogg_open(&ogg,broken,sizeof(ogg_stereo),arena,sizeof(arena),NULL));
    }
    memcpy(broken,ogg_stereo,sizeof(ogg_stereo));broken[sizeof(ogg_stereo)]=0;
    assert(!kui_ogg_open(&ogg,broken,sizeof(broken),arena,sizeof(arena),NULL));
    /* Well-checksummed but unsupported headers (three channels, high sample
     * rate), changing serials, and broken Vorbis signature are rejected too. */
    memcpy(broken,ogg_stereo,sizeof(ogg_stereo));size_t payload=27u+broken[26];
    assert(broken[payload]==1 && !memcmp(broken+payload+1,"vorbis",6));
    broken[payload+11]=3;recheck(broken,sizeof(ogg_stereo));
    assert(!kui_ogg_open(&ogg,broken,sizeof(ogg_stereo),arena,sizeof(arena),NULL));
    assert(!kui_ogg_reopen(&ogg,broken,sizeof(ogg_stereo),arena,sizeof(arena)));
    memcpy(broken,ogg_stereo,sizeof(ogg_stereo));put32(broken+payload+12,96000);recheck(broken,sizeof(ogg_stereo));
    assert(!kui_ogg_open(&ogg,broken,sizeof(ogg_stereo),arena,sizeof(arena),NULL));
    assert(!kui_ogg_reopen(&ogg,broken,sizeof(ogg_stereo),arena,sizeof(arena)));
    /* Reopening already-validated bytes skips only the whole-file page scan:
     * it restarts identically, and arena and format checks still apply. */
    assert(kui_ogg_open(&ogg,ogg_mono,sizeof(ogg_mono),arena,sizeof(arena),NULL));
    assert(kui_ogg_fill(&ogg,pcm,8192)==8192);kui_ogg_close(&ogg);
    assert(kui_ogg_reopen(&ogg,ogg_mono,sizeof(ogg_mono),arena,sizeof(arena)));
    assert(ogg.rate==22050u && ogg.channels==1u && ogg.frames==11025u);
    assert(kui_ogg_fill(&ogg,second,8192)==8192 && !memcmp(pcm,second,8192));kui_ogg_close(&ogg);
    assert(!kui_ogg_reopen(&ogg,ogg_mono,sizeof(ogg_mono),arena+1,sizeof(arena)-1));
    assert(!kui_ogg_reopen(&ogg,ogg_mono,sizeof(ogg_mono),arena,1024));
    assert(!kui_ogg_reopen(&ogg,NULL,sizeof(ogg_mono),arena,sizeof(arena)) && !ogg.decoder);
    memcpy(broken,ogg_stereo,sizeof(ogg_stereo));broken[payload+1]='x';recheck(broken,sizeof(ogg_stereo));
    assert(!kui_ogg_open(&ogg,broken,sizeof(ogg_stereo),arena,sizeof(arena),NULL));
    memcpy(broken,ogg_stereo,sizeof(ogg_stereo));size_t page=next_page(broken,0);
    put32(broken+page+14,read32(broken+page+14)+1u);recheck(broken,sizeof(ogg_stereo));
    assert(!kui_ogg_open(&ogg,broken,sizeof(ogg_stereo),arena,sizeof(arena),NULL));
    puts("PASS Ogg: mono/stereo decode, exact loop, bounded arena, cancellation, truncation, bad CRC/headers/serial/trailing data, identical reopen");
    return 0;
}
