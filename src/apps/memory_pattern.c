/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/memory_test.h"
#include <stdint.h>
#include <string.h>
#define CHUNK_WORDS 16384u
#define BLOCK_WORDS 1024u

static bool cancelled(const struct kui_memory_test_ops *ops,
    struct kui_memory_test_result *r) {
    if(ops && ops->cancelled && ops->cancelled(ops->ctx)) r->stopped=true;
    return r->stopped;
}
static void publish(const struct kui_memory_test_ops *ops,
    const struct kui_memory_test_result *r) {
    if(ops && ops->progress) ops->progress(ops->ctx,r);
    if(ops && ops->yield) ops->yield(ops->ctx);
}
static uint32_t pattern(unsigned pass,size_t word) {
    static const uint32_t fixed[]={0,UINT32_MAX,UINT32_C(0xaaaaaaaa),UINT32_C(0x55555555)};
    if(pass<4) return fixed[pass];
    uint32_t address=(uint32_t)word^UINT32_C(0xa5a55a5a);
    return pass==4?address:~address;
}
static bool check(struct kui_memory_test_result *r,size_t word,uint32_t expected,uint32_t actual) {
    r->work_done+=sizeof(uint32_t);
    if(actual==expected) return true;
    ++r->errors;r->first_failure_offset=word*sizeof(uint32_t);
    r->expected=expected;r->actual=actual;return false;
}
bool kui_memory_test(volatile uint32_t *buffer,size_t bytes,
    const struct kui_memory_test_ops *ops,struct kui_memory_test_result *r) {
    if(!r) return false;
    memset(r,0,sizeof(*r));r->first_failure_offset=SIZE_MAX;
    if(!buffer || !bytes || bytes>KUI_MEMORY_TEST_MAX || bytes%sizeof(uint32_t) ||
       (uintptr_t)buffer%_Alignof(uint32_t)) {r->invalid=true;return false;}
    r->buffer_bytes=bytes;r->total_passes=70;
    size_t words=bytes/sizeof(uint32_t),samples=0;
    for(size_t base=0;base<words;base+=BLOCK_WORDS) samples+=words-base>1?2:1;
    r->work_total=(uint64_t)bytes*12u+(uint64_t)samples*64u*8u;
    if(cancelled(ops,r)) goto done;
    publish(ops,r);
    for(unsigned pass=0;pass<6;++pass) {
        for(size_t base=0;base<words;base+=CHUNK_WORDS) {
            if(cancelled(ops,r)) goto done;
            size_t end=words-base>CHUNK_WORDS?base+CHUNK_WORDS:words;
            for(size_t i=base;i<end;++i) buffer[i]=pattern(pass,i);
            r->work_done+=(uint64_t)(end-base)*sizeof(uint32_t);publish(ops,r);
        }
        for(size_t base=0;base<words;base+=CHUNK_WORDS) {
            if(cancelled(ops,r)) goto done;
            size_t end=words-base>CHUNK_WORDS?base+CHUNK_WORDS:words;
            for(size_t i=base;i<end;++i) {
                if(!check(r,i,pattern(pass,i),buffer[i])) goto done;
                if(r->tested_bytes<(i+1)*sizeof(uint32_t)) r->tested_bytes=(i+1)*sizeof(uint32_t);
            }
            publish(ops,r);
        }
        ++r->passes;publish(ops,r);
    }
    for(unsigned pass=0;pass<64;++pass) {
        uint32_t value=UINT32_C(1)<<(pass%32);
        if(pass>=32) value=~value;
        for(size_t base=0;base<words;base+=BLOCK_WORDS) {
            if(cancelled(ops,r)) goto done;
            size_t last=words-base>BLOCK_WORDS?base+BLOCK_WORDS-1:words-1;
            buffer[base]=value;r->work_done+=sizeof(uint32_t);
            if(!check(r,base,value,buffer[base])) goto done;
            if(last!=base) {
                buffer[last]=value;r->work_done+=sizeof(uint32_t);
                if(!check(r,last,value,buffer[last])) goto done;
            }
        }
        ++r->passes;publish(ops,r);
    }
    r->complete=true;
done:
    publish(ops,r);
    return r->complete && !r->errors && !r->stopped;
}
