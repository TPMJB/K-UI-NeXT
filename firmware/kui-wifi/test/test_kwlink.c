/* SPDX-License-Identifier: MIT */
/* The link layer on its own: two ends joined by a simulated SPI bus that
 * flips bits and cuts frames short. Every payload must arrive exactly once
 * and in order, both ways, whatever the errors. */
#include "kwlink.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if(!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while(0)

static uint32_t rng = 12345u;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* One direction's traffic: payload n is n's length and bytes derived from n. */
struct stream {
    uint32_t next_out, total, next_in;
    uint32_t max_len;
};
static size_t payload_len(uint32_t n, uint32_t max) { return 4u + (n * 2654435761u >> 7) % (max - 4u); }
static uint8_t payload_byte(uint32_t n, size_t i) { return (uint8_t)(n * 31u + i * 7u + (i >> 3)); }
static size_t fill(void *ctx, uint8_t *p, size_t capacity) {
    struct stream *s = ctx;
    if(s->next_out >= s->total) return 0;
    size_t len = payload_len(s->next_out, s->max_len);
    if(len > capacity) len = capacity;
    kwl_put32(p, s->next_out);
    for(size_t i = 4; i < len; ++i) p[i] = payload_byte(s->next_out, i);
    ++s->next_out;
    return len;
}
static void deliver(void *ctx, const uint8_t *p, size_t len) {
    struct stream *s = ctx;
    CHECK(len >= 4);
    uint32_t n = kwl_get32(p);
    CHECK(n == s->next_in);
    for(size_t i = 4; i < len; ++i) CHECK(p[i] == payload_byte(n, i));
    ++s->next_in;
}
static size_t pending(void *ctx) {
    struct stream *s = ctx;
    return s->next_out < s->total ? (size_t)(s->total - s->next_out) * s->max_len / 2u : 0;
}

struct end {
    struct kwl link;
    struct stream out;  /* what this end sends */
    struct stream *in;  /* what the other end sends (checked here) */
    struct kwl_io io;
    uint8_t frame[KWL_FRAME_MAX];
};
static void setup(struct end *e, enum kwl_role role, uint32_t total, uint32_t max_len, struct stream *peer_out) {
    kwl_init(&e->link, role);
    e->out = (struct stream){0, total, 0, max_len};
    e->in = peer_out;
    e->io = (struct kwl_io){&e->out, fill, NULL, pending};
}
/* The receiving side delivers into the peer's stream record. */
static enum kwl_result receive(struct end *e, const uint8_t *in, size_t clocked) {
    struct kwl_io io = e->io;
    io.ctx = e->in;
    io.deliver = deliver;
    enum kwl_result r = kwl_receive(&e->link, in, clocked, &io);
    return r;
}

struct faults {unsigned flip_permille, cut_permille;};
static void flip(uint8_t *b, size_t len, unsigned permille) {
    if(rnd() % 1000u < permille) b[rnd() % len] ^= (uint8_t)(1u << (rnd() % 8u));
}

/* Runs transfers until both streams have fully arrived; returns how many. */
static unsigned run(struct end *host, struct end *bridge, struct faults f, unsigned limit, bool *lost) {
    static uint8_t host_in[KWL_FRAME_MAX], bridge_in[KWL_FRAME_MAX];
    /* The bridge arms its first transfer before the host starts. */
    size_t armed = kwl_build(&bridge->link, bridge->frame, &bridge->io);
    memset(bridge->frame + armed, 0, KWL_FRAME_MAX - armed);
    unsigned n = 0;
    for(; n < limit; ++n) {
        if(host->link.live && bridge->out.next_in >= bridge->out.total && host->out.next_in >= host->out.total &&
           !kwl_busy(&host->link) && !kwl_busy(&bridge->link))
            break;
        size_t own = kwl_build(&host->link, host->frame, &host->io);
        memset(host->frame + own, 0, KWL_FRAME_MAX - own);
        size_t clocked = kwl_host_length(&host->link, own);
        CHECK(clocked % 4u == 0 && clocked >= own && clocked <= KWL_FRAME_MAX);
        /* A host that sizes a transfer wrongly (as after a lost header). */
        if(rnd() % 1000u < f.cut_permille && clocked > KWL_HEADER + 16u && clocked > own + 4u) clocked -= 4u * (1u + rnd() % 4u);
        if(clocked < own) clocked = (own + 3u) & ~3u;
        memcpy(host_in, bridge->frame, clocked);
        memcpy(bridge_in, host->frame, clocked);
        flip(host_in, clocked, f.flip_permille);
        flip(bridge_in, clocked, f.flip_permille);
        receive(bridge, bridge_in, clocked);
        if(receive(host, host_in, clocked) == KWL_LOST && lost) {
            /* The caller starts a new session with fresh streams. */
            *lost = true;
            return n + 1;
        }
        armed = kwl_build(&bridge->link, bridge->frame, &bridge->io);
        memset(bridge->frame + armed, 0, KWL_FRAME_MAX - armed);
    }
    return n;
}

static void clean_exchange(void) {
    struct end *host = calloc(1, sizeof *host), *bridge = calloc(1, sizeof *bridge);
    setup(host, KWL_HOST, 200, 4000, NULL);
    setup(bridge, KWL_BRIDGE, 300, 4080, NULL);
    host->in = &bridge->out;
    bridge->in = &host->out;
    kwl_bridge_reset(&bridge->link);
    kwl_host_sync(&host->link, 0x1234);
    unsigned n = run(host, bridge, (struct faults){0, 0}, 5000, NULL);
    CHECK(n < 5000);
    CHECK(host->out.next_in == 200 && bridge->out.next_in == 300);
    CHECK(host->link.stats.bad == 0 && bridge->link.stats.bad == 0);
    CHECK(host->link.stats.resent == 0 && bridge->link.stats.resent == 0);
    printf("clean: %u transfers for 200 + 300 payloads\n", n);
    free(host);
    free(bridge);
}

static void noisy_exchange(unsigned flip_permille, unsigned cut_permille) {
    struct end *host = calloc(1, sizeof *host), *bridge = calloc(1, sizeof *bridge);
    setup(host, KWL_HOST, 3000, 3000, NULL);
    setup(bridge, KWL_BRIDGE, 3000, 4080, NULL);
    host->in = &bridge->out;
    bridge->in = &host->out;
    kwl_bridge_reset(&bridge->link);
    kwl_host_sync(&host->link, 0x4321);
    unsigned n = run(host, bridge, (struct faults){flip_permille, cut_permille}, 200000, NULL);
    CHECK(n < 200000);
    CHECK(host->out.next_in == 3000 && bridge->out.next_in == 3000);
    printf("noisy (%u/1000 flips, %u/1000 cuts): %u transfers; host %u bad %u resent %u truncated, bridge %u bad %u resent\n",
           flip_permille, cut_permille, n, (unsigned)host->link.stats.bad, (unsigned)host->link.stats.resent,
           (unsigned)host->link.stats.truncated, (unsigned)bridge->link.stats.bad, (unsigned)bridge->link.stats.resent);
    free(host);
    free(bridge);
}

/* The bridge forgets the session mid-stream: the host must see it. */
static void bridge_restart(void) {
    struct end *host = calloc(1, sizeof *host), *bridge = calloc(1, sizeof *bridge);
    setup(host, KWL_HOST, 50, 1000, NULL);
    setup(bridge, KWL_BRIDGE, 50, 1000, NULL);
    host->in = &bridge->out;
    bridge->in = &host->out;
    kwl_bridge_reset(&bridge->link);
    kwl_host_sync(&host->link, 7);
    run(host, bridge, (struct faults){0, 0}, 12, NULL);
    CHECK(host->link.live);
    kwl_bridge_reset(&bridge->link);
    bool lost = false;
    run(host, bridge, (struct faults){0, 0}, 4, &lost);
    CHECK(lost && !host->link.live);
    /* A new session starts both streams again. */
    host->out = (struct stream){0, 40, 0, 1000};
    bridge->out = (struct stream){0, 40, 0, 1000};
    kwl_host_sync(&host->link, 8);
    unsigned n = run(host, bridge, (struct faults){0, 0}, 2000, NULL);
    CHECK(n < 2000 && host->out.next_in == 40 && bridge->out.next_in == 40);
    printf("bridge restart: resynchronised\n");
    free(host);
    free(bridge);
}

/* Nothing answers (MISO floating high or low): no frame, no progress. */
static void silence(void) {
    struct kwl host;
    kwl_init(&host, KWL_HOST);
    kwl_host_sync(&host, 3);
    uint8_t frame[KWL_FRAME_MAX], ones[KWL_FRAME_MAX], zeros[KWL_FRAME_MAX] = {0};
    memset(ones, 0xff, sizeof ones);
    size_t own = kwl_build(&host, frame, NULL);
    CHECK(own == KWL_HEADER && frame[2] == KWL_F_SYNC);
    CHECK(kwl_receive(&host, ones, 128, NULL) == KWL_NOTHING);
    kwl_build(&host, frame, NULL);
    CHECK(kwl_receive(&host, zeros, 128, NULL) == KWL_NOTHING);
    /* Our own frame looped back (MOSI shorted to MISO) is not the bridge. */
    kwl_build(&host, frame, NULL);
    CHECK(kwl_receive(&host, frame, 128, NULL) == KWL_NOTHING);
    CHECK(!host.live);
    printf("silence: ignored\n");
}

int main(void) {
    silence();
    clean_exchange();
    bridge_restart();
    noisy_exchange(20, 0);
    noisy_exchange(0, 30);
    noisy_exchange(100, 50);
    noisy_exchange(400, 200);
    printf("test_kwlink: all passed\n");
    return 0;
}
