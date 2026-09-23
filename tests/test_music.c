/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music.h"
#include "kui/wav.h"
#include "platform.h"
#include <dc/sound/stream.h>
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static struct {
    uint8_t bytes[70044];size_t size,pos;
    unsigned connects,disconnects,opens,reads,closes,unmounts,init,shutdown,alloc,destroy,polls,starts;
    bool cancel,missing,short_read,close_error,unmount_error,init_error,alloc_error,poll_error,queued,active;
    unsigned cancel_after_reads,volume;size_t advertised_size;
    snd_stream_callback_t callback;
    void *previous;uint8_t previous_copy[32768];size_t previous_size;
} fake;
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i));}
static void reset(unsigned channels) {
    memset(&fake,0,sizeof(fake));fake.size=sizeof(fake.bytes);
    memcpy(fake.bytes,"RIFF",4);put32(fake.bytes+4,(uint32_t)fake.size-8);memcpy(fake.bytes+8,"WAVEfmt ",8);
    put32(fake.bytes+16,16);put16(fake.bytes+20,1);put16(fake.bytes+22,channels);put32(fake.bytes+24,22050);
    put32(fake.bytes+28,22050*2*channels);put16(fake.bytes+32,2*channels);put16(fake.bytes+34,16);
    memcpy(fake.bytes+36,"data",4);put32(fake.bytes+40,(uint32_t)fake.size-44);
    for(size_t i=44;i<fake.size;i++) fake.bytes[i]=(uint8_t)i;
}
static bool cancel(void) {return fake.cancel || (fake.cancel_after_reads && fake.reads>=fake.cancel_after_reads);}
static void log_line(const char *fmt,...) {(void)fmt;}
bool kui_sd_connect(void) {assert(!fake.active);++fake.connects;return true;}
void kui_sd_disconnect(void) {++fake.disconnects;}
bool kui_mount(FATFS *fs,kui_log_fn log) {(void)fs;(void)log;return true;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE opt) {assert(!fs && !strcmp(path,"0:") && !opt);++fake.unmounts;return fake.unmount_error?FR_DISK_ERR:FR_OK;}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(!fake.active && mode==FA_READ && !strncmp(path,"0:/KUI/apps/music/",18));++fake.opens;
    if(fake.missing) return FR_NO_FILE;
    memset(file,0,sizeof(*file));file->obj.objsize=fake.advertised_size?fake.advertised_size:fake.size;fake.pos=0;return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT requested,UINT *got) {
    (void)file;assert(!fake.active && requested<=32768 && requested<=fake.size-fake.pos);++fake.reads;
    *got=fake.short_read?requested-1:requested;memcpy(out,fake.bytes+fake.pos,*got);fake.pos+=*got;return FR_OK;
}
FRESULT f_close(FIL *file) {(void)file;++fake.closes;return fake.close_error?FR_DISK_ERR:FR_OK;}
int snd_stream_init_ex(int channels,size_t size) {assert(channels==2 && size==16384);++fake.init;return fake.init_error?-1:0;}
void snd_stream_shutdown(void) {assert(!fake.active);++fake.shutdown;}
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb,int size) {assert(size==16384 && !fake.active);++fake.alloc;fake.callback=cb;return fake.alloc_error?SND_STREAM_INVALID:0;}
void snd_stream_destroy(snd_stream_hnd_t hnd) {assert(hnd==0);++fake.destroy;fake.active=false;fake.previous=NULL;}
void snd_stream_queue_enable(snd_stream_hnd_t hnd) {assert(hnd==0);fake.queued=true;}
void snd_stream_queue_disable(snd_stream_hnd_t hnd) {assert(hnd==0);fake.queued=false;}
void snd_stream_queue_go(snd_stream_hnd_t hnd) {assert(hnd==0 && fake.queued);assert(fake.volume<=255);fake.active=true;}
static void fill(unsigned requested) {
    int got=0;void *data=fake.callback(0,(int)requested,&got);
    assert(data && got==(int)requested && !((uintptr_t)data&31));
    if(fake.previous) {
        assert(data!=fake.previous);
        assert(!memcmp(fake.previous,fake.previous_copy,fake.previous_size));
    }
    memcpy(fake.previous_copy,data,requested);fake.previous=data;fake.previous_size=requested;
}
void snd_stream_start(snd_stream_hnd_t hnd,uint32_t rate,int stereo) {
    assert(hnd==0 && rate==22050 && fake.queued);++fake.starts;fake.volume=255;
    fill(stereo?16384:8192);fill(stereo?16384:8192);
}
void snd_stream_volume(snd_stream_hnd_t hnd,int volume) {assert(hnd==0 && volume>=0 && volume<=255);fake.volume=(unsigned)volume;}
int snd_stream_poll(snd_stream_hnd_t hnd) {assert(hnd==0 && fake.active);++fake.polls;if(fake.poll_error) return -1;fill(32768);return 0;}
int main(void) {
    for(unsigned channels=1;channels<=2;channels++) {
        reset(channels);kui_music_init(log_line);assert(!fake.connects && !fake.init);
        kui_music_set_config(true,30);assert(!fake.connects && !fake.init);
        assert(kui_music_load(0,cancel));const struct kui_music_status *s=kui_music_status();
        assert(s->loaded && !s->playing && s->current_index==0 && s->pcm_bytes==70000);
        assert(fake.disconnects==1 && fake.unmounts==1 && fake.closes==1);
        unsigned reads=fake.reads;kui_music_resume();assert(s->playing && fake.volume==76);
        for(unsigned i=0;i<20;i++) kui_music_service();
        assert(fake.reads==reads && fake.connects==1);
        kui_music_set_config(true,50);assert(fake.volume==127);
        kui_music_pause();assert(!s->playing && s->paused && !fake.active && fake.destroy==1);
        unsigned polls=fake.polls;kui_music_service();assert(fake.polls==polls);
        kui_music_resume();assert(s->playing && fake.reads==reads && fake.connects==1);
        kui_music_set_config(false,50);assert(!s->playing && s->loaded && !s->paused);
        kui_music_resume();assert(!s->playing);kui_music_set_config(true,500);kui_music_resume();assert(fake.volume==255);
        fake.poll_error=true;kui_music_service();assert(!s->playing && strstr(s->message,"playback error"));
        unsigned allocations=fake.alloc;
        for(unsigned i=0;i<10;i++) {kui_music_resume();kui_music_service();}
        assert(fake.alloc==allocations);kui_music_pause();assert(strstr(s->message,"playback error"));
        kui_music_shutdown();assert(!s->loaded && !fake.active && fake.shutdown==1);
    }
    for(unsigned fault=0;fault<7;fault++) {
        reset(1);kui_music_init(log_line);kui_music_set_config(true,15);
        if(fault==0) fake.cancel=true;
        if(fault==1) fake.cancel_after_reads=1;
        if(fault==2) fake.missing=true;
        if(fault==3) fake.short_read=true;
        if(fault==4) fake.advertised_size=KUI_MUSIC_FILE_MAX+1u;
        if(fault==5) fake.close_error=true;
        if(fault==6) fake.unmount_error=true;
        assert(!kui_music_load(1,cancel));assert(!kui_music_status()->loaded && !fake.init);
        if(fault==0) assert(!fake.connects);else assert(fake.disconnects==1 && fake.unmounts==1);
        unsigned connects=fake.connects;for(unsigned i=0;i<5;i++) {kui_music_resume();kui_music_service();}
        assert(fake.connects==connects);kui_music_shutdown();
    }
    reset(1);kui_music_init(log_line);fake.bytes[20]=3;assert(!kui_music_load(0,cancel));assert(!kui_music_status()->loaded);
    assert(!kui_music_load(KUI_MUSIC_TRACKS,cancel));kui_music_shutdown();
    reset(1);kui_music_init(log_line);kui_music_set_config(true,15);assert(kui_music_load(0,cancel));
    fake.init_error=true;kui_music_resume();assert(!kui_music_status()->playing && fake.shutdown==1);
    for(unsigned i=0;i<10;i++) kui_music_resume();
    assert(fake.init==1);fake.init_error=false;kui_music_set_config(true,15);
    fake.alloc_error=true;kui_music_resume();assert(!kui_music_status()->playing);
    unsigned allocations=fake.alloc;fake.alloc_error=false;kui_music_resume();assert(fake.alloc==allocations);
    kui_music_set_config(true,15);
    kui_music_resume();assert(kui_music_status()->playing);fake.cancel_after_reads=fake.reads+1;
    assert(!kui_music_load(2,cancel));assert(!fake.active && !kui_music_status()->loaded);kui_music_shutdown();
    puts("PASS music: bounded/cancellable preload, no streaming I/O, PCM buffers, volume, pause/drain, errors and cleanup");return 0;
}
