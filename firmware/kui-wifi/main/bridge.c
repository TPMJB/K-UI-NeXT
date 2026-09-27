/* SPDX-License-Identifier: MIT */
/* The Wi-Fi board's side of the K-UI link. See PROTOCOL.md and bridge.h. */
#include "bridge.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif
#define CONNECT_MS 15000u
#define CLOSE_MS 10000u
#define REBOOT_DELAY_MS 300u
#define REBOOT_LIMIT_MS 2000u
#define STATE_BODY 10u
#define WIFI_BODY 30u
#define HELLO_BODY 15u
#define UDP_RECORD KWM_UDP_OVERHEAD

static uint32_t now(const struct kwb *b) { return b->pf && b->pf->now_ms ? b->pf->now_ms(b->pf->ctx) : 0; }
static bool due(uint32_t t, uint32_t deadline) { return (int32_t)(t - deadline) >= 0; }
static size_t smaller(size_t a, size_t b) { return a < b ? a : b; }

/* Byte rings for each slot's two directions. */
static size_t ring_room(const struct kwb_ring *r) { return r->cap - r->len; }
static uint8_t *ring_space(struct kwb_ring *r, size_t *n) {
    size_t tail = (r->head + r->len) % r->cap;
    *n = smaller(r->cap - tail, ring_room(r));
    return r->buf + tail;
}
static const uint8_t *ring_data(const struct kwb_ring *r, size_t *n) {
    *n = smaller(r->len, r->cap - r->head);
    return r->buf + r->head;
}
static void ring_drop(struct kwb_ring *r, size_t n) {
    r->head = (r->head + n) % r->cap;
    r->len -= n;
    if(!r->len) r->head = 0;
}
static bool ring_put(struct kwb_ring *r, const void *data, size_t n) {
    if(!r->buf || ring_room(r) < n) return false;
    const uint8_t *p = data;
    while(n) {
        size_t room;
        uint8_t *dst = ring_space(r, &room);
        size_t k = smaller(room, n);
        memcpy(dst, p, k);
        r->len += k;
        p += k;
        n -= k;
    }
    return true;
}
static void ring_copy(const struct kwb_ring *r, size_t offset, uint8_t *out, size_t n) {
    size_t pos = (r->head + offset) % r->cap;
    while(n) {
        size_t k = smaller(n, r->cap - pos);
        memcpy(out, r->buf + pos, k);
        out += k;
        n -= k;
        pos = 0;
    }
}
static bool ring_ready(struct kwb_ring *r, size_t cap) {
    if(!r->buf) {
        r->buf = malloc(cap);
        r->cap = r->buf ? cap : 0;
    }
    r->head = r->len = 0;
    return r->buf != NULL;
}

static uint8_t map_errno(int e) {
    switch(e) {
    case ECONNREFUSED: return KWM_E_REFUSED;
    case ETIMEDOUT: return KWM_E_TIMEOUT;
    case ECONNRESET: case ECONNABORTED: case EPIPE: return KWM_E_RESET;
    case EHOSTUNREACH: case ENETUNREACH: case ENETDOWN: return KWM_E_UNREACHABLE;
    case ENOMEM: case ENOBUFS: case EMFILE: case ENFILE: return KWM_E_NOMEM;
    case EADDRINUSE: return KWM_E_INUSE;
    case EINVAL: return KWM_E_INVALID;
    default: return KWM_E_OTHER;
    }
}
static bool would_block(int e) { return e == EAGAIN || e == EWOULDBLOCK || e == EINTR; }
static bool nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) >= 0;
}
/* Close with RST rather than FIN: nothing more is sent. */
static void abort_fd(int fd) {
    struct linger linger = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &linger, sizeof linger);
    close(fd);
}
static struct sockaddr_in address(const uint8_t ip[4], uint16_t port) {
    struct sockaddr_in a;
    memset(&a, 0, sizeof a);
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    if(ip) memcpy(&a.sin_addr.s_addr, ip, 4);
    else a.sin_addr.s_addr = htonl(INADDR_ANY);
    return a;
}
static uint16_t bound_port(int fd) {
    struct sockaddr_in a;
    socklen_t len = sizeof a;
    return getsockname(fd, (struct sockaddr *)&a, &len) == 0 ? ntohs(a.sin_port) : 0;
}

static uint8_t slot_byte(const struct kwb_slot *s, unsigned i) { return (uint8_t)(i | (unsigned)s->gen << 3); }
/* A peer's close or the end of a connection is shown only once all the
 * data received before it has gone to the host. */
static uint8_t visible(const struct kwb_slot *s) {
    if(s->rx.len && s->kind != KWM_UDP && (s->state == KWM_PEER_CLOSED || s->state == KWM_CLOSED))
        return s->closing ? KWM_CLOSING : KWM_ESTABLISHED;
    return s->state;
}
/* Everything for this slot stops except handing over data received. */
static void finish(struct kwb_slot *s, uint8_t err, bool reset) {
    if(s->fd >= 0) {
        if(reset) abort_fd(s->fd);
        else close(s->fd);
    }
    s->fd = -1;
    s->state = KWM_CLOSED;
    s->err = err;
    s->ended = true;
    s->closing = s->fin_sent = false;
    s->grant = 0;
    s->tx.head = s->tx.len = 0;
    s->dirty = true;
}
/* Back to an unused slot; its buffers are kept for the next connection. */
static void slot_clear(struct kwb_slot *s) {
    if(s->fd >= 0) abort_fd(s->fd);
    struct kwb_ring rx = s->rx, tx = s->tx;
    memset(s, 0, sizeof *s);
    s->fd = -1;
    s->rx = rx;
    s->tx = tx;
    s->rx.head = s->rx.len = s->tx.head = s->tx.len = 0;
}
static void set_options(const struct kwb_slot *s) {
    int one = 1;
    if(s->flags & KWM_NODELAY) setsockopt(s->fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    if(s->flags & KWM_KEEPALIVE) {
        setsockopt(s->fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
#ifdef TCP_KEEPIDLE
        int idle = s->keepalive ? s->keepalive : 60, interval = 5, count = 3;
        setsockopt(s->fd, IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof idle);
        setsockopt(s->fd, IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof interval);
        setsockopt(s->fd, IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof count);
#endif
    }
}
static void established(struct kwb_slot *s) {
    set_options(s);
    s->state = KWM_ESTABLISHED;
    s->grant = (uint32_t)s->tx.cap;
    s->dirty = true;
}

static void reply(struct kwb *b, uint8_t type, uint8_t slot, const void *body, size_t len) {
    if(!kwm_put(b->reply, sizeof b->reply, &b->reply_len, type, slot, body, len)) ++b->stats.dropped_replies;
}
static void close_all(struct kwb *b) {
    for(unsigned i = 0; i < KWM_SLOTS; ++i) slot_clear(&b->slot[i]);
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        if(b->listener[i].fd >= 0) close(b->listener[i].fd);
        b->listener[i].fd = -1;
        b->listener[i].port = 0;
    }
}
/* A new session or a reset: nothing survives from the old one. */
static void forget(struct kwb *b) {
    close_all(b);
    b->reply_len = 0;
    b->scan_busy = b->dns_busy = false;
    b->wifi_dirty = true;
}

void kwb_init(struct kwb *b, const struct kwb_platform *pf, size_t buffer) {
    memset(b, 0, sizeof *b);
    b->pf = pf;
    b->buffer = buffer < 1024u ? 1024u : buffer;
    for(unsigned i = 0; i < KWM_SLOTS; ++i) b->slot[i].fd = b->listener[i].fd = -1;
    kwl_init(&b->link, KWL_BRIDGE);
    kwl_bridge_reset(&b->link);
}
void kwb_reset(struct kwb *b) {
    kwl_bridge_reset(&b->link);
    forget(b);
}
void kwb_release(struct kwb *b) {
    close_all(b);
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        free(b->slot[i].rx.buf);
        free(b->slot[i].tx.buf);
        b->slot[i].rx = b->slot[i].tx = (struct kwb_ring){0};
    }
}
void kwb_notify(struct kwb *b, uint32_t notes) { __atomic_fetch_or(&b->notes, notes, __ATOMIC_RELEASE); }

/* Messages from the host. */
static void hello(struct kwb *b) {
    struct kwb_info info;
    memset(&info, 0, sizeof info);
    if(b->pf->info) b->pf->info(b->pf->ctx, &info);
    size_t vlen = strnlen(info.version, KWM_VERSION_MAX);
    uint8_t body[HELLO_BODY + KWM_VERSION_MAX];
    kwl_put16(body, KWL_PROTOCOL);
    body[2] = KWM_SLOTS;
    body[3] = info.chip;
    kwl_put16(body + 4, KWL_PAYLOAD_MAX);
    kwl_put16(body + 6, 0);
    memcpy(body + 8, info.mac, 6);
    body[14] = (uint8_t)vlen;
    memcpy(body + HELLO_BODY, info.version, vlen);
    reply(b, KWM_HELLO_R, 0, body, HELLO_BODY + vlen);
}
static void join(struct kwb *b, const struct kwm *m) {
    uint8_t status = KWM_E_INVALID;
    if(m->len >= 4) {
        size_t ssid_len = m->body[2], pass_len = m->body[3];
        if(ssid_len && ssid_len <= KWM_SSID_MAX && pass_len <= KWM_PASSWORD_MAX && 4u + ssid_len + pass_len <= m->len) {
            char ssid[KWM_SSID_MAX + 1], password[KWM_PASSWORD_MAX + 1];
            memcpy(ssid, m->body + 4, ssid_len);
            ssid[ssid_len] = 0;
            memcpy(password, m->body + 4 + ssid_len, pass_len);
            password[pass_len] = 0;
            status = b->pf->wifi_join ? b->pf->wifi_join(b->pf->ctx, ssid, password, m->body[1] != 0, m->body[0])
                                      : KWM_E_OTHER;
            memset(password, 0, sizeof password);
        }
    }
    reply(b, KWM_WIFI_JOIN_R, 0, &status, 1);
    b->wifi_dirty = true;
}
static void dns(struct kwb *b, const struct kwm *m) {
    uint8_t answer[6] = {m->len ? m->body[0] : 0, 1, 0, 0, 0, 0};
    if(b->dns_busy) {
        answer[1] = 3;
    } else if(m->len >= 2 && m->body[1] && 2u + m->body[1] <= m->len) {
        char name[KWM_NAME_MAX + 3];
        size_t len = smaller(m->body[1], KWM_NAME_MAX);
        memcpy(name, m->body + 2, len);
        name[len] = 0;
        if(b->pf->dns_start && b->pf->dns_start(b->pf->ctx, name)) {
            b->dns_busy = true;
            b->dns_tag = m->body[0];
            return;
        }
    }
    reply(b, KWM_DNS_R, 0, answer, sizeof answer);
}
static void clock_reply(struct kwb *b) {
    uint8_t body[12] = {1};
    uint64_t ms = 0;
    if(b->pf->time_now && b->pf->time_now(b->pf->ctx, &ms)) body[0] = 0;
    uint64_t seconds = ms / 1000u;
    kwl_put16(body + 2, (uint16_t)(ms % 1000u));
    kwl_put32(body + 4, (uint32_t)seconds);
    kwl_put32(body + 8, (uint32_t)(seconds >> 32));
    reply(b, KWM_TIME_R, 0, body, sizeof body);
}
static void ota_reply(struct kwb *b, uint8_t phase, uint8_t status, uint32_t written) {
    uint8_t body[8] = {phase, status, 0, 0};
    kwl_put32(body + 4, written);
    reply(b, KWM_OTA_R, 0, body, sizeof body);
}
static void ota(struct kwb *b, const struct kwm *m) {
    const struct kwb_platform *pf = b->pf;
    if(m->type == KWM_OTA_BEGIN) {
        uint8_t status = m->len >= 36 && pf->ota_begin ? pf->ota_begin(pf->ctx, kwl_get32(m->body), m->body + 4)
                                                        : KWM_E_INVALID;
        ota_reply(b, KWM_OTA_BEGIN_PHASE, status, 0);
    } else if(m->type == KWM_OTA_DATA) {
        uint32_t offset = m->len >= 4 ? kwl_get32(m->body) : 0;
        uint8_t status = m->len >= 4 && pf->ota_write ? pf->ota_write(pf->ctx, offset, m->body + 4, m->len - 4u)
                                                       : KWM_E_INVALID;
        ota_reply(b, KWM_OTA_DATA_PHASE, status, offset + (m->len >= 4 ? m->len - 4u : 0));
    } else {
        ota_reply(b, KWM_OTA_END_PHASE, pf->ota_end ? pf->ota_end(pf->ctx) : KWM_E_INVALID, 0);
    }
}

static void sock_open(struct kwb *b, unsigned i, uint8_t gen, const struct kwm *m) {
    struct kwb_slot *s = &b->slot[i];
    slot_clear(s);
    s->gen = gen;
    s->dirty = true;
    if(m->len < 16) {
        s->err = KWM_E_INVALID;
        return;
    }
    const uint8_t *p = m->body;
    s->kind = p[0];
    s->flags = p[1];
    s->local_port = kwl_get16(p + 2);
    memcpy(s->remote_ip, p + 4, 4);
    s->remote_port = kwl_get16(p + 8);
    s->keepalive = kwl_get16(p + 10);
    s->host_credit = kwl_get32(p + 12);
    if(!ring_ready(&s->rx, b->buffer) || !ring_ready(&s->tx, b->buffer)) {
        s->err = KWM_E_NOMEM;
        return;
    }
    int one = 1;
    if(s->kind == KWM_TCP_LISTEN) {
        /* The listening socket itself is managed per port in the service. */
        if(s->local_port) s->state = KWM_LISTEN;
        else s->err = KWM_E_INVALID;
    } else if(s->kind == KWM_TCP_CONNECT) {
        s->fd = socket(AF_INET, SOCK_STREAM, 0);
        if(s->fd < 0 || !nonblocking(s->fd)) {
            finish(s, KWM_E_NOMEM, true);
            return;
        }
        if(s->local_port) {
            setsockopt(s->fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            struct sockaddr_in local = address(NULL, s->local_port);
            if(bind(s->fd, (struct sockaddr *)&local, sizeof local) < 0) {
                finish(s, map_errno(errno), true);
                return;
            }
        }
        struct sockaddr_in to = address(s->remote_ip, s->remote_port);
        s->state = KWM_CONNECTING;
        s->deadline = now(b) + CONNECT_MS;
        if(connect(s->fd, (struct sockaddr *)&to, sizeof to) == 0) established(s);
        else if(errno != EINPROGRESS && !would_block(errno)) {
            finish(s, map_errno(errno), true);
            return;
        }
        s->local_port = bound_port(s->fd);
    } else if(s->kind == KWM_UDP) {
        s->fd = socket(AF_INET, SOCK_DGRAM, 0);
        struct sockaddr_in local = address(NULL, s->local_port);
        if(s->fd < 0 || !nonblocking(s->fd) || bind(s->fd, (struct sockaddr *)&local, sizeof local) < 0) {
            finish(s, s->fd < 0 ? KWM_E_NOMEM : map_errno(errno), true);
            return;
        }
        s->local_port = bound_port(s->fd);
        s->state = KWM_UDP_OPEN;
        s->grant = (uint32_t)s->tx.cap;
    } else {
        s->err = KWM_E_INVALID;
    }
}
static void sock_close(struct kwb_slot *s, uint8_t how) {
    if(how == 1 && (s->state == KWM_ESTABLISHED || s->state == KWM_PEER_CLOSED)) {
        /* FIN once the queued data has gone (see kwb_service). */
        s->closing = true;
        s->state = KWM_CLOSING;
        s->dirty = true;
    } else if(s->state != KWM_CLOSED || s->rx.len) {
        finish(s, KWM_E_NONE, true);
        s->rx.head = s->rx.len = 0;
    }
}
static void sock_send(struct kwb *b, struct kwb_slot *s, const struct kwm *m) {
    if(s->kind == KWM_UDP) {
        if(s->state != KWM_UDP_OPEN || m->len < 6) return;
        size_t n = m->len - 6u;
        uint8_t head[UDP_RECORD];
        kwl_put16(head, (uint16_t)n);
        memcpy(head + 2, m->body, 6);
        if(ring_room(&s->tx) < UDP_RECORD + n) {
            ++b->stats.dropped_sends;
            return;
        }
        ring_put(&s->tx, head, UDP_RECORD);
        ring_put(&s->tx, m->body + 6, n);
        return;
    }
    if(s->closing || (s->state != KWM_ESTABLISHED && s->state != KWM_PEER_CLOSED)) return;
    size_t n = smaller(m->len, ring_room(&s->tx));
    ring_put(&s->tx, m->body, n);
    if(n < m->len) ++b->stats.dropped_sends;
}
static void socket_message(struct kwb *b, const struct kwm *m) {
    unsigned i = m->slot & 7u;
    uint8_t gen = (uint8_t)(m->slot >> 3);
    struct kwb_slot *s = &b->slot[i];
    if(m->type == KWM_SOCK_OPEN) {
        sock_open(b, i, gen, m);
        return;
    }
    if(gen != s->gen) return; /* for a connection that has since been replaced */
    if(m->type == KWM_SOCK_CLOSE) sock_close(s, m->len ? m->body[0] : 0);
    else if(m->type == KWM_SOCK_SEND) sock_send(b, s, m);
    else if(m->type == KWM_SOCK_CREDIT && m->len >= 4) {
        uint32_t more = kwl_get32(m->body);
        s->host_credit = s->host_credit > UINT32_MAX - more ? UINT32_MAX : s->host_credit + more;
    }
}
static void handle(struct kwb *b, const struct kwm *m) {
    const struct kwb_platform *pf = b->pf;
    switch(m->type) {
    case KWM_HELLO: hello(b); break;
    case KWM_WIFI_STATUS_GET: b->wifi_dirty = true; break;
    case KWM_WIFI_SCAN:
        if(!b->scan_busy) {
            if(pf->wifi_scan && pf->wifi_scan(pf->ctx)) b->scan_busy = true;
            else reply(b, KWM_WIFI_SCAN_R, 0, (const uint8_t[2]){1, 0}, 2);
        }
        break;
    case KWM_WIFI_JOIN: join(b, m); break;
    case KWM_WIFI_LEAVE:
        if(pf->wifi_leave) pf->wifi_leave(pf->ctx, m->len && m->body[0]);
        b->wifi_dirty = true;
        break;
    case KWM_SOCK_OPEN: case KWM_SOCK_CLOSE: case KWM_SOCK_SEND: case KWM_SOCK_CREDIT: socket_message(b, m); break;
    case KWM_DNS: dns(b, m); break;
    case KWM_TIME: clock_reply(b); break;
    case KWM_OTA_BEGIN: case KWM_OTA_DATA: case KWM_OTA_END: ota(b, m); break;
    case KWM_REBOOT:
        reply(b, KWM_REBOOT_R, 0, (const uint8_t[1]){0}, 1);
        b->reboot_due = true;
        b->reboot_at = now(b) + REBOOT_DELAY_MS;
        break;
    default: break;
    }
}
static void deliver(void *ctx, const uint8_t *payload, size_t len) {
    struct kwb *b = ctx;
    const uint8_t *p = payload;
    struct kwm m;
    while(kwm_next(&p, payload + len, &m)) handle(b, &m);
}

/* Messages to the host: answers first, then socket states, transmit
 * credit, Wi-Fi status, and received data from the slots in turn. */
static bool put_state(uint8_t *p, size_t cap, size_t *used, unsigned i, struct kwb_slot *s, uint8_t shown) {
    uint8_t *body = kwm_put(p, cap, used, KWM_SOCK_STATE, slot_byte(s, i), NULL, STATE_BODY);
    if(!body) return false;
    body[0] = shown;
    body[1] = s->err;
    kwl_put16(body + 2, s->local_port);
    memcpy(body + 4, s->remote_ip, 4);
    kwl_put16(body + 8, s->remote_port);
    return true;
}
static bool put_wifi(struct kwb *b, uint8_t *p, size_t cap, size_t *used) {
    struct kwb_wifi w;
    memset(&w, 0, sizeof w);
    if(b->pf->wifi_status) b->pf->wifi_status(b->pf->ctx, &w);
    size_t ssid_len = strnlen(w.ssid, KWM_SSID_MAX);
    uint8_t *body = kwm_put(p, cap, used, KWM_WIFI_STATUS, 0, NULL, WIFI_BODY + ssid_len);
    if(!body) return false;
    body[0] = w.state;
    body[1] = w.band;
    body[2] = w.channel;
    body[3] = (uint8_t)w.rssi;
    memcpy(body + 4, w.ip, 4);
    memcpy(body + 8, w.mask, 4);
    memcpy(body + 12, w.gateway, 4);
    memcpy(body + 16, w.dns, 4);
    memcpy(body + 20, w.bssid, 6);
    body[26] = w.band_mode;
    body[27] = w.reason;
    body[28] = w.saved;
    body[29] = (uint8_t)ssid_len;
    memcpy(body + WIFI_BODY, w.ssid, ssid_len);
    return true;
}
/* Received data for one slot: as much as fits and the host can take. */
static bool put_data(uint8_t *p, size_t cap, size_t *used, unsigned i, struct kwb_slot *s) {
    if(!s->rx.len || !s->host_credit) return false;
    if(*used + KWM_HEADER >= cap) return false;
    size_t space = cap - *used - KWM_HEADER;
    if(s->kind == KWM_UDP) {
        uint8_t head[UDP_RECORD];
        ring_copy(&s->rx, 0, head, UDP_RECORD);
        size_t n = kwl_get16(head);
        if(space < 6u + n || s->host_credit < UDP_RECORD + n) return false;
        uint8_t *body = kwm_put(p, cap, used, KWM_SOCK_DATA, slot_byte(s, i), NULL, 6u + n);
        memcpy(body, head + 2, 6);
        ring_copy(&s->rx, UDP_RECORD, body + 6, n);
        ring_drop(&s->rx, UDP_RECORD + n);
        s->host_credit -= (uint32_t)(UDP_RECORD + n);
        return true;
    }
    size_t n = smaller(smaller(s->rx.len, s->host_credit), smaller(space, KWM_BODY_MAX));
    if(!n) return false;
    uint8_t *body = kwm_put(p, cap, used, KWM_SOCK_DATA, slot_byte(s, i), NULL, n);
    ring_copy(&s->rx, 0, body, n);
    ring_drop(&s->rx, n);
    s->host_credit -= (uint32_t)n;
    return true;
}
static size_t fill(void *ctx, uint8_t *p, size_t cap) {
    struct kwb *b = ctx;
    size_t used = 0, done = 0;
    while(done < b->reply_len) {
        size_t len = KWM_HEADER + kwl_get16(b->reply + done + 2);
        if(used + len > cap) break;
        memcpy(p + used, b->reply + done, len);
        used += len;
        done += len;
    }
    memmove(b->reply, b->reply + done, b->reply_len - done);
    b->reply_len -= done;
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwb_slot *s = &b->slot[i];
        uint8_t shown = visible(s);
        if((s->dirty || shown != s->shown) && put_state(p, cap, &used, i, s, shown)) {
            s->dirty = false;
            s->shown = shown;
        }
    }
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwb_slot *s = &b->slot[i];
        uint8_t body[4];
        kwl_put32(body, s->grant);
        if(s->grant && kwm_put(p, cap, &used, KWM_SOCK_TXCREDIT, slot_byte(s, i), body, 4)) s->grant = 0;
    }
    if(b->wifi_dirty && put_wifi(b, p, cap, &used)) b->wifi_dirty = false;
    /* Data, one message per slot per round, starting where the last frame
     * stopped so no slot can starve the others. */
    for(bool more = true; more;) {
        more = false;
        for(unsigned k = 0; k < KWM_SLOTS; ++k) {
            unsigned i = (b->next_data + k) % KWM_SLOTS;
            if(put_data(p, cap, &used, i, &b->slot[i])) more = true;
        }
        b->next_data = (b->next_data + 1u) % KWM_SLOTS;
    }
    return used;
}
static size_t pending(void *ctx) {
    struct kwb *b = ctx;
    size_t n = b->reply_len + (b->wifi_dirty ? KWM_HEADER + WIFI_BODY + KWM_SSID_MAX : 0);
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwb_slot *s = &b->slot[i];
        if(s->dirty || visible(s) != s->shown) n += KWM_HEADER + STATE_BODY;
        if(s->grant) n += KWM_HEADER + 4u;
        size_t data = smaller(s->rx.len, s->host_credit);
        if(data) n += data + KWM_HEADER;
    }
    return n;
}

size_t kwb_frame(struct kwb *b, uint8_t frame[KWL_FRAME_MAX]) {
    struct kwl_io io = {b, fill, deliver, pending};
    return kwl_build(&b->link, frame, &io);
}
void kwb_transfer(struct kwb *b, const uint8_t *in, size_t clocked) {
    struct kwl_io io = {b, fill, deliver, pending};
    if(kwl_receive(&b->link, in, clocked, &io) == KWL_SYNCED) forget(b);
    kwb_service(b);
}

/* Notes from other tasks. */
static void notes(struct kwb *b) {
    uint32_t n = __atomic_exchange_n(&b->notes, 0u, __ATOMIC_ACQUIRE);
    if(n & KWB_NOTE_WIFI) b->wifi_dirty = true;
    if((n & KWB_NOTE_SCAN) && b->scan_busy) {
        struct kwb_ap aps[KWB_SCAN_MAX];
        size_t count = b->pf->wifi_scan_results ? b->pf->wifi_scan_results(b->pf->ctx, aps, KWB_SCAN_MAX) : 0;
        uint8_t body[2 + KWB_SCAN_MAX * (10u + KWM_SSID_MAX)];
        size_t len = 2;
        body[0] = 0;
        body[1] = (uint8_t)count;
        for(size_t i = 0; i < count; ++i) {
            size_t ssid_len = strnlen(aps[i].ssid, KWM_SSID_MAX);
            body[len] = aps[i].channel;
            body[len + 1] = (uint8_t)aps[i].rssi;
            body[len + 2] = aps[i].auth;
            body[len + 3] = (uint8_t)ssid_len;
            memcpy(body + len + 4, aps[i].bssid, 6);
            memcpy(body + len + 10, aps[i].ssid, ssid_len);
            len += 10u + ssid_len;
        }
        reply(b, KWM_WIFI_SCAN_R, 0, body, len);
        b->scan_busy = false;
    }
    if((n & KWB_NOTE_DNS) && b->dns_busy) {
        uint8_t body[6] = {b->dns_tag, 1};
        if(b->pf->dns_result && b->pf->dns_result(b->pf->ctx, body + 2)) body[1] = 0;
        reply(b, KWM_DNS_R, 0, body, sizeof body);
        b->dns_busy = false;
    }
}

/* One listening socket per port that a slot is listening on. */
static bool listening_on(const struct kwb *b, uint16_t port) {
    for(unsigned i = 0; i < KWM_SLOTS; ++i)
        if(b->slot[i].state == KWM_LISTEN && b->slot[i].local_port == port) return true;
    return false;
}
static struct kwb_listener *listener_for(struct kwb *b, uint16_t port) {
    for(unsigned i = 0; i < KWM_SLOTS; ++i)
        if(b->listener[i].fd >= 0 && b->listener[i].port == port) return &b->listener[i];
    return NULL;
}
static void listeners(struct kwb *b) {
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwb_listener *l = &b->listener[i];
        if(l->fd >= 0 && !listening_on(b, l->port)) {
            close(l->fd);
            l->fd = -1;
        }
    }
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwb_slot *s = &b->slot[i];
        if(s->state != KWM_LISTEN || listener_for(b, s->local_port)) continue;
        struct kwb_listener *l = NULL;
        for(unsigned k = 0; k < KWM_SLOTS && !l; ++k)
            if(b->listener[k].fd < 0) l = &b->listener[k];
        int fd = socket(AF_INET, SOCK_STREAM, 0), one = 1, err = ENOMEM;
        struct sockaddr_in local = address(NULL, s->local_port);
        if(fd >= 0) {
            setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
            if(nonblocking(fd) && bind(fd, (struct sockaddr *)&local, sizeof local) == 0 && listen(fd, 4) == 0 && l) {
                l->fd = fd;
                l->port = s->local_port;
                continue;
            }
            err = l ? errno : ENOMEM;
            close(fd);
        }
        /* The port cannot be used: every slot waiting on it fails. */
        uint16_t port = s->local_port;
        for(unsigned k = 0; k < KWM_SLOTS; ++k)
            if(b->slot[k].state == KWM_LISTEN && b->slot[k].local_port == port) finish(&b->slot[k], map_errno(err), false);
    }
}
/* A connection to a listening port goes to the first slot listening on it,
 * as with the W5500; with none left it is refused. */
static void accept_on(struct kwb *b, struct kwb_listener *l) {
    for(unsigned rounds = 0; rounds < KWM_SLOTS; ++rounds) {
        struct sockaddr_in from;
        socklen_t len = sizeof from;
        int fd = accept(l->fd, (struct sockaddr *)&from, &len);
        if(fd < 0) return;
        struct kwb_slot *s = NULL;
        for(unsigned i = 0; i < KWM_SLOTS && !s; ++i)
            if(b->slot[i].state == KWM_LISTEN && b->slot[i].local_port == l->port) s = &b->slot[i];
        if(!s || !nonblocking(fd)) {
            abort_fd(fd);
            ++b->stats.refused;
            continue;
        }
        ++b->stats.accepted;
        s->fd = fd;
        memcpy(s->remote_ip, &from.sin_addr.s_addr, 4);
        s->remote_port = ntohs(from.sin_port);
        established(s);
    }
}
static void read_tcp(struct kwb_slot *s) {
    for(unsigned rounds = 0; rounds < 4 && s->fd >= 0; ++rounds) {
        size_t room;
        uint8_t *dst = ring_space(&s->rx, &room);
        if(!room) return;
        ssize_t n = recv(s->fd, dst, room, 0);
        if(n > 0) {
            s->rx.len += (size_t)n;
            continue;
        }
        if(n == 0) {
            /* The peer has finished sending. */
            s->ended = true;
            if(s->closing) {
                if(s->fin_sent) finish(s, KWM_E_NONE, false);
            } else {
                s->state = KWM_PEER_CLOSED;
                s->dirty = true;
            }
        } else if(!would_block(errno)) {
            finish(s, map_errno(errno), true);
        }
        return;
    }
}
static void write_tcp(struct kwb_slot *s) {
    while(s->tx.len && s->fd >= 0) {
        size_t n;
        const uint8_t *src = ring_data(&s->tx, &n);
        ssize_t sent = send(s->fd, src, n, MSG_NOSIGNAL);
        if(sent > 0) {
            ring_drop(&s->tx, (size_t)sent);
            s->grant += (uint32_t)sent;
            continue;
        }
        if(sent < 0 && !would_block(errno)) finish(s, map_errno(errno), true);
        return;
    }
}
static void read_udp(struct kwb *b, struct kwb_slot *s) {
    for(unsigned rounds = 0; rounds < 8; ++rounds) {
        struct sockaddr_in from;
        socklen_t len = sizeof from;
        ssize_t n = recvfrom(s->fd, b->scratch, sizeof b->scratch, 0, (struct sockaddr *)&from, &len);
        if(n < 0) return;
        if(ring_room(&s->rx) < UDP_RECORD + (size_t)n) {
            ++b->stats.dropped_datagrams;
            continue;
        }
        uint8_t head[UDP_RECORD];
        kwl_put16(head, (uint16_t)n);
        memcpy(head + 2, &from.sin_addr.s_addr, 4);
        kwl_put16(head + 6, ntohs(from.sin_port));
        ring_put(&s->rx, head, UDP_RECORD);
        ring_put(&s->rx, b->scratch, (size_t)n);
    }
}
static void write_udp(struct kwb *b, struct kwb_slot *s) {
    while(s->tx.len >= UDP_RECORD) {
        uint8_t head[UDP_RECORD];
        ring_copy(&s->tx, 0, head, UDP_RECORD);
        size_t n = kwl_get16(head);
        ring_copy(&s->tx, UDP_RECORD, b->scratch, n);
        struct sockaddr_in to = address(head + 2, kwl_get16(head + 6));
        if(sendto(s->fd, b->scratch, n, 0, (struct sockaddr *)&to, sizeof to) < 0 &&
           (would_block(errno) || errno == ENOMEM || errno == ENOBUFS))
            return; /* try again later */
        ring_drop(&s->tx, UDP_RECORD + n);
        s->grant += (uint32_t)(UDP_RECORD + n);
    }
}

void kwb_service(struct kwb *b) {
    notes(b);
    listeners(b);
    uint32_t t = now(b);
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwb_slot *s = &b->slot[i];
        if(s->state == KWM_CONNECTING && due(t, s->deadline)) finish(s, KWM_E_TIMEOUT, true);
    }
    fd_set readable, writable;
    FD_ZERO(&readable);
    FD_ZERO(&writable);
    int top = -1;
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        int fd = b->listener[i].fd;
        if(fd < 0) continue;
        FD_SET(fd, &readable);
        if(fd > top) top = fd;
    }
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwb_slot *s = &b->slot[i];
        if(s->fd < 0) continue;
        bool read = false, write = false;
        if(s->state == KWM_CONNECTING) write = true;
        else if(s->kind == KWM_UDP) {
            read = ring_room(&s->rx) > UDP_RECORD;
            write = s->tx.len != 0;
        } else {
            read = !s->ended && ring_room(&s->rx) != 0;
            write = s->tx.len != 0;
        }
        if(read) FD_SET(s->fd, &readable);
        if(write) FD_SET(s->fd, &writable);
        if((read || write) && s->fd > top) top = s->fd;
    }
    if(top >= 0) {
        struct timeval zero = {0, 0};
        if(select(top + 1, &readable, &writable, NULL, &zero) > 0) {
            for(unsigned i = 0; i < KWM_SLOTS; ++i)
                if(b->listener[i].fd >= 0 && FD_ISSET(b->listener[i].fd, &readable)) accept_on(b, &b->listener[i]);
            for(unsigned i = 0; i < KWM_SLOTS; ++i) {
                struct kwb_slot *s = &b->slot[i];
                int fd = s->fd;
                if(fd < 0) continue;
                if(s->state == KWM_CONNECTING) {
                    if(!FD_ISSET(fd, &writable)) continue;
                    int err = 0;
                    socklen_t len = sizeof err;
                    if(getsockopt(fd, SOL_SOCKET, SO_ERROR, &err, &len) < 0) err = errno;
                    if(err) finish(s, map_errno(err), true);
                    else established(s);
                    continue;
                }
                if(s->kind == KWM_UDP) {
                    if(FD_ISSET(fd, &readable)) read_udp(b, s);
                    if(FD_ISSET(fd, &writable)) write_udp(b, s);
                    continue;
                }
                if(FD_ISSET(fd, &writable)) write_tcp(s);
                if(s->fd == fd && FD_ISSET(fd, &readable)) read_tcp(s);
            }
        }
    }
    /* Orderly closes: FIN once the queued data has gone, then wait (not
     * for ever) for the peer's FIN. */
    for(unsigned i = 0; i < KWM_SLOTS; ++i) {
        struct kwb_slot *s = &b->slot[i];
        if(!s->closing || s->fd < 0) continue;
        if(!s->fin_sent && !s->tx.len) {
            shutdown(s->fd, SHUT_WR);
            s->fin_sent = true;
            s->deadline = t + CLOSE_MS;
            if(s->ended) finish(s, KWM_E_NONE, false);
        } else if(s->fin_sent && due(t, s->deadline)) {
            finish(s, KWM_E_NONE, true);
        }
    }
    if(b->reboot_due && due(t, b->reboot_at) &&
       ((!b->reply_len && !kwl_busy(&b->link)) || due(t, b->reboot_at + REBOOT_LIMIT_MS))) {
        b->reboot_due = false;
        if(b->pf->reboot) b->pf->reboot(b->pf->ctx);
    }
}
