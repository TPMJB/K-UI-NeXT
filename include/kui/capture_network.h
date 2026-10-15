/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_CAPTURE_NETWORK_H
#define KUI_CAPTURE_NETWORK_H
#include "kui/apps.h"
#include "kui/network_rip.h"

#define KUI_CAPTURE_NETWORK_CONFIG "0:/KUI/network-rip.cfg"
struct kui_capture_network;
/* Read only while the LOCAL card is mounted, before disconnecting it.
 * Strict bounded read; preserves out on failure. Never logs the key. */
bool kui_capture_network_config_load(struct kui_network_rip_config *out, char problem[128]);
/* One I/O worker owns this session. Before start, unmount and disconnect
 * the local card (including any SCI reservation). No local file accesses
 * are allowed until the remote filesystem is unmounted, end has returned,
 * and the local device is reconnected. The config must already be in RAM.
 * Successful start registers authenticated remote media; it does not mount.
 * Failure closes every adapter it opened and leaves media disconnected. */
struct kui_capture_network *kui_capture_network_start(const struct kui_net_ports *ports,
    const struct kui_network_rip_config *config, kui_log_fn log, kui_cancel_fn cancel,
    void (*stage)(const char *), char problem[128]);
/* Caller MUST unmount first. Clears media before closing and wiping the
 * socket/adapter. Never restores local media or writes any local settings.
 * True only after final SYNC/CLOSE acknowledgements and socket close (or
 * for NULL). On failure, logs the final problem before freeing the session;
 * callers must not report a complete capture when this returns false. */
bool kui_capture_network_end(struct kui_capture_network *session);
const char *kui_capture_network_problem(const struct kui_capture_network *session);
#endif
