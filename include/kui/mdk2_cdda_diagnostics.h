/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MDK2_CDDA_DIAGNOSTICS_H
#define KUI_MDK2_CDDA_DIAGNOSTICS_H
#include <stddef.h>
#include <stdint.h>
#include "kui/toy_pilot.h"

/* MDK2-only extension. The shared API8 snapshot remains exactly448 bytes.
 * These scalars survive RESET and SDK shutdown; they never grant ownership. */
#define KUI_MDK2_CDDA_DIAGNOSTIC_MAGIC UINT32_C(0x4d444b31)
#define KUI_MDK2_CDDA_DIAGNOSTIC_VERSION 1u
enum kui_mdk2_cdda_play_refusal {
    KUI_MDK2_CDDA_PLAY_NOT_CONFIGURED=1u,
    KUI_MDK2_CDDA_PLAY_GENERATION,
    KUI_MDK2_CDDA_PLAY_DISABLED,
    KUI_MDK2_CDDA_PLAY_SDK_NOT_READY,
    KUI_MDK2_CDDA_PLAY_TRACK_RANGE,
    KUI_MDK2_CDDA_PLAY_REPEAT,
    KUI_MDK2_CDDA_PLAY_SOURCE
};
enum kui_mdk2_cdda_audio_state_flag {
    KUI_MDK2_CDDA_AUDIO_CONFIGURED=1u,
    KUI_MDK2_CDDA_AUDIO_SDK_READY=2u,
    KUI_MDK2_CDDA_AUDIO_DISABLED=4u,
    KUI_MDK2_CDDA_AUDIO_DRIVER_VERIFIED=8u
};
struct kui_mdk2_cdda_audio_diagnostics {
    uint32_t magic,version,play_requests,play_accepted;
    uint32_t play_rejected,first_reason,p0,p1;
    uint32_t p2,state_flags,verified_epoch,mailbox_generation;
    uint32_t driver_generation,host_installed,ac_ready,am_ready;
};
/* The GD adapter owns the two complete records and their word legend. */
struct kui_mdk2_cdda_gd_diagnostics { uint32_t words[16]; };
enum kui_mdk2_cdda_sound_probe_flag {
    KUI_MDK2_CDDA_PROBE_PCM=UINT32_C(1)<<0,
    KUI_MDK2_CDDA_PROBE_LEFT_READ=UINT32_C(1)<<1,
    KUI_MDK2_CDDA_PROBE_RIGHT_READ=UINT32_C(1)<<2,
    KUI_MDK2_CDDA_PROBE_LEFT_MATCH=UINT32_C(1)<<3,
    KUI_MDK2_CDDA_PROBE_RIGHT_MATCH=UINT32_C(1)<<4,
    KUI_MDK2_CDDA_PROBE_LEFT_CONTROL=UINT32_C(1)<<5,
    KUI_MDK2_CDDA_PROBE_RIGHT_CONTROL=UINT32_C(1)<<6,
    KUI_MDK2_CDDA_PROBE_LEFT_ROUTE=UINT32_C(1)<<7,
    KUI_MDK2_CDDA_PROBE_RIGHT_ROUTE=UINT32_C(1)<<8,
    KUI_MDK2_CDDA_PROBE_LEFT_ATTENUATION=UINT32_C(1)<<9,
    KUI_MDK2_CDDA_PROBE_RIGHT_ATTENUATION=UINT32_C(1)<<10,
    KUI_MDK2_CDDA_PROBE_MASTER=UINT32_C(1)<<11,
    KUI_MDK2_CDDA_PROBE_START=UINT32_C(1)<<12,
    KUI_MDK2_CDDA_PROBE_BUSY=UINT32_C(1)<<13,
    KUI_MDK2_CDDA_PROBE_FAILED=UINT32_C(1)<<14,
    KUI_MDK2_CDDA_PROBE_BUDGET=UINT32_C(1)<<15
};
struct kui_mdk2_cdda_sound_diagnostics {
    uint32_t source_lba,track,pcm_peak,nonzero_frames;
    uint32_t expected_left,readback_left,expected_right,readback_right;
    uint32_t control_left,control_right,route_left,route_right;
    uint32_t attenuation_left,attenuation_right,master,flags;
};
struct kui_mdk2_cdda_diagnostics {
    struct kui_mdk2_cdda_audio_diagnostics audio;
    struct kui_mdk2_cdda_gd_diagnostics gd_failure,gd_refusal;
    struct kui_mdk2_cdda_sound_diagnostics sound;
};
struct kui_mdk2_cdda_extended_snapshot {
    struct kui_toy_pilot_snapshot pilot;
    struct kui_mdk2_cdda_diagnostics diagnostics;
};
_Static_assert(sizeof(struct kui_mdk2_cdda_audio_diagnostics)==64u,
    "MDK2 audio evidence has16 words");
_Static_assert(sizeof(struct kui_mdk2_cdda_sound_diagnostics)==64u,
    "MDK2 sound evidence has16 words");
_Static_assert(sizeof(struct kui_mdk2_cdda_diagnostics)==256u,
    "MDK2 evidence has64 words");
_Static_assert(offsetof(struct kui_mdk2_cdda_extended_snapshot,diagnostics)==448u,
    "MDK2 extension follows unchanged API8 snapshot");
/* Writable only inside the serialized authored adapter. No game code calls
 * this accessor; the low terminal reads the extended snapshot as const. */
struct kui_mdk2_cdda_diagnostics *kui_mdk2_cdda_diagnostic_state(void);
#endif
