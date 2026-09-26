/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "w5500_model.h"
#include <arpa/inet.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define RING 16384u
enum {SN_MR = 0x00, SN_CR = 0x01, SN_IR = 0x02, SN_SR = 0x03, SN_PORT = 0x04, SN_DIPR = 0x0c, SN_DPORT = 0x10,
      SN_RXBUF = 0x1e, SN_TXBUF = 0x1f, SN_FSR = 0x20, SN_TX_RD = 0x22, SN_TX_WR = 0x24, SN_RSR = 0x26,
      SN_RX_RD = 0x28, SN_RX_WR = 0x2a};
struct sock {
    uint8_t regs[0x40], tx[RING], rx[RING];
    uint16_t tx_rd, tx_wr, rx_rd, rx_wr, send_end;
    unsigned send_frame; /* a SEND goes out a few model steps after it starts */
    bool sending, discon;
    int fd;
};
static struct {
    struct w5500_model_options options;
    uint8_t common[0x40];
    struct sock s[KUI_W5500_SOCKETS];
    struct {int fd; uint16_t port;} listeners[KUI_W5500_SOCKETS];
    unsigned reads, ticks;
    uint64_t offset_ms;
} m;
struct w5500_model_counts w5500_model_counts;
const uint8_t w5500_model_lease[4] = {10, 0, 0, 2};
static const uint8_t gateway_ip[4] = {10, 0, 0, 1}, gateway_mac[6] = {0x02, 0, 0, 0, 0, 0x01};
static const uint8_t other_mac[6] = {0x02, 0, 0, 0, 0, 0x77};

static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static void put16(uint8_t *p, unsigned v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }
static unsigned rx_size(const struct sock *s) { return s->regs[SN_RXBUF] * 1024u; }
static unsigned tx_size(const struct sock *s) { return s->regs[SN_TXBUF] * 1024u; }
static uint8_t protocol(const struct sock *s) { return s->regs[SN_MR] & 0x0fu; }
static void drop(struct sock *s, bool reset) {
    if(s->fd < 0) return;
    if(reset) { struct linger l = {1, 0}; setsockopt(s->fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l)); }
    close(s->fd);
    s->fd = -1;
}
static void reset_all(void) {
    for(unsigned i = 0; i < KUI_W5500_SOCKETS; ++i) {
        drop(&m.s[i], true);
        memset(&m.s[i], 0, sizeof(m.s[i]));
        m.s[i].fd = -1;
        m.s[i].regs[SN_RXBUF] = m.s[i].regs[SN_TXBUF] = 2;
        m.s[i].regs[0x2c] = 0xff;
        if(m.listeners[i].fd >= 0) close(m.listeners[i].fd);
        m.listeners[i].fd = -1;
    }
    memset(m.common, 0, sizeof(m.common));
    put16(m.common + KUI_W5500_RTR, 2000);
    m.common[KUI_W5500_RCR] = 8;
    ++w5500_model_counts.resets;
}
void w5500_model_start(const struct w5500_model_options *options) {
    memset(&m, 0, sizeof(m));
    memset(&w5500_model_counts, 0, sizeof(w5500_model_counts));
    for(unsigned i = 0; i < KUI_W5500_SOCKETS; ++i) m.s[i].fd = m.listeners[i].fd = -1;
    if(options) m.options = *options;
    if(!m.options.lease_seconds) m.options.lease_seconds = 3600;
    reset_all();
    w5500_model_counts.resets = 0;
}
void w5500_model_stop(void) {
    reset_all();
}
void w5500_model_advance(uint64_t ms) { m.offset_ms += ms; }
struct w5500_model_options *w5500_model_live(void) { return &m.options; }

/* Checksums and frames for the model's small network. */
static uint16_t checksum(const uint8_t *p, size_t n) {
    uint32_t sum = 0;
    for(size_t i = 0; i + 1 < n; i += 2) sum += get16(p + i);
    if(n & 1) sum += (uint32_t)p[n - 1] << 8;
    while(sum >> 16) sum = (sum & 0xffff) + (sum >> 16);
    return (uint16_t)~sum;
}
static bool ring_frame(struct sock *s, const uint8_t *head, size_t head_bytes, const uint8_t *data, size_t bytes) {
    unsigned size = rx_size(s);
    size_t total = head_bytes + bytes;
    if(!size || (uint16_t)(s->rx_wr - s->rx_rd) + total > size) return false;
    for(size_t i = 0; i < total; ++i)
        s->rx[(uint16_t)(s->rx_wr + i) & (size - 1u)] = i < head_bytes ? head[i] : data[i - head_bytes];
    s->rx_wr = (uint16_t)(s->rx_wr + total);
    s->regs[SN_IR] |= KUI_W5500_IR_RECV;
    return true;
}
static void deliver_frame(const uint8_t *frame, size_t bytes) {
    struct sock *s = &m.s[0];
    if(s->regs[SN_SR] != KUI_W5500_MACRAW_OPEN) return;
    uint8_t head[2];
    put16(head, (unsigned)bytes + 2u);
    ring_frame(s, head, 2, frame, bytes);
}
static size_t dhcp_reply(const uint8_t *q, size_t n, uint8_t *r) {
    if(n < 240 || q[0] != 1 || q[1] != 1 || q[2] != 6 || get16(q + 236) != 0x6382 || get16(q + 238) != 0x5363)
        return 0;
    unsigned type = 0;
    const uint8_t *requested = NULL;
    for(size_t k = 240; k < n;) {
        unsigned tag = q[k++];
        if(!tag) continue;
        if(tag == 255 || k >= n) break;
        unsigned len = q[k++];
        if(k + len > n) return 0;
        if(tag == 53 && len == 1) type = q[k];
        if(tag == 50 && len == 4) requested = q + k;
        k += len;
    }
    if(type == 1) ++w5500_model_counts.dhcp_discovers;
    if(type == 3) ++w5500_model_counts.dhcp_requests;
    if(m.options.dhcp_silent || (type != 1 && type != 3)) return 0;
    unsigned reply = type == 1 ? 2u : 5u;
    if(type == 3) {
        const uint8_t *asked = requested ? requested : q + 12;
        if(m.options.dhcp_nak || memcmp(asked, w5500_model_lease, 4)) reply = 6;
    }
    memset(r, 0, 300);
    r[0] = 2; r[1] = 1; r[2] = 6;
    memcpy(r + 4, q + 4, 4);   /* transaction */
    memcpy(r + 10, q + 10, 2); /* flags */
    if(reply != 6) memcpy(r + 16, w5500_model_lease, 4);
    memcpy(r + 28, q + 28, 16);
    put16(r + 236, 0x6382); put16(r + 238, 0x5363);
    size_t k = 240;
    r[k++] = 53; r[k++] = 1; r[k++] = (uint8_t)reply;
    r[k++] = 54; r[k++] = 4; memcpy(r + k, gateway_ip, 4); k += 4;
    if(reply != 6) {
        static const uint8_t mask[4] = {255, 255, 255, 0}, dns[4] = {10, 0, 0, 53};
        uint32_t lease = m.options.lease_seconds;
        r[k++] = 51; r[k++] = 4; r[k++] = (uint8_t)(lease >> 24); r[k++] = (uint8_t)(lease >> 16);
        r[k++] = (uint8_t)(lease >> 8); r[k++] = (uint8_t)lease;
        r[k++] = 1; r[k++] = 4; memcpy(r + k, mask, 4); k += 4;
        r[k++] = 3; r[k++] = 4; memcpy(r + k, gateway_ip, 4); k += 4;
        r[k++] = 6; r[k++] = 4; memcpy(r + k, dns, 4); k += 4;
    }
    r[k++] = 255;
    return k < 300 ? 300 : k;
}
static void ethernet(uint8_t *f, const uint8_t *dst, const uint8_t *src, unsigned type) {
    memcpy(f, dst, 6); memcpy(f + 6, src, 6); put16(f + 12, type);
}
static void ip_header(uint8_t *ip, size_t total, unsigned proto, const uint8_t *src, const uint8_t *dst) {
    memset(ip, 0, 20);
    ip[0] = 0x45; put16(ip + 2, (unsigned)total); ip[8] = 64; ip[9] = (uint8_t)proto;
    memcpy(ip + 12, src, 4); memcpy(ip + 16, dst, 4);
    put16(ip + 10, checksum(ip, 20));
}
static void network(const uint8_t *f, size_t n) {
    static const uint8_t all[6] = {255, 255, 255, 255, 255, 255}, broadcast[4] = {255, 255, 255, 255};
    uint8_t out[1514];
    if(n >= 42 && get16(f + 12) == 0x0806 && get16(f + 20) == 1) {
        const uint8_t *sender_ip = f + 28, *target = f + 38;
        const uint8_t *owner = NULL;
        if(!memcmp(target, gateway_ip, 4)) owner = gateway_mac;
        if(!memcmp(target, w5500_model_lease, 4) && !memcmp(sender_ip, "\0\0\0\0", 4)) {
            ++w5500_model_counts.arp_probes;
            if(m.options.conflict) owner = other_mac;
        }
        if(!owner) return;
        memset(out, 0, 60);
        ethernet(out, f + 6, owner, 0x0806);
        put16(out + 14, 1); put16(out + 16, 0x0800); out[18] = 6; out[19] = 4; put16(out + 20, 2);
        memcpy(out + 22, owner, 6); memcpy(out + 28, target, 4);
        memcpy(out + 32, f + 22, 6); memcpy(out + 38, sender_ip, 4);
        deliver_frame(out, 60);
        return;
    }
    if(n < 34 || get16(f + 12) != 0x0800 || (f[14] & 0x0f) != 5) return;
    const uint8_t *ip = f + 14;
    if(ip[9] == 17 && n >= 42 + 240 && get16(f + 36) == 67) {
        size_t size = dhcp_reply(f + 42, n - 42, out + 42);
        if(!size) return;
        bool wide = get16(f + 42 + 10) & 0x8000;
        ethernet(out, wide ? all : f + 6, gateway_mac, 0x0800);
        ip_header(out + 14, size + 28, 17, gateway_ip, wide ? broadcast : w5500_model_lease);
        put16(out + 34, 67); put16(out + 36, 68); put16(out + 38, (unsigned)size + 8u); put16(out + 40, 0);
        deliver_frame(out, size + 42);
        return;
    }
    if(ip[9] == 1 && !memcmp(ip + 16, gateway_ip, 4) && n >= 42 && f[34] == 8) {
        size_t total = get16(ip + 2);
        if(total < 28 || total + 14 > n) return;
        ++w5500_model_counts.pings;
        memcpy(out, f, total + 14);
        ethernet(out, f + 6, gateway_mac, 0x0800);
        ip_header(out + 14, total, 1, gateway_ip, ip + 12);
        out[34] = 0; put16(out + 36, 0); put16(out + 36, checksum(out + 34, total - 20));
        deliver_frame(out, total + 14 < 60 ? 60 : total + 14);
    }
}

static int listener(uint16_t port) {
    for(unsigned i = 0; i < KUI_W5500_SOCKETS; ++i)
        if(m.listeners[i].fd >= 0 && m.listeners[i].port == port) return m.listeners[i].fd;
    for(unsigned i = 0; i < KUI_W5500_SOCKETS; ++i) {
        if(m.listeners[i].fd >= 0) continue;
        int fd = socket(AF_INET, SOCK_STREAM, 0), yes = 1;
        assert(fd >= 0);
        setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
        struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
        a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if(bind(fd, (struct sockaddr *)&a, sizeof(a)) || listen(fd, 8)) {
            fprintf(stderr, "model: cannot listen on port %u: %s\n", port, strerror(errno));
            close(fd);
            return -1;
        }
        fcntl(fd, F_SETFL, O_NONBLOCK);
        m.listeners[i].fd = fd;
        m.listeners[i].port = port;
        return fd;
    }
    return -1;
}
static void attach(struct sock *s, int fd) {
    fcntl(fd, F_SETFL, O_NONBLOCK);
    /* The chip has no out-of-band data: urgent bytes arrive in line. */
    int yes = 1;
    setsockopt(fd, SOL_SOCKET, SO_OOBINLINE, &yes, sizeof(yes));
    s->fd = fd;
    struct sockaddr_in peer;
    socklen_t size = sizeof(peer);
    getpeername(fd, (struct sockaddr *)&peer, &size);
    memcpy(s->regs + SN_DIPR, &peer.sin_addr.s_addr, 4);
    put16(s->regs + SN_DPORT, ntohs(peer.sin_port));
    s->regs[SN_SR] = KUI_W5500_ESTABLISHED;
    s->regs[SN_IR] |= KUI_W5500_IR_CON;
}
static void datagram(unsigned n) {
    struct sock *s = &m.s[n];
    unsigned size = tx_size(s);
    size_t bytes = (uint16_t)(s->tx_wr - s->tx_rd);
    uint8_t q[RING], r[600];
    for(size_t i = 0; i < bytes; ++i) q[i] = s->tx[(uint16_t)(s->tx_rd + i) & (size - 1u)];
    s->tx_rd = s->tx_wr;
    s->regs[SN_IR] |= KUI_W5500_IR_SENDOK;
    if(protocol(s) == KUI_W5500_MACRAW) { network(q, bytes); return; }
    if(get16(s->regs + SN_DPORT) != 67) return;
    ++w5500_model_counts.udp_requests;
    size_t reply = dhcp_reply(q, bytes, r);
    if(!reply) return;
    uint8_t head[8];
    memcpy(head, gateway_ip, 4); put16(head + 4, 67); put16(head + 6, (unsigned)reply);
    ring_frame(s, head, 8, r, reply);
}
static void command(unsigned n, uint8_t code) {
    struct sock *s = &m.s[n];
    uint8_t state = s->regs[SN_SR];
    ++w5500_model_counts.commands;
    switch(code) {
    case KUI_W5500_OPEN:
        drop(s, true);
        s->tx_rd = s->tx_wr = s->rx_rd = s->rx_wr = 0;
        s->sending = s->discon = false;
        s->regs[SN_IR] = 0;
        s->regs[SN_SR] = protocol(s) == KUI_W5500_TCP ? KUI_W5500_INIT : protocol(s) == KUI_W5500_UDP ?
            KUI_W5500_UDP_OPEN : protocol(s) == KUI_W5500_MACRAW && !n ? KUI_W5500_MACRAW_OPEN : KUI_W5500_CLOSED;
        break;
    case KUI_W5500_LISTEN:
        if(state == KUI_W5500_INIT && listener(get16(s->regs + SN_PORT)) >= 0) s->regs[SN_SR] = KUI_W5500_LISTENING;
        break;
    case KUI_W5500_CONNECT:
        if(state == KUI_W5500_INIT) {
            int fd = socket(AF_INET, SOCK_STREAM, 0);
            struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(get16(s->regs + SN_DPORT))};
            memcpy(&a.sin_addr.s_addr, s->regs + SN_DIPR, 4);
            if(fd >= 0 && !connect(fd, (struct sockaddr *)&a, sizeof(a))) attach(s, fd);
            else {
                if(fd >= 0) close(fd);
                s->regs[SN_SR] = KUI_W5500_CLOSED;
                s->regs[SN_IR] |= KUI_W5500_IR_TIMEOUT;
            }
        }
        break;
    case KUI_W5500_DISCON:
        if(state == KUI_W5500_ESTABLISHED || state == KUI_W5500_CLOSE_WAIT) s->discon = true;
        else if(state == KUI_W5500_LISTENING || state == KUI_W5500_INIT || state == KUI_W5500_SYNSENT) {
            drop(s, true);
            s->regs[SN_SR] = KUI_W5500_CLOSED;
        }
        break;
    case KUI_W5500_CLOSE:
        drop(s, true);
        s->regs[SN_SR] = KUI_W5500_CLOSED;
        s->sending = s->discon = false;
        break;
    case KUI_W5500_SEND:
        if(state == KUI_W5500_UDP_OPEN || state == KUI_W5500_MACRAW_OPEN) datagram(n);
        else if(state == KUI_W5500_ESTABLISHED || state == KUI_W5500_CLOSE_WAIT) {
            /* Each SEND must wait for the previous one's SEND_OK. */
            assert(!s->sending);
            s->send_end = s->tx_wr;
            s->send_frame = m.ticks;
            s->sending = true;
        }
        break;
    case KUI_W5500_RECV:
        break;
    default:
        assert(!"unknown W5500 command");
    }
}
static void closed(struct sock *s, uint8_t events) {
    drop(s, false);
    s->regs[SN_SR] = KUI_W5500_CLOSED;
    s->regs[SN_IR] |= events;
    s->sending = s->discon = false;
}
static void tick(void) {
    ++m.ticks;
    for(unsigned i = 0; i < KUI_W5500_SOCKETS; ++i) {
        if(m.listeners[i].fd < 0) continue;
        for(;;) {
            int fd = accept(m.listeners[i].fd, NULL, NULL);
            if(fd < 0) break;
            struct sock *to = NULL;
            for(unsigned n = 0; n < KUI_W5500_SOCKETS && !to; ++n)
                if(m.s[n].regs[SN_SR] == KUI_W5500_LISTENING && get16(m.s[n].regs + SN_PORT) == m.listeners[i].port)
                    to = &m.s[n];
            if(to) { attach(to, fd); ++w5500_model_counts.accepted; }
            else {
                struct linger l = {1, 0};
                setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
                close(fd);
                ++w5500_model_counts.refused;
            }
        }
        /* No socket listens on the port any more: connections are refused,
         * as the chip refuses them. */
        bool listening = false;
        for(unsigned n = 0; n < KUI_W5500_SOCKETS && !listening; ++n)
            listening = m.s[n].regs[SN_SR] == KUI_W5500_LISTENING && get16(m.s[n].regs + SN_PORT) == m.listeners[i].port;
        if(!listening) { close(m.listeners[i].fd); m.listeners[i].fd = -1; }
    }
    for(unsigned n = 0; n < KUI_W5500_SOCKETS; ++n) {
        struct sock *s = &m.s[n];
        if(s->fd < 0) continue;
        unsigned size = tx_size(s);
        bool due = m.ticks - s->send_frame >= 3u;
        while(s->sending && due && s->tx_rd != s->send_end) {
            unsigned at = s->tx_rd & (size - 1u), run = (uint16_t)(s->send_end - s->tx_rd);
            if(run > size - at) run = size - at;
            ssize_t sent = send(s->fd, s->tx + at, run, MSG_DONTWAIT | MSG_NOSIGNAL);
            if(sent > 0) { s->tx_rd = (uint16_t)(s->tx_rd + sent); w5500_model_counts.sent_bytes += (uint64_t)sent; }
            else if(sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
            else { closed(s, KUI_W5500_IR_DISCON | KUI_W5500_IR_TIMEOUT); break; }
        }
        if(s->fd < 0) continue;
        if(s->sending && due && s->tx_rd == s->send_end) { s->sending = false; s->regs[SN_IR] |= KUI_W5500_IR_SENDOK; }
        if(s->discon && !s->sending) {
            s->discon = false;
            if(s->regs[SN_SR] == KUI_W5500_ESTABLISHED) { shutdown(s->fd, SHUT_WR); s->regs[SN_SR] = KUI_W5500_FIN_WAIT; }
            else { closed(s, KUI_W5500_IR_DISCON); continue; }
        }
        uint8_t state = s->regs[SN_SR];
        if(state != KUI_W5500_ESTABLISHED && state != KUI_W5500_FIN_WAIT) continue;
        unsigned rsize = rx_size(s);
        for(;;) {
            unsigned used = (uint16_t)(s->rx_wr - s->rx_rd), at = s->rx_wr & (rsize - 1u);
            if(used >= rsize) break;
            unsigned room = rsize - used;
            if(room > rsize - at) room = rsize - at;
            ssize_t got = recv(s->fd, s->rx + at, room, MSG_DONTWAIT);
            if(got > 0) {
                s->rx_wr = (uint16_t)(s->rx_wr + got);
                s->regs[SN_IR] |= KUI_W5500_IR_RECV;
                w5500_model_counts.received_bytes += (uint64_t)got;
            } else if(got == 0) {
                /* The peer's FIN. */
                if(s->regs[SN_SR] == KUI_W5500_ESTABLISHED) {
                    s->regs[SN_SR] = KUI_W5500_CLOSE_WAIT;
                    s->regs[SN_IR] |= KUI_W5500_IR_DISCON;
                } else closed(s, KUI_W5500_IR_DISCON);
                break;
            } else {
                if(errno != EAGAIN && errno != EWOULDBLOCK) closed(s, KUI_W5500_IR_DISCON);
                break;
            }
        }
    }
}

static uint8_t common_get(uint16_t a) {
    if(a == KUI_W5500_MR) return m.common[a] & 0x7fu;
    if(a == KUI_W5500_PHYCFGR) return m.options.link_down ? 0xb8u : 0xbfu;
    if(a == KUI_W5500_VERSIONR) return KUI_W5500_VERSION;
    return a < sizeof(m.common) ? m.common[a] : 0;
}
static void common_put(uint16_t a, uint8_t v) {
    if(a == KUI_W5500_MR && (v & KUI_W5500_MR_RST)) { reset_all(); return; }
    if(a < sizeof(m.common)) m.common[a] = v;
}
static uint8_t socket_get(struct sock *s, uint16_t a) {
    unsigned tx = tx_size(s);
    uint16_t free_bytes = (uint16_t)(tx - (uint16_t)(s->tx_wr - s->tx_rd)), waiting = (uint16_t)(s->rx_wr - s->rx_rd);
    switch(a) {
    case SN_CR: return 0;
    case SN_FSR: return (uint8_t)(free_bytes >> 8);
    case SN_FSR + 1: return (uint8_t)free_bytes;
    case SN_TX_RD: return (uint8_t)(s->tx_rd >> 8);
    case SN_TX_RD + 1: return (uint8_t)s->tx_rd;
    case SN_TX_WR: return (uint8_t)(s->tx_wr >> 8);
    case SN_TX_WR + 1: return (uint8_t)s->tx_wr;
    case SN_RSR: return (uint8_t)(waiting >> 8);
    case SN_RSR + 1: return (uint8_t)waiting;
    case SN_RX_RD: return (uint8_t)(s->rx_rd >> 8);
    case SN_RX_RD + 1: return (uint8_t)s->rx_rd;
    case SN_RX_WR: return (uint8_t)(s->rx_wr >> 8);
    case SN_RX_WR + 1: return (uint8_t)s->rx_wr;
    default: return a < sizeof(s->regs) ? s->regs[a] : 0;
    }
}
static void socket_put(unsigned n, uint16_t a, uint8_t v) {
    struct sock *s = &m.s[n];
    switch(a) {
    case SN_CR: command(n, v); break;
    case SN_IR: s->regs[SN_IR] &= (uint8_t)~v; break;
    case SN_SR: case SN_FSR: case SN_FSR + 1: case SN_TX_RD: case SN_TX_RD + 1:
    case SN_RSR: case SN_RSR + 1: case SN_RX_WR: case SN_RX_WR + 1: break;
    case SN_TX_WR: s->tx_wr = (uint16_t)(v << 8 | (s->tx_wr & 0xff)); break;
    case SN_TX_WR + 1: s->tx_wr = (uint16_t)((s->tx_wr & 0xff00) | v); break;
    case SN_RX_RD: s->rx_rd = (uint16_t)(v << 8 | (s->rx_rd & 0xff)); break;
    case SN_RX_RD + 1: s->rx_rd = (uint16_t)((s->rx_rd & 0xff00) | v); break;
    case SN_RXBUF: case SN_TXBUF:
        assert(v == 0 || v == 1 || v == 2 || v == 4 || v == 8 || v == 16);
        s->regs[a] = v;
        break;
    default: if(a < sizeof(s->regs)) s->regs[a] = v;
    }
}
static bool frame(void *ctx, const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes) {
    (void)ctx;
    tick();
    ++w5500_model_counts.frames;
    uint16_t address = get16(header);
    unsigned block = header[2] >> 3;
    bool write = header[2] & 4u;
    assert((header[2] & 3u) == 0);          /* variable-length frames only */
    assert(write ? out && !in : in && !out);
    assert(block == 0 || (block - 1u) % 4u != 3u); /* reserved blocks */
    unsigned n = block ? (block - 1u) / 4u : 0, kind = block ? (block - 1u) % 4u : 3u;
    assert(n < KUI_W5500_SOCKETS);
    for(size_t i = 0; i < bytes; ++i) {
        uint16_t a = (uint16_t)(address + i);
        struct sock *s = &m.s[n];
        if(write) {
            if(m.options.absent) continue;
            if(kind == 3) common_put(a, out[i]);
            else if(kind == 0) socket_put(n, a, out[i]);
            else if(kind == 1 && tx_size(s)) s->tx[a & (tx_size(s) - 1u)] = out[i];
            else if(kind == 2 && rx_size(s)) s->rx[a & (rx_size(s) - 1u)] = out[i];
        } else {
            uint8_t v = kind == 3 ? common_get(a) : kind == 0 ? socket_get(s, a) :
                kind == 1 ? (tx_size(s) ? s->tx[a & (tx_size(s) - 1u)] : 0) : (rx_size(s) ? s->rx[a & (rx_size(s) - 1u)] : 0);
            if(m.options.absent) v = 0xff;
            if(m.options.corrupt_reads && ++m.reads % 97u == 0) v ^= 0x10;
            in[i] = v;
        }
    }
    return true;
}
static uint64_t now_ms(void *ctx) {
    (void)ctx;
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000u + (uint64_t)t.tv_nsec / 1000000u + m.offset_ms;
}
static void pause_ms(void *ctx, unsigned ms) {
    (void)ctx;
    tick();
    usleep(ms ? ms * 1000u : 100u);
}
const struct kui_w5500_bus w5500_model_bus = {NULL, frame, now_ms, pause_ms};
