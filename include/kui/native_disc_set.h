/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NATIVE_DISC_SET_H
#define KUI_NATIVE_DISC_SET_H
#include "kui/native_session.h"
#include "kui/retail_image.h"

/* This optional callback is pinned by the stage to the active New low reader.
 * Calls occur only with IMASK15 on the protected worker stack. Success is 1,
 * busy is 0 (nothing changed), fault is -1; STATUS returns the flags below. */
enum kui_native_session_operation {
    KUI_NATIVE_SESSION_STATUS=0u,
    KUI_NATIVE_SESSION_DISC_OPEN=1u,
    KUI_NATIVE_SESSION_DISC_CLOSE=2u,
    KUI_NATIVE_SESSION_RETURN=3u,
    KUI_NATIVE_SESSION_BIND=4u /* Cold low entry returns its bounded descriptor address. */
};
enum kui_native_session_status {
    KUI_NATIVE_SESSION_READY=1u,
    KUI_NATIVE_SESSION_OPEN=2u,
    KUI_NATIVE_SESSION_EMPTY=3u,
    KUI_NATIVE_SESSION_OBSERVED=0x100u
};
struct kui_loader_sd;
typedef int (*kui_native_session_control)(uint32_t operation,
    const struct kui_retail_manifest *next,struct kui_loader_sd *return_card);

enum kui_native_disc_phase {
    KUI_NATIVE_DISC_IDLE=0u,KUI_NATIVE_DISC_OPENING,KUI_NATIVE_DISC_WAITING,
    KUI_NATIVE_DISC_CLOSING,KUI_NATIVE_DISC_FAULT
};
/* Fixed map storage is padded to 1024 bytes on both host and SH-4. The actual
 * 64-slot manifest occupies 1020 bytes on the console; no runtime decoder ever
 * writes the GUI's larger 256-slot structure into this bounded reservation. */
struct kui_native_disc_set {
    uint32_t count,selected,phase,visits;
    _Alignas(8) uint8_t maps[KUI_NATIVE_SESSION_DISCS_MAX][1024u];
};
/* Decoder objects must be built with KUI_RETAIL_MANIFEST_SLOTS=64. All maps
 * share card/partition/transport/reader and the selected map matches active.
 * Return-only descriptors produce an empty, valid disc set. */
bool kui_native_disc_set_prepare_wire(struct kui_native_disc_set *out,
    const void *wire,size_t bytes,const struct kui_retail_manifest *active);
const struct kui_retail_manifest *kui_native_disc_set_current(const struct kui_native_disc_set *);
/* A request starts one next-disc cycle; repeated held keys do not skip discs.
 * Visit is called by the normal controller completion route. It waits until
 * the reader is idle, opens the lid, and closes only after the guest observes
 * that condition and at least 30 controller completions have elapsed. No frame
 * buffer, saved-game state or game RAM is modified by this model. */
bool kui_native_disc_set_request_next(struct kui_native_disc_set *);
int kui_native_disc_set_visit(struct kui_native_disc_set *,kui_native_session_control);
#endif
