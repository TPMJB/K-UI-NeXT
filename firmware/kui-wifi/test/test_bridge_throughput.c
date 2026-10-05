/* SPDX-License-Identifier: MIT */
/* Compare useful bytes per transfer, not wall-clock speed. A real TCP
 * producer keeps the bridge's ring full between simulated SPI transfers;
 * the real host and bridge protocol choose all frame sizes and credits.
 * This makes the 4 KiB window-collapse regression reproducible without
 * assumptions about the host scheduler, radio or Dreamcast timing. */
#include "bridge.h"
#include "kwhost.h"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define CHECK(c) do { if(!(c)) { fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #c); exit(1); } } while(0)
#define TOTAL (256u * 1024u)
#define MAX_RING 8192u

struct fixture {
    struct kwb bridge;
    struct kwh host;
    uint8_t armed[KWL_FRAME_MAX];
    int peer;
    bool streaming, damage;
    size_t produced, received;
    uint32_t sent_crc, received_crc;
    unsigned transfers, corrupted;
};
struct result { unsigned transfers, bad, resent; uint32_t crc; };
static uint8_t pattern(size_t offset) { return (uint8_t)(offset * 131u + (offset >> 11) + 73u); }

static void arm(struct fixture *f) {
    size_t len = kwb_frame(&f->bridge, f->armed);
    memset(f->armed + len, 0, KWL_FRAME_MAX - len);
}
static void refill(struct fixture *f) {
    if(!f->streaming || f->produced == TOTAL) return;
    struct kwb_slot *slot = &f->bridge.slot[0];
    size_t wanted = slot->rx.cap - slot->rx.len;
    if(wanted > TOTAL - f->produced) wanted = TOTAL - f->produced;
    if(!wanted) return;
    uint8_t data[MAX_RING];
    CHECK(wanted <= sizeof data);
    for(size_t i = 0; i < wanted; ++i) data[i] = pattern(f->produced + i);
    for(size_t sent = 0; sent < wanted;) {
        ssize_t n = send(f->peer, data + sent, wanted - sent, 0);
        CHECK(n > 0);
        sent += (size_t)n;
    }
    f->sent_crc = kwl_crc32(f->sent_crc, data, wanted);
    f->produced += wanted;
    size_t target = slot->rx.len + wanted;
    /* Drain what was just sent through the production socket reader.
     * poll's timeout is a hang guard, not a throughput assertion. */
    while(slot->rx.len < target) {
        kwb_service(&f->bridge);
        if(slot->rx.len == target) break;
        struct pollfd fd = {slot->fd, POLLIN, 0};
        CHECK(poll(&fd, 1, 1000) == 1 && (fd.revents & POLLIN));
    }
}
static bool transfer(void *ctx, const uint8_t *out, uint8_t *in, size_t bytes) {
    struct fixture *f = ctx;
    CHECK(bytes >= KWL_HEADER && bytes <= KWL_FRAME_MAX && bytes % 4u == 0);
    memcpy(in, f->armed, bytes);
    if(f->streaming) {
        ++f->transfers;
        size_t payload = kwl_get16(in + 6);
        if(f->damage && f->transfers % 17u == 0 && payload && KWL_HEADER + payload <= bytes) {
            in[KWL_HEADER + payload - 1u] ^= 0x80u;
            ++f->corrupted;
        }
    }
    refill(f);
    kwb_transfer(&f->bridge, out, bytes);
    arm(f);
    return true;
}
static void consume(struct fixture *f) {
    uint8_t data[KWH_RX];
    size_t n = kwh_recv(&f->host, 0, data, sizeof data);
    CHECK(n <= TOTAL - f->received);
    for(size_t i = 0; i < n; ++i) CHECK(data[i] == pattern(f->received + i));
    f->received_crc = kwl_crc32(f->received_crc, data, n);
    f->received += n;
}
static struct result run(size_t ring, bool damage) {
    struct fixture *f = calloc(1, sizeof *f);
    CHECK(f);
    f->peer = -1;
    const struct kwb_platform platform = {0};
    kwb_init(&f->bridge, &platform, ring);
    const struct kwh_bus bus = {f, transfer};
    kwh_init(&f->host, &bus);
    kwh_start(&f->host, 0x1234);
    arm(f);
    for(unsigned i = 0; !f->host.ready; ++i) {
        CHECK(i < 20u && kwh_step(&f->host));
    }
    int server = socket(AF_INET, SOCK_STREAM, 0);
    CHECK(server >= 0);
    struct sockaddr_in address = {0};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(bind(server, (struct sockaddr *)&address, sizeof address) == 0 && listen(server, 1) == 0);
    socklen_t address_len = sizeof address;
    CHECK(getsockname(server, (struct sockaddr *)&address, &address_len) == 0);
    const uint8_t ip[4] = {127, 0, 0, 1};
    CHECK(kwh_connect(&f->host, 0, ip, ntohs(address.sin_port), KWM_NODELAY));
    for(unsigned i = 0; kwh_state(&f->host, 0) != KWM_ESTABLISHED; ++i) {
        CHECK(i < 1000u && kwh_step(&f->host));
    }
    f->peer = accept(server, NULL, NULL);
    CHECK(f->peer >= 0);
    int one = 1;
    CHECK(setsockopt(f->peer, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one) == 0);
    CHECK(f->bridge.slot[0].rx.cap == ring && f->bridge.slot[0].tx.cap == ring);
    f->streaming = true;
    f->damage = damage;
    while(f->received < TOTAL) {
        consume(f);
        if(f->received == TOTAL) break;
        CHECK(f->transfers < 5000u && kwh_step(&f->host));
    }
    CHECK(f->produced == TOTAL && f->sent_crc == f->received_crc);
    CHECK(!f->host.failures && !f->bridge.stats.dropped_sends);
    if(damage) CHECK(f->corrupted && f->host.link.stats.bad && f->bridge.link.stats.resent);
    else CHECK(!f->host.link.stats.bad && !f->bridge.link.stats.resent);
    struct result result = {f->transfers, f->host.link.stats.bad, f->bridge.link.stats.resent, f->received_crc};
    close(f->peer);
    close(server);
    kwb_release(&f->bridge);
    free(f);
    return result;
}
int main(void) {
    struct result small = run(4096u, false), large = run(8192u, false), damaged = run(8192u, true);
    CHECK(small.crc == large.crc && large.crc == damaged.crc);
    /* An 8 KiB ring should carry over 50% more useful bytes per transfer
     * than the old ring. No scheduler/clock/radio time appears here. */
    CHECK(large.transfers * 3u < small.transfers * 2u);
    printf("TCP ring throughput: 256 KiB, 4 KiB ring %u transfers; 8 KiB ring %u; "
           "damaged link %u (%u bad, %u retransmitted); CRC32 %08x\n",
           small.transfers, large.transfers, damaged.transfers, damaged.bad, damaged.resent, large.crc);
    return 0;
}
