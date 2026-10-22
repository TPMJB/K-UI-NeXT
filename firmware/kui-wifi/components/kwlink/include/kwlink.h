/* SPDX-License-Identifier: MIT */
/* The K-UI Wi-Fi link: frames between the Dreamcast (the host, SPI master)
 * and the Wi-Fi board (the bridge, SPI slave). Both sides use this file;
 * PROTOCOL.md describes the wire format. No allocation, no OS calls. */
#ifndef KWLINK_H
#define KWLINK_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define KWL_PROTOCOL 1u
#define KWL_MAGIC0 0x4bu
#define KWL_MAGIC1 0x57u
#define KWL_HEADER 16u
#define KWL_FRAME_MAX 4096u
#define KWL_PAYLOAD_MAX (KWL_FRAME_MAX - KWL_HEADER)
/* Payload the host clocks for the bridge when it has nothing larger to say. */
#define KWL_POLL 112u
/* Frames a side may have sent and not yet seen acknowledged. */
#define KWL_WINDOW 2u

#define KWL_F_DATA 0x01u
#define KWL_F_ACK 0x02u
#define KWL_F_SYNC 0x04u
#define KWL_F_BRIDGE 0x08u
#define KWL_F_NOSESSION 0x10u

enum kwl_role { KWL_HOST, KWL_BRIDGE };
enum kwl_result {
    KWL_NOTHING,   /* no frame for us: silence, noise or our own direction */
    KWL_OK,        /* a valid frame; a new payload was delivered */
    KWL_TRUNCATED, /* the peer's frame was longer than the transfer */
    KWL_BAD,       /* wrong length or checksum */
    KWL_IGNORED,   /* valid, but not for the current session */
    KWL_SYNCED,    /* bridge: a new session began; host: the bridge took ours */
    KWL_LOST,      /* host: the bridge no longer has our session */
};

struct kwl_stats {
    uint32_t transfers, sent, resent, received, duplicates, gaps, bad, truncated, ignored;
};
struct kwl_out {
    uint16_t len;
    uint8_t seq;
    bool sent;
    uint32_t sent_at;
    uint8_t payload[KWL_PAYLOAD_MAX];
};
struct kwl {
    enum kwl_role role;
    uint16_t session;
    /* Host: the bridge has answered our SYNC. Bridge: a session exists. */
    bool live;
    uint8_t next_seq, expect;
    /* A peer frame has been accepted this session, so our ack means something. */
    bool acked_any;
    /* Sent or waiting frames, oldest at out[out_head]. */
    struct kwl_out out[KWL_WINDOW];
    unsigned out_head, out_count;
    /* Transfers completed. */
    uint32_t clock;
    /* Host: the bridge's latest hint of what it still has to send.
     * Bridge: the payload capacity the host last promised. */
    uint16_t peer_window;
    /* Host: capacity announced in its last two frames (newest first), the
     * payload the current transfer must leave room for, and the length of
     * a bridge frame seen cut short, until one arrives whole. */
    uint16_t promised[2], need, want;
    /* Our frame in the transfer under way: out[] index or -1, and bytes. */
    int current;
    size_t current_len;
    struct kwl_stats stats;
};
struct kwl_io {
    void *ctx;
    /* A new frame's payload: whole messages, at most `capacity` bytes. */
    size_t (*fill)(void *ctx, uint8_t *payload, size_t capacity);
    /* A new payload from the peer, in order, exactly once. */
    void (*deliver)(void *ctx, const uint8_t *payload, size_t len);
    /* Bridge: bytes it still has waiting after this frame (a sizing hint). */
    size_t (*pending)(void *ctx);
};

void kwl_init(struct kwl *l, enum kwl_role role);
/* Host: begin a session; frames carry SYNC until the bridge answers. */
void kwl_host_sync(struct kwl *l, uint16_t session);
/* Bridge: forget the session (at start-up or on a reset request). */
void kwl_bridge_reset(struct kwl *l);
/* Our frame for the next transfer into `frame`; returns its length. */
size_t kwl_build(struct kwl *l, uint8_t frame[KWL_FRAME_MAX], const struct kwl_io *io);
/* Host: bytes to clock for a frame of `frame_len` (a multiple of 4). */
size_t kwl_host_length(const struct kwl *l, size_t frame_len);
/* The peer's bytes from the transfer just finished; `clocked` bytes went
 * each way. Call exactly once per transfer, after kwl_build. */
enum kwl_result kwl_receive(struct kwl *l, const uint8_t *in, size_t clocked, const struct kwl_io *io);
/* Unacknowledged frames or a frame waiting to go again. */
bool kwl_busy(const struct kwl *l);
/* zlib-style CRC-32: start with 0, feed the previous result back in. */
uint32_t kwl_crc32(uint32_t crc, const void *data, size_t len);

static inline void kwl_put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static inline void kwl_put32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}
static inline uint16_t kwl_get16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static inline uint32_t kwl_get32(const uint8_t *p) {
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}
#endif
