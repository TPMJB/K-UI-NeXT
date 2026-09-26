/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_TEST_W5500_MODEL_H
#define KUI_TEST_W5500_MODEL_H
#include "kui/w5500.h"

/* A W5500 for host tests: its SPI frames, registers, buffer rings and
 * socket states, as the datasheet describes them. TCP sockets are real
 * sockets on 127.0.0.1 (a W5500 port is the same host port), so a real
 * client can connect. MACRAW and UDP reach a small network inside the
 * model: a DHCP server at 10.0.0.1 that also answers ARP and ping for the
 * gateway. */
struct w5500_model_options {
    bool link_down;       /* PHYCFGR reports no cable link */
    bool absent;          /* no chip: every read returns 0xff */
    bool corrupt_reads;   /* flips a bit in every 97th byte read */
    bool dhcp_silent;     /* the DHCP server never answers */
    bool dhcp_nak;        /* the DHCP server refuses REQUESTs */
    bool conflict;        /* someone answers ARP for the leased address */
    uint32_t lease_seconds;
};
extern const struct kui_w5500_bus w5500_model_bus;
void w5500_model_start(const struct w5500_model_options *options);
void w5500_model_stop(void);
/* The address the model's DHCP server leases (10.0.0.2 in 10.0.0.0/24). */
extern const uint8_t w5500_model_lease[4];
/* Counters for assertions. */
struct w5500_model_counts {
    unsigned frames, commands, dhcp_discovers, dhcp_requests, udp_requests, arp_probes, pings;
    unsigned accepted, refused, resets;
    uint64_t sent_bytes, received_bytes;
};
extern struct w5500_model_counts w5500_model_counts;
/* Advances the clock the model reports (the host clock plus this). */
void w5500_model_advance(uint64_t ms);
/* The options in force, to change while running (a noisier bus, say). */
struct w5500_model_options *w5500_model_live(void);
#endif
