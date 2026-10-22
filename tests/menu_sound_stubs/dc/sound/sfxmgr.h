/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MENU_SOUND_TEST_SFX_H
#define KUI_MENU_SOUND_TEST_SFX_H
#include <stddef.h>
#include <stdint.h>
typedef uint32_t sfxhnd_t;
#define SFXHND_INVALID 0
sfxhnd_t snd_sfx_load_raw_buf(char *buf,size_t len,uint32_t rate,uint16_t bits,uint16_t channels);
void snd_sfx_unload(sfxhnd_t effect);
int snd_sfx_play_chn(int channel,sfxhnd_t effect,int volume,int pan);
void snd_sfx_stop(int channel);
int snd_sfx_chn_alloc(void);
void snd_sfx_chn_free(int channel);
#endif
