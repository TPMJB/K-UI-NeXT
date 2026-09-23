/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NETWORK_PROBE_H
#define KUI_NETWORK_PROBE_H
#include "kui/apps.h"
#include <stddef.h>
#define KUI_NETWORK_FRAME_MAX 1536u
struct kui_network_config { uint8_t ip[4],mask[4],gateway[4],dns[4]; };
enum kui_network_stage {KUI_NET_DISCOVER,KUI_NET_REQUEST,KUI_NET_CONFLICT,KUI_NET_ARP,KUI_NET_ECHO,KUI_NET_DONE,KUI_NET_FAILED};
struct kui_network_probe {
    enum kui_network_stage stage;
    struct kui_network_config config;
    uint8_t mac[6],server[4],peer_mac[6];
    uint32_t transaction;
    uint64_t start_ms,stage_ms,last_send_ms,echo_ms;
    unsigned sends,received,ignored;
    bool dhcp,leased,arp_reply,echo_reply;
    char failure[80];
};
bool kui_network_config_valid(const struct kui_network_config *config);
bool kui_network_parse_ipv4(const char *text,uint8_t out[4]);
void kui_network_probe_begin(struct kui_network_probe *p,const uint8_t mac[6],uint32_t xid,
    const struct kui_network_config *config,uint64_t now_ms);
/* Receive is called only by the worker, never directly from an interrupt. */
void kui_network_probe_receive(struct kui_network_probe *p,const uint8_t *frame,size_t size,uint64_t now_ms);
/* Returns a complete outbound frame, or zero. Updates timeout/completion state. */
size_t kui_network_probe_step(struct kui_network_probe *p,uint8_t frame[KUI_NETWORK_FRAME_MAX],uint64_t now_ms);
/* Owns a temporary Ethernet session. NULL requests DHCP; a supplied validated
 * static config is used only during the test. No flash or saved config writes.
 * Refuses an already running interface instead of interrupting another client. */
void kui_network_connect_run(const struct kui_network_config *config,struct kui_app_status *out,
    kui_log_fn log,kui_cancel_fn cancel,kui_app_progress_fn progress);
#endif
