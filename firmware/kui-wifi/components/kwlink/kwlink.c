/* SPDX-License-Identifier: MIT */
/* The K-UI Wi-Fi link's frames: header, checksum, sessions and go-back-N
 * delivery with a window of two. See PROTOCOL.md. */
#include "kwlink.h"
#include <string.h>

static const uint32_t crc_table[256] = {
    0x00000000u, 0x77073096u, 0xee0e612cu, 0x990951bau, 0x076dc419u, 0x706af48fu,
    0xe963a535u, 0x9e6495a3u, 0x0edb8832u, 0x79dcb8a4u, 0xe0d5e91eu, 0x97d2d988u,
    0x09b64c2bu, 0x7eb17cbdu, 0xe7b82d07u, 0x90bf1d91u, 0x1db71064u, 0x6ab020f2u,
    0xf3b97148u, 0x84be41deu, 0x1adad47du, 0x6ddde4ebu, 0xf4d4b551u, 0x83d385c7u,
    0x136c9856u, 0x646ba8c0u, 0xfd62f97au, 0x8a65c9ecu, 0x14015c4fu, 0x63066cd9u,
    0xfa0f3d63u, 0x8d080df5u, 0x3b6e20c8u, 0x4c69105eu, 0xd56041e4u, 0xa2677172u,
    0x3c03e4d1u, 0x4b04d447u, 0xd20d85fdu, 0xa50ab56bu, 0x35b5a8fau, 0x42b2986cu,
    0xdbbbc9d6u, 0xacbcf940u, 0x32d86ce3u, 0x45df5c75u, 0xdcd60dcfu, 0xabd13d59u,
    0x26d930acu, 0x51de003au, 0xc8d75180u, 0xbfd06116u, 0x21b4f4b5u, 0x56b3c423u,
    0xcfba9599u, 0xb8bda50fu, 0x2802b89eu, 0x5f058808u, 0xc60cd9b2u, 0xb10be924u,
    0x2f6f7c87u, 0x58684c11u, 0xc1611dabu, 0xb6662d3du, 0x76dc4190u, 0x01db7106u,
    0x98d220bcu, 0xefd5102au, 0x71b18589u, 0x06b6b51fu, 0x9fbfe4a5u, 0xe8b8d433u,
    0x7807c9a2u, 0x0f00f934u, 0x9609a88eu, 0xe10e9818u, 0x7f6a0dbbu, 0x086d3d2du,
    0x91646c97u, 0xe6635c01u, 0x6b6b51f4u, 0x1c6c6162u, 0x856530d8u, 0xf262004eu,
    0x6c0695edu, 0x1b01a57bu, 0x8208f4c1u, 0xf50fc457u, 0x65b0d9c6u, 0x12b7e950u,
    0x8bbeb8eau, 0xfcb9887cu, 0x62dd1ddfu, 0x15da2d49u, 0x8cd37cf3u, 0xfbd44c65u,
    0x4db26158u, 0x3ab551ceu, 0xa3bc0074u, 0xd4bb30e2u, 0x4adfa541u, 0x3dd895d7u,
    0xa4d1c46du, 0xd3d6f4fbu, 0x4369e96au, 0x346ed9fcu, 0xad678846u, 0xda60b8d0u,
    0x44042d73u, 0x33031de5u, 0xaa0a4c5fu, 0xdd0d7cc9u, 0x5005713cu, 0x270241aau,
    0xbe0b1010u, 0xc90c2086u, 0x5768b525u, 0x206f85b3u, 0xb966d409u, 0xce61e49fu,
    0x5edef90eu, 0x29d9c998u, 0xb0d09822u, 0xc7d7a8b4u, 0x59b33d17u, 0x2eb40d81u,
    0xb7bd5c3bu, 0xc0ba6cadu, 0xedb88320u, 0x9abfb3b6u, 0x03b6e20cu, 0x74b1d29au,
    0xead54739u, 0x9dd277afu, 0x04db2615u, 0x73dc1683u, 0xe3630b12u, 0x94643b84u,
    0x0d6d6a3eu, 0x7a6a5aa8u, 0xe40ecf0bu, 0x9309ff9du, 0x0a00ae27u, 0x7d079eb1u,
    0xf00f9344u, 0x8708a3d2u, 0x1e01f268u, 0x6906c2feu, 0xf762575du, 0x806567cbu,
    0x196c3671u, 0x6e6b06e7u, 0xfed41b76u, 0x89d32be0u, 0x10da7a5au, 0x67dd4accu,
    0xf9b9df6fu, 0x8ebeeff9u, 0x17b7be43u, 0x60b08ed5u, 0xd6d6a3e8u, 0xa1d1937eu,
    0x38d8c2c4u, 0x4fdff252u, 0xd1bb67f1u, 0xa6bc5767u, 0x3fb506ddu, 0x48b2364bu,
    0xd80d2bdau, 0xaf0a1b4cu, 0x36034af6u, 0x41047a60u, 0xdf60efc3u, 0xa867df55u,
    0x316e8eefu, 0x4669be79u, 0xcb61b38cu, 0xbc66831au, 0x256fd2a0u, 0x5268e236u,
    0xcc0c7795u, 0xbb0b4703u, 0x220216b9u, 0x5505262fu, 0xc5ba3bbeu, 0xb2bd0b28u,
    0x2bb45a92u, 0x5cb36a04u, 0xc2d7ffa7u, 0xb5d0cf31u, 0x2cd99e8bu, 0x5bdeae1du,
    0x9b64c2b0u, 0xec63f226u, 0x756aa39cu, 0x026d930au, 0x9c0906a9u, 0xeb0e363fu,
    0x72076785u, 0x05005713u, 0x95bf4a82u, 0xe2b87a14u, 0x7bb12baeu, 0x0cb61b38u,
    0x92d28e9bu, 0xe5d5be0du, 0x7cdcefb7u, 0x0bdbdf21u, 0x86d3d2d4u, 0xf1d4e242u,
    0x68ddb3f8u, 0x1fda836eu, 0x81be16cdu, 0xf6b9265bu, 0x6fb077e1u, 0x18b74777u,
    0x88085ae6u, 0xff0f6a70u, 0x66063bcau, 0x11010b5cu, 0x8f659effu, 0xf862ae69u,
    0x616bffd3u, 0x166ccf45u, 0xa00ae278u, 0xd70dd2eeu, 0x4e048354u, 0x3903b3c2u,
    0xa7672661u, 0xd06016f7u, 0x4969474du, 0x3e6e77dbu, 0xaed16a4au, 0xd9d65adcu,
    0x40df0b66u, 0x37d83bf0u, 0xa9bcae53u, 0xdebb9ec5u, 0x47b2cf7fu, 0x30b5ffe9u,
    0xbdbdf21cu, 0xcabac28au, 0x53b39330u, 0x24b4a3a6u, 0xbad03605u, 0xcdd70693u,
    0x54de5729u, 0x23d967bfu, 0xb3667a2eu, 0xc4614ab8u, 0x5d681b02u, 0x2a6f2b94u,
    0xb40bbe37u, 0xc30c8ea1u, 0x5a05df1bu, 0x2d02ef8du,
};

uint32_t kwl_crc32(uint32_t crc, const void *data, size_t len) {
    const uint8_t *p = data;
    crc = ~crc;
    while(len--) crc = crc_table[(crc ^ *p++) & 0xffu] ^ (crc >> 8);
    return ~crc;
}

static uint16_t capacity_for(uint32_t wanted) {
    return (uint16_t)(wanted < KWL_POLL ? KWL_POLL : wanted > KWL_PAYLOAD_MAX ? KWL_PAYLOAD_MAX : wanted);
}
static struct kwl_out *nth(struct kwl *l, unsigned i) { return &l->out[(l->out_head + i) % KWL_WINDOW]; }
/* Numbering starts again: nothing sent, nothing received. */
static void restart(struct kwl *l) {
    l->next_seq = l->expect = 0;
    l->acked_any = false;
    l->out_head = l->out_count = 0;
    l->want = 0;
    l->current = -1;
    l->current_len = 0;
}

void kwl_init(struct kwl *l, enum kwl_role role) {
    memset(l, 0, sizeof *l);
    l->role = role;
    l->current = -1;
    l->promised[0] = l->promised[1] = l->need = KWL_POLL;
    l->peer_window = role == KWL_BRIDGE ? KWL_POLL : 0;
}
void kwl_host_sync(struct kwl *l, uint16_t session) {
    restart(l);
    l->session = session ? session : 1u;
    l->live = false;
    l->peer_window = 0;
}
void kwl_bridge_reset(struct kwl *l) {
    restart(l);
    l->session = 0;
    l->live = false;
    l->peer_window = KWL_POLL;
}
bool kwl_busy(const struct kwl *l) { return l->out_count != 0; }

/* Which frame goes next: one to (re)send, a new one, or none (-1). */
static int choose(struct kwl *l, const struct kwl_io *io) {
    if(!l->live) return -1;
    /* Still unacknowledged two transfers after it went out: the answer
     * could have come back by now, so it was lost. Go back to it; the peer
     * dropped everything after it too. */
    if(l->out_count && nth(l, 0)->sent && l->clock - nth(l, 0)->sent_at >= 2u)
        for(unsigned i = 0; i < l->out_count; ++i) nth(l, i)->sent = false;
    for(unsigned i = 0; i < l->out_count; ++i)
        if(!nth(l, i)->sent) {
            ++l->stats.resent;
            return (int)((l->out_head + i) % KWL_WINDOW);
        }
    if(l->out_count >= KWL_WINDOW || !io || !io->fill) return -1;
    size_t capacity = l->role == KWL_BRIDGE ? capacity_for(l->peer_window) : KWL_PAYLOAD_MAX;
    struct kwl_out *o = nth(l, l->out_count);
    size_t n = io->fill(io->ctx, o->payload, capacity);
    if(!n) return -1;
    if(n > capacity) n = capacity;
    o->len = (uint16_t)n;
    o->seq = l->next_seq++;
    o->sent = false;
    ++l->out_count;
    ++l->stats.sent;
    return (int)(o - l->out);
}

size_t kwl_build(struct kwl *l, uint8_t frame[KWL_FRAME_MAX], const struct kwl_io *io) {
    int index = choose(l, io);
    uint8_t flags = l->role == KWL_BRIDGE ? KWL_F_BRIDGE : 0, seq = 0;
    uint16_t len = 0, window;
    if(index >= 0) {
        struct kwl_out *o = &l->out[index];
        o->sent = true;
        o->sent_at = l->clock;
        flags |= KWL_F_DATA;
        len = o->len;
        seq = o->seq;
        memcpy(frame + KWL_HEADER, o->payload, len);
    }
    if(l->acked_any) flags |= KWL_F_ACK;
    if(l->role == KWL_HOST) {
        if(!l->live) flags |= KWL_F_SYNC;
        /* This transfer must hold what the last two frames promised (the
         * bridge may have missed the newer one); this frame promises the
         * bridge's latest hint. */
        l->need = l->promised[0] > l->promised[1] ? l->promised[0] : l->promised[1];
        l->promised[1] = l->promised[0];
        l->promised[0] = window = capacity_for(l->peer_window);
    } else {
        if(!l->live) flags |= KWL_F_NOSESSION;
        size_t pending = io && io->pending ? io->pending(io->ctx) : 0;
        window = (uint16_t)(pending > 0xffffu ? 0xffffu : pending);
    }
    frame[0] = KWL_MAGIC0;
    frame[1] = KWL_MAGIC1;
    frame[2] = flags;
    frame[3] = seq;
    frame[4] = (uint8_t)(l->expect - 1u);
    frame[5] = 0;
    kwl_put16(frame + 6, len);
    kwl_put16(frame + 8, window);
    kwl_put16(frame + 10, l->session);
    kwl_put32(frame + 12, kwl_crc32(kwl_crc32(0, frame, 12), frame + KWL_HEADER, len));
    l->current = index;
    l->current_len = KWL_HEADER + len;
    return l->current_len;
}

size_t kwl_host_length(const struct kwl *l, size_t frame_len) {
    size_t n = KWL_HEADER + (l->need > l->want ? l->need : l->want);
    if(frame_len > n) n = frame_len;
    n = (n + 3u) & ~(size_t)3u;
    return n > KWL_FRAME_MAX ? KWL_FRAME_MAX : n;
}

/* The peer has everything up to `ack`: those frames are done. */
static void acknowledge(struct kwl *l, uint8_t ack) {
    while(l->out_count && (uint8_t)(ack - nth(l, 0)->seq) < 0x80u) {
        l->out_head = (l->out_head + 1u) % KWL_WINDOW;
        --l->out_count;
    }
}

enum kwl_result kwl_receive(struct kwl *l, const uint8_t *in, size_t clocked, const struct kwl_io *io) {
    ++l->clock;
    ++l->stats.transfers;
    /* The bridge learns how many bytes were clocked: a frame cut short
     * never arrived, so it goes again, and so does every frame after it
     * (the host drops those as out of order). */
    if(l->role == KWL_BRIDGE && l->current >= 0 && clocked < l->current_len) {
        bool after = false;
        for(unsigned i = 0; i < l->out_count; ++i) {
            if(nth(l, i) == &l->out[l->current]) after = true;
            if(after) nth(l, i)->sent = false;
        }
    }
    l->current = -1;
    if(!in || clocked < KWL_HEADER || in[0] != KWL_MAGIC0 || in[1] != KWL_MAGIC1) return KWL_NOTHING;
    uint8_t flags = in[2];
    if(((flags & KWL_F_BRIDGE) != 0) != (l->role == KWL_HOST)) return KWL_NOTHING;
    size_t len = kwl_get16(in + 6);
    if(len > KWL_PAYLOAD_MAX) {
        ++l->stats.bad;
        return KWL_BAD;
    }
    if(KWL_HEADER + len > clocked) {
        ++l->stats.truncated;
        /* The header is unchecked, so its length is only a size to try. */
        if(l->role == KWL_HOST) l->want = (uint16_t)len;
        return KWL_TRUNCATED;
    }
    if(kwl_crc32(kwl_crc32(0, in, 12), in + KWL_HEADER, len) != kwl_get32(in + 12)) {
        ++l->stats.bad;
        return KWL_BAD;
    }
    uint16_t session = kwl_get16(in + 10);
    enum kwl_result result = KWL_OK;
    if(l->role == KWL_HOST) {
        l->want = 0;
        bool ours = !(flags & KWL_F_NOSESSION) && session == l->session;
        if(!l->live) {
            if(!ours) {
                ++l->stats.ignored;
                return KWL_IGNORED;
            }
            restart(l);
            l->live = true;
            result = KWL_SYNCED;
        } else if(!ours) {
            l->live = false;
            return KWL_LOST;
        }
    } else if(flags & KWL_F_SYNC) {
        /* The host repeats SYNC until it sees our answer; a repeat of the
         * session we already have must not restart the numbering. */
        if(l->live && session == l->session) return KWL_OK;
        restart(l);
        l->session = session ? session : 1u;
        l->live = true;
        l->peer_window = capacity_for(kwl_get16(in + 8));
        return KWL_SYNCED;
    } else if(!l->live || session != l->session) {
        ++l->stats.ignored;
        return KWL_IGNORED;
    }
    l->peer_window = kwl_get16(in + 8);
    if(flags & KWL_F_ACK) acknowledge(l, in[4]);
    if((flags & KWL_F_DATA) && len) {
        uint8_t seq = in[3], behind = (uint8_t)(l->expect - seq);
        if(!behind) {
            ++l->expect;
            l->acked_any = true;
            ++l->stats.received;
            if(io && io->deliver) io->deliver(io->ctx, in + KWL_HEADER, len);
        } else if(behind <= 0x80u) {
            ++l->stats.duplicates;
        } else {
            ++l->stats.gaps;
        }
    }
    return result;
}
