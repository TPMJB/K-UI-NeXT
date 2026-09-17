/* SPDX-License-Identifier: GPL-3.0-only */
#include "platform.h"
#include "kui/media.h"
#include <dc/sd.h>
#include <errno.h>

static uint64_t blocks(void *ctx) {
    (void)ctx;
    uint64_t bytes = sd_get_size();
    return bytes == UINT64_MAX ? 0 : bytes / 512;
}
static int read_blocks(void *ctx, uint32_t block, size_t count, uint8_t *data) {
    (void)ctx; return sd_read_blocks(block, count, data);
}
static int write_blocks(void *ctx, uint32_t block, size_t count, const uint8_t *data) {
    (void)ctx; return sd_write_blocks(block, count, data);
}
static int sync_card(void *ctx) {
    (void)ctx;
    uint8_t sector[512];
    /* The pinned sd_read_blocks sends a fresh command only after sd_wait_ready
     * (500 ms bound), then reads/checks its response. This waits out the busy
     * period of the previous write. KOS blockdev.flush is a no-op, so is not
     * used here. This is not a guarantee about an SD controller's power loss. */
    return sd_read_blocks(0, 1, sector);
}
bool kui_sd_connect(void) {
    /* Standard external serial adapter: SCIF bit-banging, CRC checks enabled.
     * The SCI interface and its DMA path are deliberately outside this probe. */
    if(sd_init() != 0) {
        kui_log("SD initialization failed: errno=%d", errno); return false;
    }
    struct kui_media_ops ops = {NULL, blocks, read_blocks, write_blocks, sync_card};
    kui_media_set(&ops);
    return true;
}
void kui_sd_disconnect(void) {
    kui_media_set(NULL);
    sd_shutdown();
}
