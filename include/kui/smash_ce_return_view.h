/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_SMASH_CE_RETURN_VIEW_H
#define KUI_SMASH_CE_RETURN_VIEW_H
#include <stdint.h>

/* Generated from the exact accepted helper and native reader ELFs. All eight
 * addresses are P2 aliases. The terminal caller must first authenticate the
 * complete immutable helper/reader, hold privileged IMASK15, and exclude every
 * old CPU callback and DMA writer of these private objects. This descriptor
 * is trusted launch-local metadata, never an address list supplied by CE.
 *
 * This view grants no retirement, bus lease, worker or write authority. The
 * predicate reads only fixed private state: it never follows a CE/guest
 * pointer, invokes a callback, touches MMIO, clears a fault or changes state. */
struct kui_smash_ce_return_view {
    uint32_t runtime,loan,continuation,fence,freeze,saved_region,region,manifest;
};
_Static_assert(sizeof(struct kui_smash_ce_return_view)==32u,"CE return view ABI");

/* 1: complete idle state; 0: valid but unfinished work; -1: invalid/faulted.
 * A never-started VMU runtime is accepted only when its complete private
 * object is zero and the separately initialized reader loan is healthy/idle.
 * DONE is busy: an unconsumed durable result is never discarded here.
 * No helper, reader, caller-owned memory or hardware is written. */
int kui_smash_ce_return_idle(const struct kui_smash_ce_return_view *);
#endif
