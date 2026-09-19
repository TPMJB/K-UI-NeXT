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

/* Which KOS transport the next kui_sd_connect() should ask for.
 *
 * KOS offers two ways to reach the same card. SD_IF_SCIF emulates SPI by
 * bit-banging the SCIF pins: it works on the common jj1odm-style adapter and
 * is what KOS's plain sd_init() hardcodes. SD_IF_SCI drives the SH4's serial
 * interface in synchronous mode and can move blocks by DMA, but needs an
 * adapter wired to the SCI pins, so it is not a safe default. check_crc
 * verifies the data-block CRC16 in software; note that KOS computes that CRC
 * on writes either way, so turning it off only affects reads.
 *
 * These start at KOS's own defaults, and a connect that has to fall back
 * rewrites them, so a failed experiment cannot persist into the next one. */
static sd_interface_t interface = SD_IF_SCIF;
static bool check_crc = true;

void kui_sd_set_params(unsigned use_sci, bool want_crc) {
    interface = use_sci ? SD_IF_SCI : SD_IF_SCIF;
    check_crc = want_crc;
}

/* What the last successful connect actually opened, after any fallback. */
unsigned kui_sd_active_sci(void) { return interface == SD_IF_SCI ? 1u : 0u; }

static const char *transport_name(sd_interface_t which) {
    return which == SD_IF_SCI ? "SCI (synchronous serial, DMA capable)"
                              : "SCIF (bit-banged SPI)";
}

bool kui_sd_connect(void) {
    sd_init_params_t params = {interface, check_crc};
    if(sd_init_ex(&params) != 0) {
        /* An adapter wired for SCIF simply will not answer on SCI. That is an
         * expected outcome of the experiment, not a fault: say so, drop back
         * to the transport KOS defaults to, and let the run continue. */
        if(interface == SD_IF_SCI) {
            kui_log("SD: SCI init failed (errno=%d); this adapter is probably "
                    "wired for SCIF. Falling back.", errno);
            interface = SD_IF_SCIF;
            params.interface = SD_IF_SCIF;
            if(sd_init_ex(&params) != 0) {
                kui_log("SD initialization failed: errno=%d", errno);
                return false;
            }
        } else {
            kui_log("SD initialization failed: errno=%d", errno);
            return false;
        }
    }
    /* Printed on every connect so a report always names the transport its
     * numbers were measured on, even if a fallback changed it mid-run. */
    kui_log("SD transport: %s, CRC check on reads %s",
        transport_name(interface), check_crc ? "on" : "off");
    struct kui_media_ops ops = {NULL, blocks, read_blocks, write_blocks, sync_card};
    kui_media_set(&ops);
    return true;
}
void kui_sd_disconnect(void) {
    kui_media_set(NULL);
    sd_shutdown();
}
