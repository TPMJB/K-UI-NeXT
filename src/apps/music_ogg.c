/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music_ogg.h"
#include "platform.h"
#ifdef KUI_ON_CONSOLE
/* Upstream initializes a shared CRC table on each decoder open. Serialize
 * codec calls across staging and playback; page validation stays outside this
 * lock. The player audio mutex is never acquired from here. */
#define KUI_AUDIO_CODEC_LOCK 1
#include <kos/mutex.h>
static mutex_t codec_lock=MUTEX_INITIALIZER;
static void lock_codec(void) {mutex_lock(&codec_lock);}
static void unlock_codec(void) {mutex_unlock(&codec_lock);}
#else
static void lock_codec(void) {}
static void unlock_codec(void) {}
#endif
#include <limits.h>
#include <string.h>
/* Upstream has two short-read guards and an unsigned granule-byte fix recorded
 * in third_party/stb/README.md. The wrapper restricts it to memory-only
 * pull decoding, two channels, and a fixed allocation arena. */
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_MAX_CHANNELS 2
#define STB_VORBIS_NO_FAST_SCALED_FLOAT
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wpedantic"
#endif
#include "../../third_party/stb/stb_vorbis.c"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

static uint32_t le32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
/* Reject truncated/corrupt pages, serial changes/chained streams and trailing
 * bytes before asking the codec to inspect setup packets. CRC is the Ogg page
 * checksum (not reflected IEEE CRC32). Cancellation is checked each 4 KiB. */
static bool pages_valid(const uint8_t *p,size_t bytes,bool (*cancel)(void)) {
    size_t at=0;uint32_t serial=0,sequence=0;bool eos=false;
    while(at<bytes) {
        if(cancel && cancel()) return false;
        if(eos || bytes-at<27u || memcmp(p+at,"OggS",4) || p[at+4] || (p[at+5]&~7u)) return false;
        size_t header=27u+p[at+26],page=header;
        if(header>bytes-at) return false;
        for(size_t i=27;i<header;i++) page+=p[at+i];
        if(page>bytes-at) return false;
        if(!at) {serial=le32(p+at+14);if(!(p[at+5]&2u) || (p[at+5]&1u)) return false;}
        else if(p[at+5]&2u) return false;
        if(le32(p+at+14)!=serial || le32(p+at+18)!=sequence++) return false;
        uint32_t crc=0;
        for(size_t i=0;i<page;i++) {
            if(!(i&4095u) && cancel && cancel()) return false;
            crc^=(uint32_t)((i>=22u && i<26u)?0:p[at+i])<<24;
            for(unsigned bit=0;bit<8;bit++) crc=crc&0x80000000u?(crc<<1)^0x04c11db7u:crc<<1;
        }
        if(crc!=le32(p+at+22)) return false;
        eos=(p[at+5]&4u)!=0;at+=page;
    }
    return at==bytes && eos;
}
void kui_ogg_close(struct kui_ogg *ogg) {
    if(!ogg) return;
    if(ogg->decoder) stb_vorbis_close(ogg->decoder);
    memset(ogg,0,sizeof(*ogg));
}
static bool open_decoder(struct kui_ogg *out,const uint8_t *file,size_t bytes,
    void *workspace,size_t workspace_bytes,bool check_pages,bool (*cancel)(void)) {
    if(!out) return false;
    memset(out,0,sizeof(*out));
    if(!file || !workspace || bytes>INT_MAX || workspace_bytes<KUI_OGG_WORKSPACE_BYTES ||
       ((uintptr_t)workspace&15u) || (check_pages && !pages_valid(file,bytes,cancel))) return false;
    stb_vorbis_alloc arena={(char *)workspace,KUI_OGG_WORKSPACE_BYTES};int error=0;
    lock_codec();stb_vorbis *v=stb_vorbis_open_memory(file,(int)bytes,&error,&arena);
    if(!v) {unlock_codec();return false;}
    stb_vorbis_info info=stb_vorbis_get_info(v);
    unsigned frames=stb_vorbis_stream_length_in_samples(v);
    if(info.channels<1 || info.channels>2 || info.sample_rate<8000u || info.sample_rate>44100u ||
       frames<1024u || frames>UINT32_MAX/(unsigned)info.channels/2u ||
       info.setup_memory_required+info.temp_memory_required>KUI_OGG_WORKSPACE_BYTES ||
       !stb_vorbis_seek_start(v) || (cancel && cancel())) {
        stb_vorbis_close(v);unlock_codec();return false;
    }
    out->decoder=v;out->rate=info.sample_rate;out->channels=(unsigned)info.channels;out->frames=frames;
    unlock_codec();return true;
}
bool kui_ogg_open(struct kui_ogg *out,const uint8_t *file,size_t bytes,
    void *workspace,size_t workspace_bytes,bool (*cancel)(void)) {
    return open_decoder(out,file,bytes,workspace,workspace_bytes,true,cancel);
}
bool kui_ogg_reopen(struct kui_ogg *out,const uint8_t *file,size_t bytes,
    void *workspace,size_t workspace_bytes) {
    return open_decoder(out,file,bytes,workspace,workspace_bytes,false,NULL);
}
size_t kui_ogg_fill(struct kui_ogg *ogg,void *pcm,size_t bytes) {
    if(!ogg || !ogg->decoder || ogg->failed || !pcm || !bytes || bytes>131072u ||
       ((uintptr_t)pcm&1u) || bytes%(ogg->channels*2u)) return 0;
    lock_codec();size_t done=0;bool just_rewound=false;
    while(done<bytes) {
        int frames=stb_vorbis_get_samples_short_interleaved(ogg->decoder,(int)ogg->channels,
            (short *)((uint8_t *)pcm+done),(int)((bytes-done)/2u));
        int error=stb_vorbis_get_error(ogg->decoder);
        if(error) {ogg->failed=true;unlock_codec();return 0;}
        if(frames>0) {done+=(size_t)frames*ogg->channels*2u;just_rewound=false;continue;}
        /* At most one rewind without output: malformed/empty input cannot spin.
         * Streams shorter than 1024 frames were rejected during open. */
        if(just_rewound || !stb_vorbis_seek_start(ogg->decoder)) {ogg->failed=true;unlock_codec();return 0;}
        just_rewound=true;
    }
    unlock_codec();return done;
}
