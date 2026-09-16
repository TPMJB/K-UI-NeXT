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
const struct kui_volume *kui_media_volume(void);
const char *kui_media_problem(void);

#endif
