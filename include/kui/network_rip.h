/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NETWORK_RIP_H
#define KUI_NETWORK_RIP_H
#include "kui/media.h"
#include "kui/net.h"

#define KUI_NETWORK_RIP_CONFIG_CAP 2048u
#define KUI_NETWORK_RIP_BLOCKS 32u
#define KUI_NETWORK_RIP_BYTES (512u * KUI_NETWORK_RIP_BLOCKS)
#define KUI_NETWORK_RIP_TIMEOUT_MS 15000u
struct kui_network_rip_config {
    uint8_t host[4];
    uint16_t port;
    uint8_t key[32], volume[16];
};
/* UTF-8/ASCII key=value lines: host, port, key (64 hex), volume (32 hex).
 * Blank lines and # comments are accepted. Unknown/duplicate/missing keys,
 * embedded NUL and invalid values fail without changing out. No defaults. */
bool kui_network_rip_config_parse(struct kui_network_rip_config *out, const char *text, size_t bytes);
bool kui_network_rip_config_valid(const struct kui_network_rip_config *config);
struct kui_network_rip_hooks {
    void *ctx;
    uint64_t (*now_ms)(void *);
    void (*pause)(void *, unsigned ms);
    bool (*cancel)(void *);
    /* Service the adapter and its address lease on every wait/transfer.
     * False permanently ends the session. Must not use this TCP socket. */
    bool (*poll)(void *);
};
/* One I/O worker, one socket, no automatic reconnect/retry after ambiguity.
 * Allocate this context (about 17 KiB), rather than putting it on the stack. */
struct kui_network_rip {
    struct kui_net_sockets net;
    struct kui_network_rip_hooks hooks;
    struct kui_network_rip_config config;
    uint64_t session, sequence, blocks;
    unsigned socket;
    bool connected, live;
    char problem[128];
    uint8_t buffer[KUI_NETWORK_RIP_BYTES];
};
/* Close after every attempted open, including failure, to wipe the key. */
bool kui_network_rip_open(struct kui_network_rip *client, const struct kui_net_sockets *net,
    unsigned socket, const struct kui_network_rip_config *config, const struct kui_network_rip_hooks *hooks);
/* out is cleared when the client has not authenticated. Never registers it. */
void kui_network_rip_media(struct kui_network_rip *client, struct kui_media_ops *out);
/* Attempts authenticated SYNC+CLOSE only on a healthy session, then always
 * closes the socket and wipes the shared key and transfer buffer. */
void kui_network_rip_close(struct kui_network_rip *client);
const char *kui_network_rip_problem(const struct kui_network_rip *client);
#endif
