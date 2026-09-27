/* SPDX-License-Identifier: MIT */
/* The Wi-Fi board's side of the K-UI link: messages from the Dreamcast in,
 * eight socket slots over BSD sockets, messages back out. Independent of
 * ESP-IDF so the host tests can run it against real sockets. One thread
 * calls everything except kwb_notify, which any task may call. */
#ifndef KUI_WIFI_BRIDGE_H
#define KUI_WIFI_BRIDGE_H
#include "bridge_platform.h"
#include "kwmsg.h"

#define KWB_REPLY 8192u
#define KWB_SCAN_MAX 40u
#define KWB_NOTE_WIFI 0x01u
#define KWB_NOTE_SCAN 0x02u
#define KWB_NOTE_DNS 0x04u

struct kwb_ring {
    uint8_t *buf;
    size_t cap, head, len;
};
struct kwb_slot {
    uint8_t kind, gen, flags, state, err, shown;
    int fd;
    uint16_t local_port, remote_port, keepalive;
    uint8_t remote_ip[4];
    /* State, error or peer changed since the last SOCK_STATE. */
    bool dirty;
    /* The connection has ended (FIN, reset or our own close); only the
     * data already received is left to hand over. */
    bool ended;
    /* The host asked for FIN after the queued data; FIN has gone. */
    bool closing, fin_sent;
    uint32_t deadline;
    struct kwb_ring rx, tx;
    /* Bytes the host can still take, and transmit credit not yet granted. */
    uint32_t host_credit, grant;
};
struct kwb_listener {
    uint16_t port;
    int fd;
};
struct kwb_stats {
    uint32_t accepted, refused, dropped_replies, dropped_sends, dropped_datagrams;
};
struct kwb {
    struct kwl link;
    const struct kwb_platform *pf;
    size_t buffer;
    struct kwb_slot slot[KWM_SLOTS];
    struct kwb_listener listener[KWM_SLOTS];
    uint8_t reply[KWB_REPLY];
    size_t reply_len;
    uint32_t notes;
    bool wifi_dirty, scan_busy, dns_busy, reboot_due;
    uint8_t dns_tag;
    uint32_t reboot_at;
    unsigned next_data;
    uint8_t scratch[KWM_BODY_MAX];
    struct kwb_stats stats;
};

/* `buffer`: bytes of receive and of transmit buffer per open slot. */
void kwb_init(struct kwb *b, const struct kwb_platform *pf, size_t buffer);
/* Our frame for the next transfer. */
size_t kwb_frame(struct kwb *b, uint8_t frame[KWL_FRAME_MAX]);
/* The host's bytes from the transfer just finished. */
void kwb_transfer(struct kwb *b, const uint8_t *in, size_t clocked);
/* Sockets, timers and notes; call often, including between transfers. */
void kwb_service(struct kwb *b);
/* From any task: Wi-Fi changed, a scan or a lookup finished. */
void kwb_notify(struct kwb *b, uint32_t notes);
/* The host's reset line: forget the session and close everything. */
void kwb_reset(struct kwb *b);
/* Closes everything and frees the slot buffers. */
void kwb_release(struct kwb *b);
#endif
