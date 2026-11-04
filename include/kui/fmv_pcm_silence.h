/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_FMV_PCM_SILENCE_H
#define KUI_FMV_PCM_SILENCE_H
#include <stdint.h>
/* Only the caller's validated, exclusively owned native CDDA output span.
 * Retain source acquisition, staging, lock/unlock and producer accounting.
 * No game/movie buffer or sound register is accessed by this control. */
static inline void kui_fmv_pcm_silence(void *destination,uint32_t bytes) {
    volatile uint8_t *out=destination;
    if(!((uintptr_t)out&3u)) {
        volatile uint32_t *words=(volatile uint32_t *)(void *)out;
        while(bytes>=4u) {*words++=0u;bytes-=4u;}
        out=(volatile uint8_t *)(void *)words;
    }
    while(bytes--) *out++=0u;
}
#endif
