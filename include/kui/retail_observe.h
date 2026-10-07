/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_RETAIL_OBSERVE_H
#define KUI_RETAIL_OBSERVE_H
#include <stdint.h>
#include "kui/retail_image.h"
#define KUI_RETAIL_OBSERVE_GDI_CRC 0xe25531d1u
#define KUI_RETAIL_OBSERVE_SLOTS 64u
enum kui_retail_observe_group {KUI_OBSERVE_CPU,KUI_OBSERVE_TMU,KUI_OBSERVE_AICA};
/* CPU: caller SR,VBR,GBR,PR,SP,MMUCR. TMU: TSTR,FRQCR,
 * then TCOR,TCNT,TCR for channels0..2. AICA: master,ARM control,
 * G2 DMA enable/start mask,ARM IRQ enable/pending,channel hash,key bits0/1.
 * Sparse observations do not reserve resources or bound no-GD intervals. */
struct kui_retail_observe {
    uint32_t calls,play[2],first_play[2][3],last_play[2][3];
    /* Terminal iteration uses a real word-array member, rather than stepping
     * across the boundaries of adjacent arrays or scalar members. */
    union {struct {uint32_t cpu_first[6],cpu_last[6],cpu_changed;};uint32_t cpu_words[13];};
    union {struct {uint32_t tmu_first[11],tmu_last[11],tmu_changed;};uint32_t tmu_words[23];};
    union {struct {uint32_t audio_first[8],audio_last[8],audio_changed;};uint32_t audio_words[17];};
    uint32_t samples[3],skipped_audio,ever_key[2];
    union {struct {uint32_t dma_first,dma_last,dma_ever,dma_changed;};uint32_t dma_words[4];};
};
_Static_assert(sizeof(struct kui_retail_observe)<=384u,"bounded observation state");
void kui_retail_observe_play(struct kui_retail_observe *,uint32_t command,const uint32_t params[3]);
void kui_retail_observe_sample(struct kui_retail_observe *,enum kui_retail_observe_group,const uint32_t *);
int kui_retail_observe_admit(const struct kui_retail_manifest *);
int kui_retail_observe_native_ip(const uint8_t ip[64]);
#endif
