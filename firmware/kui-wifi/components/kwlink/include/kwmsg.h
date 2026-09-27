/* SPDX-License-Identifier: MIT */
/* Messages inside K-UI Wi-Fi link frames. Layouts are in PROTOCOL.md. */
#ifndef KWMSG_H
#define KWMSG_H
#include "kwlink.h"
#include <string.h>

#define KWM_HEADER 4u
#define KWM_BODY_MAX (KWL_PAYLOAD_MAX - KWM_HEADER)
#define KWM_SLOTS 8u
#define KWM_SSID_MAX 32u
#define KWM_PASSWORD_MAX 64u
#define KWM_VERSION_MAX 32u
#define KWM_NAME_MAX 253u
/* Credit a UDP datagram costs beyond its payload. */
#define KWM_UDP_OVERHEAD 8u

enum kwm_type {
    KWM_HELLO = 0x01,
    KWM_WIFI_STATUS_GET = 0x02,
    KWM_WIFI_SCAN = 0x03,
    KWM_WIFI_JOIN = 0x04,
    KWM_WIFI_LEAVE = 0x05,
    KWM_SOCK_OPEN = 0x10,
    KWM_SOCK_CLOSE = 0x11,
    KWM_SOCK_SEND = 0x12,
    KWM_SOCK_CREDIT = 0x13,
    KWM_DNS = 0x20,
    KWM_TIME = 0x21,
    KWM_OTA_BEGIN = 0x30,
    KWM_OTA_DATA = 0x31,
    KWM_OTA_END = 0x32,
    KWM_REBOOT = 0x3f,
    KWM_HELLO_R = 0x81,
    KWM_WIFI_STATUS = 0x82,
    KWM_WIFI_SCAN_R = 0x83,
    KWM_WIFI_JOIN_R = 0x84,
    KWM_SOCK_STATE = 0x90,
    KWM_SOCK_TXCREDIT = 0x91,
    KWM_SOCK_DATA = 0x92,
    KWM_DNS_R = 0xa0,
    KWM_TIME_R = 0xa1,
    KWM_OTA_R = 0xb0,
    KWM_REBOOT_R = 0xbf,
};

/* Wi-Fi states. */
enum { KWM_WIFI_IDLE, KWM_WIFI_CONNECTING, KWM_WIFI_ASSOCIATED, KWM_WIFI_ONLINE,
       KWM_WIFI_BAD_PASSWORD, KWM_WIFI_NOT_FOUND, KWM_WIFI_LOST };
/* Band modes in WIFI_JOIN and WIFI_STATUS. */
enum { KWM_BAND_KEEP, KWM_BAND_24, KWM_BAND_5, KWM_BAND_BOTH };
/* Socket kinds, flags, states (the W5500's numbers) and errors. */
enum { KWM_TCP_LISTEN = 1, KWM_TCP_CONNECT = 2, KWM_UDP = 3 };
#define KWM_NODELAY 0x01u
#define KWM_KEEPALIVE 0x02u
enum { KWM_CLOSED = 0x00, KWM_LISTEN = 0x14, KWM_CONNECTING = 0x15, KWM_ESTABLISHED = 0x17,
       KWM_CLOSING = 0x18, KWM_PEER_CLOSED = 0x1c, KWM_UDP_OPEN = 0x22 };
enum { KWM_E_NONE, KWM_E_REFUSED, KWM_E_TIMEOUT, KWM_E_RESET, KWM_E_UNREACHABLE, KWM_E_NOMEM,
       KWM_E_INUSE, KWM_E_OFFLINE, KWM_E_INVALID, KWM_E_OTHER };
/* OTA_R phases. */
enum { KWM_OTA_BEGIN_PHASE = 1, KWM_OTA_DATA_PHASE = 2, KWM_OTA_END_PHASE = 3 };

struct kwm {
    uint8_t type, slot;
    uint16_t len;
    const uint8_t *body;
};

/* The next message from [*p, end): false at the end or on a message that
 * runs past it (the rest of the payload is then skipped). */
static inline bool kwm_next(const uint8_t **p, const uint8_t *end, struct kwm *m) {
    if(!p || !*p || end - *p < (ptrdiff_t)KWM_HEADER) return false;
    const uint8_t *h = *p;
    uint16_t len = kwl_get16(h + 2);
    if(end - h - (ptrdiff_t)KWM_HEADER < (ptrdiff_t)len) return false;
    m->type = h[0];
    m->slot = h[1];
    m->len = len;
    m->body = h + KWM_HEADER;
    *p = h + KWM_HEADER + len;
    return true;
}
/* Appends a message with room for `len` body bytes (copied from `body`
 * when not NULL); returns the body pointer, or NULL when it does not fit. */
static inline uint8_t *kwm_put(uint8_t *out, size_t capacity, size_t *used, uint8_t type, uint8_t slot,
                               const void *body, size_t len) {
    if(len > KWM_BODY_MAX || *used > capacity || capacity - *used < KWM_HEADER + len) return NULL;
    uint8_t *h = out + *used;
    h[0] = type;
    h[1] = slot;
    kwl_put16(h + 2, (uint16_t)len);
    if(body && len) memcpy(h + KWM_HEADER, body, len);
    *used += KWM_HEADER + len;
    return h + KWM_HEADER;
}
#endif
