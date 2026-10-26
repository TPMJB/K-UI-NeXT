/* SPDX-License-Identifier: GPL-3.0-only */
/* Independently authored from pinned KOS DMA interfaces and public Maple
 * protocol documentation. See docs/vmu-protocol.md for source provenance. */
#include "kui/vmu_emulation.h"
#include <string.h>

enum {
    CMD_INFO = 1, CMD_ALLINFO = 2, CMD_RESET = 3, CMD_KILL = 4,
    RESP_INFO = 5, RESP_ALLINFO = 6, RESP_OK = 7, RESP_DATA = 8,
    CMD_MEDIA = 10, CMD_READ = 11, CMD_WRITE = 12, CMD_SYNC = 13,
    RESP_FILEERR = 0xfb, RESP_AGAIN = 0xfc, RESP_BADCMD = 0xfd,
    RESP_BADFUNC = 0xfe,
    ERR_PARTITION = 1, ERR_PHASE = 2, ERR_BLOCK = 4,
    ERR_IO = 8, ERR_LENGTH = 16
};

static void put16(uint8_t *p, unsigned value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value) {
    for(unsigned i = 0; i < 4; ++i)
        p[i] = (uint8_t)(value >> (8 * i));
}

static int reply(uint8_t *out, size_t *length, uint8_t command,
                 uint8_t destination, uint8_t source, unsigned words) {
    out[0] = command;
    out[1] = destination;
    out[2] = source;
    out[3] = (uint8_t)words;
    *length = 4u + words * 4u;
    return 1;
}

static int file_error(uint8_t *out, size_t *length,
                      uint8_t host, uint8_t address, uint8_t error) {
    /* Error flags are a protocol big-endian numeric word, not a native int. */
    out[4] = 0;
    out[5] = 0;
    out[6] = 0;
    out[7] = error;
    return reply(out, length, RESP_FILEERR, host, address, 1);
}

int kui_vmu_emulation_init(kui_vmu_emulation_t *s, uint8_t address,
                           const struct kui_vmu_emulation_io *io) {
    unsigned unit = address & 0x3fu;
    if(!s || !io || !io->read_block || !io->commit_block ||
       !unit || unit > 0x10u || (unit & (unit - 1u)))
        return -1;
    memset(s, 0, sizeof(*s));
    s->io = *io;
    s->address = address;
    s->initialized = 1;
    return 0;
}

void kui_vmu_emulation_reset(kui_vmu_emulation_t *s) {
    if(s) {
        s->active = 0;
        s->phases = 0;
        s->committed_valid = 0;
        s->read_valid = 0;
        s->commit_attempted = 0;
        s->killed = 0;
    }
}

static void device_info(uint8_t *payload, int extended) {
    /* Retail device classifiers use this standard product-class name before
     * considering the storage function. Keep the actual emulation identity
     * in producer/version; do not advertise unsupported LCD/clock functions. */
    static const char name[] = "Visual Memory";
    static const char producer[] = "Independent K-UI memory card emulation";
    static const char version[] = "K-UI experimental storage only; 128 KiB; durable commit";
    memset(payload, 0, extended ? 192u : 112u);
    put32(payload, KUI_VMU_EMU_FUNC_MEMCARD);
    /* BE protocol value 0x000f4100: 1 partition, 512 bytes/block,
     * four writes and one read/block; no extra data CRC, not removable. */
    put32(payload + 4, UINT32_C(0x00410f00));
    payload[16] = 0xff;
    memset(payload + 18, ' ', 90);
    memcpy(payload + 18, name, sizeof(name) - 1);
    memcpy(payload + 48, producer, sizeof(producer) - 1);
    if(extended) {
        memset(payload + 112, ' ', 80);
        memcpy(payload + 112, version, sizeof(version) - 1);
    }
}

static void media_info(uint8_t *payload) {
    /* Fixed standard 200-user-block card geometry. These are little-endian
     * fields in the media structure, matching the root block structures. */
    memset(payload, 0, 28);
    put32(payload, KUI_VMU_EMU_FUNC_MEMCARD);
    put16(payload + 4, 255);  /* last physical block, not a block count */
    put16(payload + 6, 0);    /* partition */
    put16(payload + 8, 255);  /* system/root */
    put16(payload + 10, 254); /* FAT start */
    put16(payload + 12, 1);   /* FAT blocks */
    put16(payload + 14, 253); /* directory start; grows downwards */
    put16(payload + 16, 13);  /* directory blocks */
    put16(payload + 18, 0);   /* card icon */
    put16(payload + 20, 200); /* standard VMU user-block limit */
    put16(payload + 22, 31);  /* standard VMU compatibility parameter */
    /* Last word = executable-file information; none. */
}

int kui_vmu_emulation_handle(kui_vmu_emulation_t *s,
                            const uint8_t *request, size_t request_bytes,
                            uint8_t *response, size_t capacity,
                            size_t *response_bytes) {
    unsigned command, phase, block;
    uint8_t host, address;
    size_t needed = 8;
    int io_result;

    if(response_bytes)
        *response_bytes = 0;
    if(!s || !s->initialized || !request || !response || !response_bytes ||
       request_bytes < 4)
        return -1;
    address = s->address;
    host = (uint8_t)(address & 0xc0u);
    if(request[1] != address || request[2] != host)
        return 0;
    if(s->killed) {
        if(capacity < 4) return -2;
        memset(response, 0xff, 4);
        *response_bytes = 4;
        return 1;
    }
    command = request[0];
    /* Reserve maximum possible response before doing anything with storage
     * or a partial transaction; an output-buffer failure is retry-safe. */
    if(command == CMD_INFO) needed = 116;
    else if(command == CMD_ALLINFO) needed = 196;
    else if(command == CMD_MEDIA) needed = 32;
    else if(command == CMD_READ) needed = KUI_VMU_EMU_MAX_RESPONSE_BYTES;
    if(capacity < needed)
        return -2;
    if(request_bytes != 4u + 4u * request[3])
        return file_error(response, response_bytes, host, address, ERR_LENGTH);

    if(command >= CMD_INFO && command <= CMD_KILL) {
        if(request_bytes != 4)
            return file_error(response, response_bytes, host, address, ERR_LENGTH);
        if(command == CMD_RESET || command == CMD_KILL) {
            kui_vmu_emulation_reset(s);
            s->killed = (uint8_t)(command == CMD_KILL);
            return reply(response, response_bytes, RESP_OK, host, address, 0);
        }
        device_info(response + 4, command == CMD_ALLINFO);
        return reply(response, response_bytes,
                     command == CMD_INFO ? RESP_INFO : RESP_ALLINFO,
                     host, address, command == CMD_INFO ? 28 : 48);
    }

    if(command != CMD_MEDIA && command != CMD_READ &&
       command != CMD_WRITE && command != CMD_SYNC) {
        /* An unsupported function remains distinguishable from unsupported
         * commands when a standard GETCOND/SETCOND request names it. */
        if((command == 9 || command == 14) && request_bytes >= 8 &&
           memcmp(request + 4, "\0\0\0\2", 4))
            return reply(response, response_bytes, RESP_BADFUNC, host, address, 0);
        return reply(response, response_bytes, RESP_BADCMD, host, address, 0);
    }
    if(request_bytes != (command == CMD_WRITE ? 140u : 12u))
        return file_error(response, response_bytes, host, address, ERR_LENGTH);
    if(memcmp(request + 4, "\0\0\0\2", 4))
        return reply(response, response_bytes, RESP_BADFUNC, host, address, 0);
    if(request[8])
        return file_error(response, response_bytes, host, address, ERR_PARTITION);
    phase = request[9];
    block = (unsigned)request[10] * 256u + request[11];
    if(block >= KUI_VMU_EMU_BLOCK_COUNT)
        return file_error(response, response_bytes, host, address, ERR_BLOCK);

    if(command == CMD_MEDIA) {
        if(phase || block)
            return file_error(response, response_bytes, host, address,
                              phase ? ERR_PHASE : ERR_BLOCK);
        media_info(response + 4);
        return reply(response, response_bytes, RESP_DATA, host, address, 7);
    }
    if(command == CMD_READ) {
        s->read_valid = 0;
        if(phase)
            return file_error(response, response_bytes, host, address, ERR_PHASE);
        io_result = s->io.read_block(s->io.context, (uint16_t)block, response + 12);
        if(io_result == KUI_VMU_EMU_IO_BUSY)
            return reply(response, response_bytes, RESP_AGAIN, host, address, 0);
        if(io_result != KUI_VMU_EMU_IO_OK)
            return file_error(response, response_bytes, host, address, ERR_IO);
        memcpy(response + 4, request + 4, 8);
        s->read_block = (uint16_t)block;
        s->read_valid = 1;
        return reply(response, response_bytes, RESP_DATA, host, address, 130);
    }
    if(command == CMD_WRITE) {
        if(phase >= 4)
            return file_error(response, response_bytes, host, address, ERR_PHASE);
        /* Once the backing store has been called, preserve the exact block
         * until its durable result is known. A retransmitted phase cannot
         * replace bytes under a pending/retryable commit. */
        if(s->commit_attempted) {
            if(s->staged_block == block &&
               !memcmp(s->staged + phase * 128u, request + 12, 128))
                return reply(response, response_bytes, RESP_OK, host, address, 0);
            return reply(response, response_bytes, RESP_AGAIN, host, address, 0);
        }
        if(phase == 0) {
            /* Retrying an identical phase zero must not discard later
             * phases. A changed/new phase zero intentionally starts over. */
            if(!s->active || s->staged_block != block ||
               memcmp(s->staged, request + 12, 128)) {
                s->staged_block = (uint16_t)block;
                s->active = 1;
                s->phases = 1;
                s->committed_valid = 0;
                s->read_valid = 0;
                s->commit_attempted = 0;
                memcpy(s->staged, request + 12, 128);
            }
        }
        else if(!s->active || s->staged_block != block) {
            return file_error(response, response_bytes, host, address, ERR_PHASE);
        }
        else if(s->phases & (1u << phase)) {
            if(memcmp(s->staged + phase * 128u, request + 12, 128))
                return file_error(response, response_bytes, host, address, ERR_PHASE);
        }
        else {
            if(s->phases != (1u << phase) - 1u)
                return file_error(response, response_bytes, host, address, ERR_PHASE);
            memcpy(s->staged + phase * 128u, request + 12, 128);
            s->phases = (uint8_t)(s->phases | (1u << phase));
        }
        return reply(response, response_bytes, RESP_OK, host, address, 0);
    }

    /* A read has one phase: its optional get-last-error query is phase 1.
     * This is separate from the four-phase durable write commit. */
    if(phase == 1 && !s->active && s->read_valid && s->read_block == block)
        return reply(response, response_bytes, RESP_OK, host, address, 0);
    /* BSYNC is the Maple get-last-error command, phase 4, for this block.
     * Phase ACKs only promise receipt; ONLY this ACK promises durability. */
    if(phase != 4)
        return file_error(response, response_bytes, host, address, ERR_PHASE);
    if(!s->active) {
        if(s->committed_valid && s->committed_block == block)
            return reply(response, response_bytes, RESP_OK, host, address, 0);
        return file_error(response, response_bytes, host, address, ERR_PHASE);
    }
    if(s->staged_block != block || s->phases != 15)
        return file_error(response, response_bytes, host, address, ERR_PHASE);
    s->commit_attempted = 1;
    io_result = s->io.commit_block(s->io.context, (uint16_t)block, s->staged);
    if(io_result == KUI_VMU_EMU_IO_BUSY)
        return reply(response, response_bytes, RESP_AGAIN, host, address, 0);
    if(io_result != KUI_VMU_EMU_IO_OK)
        return file_error(response, response_bytes, host, address, ERR_IO);
    s->active = 0;
    s->phases = 0;
    s->committed_block = (uint16_t)block;
    s->committed_valid = 1;
    s->commit_attempted = 0;
    return reply(response, response_bytes, RESP_OK, host, address, 0);
}
