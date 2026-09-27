/* SPDX-License-Identifier: MIT */
/* The Dreamcast's side of the K-UI Wi-Fi link. See kwhost.h and PROTOCOL.md. */
#include "kwhost.h"
#include <string.h>

static size_t smaller(size_t a, size_t b) { return a < b ? a : b; }
static uint8_t slot_byte(const struct kwh_slot *s, unsigned i) { return (uint8_t)(i | (unsigned)s->gen << 3); }
static bool streaming(uint8_t state) { return state == KWM_ESTABLISHED || state == KWM_PEER_CLOSED; }

/* The outgoing queue: whole messages, sent in order as frames allow. */
static uint8_t *queue(struct kwh *h, uint8_t type, uint8_t slot, const void *body, size_t len) {
    return kwm_put(h->queue, sizeof h->queue, &h->queue_len, type, slot, body, len);
}
static size_t queue_room(const struct kwh *h) {
    size_t room = sizeof h->queue - h->queue_len;
    return room > KWM_HEADER ? room - KWM_HEADER : 0;
}
static size_t fill(void *ctx, uint8_t *p, size_t capacity) {
    struct kwh *h = ctx;
    size_t used = 0;
    while(used < h->queue_len) {
        size_t len = KWM_HEADER + kwl_get16(h->queue + used + 2);
        if(used + len > capacity) break;
        used += len;
    }
    memcpy(p, h->queue, used);
    memmove(h->queue, h->queue + used, h->queue_len - used);
    h->queue_len -= used;
    return used;
}

/* Received TCP bytes and UDP records. */
static bool rx_put(struct kwh_slot *s, const uint8_t *data, size_t len) {
    if(KWH_RX - s->rx_len < len) return false;
    size_t tail = (s->rx_head + s->rx_len) % KWH_RX, first = smaller(len, KWH_RX - tail);
    memcpy(s->rx + tail, data, first);
    memcpy(s->rx, data + first, len - first);
    s->rx_len += len;
    return true;
}
static void rx_take(struct kwh_slot *s, uint8_t *out, size_t len) {
    size_t first = smaller(len, KWH_RX - s->rx_head);
    if(out) {
        memcpy(out, s->rx + s->rx_head, first);
        memcpy(out + first, s->rx, len - first);
    }
    s->rx_head = (s->rx_head + len) % KWH_RX;
    s->rx_len -= len;
    if(!s->rx_len) s->rx_head = 0;
}
static void rx_peek(const struct kwh_slot *s, uint8_t *out, size_t len) {
    for(size_t i = 0; i < len; ++i) out[i] = s->rx[(s->rx_head + i) % KWH_RX];
}

static void slot_closed(struct kwh_slot *s, uint8_t err) {
    if(s->state != KWM_CLOSED) s->events |= KWH_EV_CLOSED;
    s->state = KWM_CLOSED;
    s->err = err;
    s->tx_credit = 0;
}
static void begin(struct kwh *h, uint16_t session) {
    h->session = session ? session : 1u;
    kwl_host_sync(&h->link, h->session);
    h->ready = false;
    h->queue_len = 0;
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwh_slot *s = &h->slot[i];
        slot_closed(s, KWM_E_RESET);
        s->rx_head = s->rx_len = 0;
        s->credit_back = 0;
    }
    uint8_t body[4] = {0};
    kwl_put16(body, KWL_PROTOCOL);
    queue(h, KWM_HELLO, 0, body, sizeof body);
}
void kwh_init(struct kwh *h, const struct kwh_bus *bus) {
    memset(h, 0, sizeof *h);
    h->bus = bus;
    kwl_init(&h->link, KWL_HOST);
}
void kwh_start(struct kwh *h, uint16_t session) {
    begin(h, session);
    for(unsigned i = 0; i < KWM_SLOTS; ++i) h->slot[i].events = 0;
}

/* Messages from the board. */
static void copy_text(char *out, size_t cap, const uint8_t *text, size_t len) {
    len = smaller(len, cap - 1u);
    memcpy(out, text, len);
    out[len] = 0;
}
static void hello_r(struct kwh *h, const struct kwm *m) {
    if(m->len < 15 || m->len < 15u + m->body[14]) return;
    const uint8_t *p = m->body;
    h->hello.protocol = kwl_get16(p);
    h->hello.slots = p[2];
    h->hello.chip = p[3];
    h->hello.max_payload = kwl_get16(p + 4);
    memcpy(h->hello.mac, p + 8, 6);
    copy_text(h->hello.version, sizeof h->hello.version, p + 15, p[14]);
    h->ready = true;
    ++h->counts.hello;
}
static void wifi_r(struct kwh *h, const struct kwm *m) {
    if(m->len < 30 || m->len < 30u + m->body[29]) return;
    const uint8_t *p = m->body;
    struct kwh_wifi *w = &h->wifi;
    w->state = p[0];
    w->band = p[1];
    w->channel = p[2];
    w->rssi = (int8_t)p[3];
    memcpy(w->ip, p + 4, 4);
    memcpy(w->mask, p + 8, 4);
    memcpy(w->gateway, p + 12, 4);
    memcpy(w->dns, p + 16, 4);
    memcpy(w->bssid, p + 20, 6);
    w->band_mode = p[26];
    w->reason = p[27];
    w->saved = p[28];
    copy_text(w->ssid, sizeof w->ssid, p + 30, p[29]);
    ++h->counts.wifi;
}
static void scan_r(struct kwh *h, const struct kwm *m) {
    if(m->len < 2) return;
    h->scan_status = m->body[0];
    h->scan_count = 0;
    size_t at = 2;
    for(unsigned i = 0; i < m->body[1] && h->scan_count < KWH_SCAN_MAX; ++i) {
        if(at + 10u > m->len || at + 10u + m->body[at + 3] > m->len) break;
        struct kwh_ap *ap = &h->scan[h->scan_count++];
        ap->channel = m->body[at];
        ap->rssi = (int8_t)m->body[at + 1];
        ap->auth = m->body[at + 2];
        memcpy(ap->bssid, m->body + at + 4, 6);
        copy_text(ap->ssid, sizeof ap->ssid, m->body + at + 10, m->body[at + 3]);
        at += 10u + m->body[at + 3];
    }
    ++h->counts.scan;
}
static void state_r(struct kwh_slot *s, const struct kwm *m) {
    if(m->len < 10) return;
    uint8_t state = m->body[0];
    if(s->state == KWM_CLOSING && streaming(state)) {
        /* Sent before the board saw our close. */
        if(state == KWM_PEER_CLOSED) s->events |= KWH_EV_PEER_CLOSED;
        return;
    }
    if(state == KWM_ESTABLISHED && s->state != KWM_ESTABLISHED) s->events |= KWH_EV_OPEN;
    if(state == KWM_PEER_CLOSED && s->state != KWM_PEER_CLOSED) s->events |= KWH_EV_PEER_CLOSED;
    if(state == KWM_CLOSED) slot_closed(s, m->body[1]);
    s->state = state;
    s->err = m->body[1];
    if(kwl_get16(m->body + 2)) s->local_port = kwl_get16(m->body + 2);
    if(state == KWM_ESTABLISHED) {
        memcpy(s->ip, m->body + 4, 4);
        s->port = kwl_get16(m->body + 8);
    }
}
static void data_r(struct kwh *h, struct kwh_slot *s, const struct kwm *m) {
    if(s->state == KWM_CLOSED) return;
    if(s->kind == KWM_UDP) {
        if(m->len < 6) return;
        uint8_t head[KWM_UDP_OVERHEAD];
        kwl_put16(head, (uint16_t)(m->len - 6u));
        memcpy(head + 2, m->body, 6);
        if(KWH_RX - s->rx_len < KWM_UDP_OVERHEAD + m->len - 6u) {
            ++h->failures; /* more than the credit allowed */
            return;
        }
        rx_put(s, head, sizeof head);
        rx_put(s, m->body + 6, m->len - 6u);
    } else if(!rx_put(s, m->body, m->len)) {
        ++h->failures;
        return;
    }
    s->events |= KWH_EV_DATA;
}
static void deliver(void *ctx, const uint8_t *payload, size_t len) {
    struct kwh *h = ctx;
    const uint8_t *p = payload;
    struct kwm m;
    while(kwm_next(&p, payload + len, &m)) {
        struct kwh_slot *s = &h->slot[m.slot & 7u];
        bool current = (m.slot >> 3) == s->gen;
        switch(m.type) {
        case KWM_HELLO_R: hello_r(h, &m); break;
        case KWM_WIFI_STATUS: wifi_r(h, &m); break;
        case KWM_WIFI_SCAN_R: scan_r(h, &m); break;
        case KWM_WIFI_JOIN_R:
            h->join_status = m.len ? m.body[0] : KWM_E_OTHER;
            ++h->counts.join;
            break;
        case KWM_SOCK_STATE: if(current) state_r(s, &m); break;
        case KWM_SOCK_TXCREDIT:
            if(current && m.len >= 4 && s->state != KWM_CLOSED && s->state != KWM_CLOSING) {
                uint32_t more = kwl_get32(m.body);
                s->tx_credit = s->tx_credit > UINT32_MAX - more ? UINT32_MAX : s->tx_credit + more;
            }
            break;
        case KWM_SOCK_DATA: if(current) data_r(h, s, &m); break;
        case KWM_DNS_R:
            if(m.len >= 6) {
                h->dns_tag = m.body[0];
                h->dns_status = m.body[1];
                memcpy(h->dns_ip, m.body + 2, 4);
                ++h->counts.dns;
            }
            break;
        case KWM_TIME_R:
            if(m.len >= 12) {
                h->time_status = m.body[0];
                uint64_t seconds = kwl_get32(m.body + 4) | (uint64_t)kwl_get32(m.body + 8) << 32;
                h->time_ms = seconds * 1000u + kwl_get16(m.body + 2);
                ++h->counts.time;
            }
            break;
        case KWM_OTA_R:
            if(m.len >= 8) {
                h->ota_phase = m.body[0];
                h->ota_status = m.body[1];
                h->ota_written = kwl_get32(m.body + 4);
                ++h->counts.ota;
            }
            break;
        case KWM_REBOOT_R: ++h->counts.reboot; break;
        default: break;
        }
    }
}

/* Credit for data read goes back in batches, or as soon as all is read. */
static void credit_back(struct kwh *h) {
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwh_slot *s = &h->slot[i];
        if(!s->credit_back || (s->credit_back < KWH_RX / 4u && s->rx_len)) continue;
        uint8_t body[4];
        kwl_put32(body, s->credit_back);
        if(s->state != KWM_CLOSED && !queue(h, KWM_SOCK_CREDIT, slot_byte(s, i), body, 4)) continue;
        s->credit_back = 0;
    }
}
bool kwh_step(struct kwh *h) {
    credit_back(h);
    struct kwl_io io = {h, fill, deliver, NULL};
    size_t own = kwl_build(&h->link, h->out, &io);
    size_t len = kwl_host_length(&h->link, own);
    memset(h->out + own, 0, len - own);
    if(!h->bus || !h->bus->transfer || !h->bus->transfer(h->bus->ctx, h->out, h->in, len)) {
        /* The frame went nowhere; the link sends it again later. */
        ++h->failures;
        kwl_receive(&h->link, NULL, 0, &io);
        return false;
    }
    ++h->transfers;
    if(kwl_receive(&h->link, h->in, len, &io) == KWL_LOST) {
        /* The board restarted: its sockets are gone. Start again. */
        ++h->counts.lost;
        begin(h, (uint16_t)(h->session * 40503u + 1u));
    }
    return true;
}
bool kwh_idle(const struct kwh *h) {
    return h->link.live && !h->queue_len && !kwl_busy(&h->link) && !h->link.peer_window;
}

/* Requests. */
bool kwh_wifi_query(struct kwh *h) { return queue(h, KWM_WIFI_STATUS_GET, 0, NULL, 0) != NULL; }
bool kwh_wifi_scan(struct kwh *h) { return queue(h, KWM_WIFI_SCAN, 0, NULL, 0) != NULL; }
bool kwh_wifi_join(struct kwh *h, const char *ssid, const char *password, bool save, uint8_t band) {
    size_t ssid_len = ssid ? strlen(ssid) : 0, pass_len = password ? strlen(password) : 0;
    if(!ssid_len || ssid_len > KWM_SSID_MAX || pass_len > KWM_PASSWORD_MAX) return false;
    uint8_t *body = queue(h, KWM_WIFI_JOIN, 0, NULL, 4u + ssid_len + pass_len);
    if(!body) return false;
    body[0] = band;
    body[1] = save;
    body[2] = (uint8_t)ssid_len;
    body[3] = (uint8_t)pass_len;
    memcpy(body + 4, ssid, ssid_len);
    memcpy(body + 4 + ssid_len, password, pass_len);
    return true;
}
bool kwh_wifi_leave(struct kwh *h, bool forget) {
    uint8_t body = forget;
    return queue(h, KWM_WIFI_LEAVE, 0, &body, 1) != NULL;
}
bool kwh_dns(struct kwh *h, uint8_t tag, const char *name) {
    size_t len = name ? strlen(name) : 0;
    if(!len || len > KWM_NAME_MAX) return false;
    uint8_t *body = queue(h, KWM_DNS, 0, NULL, 2u + len);
    if(!body) return false;
    body[0] = tag;
    body[1] = (uint8_t)len;
    memcpy(body + 2, name, len);
    return true;
}
bool kwh_time(struct kwh *h) { return queue(h, KWM_TIME, 0, NULL, 0) != NULL; }
bool kwh_ota_begin(struct kwh *h, uint32_t size, const uint8_t sha256[32]) {
    uint8_t body[36];
    kwl_put32(body, size);
    memcpy(body + 4, sha256, 32);
    return queue(h, KWM_OTA_BEGIN, 0, body, sizeof body) != NULL;
}
bool kwh_ota_data(struct kwh *h, uint32_t offset, const void *data, size_t len) {
    if(len > KWM_BODY_MAX - 4u) return false;
    uint8_t *body = queue(h, KWM_OTA_DATA, 0, NULL, 4u + len);
    if(!body) return false;
    kwl_put32(body, offset);
    memcpy(body + 4, data, len);
    return true;
}
bool kwh_ota_end(struct kwh *h) { return queue(h, KWM_OTA_END, 0, NULL, 0) != NULL; }
bool kwh_reboot(struct kwh *h) { return queue(h, KWM_REBOOT, 0, NULL, 0) != NULL; }

/* Sockets. */
static bool open_slot(struct kwh *h, unsigned i, uint8_t kind, uint8_t flags, uint16_t local, const uint8_t ip[4],
                      uint16_t remote) {
    if(i >= KWM_SLOTS) return false;
    struct kwh_slot *s = &h->slot[i];
    uint8_t gen = (uint8_t)((s->gen + 1u) & 31u), body[16];
    memset(body, 0, sizeof body);
    body[0] = kind;
    body[1] = flags;
    kwl_put16(body + 2, local);
    if(ip) memcpy(body + 4, ip, 4);
    kwl_put16(body + 8, remote);
    kwl_put32(body + 12, KWH_RX);
    if(!queue(h, KWM_SOCK_OPEN, (uint8_t)(i | (unsigned)gen << 3), body, sizeof body)) return false;
    memset(s, 0, sizeof *s);
    s->gen = gen;
    s->kind = kind;
    s->state = kind == KWM_TCP_LISTEN ? KWM_LISTEN : kind == KWM_TCP_CONNECT ? KWM_CONNECTING : KWM_UDP_OPEN;
    s->local_port = local;
    if(ip) memcpy(s->ip, ip, 4);
    s->port = remote;
    return true;
}
bool kwh_listen(struct kwh *h, unsigned s, uint16_t port, uint8_t flags) {
    return port && open_slot(h, s, KWM_TCP_LISTEN, flags, port, NULL, 0);
}
bool kwh_connect(struct kwh *h, unsigned s, const uint8_t ip[4], uint16_t port, uint8_t flags) {
    return ip && port && open_slot(h, s, KWM_TCP_CONNECT, flags, 0, ip, port);
}
bool kwh_udp(struct kwh *h, unsigned s, uint16_t port) { return open_slot(h, s, KWM_UDP, 0, port, NULL, 0); }
bool kwh_close(struct kwh *h, unsigned i, bool graceful) {
    if(i >= KWM_SLOTS) return false;
    struct kwh_slot *s = &h->slot[i];
    if(s->state == KWM_CLOSED) return true;
    graceful = graceful && streaming(s->state);
    uint8_t how = graceful;
    if(!queue(h, KWM_SOCK_CLOSE, slot_byte(s, i), &how, 1)) return false;
    if(graceful) {
        s->state = KWM_CLOSING;
        s->tx_credit = 0;
        return true;
    }
    /* Closed now: whatever the board still says about this connection is
     * for a generation we no longer listen to. */
    slot_closed(s, KWM_E_NONE);
    s->events &= (uint8_t)~KWH_EV_CLOSED;
    s->gen = (uint8_t)((s->gen + 1u) & 31u);
    s->rx_head = s->rx_len = 0;
    s->credit_back = 0;
    return true;
}
uint8_t kwh_state(const struct kwh *h, unsigned s) { return s < KWM_SLOTS ? h->slot[s].state : KWM_CLOSED; }
uint8_t kwh_error(const struct kwh *h, unsigned s) { return s < KWM_SLOTS ? h->slot[s].err : KWM_E_INVALID; }
uint8_t kwh_events(struct kwh *h, unsigned s) {
    if(s >= KWM_SLOTS) return 0;
    uint8_t events = h->slot[s].events;
    h->slot[s].events = 0;
    return events;
}
bool kwh_peer(const struct kwh *h, unsigned s, uint8_t ip[4], uint16_t *port) {
    if(s >= KWM_SLOTS) return false;
    if(ip) memcpy(ip, h->slot[s].ip, 4);
    if(port) *port = h->slot[s].port;
    return true;
}
size_t kwh_room(const struct kwh *h, unsigned i) {
    if(i >= KWM_SLOTS || !h->link.live) return 0;
    const struct kwh_slot *s = &h->slot[i];
    if(s->kind == KWM_UDP) {
        if(s->state != KWM_UDP_OPEN || s->tx_credit < KWM_UDP_OVERHEAD) return 0;
        return smaller(smaller(s->tx_credit - KWM_UDP_OVERHEAD, KWM_BODY_MAX - 6u),
                       queue_room(h) > 6u ? queue_room(h) - 6u : 0);
    }
    if(!streaming(s->state)) return 0;
    return smaller(smaller(s->tx_credit, KWM_BODY_MAX), queue_room(h));
}
size_t kwh_send(struct kwh *h, unsigned i, const void *data, size_t len) {
    size_t n = smaller(len, kwh_room(h, i));
    if(!n || h->slot[i].kind == KWM_UDP) return 0;
    struct kwh_slot *s = &h->slot[i];
    if(!queue(h, KWM_SOCK_SEND, slot_byte(s, i), data, n)) return 0;
    s->tx_credit -= (uint32_t)n;
    return n;
}
bool kwh_sendto(struct kwh *h, unsigned i, const uint8_t ip[4], uint16_t port, const void *data, size_t len) {
    if(i >= KWM_SLOTS || h->slot[i].kind != KWM_UDP || len > kwh_room(h, i)) return false;
    struct kwh_slot *s = &h->slot[i];
    uint8_t *body = queue(h, KWM_SOCK_SEND, slot_byte(s, i), NULL, 6u + len);
    if(!body) return false;
    memcpy(body, ip, 4);
    kwl_put16(body + 4, port);
    memcpy(body + 6, data, len);
    s->tx_credit -= (uint32_t)(KWM_UDP_OVERHEAD + len);
    return true;
}
size_t kwh_available(const struct kwh *h, unsigned s) {
    return s < KWM_SLOTS && h->slot[s].kind != KWM_UDP ? h->slot[s].rx_len : 0;
}
size_t kwh_recv(struct kwh *h, unsigned i, void *data, size_t len) {
    if(i >= KWM_SLOTS || h->slot[i].kind == KWM_UDP) return 0;
    struct kwh_slot *s = &h->slot[i];
    size_t n = smaller(len, s->rx_len);
    rx_take(s, data, n);
    s->credit_back += (uint32_t)n;
    return n;
}
size_t kwh_recvfrom(struct kwh *h, unsigned i, uint8_t ip[4], uint16_t *port, void *data, size_t capacity) {
    if(i >= KWM_SLOTS || h->slot[i].kind != KWM_UDP) return 0;
    struct kwh_slot *s = &h->slot[i];
    if(s->rx_len < KWM_UDP_OVERHEAD) return 0;
    uint8_t head[KWM_UDP_OVERHEAD];
    rx_peek(s, head, sizeof head);
    size_t len = kwl_get16(head), n = smaller(len, capacity);
    rx_take(s, NULL, sizeof head);
    rx_take(s, data, n);
    rx_take(s, NULL, len - n);
    if(ip) memcpy(ip, head + 2, 4);
    if(port) *port = kwl_get16(head + 6);
    s->credit_back += (uint32_t)(KWM_UDP_OVERHEAD + len);
    return n;
}
