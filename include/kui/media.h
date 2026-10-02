/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_MEDIA_H
#define KUI_MEDIA_H
#include "kui/core.h"

struct kui_media_ops {
    void *ctx;
    uint64_t (*blocks)(void *);
    int (*read)(void *, uint32_t block, size_t count, uint8_t *data);
    int (*write)(void *, uint32_t block, size_t count, const uint8_t *data);
    int (*sync)(void *);
};
/* Set only while unmounted. One worker owns all filesystem/device calls. */
void kui_media_set(const struct kui_media_ops *ops);
/* CD bootstrap only, while unmounted: select an already validated partition
 * without weakening the normal runtime's single-volume policy. The copied
 * raw device needs only blocks/read. Writes are protected and sync is a no-op.
 * Bounds are checked here and again at initialization; filesystem validation
 * remains the caller's mount operation. Failure clears the previous device.
 * kui_media_set(NULL) releases this view before disconnect or another view. */
bool kui_media_boot_view(const struct kui_media_ops *raw,const struct kui_volume *view);
const struct kui_volume *kui_media_volume(void);
const char *kui_media_problem(void);

#endif
