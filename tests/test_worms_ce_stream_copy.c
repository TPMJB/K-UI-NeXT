/* SPDX-License-Identifier: GPL-3.0-only */
/* The real Worms player branch with the existing authored RAM/native-call
 * fixture. Keep that fixture's arbitrary process coordinates; runtime
 * identity/address admission has separate exact-profile binder tests. */
#define KUI_WINCE_CDDA_WORMS 1
#include "kui/armada_ce_stream_native.h"
#undef KUI_ARMADA_CE_LIVE_PROCESS_BASE
#undef KUI_ARMADA_CE_LIVE_CURRENT_PROCESS
#define KUI_ARMADA_CE_LIVE_PROCESS_BASE UINT32_C(0x8c14e6e0)
#define KUI_ARMADA_CE_LIVE_CURRENT_PROCESS UINT32_C(0x8c145890)
#define main worms_unused_armada_fixture_main
#include "test_armada_ce_stream_player.c"
#undef main

static uint32_t frontier_after_chunk[3];
static unsigned chunk_calls;
static bool stop_after_chunk,status_clears_owners;
static uint32_t stop_owners[16],stop_active;
static unsigned status_calls;
void kui_armada_ce_stream_player_test_copy_chunk(uint32_t copied) {
    CHECK(bus.consumer==1u && bus.lock_held && !(copied&63u));
    ++chunk_calls;
    if(chunk_calls<=3u && frontier_after_chunk[chunk_calls-1u]!=UINT32_MAX)
        bus.frontier=frontier_after_chunk[chunk_calls-1u];
    if(stop_after_chunk) {stop_after_chunk=false;(void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);}
}
uint32_t kui_armada_ce_stream_player_test_stop_owner(unsigned index,uint32_t value) {
    CHECK(bus.control==1u && !bus.consumer);
    (void)value;CHECK(index<16u);return stop_owners[index];
}
uint32_t kui_armada_ce_stream_player_test_stop_active(void) {
    CHECK(bus.control==1u && !bus.consumer);return stop_active;
}
uint32_t kui_armada_ce_stream_player_test_stop_status(void) {
    CHECK(!bus.control && !bus.consumer);++status_calls;
    if(status_clears_owners) for(unsigned i=0;i<16u;i++)
        if(stop_owners[i]==BUFFER || stop_owners[i]==(BUFFER&0x01ffffffu)) stop_owners[i]=0u;
    return 0u;
}
static void copy_fixture(uint32_t offset,uint32_t bytes,uint32_t play,uint32_t frontier) {
    reset(true,128u);prime();
    player.model.produced=KUI_ARMADA_CE_STREAM_RING_BYTES+offset;
    player.model.consumed=player.model.produced-65536u;
    player.model.last_play=play;player.model.write_cursor=frontier;
    player.model.staged=bytes;player.model.read=(struct kui_armada_ce_stream_read){0};
    player.stage_head=0u;bus.frontier=frontier;chunk_calls=0u;stop_after_chunk=false;
    for(unsigned i=0;i<3u;i++) frontier_after_chunk[i]=UINT32_MAX;
    uint8_t *stage=(void *)(uintptr_t)STAGE_BEGIN;
    uint8_t *host=(void *)(uintptr_t)(HOST+offset);
    for(unsigned i=0;i<bytes;i++) stage[i]=expected_byte(player.model.origin+player.model.produced+i);
    /* Partial-abandon cases retain previously valid bytes. The normal native
     * Unlock fixture checks exact PCM even when publication was revoked. */
    memcpy(host,stage,bytes);bus.locks=bus.unlocks=0u;
}
int main(void) {
    void *source=mmap((void *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN,512u*1024u,
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    void *host=mmap((void *)(uintptr_t)HOST,0x60000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(source==(void *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN && host==(void *)(uintptr_t)HOST);
    /* The 11072-byte in-flight slice is end-exclusive. Adjacent publication
     * succeeds; the preceding 64-byte block remains protected. */
    copy_fixture(16384u,192u,4096u,5312u);
    memset((void *)(uintptr_t)(HOST+16384u),0xa5,192u);
    uint64_t produced=player.model.produced;
    CHECK(refill() && chunk_calls==3u && player.model.produced==produced+192u && !player.model.staged);
    CHECK(!bus.consumer && !bus.lock_held && bus.unlocks==1u);++cases;
    copy_fixture(16320u,192u,4096u,5312u);produced=player.model.produced;
    CHECK(refill() && !chunk_calls && player.model.produced==produced && player.model.staged==192u &&
        !player.model.refill.id && !player.disabled);++cases;
    /* A newly observed frontier can revoke a ticket after one or two chunks;
     * neither copied prefix advances the model or consumes staging. */
    for(unsigned after=1u;after<=2u;after++) {
        copy_fixture(32768u,256u,4096u,8192u);produced=player.model.produced;
        frontier_after_chunk[after-1u]=32768u;
        CHECK(refill() && chunk_calls==after && player.model.produced==produced &&
            player.model.staged==256u && !player.model.refill.id && !bus.consumer && !bus.lock_held &&
            bus.unlocks==1u && !player.disabled);++cases;
    }
    /* The reserved native slice wraps across ring zero. ExactlyR is the
     * reviewed temporary tail cursor and normalizes to zero, not an error. */
    copy_fixture(64u,128u,KUI_ARMADA_CE_STREAM_RING_BYTES-4096u,
        KUI_ARMADA_CE_STREAM_RING_BYTES-1024u);produced=player.model.produced;
    CHECK(refill() && !chunk_calls && player.model.produced==produced && !player.model.refill.id);++cases;
    copy_fixture(16384u,128u,0u,KUI_ARMADA_CE_STREAM_RING_BYTES);produced=player.model.produced;
    CHECK(refill() && chunk_calls==2u && player.model.produced==produced+128u);++cases;
    copy_fixture(32768u,128u,4096u,8192u);produced=player.model.produced;stop_after_chunk=true;
    CHECK(refill() && chunk_calls==1u && player.model.produced==produced && player.model.staged==128u &&
        !player.model.refill.id && !bus.consumer && bus.unlocks==1u);++cases;
    copy_fixture(32768u,128u,4096u,8192u);produced=player.model.produced;
    bus.copy_page_address=(HOST+32768u)&~0xfffu;bus.copy_page_flip=3u;bus.bad_copy_page=true;
    CHECK(!refill() && chunk_calls==1u && player.model.produced==produced &&
        !bus.consumer && bus.unlocks==1u);++cases;
    copy_fixture(16320u,128u,4096u,5312u);bus.bad_unlock=true;
    CHECK(!refill() && !chunk_calls && !bus.consumer);++cases;
    /* Callback acknowledgment clears active+112 but retains both owners.
     * GetStatus performs the separate owner sweep, with no copy lease held. */
    reset(true,128u);prime();stop_owners[0]=BUFFER;stop_owners[3]=BUFFER&0x01ffffffu;
    stop_active=9u;status_calls=0u;status_clears_owners=false;
    (void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);CHECK(control());
    CHECK(stopped() && player.model.stop_needed && bus.stops==1u && !status_calls);
    CHECK(player.stop_voice_mask==9u);
    CHECK(stopped() && player.model.stop_needed && bus.stops==1u && !status_calls);
    stop_active=0u;
    CHECK(stopped() && player.model.stop_needed && status_calls==1u && bus.stops==1u);
    status_clears_owners=true;
    CHECK(stopped() && !player.model.stop_needed && status_calls==2u && bus.stops==1u &&
        !player.stop_requested_epoch && !player.stop_voice_mask);++cases;
    /* An active bit reused by a different buffer cannot retain our old
     * lease; absence alone while its saved bit remains active is insufficient. */
    reset(true,128u);prime();stop_owners[0]=BUFFER;stop_active=1u;status_calls=0u;
    (void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);CHECK(control());
    CHECK(stopped() && player.model.stop_needed && !status_calls);
    stop_owners[0]=0u;
    CHECK(stopped() && player.model.stop_needed && !status_calls && bus.stops==1u);
    stop_owners[0]=BUFFER+4u;
    CHECK(stopped() && !player.model.stop_needed && status_calls==1u && bus.stops==1u);++cases;
    CHECK(munmap(source,512u*1024u)==0 && munmap(host,0x60000u)==0);
    printf("Worms atomic host copy: %u cases, %u checks; in-flight boundary/wrap, moving frontier, stale mail and asynchronous Stop quiescence passed\n",cases,checks);
    return 0;
}
