/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_VMU_MAPLE_OBSERVE_IO_H
#define KUI_VMU_MAPLE_OBSERVE_IO_H
#include <stdint.h>

/* Call expressions execute once and preserve their result. The sentinel
 * distinguishes an acquisition failure from an image read which ran. */
#define KUI_VMU_OBSERVE_IO_ACQUIRE(trace, call) \
    (((trace) = UINT32_MAX), (call))
#define KUI_VMU_OBSERVE_IO_IMAGE(trace, call) \
    ((trace) = (uint32_t)(call))

#endif
