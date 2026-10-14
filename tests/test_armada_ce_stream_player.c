/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE 1
#define KUI_ARMADA_CE_STREAM 1
#define KUI_RETAIL_CE 1
#define KUI_ARMADA_CE_STREAM_PLAYER_TEST 1
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
/* Test the actual player transaction functions and actual pure model. The
 * authored bus below supplies no game/kernel/audio instruction fixtures. */
#include "../src/loader/armada_ce_stream_player.c"

#define OWNER UINT32_C(0x8c14e818)
#define SLOT UINT32_C(0x06000000)
#define DEVICE UINT32_C(0x06100000)
#define BUFFER UINT32_C(0x06110000)
#define HOST UINT32_C(0x06180000)
#define DS UINT32_C(0x07cd0000)
#define CORE UINT32_C(0x07d10000)
#define OWNER_HANDLE UINT32_C(0x12345679)
static unsigned checks,cases;
#define CHECK(x) do { ++checks; if(!(x)) { \
    fprintf(stderr,"player line %d: %s\n",__LINE__,#x);abort(); \
} } while(0)

struct authored_bus {
    unsigned consumer,control,locks,unlocks,plays,stops,releases;
    unsigned submits,pumps,cancels,waits,starts;
    uint32_t cursor,frontier,now,epoch,lba,sectors,bytes,raw;
    uint32_t position_delay_ms,play_delay_ms;
    uint32_t submit_epoch[4096],last_lock_offset,last_lock_bytes;
    unsigned pending_pumps,cancel_pumps,busy_submits,sleep_visits;
    bool pending,cancelled,revoked,stale_unlock,stale_source,bad_unlock,fail_end;
    bool bad_current,fail_ticks;
    bool bad_object_page,bad_copy_page,lock_held;
    uint32_t copy_page_address;
    unsigned copy_page_hits,copy_page_flip;
} bus;

static int __attribute__((aligned(4))) revalidate(
    const struct kui_armada_ce_excerpt_binding *binding) {
    CHECK(binding==&player.binding);return 1;
}
/* Microsoft ABI outputs are32-bit addresses. Fixed owner RAM is genuinely
 * mapped below4GiB; host stack outputs retain the current stack's upper bits.
 * The test must be built-fno-pie/-no-pie so authored globals/functions also
 * retain their32-bit addresses. */
static void *stack_pointer(uint32_t address) {
    uintptr_t local=(uintptr_t)&address;
    return (void *)((local&~(uintptr_t)UINT32_MAX)|(uintptr_t)address);
}
static uint8_t sample_byte(uint32_t lba,uint32_t offset) {
    return (uint8_t)(lba*17u+offset*13u+(offset>>8)*7u);
}
static uint8_t expected_byte(uint64_t logical) {
    if(!player.model.infinite && logical>=player.model.finite_end) return 0;
    uint64_t span=(uint64_t)(player.model.end_lba-player.model.first_lba)*2352u;
    CHECK(span);
    uint64_t at=logical%span;
    return sample_byte(player.model.first_lba+(uint32_t)(at/2352u),(uint32_t)(at%2352u));
}
uint32_t kui_armada_ce_stream_player_test_word(uint32_t address) {
    if(address==CURRENT_PROCESS) return bus.bad_current?OWNER+0x9cu:OWNER;
    if(address==OWNER+12u) return SLOT;
    if(address==OWNER+8u) return OWNER_HANDLE;
    if(address==0x8c14fa80u) return 0x8c011b20u;
    if(address==0x8c011b20u) return 0x32336e57u;
    if(address==0x8c011b24u) return 0x00900003u;
    if(address==0x8c011b28u) return 0x8c0118e0u;
    if(address==0x8c011aa4u) return UINT32_MAX;
    if(address==KUI_ARMADA_CE_EXCERPT_RAM_END_WORD) return KUI_ARMADA_CE_EXCERPT_POOL_BEGIN;
    fprintf(stderr,"unproved player word %08" PRIx32 "\n",address);abort();
}
bool kui_armada_ce_stream_player_test_mapped(uint32_t address,
    struct kui_armada_ce_ram_binding *out) {
    CHECK(out && address>=SLOT && address<SLOT+0x02000000u && !(address&3u));
    uint32_t physical=0x8c400000u+(address&0x00ffffffu);
    out->physical_address=physical;out->page_word=(physical&0x1ffff000u)|0x79u;out->value=0;
    if(address==DEVICE) {
        out->value=player.binding.device_vtable;
        if(bus.bad_object_page) out->page_word^=0x1000u;
    } else if(address==BUFFER) {
        out->value=player.binding.buffer_vtable;
        if(bus.bad_object_page) out->page_word^=0x1000u;
    } else if(address==BUFFER+116u) {
        CHECK(bus.consumer==1u);out->value=bus.frontier;
    } else if(address>=DEVICE+24u && address<DEVICE+24u+16u*4u) {
        CHECK(bus.control==1u);out->value=0;
    } else if(address>=HOST && address<HOST+KUI_ARMADA_CE_STREAM_RING_BYTES) {
        CHECK(bus.consumer==1u);
        if(address==bus.copy_page_address && ++bus.copy_page_hits>=bus.copy_page_flip &&
           bus.bad_copy_page) out->page_word^=0x1000u;
    } else {
        static const uint32_t offset[9]={8u,12u,44u,76u,48u,72u,36u,16u,52u};
        uint32_t value[9]={player.binding.buffer_release,player.binding.get_caps,player.binding.lock,
            player.binding.unlock,player.binding.play,player.binding.stop,player.binding.get_status,
            player.binding.get_position,player.native.api.buffer_set_position};
        bool found=false;
        for(unsigned i=0;i<9u;i++) if(address==player.binding.buffer_vtable+offset[i]) {
            out->value=value[i];found=true;break;
        }
        CHECK(found);
    }
    return true;
}

uint32_t kui_armada_ce_excerpt_ce_call(uint32_t target,const uint32_t args[8]) {
    CHECK(args);
    if(target==player.binding.add_ref) {CHECK(!bus.consumer);return 4u;}
    if(target==player.binding.create_buffer) {
        CHECK(!bus.consumer);
        const uint32_t *desc=stack_pointer(args[1]);
        CHECK(desc[0]==20u && desc[1]==8u && desc[2]==KUI_ARMADA_CE_STREAM_RING_BYTES);
        const uint32_t *wave=stack_pointer(desc[4]);
        CHECK(wave[0]==0x00020001u && wave[1]==44100u && wave[2]==176400u && wave[3]==0x00100004u);
        *(uint32_t *)stack_pointer(args[2])=BUFFER;return 0;
    }
    if(target==player.binding.get_caps) {
        CHECK(!bus.consumer && args[0]==BUFFER);
        uint32_t *caps=stack_pointer(args[1]);caps[0]=20u;caps[1]=8u;caps[2]=KUI_ARMADA_CE_STREAM_RING_BYTES;
        return 0;
    }
    if(target==player.binding.lock) {
        CHECK(bus.consumer==1u && !bus.lock_held && args[0]==BUFFER && args[7]==0u);
        CHECK(!(args[1]&63u) && !(args[2]&63u) && args[2] &&
            args[1]+args[2]<=KUI_ARMADA_CE_STREAM_RING_BYTES);
        bus.last_lock_offset=args[1];bus.last_lock_bytes=args[2];bus.lock_held=true;++bus.locks;
        *(uint32_t *)stack_pointer(args[3])=HOST+args[1];
        *(uint32_t *)stack_pointer(args[4])=args[2];
        *(uint32_t *)stack_pointer(args[5])=0;*(uint32_t *)stack_pointer(args[6])=0;return 0;
    }
    if(target==player.binding.unlock) {
        CHECK(bus.consumer==1u && bus.lock_held && args[0]==BUFFER &&
            args[1]==HOST+bus.last_lock_offset && args[2]==bus.last_lock_bytes && !args[3] && !args[4]);
        const uint8_t *host=(const uint8_t *)(uintptr_t)args[1];
        for(unsigned i=0;i<args[2];i++) CHECK(host[i]==expected_byte(player.model.origin+player.model.produced+i));
        bus.lock_held=false;++bus.unlocks;
        if(bus.stale_unlock) {bus.stale_unlock=false;(void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);}
        return bus.bad_unlock?0x80004005u:0u;
    }
    if(target==player.binding.enter_cs) {
        CHECK(args[0]==DEVICE+88u && !bus.consumer && !bus.control);++bus.control;return 0;
    }
    if(target==player.binding.leave_cs) {
        CHECK(args[0]==DEVICE+88u && !bus.consumer && bus.control==1u);--bus.control;return 0;
    }
    if(target==player.native.api.buffer_set_position) {
        CHECK(!bus.consumer && !bus.control && args[0]==BUFFER && !args[1]);
        bus.cursor=bus.frontier=0;return 0;
    }
    if(target==player.binding.play) {
        CHECK(!bus.consumer && bus.control==1u && args[0]==BUFFER && args[3]==1u);
        CHECK(player.model.produced==KUI_ARMADA_CE_STREAM_RING_BYTES &&
            !player.model.read.id && !player.model.refill.id);
        const uint8_t *host=(const uint8_t *)(uintptr_t)HOST;
        for(unsigned i=0;i<KUI_ARMADA_CE_STREAM_RING_BYTES;i++) CHECK(host[i]==expected_byte(player.model.origin+i));
        bus.now+=bus.play_delay_ms;
        bus.cursor=(bus.cursor+((bus.play_delay_ms*176400u/1000u)&~3u))%
            KUI_ARMADA_CE_STREAM_RING_BYTES;
        bus.play_delay_ms=0;
        ++bus.plays;bus.frontier=(bus.cursor+11008u)%KUI_ARMADA_CE_STREAM_RING_BYTES;return 0;
    }
    if(target==player.binding.stop) {CHECK(!bus.consumer && !bus.control);++bus.stops;return 0;}
    if(target==player.binding.get_position) {
        CHECK(!bus.consumer && args[0]==BUFFER);
        bus.now+=bus.position_delay_ms;
        bus.cursor=(bus.cursor+((bus.position_delay_ms*176400u/1000u)&~3u))%
            KUI_ARMADA_CE_STREAM_RING_BYTES;
        bus.frontier=(bus.cursor+11008u)%KUI_ARMADA_CE_STREAM_RING_BYTES;
        bus.position_delay_ms=0;
        *(uint32_t *)stack_pointer(args[1])=bus.cursor;
        *(uint32_t *)stack_pointer(args[2])=0u;return 0;
    }
    if(target==player.binding.buffer_release || target==player.binding.device_release) {
        CHECK(!bus.consumer && !bus.control);++bus.releases;return 2u;
    }
    fprintf(stderr,"unexpected player CE target %08" PRIx32 "\n",target);abort();
}
int kui_armada_ce_stream_bind_deferred(struct kui_armada_ce_excerpt_binding *out) {
    CHECK(out);*out=player.binding;return 1;
}
int kui_armada_ce_stream_bind_native(struct kui_armada_ce_live_stream_api *out) {
    CHECK(out);*out=player.native.api;return 1;
}
int kui_armada_ce_stream_rebind_owner(const struct kui_armada_ce_excerpt_binding *binding,
    uint32_t owner_handle,struct kui_armada_ce_live_stream_api *out) {
    CHECK(binding==&player.binding && player.worker_started && owner_handle==OWNER_HANDLE && out);
    *out=player.native.api;return 1;
}
void kui_armada_ce_excerpt_owner_deferred(uint32_t unused) {(void)unused;kui_armada_ce_excerpt_attempt(SR_REQUIRED,0x40000u);}
bool kui_armada_ce_stream_native_start(struct kui_armada_ce_stream_native *state,
    const struct kui_armada_ce_live_stream_api *api,uint32_t worker,uint32_t arg,
    const struct kui_armada_ce_stream_native_ops *ops) {
    CHECK(state==&player.native && api==&state->api && !ops && !bus.consumer && !bus.control);
    CHECK(worker==(uint32_t)(uintptr_t)kui_armada_ce_stream_worker && arg==(uint32_t)(uintptr_t)&player);
    ++bus.starts;state->resumed=1u;return true;
}
bool kui_armada_ce_stream_native_ticks(struct kui_armada_ce_stream_native *state,
    const struct kui_armada_ce_stream_native_ops *ops,uint32_t *out) {
    CHECK(state==&player.native && !ops && out && !bus.consumer && !bus.control);
    if(bus.fail_ticks) {state->status=KUI_ARMADA_CE_STREAM_NATIVE_CONTEXT;return false;}
    *out=bus.now;return true;
}
bool kui_armada_ce_stream_native_sleep(struct kui_armada_ce_stream_native *state,
    const struct kui_armada_ce_stream_native_ops *ops,uint32_t ms) {
    CHECK(state==&player.native && !ops && ms<=100u && !bus.consumer && !bus.control);++bus.waits;
    if(bus.sleep_visits) {--bus.sleep_visits;return true;}
    return false;
}
bool kui_armada_ce_stream_native_write_begin(struct kui_armada_ce_stream_native *state,
    const struct kui_armada_ce_stream_native_ops *ops,uint32_t device) {
    CHECK(state==&player.native && !ops && device==DEVICE && !bus.consumer && !bus.control);
    ++bus.consumer;state->consumer_cs=DEVICE+108u;return true;
}
bool kui_armada_ce_stream_native_write_end(struct kui_armada_ce_stream_native *state,
    const struct kui_armada_ce_stream_native_ops *ops) {
    CHECK(state==&player.native && !ops && bus.consumer==1u && !bus.lock_held);
    if(bus.fail_end) return false;
    --bus.consumer;state->consumer_cs=0;return true;
}

bool kui_armada_ce_stream_transport_init(const struct kui_retail_manifest *map,uintptr_t begin,uintptr_t end) {
    CHECK(map && !bus.consumer && begin==RAW_BEGIN && end>begin);return true;
}
static void complete_bus(void) {
    CHECK(!bus.consumer && !bus.control && bus.pending);
    uint8_t *dst=(uint8_t *)(uintptr_t)bus.raw;
    for(uint32_t s=0;s<bus.sectors;s++) for(uint32_t i=0;i<2352u;i++)
        dst[s*2352u+i]=sample_byte(bus.lba+s,i);
    bus.bytes=bus.sectors*2352u;bus.pending=false;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_submit(
    uint32_t epoch,uint32_t lba,uint32_t sectors,uintptr_t destination,uint32_t capacity) {
    CHECK(!bus.consumer && !bus.control && epoch && sectors && sectors<=4u &&
        destination==RAW_BEGIN && capacity==RAW_BYTES && bus.submits<4096u);
    bus.submit_epoch[bus.submits++]=epoch;
    if(bus.busy_submits) {--bus.busy_submits;return KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY;}
    if(bus.cancelled || bus.pending) return KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY;
    bus.epoch=epoch;bus.lba=lba;bus.sectors=sectors;bus.raw=(uint32_t)destination;bus.bytes=0;bus.pending=true;bus.revoked=false;
    if(!bus.pending_pumps) complete_bus();
    return bus.pending?KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING:KUI_ARMADA_CE_STREAM_TRANSPORT_DONE;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_pump(void) {
    CHECK(!bus.consumer && !bus.control);++bus.pumps;
    if(bus.cancelled) {
        if(bus.cancel_pumps) {--bus.cancel_pumps;return KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY;}
        bus.cancelled=bus.pending=false;bus.bytes=0;return KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
    }
    if(bus.pending) {
        if(bus.pending_pumps) {--bus.pending_pumps;return KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;}
        complete_bus();
    }
    if(bus.revoked) return KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
    return bus.epoch?KUI_ARMADA_CE_STREAM_TRANSPORT_DONE:KUI_ARMADA_CE_STREAM_TRANSPORT_IDLE;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_snapshot(
    uint32_t epoch,struct kui_armada_ce_stream_transport_snapshot *out) {
    CHECK(!bus.consumer && !bus.control && out && epoch==bus.epoch);
    *out=(struct kui_armada_ce_stream_transport_snapshot){.epoch=epoch,.bytes=bus.bytes,
        .owner=bus.pending?2u:0u};
    if(bus.stale_source) {bus.stale_source=false;(void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);}
    if(bus.revoked) return KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
    return bus.pending?KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING:KUI_ARMADA_CE_STREAM_TRANSPORT_DONE;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_cancel(uint32_t epoch) {
    /* Revocation is authored state only: it performs no SCI/native operation
     * and is valid while an ambiguous native consumer lease is retained. */
    CHECK(!bus.control && epoch==bus.epoch);++bus.cancels;bus.cancelled=bus.revoked=true;
    return KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
}
enum kui_game_result kui_retail_manifest_decode(const uint8_t wire[KUI_RETAIL_IMAGE_WIRE_BYTES],struct kui_retail_manifest *out) {
    (void)wire;(void)out;return KUI_GAME_INVALID; /* Seed decoder has its separate actual tests. */
}

static void reset(bool infinite,uint32_t sectors) {
    memset(&player,0,sizeof(player));memset(&bus,0,sizeof(bus));
    memset((void *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN,0,512u*1024u);
    memset((void *)(uintptr_t)HOST,0xa5,KUI_ARMADA_CE_STREAM_RING_BYTES);
    CHECK((uintptr_t)revalidate<=UINT32_MAX && (uintptr_t)&player<=UINT32_MAX);
    player.binding=(struct kui_armada_ce_excerpt_binding){OWNER,SLOT,DEVICE,DS+0x116cu,
        DS+0x1994u,DS+0x199cu,DS+0x19dcu,DS+0x11ccu,DS+0x2910u,DS+0x29fcu,DS+0x2cbcu,
        DS+0x39acu,DS+0x2e54u,DS+0x3898u,DS+0x2c2cu,DS+0x2a44u,
        CORE+0x8ea0u,CORE+0x8efcu,(uint32_t)(uintptr_t)revalidate};
    player.native.api=(struct kui_armada_ce_live_stream_api){.owner_process=OWNER,.owner_slot=SLOT,
        .owner_handle=OWNER_HANDLE,.coredll_module=0x8c300300u,.create_thread=CORE+0x8d20u,
        .resume_thread=CORE+0x8814u,.sleep=CORE+0x8708u,.set_thread_priority=CORE+0x8834u,
        .wait_single=CORE+0x877cu,.close_handle=CORE+0x852cu,.virtual_alloc=CORE+0x88acu,
        .virtual_free=CORE+0x88b4u,.set_kmode=CORE+0x8c44u,.flush_instruction_cache=CORE+0x8cd8u,
        .get_tick_count=CORE+0x890cu,.buffer_set_position=DS+0x3154u,
        .enter_cs=CORE+0x8ea0u,.leave_cs=CORE+0x8efcu};
    player.tracks[0]=(struct kui_armada_ce_stream_track){1u,0,100u,100u+sectors};
    CHECK(kui_armada_ce_stream_init(&player.model,player.tracks,1u)==KUI_ARMADA_CE_STREAM_OK);
    player.initialized=1u;player.worker_started=1u;
    (void)kui_armada_ce_excerpt_queue(NULL,20u,1u,1u,infinite?15u:0u);
    CHECK(control());CHECK(guard());
}
static void visit(void) {
    player.now_ms=bus.now;step(SR_REQUIRED,0x40000u);
    CHECK(!bus.consumer && !bus.control && !bus.lock_held);
}
static void prime(void) {
    for(unsigned i=0;i<1000u && !bus.plays && !player.disabled;i++) visit();
    CHECK(!player.disabled && bus.plays==1u && player.model.state==KUI_ARMADA_CE_STREAM_PLAYING);
    CHECK(player.stats.device_reference && player.stats.buffer==BUFFER);
}
static void advance(uint32_t bytes) {
    bus.now+=20u;bus.cursor=(bus.cursor+bytes)%KUI_ARMADA_CE_STREAM_RING_BYTES;
    bus.frontier=(bus.cursor+11008u)%KUI_ARMADA_CE_STREAM_RING_BYTES;visit();
}
int main(void) {
    void *source_mapping=mmap((void *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN,512u*1024u,
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(source_mapping==(void *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN);
    void *host=mmap((void *)(uintptr_t)HOST,0x60000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(host==(void *)(uintptr_t)HOST);
    reset(true,1u);prime();++cases;
    /* Native calls may wait after the worker entry timestamp. A delayed
     * cursor is checked against a clock captured after that call, and Play
     * records a fresh pre-call time because hardware may start while Play
     * is still blocked before its successful return. */
    reset(true,128u);bus.play_delay_ms=80u;prime();
    CHECK(player.model.last_ms==0u && bus.now==80u && bus.cursor==14112u);
    bus.now+=20u;bus.cursor+=3528u;bus.position_delay_ms=80u;visit();
    CHECK(!player.disabled && player.model.recoveries==0u &&
        player.model.last_ms==180u && player.model.consumed==31752u);++cases;
    bus.now+=20u;bus.cursor+=35280u;visit();
    uint32_t recovery[5];kui_armada_ce_stream_fault(recovery);
    CHECK(player.model.recoveries==1u && recovery[0]==11u+KUI_ARMADA_CE_STREAM_CURSOR &&
        recovery[2]==35280u && recovery[3]==7056u && recovery[4]==20u);
    bus.bad_object_page=true;visit();kui_armada_ce_stream_fault(recovery);
    CHECK(recovery[0]==STREAM_FAULT_DEVICE);++cases;
    reset(true,128u);prime();
    player.recovery[0]=11u+KUI_ARMADA_CE_STREAM_CURSOR;
    player.stats.status=KUI_ARMADA_CE_EXCERPT_IDENTITY;
    kui_armada_ce_stream_fault(recovery);
    CHECK(!recovery[0] && !recovery[1] && !recovery[2] && !recovery[3] && !recovery[4]);++cases;
    reset(true,1u);prime();
    /* A synthetic one-sector selection verifies the2352/64 carry at a
     * consumption rate below its one-request-per-step source service bound.
     * The long-track case below separately uses the full44100Hz byte rate. */
    for(unsigned i=0;i<720u;i++) {advance(1764u);CHECK(!player.disabled && player.model.native_running);}
    CHECK(player.model.consumed>3u*KUI_ARMADA_CE_STREAM_RING_BYTES && player.model.recoveries==0u);++cases;
    reset(true,128u);prime();
    for(unsigned i=0;i<360u;i++) {advance(3528u);CHECK(!player.disabled && player.model.native_running);}
    CHECK(player.model.consumed>3u*KUI_ARMADA_CE_STREAM_RING_BYTES && player.model.recoveries==0u);++cases;
    /* Mailbox STOP dominates an immediately following RELEASE. */
    uint32_t progress[3],source_total=player.source_total;
    (void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);
    (void)kui_armada_ce_excerpt_queue(NULL,23u,0,0,0);visit();
    CHECK(player.model.state==KUI_ARMADA_CE_STREAM_STOPPED && !player.model.native_running &&
        !player.model.selection && bus.stops==1u && !player.disabled);++cases;
    kui_armada_ce_stream_progress(progress);
    CHECK(source_total && progress[0]==source_total && player.source_total==source_total &&
        player.model.fetch==0u);++cases;
    /* Repeated RELEASE while a read is in flight keeps the same useful job. */
    reset(true,1u);bus.pending_pumps=8u;visit();
    CHECK(player.read_active);uint32_t read_id=player.read.id,model_epoch=player.model.epoch;
    (void)kui_armada_ce_excerpt_queue(NULL,23u,0,0,0);visit();
    (void)kui_armada_ce_excerpt_queue(NULL,23u,0,0,0);visit();
    CHECK(player.read_active && player.read.id==read_id && player.model.epoch==model_epoch && !bus.cancels);++cases;
    /* Cancel fences temporarily retain the singleton; retry the same ticket. */
    bus.cancel_pumps=5u;(void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);visit();
    unsigned first_retry=bus.submits;
    (void)kui_armada_ce_excerpt_queue(NULL,20u,1u,1u,15u);visit();
    CHECK(!player.disabled && player.model.read.id);read_id=player.read.id;
    model_epoch=player.model.epoch;
    for(unsigned i=0;i<5u;i++) {
        visit();CHECK(!player.disabled && player.model.read.id==read_id && player.model.epoch==model_epoch);
    }
    CHECK(bus.submits>=first_retry+3u);
    for(unsigned i=first_retry;i<bus.submits;i++) CHECK(bus.submit_epoch[i]==read_id);
    ++cases;
    /* A BUSY retry independently proves no job-id/epoch consumption. */
    reset(true,1u);bus.busy_submits=3u;
    visit();read_id=player.model.read.id;model_epoch=player.model.epoch;
    for(unsigned i=0;i<2u;i++) {visit();CHECK(player.model.read.id==read_id && player.model.epoch==model_epoch);}
    CHECK(bus.submit_epoch[0]==read_id && bus.submit_epoch[1]==read_id && bus.submit_epoch[2]==read_id);
    visit();CHECK(!player.disabled && player.model.fetch==2352u);++cases;
    /* Unlock is the publication boundary: newer mail revokes the commit. */
    reset(true,1u);CHECK(create_buffer());CHECK(source());uint64_t produced=player.model.produced;
    uint32_t staged=player.model.staged;bus.stale_unlock=true;CHECK(refill());
    CHECK(player.model.produced==produced && player.model.staged==staged && bus.unlocks==1u &&
        player.mail.epoch!=player.applied_epoch);++cases;
    /* A source completion revoked before staging cannot publish old data. */
    reset(true,1u);bus.stale_source=true;CHECK(source());
    CHECK(!player.model.fetch && !player.model.staged && !player.read_active);++cases;
    /* A one-sector finite track pads its native prime and stops exactly atEOF. */
    reset(false,1u);prime();advance(2304u);CHECK(player.model.native_running);
    advance(48u);CHECK(!player.disabled && !player.model.native_running &&
        player.model.state==KUI_ARMADA_CE_STREAM_ENDED && player.model.audible==2352u && bus.stops==1u);++cases;
    /* Reference identity alone is insufficient if the object's PFN changes. */
    reset(true,1u);prime();bus.bad_object_page=true;visit();
    CHECK(player.disabled && player.stats.status==KUI_ARMADA_CE_EXCERPT_IDENTITY && bus.stops==0u);
    uint32_t failure[5];kui_armada_ce_stream_fault(failure);
    CHECK(failure[0]==STREAM_FAULT_DEVICE && failure[1]==DEVICE && failure[2]!=failure[3]);++cases;
    /* Reproduce the screenshot's twenty four-sector jobs before a fatal
     * identity failure. An already-issued next reception is revoked, fenced
     * over bounded worker visits, and never promoted to native playback. */
    reset(true,128u);
    for(unsigned i=0;i<20u;i++) visit();
    CHECK(player.source_total==20u*4u*2352u && bus.submits==20u && !bus.plays && !player.read_active);
    bus.pending_pumps=8u;visit();CHECK(player.read_active && bus.pending && bus.submits==21u);
    bus.cancel_pumps=5u;bus.bad_object_page=true;visit();
    CHECK(player.disabled && !player.read_active && player.cleanup_epoch && bus.cancelled && !bus.plays && !bus.stops);
    unsigned revoked_submits=bus.submits,revoked_pumps=bus.pumps;
    bus.sleep_visits=8u;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(!player.cleanup_epoch && !bus.pending && !bus.cancelled && bus.cancels==1u &&
        bus.submits==revoked_submits && bus.pumps==revoked_pumps+5u && !bus.plays && !bus.stops && !bus.releases);
    CHECK(player.source_total==20u*4u*2352u);
    kui_armada_ce_stream_fault(failure);
    CHECK(failure[0]==STREAM_FAULT_DEVICE && failure[1]==DEVICE && failure[2]!=failure[3]);++cases;
    /* Caller SR/FPSCR refusal must take the same storage-revocation path,
     * without attempting a native Stop through an inadmissible context. */
    reset(true,128u);bus.pending_pumps=8u;visit();CHECK(player.read_active && bus.pending);
    bus.cancel_pumps=3u;step(SR_REQUIRED|0x10000000u,0x40000u);
    CHECK(player.disabled && player.cleanup_epoch && !player.read_active && !bus.stops);
    bus.sleep_visits=8u;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(!player.cleanup_epoch && !bus.pending && !bus.cancelled && !bus.stops && !bus.plays && !bus.releases && bus.submits==1u);
    kui_armada_ce_stream_fault(failure);
    CHECK(failure[0]==STREAM_FAULT_SR && failure[2]==(SR_REQUIRED|0x10000000u));++cases;
    reset(true,128u);bus.pending_pumps=8u;visit();CHECK(player.read_active);
    bus.cancel_pumps=2u;step(SR_REQUIRED,FPSCR_NONSCALAR);
    bus.sleep_visits=8u;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(player.disabled && !player.cleanup_epoch && !bus.pending && !bus.cancelled && !bus.stops && !bus.plays);
    kui_armada_ce_stream_fault(failure);CHECK(failure[0]==STREAM_FAULT_FPSCR);++cases;
    /* A worker tick-context refusal also revokes a pending audio job. */
    reset(true,128u);bus.pending_pumps=8u;visit();CHECK(player.read_active);
    bus.cancel_pumps=4u;bus.fail_ticks=true;bus.sleep_visits=8u;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(player.disabled && !player.cleanup_epoch && !bus.pending && !bus.cancelled && !bus.stops && !bus.plays && bus.submits==1u);
    kui_armada_ce_stream_fault(failure);CHECK(failure[0]==STREAM_FAULT_TICKS);++cases;
    /* Recheck all native copy pages before committing the producer frontier. */
    reset(true,4u);CHECK(create_buffer());CHECK(source());produced=player.model.produced;
    bus.bad_copy_page=true;bus.copy_page_address=HOST;bus.copy_page_flip=2u;
    CHECK(!refill());CHECK(player.model.produced==produced && !bus.consumer && bus.unlocks==1u);++cases;
    /* Bootstrap initializes state before handing its entry to nativeThreadBase. */
    reset(true,1u);player.worker_started=0u;player.dispatching=1u;
    kui_armada_ce_excerpt_attempt(SR_REQUIRED,0x40000u);
    CHECK(bus.starts==1u && player.worker_started && !player.booting && !player.disabled);++cases;
    reset(true,1u);player.worker_started=0u;player.dispatching=0u;
    CHECK(!kui_armada_ce_excerpt_queue(NULL,33u,0,0,0) && !kui_armada_ce_stream_needs_deferred());
    CHECK(!kui_armada_ce_excerpt_queue(NULL,23u,0,0,0) && !kui_armada_ce_stream_needs_deferred());
    kui_armada_ce_excerpt_dispatch(SR_REQUIRED,0x061ff000u);
    CHECK(!bus.starts && !player.worker_started && !player.dispatching);++cases;
    /* If native context refuses LeaveCS, pure revocation is still permitted.
     * No Stop, SCI pump, Sleep or forced lease clear may follow it. */
    reset(true,1u);prime();bus.pending_pumps=5u;advance(3528u);
    CHECK(player.read_active);unsigned pumps=bus.pumps,cancels=bus.cancels,stops=bus.stops;
    bus.fail_end=true;step(SR_REQUIRED,0x40000u);
    CHECK(player.disabled && bus.consumer==1u && player.native.consumer_cs==DEVICE+108u &&
        bus.stops==stops && bus.pumps==pumps && bus.cancels==cancels+1u &&
        !player.read_active && player.cleanup_epoch==bus.epoch && bus.revoked);
    unsigned sleeps=bus.waits;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(bus.consumer==1u && player.native.consumer_cs==DEVICE+108u &&
        bus.stops==stops && bus.pumps==pumps && bus.waits==sleeps && bus.cancels==cancels+1u);++cases;
    CHECK(munmap(source_mapping,512u*1024u)==0 && munmap(host,0x60000u)==0);
    printf("armada stream player: %u cases, %u checks\n",cases,checks);return 0;
}
