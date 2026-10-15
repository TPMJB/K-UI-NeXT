/* SPDX-License-Identifier: GPL-3.0-only */
/* The controller publishes zero until native PLAY succeeds. The isolated
 * WWP link resolves this word; a DRIVE poll never enters controller code. */
extern volatile uint32_t kui_wwp_cdda_drive_status;
static __attribute__((noinline,noclone)) uint32_t wwp_ce_stream_drive_status(void) {
    return kui_wwp_cdda_drive_status;
}
