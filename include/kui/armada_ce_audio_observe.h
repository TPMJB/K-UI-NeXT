/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_ARMADA_CE_AUDIO_OBSERVE_H
#define KUI_ARMADA_CE_AUDIO_OBSERVE_H

#include "kui/retail_gd.h"

/* This diagnostic is never an audio implementation. Only the separate
 * Armada CE SCI-background candidate may compile it into a resident. */
#ifndef KUI_ARMADA_CE_AUDIO_OBSERVE
#define KUI_ARMADA_CE_AUDIO_OBSERVE 0
#endif
#ifndef KUI_ARMADA_CE_RAM_BIND
#define KUI_ARMADA_CE_RAM_BIND 0
#endif
#ifndef KUI_ARMADA_CE_MAPLE_OBSERVE
#define KUI_ARMADA_CE_MAPLE_OBSERVE 0
#endif
#if KUI_ARMADA_CE_MAPLE_OBSERVE != 0 && KUI_ARMADA_CE_MAPLE_OBSERVE != 1
#error "KUI_ARMADA_CE_MAPLE_OBSERVE must be 0 or 1"
#endif
#if KUI_ARMADA_CE_MAPLE_OBSERVE && !KUI_ARMADA_CE_RAM_BIND
#error "CE Maple discovery requires its isolated admitted binding observer"
#endif
#if KUI_ARMADA_CE_RAM_BIND != 0 && KUI_ARMADA_CE_RAM_BIND != 1
#error "KUI_ARMADA_CE_RAM_BIND must be 0 or 1"
#endif
#if KUI_ARMADA_CE_RAM_BIND && !KUI_ARMADA_CE_AUDIO_OBSERVE
#error "Armada RAM binding belongs only to its admitted audio observer"
#endif
#if KUI_ARMADA_CE_AUDIO_OBSERVE != 0 && KUI_ARMADA_CE_AUDIO_OBSERVE != 1
#error "KUI_ARMADA_CE_AUDIO_OBSERVE must be 0 or 1"
#endif
#if KUI_ARMADA_CE_AUDIO_OBSERVE
#if !defined(KUI_RETAIL_CE) || !defined(KUI_RETAIL_ASYNC) || KUI_RETAIL_TRANSPORT != 1 || KUI_RETAIL_TOY_PILOT || KUI_RETAIL_MDK2_CDDA
#error "Armada audio observation requires the separate CE SCI background resident"
#endif

struct kui_armada_ce_audio_observation {
    uint32_t accepted[5]; /* PLAY, PLAY2, PAUSE, RELEASE, STOP. */
    uint32_t rejected, status_requests, drive_calls;
    uint32_t last_play_command, play_parameters[3]; /* Latest accepted PLAY/PLAY2. */
};
_Static_assert(sizeof(struct kui_armada_ce_audio_observation) == 48u,
    "Armada audio observation stays within the CE resident reservation");

static inline void kui_armada_ce_audio_count(uint32_t *count) {
    if(*count != UINT32_MAX) ++*count;
}

/* Call immediately after the GD dispatcher, before any later service work.
 * A positive REQUEST result owns the token and outputs are its validated
 * parameter copy. A rejected request may leave old outputs in the service;
 * never sample those outputs or any caller pointer after rejection. This
 * bookkeeping neither changes the GD service nor reads storage/hardware.
 * GD INIT/RESET intentionally do not reset these boot-lifetime counters. */
static inline void kui_armada_ce_audio_observe(
    struct kui_armada_ce_audio_observation *observation,
    uint32_t function, uint32_t request_command, int32_t result,
    const struct kui_retail_gd *service) {
    if(function == KUI_GD_DRIVE && result == 0)
        kui_armada_ce_audio_count(&observation->drive_calls);
    if(function != KUI_GD_REQUEST) return;
    if(request_command == KUI_RETAIL_GD_REQ_STAT && result > 0)
        kui_armada_ce_audio_count(&observation->status_requests);
    uint32_t index = request_command - KUI_RETAIL_GD_PLAY;
    if(index >= 4u) {
        if(request_command != KUI_GD_STOP) return;
        index = 4u;
    }
    if(result <= 0) {
        kui_armada_ce_audio_count(&observation->rejected);
        return;
    }
    kui_armada_ce_audio_count(&observation->accepted[index]);
    if(index < 2u) {
        observation->last_play_command = request_command;
        for(unsigned i = 0; i < 3u; ++i)
            observation->play_parameters[i] = service->outputs[i];
    }
}
#endif
#endif
