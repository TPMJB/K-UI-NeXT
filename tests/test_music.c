/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/music.h"
#include "kui/wav.h"
#include "kui/music_ogg.h"
#include "fixtures/music_vorbis.h"
#include "platform.h"
#include <dc/sound/stream.h>
#ifdef KUI_ON_CONSOLE
#include <kos/thread.h>
#endif
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
/* The player's one shared decoder allocation, including alignment slack. */
#define ARENA_BYTES (KUI_OGG_WORKSPACE_BYTES+15u)
#ifdef KUI_MUSIC_ALLOC_TEST
/* The linker wraps the real allocation calls in the player. This independent
 * ledger catches retained allocations even if status accounting is wrong. */
static struct {
    struct {void *pointer;size_t bytes;} live[16];
    size_t bytes,peak;
    unsigned allocations,frees;
    unsigned fail_at; /* 1 fails the next allocation, 2 the one after, ... */
} heap;
void *__real_malloc(size_t bytes);
void __real_free(void *pointer);
void *__wrap_malloc(size_t bytes) {
    if(heap.fail_at && !--heap.fail_at) return NULL;
    void *pointer=__real_malloc(bytes);
    if(!pointer) return NULL;
    unsigned i=0;while(i<16u && heap.live[i].pointer) ++i;assert(i<16u);
    heap.live[i].pointer=pointer;heap.live[i].bytes=bytes;
    heap.bytes+=bytes;++heap.allocations;if(heap.bytes>heap.peak) heap.peak=heap.bytes;
    assert(heap.bytes<=KUI_MUSIC_CACHE_MAX);return pointer;
}
void __wrap_free(void *pointer) {
    if(!pointer) return;
    unsigned i=0;while(i<16u && heap.live[i].pointer!=pointer) ++i;assert(i<16u);
    assert(heap.bytes>=heap.live[i].bytes);heap.bytes-=heap.live[i].bytes;++heap.frees;
    memset(&heap.live[i],0,sizeof(heap.live[i]));__real_free(pointer);
}
#endif
static struct {
    uint8_t header[44];const uint8_t *ogg_file;size_t size,pos;
    /* Bundled folder contents: packages now ship the Oggs; a 1.5 card holds
     * only the WAVs. open_* describe the file currently being read. */
    bool bundled_ogg,bundled_wav;const uint8_t *open_ogg;size_t open_size;
    unsigned connects,disconnects,opens,reads,closes,unmounts,init,shutdown,alloc,destroy,polls,starts;
    bool cancel,missing,short_read,close_error,unmount_error,init_error,alloc_error,poll_error,queued,active;
    unsigned cancel_after_reads,volume;size_t advertised_size;
    snd_stream_callback_t callback;
    void *previous;uint8_t previous_copy[131072];size_t previous_size;
    unsigned log_lines;char last_log[256];
} fake;
#ifdef KUI_ON_CONSOLE
static kthread_t fake_thread;
static bool thread_live,thread_fail;
kthread_t *thd_create_ex(const kthread_attr_t *attrs,void *(*routine)(void *),void *param) {
    assert(!thread_live && attrs->stack_size==32768 && !strcmp(attrs->label,"kui-audio") && routine && !param);
    if(thread_fail) return NULL;
    thread_live=true;return &fake_thread;
}
int thd_join(kthread_t *thread,void **result) {assert(thread==&fake_thread && thread_live && !result);thread_live=false;return 0;}
void thd_sleep(unsigned ms) {assert(ms==8);}
#endif
static void put16(uint8_t *p,unsigned n) {p[0]=(uint8_t)n;p[1]=(uint8_t)(n>>8);}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;i++) p[i]=(uint8_t)(n>>(8*i));}
static void source(unsigned channels,size_t pcm) {
    fake.ogg_file=NULL;fake.size=pcm+44u;fake.pos=0;memset(fake.header,0,sizeof(fake.header));
    memcpy(fake.header,"RIFF",4);put32(fake.header+4,(uint32_t)fake.size-8);memcpy(fake.header+8,"WAVEfmt ",8);
    put32(fake.header+16,16);put16(fake.header+20,1);put16(fake.header+22,channels);put32(fake.header+24,22050);
    put32(fake.header+28,22050*2*channels);put16(fake.header+32,2*channels);put16(fake.header+34,16);
    memcpy(fake.header+36,"data",4);put32(fake.header+40,(uint32_t)pcm);
}
static void reset(unsigned channels) {
#ifdef KUI_MUSIC_ALLOC_TEST
    assert(!heap.bytes && heap.allocations==heap.frees);
    memset(&heap,0,sizeof(heap));
#endif
    /* Default to a 1.5 card, so the PCM cases below also cover the fallback
     * from each missing bundled Ogg to its original WAV. */
    memset(&fake,0,sizeof(fake));fake.bundled_wav=true;source(channels,70000u);
}
static struct kui_music_status state(void) {struct kui_music_status out;kui_music_status_copy(&out);return out;}
#if defined(KUI_ON_CONSOLE) && defined(KUI_MUSIC_ALLOC_TEST)
static bool interleave_resume,inside_unlock_hook;
static unsigned resume_interleavings;
void kui_music_test_unlock_hook(void) {
    if(!interleave_resume || inside_unlock_hook) return;
    inside_unlock_hook=true;++resume_interleavings;
    /* Model a control thread resuming at every possible mutex-release boundary.
     * The public resume locks again; guard only this deterministic test hook. */
    kui_music_resume();
    inside_unlock_hook=false;
}
#endif
static void allocations_match(void) {
    struct kui_music_status s=state();
    assert(s.cache_bytes+s.loading_bytes<=KUI_MUSIC_CACHE_MAX);
    assert(s.peak_file_bytes<=KUI_MUSIC_CACHE_MAX);
#ifdef KUI_MUSIC_ALLOC_TEST
    assert(heap.bytes==s.cache_bytes+s.loading_bytes);
    assert(heap.peak==s.peak_file_bytes);
    assert(heap.allocations==s.file_allocations && heap.frees==s.file_frees);
#endif
}
static bool cancel(void) {return fake.cancel || (fake.cancel_after_reads && fake.reads>=fake.cancel_after_reads);}
static void log_line(const char *fmt,...) {
    /* A logger may snapshot status: logging under audio_lock would deadlock. */
    (void)state();++fake.log_lines;
    va_list ap;va_start(ap,fmt);vsnprintf(fake.last_log,sizeof(fake.last_log),fmt,ap);va_end(ap);
}
bool kui_sd_connect(void) {++fake.connects;return true;}
void kui_sd_disconnect(void) {++fake.disconnects;}
bool kui_mount(FATFS *fs,kui_log_fn log) {(void)fs;(void)log;return true;}
FRESULT f_mount(FATFS *fs,const TCHAR *path,BYTE opt) {assert(!fs && !strcmp(path,"0:") && !opt);++fake.unmounts;return fake.unmount_error?FR_DISK_ERR:FR_OK;}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    bool bundled=!strncmp(path,"0:/KUI/apps/music/",18),ogg=strlen(path)>4u && !strcmp(path+strlen(path)-4u,".ogg");
    assert(mode==FA_READ && (bundled || !strcmp(path,"0:/Music/test.wav") || !strcmp(path,"0:/Music/test.ogg")));++fake.opens;
    if(fake.missing || (bundled && !(ogg?fake.bundled_ogg:fake.bundled_wav))) return FR_NO_FILE;
    /* Every bundled Ogg is the mono fixture; other files follow source(). */
    fake.open_ogg=bundled?(ogg?ogg_mono:NULL):fake.ogg_file;
    fake.open_size=bundled && ogg?sizeof(ogg_mono):fake.size;
    memset(file,0,sizeof(*file));file->obj.objsize=fake.advertised_size?fake.advertised_size:fake.open_size;fake.pos=0;return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT requested,UINT *got) {
    (void)file;assert(requested<=32768 && requested<=fake.open_size-fake.pos);++fake.reads;
    /* An Ogg is read with its staging arena already reserved and allocated. */
    allocations_match();assert(state().loading_bytes==fake.open_size+(fake.open_ogg?ARENA_BYTES:0u));
    *got=fake.short_read?requested-1:requested;
    uint8_t *p=out;for(UINT i=0;i<*got;i++) {size_t at=fake.pos+i;p[i]=fake.open_ogg?fake.open_ogg[at]:at<44u?fake.header[at]:(uint8_t)at;}
    fake.pos+=*got;
    /* Model the independently scheduled RAM-only audio poll during card I/O. */
    if(fake.active) kui_music_service();
    return FR_OK;
}
FRESULT f_close(FIL *file) {(void)file;++fake.closes;return fake.close_error?FR_DISK_ERR:FR_OK;}
int snd_stream_init_ex(int channels,size_t size) {assert(channels==2 && size==65536);++fake.init;return fake.init_error?-1:0;}
void snd_stream_shutdown(void) {assert(!fake.active);++fake.shutdown;}
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t cb,int size) {assert(size==65536 && !fake.active);++fake.alloc;fake.callback=cb;return fake.alloc_error?SND_STREAM_INVALID:0;}
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
    fill(stereo?65536:32768);fill(stereo?65536:32768);
}
void snd_stream_volume(snd_stream_hnd_t hnd,int volume) {assert(hnd==0 && volume>=0 && volume<=255);fake.volume=(unsigned)volume;}
int snd_stream_poll(snd_stream_hnd_t hnd) {assert(hnd==0 && fake.active);++fake.polls;if(fake.poll_error) return -1;fill(131072);return 0;}
/* Mono starts fill two 32 KiB halves; previous_copy holds the second. A WAV
 * source's PCM byte k is (uint8_t)(44+k). */
static bool played_pcm(void) {
    for(size_t i=0;i<fake.previous_size;i++) if(fake.previous_copy[i]!=(uint8_t)(44u+32768u+i)) return false;
    return fake.previous_size==32768u;
}
/* A selected Ogg restarts from its first decoded sample. */
static bool played_ogg(void) {
    static _Alignas(16) uint8_t arena[KUI_OGG_WORKSPACE_BYTES];
    static uint8_t first[32768],second[32768];
    struct kui_ogg ogg;
    if(!kui_ogg_open(&ogg,ogg_mono,sizeof(ogg_mono),arena,sizeof(arena),NULL)) return false;
    bool same=kui_ogg_fill(&ogg,first,sizeof(first))==sizeof(first) &&
        kui_ogg_fill(&ogg,second,sizeof(second))==sizeof(second) &&
        fake.previous_size==sizeof(second) && !memcmp(fake.previous_copy,second,sizeof(second));
    kui_ogg_close(&ogg);return same;
}
static void compressed_music(void) {
    (void)ogg_stereo;
    reset(1);kui_music_init(log_line);kui_music_set_config(true,20);
    assert(kui_music_load(0,cancel));
    fake.ogg_file=ogg_mono;fake.size=sizeof(ogg_mono);
    assert(kui_music_load_path("/Music/test.ogg","Compressed song",cancel));
    allocations_match();assert(state().compressed && state().decoder_bytes==ARENA_BYTES);
    assert(state().playing && state().current_index==KUI_MUSIC_CUSTOM_INDEX);
    unsigned reads=fake.reads,connects=fake.connects,allocations=state().file_allocations;
    for(unsigned i=0;i<30u;i++) kui_music_service();
    unsigned wanted=99;assert(kui_music_step_cached(1,&wanted) && wanted==0);
    assert(!state().compressed && played_pcm());
    assert(kui_music_step_cached(-1,&wanted) && wanted==KUI_MUSIC_CUSTOM_INDEX);
    assert(state().compressed && played_ogg());
    for(unsigned i=0;i<30u;i++) kui_music_service();
    assert(fake.reads==reads && fake.connects==connects && state().file_allocations==allocations);
    /* A second compressed load is validated in a tracked staging arena while
     * the first decoder keeps polling in the shared one; no hidden heap. */
    for(unsigned i=0;i<20u;i++) {
        assert(kui_music_load_path("/Music/test.ogg","Replacement Ogg",cancel));allocations_match();
        fake.cancel_after_reads=fake.reads+1;
        assert(!kui_music_load_path("/Music/test.ogg","Cancelled Ogg",cancel));
        fake.cancel_after_reads=0;allocations_match();assert(state().playing && state().compressed);
    }
    reads=fake.reads;connects=fake.connects;kui_music_clear_cache();allocations_match();
    assert(!state().loaded && !state().playing && !state().cache_bytes && !state().cached_mask &&
           !state().compressed && !state().decoder_bytes && state().enabled && state().volume==20);
    assert(fake.reads==reads && fake.connects==connects && state().file_allocations==state().file_frees);
    assert(kui_music_next_index(0,-1)==4 && kui_music_next_index(4,1)==0);
    kui_music_shutdown();
}
#ifdef KUI_MUSIC_ALLOC_TEST
/* Model RAM corruption of the single live allocation of this size. */
static void corrupt_cached(size_t bytes,size_t at) {
    unsigned found=0;
    for(unsigned i=0;i<16u;i++) if(heap.live[i].pointer && heap.live[i].bytes==bytes) {
        ((uint8_t *)heap.live[i].pointer)[at]^=0xffu;++found;
    }
    assert(found==1u);
}
#endif
static void bundled_ogg(void) {
    /* The shipped layout: five compressed songs share one decoder arena, and
     * cycling reopens the chosen song without allocating or touching SD. */
    reset(1);fake.bundled_ogg=true;fake.bundled_wav=false;
    kui_music_init(log_line);kui_music_set_config(true,20);
    for(unsigned i=0;i<KUI_MUSIC_TRACKS;i++) {assert(kui_music_load(i,cancel));allocations_match();}
    struct kui_music_status s=state();
    assert(s.cached_mask==31u && s.playing && s.compressed && s.current_index==4 && s.pcm_bytes==22050u);
    assert(s.decoder_bytes==ARENA_BYTES && s.cache_bytes==KUI_MUSIC_TRACKS*sizeof(ogg_mono)+ARENA_BYTES);
    /* Each load staged its own arena; only the first was kept for playback. */
    assert(s.file_allocations==2u*KUI_MUSIC_TRACKS && s.file_frees==KUI_MUSIC_TRACKS-1u);
    assert(s.peak_file_bytes==KUI_MUSIC_TRACKS*sizeof(ogg_mono)+2u*ARENA_BYTES);
    assert(fake.opens==KUI_MUSIC_TRACKS);
    unsigned reads=fake.reads,connects=fake.connects,allocated=s.file_allocations,starts=fake.starts;
    for(unsigned i=0;i<100u;i++) {
        assert(kui_music_select_cached(i%KUI_MUSIC_TRACKS));allocations_match();
        assert(state().playing && state().compressed && state().current_index==i%KUI_MUSIC_TRACKS);
        kui_music_service();
    }
    assert(fake.starts==starts+100u);
    unsigned wanted=99;assert(kui_music_step_cached(-1,&wanted) && wanted==3 && state().compressed && played_ogg());
    assert(fake.reads==reads && fake.connects==connects && state().file_allocations==allocated);
    /* A browser path with the 1.5 WAV name, or any ASCII case, reuses the slot. */
    assert(kui_music_load_path("/KUI/apps/music/menu.wav","Legacy name",cancel));
    assert(state().current_index==0 && state().compressed && !strcmp(state().title,"After Hours"));
    assert(kui_music_load_path("/kui/APPS/MUSIC/NEON-CIRCUIT.OGG","Upper case",cancel));
    assert(state().current_index==1 && state().file_allocations==allocated && fake.reads==reads);
    /* A custom Ogg shares the same arena; its staging arena is released. */
    fake.ogg_file=ogg_mono;fake.size=sizeof(ogg_mono);
    assert(kui_music_load_path("/Music/test.ogg","Custom Ogg",cancel));allocations_match();
    assert(state().current_index==KUI_MUSIC_CUSTOM_INDEX && state().compressed && state().decoder_bytes==ARENA_BYTES);
    assert(state().cache_bytes==(KUI_MUSIC_TRACKS+1u)*sizeof(ogg_mono)+ARENA_BYTES);
#ifdef KUI_MUSIC_ALLOC_TEST
    /* Failing either the file or its staging arena keeps the current song. */
    for(unsigned fail=1;fail<=2u;fail++) {
        allocated=state().file_allocations;heap.fail_at=fail;
        assert(!kui_music_load_path("/Music/test.ogg","No memory",cancel));
        assert(!heap.fail_at && strstr(fake.last_log,"Not enough RAM"));allocations_match();
        assert(state().playing && !strcmp(state().title,"Custom Ogg") && !state().loading_bytes);
        assert(state().file_allocations==allocated+fail-1u);
    }
#endif
    kui_music_clear_cache();allocations_match();
    assert(!state().cache_bytes && !state().decoder_bytes && !state().loaded);
    assert(state().file_allocations==state().file_frees);
    kui_music_shutdown();
    /* Both formats present: the Ogg wins. Only the WAV: it is played as PCM. */
    reset(1);fake.bundled_ogg=true;kui_music_init(log_line);kui_music_set_config(true,20);
    assert(kui_music_load(2,cancel) && state().compressed && fake.opens==1);
    kui_music_shutdown();
    reset(1);kui_music_init(log_line);kui_music_set_config(true,20);
    assert(kui_music_load(2,cancel) && !state().compressed && state().pcm_bytes==70000u && fake.opens==2);
    assert(!state().decoder_bytes && state().cache_bytes==70044u);
    kui_music_shutdown();
    reset(1);fake.bundled_wav=false;kui_music_init(log_line);kui_music_set_config(true,20);
    assert(!kui_music_load(2,cancel) && fake.opens==2 && strstr(state().message,"Track file missing"));
    kui_music_shutdown();
    /* Replacing the only cached Ogg with a WAV releases the shared arena. */
    reset(1);kui_music_init(log_line);kui_music_set_config(true,20);
    assert(kui_music_load(0,cancel));
    fake.ogg_file=ogg_mono;fake.size=sizeof(ogg_mono);
    assert(kui_music_load_path("/Music/test.ogg","Custom Ogg",cancel) && state().decoder_bytes==ARENA_BYTES);
    fake.ogg_file=NULL;source(1,70000u);
    assert(kui_music_load_path("/Music/test.wav","Custom WAV",cancel));allocations_match();
    assert(!state().compressed && !state().decoder_bytes && state().cache_bytes==2u*70044u && state().playing);
    assert(played_pcm());
    kui_music_shutdown();
    /* Evicting the last cached Ogg also releases its arena, which is exactly
     * what lets this 2 MiB replacement reach the 8 MiB budget. */
    reset(1);fake.bundled_ogg=true;fake.bundled_wav=false;
    kui_music_init(log_line);kui_music_set_config(true,20);
    for(unsigned i=0;i<KUI_MUSIC_TRACKS;i++) assert(kui_music_cache_menu(i,cancel));
    source(1,KUI_MUSIC_CUSTOM_MAX-44u);
    assert(kui_music_load_path("/Music/test.wav","Six MiB",cancel));allocations_match();
    assert(state().decoder_bytes==ARENA_BYTES && KUI_MUSIC_CACHE_MAX-state().cache_bytes<2u*1024u*1024u);
    source(1,2u*1024u*1024u-44u);
    assert(kui_music_load_path("/Music/test.wav","Two MiB",cancel));allocations_match();
    assert(state().cached_mask==1u<<KUI_MUSIC_CUSTOM_INDEX && !state().decoder_bytes && state().playing);
    assert(state().cache_bytes==2u*1024u*1024u && state().peak_file_bytes==KUI_MUSIC_CACHE_MAX);
    kui_music_shutdown();
#ifdef KUI_MUSIC_ALLOC_TEST
    /* A cached Ogg that no longer opens stops cleanly; others still play. */
    reset(1);kui_music_init(log_line);kui_music_set_config(true,20);
    assert(kui_music_load(0,cancel));
    fake.ogg_file=ogg_mono;fake.size=sizeof(ogg_mono);
    assert(kui_music_load_path("/Music/test.ogg","Custom Ogg",cancel) && kui_music_select_cached(0));
    corrupt_cached(sizeof(ogg_mono),27u+ogg_mono[26]+1u);
    allocated=state().file_allocations;
    assert(!kui_music_step_cached(-1,&wanted) && wanted==KUI_MUSIC_CUSTOM_INDEX);
    assert(!state().loaded && !state().playing && strstr(state().message,"did not reopen"));
    assert(kui_music_select_cached(0) && state().playing && !state().compressed && played_pcm());
    assert(state().file_allocations==allocated);allocations_match();
    kui_music_shutdown();
#endif
}
static void cache_churn(void) {
    /* Reproduce the 1.5 five-WAV working set, then cycle it repeatedly.
     * Selecting cached music must not allocate file memory or touch storage. */
    static const size_t sizes[KUI_MUSIC_TRACKS]={1058444,846764,1058444,769790,940844};
    reset(1);kui_music_init(log_line);kui_music_set_config(true,15);
    for(unsigned i=0;i<KUI_MUSIC_TRACKS;i++) {
        source(1,sizes[i]-44u);assert(kui_music_load(i,cancel));allocations_match();
    }
    assert(state().cache_bytes==4674286u);
    unsigned allocated=state().file_allocations,reads=fake.reads,connects=fake.connects;
    assert(kui_music_load_path("/KUI/apps/music/menu.wav","Manual first song",cancel));
    assert(state().current_index==0 && !strcmp(state().title,"After Hours"));
    assert(kui_music_load_path("/kui/APPS/MUSIC/NEON-CIRCUIT.WAV","Manual second song",cancel));
    assert(state().current_index==1 && state().cached_mask==31u);
    assert(state().file_allocations==allocated && state().cache_bytes==4674286u);
    fake.cancel=true;
    assert(!kui_music_load_path("/KUI/apps/music/menu.wav","Cancelled selection",cancel));
    fake.cancel=false;assert(state().current_index==1);
    for(unsigned i=0;i<1000u;i++) {
        assert(kui_music_select_cached(i%KUI_MUSIC_TRACKS));allocations_match();
        assert(state().cache_bytes==4674286u && state().file_allocations==allocated);
    }
    assert(fake.reads==reads && fake.connects==connects);
    /* Each manual selection replaces the custom slot. Failed or cancelled
     * staging must release the temporary bytes and preserve the current song. */
    for(unsigned i=0;i<300u;i++) {
        source(1,65536u+(i%7u)*2048u);
        assert(kui_music_load_path("/Music/test.wav","Repeated choice",cancel));
        allocations_match();assert(state().cache_bytes==4674286u+fake.size);
        if(i%3u==0) fake.short_read=true;
        else if(i%3u==1) fake.cancel_after_reads=fake.reads+1u;
        else fake.close_error=true;
        assert(!kui_music_load_path("/Music/test.wav","Failed replacement",cancel));
        fake.short_read=false;fake.cancel_after_reads=0;fake.close_error=false;
        allocations_match();assert(state().loading_bytes==0);
        assert(!strcmp(state().title,"Repeated choice") && state().playing);
        assert(kui_music_select_cached(i%KUI_MUSIC_TRACKS));
    }
    unsigned lines=fake.log_lines;kui_music_log_stats("stress check");
    assert(fake.log_lines==lines+2u && strstr(fake.last_log,"Stop/mute retains cache"));
    kui_music_shutdown();
#ifdef KUI_MUSIC_ALLOC_TEST
    assert(heap.bytes==0 && heap.allocations==heap.frees);
#endif
    /* Large replacements exercise eviction and the exact staging-inclusive
     * limit, not just small files that would fit even with a leaked old slot. */
    reset(1);kui_music_init(log_line);kui_music_set_config(true,15);
    source(1,4u*1024u*1024u-44u);
    assert(kui_music_load_path("/Music/test.wav","Large first",cancel));
    for(unsigned i=0;i<16u;i++) {
        assert(kui_music_load_path("/Music/test.wav","Large replacement",cancel));
        allocations_match();assert(state().cache_bytes==4u*1024u*1024u);
        assert(state().peak_file_bytes==KUI_MUSIC_CACHE_MAX);
    }
    source(1,KUI_MUSIC_CUSTOM_MAX-44u);allocated=state().file_allocations;
    assert(!kui_music_load_path("/Music/test.wav","Over budget",cancel));
    allocations_match();assert(state().file_allocations==allocated && state().playing);
#ifdef KUI_MUSIC_ALLOC_TEST
    source(1,65536u);heap.fail_at=1;
    assert(!kui_music_load_path("/Music/test.wav","Allocation failure",cancel));
    assert(!heap.fail_at);allocations_match();
    assert(state().file_allocations==allocated && state().playing);
#endif
    kui_music_shutdown();
#ifdef KUI_MUSIC_ALLOC_TEST
    assert(heap.bytes==0 && heap.allocations==heap.frees);
#endif
}
static void replacement_interleave(void) {
#if defined(KUI_ON_CONSOLE) && defined(KUI_MUSIC_ALLOC_TEST)
    reset(1);kui_music_init(log_line);kui_music_set_config(true,15);
    assert(kui_music_load_path("/Music/test.wav","First custom song",cancel));
    interleave_resume=true;
    for(unsigned i=0;i<16u;i++) {
        source(1,70000u+i*2048u);
        assert(kui_music_load_path("/Music/test.wav","Replacement song",cancel));
        allocations_match();assert(state().playing && state().pcm_bytes==fake.size-44u);
    }
    /* Alternating Ogg and WAV replacements also move the shared decoder arena
     * in and out of use; each swap stays one audio-lock transaction. */
    for(unsigned i=0;i<16u;i++) {
        bool ogg=(i&1u)==0;
        if(ogg) {fake.ogg_file=ogg_mono;fake.size=sizeof(ogg_mono);}
        else {fake.ogg_file=NULL;source(1,70000u+i*2048u);}
        assert(kui_music_load_path(ogg?"/Music/test.ogg":"/Music/test.wav","Mixed replacement",cancel));
        allocations_match();assert(state().playing && state().compressed==ogg);
        assert(state().decoder_bytes==(ogg?ARENA_BYTES:0u));
    }
    interleave_resume=false;assert(resume_interleavings>32u);
    kui_music_shutdown();assert(heap.bytes==0 && heap.allocations==heap.frees);
#endif
}
int main(void) {
    for(unsigned channels=1;channels<=2;channels++) {
        reset(channels);kui_music_init(log_line);assert(!fake.connects && !fake.init);
        kui_music_set_config(true,30);assert(!fake.connects && !fake.init);
        assert(kui_music_load(0,cancel));
        assert(state().loaded && state().playing && state().current_index==0 && state().pcm_bytes==70000);
        assert(fake.disconnects==1 && fake.unmounts==1 && fake.closes==1 && fake.volume==76);
        unsigned reads=fake.reads;
        for(unsigned i=0;i<20;i++) kui_music_service();
        assert(fake.reads==reads && fake.connects==1);
        /* Background preload doesn't interrupt the old song; all five can be
         * selected during a rip without making even one filesystem call. */
        unsigned starts=fake.starts,destroy=fake.destroy;
        for(unsigned i=1;i<KUI_MUSIC_TRACKS;i++) assert(kui_music_cache_menu(i,cancel));
        assert(state().cached_mask==31u && state().current_index==0 && fake.starts==starts && fake.destroy==destroy);
        reads=fake.reads;unsigned connects=fake.connects;
        unsigned wanted=99;assert(kui_music_step_cached(-1,&wanted) && wanted==4 && state().current_index==4);
        assert(kui_music_step_cached(1,&wanted) && wanted==0);
        assert(kui_music_select_cached(3) && state().current_index==3);
        assert(fake.reads==reads && fake.connects==connects);
        kui_music_set_config(true,50);assert(fake.volume==127);
        kui_music_pause();assert(!state().playing && state().paused && !fake.active);
        unsigned polls=fake.polls;kui_music_service();assert(fake.polls==polls);
        kui_music_resume();assert(state().playing && fake.reads==reads && fake.connects==connects);
        kui_music_set_config(false,50);assert(!state().playing && state().loaded && !state().paused);
        kui_music_resume();assert(!state().playing);kui_music_set_config(true,500);kui_music_resume();assert(fake.volume==255);
        fake.poll_error=true;kui_music_service();assert(!state().playing && strstr(state().message,"playback error"));
        unsigned allocations=fake.alloc;
        for(unsigned i=0;i<10;i++) {kui_music_resume();kui_music_service();}
        assert(fake.alloc==allocations);
        fake.poll_error=false;assert(kui_music_select_cached(state().current_index));
        assert(state().playing && fake.alloc==allocations+1u);
        kui_music_shutdown();assert(!state().loaded && !fake.active && fake.shutdown==1);
    }
    for(unsigned fault=0;fault<8;fault++) {
        reset(1);kui_music_init(log_line);kui_music_set_config(true,15);
        assert(kui_music_load(0,cancel));unsigned starts=fake.starts,destroy=fake.destroy;
        if(fault==0) fake.cancel=true;
        if(fault==1) fake.cancel_after_reads=fake.reads+1;
        if(fault==2) fake.missing=true;
        if(fault==3) fake.short_read=true;
        if(fault==4) fake.advertised_size=KUI_MUSIC_FILE_MAX+1u;
        if(fault==5) fake.close_error=true;
        if(fault==6) fake.unmount_error=true;
        if(fault==7) fake.header[20]=3;
        assert(!kui_music_load(1,cancel));
        assert(state().loaded && state().playing && state().current_index==0 && fake.starts==starts && fake.destroy==destroy);
        unsigned connects=fake.connects;for(unsigned i=0;i<5;i++) kui_music_service();
        assert(fake.connects==connects);kui_music_shutdown();
    }
    reset(1);kui_music_init(log_line);kui_music_set_config(true,15);
    assert(kui_music_load(0,cancel));source(2,3u*1024u*1024u);
    assert(kui_music_load_path("/Music/test.wav","Long song",cancel));
    assert(state().playing && state().current_index==KUI_MUSIC_CUSTOM_INDEX && state().pcm_bytes==3u*1024u*1024u);
    assert(!strcmp(state().title,"Long song"));unsigned reads=fake.reads;
    for(unsigned i=0;i<20;i++) kui_music_service();
    assert(fake.reads==reads);
    /* 6 MiB plus retained 3 MiB cannot exceed the staging-inclusive budget. */
    source(1,KUI_MUSIC_CUSTOM_MAX-44u);assert(!kui_music_load_path("/Music/test.wav","Too much staging",cancel));
    assert(state().playing && !strcmp(state().title,"Long song") && state().cache_bytes<=KUI_MUSIC_CACHE_MAX);
    assert(kui_music_load(0,cancel)==false); /* fake source now exceeds bundled limit */
    kui_music_shutdown();
    reset(1);kui_music_init(log_line);kui_music_set_config(true,15);source(1,KUI_MUSIC_CUSTOM_MAX-44u);
    assert(kui_music_load_path("/Music/test.wav","Six MiB",cancel));assert(state().cache_bytes==KUI_MUSIC_CUSTOM_MAX);
    assert(!kui_music_select_cached(4));unsigned wanted=99;
    assert(!kui_music_step_cached(1,&wanted) && wanted==0);
    kui_music_shutdown();
    reset(1);kui_music_init(log_line);assert(!kui_music_load(KUI_MUSIC_TRACKS,cancel));
    assert(!kui_music_load_path("relative.wav","Bad",cancel));kui_music_shutdown();
    reset(1);kui_music_init(log_line);kui_music_set_config(true,15);fake.init_error=true;
    assert(kui_music_load(0,cancel));assert(!state().playing && fake.shutdown==1);
    for(unsigned i=0;i<10;i++) kui_music_resume();
    assert(fake.init==1);
    fake.init_error=false;kui_music_set_config(true,15);fake.alloc_error=true;kui_music_resume();
    assert(!state().playing);unsigned allocations=fake.alloc;fake.alloc_error=false;kui_music_resume();assert(fake.alloc==allocations);
    kui_music_set_config(true,15);kui_music_resume();assert(state().playing);kui_music_shutdown();
#ifdef KUI_ON_CONSOLE
    reset(1);thread_fail=true;kui_music_init(log_line);kui_music_set_config(true,15);
    assert(kui_music_load(0,cancel));assert(!state().playing && strstr(state().message,"Audio service unavailable"));
    assert(!fake.init && !thread_live);kui_music_shutdown();thread_fail=false;
#endif
    cache_churn();compressed_music();bundled_ogg();
    replacement_interleave();
    puts("PASS background music: independent polling, no playback I/O, preserved replacements, 1000 cached switches, 300 replacements/failures, exact 8MiB staging budget, allocation cleanup, replacement/resume interleaving, bundled Oggs sharing one decoder arena, 1.5 WAV fallback");return 0;
}
