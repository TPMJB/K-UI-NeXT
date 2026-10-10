/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production helper against a cache/publication model. This
 * validates byte ownership and passthrough; it does not model hardware speed.
 */
#include "kui/toy_loader_payload_control.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static _Alignas(32) uint8_t image_storage[576];
static _Alignas(32) uint8_t cached_storage[512];
static uint8_t other_storage[512];
#define IMAGE_BLOCK (image_storage+32u)
static uint32_t model_ccr;
static unsigned original_calls,purge_calls,callback_purge_calls;
static bool original_success;
static const uint8_t *expected_tx;
static uint8_t *expected_rx;
static size_t expected_count;
static bool expected_slow;
static bool expected_scratch;
static uint16_t *expected_crc;
static int context_marker;

uint32_t kui_toy_loader_payload_control_host_ccr(void) {return model_ccr;}
uint8_t *kui_toy_loader_payload_control_host_cached(uint8_t *rx) {
    assert(rx==IMAGE_BLOCK);return cached_storage;
}
void kui_toy_loader_payload_control_host_purge(uint8_t *cached,uint32_t bytes) {
    assert(cached==cached_storage && bytes==512u);
    assert(purge_calls<2u);
    if(!purge_calls) {
        /* A pre-invalidate/refill sees the owner's current P2 bytes. */
        memcpy(cached_storage,IMAGE_BLOCK,512u);
    } else {
        /* Publishing cached writes makes them visible through the P2 owner. */
        memcpy(IMAGE_BLOCK,cached_storage,512u);
    }
    ++purge_calls;
}
static bool original(void *context,const uint8_t *tx,uint8_t *rx,
    size_t count,bool slow,uint16_t *crc) {
    assert(context==&context_marker && tx==expected_tx);
    assert(count==expected_count && slow==expected_slow && crc==expected_crc);
    ++original_calls;assert(original_calls==1u);
    callback_purge_calls=purge_calls;
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 2
    if(expected_scratch) assert(rx && ((uintptr_t)rx&31u)==1u);
    else
#endif
    assert(rx==expected_rx);
    /* Declined operations exercise exact argument passthrough as well. */
    if(rx) for(size_t i=0;i<(original_success?count:(count<97u?count:97u));++i)
        rx[i]=(uint8_t)(i*29u+7u);
    if(crc && original_success) *crc=UINT16_C(0xbca7);
    return original_success;
}
static void seed(void) {
    memset(image_storage,0xa5,sizeof(image_storage));
    memset(cached_storage,0xc3,sizeof(cached_storage));
    memset(other_storage,0xd4,sizeof(other_storage));
    original_calls=purge_calls=callback_purge_calls=0u;
    model_ccr=UINT32_C(0x101);original_success=true;
    kui_toy_loader_payload_control_host_bind(IMAGE_BLOCK);
}
static void guards(void) {
    for(size_t i=0;i<32u;++i) assert(image_storage[i]==0xa5);
    for(size_t i=544u;i<576u;++i) assert(image_storage[i]==0xa5);
}
static void logical(size_t valid) {
    for(size_t i=0;i<512u;++i)
        assert(IMAGE_BLOCK[i]==(i<valid?(uint8_t)(i*29u+7u):0xa5));
    guards();
}
static bool invoke(const uint8_t *tx,uint8_t *rx,size_t count,bool slow,
    uint16_t *crc,uint8_t *original_rx) {
    expected_tx=tx;expected_rx=original_rx;expected_count=count;
    expected_slow=slow;expected_crc=crc;
    expected_scratch=original_rx==NULL && rx==IMAGE_BLOCK;
    bool result=kui_toy_loader_payload_call(original,&context_marker,tx,rx,count,slow,crc);
    assert(original_calls==1u);
    return result;
}
int main(void) {
    kui_toy_loader_payload_control_host_reset();seed();
    uint16_t crc=0u;
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 1
    uint8_t *selected=cached_storage;
#elif KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 2
    uint8_t *selected=NULL; /* Original must receive private scratch + 1. */
#else
    uint8_t *selected=IMAGE_BLOCK;
#endif
    assert(invoke(NULL,IMAGE_BLOCK,512u,false,&crc,selected));
    assert(crc==UINT16_C(0xbca7));logical(512u);
    const struct kui_toy_loader_payload_control_counts *c=kui_toy_loader_payload_control_counts();
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE
    assert(c->attempts==1u && !c->declines && c->publications==1u && !c->failed);
#else
    assert(!c->attempts && !c->declines && !c->publications && !c->failed);
#endif
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 1
    assert(purge_calls==2u && callback_purge_calls==1u);
#else
    assert(!purge_calls && !callback_purge_calls);
#endif

    /* Failure must return once; W must publish/retire every cached line,
     * while X must not expose its incomplete private scratch bytes.
     */
    seed();original_success=false;crc=UINT16_C(0x5aa5);
    assert(!invoke(NULL,IMAGE_BLOCK,512u,false,&crc,selected));
    assert(crc==UINT16_C(0x5aa5));
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 1
    logical(97u);assert(purge_calls==2u && callback_purge_calls==1u);
    assert(c->attempts==2u && c->publications==2u && c->failed==1u);
#elif KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 2
    logical(0u);assert(!purge_calls && c->attempts==2u && c->publications==1u && c->failed==1u);
#else
    logical(97u);assert(!purge_calls);
#endif

    /* Foreign pointers, cached alias, writes, short blocks, slow transfers,
     * and absent receive pointers are untouched one-call delegates.
     */
    for(unsigned which=0;which<6u;++which) {
        seed();
        const uint8_t *tx=which==0u?other_storage:NULL;
        uint8_t *rx=which==1u?other_storage:which==2u?cached_storage:which==5u?NULL:IMAGE_BLOCK;
        size_t count=which==3u?511u:512u;bool slow=which==4u;
        uint32_t attempted=c->attempts,declined=c->declines;
        assert(invoke(tx,rx,count,slow,&crc,rx));
        assert(c->attempts==attempted && !purge_calls);
#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE
        assert(c->declines==declined+1u);
#else
        assert(c->declines==declined);
#endif
        guards();
    }

#if KUI_TOY_PILOT_DATA_PAYLOAD_MODE == 1
    /* Cache control is sampled only, and incompatible profiles delegate. */
    const uint32_t declined_profiles[]={0u,0x100u,0x105u,0x104u,0x1u};
    for(size_t n=0;n<sizeof(declined_profiles)/sizeof(declined_profiles[0]);++n) {
        seed();model_ccr=declined_profiles[n];
        uint32_t attempted=c->attempts,declined=c->declines;
        assert(invoke(NULL,IMAGE_BLOCK,512u,false,&crc,IMAGE_BLOCK));
        assert(model_ccr==declined_profiles[n] && !purge_calls);
        assert(c->attempts==attempted && c->declines==declined+1u);logical(512u);
    }
#endif
    /* CRC-null is legitimate in the original block contract. */
    seed();assert(invoke(NULL,IMAGE_BLOCK,512u,false,NULL,selected));logical(512u);
    /* Counters never wrap, including failure/publication controls. */
    struct kui_toy_loader_payload_control_counts *mutable=(struct kui_toy_loader_payload_control_counts *)(uintptr_t)c;
    mutable->attempts=mutable->declines=mutable->publications=mutable->failed=UINT32_MAX;
    seed();original_success=false;
    assert(!invoke(NULL,IMAGE_BLOCK,512u,false,&crc,selected));
    seed();assert(invoke(NULL,other_storage,512u,false,&crc,other_storage));
    assert(c->attempts==UINT32_MAX && c->declines==UINT32_MAX &&
        c->publications==UINT32_MAX && c->failed==UINT32_MAX);
    guards();
    printf("Payload mode %u ownership/passthrough/publication PASS\n",KUI_TOY_PILOT_DATA_PAYLOAD_MODE);
    return 0;
}
