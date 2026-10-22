/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NETWORK_TEST_H
#define KUI_NETWORK_TEST_H
#include "kui/apps.h"
enum kui_network_link { KUI_NETWORK_LINK_UNKNOWN, KUI_NETWORK_LINK_DOWN, KUI_NETWORK_LINK_UP };
struct kui_network_snapshot {
    char name[24],description[64];
    bool present,ethernet,initialized,running;
    enum kui_network_link link;
    uint8_t ip[4],netmask[4],gateway[4],dns[4];
};
/* Formats only the evidence supplied. Interface RUNNING, a saved IP, and a
 * physical carrier are not DHCP leases or Internet reachability checks. */
void kui_network_describe(struct kui_app_status *out,const struct kui_network_snapshot *snapshot);
#endif
