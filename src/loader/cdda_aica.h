/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CDDA_AICA_H
#define KUI_CDDA_AICA_H

#include <stdint.h>

#define KUI_CDDA_AICA_RING_FRAMES 16384u
#define KUI_CDDA_AICA_HALF_FRAMES 8192u
#define KUI_CDDA_AICA_WRITE_FRAMES 128u
#define KUI_CDDA_AICA_LEFT_OFFSET UINT32_C(0x100000)
#define KUI_CDDA_AICA_RIGHT_OFFSET UINT32_C(0x108000)

enum kui_cdda_aica_result {
    KUI_CDDA_AICA_OK = 0,
    KUI_CDDA_AICA_ARGUMENT,
    KUI_CDDA_AICA_NOT_READY,
    KUI_CDDA_AICA_TIMEOUT,
    KUI_CDDA_AICA_PHASE,
    KUI_CDDA_AICA_ACTIVE_HALF
};

/* Standalone harness only, after KOS teardown with SH-4 interrupts masked.
 * Takes exclusive ownership of the AICA ARM, every sound channel, mixer and
 * the two PCM rings. The caller owns/runs TMU1 as a free-running counter at
 * the reference in kui/cdda_clock.h and ensures all G2 DMA channels are idle.
 * The ARM stays reset.
 * This is not a retail game sound coexistence contract. */
enum kui_cdda_aica_result kui_cdda_aica_init(void);
/* The caller must fill both rings before start; no fill tracking is done here.
 * Both channel key-on bits are committed by one shared KYONEX write. Each
 * start is a new key-on at ring frame zero; it is not a hardware resume. */
enum kui_cdda_aica_result kui_cdda_aica_start(void);
enum kui_cdda_aica_result kui_cdda_aica_stop(void);
/* Returns the left channel frame, after checking both hardware positions.
 * Sequential observations can differ by <=32 frames. Values are 0..16383.
 * A phase/range fault stops/mutes playback, as does a bounded bus timeout
 * when the bus permits cleanup. Output is unchanged on failure. Capture a
 * checked played cursor before stop; stopped channel positions are not a
 * reliable pause/status cursor. */
enum kui_cdda_aica_result kui_cdda_aica_position(uint32_t *frame);
/* Planar little-endian signed PCM16. Count 1..128; range must stay inside one
 * half. During playback every <=32-byte stereo burst checks both positions
 * before and after writing, and refuses either active half or its final
 * 32-frame approach. Faults stop/mute the owned channels. Partial writes on
 * error are possible and must be discarded. Caller must also use monotonic
 * elapsed time to detect a missed full lap; positions alone cannot do this. */
enum kui_cdda_aica_result kui_cdda_aica_write_samples(
    unsigned half, unsigned frame_offset, const int16_t *left,
    const int16_t *right, unsigned count);

#endif
