/* SPDX-License-Identifier: GPL-3.0-only */
/* WIZnet W5500 driver, written from WIZnet's W5500 datasheet. Independent
 * of the transport: every access is kui_w5500_bus.frame. */
#include "kui/w5500.h"
#include <string.h>

/* The chip takes a command within microseconds and finishes a software
 * reset within a millisecond; anything far longer means it is not there. */
#define COMMAND_MS 100u
#define RESET_MS 100u

static bool ready(const struct kui_w5500 *w) { return w && w->bus && w->bus->frame && !w->failed; }
static uint64_t now(struct kui_w5500 *w) { return w->bus->now_ms ? w->bus->now_ms(w->bus->ctx) : 0; }
static void pause(struct kui_w5500 *w, unsigned ms) { if(w->bus->pause) w->bus->pause(w->bus->ctx, ms); }
static bool frame(struct kui_w5500 *w, uint8_t block, uint16_t address, bool write, const void *out, void *in,
                  size_t bytes) {
    if(!ready(w)) return false;
    const uint8_t header[3] = {(uint8_t)(address >> 8), (uint8_t)address, (uint8_t)(block << 3 | (write ? 4u : 0u))};
    if(!w->bus->frame(w->bus->ctx, header, write ? out : NULL, write ? NULL : in, bytes)) {
        w->failed = true;
        return false;
    }
    return true;
}
bool kui_w5500_read(struct kui_w5500 *w, uint8_t block, uint16_t address, void *data, size_t bytes) {
    return data && frame(w, block, address, false, NULL, data, bytes);
}
bool kui_w5500_write(struct kui_w5500 *w, uint8_t block, uint16_t address, const void *data, size_t bytes) {
    return data && frame(w, block, address, true, data, NULL, bytes);
}
bool kui_w5500_read8(struct kui_w5500 *w, uint8_t block, uint16_t address, uint8_t *value) {
    return kui_w5500_read(w, block, address, value, 1);
}
bool kui_w5500_write8(struct kui_w5500 *w, uint8_t block, uint16_t address, uint8_t value) {
    return kui_w5500_write(w, block, address, &value, 1);
}
static bool read16_once(struct kui_w5500 *w, uint8_t block, uint16_t address, uint16_t *value) {
    uint8_t b[2];
    if(!kui_w5500_read(w, block, address, b, 2)) return false;
    *value = (uint16_t)(b[0] << 8 | b[1]);
    return true;
}
bool kui_w5500_read16(struct kui_w5500 *w, uint8_t block, uint16_t address, uint16_t *value) {
    uint16_t a, b;
    if(!value || !read16_once(w, block, address, &a)) return false;
    for(unsigned i = 0; i < 16; ++i) {
        if(!read16_once(w, block, address, &b)) return false;
        if(a == b) { *value = a; return true; }
        a = b;
    }
    /* Still changing under heavy traffic: report nothing this time rather
     * than a reading that may mix an old byte with a new one. */
    *value = 0;
    return true;
}
bool kui_w5500_write16(struct kui_w5500 *w, uint8_t block, uint16_t address, uint16_t value) {
    const uint8_t b[2] = {(uint8_t)(value >> 8), (uint8_t)value};
    return kui_w5500_write(w, block, address, b, 2);
}

static bool socket_valid(const struct kui_w5500 *w, unsigned s) { return ready(w) && s < KUI_W5500_SOCKETS; }
static bool command(struct kui_w5500 *w, unsigned s, uint8_t code) {
    if(!kui_w5500_write8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_CR, code)) return false;
    uint64_t start = now(w);
    for(unsigned polls = 0;; ++polls) {
        uint8_t value;
        if(!kui_w5500_read8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_CR, &value)) return false;
        if(!value) return true;
        if(now(w) - start > COMMAND_MS) { w->failed = true; return false; }
        if(polls >= 8) pause(w, 1);
    }
}
/* Sn_IR bits read so far and not yet handed to kui_w5500_events. A SEND
 * finishing (or timing out) also ends the wait for that socket's SEND. */
static bool collect(struct kui_w5500 *w, unsigned s) {
    uint8_t bits;
    if(!kui_w5500_read8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_IR, &bits)) return false;
    if(!bits) return true;
    if(!kui_w5500_write8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_IR, bits)) return false;
    w->pending[s] |= bits;
    if(bits & (KUI_W5500_IR_SENDOK | KUI_W5500_IR_TIMEOUT)) w->sending &= (uint8_t)~(1u << s);
    return true;
}

bool kui_w5500_reset(struct kui_w5500 *w, uint8_t *version) {
    if(!w || !w->bus || !w->bus->frame) return false;
    w->failed = false;
    w->sending = 0;
    memset(w->pending, 0, sizeof(w->pending));
    for(unsigned s = 0; s < KUI_W5500_SOCKETS; ++s) w->rx_kb[s] = w->tx_kb[s] = 2;
    if(version) *version = 0;
    if(!kui_w5500_write8(w, KUI_W5500_COMMON, KUI_W5500_MR, KUI_W5500_MR_RST)) return false;
    uint64_t start = now(w);
    bool reset = false;
    while(!reset) {
        uint8_t mode;
        if(!kui_w5500_read8(w, KUI_W5500_COMMON, KUI_W5500_MR, &mode)) return false;
        reset = !(mode & KUI_W5500_MR_RST);
        if(!reset && now(w) - start > RESET_MS) break;
        if(!reset) pause(w, 1);
    }
    /* Read the version either way: 0xff or 0x00 tells the reader nothing
     * is driving the data line back. */
    uint8_t found;
    if(!kui_w5500_read8(w, KUI_W5500_COMMON, KUI_W5500_VERSIONR, &found)) return false;
    if(version) *version = found;
    if(!reset || found != KUI_W5500_VERSION) { w->failed = true; return false; }
    return true;
}
bool kui_w5500_bus_check(struct kui_w5500 *w, unsigned rounds) {
    /* Gateway, netmask, MAC and own address are 18 plain read/write bytes
     * in a row; the caller sets their real values afterwards. */
    uint8_t out[18], in[18];
    uint32_t seed = 0x4b554931u;
    for(unsigned round = 0; round < rounds; ++round) {
        for(unsigned i = 0; i < sizeof(out); ++i) {
            seed = seed * 1103515245u + 12345u;
            out[i] = (uint8_t)(seed >> 16);
        }
        /* Fixed patterns first: all ones, all zeros and both alternations. */
        if(round < 4) memset(out, round == 0 ? 0xff : round == 1 ? 0x00 : round == 2 ? 0x55 : 0xaa, sizeof(out));
        if(!kui_w5500_write(w, KUI_W5500_COMMON, KUI_W5500_GAR, out, sizeof(out)) ||
           !kui_w5500_read(w, KUI_W5500_COMMON, KUI_W5500_GAR, in, sizeof(in))) return false;
        if(memcmp(out, in, sizeof(out))) return false;
    }
    static const uint8_t zero[18];
    return kui_w5500_write(w, KUI_W5500_COMMON, KUI_W5500_GAR, zero, sizeof(zero));
}
bool kui_w5500_link(struct kui_w5500 *w, struct kui_w5500_link *out) {
    uint8_t phy;
    if(!out || !kui_w5500_read8(w, KUI_W5500_COMMON, KUI_W5500_PHYCFGR, &phy)) return false;
    out->up = phy & KUI_W5500_PHY_LINK;
    out->fast = phy & KUI_W5500_PHY_100;
    out->full = phy & KUI_W5500_PHY_FULL;
    return true;
}
static bool size_valid(uint8_t kb) { return kb == 0 || kb == 1 || kb == 2 || kb == 4 || kb == 8 || kb == 16; }
bool kui_w5500_buffers(struct kui_w5500 *w, const uint8_t rx_kb[KUI_W5500_SOCKETS],
                       const uint8_t tx_kb[KUI_W5500_SOCKETS]) {
    if(!ready(w) || !rx_kb || !tx_kb) return false;
    unsigned rx = 0, tx = 0;
    for(unsigned s = 0; s < KUI_W5500_SOCKETS; ++s) {
        if(!size_valid(rx_kb[s]) || !size_valid(tx_kb[s])) return false;
        rx += rx_kb[s];
        tx += tx_kb[s];
    }
    if(rx > 16 || tx > 16) return false;
    for(unsigned s = 0; s < KUI_W5500_SOCKETS; ++s) {
        if(!kui_w5500_write8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_RXBUF_SIZE, rx_kb[s]) ||
           !kui_w5500_write8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_TXBUF_SIZE, tx_kb[s])) return false;
        w->rx_kb[s] = rx_kb[s];
        w->tx_kb[s] = tx_kb[s];
    }
    return true;
}
bool kui_w5500_set_mac(struct kui_w5500 *w, const uint8_t mac[6]) {
    return mac && kui_w5500_write(w, KUI_W5500_COMMON, KUI_W5500_SHAR, mac, 6);
}
bool kui_w5500_set_ipv4(struct kui_w5500 *w, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gateway[4]) {
    return ip && mask && gateway && kui_w5500_write(w, KUI_W5500_COMMON, KUI_W5500_GAR, gateway, 4) &&
        kui_w5500_write(w, KUI_W5500_COMMON, KUI_W5500_SUBR, mask, 4) &&
        kui_w5500_write(w, KUI_W5500_COMMON, KUI_W5500_SIPR, ip, 4);
}

bool kui_w5500_status(struct kui_w5500 *w, unsigned s, uint8_t *state) {
    return socket_valid(w, s) && state && kui_w5500_read8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_SR, state);
}
static bool expect(struct kui_w5500 *w, unsigned s, uint8_t wanted) {
    /* The new state shows as soon as the chip has taken the command. */
    for(unsigned i = 0; i < 4; ++i) {
        uint8_t state;
        if(!kui_w5500_status(w, s, &state)) return false;
        if(state == wanted) return true;
        pause(w, 1);
    }
    return false;
}
bool kui_w5500_close(struct kui_w5500 *w, unsigned s) {
    if(!socket_valid(w, s) || !command(w, s, KUI_W5500_CLOSE)) return false;
    w->sending &= (uint8_t)~(1u << s);
    w->pending[s] = 0;
    return kui_w5500_write8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_IR, 0xff) && expect(w, s, KUI_W5500_CLOSED);
}
bool kui_w5500_open(struct kui_w5500 *w, unsigned s, uint8_t mode, uint16_t port) {
    uint8_t protocol = mode & 0x0fu, wanted = protocol == KUI_W5500_TCP ? KUI_W5500_INIT :
        protocol == KUI_W5500_UDP ? KUI_W5500_UDP_OPEN : protocol == KUI_W5500_MACRAW ? KUI_W5500_MACRAW_OPEN : 0xffu;
    if(!socket_valid(w, s) || wanted == 0xffu || (protocol == KUI_W5500_MACRAW && s) || !w->rx_kb[s]) return false;
    return kui_w5500_close(w, s) && kui_w5500_write8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_MR, mode) &&
        kui_w5500_write16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_PORT, port) &&
        command(w, s, KUI_W5500_OPEN) && expect(w, s, wanted);
}
bool kui_w5500_listen(struct kui_w5500 *w, unsigned s) {
    return socket_valid(w, s) && command(w, s, KUI_W5500_LISTEN) && expect(w, s, KUI_W5500_LISTENING);
}
bool kui_w5500_connect(struct kui_w5500 *w, unsigned s, const uint8_t ip[4], uint16_t port) {
    return socket_valid(w, s) && ip && kui_w5500_write(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_DIPR, ip, 4) &&
        kui_w5500_write16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_DPORT, port) && command(w, s, KUI_W5500_CONNECT);
}
bool kui_w5500_disconnect(struct kui_w5500 *w, unsigned s) {
    return socket_valid(w, s) && command(w, s, KUI_W5500_DISCON);
}
bool kui_w5500_events(struct kui_w5500 *w, unsigned s, uint8_t *events) {
    if(!socket_valid(w, s) || !events || !collect(w, s)) return false;
    *events = w->pending[s];
    w->pending[s] = 0;
    return true;
}
bool kui_w5500_peer(struct kui_w5500 *w, unsigned s, uint8_t ip[4], uint16_t *port) {
    uint8_t b[6];
    if(!socket_valid(w, s) || !ip || !port || !kui_w5500_read(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_DIPR, b, 6))
        return false;
    memcpy(ip, b, 4);
    *port = (uint16_t)(b[4] << 8 | b[5]);
    return true;
}
bool kui_w5500_keepalive(struct kui_w5500 *w, unsigned s, unsigned seconds) {
    unsigned steps = (seconds + 4u) / 5u;
    return socket_valid(w, s) && kui_w5500_write8(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_KPALVTR,
        (uint8_t)(steps > 255u ? 255u : steps));
}

bool kui_w5500_received(struct kui_w5500 *w, unsigned s, uint16_t *bytes) {
    if(!socket_valid(w, s) || !bytes || !kui_w5500_read16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_RX_RSR, bytes))
        return false;
    /* More than the buffer holds is a misread, not data. */
    if(*bytes > w->rx_kb[s] * 1024u) { w->failed = true; return false; }
    return true;
}
bool kui_w5500_receive(struct kui_w5500 *w, unsigned s, void *data, uint16_t bytes) {
    uint16_t at;
    if(!socket_valid(w, s) || (!data && bytes)) return false;
    if(!bytes) return true;
    return kui_w5500_read16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_RX_RD, &at) &&
        kui_w5500_read(w, KUI_W5500_SOCKET_RX(s), at, data, bytes) &&
        kui_w5500_write16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_RX_RD, (uint16_t)(at + bytes)) &&
        command(w, s, KUI_W5500_RECV);
}
bool kui_w5500_room(struct kui_w5500 *w, unsigned s, uint16_t *bytes) {
    if(!socket_valid(w, s) || !bytes) return false;
    *bytes = 0;
    if(w->sending & (1u << s)) {
        if(!collect(w, s)) return false;
        /* A SEND that timed out: the chip has closed the connection. */
        if(w->pending[s] & KUI_W5500_IR_TIMEOUT) return false;
        if(w->sending & (1u << s)) return true;
    }
    if(!kui_w5500_read16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_TX_FSR, bytes)) return false;
    if(*bytes > w->tx_kb[s] * 1024u) { w->failed = true; return false; }
    return true;
}
bool kui_w5500_send(struct kui_w5500 *w, unsigned s, const void *data, uint16_t bytes) {
    uint16_t at;
    if(!socket_valid(w, s) || !data || !bytes || (w->sending & (1u << s))) return false;
    if(!kui_w5500_read16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_TX_WR, &at) ||
       !kui_w5500_write(w, KUI_W5500_SOCKET_TX(s), at, data, bytes) ||
       !kui_w5500_write16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_TX_WR, (uint16_t)(at + bytes)) ||
       !command(w, s, KUI_W5500_SEND)) return false;
    w->sending |= (uint8_t)(1u << s);
    return true;
}
/* Waits up to 100 ms for room for `bytes`. */
static bool room_for(struct kui_w5500 *w, unsigned s, size_t bytes) {
    uint64_t start = now(w);
    for(;;) {
        uint16_t room;
        if(!kui_w5500_room(w, s, &room)) return false;
        if(room >= bytes) return true;
        if(now(w) - start > COMMAND_MS) return false;
        pause(w, 1);
    }
}
bool kui_w5500_datagram_send(struct kui_w5500 *w, unsigned s, const uint8_t ip[4], uint16_t port,
                             const void *data, uint16_t bytes) {
    return socket_valid(w, s) && ip && bytes && bytes <= w->tx_kb[s] * 1024u && room_for(w, s, bytes) &&
        kui_w5500_write(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_DIPR, ip, 4) &&
        kui_w5500_write16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_DPORT, port) && kui_w5500_send(w, s, data, bytes);
}
/* Drops everything waiting: the receive ring no longer makes sense. */
static bool discard(struct kui_w5500 *w, unsigned s, uint16_t waiting) {
    uint16_t at;
    return kui_w5500_read16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_RX_RD, &at) &&
        kui_w5500_write16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_RX_RD, (uint16_t)(at + waiting)) &&
        command(w, s, KUI_W5500_RECV);
}
bool kui_w5500_datagram_receive(struct kui_w5500 *w, unsigned s, uint8_t ip[4], uint16_t *port,
                                void *data, size_t capacity, size_t *bytes) {
    uint16_t waiting, at;
    uint8_t head[8];
    if(!bytes || !ip || !port || !kui_w5500_received(w, s, &waiting)) return false;
    *bytes = 0;
    if(waiting < sizeof(head)) return true;
    if(!kui_w5500_read16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_RX_RD, &at) ||
       !kui_w5500_read(w, KUI_W5500_SOCKET_RX(s), at, head, sizeof(head))) return false;
    size_t length = (size_t)head[6] << 8 | head[7];
    if(length > waiting - sizeof(head)) return discard(w, s, waiting);
    memcpy(ip, head, 4);
    *port = (uint16_t)(head[4] << 8 | head[5]);
    if(length <= capacity && length) {
        if(!data || !kui_w5500_read(w, KUI_W5500_SOCKET_RX(s), (uint16_t)(at + sizeof(head)), data, length))
            return false;
        *bytes = length;
    }
    return kui_w5500_write16(w, KUI_W5500_SOCKET_REGS(s), KUI_W5500_SN_RX_RD, (uint16_t)(at + sizeof(head) + length)) &&
        command(w, s, KUI_W5500_RECV);
}
bool kui_w5500_frame_receive(struct kui_w5500 *w, uint8_t *frame_out, size_t capacity, size_t *bytes) {
    uint16_t waiting, at;
    uint8_t head[2];
    if(!bytes || !kui_w5500_received(w, 0, &waiting)) return false;
    *bytes = 0;
    if(waiting < sizeof(head)) return true;
    if(!kui_w5500_read16(w, KUI_W5500_SOCKET_REGS(0), KUI_W5500_SN_RX_RD, &at) ||
       !kui_w5500_read(w, KUI_W5500_SOCKET_RX(0), at, head, sizeof(head))) return false;
    /* Each frame is stored behind a length that counts those two bytes. */
    size_t length = (size_t)head[0] << 8 | head[1];
    if(length < sizeof(head) || length > waiting) return discard(w, 0, waiting);
    size_t size = length - sizeof(head);
    if(size && size <= capacity) {
        if(!frame_out || !kui_w5500_read(w, KUI_W5500_SOCKET_RX(0), (uint16_t)(at + sizeof(head)), frame_out, size))
            return false;
        *bytes = size;
    }
    return kui_w5500_write16(w, KUI_W5500_SOCKET_REGS(0), KUI_W5500_SN_RX_RD, (uint16_t)(at + length)) &&
        command(w, 0, KUI_W5500_RECV);
}
bool kui_w5500_frame_send(struct kui_w5500 *w, const uint8_t *frame_in, size_t bytes) {
    return frame_in && bytes && bytes <= w->tx_kb[0] * 1024u && room_for(w, 0, bytes) &&
        kui_w5500_send(w, 0, frame_in, (uint16_t)bytes);
}
