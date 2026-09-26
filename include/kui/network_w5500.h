/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_NETWORK_W5500_H
#define KUI_NETWORK_W5500_H
#include "kui/apps.h"
#include "kui/network_probe.h"
#include "kui/w5500.h"

/* A W5500 wired to the SH-4's SCI port (a console modification): found
 * only when asked for, never at start-up. The SD card stays on SCIF.
 *
 * The console's link to the chip: the SPI bus, its speeds from the fastest
 * down, and a MAC address for this console (the W5500 has none). */
struct kui_w5500_port {
    const struct kui_w5500_bus *bus;
    unsigned levels;
    bool (*open)(unsigned level);
    void (*close)(void);
    const char *(*speed)(unsigned level); /* "12.5 MHz" */
    void (*mac)(uint8_t mac[6]);
};
/* Provided by the platform: src/dreamcast/w5500_sci.c on the console. */
const struct kui_w5500_port *kui_w5500_console_port(void);

/* Rounds of the wiring check at each speed. */
#define KUI_W5500_CHECK_ROUNDS 64u
/* The cable link: auto-negotiation normally takes two or three seconds. */
#define KUI_W5500_LINK_MS 10000u

struct kui_w5500_session {
    struct kui_w5500 chip;
    const struct kui_w5500_port *port;
    unsigned level;      /* the speed in use */
    uint8_t mac[6], version;
    struct kui_w5500_link link;
    /* The lease: address, netmask, gateway and DNS, and the server. */
    struct kui_network_config config;
    uint8_t server[4];
    uint32_t lease_seconds;
    uint64_t leased_ms;  /* when the lease was granted or last renewed */
    bool open, found, leased;
    char problem[96];
};
/* Opens the port from its fastest speed down until the chip resets, reads
 * version 0x04 and passes the wiring check, then sets the MAC address.
 * False with `problem` set when none works (the port is closed again). */
bool kui_w5500_session_find(struct kui_w5500_session *s, const struct kui_w5500_port *port, kui_log_fn log);
/* Waits up to KUI_W5500_LINK_MS for the cable link. */
bool kui_w5500_session_link(struct kui_w5500_session *s, kui_cancel_fn cancel);
/* DHCP over MACRAW on socket 0 with the network probe. lease_only: stop
 * once the address is leased and checked for conflicts; otherwise also
 * test the gateway with ARP and ping, as the network test does. The probe
 * is returned for reporting. Closes socket 0 afterwards. */
bool kui_w5500_session_dhcp(struct kui_w5500_session *s, bool lease_only, struct kui_network_probe *probe,
                            kui_log_fn log, kui_cancel_fn cancel, void (*stage)(enum kui_network_stage stage));
/* Renews the lease with a DHCP REQUEST over UDP on socket `udp` (closed,
 * with a buffer); used while TCP sockets stay open. Waits at most
 * limit_ms for the server. True with the lease extended; false with
 * `problem` set (*refused: the server said no). */
bool kui_w5500_session_renew(struct kui_w5500_session *s, unsigned udp, unsigned limit_ms, bool *refused,
                             kui_cancel_fn cancel);
/* Resets the chip (every socket closes) and closes the port. */
void kui_w5500_session_end(struct kui_w5500_session *s);
/* Seconds until the lease should be renewed (half of it) and until it
 * runs out, from `now_ms`; zero when already due. */
uint32_t kui_w5500_session_renew_in(const struct kui_w5500_session *s, uint64_t now_ms);
uint32_t kui_w5500_session_expires_in(const struct kui_w5500_session *s, uint64_t now_ms);

/* The Network app: inspection (A) and the connection test (X) when no
 * BBA or LAN adapter is present. False when no W5500 answers (out is then
 * untouched), so the caller reports that no adapter was found. */
bool kui_w5500_network_inspect(struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel);
bool kui_w5500_network_test(struct kui_app_status *out, kui_log_fn log, kui_cancel_fn cancel,
                            kui_app_progress_fn progress);
#endif
