/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_DISC_IDENTITY_H
#define KUI_DISC_IDENTITY_H
#include <stdbool.h>
#include <stdint.h>

#define KUI_DISC_IDENTITY_TITLE_CAP 129u
#define KUI_DISC_IDENTITY_POLL_MS 500u

enum kui_disc_identity_state {
    KUI_DISC_IDENTITY_UNKNOWN, KUI_DISC_IDENTITY_OPEN,
    KUI_DISC_IDENTITY_EMPTY, KUI_DISC_IDENTITY_WAITING,
    KUI_DISC_IDENTITY_NON_GD, KUI_DISC_IDENTITY_PENDING,
    KUI_DISC_IDENTITY_READING, KUI_DISC_IDENTITY_READY,
    KUI_DISC_IDENTITY_ERROR, KUI_DISC_IDENTITY_RESET_REQUIRED
};
struct kui_disc_identity {
    enum kui_disc_identity_state state;
    char title[KUI_DISC_IDENTITY_TITLE_CAP];
    uint64_t next_poll_ms;
    int disc_type;
    bool observed, armed, needs_identification;
};
/* Status values are the pinned KOS cd_stat_t / cd_disc_types_t integers.
 * prepare/read_one are bounded calls through the existing exclusive adapter.
 * read_one writes exactly one 2352-byte raw sector from FAD 45150. No caller
 * buffer is ever passed to firmware by the console implementation. */
struct kui_disc_identity_ops {
    void *ctx;
    int (*status)(void *ctx, int *status, int *disc_type);
    bool (*prepare)(void *ctx);
    bool (*read_one)(void *ctx, uint8_t *raw);
    bool (*cancelled)(void *ctx);
};
void kui_disc_identity_init(struct kui_disc_identity *identity);
/* Only the single I/O worker calls these. Passing io_idle=false makes NO
 * firmware call. Cheap status checks are throttled; true requests one title
 * read, which the worker performs only after pausing any music/SD activity.
 * The UI reads a locked copy of this structure, never this live worker state. */
bool kui_disc_identity_poll(struct kui_disc_identity *identity,
    const struct kui_disc_identity_ops *ops, uint64_t now_ms, bool io_idle);
void kui_disc_identity_read(struct kui_disc_identity *identity,
    const struct kui_disc_identity_ops *ops);
/* Invalidates presentation after an explicit operation that may have changed
 * media while idle polling was suspended. It never resets the drive adapter.
 * The next stable GD status permits ONE new identification attempt. */
void kui_disc_identity_invalidate(struct kui_disc_identity *identity);
const char *kui_disc_identity_text(enum kui_disc_identity_state state);
/* Production callbacks use the pinned BIOS status syscall directly and the
 * existing guarded disc adapter. Host tests supply injected callbacks. */
const struct kui_disc_identity_ops *kui_disc_identity_console_ops(void);
#endif
