#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Sanitize the actual gated CE WRITE capture and final-source comparison."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

import test_armada_ce_cache_snapshot_wrapper as production_extractor

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'src/loader/armada_ce_maple_traffic.c'

PREFIX = r'''
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#define KUI_ARMADA_CE_MAPLE_WRITE_STAGING 1
#define KUI_ARMADA_CE_MAPLE_TRAFFIC_RECORD_LIMIT 16u
#include "kui/armada_ce_vmu_a2_publish.h"
static unsigned checks,reads,fail_at;
#define CHECK(test) do { ++checks; if(!(test)) { \
    fprintf(stderr,"capture check failed at line %u: %s\n",__LINE__,#test); abort(); \
} } while(0)
static struct { uint32_t word[10],row[16][5]; } traffic_scratch;
static uint8_t tx[4096];
static bool traffic_read_p2(void *context,uint32_t address,uint32_t *value) {
    CHECK(context==NULL);
    CHECK(!(address&3u) && address>=0xac300000u && address<=0xac300ffcu);
    if(++reads==fail_at) return false;
    unsigned at=address-0xac300000u;
    *value=(uint32_t)tx[at]|(uint32_t)tx[at+1u]<<8|
        (uint32_t)tx[at+2u]<<16|(uint32_t)tx[at+3u]<<24;
    return true;
}
'''

HARNESS = r'''
enum { INDEX=3, WRITE_WORDS=34, WRITE_BYTES=140 };
struct guarded { uint8_t before[16],request[WRITE_BYTES],after[16]; };
static void put32(uint8_t *out,uint32_t word) {
    for(unsigned j=0;j<4u;j++) out[j]=(uint8_t)(word>>(8u*j));
}
static void fixture(unsigned offset,unsigned declared) {
    memset(&traffic_scratch,0,sizeof(traffic_scratch));
    for(unsigned i=0;i<sizeof(tx);i++) tx[i]=(uint8_t)(13u+i*7u+(i>>5));
    traffic_scratch.word[1]=0x0c300000u;
    traffic_scratch.row[INDEX][0]=offset;
    traffic_scratch.row[INDEX][3]=(declared<<24)|0x0000020cu;
    reads=fail_at=0;
}
static void guard_init(struct guarded *out) {
    memset(out,0xa5,sizeof(*out));
    put32(out->request,traffic_scratch.row[INDEX][3]);
}
static void guard_check(const struct guarded *out) {
    for(unsigned i=0;i<16u;i++) {
        CHECK(out->before[i]==0xa5u);CHECK(out->after[i]==0xa5u);
    }
    uint8_t header[4];put32(header,traffic_scratch.row[INDEX][3]);
    CHECK(!memcmp(header,out->request,4u));
}
static void refused(unsigned index,unsigned count) {
    struct guarded out;guard_init(&out);
    uint8_t original[WRITE_BYTES];memcpy(original,out.request,sizeof(original));
    reads=fail_at=0;CHECK(!a2_payload(index,count,out.request+4u));CHECK(!reads);
    CHECK(!memcmp(original,out.request,sizeof(original)));guard_check(&out);
    CHECK(!a2_payload_matches(index,count,out.request+4u));CHECK(!reads);
}
static struct kui_armada_ce_vmu_a2_input in;
static struct kui_armada_ce_vmu_a2_publish_scratch scratch;
static struct kui_armada_ce_vmu_a2_publish_snapshot publication;
static uint32_t guest_rx[0x2000u/4u],guest_before[0x2000u/4u];
static unsigned source_mutation,guest_stores,backend_calls,admit_calls;
static int private_ack(void *context,const uint8_t *request,size_t bytes,
    uint8_t *reply,size_t capacity,size_t *produced) {
    CHECK(context==&in && bytes==140u && capacity==384u);
    CHECK(admit_calls==1u && !backend_calls && !guest_stores);
    CHECK(request==(const uint8_t *)(const void *)scratch.reply+384u);
    CHECK(!memcmp(request,in.target.request,140u));
    CHECK((uintptr_t)reply+capacity<=(uintptr_t)request);
    ++backend_calls;
    reply[0]=7u;reply[1]=0u;reply[2]=2u;reply[3]=0u;*produced=4u;
    if(source_mutation<WRITE_WORDS) tx[64u+12u+4u*source_mutation]^=0x80u;
    return 1;
}
static bool final_source(void *context) {
    CHECK(context==&in && !guest_stores);++admit_calls;
    CHECK(admit_calls<=2u && backend_calls==admit_calls-1u);
    return a2_payload_matches(0u,WRITE_WORDS,in.target.request+4u);
}
static bool guest_store(void *context,uint32_t address,uint32_t value) {
    CHECK(context==&in && admit_calls==2u && backend_calls==1u);
    CHECK(guest_stores<2u);
    CHECK(address==(guest_stores?in.parent.rx_bus:in.target.rx_bus)+0xa0000000u);
    CHECK(value==(guest_stores?(in.parent.response_header|0x00020000u):0x00020007u));
    guest_rx[((address&0x1fffffffu)-in.pool_bus)/4u]=value;++guest_stores;
    return true;
}
static void publication_fixture(unsigned mutation) {
    fixture(64u,WRITE_WORDS);memcpy(traffic_scratch.row[0],traffic_scratch.row[INDEX],sizeof(traffic_scratch.row[0]));
    put32(tx+76u,0x02000000u);put32(tx+80u,42u<<24);
    memset(&in,0,sizeof(in));memset(&scratch,0,sizeof(scratch));memset(&publication,0,sizeof(publication));
    memset(guest_rx,0xa5,sizeof(guest_rx));source_mutation=mutation;
    guest_stores=backend_calls=admit_calls=0u;
    in.pool_bus=0x0c400000u;in.pool_bytes=0x2000u;
    in.tx_bus=traffic_scratch.word[1];in.tx_bytes=0x1000u;
    in.protected_bus=0x0cfca000u;in.protected_bytes=0x6000u;
    in.parent.request_header=0x01002009u;in.parent.response_header=0x03200008u;
    in.parent.request_function=in.parent.response_function=0x01000000u;
    in.parent.rx_bus=in.pool_bus;in.parent.capacity=16u;
    in.target_present=1u;in.target.request_bytes=140u;
    in.target.response_header=UINT32_MAX;in.target.rx_bus=in.pool_bus+0x400u;in.target.capacity=4u;
    put32(in.target.request,traffic_scratch.row[0][3]);
    CHECK(a2_payload(0u,WRITE_WORDS,in.target.request+4u));CHECK(reads==WRITE_WORDS);
    guest_rx[0]=in.parent.response_header;guest_rx[0x400u/4u]=UINT32_MAX;
    memcpy(guest_before,guest_rx,sizeof(guest_rx));reads=0;
    unsigned result=kui_armada_ce_vmu_a2_publish(&in,private_ack,&in,final_source,&in,
        guest_store,&in,&scratch,&publication);
    CHECK(backend_calls==1u && admit_calls==2u);
    if(mutation<WRITE_WORDS) {
        CHECK(result==KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
        CHECK(publication.fault[0]==KUI_ARMADA_CE_VMU_A2_PUBLISH_ADMISSION);
        CHECK(!guest_stores && !publication.status[4]);
        CHECK(!memcmp(guest_rx,guest_before,sizeof(guest_rx)));
    } else {
        CHECK(result==KUI_ARMADA_CE_VMU_A2_PUBLISH_OK && guest_stores==2u);
        CHECK(publication.status[4]==2u && publication.counts[4]==1u && publication.counts[3]==1u);
    }
}
int main(void) {
    struct guarded saved,out;
    fixture(64u,WRITE_WORDS);guard_init(&saved);
    CHECK(a2_payload(INDEX,WRITE_WORDS,saved.request+4u));CHECK(reads==WRITE_WORDS);
    CHECK(!memcmp(saved.request+4u,tx+64u+12u,WRITE_BYTES-4u));guard_check(&saved);
    uint8_t immutable[WRITE_BYTES];memcpy(immutable,saved.request,sizeof(immutable));
    reads=0;CHECK(a2_payload_matches(INDEX,WRITE_WORDS,saved.request+4u));
    CHECK(reads==WRITE_WORDS);CHECK(!memcmp(immutable,saved.request,sizeof(immutable)));
    for(unsigned i=0;i<WRITE_WORDS;i++) {
        unsigned at=64u+12u+4u*i;tx[at]^=0x80u;reads=0;
        CHECK(!a2_payload_matches(INDEX,WRITE_WORDS,saved.request+4u));
        CHECK(reads==i+1u);tx[at]^=0x80u;
        CHECK(!memcmp(immutable,saved.request,sizeof(immutable)));guard_check(&saved);
    }
    for(unsigned fault=1;fault<=WRITE_WORDS;fault++) {
        guard_init(&out);reads=0;fail_at=fault;
        CHECK(!a2_payload(INDEX,WRITE_WORDS,out.request+4u));CHECK(reads==fault);
        CHECK(!memcmp(out.request+4u,tx+64u+12u,4u*(fault-1u)));
        for(unsigned j=4u+4u*(fault-1u);j<WRITE_BYTES;j++) CHECK(out.request[j]==0xa5u);
        guard_check(&out);reads=0;
        CHECK(!a2_payload_matches(INDEX,WRITE_WORDS,saved.request+4u));CHECK(reads==fault);
        CHECK(!memcmp(immutable,saved.request,sizeof(immutable)));guard_check(&saved);
    }
    fixture(64u,WRITE_WORDS);
    refused(16u,WRITE_WORDS);refused(UINT_MAX,WRITE_WORDS);
    refused(INDEX,35u);refused(INDEX,UINT_MAX);
    fixture(64u,33u);refused(INDEX,WRITE_WORDS);
    fixture(64u,0u);refused(INDEX,1u);
    fixture(3952u,WRITE_WORDS);refused(INDEX,WRITE_WORDS);
    fixture(4084u,WRITE_WORDS);refused(INDEX,1u);
    fixture(4085u,WRITE_WORDS);refused(INDEX,0u);
    fixture(UINT_MAX,WRITE_WORDS);refused(INDEX,0u);
    /* Exact 4-KiB endpoint is legal; one additional word is not. */
    fixture(3948u,WRITE_WORDS);guard_init(&out);
    CHECK(a2_payload(INDEX,WRITE_WORDS,out.request+4u));CHECK(reads==WRITE_WORDS);
    CHECK(!memcmp(out.request+4u,tx+3960u,WRITE_BYTES-4u));guard_check(&out);
    reads=0;CHECK(a2_payload_matches(INDEX,WRITE_WORDS,out.request+4u));CHECK(reads==WRITE_WORDS);
    fixture(4080u,1u);guard_init(&out);
    CHECK(a2_payload(INDEX,1u,out.request+4u));CHECK(reads==1u);guard_check(&out);
    CHECK(!memcmp(out.request+4u,tx+4092u,4u));reads=0;
    CHECK(a2_payload_matches(INDEX,1u,out.request+4u));CHECK(reads==1u);
    fixture(4084u,0u);guard_init(&out);
    CHECK(a2_payload(INDEX,0u,out.request+4u));CHECK(!reads);guard_check(&out);
    CHECK(a2_payload_matches(INDEX,0u,out.request+4u));CHECK(!reads);
    for(unsigned i=0;i<WRITE_WORDS;i++) publication_fixture(i);
    publication_fixture(UINT_MAX);
    printf("{\"checks\":%u,\"write_request_bytes\":140,\"payload_words\":34,"
        "\"source_word_mutations_rejected\":34,\"capture_read_failures_rejected\":34,"
        "\"comparison_read_failures_rejected\":34,\"invalid_geometry_cases_rejected\":10,"
        "\"source_word_mutations_zero_guest_stores\":34,\"unchanged_control_guest_stores\":2}\n",checks);
    return 0;
}
'''


def extract(source, name):
    """Reuse the balanced-brace parser; change only its definition pattern."""
    pattern = production_extractor.PATTERN
    try:
        production_extractor.PATTERN = rf'(?m)^static\s+bool\s+{name}\s*\('
        return production_extractor.extract(source)
    finally:
        production_extractor.PATTERN = pattern


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--json', type=Path)
    parser.add_argument('--cc', default=os.environ.get('CC', 'cc'))
    args = parser.parse_args()
    source = SOURCE.read_text()
    definitions = {name: extract(source, name) for name in ('a2_payload', 'a2_payload_matches')}
    compiler = shlex.split(args.cc)
    flags = ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror',
             '-fsanitize=address,undefined', '-fno-omit-frame-pointer',
             '-DKUI_ARMADA_CE_MAPLE_WRITE_STAGING=1', '-I', str(ROOT / 'include')]
    with tempfile.TemporaryDirectory(prefix='armada-ce-write-capture-') as temporary:
        fixture = Path(temporary) / 'capture.c'
        binary = Path(temporary) / 'capture'
        fixture.write_text(PREFIX + '\n'.join(definitions.values()) + HARNESS)
        subprocess.run(compiler + flags + [str(fixture),
                       str(ROOT / 'src/loader/armada_ce_vmu_a2_publish.c'),
                       '-o', str(binary)], check=True)
        environment = dict(os.environ, ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',
                           UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
        result = subprocess.run([str(binary)], check=True, text=True, capture_output=True,
                                env=environment)
        report = json.loads(result.stdout)
    report.update(status='PASS', sanitizers=['address', 'undefined'],
                  production_definition_sha256={name: hashlib.sha256(body.encode()).hexdigest()
                                                for name, body in definitions.items()},
                  actual_production_definitions_extracted=True,
                  actual_production_publisher_linked=True,
                  canaries_checked=True, host_only=True,
                  scope='Gated WRITE capture/final comparison; no backend commit or native timing claim')
    if args.json:
        args.json.write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
