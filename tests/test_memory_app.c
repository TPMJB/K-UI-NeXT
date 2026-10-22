/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/memory_test.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct fixture {
    volatile uint32_t *memory;
    size_t bytes;
    bool stop,corrupt,cancel_after_write;
    unsigned updates,yields;
    uint64_t last_done;
};
static bool stopped(void *ctx) {return ((struct fixture *)ctx)->stop;}
static void yield(void *ctx) {++((struct fixture *)ctx)->yields;}
static void progress(void *ctx,const struct kui_memory_test_result *r) {
    struct fixture *f=ctx;++f->updates;
    assert(r->work_done>=f->last_done && r->work_done<=r->work_total);
    f->last_done=r->work_done;
    assert(r->buffer_bytes==f->bytes && r->tested_bytes<=f->bytes);
    assert(r->passes<=r->total_passes);
    /* Between the first fill and read: emulate a stored bit changing. */
    if(!r->passes && r->work_done==f->bytes) {
        if(f->corrupt) {f->memory[7]^=UINT32_C(0x80000000);f->corrupt=false;}
        if(f->cancel_after_write) f->stop=true;
    }
}
static void exercise(size_t bytes) {
    size_t words=bytes/sizeof(uint32_t);
    uint32_t *owned=malloc(bytes+2*sizeof(uint32_t));assert(owned);
    owned[0]=UINT32_C(0x12345678);owned[words+1]=UINT32_C(0x87654321);
    struct fixture f={.memory=owned+1,.bytes=bytes};
    struct kui_memory_test_ops ops={&f,stopped,progress,yield};
    struct kui_memory_test_result result;
    assert(kui_memory_test(owned+1,bytes,&ops,&result));
    assert(result.complete && !result.stopped && !result.errors && !result.invalid);
    assert(result.passes==70 && result.tested_bytes==bytes);
    assert(result.work_done==result.work_total && result.first_failure_offset==SIZE_MAX);
    assert(f.updates && f.yields==f.updates);
    assert(owned[0]==UINT32_C(0x12345678) && owned[words+1]==UINT32_C(0x87654321));
    free(owned);
}
int main(void) {
    /* Tiny, odd-block-tail and maximum legal allocations check every boundary. */
    exercise(4);exercise(4092);exercise(4096);exercise(4100);exercise(KUI_MEMORY_TEST_MAX);
    uint32_t memory[80];memset(memory,0x3c,sizeof(memory));
    struct fixture f={.memory=memory,.bytes=sizeof(memory),.stop=true};
    struct kui_memory_test_ops ops={&f,stopped,progress,yield};
    struct kui_memory_test_result result;
    assert(!kui_memory_test(memory,sizeof(memory),&ops,&result));
    assert(result.stopped && !result.complete && !result.errors && result.work_done==0);
    for(unsigned i=0;i<80;++i) assert(memory[i]==UINT32_C(0x3c3c3c3c));
    f=(struct fixture){.memory=memory,.bytes=sizeof(memory),.cancel_after_write=true};
    assert(!kui_memory_test(memory,sizeof(memory),&ops,&result));
    assert(result.stopped && !result.complete && !result.errors && !result.passes);
    assert(result.tested_bytes==0 && result.work_done==sizeof(memory));
    f=(struct fixture){.memory=memory,.bytes=sizeof(memory),.corrupt=true};
    assert(!kui_memory_test(memory,sizeof(memory),&ops,&result));
    assert(!result.complete && !result.stopped && result.errors==1 && !result.passes);
    assert(result.first_failure_offset==7*sizeof(uint32_t));
    assert(result.expected==0 && result.actual==UINT32_C(0x80000000));
    assert(!kui_memory_test(NULL,4,NULL,&result) && result.invalid);
    assert(!kui_memory_test(memory,0,NULL,&result) && result.invalid);
    assert(!kui_memory_test(memory,3,NULL,&result) && result.invalid);
    assert(!kui_memory_test(memory,KUI_MEMORY_TEST_MAX+4u,NULL,&result) && result.invalid);
    assert(!kui_memory_test((volatile uint32_t *)((uint8_t *)memory+1),4,NULL,&result) && result.invalid);
    assert(!kui_memory_test(memory,sizeof(memory),NULL,NULL));
    puts("PASS memory app: exact region bounds, constants/address/walking passes, bit fault and cancellation");
    return 0;
}
