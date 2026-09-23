/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music_player.h"
#include "kui/music.h"
#include "platform.h"
#include <dc/sound/stream.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct {
    uint8_t header[44],previous_copy[65536];
    uint64_t size,pos,ms,pcm_seen,next_fill;
    unsigned channels,reads,connects,disconnects,closes,unmounts,releases,pauses,init,shutdown,destroy,starts,polls,volume;
    unsigned dirpos,dircloses,progress;
    bool connected,active,queued,cancel,short_audio,bad_format,init_error,alloc_error,poll_error,close_error,unmount_error;
    uint64_t cancel_ms;unsigned cancel_reads;
    snd_stream_callback_t callback;
    void *previous;size_t previous_size;
} fake;
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i));}
static void reset(uint32_t pcm,unsigned channels) {
    memset(&fake,0,sizeof(fake));fake.size=(uint64_t)pcm+44;fake.channels=channels;
    memcpy(fake.header,"RIFF",4);put32(fake.header+4,pcm+36);memcpy(fake.header+8,"WAVEfmt ",8);
    put32(fake.header+16,16);put16(fake.header+20,1);put16(fake.header+22,channels);put32(fake.header+24,44100);
    put32(fake.header+28,44100*channels*2);put16(fake.header+32,channels*2);put16(fake.header+34,16);
    memcpy(fake.header+36,"data",4);put32(fake.header+40,pcm);
}
static uint8_t sample(uint64_t at) {return (uint8_t)(at*13u+7u);}
static bool cancel(void) {return fake.cancel || (fake.cancel_ms && fake.ms>=fake.cancel_ms) ||
    (fake.cancel_reads && fake.reads>=fake.cancel_reads);}
static void log_line(const char *fmt,...) {(void)fmt;}
static void progress(const struct kui_app_status *s) {assert(s->done<=s->total);++fake.progress;}
void kui_music_pause(void) {assert(!fake.active);++fake.pauses;}
void kui_music_release_audio(void) {assert(!fake.active);++fake.releases;}
bool kui_sd_connect(void) {assert(!fake.connected && !fake.active);fake.connected=true;++fake.connects;return true;}
void kui_sd_disconnect(void) {assert(fake.connected && !fake.active);fake.connected=false;++fake.disconnects;}
bool kui_mount(FATFS *fs,kui_log_fn log) {(void)fs;(void)log;assert(fake.connected);return true;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE mode) {
    assert(!fs && !strcmp(path,"0:") && !mode && !fake.active);++fake.unmounts;
    return fake.unmount_error?FR_DISK_ERR:FR_OK;
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(fake.connected && mode==FA_READ && !strcmp(path,"0:/Music/test.wav"));
    memset(file,0,sizeof(*file));file->obj.objsize=(FSIZE_t)fake.size;fake.pos=0;return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT requested,UINT *got) {
    (void)file;assert(fake.connected && requested<=32768u && requested<=fake.size-fake.pos);++fake.reads;
    *got=fake.short_audio && fake.pos>=44u?requested-1u:requested;
    uint8_t *p=out;
    for(UINT i=0;i<*got;i++) {
        uint64_t at=fake.pos+i;
        p[i]=at<44?fake.header[at]:sample(at-44u);
    }
    fake.pos+=*got;return FR_OK;
}
FRESULT f_lseek(FIL *file,FSIZE_t pos) {(void)file;assert((uint64_t)pos<=fake.size);fake.pos=pos;return FR_OK;}
FRESULT f_close(FIL *file) {(void)file;assert(!fake.active);++fake.closes;return fake.close_error?FR_DISK_ERR:FR_OK;}
FRESULT f_opendir(DIR *dir,const TCHAR *path) {
    (void)dir;assert(!strcmp(path,"0:/Music") && fake.connected);fake.dirpos=0;return FR_OK;
}
FRESULT f_readdir(DIR *dir,FILINFO *info) {
    (void)dir;memset(info,0,sizeof(*info));
    unsigned at=fake.dirpos++;
    if(at==0) {strcpy(info->fname,"Albums");info->fattrib=AM_DIR;}
    else if(at==1) strcpy(info->fname,"ignore.mp3");
    else if(at==2) {strcpy(info->fname,"hidden.wav");info->fattrib=AM_HID;}
    else if(at<13) snprintf(info->fname,sizeof(info->fname),"song%02u.%s",at-3u,at&1u?"WAV":"wav");
    return FR_OK;
}
FRESULT f_closedir(DIR *dir) {(void)dir;++fake.dircloses;return FR_OK;}
uint64_t timer_ms_gettime64(void) {return fake.ms;}
void thd_sleep(unsigned ms) {fake.ms+=ms;}
int snd_stream_init_ex(int channels,size_t size) {assert(channels==2 && size==65536u);++fake.init;return fake.init_error?-1:0;}
void snd_stream_shutdown(void) {assert(!fake.active);++fake.shutdown;}
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb,int size) {
    assert(size==65536 && !fake.active);fake.callback=cb;return fake.alloc_error?SND_STREAM_INVALID:0;
}
void snd_stream_destroy(snd_stream_hnd_t hnd) {assert(hnd==0);fake.active=false;fake.previous=NULL;++fake.destroy;}
void snd_stream_queue_enable(snd_stream_hnd_t hnd) {assert(hnd==0);fake.queued=true;}
void snd_stream_queue_disable(snd_stream_hnd_t hnd) {assert(hnd==0);fake.queued=false;}
void snd_stream_queue_go(snd_stream_hnd_t hnd) {assert(hnd==0 && fake.queued);fake.active=true;}
static bool fill(unsigned requested) {
    int got=0;void *data=fake.callback(0,(int)requested,&got);
    if(!data) {assert(got==0 && (cancel() || fake.short_audio));return false;}
    assert(got==(int)requested && !((uintptr_t)data&31u));
    if(fake.previous) {
        assert(data!=fake.previous);
        assert(!memcmp(fake.previous,fake.previous_copy,fake.previous_size));
    }
    uint64_t remaining=fake.size-44u-fake.pcm_seen;
    size_t actual=remaining<requested?(size_t)remaining:requested;
    for(unsigned i=0;i<requested;i++) assert(((uint8_t *)data)[i]==(i<actual?sample(fake.pcm_seen+i):0));
    fake.pcm_seen+=actual;memcpy(fake.previous_copy,data,requested);fake.previous=data;fake.previous_size=requested;
    return true;
}
void snd_stream_start(snd_stream_hnd_t hnd,uint32_t rate,int stereo) {
    assert(hnd==0 && rate==44100 && stereo==(fake.channels==2) && fake.queued);++fake.starts;fake.volume=255;
    unsigned requested=32768u*fake.channels;
    (void)fill(requested);(void)fill(requested);
    fake.next_fill=fake.ms+371u;
}
void snd_stream_volume(snd_stream_hnd_t hnd,int volume) {assert(hnd==0 && volume>=0 && volume<=255);fake.volume=(unsigned)volume;}
int snd_stream_poll(snd_stream_hnd_t hnd) {
    assert(hnd==0 && fake.active);++fake.polls;if(fake.poll_error) return -1;
    if(fake.ms>=fake.next_fill) {fake.next_fill=fake.ms+371u;return fill(32768u*fake.channels)?0:-3;}
    return 0;
}
int main(void) {
    struct kui_app_status out;struct kui_music_player_page page;
    for(unsigned channels=1;channels<=2;channels++) {
        reset(3u*1024u*1024u+12u,channels);
        kui_music_player_run("/Music/test.wav",75,&out,log_line,cancel,progress);
        assert(out.complete && out.passed && !out.stopped && !out.errors && out.done==fake.size-44u);
        assert(fake.pcm_seen==fake.size-44u && fake.volume==191u);
        assert(fake.connects==1 && fake.disconnects==1 && fake.closes==1 && fake.unmounts==1);
        assert(fake.releases==1 && fake.destroy==1 && fake.shutdown==1 && fake.progress>1);
    }
    for(unsigned fault=0;fault<9;fault++) {
        reset(300000u,2);
        if(fault==0) fake.cancel=true;
        if(fault==1) fake.cancel_ms=500;
        if(fault==2) fake.short_audio=true;
        if(fault==3) fake.header[20]=3;
        if(fault==4) fake.init_error=true;
        if(fault==5) fake.alloc_error=true;
        if(fault==6) fake.poll_error=true;
        if(fault==7) fake.close_error=true;
        if(fault==8) fake.unmount_error=true;
        kui_music_player_run("/Music/test.wav",120,&out,log_line,cancel,progress);
        assert(out.complete && !out.passed && !fake.active && !fake.connected);
        if(fault<2) assert(out.stopped && !out.errors);else assert(out.errors==1);
        if(fault==0) assert(!fake.connects);else assert(fake.disconnects==1 && fake.closes==1 && fake.unmounts==1);
    }
    reset(300000u,2);fake.cancel_reads=5;
    kui_music_player_run("/Music/test.wav",20,&out,log_line,cancel,progress);
    assert(out.stopped && !fake.active && fake.disconnects==1);
    reset(8,1);kui_music_player_run("/Music/../bad.wav",10,&out,log_line,cancel,progress);assert(!out.passed && !fake.connects);
    reset(8,1);kui_music_player_run("/Music/test.mp3",10,&out,log_line,cancel,progress);assert(!out.passed && !fake.connects);
    reset(8,1);assert(kui_music_player_list("/Music",0,&page,log_line,cancel));
    assert(page.count==8 && page.has_more && page.entries[0].directory && !strcmp(page.entries[0].name,"Albums"));
    assert(!strcmp(page.entries[1].name,"song00.WAV") && !page.entries[1].directory);
    assert(fake.disconnects==1 && fake.dircloses==1);
    assert(kui_music_player_list("/Music",8,&page,log_line,cancel));
    assert(page.count==3 && !page.has_more && !strcmp(page.entries[0].name,"song07.wav"));
    unsigned connects=fake.connects;fake.cancel=true;
    assert(!kui_music_player_list("/Music",0,&page,log_line,cancel) && fake.connects==connects);
    puts("PASS SD music: >2MiB stereo/mono, exact PCM/tail, DMA buffer lifetime, stop/fault cleanup, read-only browsing");return 0;
}
