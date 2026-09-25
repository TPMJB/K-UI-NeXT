/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MUSIC_OGG_H
#define KUI_MUSIC_OGG_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* All codec state and temporary decode memory must fit this caller-owned arena.
 * The decoder never allocates beyond it or reads any filesystem. */
#define KUI_OGG_WORKSPACE_BYTES (384u*1024u)
struct kui_ogg {void *decoder;uint32_t rate,frames;unsigned channels;bool failed;};
bool kui_ogg_open(struct kui_ogg *out,const uint8_t *file,size_t bytes,
    void *workspace,size_t workspace_bytes,bool (*cancel)(void));
/* Rebuild a decoder for bytes that already passed kui_ogg_open unchanged.
 * Skips only the whole-file page scan; format and arena checks still apply. */
bool kui_ogg_reopen(struct kui_ogg *out,const uint8_t *file,size_t bytes,
    void *workspace,size_t workspace_bytes);
size_t kui_ogg_fill(struct kui_ogg *ogg,void *pcm,size_t bytes);
void kui_ogg_close(struct kui_ogg *ogg);
#endif
