/* SPDX-License-Identifier: GPL-3.0-only */
/* Original, integer-synthesized UI tones. KOS API semantics checked against
 * fcfa7d869471591ca1c777543261a7bfea7cb726 snd_sfxmgr.c/snd_iface.c:
 * snd_init is idempotent; channel allocation is IRQ-protected; streams reserve
 * their own channels. Own channel avoids stealing/stopping a music stream. */
#include "kui/menu_sound.h"
#include <dc/sound/sound.h>
#include <dc/sound/sfxmgr.h>
#include <stdint.h>

#define RATE 22050u
#define MOVE_SAMPLES 608u
#define CONFIRM_SAMPLES 1760u
static sfxhnd_t tones[2]={SFXHND_INVALID,SFXHND_INVALID};
static int channel=-1;
static bool enabled;
static unsigned volume=30;

static void synthesize(int16_t *out,unsigned samples,bool confirm) {
    uint32_t phase=0;
    for(unsigned i=0;i<samples;i++) {
        unsigned hz=confirm?(i<samples/2?660u:880u):1200u;
        unsigned attack=i<32?i:32;
        unsigned remain=samples-1-i;
        int32_t triangle=phase<32768u?(int32_t)(phase*2u)-32767:98303-(int32_t)(phase*2u);
        int32_t sample=triangle*5000/32768;
        sample=sample*(int32_t)attack/32;
        sample=sample*(int32_t)remain/(int32_t)samples;
        out[i]=(int16_t)sample;
        phase=(phase+(65536u*hz/RATE))&65535u;
    }
}
bool kui_menu_sound_init(void) {
    if(channel>=0) return true;
    if(snd_init()<0) return false;
    channel=snd_sfx_chn_alloc();
    if(channel<0) return false;
    _Alignas(32) int16_t samples[CONFIRM_SAMPLES];
    synthesize(samples,MOVE_SAMPLES,false);
    tones[0]=snd_sfx_load_raw_buf((char *)samples,MOVE_SAMPLES*sizeof(*samples),RATE,16,1);
    if(tones[0]==SFXHND_INVALID) {kui_menu_sound_shutdown();return false;}
    synthesize(samples,CONFIRM_SAMPLES,true);
    tones[1]=snd_sfx_load_raw_buf((char *)samples,sizeof(samples),RATE,16,1);
    if(tones[1]==SFXHND_INVALID) {kui_menu_sound_shutdown();return false;}
    return true;
}
void kui_menu_sound_config(bool value,unsigned percent) {
    enabled=value;volume=percent>100?100:percent;
    if((!enabled || !volume) && channel>=0) snd_sfx_stop(channel);
}
void kui_menu_sound_play(enum kui_menu_sound effect) {
    if(!enabled || !volume || channel<0 || (unsigned)effect>1u || tones[effect]==SFXHND_INVALID) return;
    /* One short voice, centered and deliberately quieter than full-scale.
     * Triggering a new effect cannot accumulate channels or allocate RAM. */
    snd_sfx_stop(channel);
    (void)snd_sfx_play_chn(channel,tones[effect],(int)(volume*128u/100u),128);
}
void kui_menu_sound_shutdown(void) {
    if(channel>=0) snd_sfx_stop(channel);
    for(unsigned i=0;i<2;i++) {
        if(tones[i]!=SFXHND_INVALID) snd_sfx_unload(tones[i]);
        tones[i]=SFXHND_INVALID;
    }
    if(channel>=0) snd_sfx_chn_free(channel);
    channel=-1;
}
