/* SPDX-License-Identifier: BSD-3-Clause
 * Register contracts adapted from KallistiOS, pinned upstream
 * fcfa7d869471591ca1c777543261a7bfea7cb726:
 * kernel/arch/dreamcast/sound/arm/aica.c
 *   Copyright (C) 2000-2002 Megan Potter
 *   Copyright (C) 2024 Stefanos Kornilios Mitsis Poiitidis
 *   Copyright (C) 2026 Ruslan Rostovtsev
 * kernel/arch/dreamcast/hardware/spu.c
 *   Copyright (C) 2000, 2001 Megan Potter
 *   Copyright (C) 2023, 2024, 2026 Ruslan Rostovtsev
 * kernel/arch/dreamcast/hardware/g2bus.c and include/dc/g2bus.h
 *   Copyright (C) 2000-2002 Megan Potter
 *   Copyright (C) 2023 Andy Barajas
 *   Copyright (C) 2024 Ruslan Rostovtsev
 * kernel/arch/dreamcast/include/dc/fifo.h
 *   Copyright (C) 2023 Andy Barajas
 * K-UI adaptation Copyright (C) 2026 K-UI contributors.
 * See LICENSES/LICENSE.KOS for retained conditions and disclaimer.
 */
#include "cdda_aica.h"
#include <stdbool.h>
#include <stddef.h>

#define AICA UINT32_C(0xa0700000)
#define SOUND_RAM UINT32_C(0xa0800000)
#define FIFO UINT32_C(0xa05f688c)
#define TCNT1 UINT32_C(0xffd80018)
#define G2_DMA UINT32_C(0xa05f7800)
#define FIFO_MASK 0x31u /* AICA, G2 and SH-4 */
#define FIFO_POLLS 10000u
#define FIFO_TICKS 25000u /* 2ms at 12.5MHz; iteration limit covers stopped TMU */
#define PHASE_FRAMES 32u
#define KEY_ON 0x4000u
#define KEY_EXEC 0x8000u
#define LOOP 0x0200u
#define CONTROL (LOOP | (KUI_CDDA_AICA_LEFT_OFFSET >> 16))

#ifdef KUI_CDDA_AICA_TEST
extern uint32_t kui_cdda_aica_test_read(uint32_t address, unsigned width);
extern void kui_cdda_aica_test_write(uint32_t address, uint32_t value,
                                     unsigned width);
extern void kui_cdda_aica_test_delay(void);
#define read32(a) kui_cdda_aica_test_read((a), 4)
#define write32(a,v) kui_cdda_aica_test_write((a), (v), 4)
#define write16(a,v) kui_cdda_aica_test_write((a), (v), 2)
#define delay() kui_cdda_aica_test_delay()
#else
#define read32(a) (*(volatile uint32_t *)(uintptr_t)(a))
#define write32(a,v) (read32(a) = (uint32_t)(v))
#define write16(a,v) (*(volatile uint16_t *)(uintptr_t)(a) = (uint16_t)(v))
static void delay(void) {
    for(unsigned i = 0; i < 20u; ++i) __asm__ __volatile__("nop");
}
#endif

static bool ready, playing;
static uint32_t ticks(void) { return ~read32(TCNT1); }
static bool fifo_wait(void) {
    uint32_t start = ticks();
    for(unsigned i = 0; i < FIFO_POLLS; ++i) {
        if(!(read32(FIFO) & FIFO_MASK)) return true;
        if(ticks() - start >= FIFO_TICKS) return false;
    }
    return false;
}
static bool put(uint32_t offset, uint32_t value) {
    if(!fifo_wait()) return false;
    write32(AICA + offset, value);
    return fifo_wait();
}
static bool get(uint32_t offset, uint32_t *value) {
    if(!fifo_wait()) return false;
    *value = read32(AICA + offset);
    return true;
}
static bool channel_put(unsigned channel, uint32_t offset, uint32_t value) {
    return put(channel * 0x80u + offset, value);
}
/* Cleanup is also bounded. A broken/stuck G2 bus cannot promise an audible
 * stop; never issue unsafe writes merely to claim a successful mute. */
static enum kui_cdda_aica_result stop_owned(void) {
    bool ok = put(0x2800u, 0); /* retail 2MB, stereo, master mute */
    bool left = channel_put(0, 0, CONTROL | KEY_EXEC);
    bool right = channel_put(1, 0, CONTROL | KEY_EXEC);
    playing = false;
    if(!ok || !left || !right) {
        ready = false;
        return KUI_CDDA_AICA_TIMEOUT;
    }
    return KUI_CDDA_AICA_OK;
}
static enum kui_cdda_aica_result fault(enum kui_cdda_aica_result reason) {
    (void)stop_owned();
    if(reason == KUI_CDDA_AICA_TIMEOUT) ready = false;
    return reason;
}
enum kui_cdda_aica_result kui_cdda_aica_stop(void) {
    if(!ready) return KUI_CDDA_AICA_NOT_READY;
    return stop_owned();
}

enum kui_cdda_aica_result kui_cdda_aica_init(void) {
    ready = false; playing = false;
    /* Exclusive standalone ownership; suspend G2 DMA before any G2 PIO.
     * The SCI source DMA is SH-4 DMAC, independent of this bus. */
    for(unsigned i = 0; i < 4u; ++i)
        write32(G2_DMA + i * 0x20u + 0x1cu, 1);
    for(unsigned i = 0; i < 4u; ++i)
        if(read32(G2_DMA + i * 0x20u + 0x18u) & 1u)
            return KUI_CDDA_AICA_NOT_READY;
    uint32_t arm;
    if(!get(0x2c00u, &arm) || !put(0x2c00u, arm | 1u) ||
       !put(0x2800u, 0) || !put(0x289cu, 0) || !put(0x28b4u, 0) ||
       !put(0x28a4u, 0x7ffu) || !put(0x28bcu, 0x7ffu))
        return fault(KUI_CDDA_AICA_TIMEOUT);
    /* Only init touches all 64 channels. None can retain KEYONB when the
     * two owned channels later share a global key-execute commit. */
    for(unsigned c = 0; c < 64u; ++c) {
        if(!channel_put(c, 0, 0)) return fault(KUI_CDDA_AICA_TIMEOUT);
        for(unsigned offset = 4u; offset < 0x80u; offset += 4u)
            if(!channel_put(c, offset, 0)) return fault(KUI_CDDA_AICA_TIMEOUT);
    }
    if(!channel_put(0, 0, KEY_EXEC)) return fault(KUI_CDDA_AICA_TIMEOUT);
    for(unsigned c = 0; c < 2u; ++c) {
        uint32_t base = c ? KUI_CDDA_AICA_RIGHT_OFFSET : KUI_CDDA_AICA_LEFT_OFFSET;
        if(!channel_put(c, 0, LOOP | (base >> 16)) ||
           !channel_put(c, 4, base & 0xffffu) ||
           !channel_put(c, 8, 0) ||
           !channel_put(c, 12, KUI_CDDA_AICA_RING_FRAMES) ||
           !channel_put(c, 16, 0x1fu) || /* fastest attack, no envelope */
           !channel_put(c, 20, 0x1fu) ||
           !channel_put(c, 24, 0) || /* PCM16 44100Hz exponent/mantissa */
           !channel_put(c, 36, c ? 0x0f0fu : 0x0f1fu) ||
           !channel_put(c, 40, 0x24u)) /* LPF off, attenuation=0 */
            return fault(KUI_CDDA_AICA_TIMEOUT);
    }
    ready = true;
    return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_start(void) {
    if(!ready || playing) return KUI_CDDA_AICA_NOT_READY;
    /* KYONB stages both channels; KYONEX is a shared execute operation. */
    if(!channel_put(0, 0, CONTROL | KEY_ON) ||
       !channel_put(1, 0, CONTROL | KEY_ON) ||
       !put(0x2800u, 0x0fu) ||
       !channel_put(0, 0, CONTROL | KEY_ON | KEY_EXEC))
        return fault(KUI_CDDA_AICA_TIMEOUT);
    playing = true;
    return KUI_CDDA_AICA_OK;
}

static enum kui_cdda_aica_result positions(uint32_t *left, uint32_t *right) {
    /* 0x280c MSLC lives in bits 8..13 (byte 0x280d). Other monitor fields
     * are irrelevant under exclusive ownership. KOS uses 20 nop settling. */
    uint32_t a, b;
    if(!put(0x280cu, 0)) return KUI_CDDA_AICA_TIMEOUT;
    delay();
    if(!get(0x2814u, &a) || !put(0x280cu, 1u << 8))
        return KUI_CDDA_AICA_TIMEOUT;
    delay();
    if(!get(0x2814u, &b)) return KUI_CDDA_AICA_TIMEOUT;
    a &= 0xffffu; b &= 0xffffu;
    if(a >= KUI_CDDA_AICA_RING_FRAMES || b >= KUI_CDDA_AICA_RING_FRAMES)
        return KUI_CDDA_AICA_PHASE;
    uint32_t distance = (a - b) & (KUI_CDDA_AICA_RING_FRAMES - 1u);
    if(distance > PHASE_FRAMES &&
       distance < KUI_CDDA_AICA_RING_FRAMES - PHASE_FRAMES)
        return KUI_CDDA_AICA_PHASE;
    *left = a; *right = b;
    return KUI_CDDA_AICA_OK;
}
enum kui_cdda_aica_result kui_cdda_aica_position(uint32_t *frame) {
    if(!frame) return KUI_CDDA_AICA_ARGUMENT;
    if(!ready) return KUI_CDDA_AICA_NOT_READY;
    uint32_t left, right;
    enum kui_cdda_aica_result result = positions(&left, &right);
    if(result != KUI_CDDA_AICA_OK) return fault(result);
    *frame = left;
    return KUI_CDDA_AICA_OK;
}
static enum kui_cdda_aica_result writable(unsigned half) {
    if(!playing) return KUI_CDDA_AICA_OK;
    uint32_t left, right;
    enum kui_cdda_aica_result result = positions(&left, &right);
    if(result != KUI_CDDA_AICA_OK) return result;
    if(left / KUI_CDDA_AICA_HALF_FRAMES == half ||
       right / KUI_CDDA_AICA_HALF_FRAMES == half ||
       left % KUI_CDDA_AICA_HALF_FRAMES >= KUI_CDDA_AICA_HALF_FRAMES - PHASE_FRAMES ||
       right % KUI_CDDA_AICA_HALF_FRAMES >= KUI_CDDA_AICA_HALF_FRAMES - PHASE_FRAMES)
        return KUI_CDDA_AICA_ACTIVE_HALF;
    return KUI_CDDA_AICA_OK;
}

enum kui_cdda_aica_result kui_cdda_aica_write_samples(
    unsigned half, unsigned frame_offset, const int16_t *left,
    const int16_t *right, unsigned count) {
    if(half > 1u || frame_offset >= KUI_CDDA_AICA_HALF_FRAMES || !left || !right ||
       !count || count > KUI_CDDA_AICA_WRITE_FRAMES ||
       count > KUI_CDDA_AICA_HALF_FRAMES - frame_offset)
        return KUI_CDDA_AICA_ARGUMENT;
    if(!ready) return KUI_CDDA_AICA_NOT_READY;
    uint32_t offset = (half * KUI_CDDA_AICA_HALF_FRAMES + frame_offset) * 2u;
    for(unsigned done = 0; done < count;) {
        /* Drain FIFO before taking a fresh playback observation. No bounded
         * wait can sit between that observation and the <=32-byte burst. */
        if(!fifo_wait()) return fault(KUI_CDDA_AICA_TIMEOUT);
        enum kui_cdda_aica_result result = writable(half);
        if(result != KUI_CDDA_AICA_OK) return fault(result);
        unsigned n = count - done > 8u ? 8u : count - done;
        /* If the start sample is odd, use 16-bit writes to avoid touching
         * the adjacent sample. 2 stereo writes/frame still fit the FIFO. */
        if((offset & 3u) || (n & 1u)) {
            for(unsigned i = 0; i < n; ++i) {
                write16(SOUND_RAM + KUI_CDDA_AICA_LEFT_OFFSET + offset + i * 2u,
                        (uint16_t)left[done + i]);
                write16(SOUND_RAM + KUI_CDDA_AICA_RIGHT_OFFSET + offset + i * 2u,
                        (uint16_t)right[done + i]);
            }
        } else {
            for(unsigned i = 0; i < n; i += 2u) {
                uint32_t l = (uint16_t)left[done + i] |
                    ((uint32_t)(uint16_t)left[done + i + 1u] << 16);
                uint32_t r = (uint16_t)right[done + i] |
                    ((uint32_t)(uint16_t)right[done + i + 1u] << 16);
                write32(SOUND_RAM + KUI_CDDA_AICA_LEFT_OFFSET + offset + i * 2u, l);
                write32(SOUND_RAM + KUI_CDDA_AICA_RIGHT_OFFSET + offset + i * 2u, r);
            }
        }
        /* Drain before monitor selector writes, then recheck both channels. */
        if(!fifo_wait()) return fault(KUI_CDDA_AICA_TIMEOUT);
        result = writable(half);
        if(result != KUI_CDDA_AICA_OK) return fault(result);
        done += n; offset += n * 2u;
    }
    return KUI_CDDA_AICA_OK;
}
