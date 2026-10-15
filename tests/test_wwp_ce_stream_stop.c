/* SPDX-License-Identifier: GPL-3.0-only */
/* Focused authored asynchronous-stop fixture; no retail instructions/data. */
#define KUI_WINCE_CDDA_WWP 1
#define KUI_ARMADA_CE_GENERIC_APPLICATION 1
#define KUI_ARMADA_CE_STREAM 1
#define KUI_RETAIL_CE 1
#define KUI_ARMADA_CE_STREAM_PLAYER_TEST 1
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../src/loader/armada_ce_stream_player.c"

#define DEVICE 0x06100000u
#define BUFFER 0x06110000u
static uint32_t owners[16],active;
static unsigned allocation_lock,consumer_lock,stops,sweeps;
static bool release_on_second_sweep,read_failure;

bool kui_armada_ce_stream_player_test_mapped(uint32_t address,
    struct kui_armada_ce_ram_binding *out) {
    assert(allocation_lock==1u && consumer_lock==1u);
    if(read_failure) return false;
    *out=(struct kui_armada_ce_ram_binding){0};
    if(address==DEVICE+132u) out->value=active;
    else {
        assert(address>=DEVICE+24u && address<DEVICE+88u);
        out->value=owners[(address-DEVICE-24u)/4u];
    }
    return true;
}
uint32_t kui_armada_ce_excerpt_ce_call(uint32_t target,const uint32_t args[8]) {
    if(target==1u) {
        if(args[0]==DEVICE+88u) {assert(!allocation_lock && !consumer_lock);allocation_lock=1u;}
        else {assert(args[0]==DEVICE+108u && allocation_lock && !consumer_lock);consumer_lock=1u;}
    } else if(target==2u) {
        if(args[0]==DEVICE+108u) {assert(allocation_lock && consumer_lock);consumer_lock=0u;}
        else {assert(args[0]==DEVICE+88u && allocation_lock && !consumer_lock);allocation_lock=0u;}
    } else if(target==3u) {
        assert(args[0]==BUFFER && !allocation_lock && !consumer_lock);++stops;
    } else {
        assert(target==4u && args[0]==BUFFER && !allocation_lock && !consumer_lock);
        ++sweeps;
        if(release_on_second_sweep && sweeps==2u) owners[2]=owners[3]=0u;
    }
    return 0u;
}
void kui_armada_ce_stream_transport_priority(uint32_t bytes,bool enabled) {(void)bytes;(void)enabled;}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_cancel(uint32_t epoch) {
    (void)epoch;assert(0);return KUI_ARMADA_CE_STREAM_TRANSPORT_STALE;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_pump(void) {
    assert(0);return KUI_ARMADA_CE_STREAM_TRANSPORT_STALE;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_snapshot(
    uint32_t epoch,struct kui_armada_ce_stream_transport_snapshot *out) {
    (void)epoch;(void)out;assert(0);return KUI_ARMADA_CE_STREAM_TRANSPORT_STALE;
}
static void reset(void) {
    memset(&player,0,sizeof(player));memset(owners,0,sizeof(owners));
    allocation_lock=consumer_lock=stops=sweeps=0u;release_on_second_sweep=read_failure=false;
    player.binding.owner_slot=0x06000000u;player.binding.device=DEVICE;
    player.binding.enter_cs=1u;player.binding.leave_cs=2u;
    player.binding.stop=3u;player.binding.get_status=4u;player.stats.buffer=BUFFER;
    player.model.initialized=true;player.model.epoch=7u;
    player.model.native_running=player.model.stop_needed=true;
    player.model.state=KUI_ARMADA_CE_STREAM_QUIESCING;
    player.model.after_stop=KUI_ARMADA_CE_STREAM_STOPPED;
    owners[2]=BUFFER;owners[3]=BUFFER&0x01ffffffu;
    owners[9]=BUFFER+0x1000u;active=(1u<<2)|(1u<<3)|(1u<<9);
}
static void pending(void) {
    assert(player.model.stop_needed && player.model.native_running && !player.stats.stop_ok);
    assert(stops==1u && !allocation_lock && !consumer_lock);
}
int main(void) {
    reset();release_on_second_sweep=true;
    assert(stopped());pending();assert(!sweeps);
    assert(stopped());pending();assert(stops==1u && !sweeps);
    active=1u<<9;
    assert(stopped());pending();assert(sweeps==1u);
    assert(stopped());assert(sweeps==2u && stops==1u && !player.model.stop_needed);
    assert(!player.model.native_running && player.stats.stop_ok==1u && !allocation_lock && !consumer_lock);
    /* A saved voice with zero owner is still pending until callback ack. */
    reset();assert(stopped());owners[2]=owners[3]=0u;
    assert(stopped());pending();assert(!sweeps);
    active=1u<<9;assert(stopped());assert(!player.model.stop_needed && sweeps==1u);
    /* A reused voice belongs to the replacement owner, including its bit. */
    reset();assert(stopped());owners[2]=owners[3]=BUFFER+0x1000u;
    assert(stopped());assert(!player.model.stop_needed && sweeps==1u && stops==1u);
    reset();read_failure=true;assert(!stopped());
    assert(player.model.stop_needed && player.model.native_running && !stops);
    assert(!allocation_lock && !consumer_lock);
    puts("WWP stop: asynchronous ack, stereo sweeps, zero/reused owners and read refusal pass");
    return 0;
}
