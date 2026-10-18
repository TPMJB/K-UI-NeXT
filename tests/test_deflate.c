/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/deflate.h"
#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <zlib.h>

/* zlib is a host-only independent compressor and acceptance oracle. Explicit
 * bit streams exercise invalid code trees and tokens a compressor cannot emit.
 * The production decoder has no zlib dependency. */
#define CAP 8192u
static struct {
    uint8_t data[CAP];
    uint32_t size, base, limit, calls, fail_on;
    size_t largest;
    enum kui_zso_result fault;
} source;
static struct {uint8_t before[19], data[2048], after[23];} output;
static struct {uint8_t before[13]; struct kui_deflate_workspace data; uint8_t after[17];} work;
static uint8_t expected[2048], oracle[2048], backup[CAP];
static uint32_t bit_count, random_state = 0x6af198c3u;
static uint32_t random_word(void) {
    random_state ^= random_state << 13; random_state ^= random_state >> 17;
    random_state ^= random_state << 5; return random_state;
}
static enum kui_zso_result read_source(void *context, uint32_t offset, void *out, size_t count) {
    (void)context;
    ++source.calls;
    assert(source.calls < 16384u);
    assert(offset >= source.base && offset <= source.limit && count <= source.limit - offset);
    assert(offset - source.base <= source.size && count <= source.size - (offset - source.base));
    if(count > source.largest) source.largest = count;
    if(source.calls == source.fail_on) {
        memcpy(out, source.data + offset - source.base, count / 2u); return source.fault;
    }
    memcpy(out, source.data + offset - source.base, count); return KUI_ZSO_OK;
}
static void canaries(void) {
    for(unsigned i = 0; i < sizeof(output.before); ++i) assert(output.before[i] == 0xa6);
    for(unsigned i = 0; i < sizeof(output.after); ++i) assert(output.after[i] == 0xa6);
    for(unsigned i = 0; i < sizeof(work.before); ++i) assert(work.before[i] == 0xa6);
    for(unsigned i = 0; i < sizeof(work.after); ++i) assert(work.after[i] == 0xa6);
}
static enum kui_zso_result decode(uint32_t bytes, uint32_t *consumed) {
    assert(bytes <= source.size && bytes <= UINT32_MAX - source.base);
    source.limit = source.base + bytes; source.calls = 0;
    memset(&output, 0xa6, sizeof(output)); memset(&work, 0xa6, sizeof(work));
    enum kui_zso_result result = kui_deflate_decode(read_source, &source, source.base,
        source.limit, output.data, &work.data, consumed);
    if(bytes && bytes <= KUI_ZSO_MAX_SPAN) {
        assert(work.data.input.read == NULL && work.data.input.context == NULL);
    }
    canaries(); return result;
}
static void begin(void) {
    memset(&source, 0, sizeof(source)); source.base = 37; bit_count = 0;
}
static void put(unsigned value, unsigned bits) {
    while(bits--) {
        assert(bit_count / 8u < CAP);
        source.data[bit_count / 8u] |= (uint8_t)((value & 1u) << (bit_count & 7u));
        ++bit_count; value >>= 1;
    }
    source.size = (bit_count + 7u) / 8u;
}
static void huffman(unsigned value, unsigned bits) {
    while(bits--) put((value >> bits) & 1u, 1u);
}
static void fixed_symbol(unsigned value) {
    if(value < 144u) huffman(value + 48u, 8u);
    else if(value < 256u) huffman(value - 144u + 400u, 9u);
    else if(value < 280u) huffman(value - 256u, 7u);
    else huffman(value - 280u + 192u, 8u);
}
static void fixed_header(bool final) {put(final ? 1u : 0u, 1u); put(1u, 2u);}
static void singleton_header(bool final) {
    /* Code-length tree: symbol18 has code0/1bit; symbols0,1 have
     * codes10,11/2bits. A literal tree with only EOB and no distance tree
     * is permitted when this DEFLATE block emits no matches. */
    static const unsigned order[19] = {16,17,18,0,8,7,9,6,10,5,11,4,12,3,13,2,14,1,15};
    put(final ? 1u : 0u, 1u); put(2, 2u); put(0, 5u); put(0, 5u); put(14, 4u);
    for(unsigned i = 0; i < 18u; ++i)
        put(order[i] == 18u ? 1u : order[i] <= 1u ? 2u : 0u, 3u);
    huffman(0, 1u); put(127, 7u); /* 138 zero widths. */
    huffman(0, 1u); put(107, 7u); /* 118 zero widths. */
    huffman(3, 2u); /* Literal EOB256 has width1. */
    huffman(2, 2u); /* Sole declared distance symbol has width0. */
    huffman(0, 1u); /* EOB. */
}
static void stored(unsigned bytes, bool final) {
    put(final ? 1u : 0u, 1u); put(0, 2u);
    while(bit_count & 7u) put(1u, 1u); /* Ignored alignment bits may be nonzero. */
    put(bytes, 16u); put(bytes ^ 65535u, 16u);
    for(unsigned i = 0; i < bytes; ++i) put((i * 37u) & 255u, 8u);
}
static void oracle_check(const uint8_t *bytes, unsigned size, const uint8_t *wanted) {
    z_stream stream; memset(&stream, 0, sizeof(stream));
    assert(inflateInit2(&stream, -15) == Z_OK);
    stream.next_in = (Bytef *)bytes; stream.avail_in = size;
    stream.next_out = oracle; stream.avail_out = sizeof(oracle);
    assert(inflate(&stream, Z_FINISH) == Z_STREAM_END);
    assert(stream.total_out == sizeof(oracle) && stream.total_in == size);
    assert(!memcmp(oracle, wanted, sizeof(oracle)));
    assert(inflateEnd(&stream) == Z_OK);
}
static void valid(bool faults) {
    oracle_check(source.data, source.size, expected);
    uint32_t consumed = 0xdeadbeefu;
    assert(decode(source.size, &consumed) == KUI_ZSO_OK && consumed == source.size);
    assert(!memcmp(output.data, expected, sizeof(expected)));
    uint32_t calls = source.calls;
    if(faults) for(unsigned error = 0; error < 2; ++error) {
        source.fault = error ? KUI_ZSO_CANCELLED : KUI_ZSO_IO;
        for(uint32_t call = 1; call <= calls; ++call) {
            source.fail_on = call; consumed = 0xdeadbeefu;
            assert(decode(source.size, &consumed) == source.fault);
            assert(consumed == 0xdeadbeefu && source.calls == call);
        }
    }
    source.fail_on = 0;
}
static void token_forms(void) {
    begin(); stored(2048, true);
    for(unsigned i = 0; i < 2048; ++i) expected[i] = (uint8_t)(i * 37u);
    valid(true);
    begin(); fixed_header(true);
    for(unsigned i = 0; i < 2048; ++i) {expected[i] = (uint8_t)i; fixed_symbol(expected[i]);}
    fixed_symbol(256); valid(true);
    /* Multiple DEFLATE blocks share this independent CSO sector's dictionary. */
    begin(); stored(3, false); fixed_header(false);
    fixed_symbol(285); huffman(2, 5u); /* length258, distance3. */
    fixed_symbol(256); fixed_header(true);
    for(unsigned i = 261; i < 2048; ++i) fixed_symbol(i & 255u);
    fixed_symbol(256);
    expected[0] = 0; expected[1] = 37; expected[2] = 74;
    for(unsigned i = 3; i < 261; ++i) expected[i] = expected[i - 3u];
    for(unsigned i = 261; i < 2048; ++i) expected[i] = (uint8_t)i;
    valid(true);
    /* A final empty stored block after an exactly full compressed block. */
    begin(); fixed_header(false);
    for(unsigned i = 0; i < 2048; ++i) {expected[i] = (uint8_t)i; fixed_symbol(expected[i]);}
    fixed_symbol(256); stored(0, true); valid(true);
    begin(); singleton_header(false); stored(2048, true);
    for(unsigned i = 0; i < 2048; ++i) expected[i] = (uint8_t)(i * 37u);
    valid(true);
    puts("PASS DEFLATE tokens: stored/fixed ranges, arbitrary alignment bits, cross-block matches and empty final blocks against zlib");
}
static void compress_sample(unsigned sample, int strategy, int level) {
    begin();
    for(unsigned i = 0; i < 2048; ++i) {
        unsigned value = random_word();
        switch(sample % 7u) {
        case 0: expected[i] = (uint8_t)value; break;
        case 1: expected[i] = (uint8_t)(value % 4u); break;
        case 2: expected[i] = (uint8_t)(i % (sample % 127u + 1u)); break;
        case 3: expected[i] = (uint8_t)((i / 31u) % 3u); break;
        case 4: expected[i] = i > 513u && (value & 7u) ? expected[i - 513u] : (uint8_t)value; break;
        case 5: expected[i] = i > 7u && (value & 3u) ? expected[i - 7u] : (uint8_t)value; break;
        default: expected[i] = 'A'; break;
        }
    }
    z_stream stream; memset(&stream, 0, sizeof(stream));
    assert(deflateInit2(&stream, level, Z_DEFLATED, -15, 8, strategy) == Z_OK);
    stream.next_in = expected; stream.avail_in = sizeof(expected);
    stream.next_out = source.data; stream.avail_out = CAP;
    assert(deflate(&stream, Z_FINISH) == Z_STREAM_END);
    source.size = (uint32_t)stream.total_out;
    assert(deflateEnd(&stream) == Z_OK);
}
static void compressed_oracles(void) {
    const int strategies[] = {Z_DEFAULT_STRATEGY, Z_FILTERED, Z_HUFFMAN_ONLY, Z_RLE, Z_FIXED};
    unsigned types = 0;
    for(unsigned sample = 0; sample < 1800; ++sample) {
        compress_sample(sample, strategies[sample % 5u], (int)(sample % 10u));
        types |= 1u << ((source.data[0] >> 1) & 3u);
        valid(sample < 10u);
    }
    assert((types & 7u) == 7u);
    puts("PASS DEFLATE oracle: 1800 zlib blocks, levels0--9, all5 strategies and stored/fixed/dynamic trees");
}
static void reject(void) {
    uint32_t consumed = 0xdeadbeefu;
    assert(decode(source.size, &consumed) != KUI_ZSO_OK && consumed == 0xdeadbeefu);
}
static void code_header(unsigned first, unsigned second, unsigned third, unsigned fourth) {
    put(1, 1u); put(2, 2u); put(0, 5u); put(0, 5u); put(0, 4u);
    put(first, 3u); put(second, 3u); put(third, 3u); put(fourth, 3u);
}
static void malformed(void) {
    begin(); stored(2048, true); unsigned full = source.size;
    for(unsigned n = 0; n < full; ++n) {
        uint32_t consumed = 0xdeadbeefu;
        assert(decode(n, &consumed) == KUI_ZSO_INVALID && consumed == 0xdeadbeefu);
    }
    source.data[source.size++] = 0xa7; source.data[source.size++] = 0x19;
    uint32_t consumed = 0;
    assert(decode(source.size, &consumed) == KUI_ZSO_OK && consumed == full);
    source.base = UINT32_MAX - source.size;
    assert(decode(source.size, &consumed) == KUI_ZSO_OK && consumed == full);
    source.base = 37;
    source.data[3] ^= 1u; reject(); /* Bad stored LEN/NLEN complement. */
    for(unsigned dynamic = 0; dynamic < 2u; ++dynamic) {
        compress_sample(6, dynamic ? Z_HUFFMAN_ONLY : Z_FIXED, 6);
        assert(((source.data[0] >> 1) & 3u) == (dynamic ? 2u : 1u));
        full = source.size;
        for(unsigned n = 0; n < full; ++n) {
            uint32_t used = 0xdeadbeefu;
            assert(decode(n, &used) == KUI_ZSO_INVALID && used == 0xdeadbeefu);
        }
    }
    begin(); stored(2049, true); reject();
    begin(); stored(2047, true); reject();
    begin(); put(7, 3u); reject(); /* Reserved block type. */
    begin(); fixed_header(true); fixed_symbol(256); reject();
    for(unsigned reserved = 286; reserved < 288; ++reserved) {
        begin(); fixed_header(true); fixed_symbol(reserved); reject();
    }
    for(unsigned reserved = 30; reserved < 32; ++reserved) {
        begin(); fixed_header(true); fixed_symbol('A'); fixed_symbol(257);
        huffman(reserved, 5u); fixed_symbol(256); reject();
    }
    begin(); fixed_header(true); fixed_symbol(257); huffman(0, 5u); fixed_symbol(256); reject();
    begin(); fixed_header(true); fixed_symbol('A'); fixed_symbol(257);
    huffman(1, 5u); fixed_symbol(256); reject(); /* Match before output. */
    begin(); fixed_header(true);
    for(unsigned i = 0; i < 2048; ++i) fixed_symbol('A');
    fixed_symbol('B'); fixed_symbol(256); reject();
    begin(); fixed_header(true);
    for(unsigned i = 0; i < 2047; ++i) fixed_symbol('A');
    fixed_symbol(257); huffman(0, 5u); fixed_symbol(256); reject();
    begin(); code_header(0, 0, 0, 0); reject(); /* Empty code-length tree. */
    begin(); code_header(1, 1, 1, 1); reject(); /* Oversubscribed code tree. */
    begin(); code_header(2, 0, 0, 0); reject(); /* Incomplete code tree. */
    begin(); code_header(1, 0, 0, 1); huffman(1, 1u); put(0, 2u); reject(); /* Repeat16 before any width. */
    begin(); code_header(0, 0, 1, 1);
    huffman(1, 1u); put(127, 7u); huffman(1, 1u); put(127, 7u); reject(); /* Width-run overflow. */
    begin(); put(1, 1u); put(2, 2u); put(31, 5u); put(0, 5u); put(0, 4u); reject(); /* HLIT exceeds286. */
    begin(); memset(source.data, 0, 4097); source.size = 4097; reject();
    source.calls = 0;
    assert(kui_deflate_decode(NULL, NULL, 0, 1, output.data, &work.data, &consumed) == KUI_ZSO_INVALID);
    assert(kui_deflate_decode(read_source, NULL, 1, 0, output.data, &work.data, &consumed) == KUI_ZSO_INVALID);
    assert(kui_deflate_decode(read_source, NULL, 1, 1, output.data, &work.data, &consumed) == KUI_ZSO_INVALID);
    assert(kui_deflate_decode(read_source, NULL, 0, 1, NULL, &work.data, &consumed) == KUI_ZSO_INVALID);
    assert(kui_deflate_decode(read_source, NULL, 0, 1, output.data, NULL, &consumed) == KUI_ZSO_INVALID);
    assert(kui_deflate_decode(read_source, NULL, 0, 1, output.data, &work.data, NULL) == KUI_ZSO_INVALID);
    assert(!source.calls);
    puts("PASS DEFLATE bounds: every stored truncation, padding/UINT32 boundary, reserved symbols, invalid trees, runs, lookbehind and output overflow");
}
static void mutations(void) {
    compress_sample(4, Z_HUFFMAN_ONLY, 6);
    assert(((source.data[0] >> 1) & 3u) == 2u);
    unsigned bytes = source.size; memcpy(backup, source.data, bytes);
    for(unsigned sample = 0; sample < 30000; ++sample) {
        source.size = bytes; memcpy(source.data, backup, bytes);
        unsigned flips = random_word() % 4u + 1u;
        while(flips--) source.data[random_word() % bytes] = (uint8_t)random_word();
        if(sample & 1u) source.size = random_word() % (bytes + 1u);
        uint32_t consumed = 0xdeadbeefu;
        enum kui_zso_result result = decode(source.size, &consumed);
        if(result == KUI_ZSO_OK) {
            assert(consumed <= source.size); oracle_check(source.data, consumed, output.data);
        } else assert(consumed == 0xdeadbeefu);
    }
    puts("PASS DEFLATE hostile streams: 30000 deterministic mutations/truncations, output/workspace canaries and zlib acceptance oracle");
}
int main(void) {
    token_forms(); compressed_oracles(); malformed(); mutations();
    puts("All DEFLATE tests passed"); return 0;
}
