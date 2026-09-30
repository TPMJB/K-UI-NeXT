/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef KUI_W5500_H
#define KUI_W5500_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* WIZnet W5500 Ethernet controller over SPI, written from WIZnet's W5500
 * datasheet. The chip keeps its own TCP/IP stack and eight sockets, each
 * with a transmit and a receive buffer in its 32 KB of memory. K-UI uses
 * its TCP sockets for the FTP server and socket 0 in MACRAW mode (whole
 * Ethernet frames) for DHCP and the network test.
 *
 * Every access is one SPI frame with chip select held: a 16-bit address, a
 * control byte (block, read or write, variable length), then the data. */
#define KUI_W5500_SOCKETS 8u
#define KUI_W5500_VERSION 0x04u

/* Blocks: the common registers, then per socket its registers and buffers. */
#define KUI_W5500_COMMON 0x00u
#define KUI_W5500_SOCKET_REGS(s) ((uint8_t)(1u + 4u * (s)))
#define KUI_W5500_SOCKET_TX(s) ((uint8_t)(2u + 4u * (s)))
#define KUI_W5500_SOCKET_RX(s) ((uint8_t)(3u + 4u * (s)))

/* Common registers. */
#define KUI_W5500_MR 0x0000u
#define KUI_W5500_GAR 0x0001u
#define KUI_W5500_SUBR 0x0005u
#define KUI_W5500_SHAR 0x0009u
#define KUI_W5500_SIPR 0x000fu
#define KUI_W5500_IR 0x0015u
#define KUI_W5500_RTR 0x0019u
#define KUI_W5500_RCR 0x001bu
#define KUI_W5500_PHYCFGR 0x002eu
#define KUI_W5500_VERSIONR 0x0039u
#define KUI_W5500_MR_RST 0x80u
#define KUI_W5500_PHY_LINK 0x01u
#define KUI_W5500_PHY_100 0x02u
#define KUI_W5500_PHY_FULL 0x04u

/* Socket registers. */
#define KUI_W5500_SN_MR 0x0000u
#define KUI_W5500_SN_CR 0x0001u
#define KUI_W5500_SN_IR 0x0002u
#define KUI_W5500_SN_SR 0x0003u
#define KUI_W5500_SN_PORT 0x0004u
#define KUI_W5500_SN_DIPR 0x000cu
#define KUI_W5500_SN_DPORT 0x0010u
#define KUI_W5500_SN_RXBUF_SIZE 0x001eu
#define KUI_W5500_SN_TXBUF_SIZE 0x001fu
#define KUI_W5500_SN_TX_FSR 0x0020u
#define KUI_W5500_SN_TX_RD 0x0022u
#define KUI_W5500_SN_TX_WR 0x0024u
#define KUI_W5500_SN_RX_RSR 0x0026u
#define KUI_W5500_SN_RX_RD 0x0028u
#define KUI_W5500_SN_RX_WR 0x002au
#define KUI_W5500_SN_KPALVTR 0x002fu

/* Sn_MR: protocol in the low bits. */
#define KUI_W5500_TCP 0x01u
#define KUI_W5500_UDP 0x02u
#define KUI_W5500_MACRAW 0x04u
#define KUI_W5500_MR_NODELAY 0x20u /* TCP: acknowledge every segment at once */
#define KUI_W5500_MR_MFEN 0x80u    /* MACRAW: only frames for this MAC or broadcast */
/* Sn_CR commands; the chip clears Sn_CR once it has taken one. */
#define KUI_W5500_OPEN 0x01u
#define KUI_W5500_LISTEN 0x02u
#define KUI_W5500_CONNECT 0x04u
#define KUI_W5500_DISCON 0x08u
#define KUI_W5500_CLOSE 0x10u
#define KUI_W5500_SEND 0x20u
#define KUI_W5500_RECV 0x40u
/* Sn_IR. */
#define KUI_W5500_IR_CON 0x01u
#define KUI_W5500_IR_DISCON 0x02u
#define KUI_W5500_IR_RECV 0x04u
#define KUI_W5500_IR_TIMEOUT 0x08u
#define KUI_W5500_IR_SENDOK 0x10u
/* Sn_SR. */
#define KUI_W5500_CLOSED 0x00u
#define KUI_W5500_INIT 0x13u
#define KUI_W5500_LISTENING 0x14u
#define KUI_W5500_SYNSENT 0x15u
#define KUI_W5500_SYNRECV 0x16u
#define KUI_W5500_ESTABLISHED 0x17u
#define KUI_W5500_FIN_WAIT 0x18u
#define KUI_W5500_CLOSING 0x1au
#define KUI_W5500_TIME_WAIT 0x1bu
#define KUI_W5500_CLOSE_WAIT 0x1cu
#define KUI_W5500_LAST_ACK 0x1du
#define KUI_W5500_UDP_OPEN 0x22u
#define KUI_W5500_MACRAW_OPEN 0x42u

struct kui_w5500_bus {
    void *ctx;
    /* One frame with chip select held across it: the three header bytes,
     * then `bytes` of data written from `out` or read into `in` (exactly one
     * of them is non-NULL; bytes may be 0). False when the transfer failed. */
    bool (*frame)(void *ctx, const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes);
    /* Milliseconds from any fixed start, and a pause that lets others run. */
    uint64_t (*now_ms)(void *ctx);
    void (*pause)(void *ctx, unsigned ms);
    /* Optional, for kui_w5500_stream: a frame whose data moves without the
     * CPU (by DMA on the console). It sends the header, starts the data and
     * returns; `done` runs once the data has moved and chip select is
     * released, with ok false if it did not all move: from an interrupt on
     * the console, from `poll` on a host. `done` may call `frame` and start
     * the next async frame, but never `pause`. At most async_max bytes.
     * False: nothing started, and `done` will not run. */
    bool (*frame_async)(void *ctx, const uint8_t header[3], const uint8_t *out, uint8_t *in, size_t bytes,
                        void (*done)(void *arg, bool ok), void *arg);
    /* Runs `done` for async frames that have ended where nothing else does
     * (a host); and gives up on one that has not ended (no `done` follows). */
    void (*poll)(void *ctx);
    void (*cancel)(void *ctx);
    size_t async_max;
    /* Optional: microseconds from any fixed start, to time a transfer. */
    uint64_t (*now_us)(void *ctx);
};
struct kui_w5500 {
    const struct kui_w5500_bus *bus;
    /* A transfer failed or the chip stopped answering; every later call
     * fails until kui_w5500_reset succeeds. */
    bool failed;
    /* The bus's async frames passed the wiring check both ways. */
    bool async;
    /* Bit s: socket s has a SEND the chip has not yet confirmed. The next
     * one waits for it (a second SEND while one runs is not allowed). */
    uint8_t sending;
    /* Sn_IR bits already cleared on the chip but not yet returned by
     * kui_w5500_events, and each socket's buffer sizes in KB. */
    uint8_t pending[KUI_W5500_SOCKETS], rx_kb[KUI_W5500_SOCKETS], tx_kb[KUI_W5500_SOCKETS];
};
struct kui_w5500_link {bool up, fast, full;};

/* Register access. */
bool kui_w5500_read(struct kui_w5500 *w, uint8_t block, uint16_t address, void *data, size_t bytes);
bool kui_w5500_write(struct kui_w5500 *w, uint8_t block, uint16_t address, const void *data, size_t bytes);
bool kui_w5500_read8(struct kui_w5500 *w, uint8_t block, uint16_t address, uint8_t *value);
bool kui_w5500_write8(struct kui_w5500 *w, uint8_t block, uint16_t address, uint8_t value);
/* A 16-bit register the chip updates by itself (free and received sizes):
 * read until two readings agree. */
bool kui_w5500_read16(struct kui_w5500 *w, uint8_t block, uint16_t address, uint16_t *value);
bool kui_w5500_write16(struct kui_w5500 *w, uint8_t block, uint16_t address, uint16_t value);

/* Software reset, then the version register must read 0x04. Afterwards
 * every socket is closed with the default 2 KB buffers. */
bool kui_w5500_reset(struct kui_w5500 *w, uint8_t *version);
/* Writes patterns to registers and 1 KB through socket 0's transmit buffer
 * and reads them back: proves the wiring carries data both ways at the
 * current SPI speed. The socket must be closed. With async frames, also
 * moves 1 KB each way through them and sets w->async if that works. */
bool kui_w5500_bus_check(struct kui_w5500 *w, unsigned rounds);
bool kui_w5500_link(struct kui_w5500 *w, struct kui_w5500_link *out);
/* Buffer sizes in KB (0, 1, 2, 4, 8 or 16), each set of eight adding up to
 * at most 16. Only while every socket is closed. */
bool kui_w5500_buffers(struct kui_w5500 *w, const uint8_t rx_kb[KUI_W5500_SOCKETS],
                       const uint8_t tx_kb[KUI_W5500_SOCKETS]);
bool kui_w5500_set_mac(struct kui_w5500 *w, const uint8_t mac[6]);
/* Own address, netmask and gateway (all zero before DHCP). */
bool kui_w5500_set_ipv4(struct kui_w5500 *w, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gateway[4]);

/* Sockets. Open closes the socket first if needed, then opens it with the
 * given Sn_MR value and local port and checks the resulting state. */
bool kui_w5500_open(struct kui_w5500 *w, unsigned s, uint8_t mode, uint16_t port);
bool kui_w5500_listen(struct kui_w5500 *w, unsigned s);
bool kui_w5500_connect(struct kui_w5500 *w, unsigned s, const uint8_t ip[4], uint16_t port);
/* DISCON: TCP's orderly close (FIN once all sent data has gone). */
bool kui_w5500_disconnect(struct kui_w5500 *w, unsigned s);
/* CLOSE: the socket is closed at once; unsent data is dropped. */
bool kui_w5500_close(struct kui_w5500 *w, unsigned s);
bool kui_w5500_status(struct kui_w5500 *w, unsigned s, uint8_t *state);
/* Reads Sn_IR and clears the bits it returns. */
bool kui_w5500_events(struct kui_w5500 *w, unsigned s, uint8_t *events);
bool kui_w5500_peer(struct kui_w5500 *w, unsigned s, uint8_t ip[4], uint16_t *port);
/* Seconds of silence (in steps of 5) before TCP keep-alive; 0 turns it off. */
bool kui_w5500_keepalive(struct kui_w5500 *w, unsigned s, unsigned seconds);

/* Bytes waiting in the receive buffer. */
bool kui_w5500_received(struct kui_w5500 *w, unsigned s, uint16_t *bytes);
/* Takes `bytes` (at most what kui_w5500_received reported) from the receive
 * buffer and tells the chip they were read. */
bool kui_w5500_receive(struct kui_w5500 *w, unsigned s, void *data, uint16_t bytes);
/* Room for a SEND now: 0 while the previous SEND is still going. A SEND
 * that ended in a timeout makes this fail (the chip closed the socket). */
bool kui_w5500_room(struct kui_w5500 *w, unsigned s, uint16_t *bytes);
/* Writes `bytes` (at most kui_w5500_room's answer) and starts a SEND. */
bool kui_w5500_send(struct kui_w5500 *w, unsigned s, const void *data, uint16_t bytes);

/* UDP: one datagram to ip:port (waiting up to 100 ms for room), and the
 * next datagram received with its sender, or *bytes = 0 when none; one
 * larger than `capacity` is dropped. */
bool kui_w5500_datagram_send(struct kui_w5500 *w, unsigned s, const uint8_t ip[4], uint16_t port,
                             const void *data, uint16_t bytes);
bool kui_w5500_datagram_receive(struct kui_w5500 *w, unsigned s, uint8_t ip[4], uint16_t *port,
                                void *data, size_t capacity, size_t *bytes);
/* MACRAW on socket 0. The next whole frame, or *bytes = 0 when none; a
 * frame larger than `capacity` is dropped. */
bool kui_w5500_frame_receive(struct kui_w5500 *w, uint8_t *frame, size_t capacity, size_t *bytes);
/* Sends one frame, waiting (up to 100 ms) for room and for the SEND. */
bool kui_w5500_frame_send(struct kui_w5500 *w, const uint8_t *frame, size_t bytes);

/* A TCP socket's data moved between the chip and a ring in memory by async
 * frames, while the CPU does other work (the SD card). Each frame's `done`
 * does the few register frames between pieces (Sn_RX_RD and RECV, or
 * Sn_TX_WR and SEND) and starts the next piece, so the CPU is needed only
 * for those. When there is nothing to move yet, a wait frame (a short
 * read that changes nothing) looks again a little later, up to a limit.
 *
 * While the bus is lent to the stream (run), nothing else may use the
 * chip; hold takes it back, waiting for the piece under way. head and tail
 * count bytes: receiving, the stream adds at head and the owner takes from
 * tail; sending, the owner puts at head and the stream takes from tail. */
#define KUI_W5500_STREAM_WAIT_BYTES 256u
struct kui_w5500_stream {
    struct kui_w5500 *w;
    uint8_t *ring;
    uint32_t size;                 /* a power of two */
    volatile uint32_t head, tail;
    volatile bool lent, busy, stop, failed;
    unsigned socket, waits;
    bool sending;
    uint16_t pointer;              /* Sn_RX_RD or Sn_TX_WR, as the stream has moved it */
    uint16_t unsent;               /* sending: written to the chip, not yet in a SEND */
    uint32_t piece, max_piece;
    uint64_t started_us, busy_us;  /* time with a piece under way */
    uint8_t scratch[KUI_W5500_STREAM_WAIT_BYTES];
};
/* Needs w->async and an open TCP socket that nothing else reads or sends
 * on until kui_w5500_stream_end. */
bool kui_w5500_stream_begin(struct kui_w5500_stream *st, struct kui_w5500 *w, unsigned s, bool sending,
                            uint8_t *ring, uint32_t size);
void kui_w5500_stream_run(struct kui_w5500_stream *st);
/* False once the stream has failed (a frame failed or never ended). */
bool kui_w5500_stream_hold(struct kui_w5500_stream *st);
/* Receiving: bytes waiting at ring + (tail & (size - 1)), then taking them. */
uint32_t kui_w5500_stream_ready(const struct kui_w5500_stream *st);
void kui_w5500_stream_take(struct kui_w5500_stream *st, uint32_t bytes);
/* Sending: room at ring + (head & (size - 1)), then putting bytes there. */
uint32_t kui_w5500_stream_room(const struct kui_w5500_stream *st);
void kui_w5500_stream_put(struct kui_w5500_stream *st, uint32_t bytes);
/* Sending, while held: everything put is on the chip and in a SEND. */
bool kui_w5500_stream_sent(const struct kui_w5500_stream *st);
/* Where nothing else ends async frames (a host): ends those that have.
 * The owner calls it while it waits. */
void kui_w5500_stream_poll(struct kui_w5500_stream *st);
/* Holds the bus and forgets the stream, leaving the socket as the plain
 * calls expect it, even after the stream failed: what the stream wrote to
 * the chip is sent. Bytes still in the ring are the owner's. False if the
 * chip has failed or that SEND did. */
bool kui_w5500_stream_end(struct kui_w5500_stream *st);
#endif
