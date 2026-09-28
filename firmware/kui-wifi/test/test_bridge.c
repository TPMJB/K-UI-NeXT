/* SPDX-License-Identifier: MIT */
/* End to end on one machine: the Dreamcast's link library (kwhost) talks to
 * the board's core (bridge) over a simulated SPI bus, and the core uses real
 * sockets on localhost, where this test plays the computer on the network. */
#include "bridge.h"
#include "kwhost.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define CHECK(c) do { if(!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while(0)
static const uint8_t LOCALHOST[4] = {127, 0, 0, 1};

static uint32_t rng = 777u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }
static uint8_t pattern(unsigned stream, size_t i) { return (uint8_t)(i * 131u + stream * 7u + (i >> 11)); }
static uint32_t ms_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

/* The board's platform, faked. */
struct fake {
    struct kwb *bridge;
    struct kwb_wifi wifi;
    char joined_ssid[33], joined_password[65];
    bool joined_save;
    uint8_t joined_band;
    /* Lookups end at once, or, held, only when the test ends them. */
    bool dns_hold;
    unsigned dns_started, dns_done, dns_ended;
    struct { uint32_t ticket; bool found; } dns[4];
    uint8_t *ota;
    uint32_t ota_size, ota_written;
    bool ota_open;
    int reboots;
};
static struct fake fake;
static uint32_t fake_now(void *ctx) { (void)ctx; return ms_now(); }
static void fake_info(void *ctx, struct kwb_info *out) {
    (void)ctx;
    static const uint8_t mac[6] = {0x02, 0x4b, 0x55, 0x49, 0x00, 0x05};
    memcpy(out->mac, mac, 6);
    out->chip = 5;
    strcpy(out->version, "test-1");
}
static void fake_wifi(void *ctx, struct kwb_wifi *out) { *out = ((struct fake *)ctx)->wifi; }
static bool fake_scan(void *ctx) {
    kwb_notify(((struct fake *)ctx)->bridge, KWB_NOTE_SCAN);
    return true;
}
static size_t fake_scan_results(void *ctx, struct kwb_ap *out, size_t max) {
    (void)ctx;
    static const struct kwb_ap aps[3] = {{"Home 5G", {1, 2, 3, 4, 5, 6}, 36, 3, -48},
                                         {"Home", {1, 2, 3, 4, 5, 7}, 6, 3, -52},
                                         {"Neighbour's network name, 32 ch", {9, 9, 9, 9, 9, 9}, 11, 4, -80}};
    size_t n = max < 3 ? max : 3;
    memcpy(out, aps, n * sizeof *out);
    return n;
}
static uint8_t fake_join(void *ctx, const char *ssid, const char *password, bool save, uint8_t band) {
    struct fake *f = ctx;
    strcpy(f->joined_ssid, ssid);
    strcpy(f->joined_password, password);
    f->joined_save = save;
    f->joined_band = band;
    strcpy(f->wifi.ssid, ssid);
    f->wifi.state = KWM_WIFI_ONLINE;
    kwb_notify(f->bridge, KWB_NOTE_WIFI);
    return 0;
}
static void fake_leave(void *ctx, bool forget) {
    struct fake *f = ctx;
    f->wifi.state = KWM_WIFI_IDLE;
    if(forget) f->wifi.saved = 0;
    kwb_notify(f->bridge, KWB_NOTE_WIFI);
}
static bool fake_band(void *ctx, uint8_t band) {
    struct fake *f = ctx;
    if(band != KWM_BAND_24 && band != KWM_BAND_5 && band != KWM_BAND_BOTH) return false;
    f->wifi.band_mode = band;
    return true;
}
static void fake_dns_end(struct fake *f) {
    CHECK(f->dns_ended < f->dns_started);
    ++f->dns_ended;
    kwb_notify(f->bridge, KWB_NOTE_DNS);
}
static bool fake_dns_start(void *ctx, uint32_t ticket, const char *name) {
    struct fake *f = ctx;
    CHECK(f->dns_started < 4);
    f->dns[f->dns_started].ticket = ticket;
    f->dns[f->dns_started].found = !strcmp(name, "localhost");
    ++f->dns_started;
    if(!f->dns_hold) fake_dns_end(f);
    return true;
}
static bool fake_dns_result(void *ctx, uint32_t *ticket, bool *found, uint8_t ip[4]) {
    struct fake *f = ctx;
    if(f->dns_done == f->dns_ended) return false;
    *ticket = f->dns[f->dns_done].ticket;
    *found = f->dns[f->dns_done].found;
    memcpy(ip, LOCALHOST, 4);
    /* Four at most in this test: start again once all have been taken. */
    if(++f->dns_done == f->dns_started) f->dns_done = f->dns_ended = f->dns_started = 0;
    return true;
}
static bool fake_time(void *ctx, uint64_t *ms) {
    (void)ctx;
    *ms = 1790000000123ull;
    return true;
}
static uint8_t fake_ota_begin(void *ctx, uint32_t size, const uint8_t sha256[32]) {
    struct fake *f = ctx;
    (void)sha256;
    free(f->ota);
    f->ota = malloc(size);
    f->ota_size = size;
    f->ota_written = 0;
    f->ota_open = f->ota != NULL;
    return f->ota_open ? 0 : 1;
}
static uint8_t fake_ota_write(void *ctx, uint32_t offset, const uint8_t *data, size_t len) {
    struct fake *f = ctx;
    if(!f->ota_open || offset != f->ota_written || len > f->ota_size - offset) return 1;
    memcpy(f->ota + offset, data, len);
    f->ota_written += (uint32_t)len;
    return 0;
}
static uint8_t fake_ota_end(void *ctx) {
    struct fake *f = ctx;
    bool ok = f->ota_open && f->ota_written == f->ota_size;
    f->ota_open = false;
    return ok ? 0 : 1;
}
static void fake_reboot(void *ctx) { ++((struct fake *)ctx)->reboots; }
static const struct kwb_platform platform = {&fake, fake_now, fake_info, fake_wifi, fake_scan, fake_scan_results,
                                             fake_join, fake_leave, fake_band, fake_dns_start, fake_dns_result,
                                             fake_time, fake_ota_begin, fake_ota_write, fake_ota_end, fake_reboot};

/* The SPI bus between them. The board arms its next frame as soon as a
 * transfer ends, as the firmware does. */
struct bus {
    struct kwb *bridge;
    uint8_t armed[KWL_FRAME_MAX], bridge_in[KWL_FRAME_MAX];
    unsigned flip_permille;
};
static void flip(uint8_t *b, size_t len, unsigned permille) {
    if(permille && rnd() % 1000u < permille) b[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8u));
}
static void arm(struct bus *b) {
    size_t n = kwb_frame(b->bridge, b->armed);
    memset(b->armed + n, 0, KWL_FRAME_MAX - n);
}
static bool bus_transfer(void *ctx, const uint8_t *out, uint8_t *in, size_t len) {
    struct bus *b = ctx;
    CHECK(len % 4u == 0 && len >= KWL_HEADER && len <= KWL_FRAME_MAX);
    memcpy(in, b->armed, len);
    memcpy(b->bridge_in, out, len);
    flip(in, len, b->flip_permille);
    flip(b->bridge_in, len, b->flip_permille);
    kwb_transfer(b->bridge, b->bridge_in, len);
    arm(b);
    return true;
}

static struct kwb *bridge;
static struct kwh *host;
static struct bus bus;
static const struct kwh_bus host_bus = {&bus, bus_transfer};
/* One host transfer, and the board's idle work between transfers. */
static void step(void) {
    CHECK(kwh_step(host));
    kwb_service(bridge);
}
#define UNTIL(cond, limit) do { unsigned n_ = 0; while(!(cond)) { CHECK(++n_ < (limit)); step(); } } while(0)

/* The computer's side. */
static int tcp_client(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(fd >= 0);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(connect(fd, (struct sockaddr *)&a, sizeof a) == 0);
    CHECK(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == 0);
    return fd;
}
static uint16_t base_port;

/* Both ways at once: `bytes` from the client to the host and back. */
struct pump {
    int fd;
    unsigned slot;
    size_t bytes, client_sent, client_got, host_sent, host_got;
    unsigned up, down; /* pattern streams: client to host, host to client */
};
static void pump_once(struct pump *p) {
    uint8_t buf[8192];
    if(p->client_sent < p->bytes) {
        size_t n = p->bytes - p->client_sent < sizeof buf ? p->bytes - p->client_sent : sizeof buf;
        for(size_t i = 0; i < n; ++i) buf[i] = pattern(p->up, p->client_sent + i);
        ssize_t w = send(p->fd, buf, n, MSG_NOSIGNAL);
        if(w > 0) p->client_sent += (size_t)w;
    }
    ssize_t r = recv(p->fd, buf, sizeof buf, 0);
    if(r > 0) {
        for(ssize_t i = 0; i < r; ++i) CHECK(buf[i] == pattern(p->down, p->client_got + (size_t)i));
        p->client_got += (size_t)r;
    }
    size_t n = kwh_recv(host, p->slot, buf, sizeof buf);
    for(size_t i = 0; i < n; ++i) CHECK(buf[i] == pattern(p->up, p->host_got + i));
    p->host_got += n;
    if(p->host_sent < p->bytes) {
        size_t room = kwh_room(host, p->slot), want = p->bytes - p->host_sent;
        if(room > want) room = want;
        if(room > sizeof buf) room = sizeof buf;
        for(size_t i = 0; i < room; ++i) buf[i] = pattern(p->down, p->host_sent + i);
        p->host_sent += kwh_send(host, p->slot, buf, room);
    }
}
static unsigned exchange(int fd, unsigned slot, size_t bytes, unsigned up, unsigned down) {
    struct pump p = {fd, slot, bytes, 0, 0, 0, 0, up, down};
    unsigned steps = 0;
    uint32_t start = ms_now();
    while(p.host_got < bytes || p.client_got < bytes) {
        pump_once(&p);
        step();
        CHECK(++steps < 200000u);
    }
    printf("  (%u ms)\n", (unsigned)(ms_now() - start));
    return steps;
}

static void start(void) {
    kwh_start(host, 0x2345);
    UNTIL(host->ready, 20);
    CHECK(host->hello.protocol == KWL_PROTOCOL && host->hello.slots == 8 && host->hello.chip == 5);
    CHECK(host->hello.max_payload == KWL_PAYLOAD_MAX && !strcmp(host->hello.version, "test-1"));
    CHECK(host->hello.mac[0] == 0x02 && host->hello.mac[5] == 0x05);
    printf("hello: version %s after %u transfers\n", host->hello.version, (unsigned)host->transfers);
}

static void wifi_controls(void) {
    uint32_t before = host->counts.wifi;
    CHECK(kwh_wifi_query(host));
    UNTIL(host->counts.wifi > before, 20);
    CHECK(host->wifi.state == KWM_WIFI_ONLINE && host->wifi.band == 5 && host->wifi.channel == 36);
    CHECK(!memcmp(host->wifi.ip, LOCALHOST, 4) && !strcmp(host->wifi.ssid, "Home 5G") && host->wifi.rssi == -48);
    CHECK(kwh_wifi_scan(host));
    UNTIL(host->counts.scan, 20);
    CHECK(host->scan_status == 0 && host->scan_count == 3);
    CHECK(!strcmp(host->scan[0].ssid, "Home 5G") && host->scan[0].channel == 36 && host->scan[0].rssi == -48);
    CHECK(!strcmp(host->scan[2].ssid, "Neighbour's network name, 32 ch") && host->scan[2].bssid[5] == 9);
    before = host->counts.wifi;
    CHECK(kwh_wifi_join(host, "Other network", "correct horse", true, KWM_BAND_5));
    UNTIL(host->counts.join && host->counts.wifi > before && !strcmp(host->wifi.ssid, "Other network"), 20);
    CHECK(host->join_status == 0 && !strcmp(fake.joined_password, "correct horse") && fake.joined_save);
    CHECK(fake.joined_band == KWM_BAND_5);
    CHECK(!kwh_wifi_join(host, "", "x", false, 0));
    before = host->counts.wifi;
    CHECK(kwh_wifi_leave(host, true));
    UNTIL(host->counts.wifi > before && host->wifi.state == KWM_WIFI_IDLE, 20);
    fake.wifi.state = KWM_WIFI_ONLINE;
    before = host->counts.wifi;
    CHECK(kwh_wifi_band(host, KWM_BAND_5));
    UNTIL(host->counts.wifi > before && host->wifi.band_mode == KWM_BAND_5, 20);
    before = host->counts.wifi;
    CHECK(kwh_wifi_band(host, 9));
    UNTIL(host->counts.wifi > before, 20);
    CHECK(host->wifi.band_mode == KWM_BAND_5);
    uint8_t sample[KWM_BODY_MAX];
    for(size_t i = 0; i < sizeof sample; ++i) sample[i] = pattern(8, i);
    CHECK(kwh_echo(host, sample, sizeof sample));
    UNTIL(host->counts.echo, 20);
    CHECK(host->echo_len == sizeof sample && !memcmp(host->echo, sample, sizeof sample));
    CHECK(kwh_dns(host, 7, "localhost"));
    UNTIL(host->counts.dns, 20);
    CHECK(host->dns_tag == 7 && host->dns_status == 0 && !memcmp(host->dns_ip, LOCALHOST, 4));
    CHECK(kwh_dns(host, 8, "nowhere.invalid"));
    UNTIL(host->counts.dns == 2, 20);
    CHECK(host->dns_tag == 8 && host->dns_status != 0);
    CHECK(kwh_time(host));
    UNTIL(host->counts.time, 20);
    CHECK(host->time_status == 0 && host->time_ms == 1790000000123ull);
    printf("wifi: status, scan, join, leave, band, echo, dns and time answered\n");
}

static void tcp_listen(void) {
    uint16_t port = base_port;
    CHECK(kwh_listen(host, 3, port, KWM_NODELAY));
    CHECK(kwh_listen(host, 4, port, 0));
    UNTIL(kwh_idle(host), 20);
    CHECK(kwh_state(host, 3) == KWM_LISTEN && kwh_state(host, 4) == KWM_LISTEN);
    int a = tcp_client(port);
    UNTIL(kwh_state(host, 3) == KWM_ESTABLISHED, 200);
    CHECK(kwh_events(host, 3) & KWH_EV_OPEN);
    uint8_t ip[4];
    uint16_t peer;
    kwh_peer(host, 3, ip, &peer);
    CHECK(!memcmp(ip, LOCALHOST, 4) && peer);
    int b = tcp_client(port);
    UNTIL(kwh_state(host, 4) == KWM_ESTABLISHED, 200);
    /* Nobody left listening on the port: a third client is refused. */
    int c = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    for(unsigned i = 0; i < 20; ++i) step();
    CHECK(connect(c, (struct sockaddr *)&addr, sizeof addr) < 0 && errno == ECONNREFUSED);
    close(c);
    unsigned steps = exchange(a, 3, 1u << 20, 1, 2);
    printf("tcp: 1 MiB each way on one connection in %u transfers\n", steps);
    /* Everything sent has left the board's buffer once its credit is back. */
    UNTIL(kwh_flushed(host, 3), 50);
    CHECK(host->slot[3].tx_capacity == 8192);
    CHECK(kwh_send(host, 3, "x", 1) == 1 && !kwh_flushed(host, 3));
    UNTIL(kwh_flushed(host, 3), 50);
    char one;
    for(unsigned i = 0; recv(a, &one, 1, 0) != 1; ++i) {
        CHECK(i < 200);
        step();
    }
    CHECK(one == 'x');
    /* Orderly close from our side: the client reads everything, then EOF. */
    CHECK(kwh_close(host, 3, true));
    CHECK(kwh_state(host, 3) == KWM_CLOSING && kwh_room(host, 3) == 0);
    char eof;
    unsigned n = 0;
    ssize_t r;
    while((r = recv(a, &eof, 1, 0)) != 0) {
        CHECK(r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK));
        step();
        CHECK(++n < 1000);
    }
    close(a);
    UNTIL(kwh_state(host, 3) == KWM_CLOSED, 1000);
    CHECK(kwh_error(host, 3) == KWM_E_NONE && (kwh_events(host, 3) & KWH_EV_CLOSED));
    /* The client finishes first: all its data arrives before the close. */
    uint8_t buf[4096];
    size_t total = 100000, sent = 0, got = 0;
    while(sent < total) {
        size_t k = total - sent < sizeof buf ? total - sent : sizeof buf;
        for(size_t i = 0; i < k; ++i) buf[i] = pattern(3, sent + i);
        ssize_t w = send(b, buf, k, MSG_NOSIGNAL);
        if(w > 0) sent += (size_t)w;
        step();
    }
    shutdown(b, SHUT_WR);
    bool seen_close = false;
    for(n = 0; !seen_close || kwh_available(host, 4); ++n) {
        CHECK(n < 5000);
        if(kwh_events(host, 4) & KWH_EV_PEER_CLOSED) {
            /* By now every byte is here, read or not. */
            CHECK(got + kwh_available(host, 4) == total);
            seen_close = true;
        }
        size_t k = kwh_recv(host, 4, buf, sizeof buf);
        for(size_t i = 0; i < k; ++i) CHECK(buf[i] == pattern(3, got + i));
        got += k;
        step();
    }
    CHECK(got == total && kwh_state(host, 4) == KWM_PEER_CLOSED);
    /* We can still answer after the peer's FIN, then close. */
    CHECK(kwh_send(host, 4, "bye", 3) == 3);
    CHECK(kwh_close(host, 4, true));
    char reply[8];
    size_t reply_len = 0;
    for(n = 0; reply_len < 3; ++n) {
        CHECK(n < 1000);
        r = recv(b, reply + reply_len, sizeof reply - reply_len, 0);
        if(r > 0) reply_len += (size_t)r;
        step();
    }
    CHECK(!memcmp(reply, "bye", 3));
    UNTIL(kwh_state(host, 4) == KWM_CLOSED, 1000);
    close(b);
    printf("tcp: listeners, refusal, orderly closes both ways\n");
}

static void tcp_connect(void) {
    uint16_t port = (uint16_t)(base_port + 1);
    int server = socket(AF_INET, SOCK_STREAM, 0), one = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    struct sockaddr_in a = {0};
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(server, (struct sockaddr *)&a, sizeof a) == 0 && listen(server, 1) == 0);
    CHECK(kwh_connect(host, 0, LOCALHOST, port, KWM_KEEPALIVE));
    UNTIL(kwh_state(host, 0) == KWM_ESTABLISHED, 200);
    int fd = accept(server, NULL, NULL);
    CHECK(fd >= 0 && fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == 0);
    unsigned steps = exchange(fd, 0, 300000, 4, 5);
    close(fd);
    UNTIL(kwh_state(host, 0) == KWM_PEER_CLOSED, 1000);
    CHECK(kwh_close(host, 0, true));
    UNTIL(kwh_state(host, 0) == KWM_CLOSED, 1000);
    close(server);
    /* Nothing listening: refused. */
    CHECK(kwh_connect(host, 0, LOCALHOST, port, 0));
    UNTIL(kwh_state(host, 0) == KWM_CLOSED, 1000);
    CHECK(kwh_error(host, 0) == KWM_E_REFUSED);
    printf("tcp: outgoing connection (%u transfers for 300 KB each way), refusal\n", steps);
}

static void udp(void) {
    uint16_t port = (uint16_t)(base_port + 2);
    CHECK(kwh_udp(host, 1, port));
    UNTIL(kwh_room(host, 1) > 1000, 50);
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    struct sockaddr_in to = {0}, local = {0};
    to.sin_family = AF_INET;
    to.sin_port = htons(port);
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    socklen_t len = sizeof local;
    CHECK(sendto(fd, "ping", 4, 0, (struct sockaddr *)&to, sizeof to) == 4);
    CHECK(getsockname(fd, (struct sockaddr *)&local, &len) == 0);
    uint8_t ip[4], data[64];
    uint16_t from = 0;
    size_t n = 0;
    for(unsigned i = 0; !n; ++i) {
        CHECK(i < 200);
        step();
        n = kwh_recvfrom(host, 1, ip, &from, data, sizeof data);
    }
    CHECK(n == 4 && !memcmp(data, "ping", 4) && !memcmp(ip, LOCALHOST, 4) && from == ntohs(local.sin_port));
    CHECK(kwh_sendto(host, 1, ip, from, "pong!", 5));
    char got[16];
    ssize_t r = -1;
    CHECK(fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK) == 0);
    for(unsigned i = 0; r < 0; ++i) {
        CHECK(i < 200);
        step();
        r = recv(fd, got, sizeof got, 0);
    }
    CHECK(r == 5 && !memcmp(got, "pong!", 5));
    /* Twenty datagrams in a burst arrive whole and in order. */
    for(unsigned i = 0; i < 20; ++i) {
        uint8_t d[300];
        memset(d, (int)i, sizeof d);
        CHECK(sendto(fd, d, 100u + i * 10u, 0, (struct sockaddr *)&to, sizeof to) == (ssize_t)(100u + i * 10u));
    }
    for(unsigned i = 0, tries = 0; i < 20; ++tries) {
        CHECK(tries < 2000);
        step();
        uint8_t d[400];
        while((n = kwh_recvfrom(host, 1, NULL, NULL, d, sizeof d))) {
            CHECK(n == 100u + i * 10u && d[0] == i && d[n - 1] == i);
            ++i;
        }
    }
    close(fd);
    CHECK(kwh_close(host, 1, false) && kwh_state(host, 1) == KWM_CLOSED);
    printf("udp: datagrams both ways\n");
}

static void noisy(void) {
    uint16_t port = (uint16_t)(base_port + 3);
    CHECK(kwh_listen(host, 2, port, 0));
    UNTIL(kwh_idle(host), 50);
    int fd = tcp_client(port);
    UNTIL(kwh_state(host, 2) == KWM_ESTABLISHED, 500);
    bus.flip_permille = 60;
    unsigned steps = exchange(fd, 2, 400000, 6, 7);
    bus.flip_permille = 0;
    printf("noise: 400 KB each way with bit errors in 6%% of transfers: %u transfers, %u+%u frames resent\n", steps,
           (unsigned)host->link.stats.resent, (unsigned)bridge->link.stats.resent);
    CHECK(kwh_close(host, 2, false));
    close(fd);
}

/* The board forgets everything (reset line): the host notices, every slot
 * closes, and a new session starts by itself. */
static void board_reset(void) {
    uint16_t port = (uint16_t)(base_port + 4);
    CHECK(kwh_listen(host, 5, port, 0));
    UNTIL(kwh_idle(host), 50);
    int fd = tcp_client(port);
    UNTIL(kwh_state(host, 5) == KWM_ESTABLISHED, 500);
    kwh_events(host, 5);
    uint32_t lost = host->counts.lost, hellos = host->counts.hello;
    kwb_reset(bridge);
    UNTIL(host->counts.lost > lost, 20);
    CHECK(kwh_state(host, 5) == KWM_CLOSED && (kwh_events(host, 5) & KWH_EV_CLOSED));
    UNTIL(host->ready && host->counts.hello > hellos, 50);
    /* The client's connection was reset. */
    char c;
    ssize_t r;
    for(unsigned i = 0; (r = recv(fd, &c, 1, 0)) < 0 && (errno == EAGAIN || errno == EWOULDBLOCK); ++i) {
        CHECK(i < 1000);
        step();
    }
    CHECK(r <= 0);
    close(fd);
    printf("reset: host resynchronised, sockets closed\n");
}

/* A lookup still running when the link resets ends after it: its result must
 * not answer the next request. */
static void dns_across_reset(void) {
    fake.dns_hold = true;
    CHECK(kwh_dns(host, 21, "localhost"));
    UNTIL(fake.dns_started == 1, 20);
    uint32_t hellos = host->counts.hello, answers = host->counts.dns;
    kwb_reset(bridge);
    UNTIL(host->ready && host->counts.hello > hellos, 50);
    CHECK(kwh_dns(host, 22, "nowhere.invalid"));
    UNTIL(fake.dns_started == 2, 20);
    fake_dns_end(&fake); /* localhost, from before the reset */
    for(unsigned i = 0; i < 20; ++i) step();
    CHECK(host->counts.dns == answers);
    fake_dns_end(&fake);
    UNTIL(host->counts.dns > answers, 20);
    CHECK(host->dns_tag == 22 && host->dns_status != 0);
    fake.dns_hold = false;
    CHECK(kwh_dns(host, 23, "localhost"));
    UNTIL(host->counts.dns > answers + 1, 20);
    CHECK(host->dns_tag == 23 && host->dns_status == 0 && !memcmp(host->dns_ip, LOCALHOST, 4));
    printf("dns: a lookup from before a reset does not answer the next request\n");
}

/* Reopening a slot at once: nothing from the old connection leaks in. */
static void reuse(void) {
    uint16_t first = (uint16_t)(base_port + 5), second = (uint16_t)(base_port + 6);
    CHECK(kwh_listen(host, 6, first, 0));
    UNTIL(kwh_idle(host), 50);
    int fd = tcp_client(first);
    UNTIL(kwh_state(host, 6) == KWM_ESTABLISHED, 500);
    CHECK(send(fd, "old data", 8, 0) == 8);
    for(unsigned i = 0; i < 5; ++i) step();
    CHECK(kwh_close(host, 6, false));
    CHECK(kwh_listen(host, 6, second, 0));
    for(unsigned i = 0; i < 30; ++i) step();
    CHECK(kwh_state(host, 6) == KWM_LISTEN && kwh_available(host, 6) == 0);
    int fd2 = tcp_client(second);
    UNTIL(kwh_state(host, 6) == KWM_ESTABLISHED, 500);
    CHECK(send(fd2, "new", 3, 0) == 3);
    UNTIL(kwh_available(host, 6) == 3, 500);
    char buf[8];
    CHECK(kwh_recv(host, 6, buf, sizeof buf) == 3 && !memcmp(buf, "new", 3));
    close(fd);
    close(fd2);
    CHECK(kwh_close(host, 6, false));
    printf("reuse: generations keep connections apart\n");
}

static void ota_and_reboot(void) {
    uint8_t sha[32] = {0}, chunk[4000];
    uint32_t size = 10000;
    CHECK(kwh_ota_begin(host, size, sha));
    for(uint32_t at = 0; at < size;) {
        uint32_t n = size - at < sizeof chunk ? size - at : (uint32_t)sizeof chunk;
        for(uint32_t i = 0; i < n; ++i) chunk[i] = pattern(9, at + i);
        if(kwh_ota_data(host, at, chunk, n)) at += n;
        step();
    }
    CHECK(kwh_ota_end(host));
    UNTIL(host->ota_phase == KWM_OTA_END_PHASE, 50);
    CHECK(host->ota_status == 0 && fake.ota_written == size && fake.ota[size - 1] == pattern(9, size - 1));
    CHECK(kwh_reboot(host));
    UNTIL(host->counts.reboot, 20);
    uint32_t start = ms_now();
    while(!fake.reboots) {
        CHECK(ms_now() - start < 3000);
        step();
        usleep(10000);
    }
    printf("ota: 10000 bytes written; reboot after the answer\n");
}

int main(void) {
    base_port = (uint16_t)(30000u + (unsigned)getpid() % 20000u);
    bridge = calloc(1, sizeof *bridge);
    host = calloc(1, sizeof *host);
    CHECK(bridge && host);
    fake.bridge = bridge;
    fake.wifi = (struct kwb_wifi){KWM_WIFI_ONLINE, 5, 36, -48, {127, 0, 0, 1}, {255, 0, 0, 0}, {127, 0, 0, 1},
                                  {127, 0, 0, 1}, {1, 2, 3, 4, 5, 6}, KWM_BAND_BOTH, 0, 1, "Home 5G"};
    kwb_init(bridge, &platform, 8192);
    bus.bridge = bridge;
    arm(&bus);
    kwh_init(host, &host_bus);
    start();
    wifi_controls();
    tcp_listen();
    tcp_connect();
    udp();
    noisy();
    board_reset();
    dns_across_reset();
    reuse();
    ota_and_reboot();
    printf("test_bridge: all passed (%u transfers)\n", (unsigned)host->transfers);
    free(fake.ota);
    kwb_release(bridge);
    free(bridge);
    free(host);
    return 0;
}
