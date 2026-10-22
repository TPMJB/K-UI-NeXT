/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_SOUND_STREAM_H
#define KUI_TEST_SOUND_STREAM_H
#include <stddef.h>
#include <stdint.h>
typedef int snd_stream_hnd_t;
typedef void *(*snd_stream_callback_t)(snd_stream_hnd_t,int,int *);
#define SND_STREAM_INVALID -1
int snd_stream_init_ex(int channels,size_t buffer_size);
void snd_stream_shutdown(void);
snd_stream_hnd_t snd_stream_alloc(snd_stream_callback_t callback,int buffer_size);
void snd_stream_destroy(snd_stream_hnd_t hnd);
void snd_stream_queue_enable(snd_stream_hnd_t hnd);
void snd_stream_queue_disable(snd_stream_hnd_t hnd);
void snd_stream_queue_go(snd_stream_hnd_t hnd);
void snd_stream_start(snd_stream_hnd_t hnd,uint32_t rate,int stereo);
void snd_stream_volume(snd_stream_hnd_t hnd,int volume);
int snd_stream_poll(snd_stream_hnd_t hnd);
#endif
