/* SPDX-License-Identifier: GPL-3.0-only */
#define _DEFAULT_SOURCE
#include "kui/w5500.h"
#include "kui/network_probe.h"
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
#include <unistd.h>

/* The W5500 driver against tests/w5500_model.c: registers, rings that
 * wrap, TCP through real localhost sockets, UDP and MACRAW. */
static const uint8_t mac[6] = {0x02, 0x4b, 0x55, 0x49, 0x00, 0x01};
static uint16_t port_base;
static struct kui_w5500 chip;

static void start(const struct w5500_model_options *options) {
    w5500_model_start(options);
    memset(&chip, 0, sizeof(chip));
    chip.bus = &w5500_model_bus;
    uint8_t version = 0;
    assert(kui_w5500_reset(&chip, &version) && version == KUI_W5500_VERSION);
    assert(kui_w5500_set_mac(&chip, mac));
}
static uint8_t state(unsigned s) { uint8_t v = 0xee; assert(kui_w5500_status(&chip, s, &v)); return v; }
static void settle(void) { w5500_model_bus.pause(NULL, 1); }
static uint8_t wait_state(unsigned s, uint8_t wanted) {
    for(unsigned i = 0; i < 2000; ++i) { uint8_t v = state(s); if(v == wanted) return v; settle(); }
    fprintf(stderr, "socket %u: state %02x, wanted %02x\n", s, state(s), wanted);
    assert(!"socket state");
    return 0;
}
static int client(uint16_t port) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons(port)};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(fd >= 0 && !connect(fd, (struct sockaddr *)&a, sizeof(a)));
    fcntl(fd, F_SETFL, O_NONBLOCK);
    return fd;
}
static uint8_t pattern(size_t i, unsigned seed) { return (uint8_t)(i * 31u + seed * 7u + (i >> 9)); }

static void chip_basics(void) {
    start(NULL);
    assert(kui_w5500_bus_check(&chip, 64));
    /* The check uses the address registers; their real values come after. */
    uint8_t cleared[6];
    assert(kui_w5500_read(&chip, KUI_W5500_COMMON, KUI_W5500_SHAR, cleared, 6) && !memcmp(cleared, "\0\0\0\0\0\0", 6));
    assert(kui_w5500_set_mac(&chip, mac));
    struct kui_w5500_link link;
    assert(kui_w5500_link(&chip, &link) && link.up && link.fast && link.full);
    uint8_t back[6];
    assert(kui_w5500_read(&chip, KUI_W5500_COMMON, KUI_W5500_SHAR, back, 6) && !memcmp(back, mac, 6));
    /* Buffer sizes: each a power of two up to 16 KB, sixteen KB per direction. */
    uint8_t rx[8] = {4, 4, 4, 1, 1, 1, 1, 0}, tx[8] = {4, 4, 4, 1, 1, 1, 1, 0}, bad[8] = {16, 1, 0, 0, 0, 0, 0, 0};
    assert(!kui_w5500_buffers(&chip, bad, tx));
    bad[0] = 3; bad[1] = 0;
    assert(!kui_w5500_buffers(&chip, bad, tx));
    assert(kui_w5500_buffers(&chip, rx, tx));
    assert(chip.rx_kb[0] == 4 && chip.tx_kb[6] == 1 && chip.rx_kb[7] == 0);
    assert(!kui_w5500_open(&chip, 7, KUI_W5500_TCP, 1000)); /* no buffer */
    assert(!kui_w5500_open(&chip, 1, KUI_W5500_MACRAW, 0)); /* MACRAW is socket 0's */
    w5500_model_stop();

    struct w5500_model_options down = {.link_down = true};
    start(&down);
    assert(kui_w5500_link(&chip, &link) && !link.up);
    w5500_model_stop();

    /* Nothing on the bus: the version reads 0xff and the chip is refused. */
    struct w5500_model_options absent = {.absent = true};
    w5500_model_start(&absent);
    memset(&chip, 0, sizeof(chip));
    chip.bus = &w5500_model_bus;
    uint8_t version = 0;
    assert(!kui_w5500_reset(&chip, &version) && version == 0xff && chip.failed);
    uint8_t v;
    assert(!kui_w5500_read8(&chip, KUI_W5500_COMMON, KUI_W5500_MR, &v)); /* refused after a failure */
    w5500_model_stop();

    /* A bus that flips bits fails the check. */
    struct w5500_model_options noisy = {.corrupt_reads = true};
    w5500_model_start(&noisy);
    memset(&chip, 0, sizeof(chip));
    chip.bus = &w5500_model_bus;
    if(kui_w5500_reset(&chip, &version)) assert(!kui_w5500_bus_check(&chip, 64));
    w5500_model_stop();
    puts("PASS W5500 reset, version, bus check, link, buffers");
}

static void tcp_server(void) {
    start(NULL);
    uint16_t port = port_base;
    assert(kui_w5500_open(&chip, 1, KUI_W5500_TCP | KUI_W5500_MR_NODELAY, port) && state(1) == KUI_W5500_INIT);
    assert(kui_w5500_listen(&chip, 1) && state(1) == KUI_W5500_LISTENING);
    int fd = client(port);
    wait_state(1, KUI_W5500_ESTABLISHED);
    uint8_t events = 0, ip[4];
    uint16_t peer_port = 0;
    assert(kui_w5500_events(&chip, 1, &events) && (events & KUI_W5500_IR_CON));
    assert(kui_w5500_peer(&chip, 1, ip, &peer_port) && ip[0] == 127 && ip[3] == 1 && peer_port);

    /* 9000 bytes through a 2 KB receive ring: it wraps several times. */
    enum {TOTAL = 9000};
    static uint8_t sent[TOTAL], got[TOTAL];
    for(size_t i = 0; i < TOTAL; ++i) sent[i] = pattern(i, 1);
    size_t out = 0, in = 0;
    while(in < TOTAL) {
        if(out < TOTAL) {
            ssize_t n = send(fd, sent + out, TOTAL - out > 700 ? 700 : TOTAL - out, MSG_DONTWAIT);
            if(n > 0) out += (size_t)n;
        }
        uint16_t waiting = 0;
        assert(kui_w5500_received(&chip, 1, &waiting) && waiting <= 2048);
        if(waiting) {
            uint16_t take = waiting > 1500 ? 1500 : waiting; /* odd sizes too */
            assert(in + take <= TOTAL && kui_w5500_receive(&chip, 1, got + in, take));
            in += take;
        } else settle();
    }
    assert(!memcmp(sent, got, TOTAL));

    /* 20000 bytes the other way through a 2 KB transmit ring. */
    enum {BACK = 20000};
    static uint8_t data[BACK], echo[BACK];
    for(size_t i = 0; i < BACK; ++i) data[i] = pattern(i, 2);
    out = in = 0;
    unsigned waits = 0;
    while(in < BACK) {
        uint16_t room = 0;
        assert(kui_w5500_room(&chip, 1, &room) && room <= 2048);
        if(room && out < BACK) {
            uint16_t n = (uint16_t)(BACK - out < room ? BACK - out : room);
            if(n > 1111) n = 1111;
            assert(kui_w5500_send(&chip, 1, data + out, n));
            /* No second SEND until the first is confirmed. */
            assert(kui_w5500_room(&chip, 1, &room));
            if(!room) ++waits;
            out += n;
        }
        ssize_t n = recv(fd, echo + in, BACK - in, MSG_DONTWAIT);
        if(n > 0) in += (size_t)n; else settle();
    }
    assert(!memcmp(data, echo, BACK) && waits);

    /* The client's FIN: data sent before it is still read, then CLOSE_WAIT. */
    assert(send(fd, "tail", 4, 0) == 4);
    shutdown(fd, SHUT_WR);
    wait_state(1, KUI_W5500_CLOSE_WAIT);
    uint16_t waiting = 0;
    assert(kui_w5500_received(&chip, 1, &waiting) && waiting == 4);
    char tail[4];
    assert(kui_w5500_receive(&chip, 1, tail, 4) && !memcmp(tail, "tail", 4));
    assert(kui_w5500_events(&chip, 1, &events) && (events & KUI_W5500_IR_DISCON));
    assert(kui_w5500_disconnect(&chip, 1));
    wait_state(1, KUI_W5500_CLOSED);
    char c;
    for(unsigned i = 0; i < 1000 && recv(fd, &c, 1, MSG_DONTWAIT) < 0; ++i) settle();
    assert(recv(fd, &c, 1, MSG_DONTWAIT) == 0);
    close(fd);

    /* Our FIN first: the client reads everything, then end of file. */
    assert(kui_w5500_open(&chip, 2, KUI_W5500_TCP, (uint16_t)(port + 1)) && kui_w5500_listen(&chip, 2));
    fd = client((uint16_t)(port + 1));
    wait_state(2, KUI_W5500_ESTABLISHED);
    assert(kui_w5500_send(&chip, 2, "bye", 3) && kui_w5500_disconnect(&chip, 2));
    char bye[8];
    ssize_t n = -1;
    for(unsigned i = 0; i < 1000 && (n = recv(fd, bye, sizeof(bye), MSG_DONTWAIT)) < 0; ++i) settle();
    assert(n == 3 && !memcmp(bye, "bye", 3));
    for(unsigned i = 0; i < 1000 && (n = recv(fd, bye, sizeof(bye), MSG_DONTWAIT)) < 0; ++i) settle();
    assert(n == 0);
    assert(state(2) == KUI_W5500_FIN_WAIT);
    close(fd);
    wait_state(2, KUI_W5500_CLOSED);

    /* A reset from the client closes the socket without CLOSE_WAIT. */
    assert(kui_w5500_open(&chip, 3, KUI_W5500_TCP, (uint16_t)(port + 2)) && kui_w5500_listen(&chip, 3));
    fd = client((uint16_t)(port + 2));
    wait_state(3, KUI_W5500_ESTABLISHED);
    struct linger l = {1, 0};
    setsockopt(fd, SOL_SOCKET, SO_LINGER, &l, sizeof(l));
    close(fd);
    wait_state(3, KUI_W5500_CLOSED);
    assert(kui_w5500_events(&chip, 3, &events) && (events & KUI_W5500_IR_DISCON));

    /* No listening socket any more: the connection is refused. */
    assert(kui_w5500_open(&chip, 4, KUI_W5500_TCP, (uint16_t)(port + 3)) && kui_w5500_listen(&chip, 4));
    assert(kui_w5500_close(&chip, 4) && state(4) == KUI_W5500_CLOSED);
    settle();
    fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in to = {.sin_family = AF_INET, .sin_port = htons((uint16_t)(port + 3))};
    to.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(fd >= 0 && connect(fd, (struct sockaddr *)&to, sizeof(to)) < 0 && errno == ECONNREFUSED);
    close(fd);
    w5500_model_stop();
    puts("PASS W5500 TCP server: accept, wrapping rings, SEND pacing, FIN both ways, reset, refusal");
}

static void tcp_client(void) {
    start(NULL);
    int server = socket(AF_INET, SOCK_STREAM, 0), yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    struct sockaddr_in a = {.sin_family = AF_INET, .sin_port = htons((uint16_t)(port_base + 10))};
    a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    assert(!bind(server, (struct sockaddr *)&a, sizeof(a)) && !listen(server, 1));
    static const uint8_t loopback[4] = {127, 0, 0, 1};
    assert(kui_w5500_open(&chip, 5, KUI_W5500_TCP, 20) && kui_w5500_connect(&chip, 5, loopback, (uint16_t)(port_base + 10)));
    wait_state(5, KUI_W5500_ESTABLISHED);
    int fd = accept(server, NULL, NULL);
    assert(fd >= 0 && kui_w5500_send(&chip, 5, "hello", 5));
    char buffer[8];
    ssize_t got = -1;
    fcntl(fd, F_SETFL, O_NONBLOCK);
    for(unsigned i = 0; i < 1000 && (got = recv(fd, buffer, sizeof(buffer), 0)) < 0; ++i) settle();
    assert(got == 5 && !memcmp(buffer, "hello", 5));
    close(fd);
    close(server);
    /* Nobody listening: the chip reports a timeout and closes. */
    assert(kui_w5500_open(&chip, 5, KUI_W5500_TCP, 20) && kui_w5500_connect(&chip, 5, loopback, (uint16_t)(port_base + 10)));
    wait_state(5, KUI_W5500_CLOSED);
    uint8_t events;
    assert(kui_w5500_events(&chip, 5, &events) && (events & KUI_W5500_IR_TIMEOUT));
    w5500_model_stop();
    puts("PASS W5500 TCP client: connect, send, refused connect times out");
}

static void datagrams(void) {
    start(NULL);
    assert(kui_w5500_open(&chip, 2, KUI_W5500_UDP, 68));
    struct kui_network_probe p;
    uint8_t frame[KUI_NETWORK_FRAME_MAX];
    kui_network_probe_begin(&p, mac, 0x1234, NULL, 0);
    size_t bytes = kui_network_probe_step(&p, frame, 0);
    assert(bytes > 42);
    static const uint8_t all[4] = {255, 255, 255, 255};
    assert(kui_w5500_datagram_send(&chip, 2, all, 67, frame + 42, (uint16_t)(bytes - 42)));
    uint8_t from[4], reply[600];
    uint16_t port = 0;
    size_t got = 0;
    for(unsigned i = 0; i < 100 && !got; ++i) {
        assert(kui_w5500_datagram_receive(&chip, 2, from, &port, reply, sizeof(reply), &got));
        if(!got) settle();
    }
    assert(got >= 240 && port == 67 && from[0] == 10 && from[3] == 1 && reply[0] == 2);
    assert(!memcmp(reply + 16, w5500_model_lease, 4) && w5500_model_counts.udp_requests == 1);
    w5500_model_stop();
    puts("PASS W5500 UDP datagram out and back");
}

static void run_probe(bool with_gateway, enum kui_network_stage expected, const char *failure) {
    struct kui_network_probe p;
    uint8_t frame[KUI_NETWORK_FRAME_MAX];
    kui_network_probe_begin(&p, mac, 0x2468, NULL, w5500_model_bus.now_ms(NULL));
    if(!with_gateway) p.lease_only = true;
    unsigned sends = 0;
    while(p.stage < KUI_NET_DONE) {
        size_t got = 0;
        do {
            assert(kui_w5500_frame_receive(&chip, frame, sizeof(frame), &got));
            if(got) kui_network_probe_receive(&p, frame, got, w5500_model_bus.now_ms(NULL));
        } while(got);
        size_t bytes = kui_network_probe_step(&p, frame, w5500_model_bus.now_ms(NULL));
        if(bytes) { assert(kui_w5500_frame_send(&chip, frame, bytes)); ++sends; }
        /* The conflict check waits 3 seconds; skip ahead. */
        if(p.stage == KUI_NET_CONFLICT) w5500_model_advance(250);
        if(p.stage == KUI_NET_DISCOVER || p.stage == KUI_NET_REQUEST) w5500_model_advance(50);
        settle();
    }
    if(p.stage != expected) fprintf(stderr, "probe: %s\n", p.failure);
    assert(p.stage == expected && sends);
    if(failure) assert(strstr(p.failure, failure));
    if(expected == KUI_NET_DONE) {
        assert(!memcmp(p.config.ip, w5500_model_lease, 4) && p.leased && p.lease_seconds == 3600);
        assert(with_gateway ? p.arp_reply && p.echo_reply : !p.arp_reply && !p.echo_reply);
    }
}
static void macraw(void) {
    start(NULL);
    assert(kui_w5500_open(&chip, 0, KUI_W5500_MACRAW | KUI_W5500_MR_MFEN, 0) && state(0) == KUI_W5500_MACRAW_OPEN);
    run_probe(true, KUI_NET_DONE, NULL);
    assert(w5500_model_counts.dhcp_discovers >= 1 && w5500_model_counts.dhcp_requests >= 1);
    assert(w5500_model_counts.arp_probes >= 1 && w5500_model_counts.pings == 1);
    w5500_model_stop();

    start(NULL);
    assert(kui_w5500_open(&chip, 0, KUI_W5500_MACRAW | KUI_W5500_MR_MFEN, 0));
    run_probe(false, KUI_NET_DONE, NULL);
    assert(!w5500_model_counts.pings);
    w5500_model_stop();

    struct w5500_model_options conflict = {.conflict = true};
    start(&conflict);
    assert(kui_w5500_open(&chip, 0, KUI_W5500_MACRAW | KUI_W5500_MR_MFEN, 0));
    run_probe(false, KUI_NET_FAILED, "conflict");
    w5500_model_stop();

    struct w5500_model_options silent = {.dhcp_silent = true};
    start(&silent);
    assert(kui_w5500_open(&chip, 0, KUI_W5500_MACRAW | KUI_W5500_MR_MFEN, 0));
    run_probe(false, KUI_NET_FAILED, "No DHCP offer");
    w5500_model_stop();

    struct w5500_model_options nak = {.dhcp_nak = true};
    start(&nak);
    assert(kui_w5500_open(&chip, 0, KUI_W5500_MACRAW | KUI_W5500_MR_MFEN, 0));
    run_probe(false, KUI_NET_FAILED, "rejected");
    w5500_model_stop();
    puts("PASS W5500 MACRAW: DHCP, conflict check, gateway ARP and ping, silent and refusing servers");
}

/* Streams: a socket's data moved by async frames, which the model ends
 * when the bus is polled (hold polls; so does each run of the loop here). */
static uint32_t least(uint32_t a, uint32_t b) { return a < b ? a : b; }
static void stream_open(unsigned s, uint16_t port, int *fd) {
    assert(kui_w5500_bus_check(&chip, 8) && chip.async);
    assert(kui_w5500_set_mac(&chip, mac));
    uint8_t rx[8] = {8, 2, 2, 1, 1, 1, 1, 0}, tx[8] = {8, 2, 2, 1, 1, 1, 1, 0};
    assert(kui_w5500_buffers(&chip, rx, tx));
    assert(kui_w5500_open(&chip, s, KUI_W5500_TCP | KUI_W5500_MR_NODELAY, port) && kui_w5500_listen(&chip, s));
    *fd = client(port);
    wait_state(s, KUI_W5500_ESTABLISHED);
}
static void streams(void) {
    enum {TOTAL = 300000, RING = 65536};
    static uint8_t sent[TOTAL], got[TOTAL], ring[RING];
    struct kui_w5500_stream st;
    int fd;

    /* Upload: the client's data into a 64 KB ring, the owner taking it as
     * the card would (32 KB at a time, sometimes after letting it fill). */
    start(NULL);
    stream_open(0, (uint16_t)(port_base + 11), &fd);
    for(size_t i = 0; i < TOTAL; ++i) sent[i] = pattern(i, 5);
    assert(!kui_w5500_stream_begin(&st, &chip, 0, false, ring, 3000)); /* not a power of two */
    assert(kui_w5500_stream_begin(&st, &chip, 0, false, ring, RING) && st.max_piece == 4096);
    size_t out = 0, in = 0;
    unsigned full = 0, rounds = 0;
    while(in < TOTAL) {
        if(out < TOTAL) {
            ssize_t n = send(fd, sent + out, TOTAL - out > 30000 ? 30000 : TOTAL - out, MSG_DONTWAIT);
            if(n > 0) out += (size_t)n;
        }
        kui_w5500_stream_run(&st);
        w5500_model_bus.pause(NULL, 0);
        assert(kui_w5500_stream_hold(&st) && !st.lent && !st.busy);
        uint32_t ready = kui_w5500_stream_ready(&st);
        assert(ready <= RING && in + ready <= TOTAL);
        if(ready == RING) ++full;
        /* The first third is taken only once the ring is full: the stream
         * must stop there and go on when there is room again. */
        ++rounds;
        if(in < TOTAL / 3 && ready < RING) continue;
        uint32_t at = st.tail & (RING - 1u), n = least(least(ready, RING - at), rounds % 3u ? 32768u : 5000u);
        memcpy(got + in, ring + at, n);
        kui_w5500_stream_take(&st, n);
        in += n;
    }
    assert(!memcmp(sent, got, TOTAL));
    assert(full);
    /* The chip has nothing left, and its pointer matches the stream's. */
    uint16_t waiting = 1, pointer = 0;
    assert(kui_w5500_received(&chip, 0, &waiting) && !waiting);
    assert(kui_w5500_read16(&chip, KUI_W5500_SOCKET_REGS(0), KUI_W5500_SN_RX_RD, &pointer) && pointer == st.pointer);
    /* The client's FIN: the stream simply finds nothing more. */
    shutdown(fd, SHUT_WR);
    wait_state(0, KUI_W5500_CLOSE_WAIT);
    kui_w5500_stream_run(&st);
    assert(kui_w5500_stream_hold(&st) && !kui_w5500_stream_ready(&st));
    kui_w5500_stream_end(&st);
    assert(!st.w && kui_w5500_close(&chip, 0));
    close(fd);
    w5500_model_stop();

    /* Download: the owner puts data in as the card gives it; pieces go to
     * the chip while a SEND is still under way, sent together after it. */
    start(NULL);
    stream_open(0, (uint16_t)(port_base + 12), &fd);
    for(size_t i = 0; i < TOTAL; ++i) sent[i] = pattern(i, 6);
    assert(kui_w5500_stream_begin(&st, &chip, 0, true, ring, RING) && st.max_piece == 4096);
    out = in = 0;
    unsigned overlapped = 0;
    for(rounds = 0; in < TOTAL; ++rounds) {
        uint32_t room = kui_w5500_stream_room(&st), at = st.head & (RING - 1u);
        uint32_t n = least(least(room, RING - at), least(rounds % 3u ? 32768u : 3000u, (uint32_t)(TOTAL - out)));
        if(n) {
            memcpy(ring + at, sent + out, n);
            kui_w5500_stream_put(&st, n);
            out += n;
        }
        kui_w5500_stream_run(&st);
        ssize_t got_now = recv(fd, got + in, TOTAL - in, MSG_DONTWAIT);
        if(got_now > 0) in += (size_t)got_now;
        w5500_model_bus.pause(NULL, 0);
        assert(kui_w5500_stream_hold(&st));
        if(st.unsent && (chip.sending & 1u)) ++overlapped;
        assert(rounds < 200000);
    }
    assert(!memcmp(sent, got, TOTAL) && overlapped);
    assert(kui_w5500_stream_sent(&st));
    assert(kui_w5500_stream_end(&st));
    /* Everything acknowledged: the whole transmit buffer is free again. */
    uint16_t room = 0;
    for(unsigned i = 0; i < 1000 && room != 8192; ++i) { settle(); assert(kui_w5500_room(&chip, 0, &room)); }
    assert(room == 8192);
    close(fd);
    w5500_model_stop();

    /* Ended with pieces on the chip that no SEND has taken yet: they are
     * sent, and the plain calls carry on from there with the ring's rest. */
    start(NULL);
    stream_open(0, (uint16_t)(port_base + 14), &fd);
    for(size_t i = 0; i < 20000; ++i) sent[i] = pattern(i, 8);
    assert(kui_w5500_stream_begin(&st, &chip, 0, true, ring, RING));
    memcpy(ring, sent, 20000);
    kui_w5500_stream_put(&st, 20000);
    for(unsigned i = 0; i < 1000 && !st.unsent; ++i) {
        kui_w5500_stream_run(&st);
        assert(kui_w5500_stream_hold(&st));
    }
    assert(st.unsent && st.head != st.tail);
    uint32_t rest = st.head - st.tail, from = st.tail;
    assert(kui_w5500_stream_end(&st) && !st.w);
    for(size_t done = 0; done < rest;) {
        uint16_t space = 0;
        assert(kui_w5500_room(&chip, 0, &space));
        uint16_t n = (uint16_t)(rest - done < space ? rest - done : space);
        if(n) { assert(kui_w5500_send(&chip, 0, ring + from + done, n)); done += n; }
        else settle();
    }
    for(in = 0; in < 20000;) {
        ssize_t n = recv(fd, got + in, 20000 - in, MSG_DONTWAIT);
        if(n > 0) in += (size_t)n; else settle();
    }
    assert(!memcmp(sent, got, 20000));
    close(fd);
    w5500_model_stop();

    /* No async frames, or ones that fail the check: none used. */
    struct w5500_model_options none = {.no_async = true}, failing = {.async_fails = true};
    start(&none);
    assert(kui_w5500_bus_check(&chip, 8) && !chip.async && !kui_w5500_stream_begin(&st, &chip, 0, false, ring, RING));
    w5500_model_stop();
    start(&failing);
    assert(kui_w5500_bus_check(&chip, 8) && !chip.async);
    w5500_model_stop();

    /* A frame that fails mid-stream fails the stream, not the chip; one
     * that never ends is given up after 50 ms, and async stays off. */
    start(NULL);
    stream_open(0, (uint16_t)(port_base + 13), &fd);
    assert(send(fd, sent, 5000, 0) == 5000);
    assert(kui_w5500_stream_begin(&st, &chip, 0, false, ring, RING));
    for(unsigned i = 0; i < 100 && kui_w5500_stream_ready(&st) < 5000; ++i) {
        kui_w5500_stream_run(&st);
        assert(kui_w5500_stream_hold(&st));
        settle();
    }
    assert(kui_w5500_stream_ready(&st) == 5000 && !memcmp(ring, sent, 5000));
    assert(send(fd, sent, 3000, 0) == 3000);
    for(unsigned i = 0; i < 20; ++i) settle();
    w5500_model_live()->async_fails = true;
    kui_w5500_stream_run(&st);
    assert(!kui_w5500_stream_hold(&st) && st.failed && !chip.failed && chip.async);
    kui_w5500_stream_run(&st); /* a failed stream stays stopped */
    assert(!st.lent && !st.busy);
    w5500_model_live()->async_fails = false;
    assert(kui_w5500_stream_begin(&st, &chip, 0, false, ring, RING));
    w5500_model_live()->async_stalls = true;
    kui_w5500_stream_run(&st);
    assert(st.busy);
    assert(!kui_w5500_stream_hold(&st) && st.failed && !chip.async && w5500_model_counts.async_cancels == 1);
    assert(!kui_w5500_stream_begin(&st, &chip, 0, false, ring, RING));
    /* The chip is still there, and the data the stream never took is too. */
    w5500_model_live()->async_stalls = false;
    uint16_t left = 0;
    assert(state(0) == KUI_W5500_ESTABLISHED && kui_w5500_received(&chip, 0, &left) && left == 3000);
    close(fd);
    w5500_model_stop();
    puts("PASS W5500 streams: upload and download by async frames, full ring, failing and stalled frames");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    /* Below Linux's ephemeral ports (32768 and up). */
    port_base = (uint16_t)(20000 + getpid() % 10000);
    chip_basics();
    tcp_server();
    tcp_client();
    datagrams();
    macraw();
    streams();
    puts("PASS W5500 driver");
    return 0;
}
