/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/menu_sound.h"
#include <dc/sound/sfxmgr.h>
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static unsigned initialized,allocated,freed,loaded,unloaded,played,stopped;
static unsigned fail_init,fail_channel,fail_load;
static bool reserved,live[3];
static int last_volume;static sfxhnd_t last_effect;
int snd_init(void) {++initialized;return fail_init?-1:0;}
int snd_sfx_chn_alloc(void) {
    if(fail_channel) return -1;
    assert(!reserved);reserved=true;++allocated;return 7; /* channels 0/1 belong to music */
}
void snd_sfx_chn_free(int channel) {assert(channel==7 && reserved);reserved=false;++freed;}
sfxhnd_t snd_sfx_load_raw_buf(char *buf,size_t length,uint32_t rate,uint16_t bits,uint16_t channels) {
    ++loaded;assert(buf && reserved && !(length%32) && rate==22050 && bits==16 && channels==1);
    assert(length==1216 || length==3520);const int16_t *pcm=(const int16_t *)buf;
    bool positive=false,negative=false;size_t samples=length/2;
    assert(pcm[0]==0 && pcm[samples-1]==0);
    for(size_t i=0;i<samples;i++) {
        assert(pcm[i]>=-5000 && pcm[i]<=5000);positive|=pcm[i]>0;negative|=pcm[i]<0;
    }
    assert(positive && negative);
    if(fail_load==loaded) return SFXHND_INVALID;
    unsigned handle=length==1216?1:2;assert(!live[handle]);live[handle]=true;return handle;
}
void snd_sfx_unload(sfxhnd_t effect) {assert(effect>0 && effect<3 && live[effect]);live[effect]=false;++unloaded;}
int snd_sfx_play_chn(int channel,sfxhnd_t effect,int volume,int pan) {
    assert(reserved && channel==7 && effect>0 && effect<3 && live[effect]);assert(volume>0 && volume<=128 && pan==128);
    ++played;last_effect=effect;last_volume=volume;return channel;
}
void snd_sfx_stop(int channel) {assert(reserved && channel==7);++stopped;}
static void reset(void) {
    kui_menu_sound_shutdown();assert(!reserved && !live[1] && !live[2]);
    initialized=allocated=freed=loaded=unloaded=played=stopped=0;
    fail_init=fail_channel=fail_load=0;kui_menu_sound_config(false,30);
}
int main(void) {
    reset();fail_init=1;assert(!kui_menu_sound_init() && !allocated && !loaded);reset();
    fail_channel=1;assert(!kui_menu_sound_init() && !allocated && !loaded);reset();
    fail_load=1;assert(!kui_menu_sound_init() && allocated==1 && freed==1 && !unloaded);reset();
    fail_load=2;assert(!kui_menu_sound_init() && allocated==1 && freed==1 && unloaded==1);reset();
    assert(kui_menu_sound_init());assert(initialized==1 && allocated==1 && loaded==2);
    assert(kui_menu_sound_init());assert(initialized==1 && allocated==1 && loaded==2);
    kui_menu_sound_play(KUI_MENU_SOUND_MOVE);assert(!played);
    kui_menu_sound_config(true,30);kui_menu_sound_play(KUI_MENU_SOUND_MOVE);assert(played==1 && last_effect==1 && last_volume==38);
    kui_menu_sound_play(KUI_MENU_SOUND_CONFIRM);assert(played==2 && last_effect==2);
    for(unsigned i=0;i<10000;i++) kui_menu_sound_play((enum kui_menu_sound)(i%2));
    assert(played==10002 && allocated==1 && loaded==2 && !unloaded);
    kui_menu_sound_config(true,200);kui_menu_sound_play(KUI_MENU_SOUND_MOVE);assert(last_volume==128);
    unsigned previous=played;kui_menu_sound_play((enum kui_menu_sound)-1);assert(played==previous);
    kui_menu_sound_config(true,0);kui_menu_sound_play(KUI_MENU_SOUND_CONFIRM);assert(played==previous);
    kui_menu_sound_config(false,100);kui_menu_sound_play(KUI_MENU_SOUND_CONFIRM);assert(played==previous);
    kui_menu_sound_shutdown();assert(!reserved && freed==1 && unloaded==2);
    kui_menu_sound_play(KUI_MENU_SOUND_CONFIRM);assert(played==previous);
    kui_menu_sound_shutdown();assert(freed==1 && unloaded==2);
    assert(kui_menu_sound_init());assert(allocated==2 && loaded==4);reset();
    puts("PASS bounded menu sound buffers, reserved channel, mute, failed initialization and repeated playback");return 0;
}
