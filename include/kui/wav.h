/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_WAV_H
#define KUI_WAV_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
struct kui_wav {size_t offset,bytes;uint32_t rate;unsigned channels,frame_bytes;};
/* Strict RIFF/WAVE PCM16, mono/stereo, 8–44.1kHz. No codec or storage access. */
bool kui_wav_parse(const void *file,size_t size,struct kui_wav *out);
/* Exact read-at callback for bounded header-only scanning of a full-length
 * WAV. No PCM payload is copied while parsing. At most 1024 RIFF chunks. */
typedef bool (*kui_wav_read_fn)(void *ctx,uint64_t offset,void *out,size_t bytes);
bool kui_wav_read(kui_wav_read_fn read,void *ctx,uint64_t size,struct kui_wav *out);
struct kui_pcm_loop {const uint8_t *data;size_t bytes,position;unsigned frame_bytes;};
bool kui_pcm_loop_init(struct kui_pcm_loop *loop,const void *data,size_t bytes,unsigned frame_bytes);
/* Request and returned length are BYTES, including both channels. A partial
 * frame is rejected. Copies wrap without reading past the final PCM frame. */
size_t kui_pcm_loop_fill(struct kui_pcm_loop *loop,void *out,size_t bytes);
unsigned kui_music_aica_volume(unsigned percent);
#endif
