/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_APPS_NET_H
#define KUI_TEST_APPS_NET_H
#include <stdint.h>
#include <sys/queue.h>
typedef struct knetif {
    LIST_ENTRY(knetif) if_list;
    const char *name,*descr;
    uint32_t flags;
    uint8_t ip_addr[4],netmask[4],gateway[4],dns[4];
} netif_t;
LIST_HEAD(netif_list,knetif);
#define NETIF_REGISTERED 1u
#define NETIF_DETECTED 2u
#define NETIF_INITIALIZED 4u
#define NETIF_RUNNING 8u
#define NETIF_NOETH UINT32_C(0x10000000)
struct netif_list *net_get_if_list(void);
int net_unreg_device(netif_t *interface);
#endif
