/* SPDX-License-Identifier: MIT */
/* The Dreamcast's side of the K-UI Wi-Fi link: eight socket slots on the
 * Wi-Fi board, used much like W5500 sockets, plus Wi-Fi control, name
 * lookup, the network clock and firmware updates. Portable C without
 * allocation; the bus does the actual SPI transfers. */
#ifndef KWHOST_H
#define KWHOST_H
#include "kwmsg.h"

#define KWH_RX 16384u
#define KWH_QUEUE 16384u
#define KWH_SCAN_MAX 40u

/* Slot events, collected until kwh_events reads them. */
#define KWH_EV_OPEN 0x01u        /* established, or UDP ready */
#define KWH_EV_PEER_CLOSED 0x02u /* the peer sent FIN; all its data is here */
#define KWH_EV_CLOSED 0x04u      /* closed; see kwh_error */
#define KWH_EV_DATA 0x08u

struct kwh_bus {
    void *ctx;
    /* One transfer: wait for the board to be ready (READY changed), then
     * clock `len` bytes out of `out` while filling `in`. False when the
     * board never became ready. */
    bool (*transfer)(void *ctx, const uint8_t *out, uint8_t *in, size_t len);
};
struct kwh_hello {
    uint16_t protocol, max_payload;
    uint8_t slots, chip, mac[6];
    char version[KWM_VERSION_MAX + 1];
};
struct kwh_wifi {
    uint8_t state, band, channel;
    int8_t rssi;
    uint8_t ip[4], mask[4], gateway[4], dns[4], bssid[6];
    uint8_t band_mode, reason, saved;
    char ssid[KWM_SSID_MAX + 1];
};
struct kwh_ap {
    char ssid[KWM_SSID_MAX + 1];
    uint8_t bssid[6], channel, auth;
    int8_t rssi;
};
struct kwh_slot {
    uint8_t kind, gen, state, err, events;
    uint8_t ip[4];
    uint16_t port, local_port;
    /* TCP: the byte stream. UDP: records of u16 length, ip[4], u16 port, data. */
    uint8_t rx[KWH_RX];
    size_t rx_head, rx_len;
    /* Bytes the board will take now; bytes read and not yet credited back. */
    uint32_t tx_credit, credit_back;
};
/* Each answer bumps its counter, so a caller can wait for a new one. */
struct kwh_counts {
    uint32_t hello, wifi, scan, join, dns, time, ota, reboot, lost;
};
struct kwh {
    const struct kwh_bus *bus;
    struct kwl link;
    uint16_t session;
    bool ready; /* the board has answered HELLO in this session */
    struct kwh_hello hello;
    struct kwh_wifi wifi;
    struct kwh_ap scan[KWH_SCAN_MAX];
    uint8_t scan_count, scan_status, join_status;
    uint8_t dns_tag, dns_status, dns_ip[4];
    uint8_t time_status;
    uint64_t time_ms;
    uint8_t ota_phase, ota_status;
    uint32_t ota_written;
    struct kwh_counts counts;
    struct kwh_slot slot[KWM_SLOTS];
    uint8_t queue[KWH_QUEUE];
    size_t queue_len;
    uint8_t out[KWL_FRAME_MAX], in[KWL_FRAME_MAX];
    uint32_t transfers, failures;
};

void kwh_init(struct kwh *h, const struct kwh_bus *bus);
/* Starts a session (any non-zero number; pick a new one per start) and
 * asks for HELLO; kwh_step until `ready`. */
void kwh_start(struct kwh *h, uint16_t session);
/* One transfer. False when the board did not become ready. If the board
 * lost the session (it restarted), every slot closes and a new session
 * starts by itself. */
bool kwh_step(struct kwh *h);
/* Nothing left to send or to acknowledge, and the board has nothing
 * waiting that it announced. */
bool kwh_idle(const struct kwh *h);

/* Requests. False when the outgoing queue is full (step, then retry). */
bool kwh_wifi_query(struct kwh *h);
bool kwh_wifi_scan(struct kwh *h);
/* band: KWM_BAND_*. The password is not kept here. */
bool kwh_wifi_join(struct kwh *h, const char *ssid, const char *password, bool save, uint8_t band);
bool kwh_wifi_leave(struct kwh *h, bool forget);
bool kwh_dns(struct kwh *h, uint8_t tag, const char *name);
bool kwh_time(struct kwh *h);
bool kwh_ota_begin(struct kwh *h, uint32_t size, const uint8_t sha256[32]);
/* At most KWM_BODY_MAX - 4 bytes per call, in order. */
bool kwh_ota_data(struct kwh *h, uint32_t offset, const void *data, size_t len);
bool kwh_ota_end(struct kwh *h);
bool kwh_reboot(struct kwh *h);

/* Sockets. Opening a slot replaces whatever it held. */
bool kwh_listen(struct kwh *h, unsigned s, uint16_t port, uint8_t flags);
bool kwh_connect(struct kwh *h, unsigned s, const uint8_t ip[4], uint16_t port, uint8_t flags);
bool kwh_udp(struct kwh *h, unsigned s, uint16_t port);
/* graceful: FIN after the data already sent (the state goes CLOSING, then
 * CLOSED); otherwise closed at once and anything unread is dropped. */
bool kwh_close(struct kwh *h, unsigned s, bool graceful);
uint8_t kwh_state(const struct kwh *h, unsigned s);
uint8_t kwh_error(const struct kwh *h, unsigned s);
uint8_t kwh_events(struct kwh *h, unsigned s);
bool kwh_peer(const struct kwh *h, unsigned s, uint8_t ip[4], uint16_t *port);
/* TCP: bytes that kwh_send takes now; UDP: the largest datagram. */
size_t kwh_room(const struct kwh *h, unsigned s);
size_t kwh_send(struct kwh *h, unsigned s, const void *data, size_t len);
bool kwh_sendto(struct kwh *h, unsigned s, const uint8_t ip[4], uint16_t port, const void *data, size_t len);
/* TCP: bytes waiting. */
size_t kwh_available(const struct kwh *h, unsigned s);
size_t kwh_recv(struct kwh *h, unsigned s, void *data, size_t len);
/* UDP: the next whole datagram (0 when none; one longer than `capacity`
 * is cut short). */
size_t kwh_recvfrom(struct kwh *h, unsigned s, uint8_t ip[4], uint16_t *port, void *data, size_t capacity);
#endif
