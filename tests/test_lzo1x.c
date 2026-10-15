/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/lzo1x.h"
#include "minilzo.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

/* The bundled public miniLZO compressor and safe decoder are independent
 * oracles. Handwritten token streams cover forms its compressor may not
 * choose for a small block, including attached literals and short matches.
 * All large work areas are static, including the oracle's compression heap. */
#define CAP 8192u
static struct {
    uint8_t data[CAP];
    uint32_t size, base, limit, calls, fail_on;
    size_t largest;
    enum kui_zso_result fault;
} source;
static uint8_t expected[2048], oracle[2048], backup[CAP];
static struct {uint8_t before[19], data[2048], after[23];} output;
static lzo_align_t work[(LZO1X_1_MEM_COMPRESS + sizeof(lzo_align_t) - 1) / sizeof(lzo_align_t)];
static uint32_t produced, random_state = 0x2d87a539u;
static uint32_t random_word(void) {
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}
static enum kui_zso_result read_source(void *context, uint32_t offset, void *out, size_t count) {
    (void)context;
    ++source.calls;
    assert(source.calls < 16384u);
    assert(offset >= source.base && offset <= source.limit && count <= source.limit - offset);
    assert(offset - source.base <= source.size && count <= source.size - (offset - source.base));
    if(count > source.largest) source.largest = count;
    if(source.fail_on == source.calls) {
        memcpy(out, source.data + offset - source.base, count / 2u);
        return source.fault;
    }
    memcpy(out, source.data + offset - source.base, count);
    return KUI_ZSO_OK;
}
static void canaries(void) {
    for(unsigned i = 0; i < sizeof(output.before); ++i) assert(output.before[i] == 0xa6);
    for(unsigned i = 0; i < sizeof(output.after); ++i) assert(output.after[i] == 0xa6);
}
static enum kui_zso_result decode(uint32_t bytes, uint32_t *consumed) {
    assert(bytes <= source.size && bytes <= UINT32_MAX - source.base);
    source.limit = source.base + bytes;
    source.calls = 0;
    memset(&output, 0xa6, sizeof(output));
    enum kui_zso_result result = kui_lzo1x_decode(read_source, NULL, source.base,
                                                source.limit, output.data, consumed);
    canaries();
    return result;
}
static void begin(void) {
    memset(&source, 0, sizeof(source));
    source.base = 37;
    produced = 0;
}
static void byte(uint32_t value) {assert(source.size < CAP); source.data[source.size++] = (uint8_t)value;}
static void extension(uint32_t value) {
    assert(value);
    while(value > 255u) {byte(0); value -= 255u;}
    byte(value);
}
static void literal_bytes(uint32_t count) {
    assert(count <= 2048u - produced);
    for(uint32_t i = 0; i < count; ++i) {
        uint8_t value = (uint8_t)(produced * 37u + (produced >> 4) * 11u);
        byte(value); expected[produced++] = value;
    }
}
static void literals(uint32_t count, bool initial) {
    if(initial && count <= 238u) {assert(count); byte(17u + count);}
    else if(count <= 18u) {assert(count >= 4u); byte(count - 3u);}
    else {byte(0); extension(count - 18u);}
    literal_bytes(count);
}
static void match_bytes(uint32_t distance, uint32_t count) {
    assert(distance && distance <= produced && count <= 2048u - produced);
    while(count--) {expected[produced] = expected[produced - distance]; ++produced;}
}
static void m1(uint32_t distance, uint32_t trailing) {
    assert(distance && distance <= 1024u && trailing <= 3u);
    byte(((distance - 1u) & 3u) << 2 | trailing);
    byte((distance - 1u) >> 2);
    match_bytes(distance, 2);
}
static void m2(uint32_t distance, uint32_t count, uint32_t trailing) {
    assert(distance && distance <= 2048u && count >= 3u && count <= 8u && trailing <= 3u);
    byte((count - 1u) << 5 | ((distance - 1u) & 7u) << 2 | trailing);
    byte((distance - 1u) >> 3);
    match_bytes(distance, count);
}
static void m3(uint32_t distance, uint32_t count, uint32_t trailing) {
    assert(distance && distance <= 16384u && count >= 3u && trailing <= 3u);
    if(count <= 33u) byte(32u | (count - 2u));
    else {byte(32); extension(count - 33u);}
    uint32_t encoded = (distance - 1u) << 2 | trailing;
    byte(encoded); byte(encoded >> 8);
    match_bytes(distance, count);
}
static void end(void) {byte(17); byte(0); byte(0);}
static void valid(bool faults) {
    assert(produced == 2048u);
    lzo_uint oracle_size = sizeof(oracle);
    assert(lzo1x_decompress_safe(source.data, source.size, oracle, &oracle_size, NULL) == LZO_E_OK);
    assert(oracle_size == 2048u && !memcmp(oracle, expected, 2048));
    uint32_t consumed = 0xdeadbeefu;
    assert(decode(source.size, &consumed) == KUI_ZSO_OK && consumed == source.size);
    assert(!memcmp(output.data, expected, 2048));
    uint32_t calls = source.calls;
    if(faults) for(unsigned error = 0; error < 2; ++error) {
        source.fault = error ? KUI_ZSO_CANCELLED : KUI_ZSO_IO;
        for(uint32_t at = 1; at <= calls; ++at) {
            source.fail_on = at; consumed = 0xdeadbeefu;
            assert(decode(source.size, &consumed) == source.fault);
            assert(consumed == 0xdeadbeefu && source.calls == at);
        }
    }
    source.fail_on = 0;
}
static void token_forms(void) {
    begin(); literals(2048, false); end(); valid(true);
    const uint32_t distances[] = {1, 2, 3, 7, 8, 9, 63, 64, 255, 256, 1024, 2040, 2045};
    for(uint32_t count = 3; count <= 8; ++count) {
        for(unsigned d = 0; d < sizeof(distances) / sizeof(distances[0]); ++d) {
            if(distances[d] > 2048u - count) continue;
            begin(); literals(2048u - count, false); m2(distances[d], count, 0); end(); valid(false);
        }
    }
    for(uint32_t count = 3; count <= 34; ++count) {
        begin(); literals(2048u - count, false); m3(2048u - count, count, 0); end(); valid(false);
    }
    const uint32_t lengths[] = {35, 287, 288, 289, 543, 544, 2047};
    for(unsigned n = 0; n < sizeof(lengths) / sizeof(lengths[0]); ++n) {
        begin(); literals(2048u - lengths[n], true); m3(1, lengths[n], 0); end(); valid(true);
    }
    for(uint32_t count = 1; count <= 3; ++count) {
        begin(); literals(count, true); m1(1, 0); m3(1, 2048u - produced, 0); end(); valid(true);
        begin(); literals(8, true); m2(8, 8, count); literal_bytes(count);
        m1(1, 0); m3(1, 2048u - produced, 0); end(); valid(true);
        begin(); literals(8, true); m3(8, 8, count); literal_bytes(count);
        m1(1, 0); m3(1, 2048u - produced, 0); end(); valid(true);
        begin(); literals(8, true); m2(8, 8, 1); literal_bytes(1);
        m1(1, count); literal_bytes(count); m1(1, 0);
        m3(1, 2048u - produced, 0); end(); valid(true);
    }
    for(unsigned d = 0; d < sizeof(distances) / sizeof(distances[0]); ++d) {
        if(distances[d] > 1024u) continue;
        begin(); literals(2042, false); m2(1, 3, 1); literal_bytes(1);
        m1(distances[d], 0); end(); valid(false);
    }
    for(uint32_t count = 4; count <= 238; ++count) {
        begin(); literals(1, true); m3(1, 2047u - count, 0);
        literals(count, false); end(); valid(false);
    }
    puts("PASS LZO1X tokens: literal states, M1/M2/M3 lengths/distances, attached literals and overlap against miniLZO");
}
static void compressed_oracles(void) {
    for(unsigned sample = 0; sample < 1500; ++sample) {
        begin();
        for(unsigned i = 0; i < 2048; ++i) {
            uint32_t value = random_word();
            switch(sample % 6u) {
            case 0: expected[i] = (uint8_t)value; break;
            case 1: expected[i] = (uint8_t)(value % 4u); break;
            case 2: expected[i] = (uint8_t)(i % (sample % 127u + 1u)); break;
            case 3: expected[i] = (uint8_t)((i / 31u) % 3u); break;
            case 4: expected[i] = i > 513u && (value & 7u) ? expected[i - 513u] : (uint8_t)value; break;
            default: expected[i] = i > 7u && (value & 3u) ? expected[i - 7u] : (uint8_t)value; break;
            }
        }
        lzo_uint bytes = sizeof(source.data);
        assert(lzo1x_1_compress(expected, 2048, source.data, &bytes, work) == LZO_E_OK);
        assert(bytes <= CAP); source.size = (uint32_t)bytes; produced = 2048;
        valid(sample < 12u);
    }
    puts("PASS LZO1X oracle: 1500 miniLZO-compressed random, low-entropy, periodic and mixed blocks");
}
static void reject(void) {
    uint32_t consumed = 0xdeadbeefu;
    assert(decode(source.size, &consumed) != KUI_ZSO_OK);
    assert(consumed == 0xdeadbeefu);
}
static void malformed(void) {
    begin(); literals(2048, false); end();
    uint32_t full = source.size;
    for(uint32_t n = 0; n < full; ++n) {
        uint32_t consumed = 0xdeadbeefu;
        assert(decode(n, &consumed) == KUI_ZSO_INVALID && consumed == 0xdeadbeefu);
    }
    /* Success stops at the marker. The container, not this decoder, owns padding. */
    byte(0xa7); byte(0x19);
    uint32_t consumed = 0;
    assert(decode(source.size, &consumed) == KUI_ZSO_OK && consumed == full);
    source.base = UINT32_MAX - source.size;
    assert(decode(source.size, &consumed) == KUI_ZSO_OK && consumed == full);
    source.base = 37;
    for(unsigned token = 18; token < 24; ++token) {
        source.data[full - 3u] = (uint8_t)token;
        reject(); /* miniLZO accepts these zero-distance M4 tokens as EOF; this API requires canonical EOF. */
    }
    source.data[full - 3u] = 17; source.data[full - 2u] = 1; reject();
    begin(); literals(2048, false); byte(16); byte(1); byte(0); byte(0); reject();
    begin(); end(); reject(); /* Empty output. */
    begin(); literals(1, true); end(); reject(); /* Short output. */
    begin(); byte(0); for(unsigned i = 0; i < 100; ++i) byte(0); reject();
    begin(); literals(2048, false); byte(64); byte(0); end(); reject(); /* Output overflow. */
    begin(); literals(1, true); byte(33); byte(4); byte(0); end(); reject(); /* M3 before output. */
    begin(); literals(1, true); byte(0); byte(1); end(); reject(); /* M1 before output. */
    begin(); literals(1, true); byte(68); byte(0); end(); reject(); /* M2 before output. */
    begin(); literals(4, true); byte(0); byte(0); end(); reject(); /* Special post-literal M1 distance2049. */
    for(unsigned token = 16; token < 32; ++token) {
        begin(); literals(4, true); byte(token);
        if(!(token & 7u)) byte(1);
        byte(4); byte(0); end(); reject(); /* M4 16KiB/32KiB lookbehind cannot fit a 2KiB block. */
    }
    for(unsigned kind = 0; kind < 2; ++kind) {
        begin(); literals(1, true); byte(kind ? 16 : 32);
        for(unsigned i = 0; i < 100; ++i) byte(0);
        reject(); /* Bounded M3/M4 length extension. */
    }
    source.calls = 0;
    assert(kui_lzo1x_decode(NULL, NULL, 0, 1, output.data, &consumed) == KUI_ZSO_INVALID);
    assert(kui_lzo1x_decode(read_source, NULL, 1, 0, output.data, &consumed) == KUI_ZSO_INVALID);
    assert(kui_lzo1x_decode(read_source, NULL, 1, 1, output.data, &consumed) == KUI_ZSO_INVALID);
    assert(kui_lzo1x_decode(read_source, NULL, 0, 1, NULL, &consumed) == KUI_ZSO_INVALID);
    assert(kui_lzo1x_decode(read_source, NULL, 0, 1, output.data, NULL) == KUI_ZSO_INVALID);
    assert(!source.calls);
    puts("PASS LZO1X bounds: every truncation, marker/size rules, all match lookbehinds, overflow, padding and UINT32 boundary");
}
static void mutations(void) {
    begin(); literals(8, true); m3(8, 2040, 0); end();
    uint32_t bytes = source.size;
    memcpy(backup, source.data, bytes);
    for(unsigned sample = 0; sample < 30000; ++sample) {
        source.size = bytes;
        memcpy(source.data, backup, bytes);
        unsigned flips = random_word() % 4u + 1u;
        while(flips--) source.data[random_word() % bytes] = (uint8_t)random_word();
        if(sample & 1u) source.size = random_word() % (bytes + 1u);
        uint32_t consumed = 0xdeadbeefu;
        enum kui_zso_result result = decode(source.size, &consumed);
        if(result == KUI_ZSO_OK) {
            lzo_uint oracle_size = sizeof(oracle);
            int status = lzo1x_decompress_safe(source.data, consumed, oracle, &oracle_size, NULL);
            assert(status == LZO_E_OK && oracle_size == 2048 && !memcmp(oracle, output.data, 2048));
            assert(consumed <= source.size);
        } else assert(consumed == 0xdeadbeefu);
    }
    puts("PASS LZO1X hostile streams: 30000 deterministic mutations/truncations, canaries and miniLZO acceptance oracle");
}
int main(void) {
    assert(lzo_init() == LZO_E_OK);
    token_forms(); compressed_oracles(); malformed(); mutations();
    puts("All LZO1X tests passed");
    return 0;
}
