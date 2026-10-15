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
    unsigned submits,pumps,cancels,waits,starts,owner_admissions,priority_updates;
    unsigned set_positions,play_submits[32];
    uint32_t play_cached[32];
    uint32_t priority_fresh;
    uint32_t cursor,frontier,now,epoch,lba,sectors,bytes,raw;
    uint32_t position_delay_ms,play_delay_ms;
    uint32_t submit_epoch[4096],last_lock_offset,last_lock_bytes;
    unsigned pending_pumps,read_latency_pumps,cancel_pumps,busy_submits,sleep_visits;
    unsigned token_slices;
    uint32_t checked_slice_bytes;
    bool release_after_source_copy;
    bool stop_after_source_copy;
    bool release_at_snapshot;
    bool release_at_unlock;
    uint32_t last_sleep_ms;
    bool pending,cancelled,revoked,stale_unlock,stale_source,bad_unlock,fail_end;
    bool bad_current,fail_ticks,driver_context,fail_owner_admission,urgent,priority_active,pio_ready;
    bool bad_object_page,bad_copy_page,lock_held;
    uint32_t copy_page_address;
    unsigned copy_page_hits,copy_page_flip;
} bus;
static struct kui_armada_ce_excerpt_binding admitted_binding;
static const struct kui_retail_manifest *decoded_manifest;
void kui_armada_ce_stream_player_test_source_copied(void) {
    if(bus.stop_after_source_copy) {
        bus.stop_after_source_copy=false;
        (void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);
    }
    if(bus.release_after_source_copy) {
        bus.release_after_source_copy=false;
        (void)kui_armada_ce_excerpt_queue(NULL,23u,0,0,0);
    }
}

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
    if(address==CURRENT_PROCESS) return bus.bad_current || bus.driver_context?OWNER+0x9cu:OWNER;
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
#if KUI_WINCE_CDDA_WORMS
        extern uint32_t kui_armada_ce_stream_player_test_stop_owner(unsigned,uint32_t);
        out->value=kui_armada_ce_stream_player_test_stop_owner((address-DEVICE-24u)/4u,out->value);
#endif
#if KUI_WINCE_CDDA_WORMS
    } else if(address==DEVICE+112u) {
        extern uint32_t kui_armada_ce_stream_player_test_stop_active(void);
        CHECK(bus.control==1u);out->value=kui_armada_ce_stream_player_test_stop_active();
#endif
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
    if(target==OWNER_CALLBACK) {
        CHECK(bus.driver_context && args[0]==(uint32_t)(uintptr_t)player.callback_info &&
            player.callback_info[0]==OWNER_HANDLE && player.callback_info[2]==CALLBACK_TOKEN &&
            !player.binding.device && !player.binding.play && !bus.starts && !bus.plays);
        bus.driver_context=false;
        uint32_t result=kui_armada_ce_excerpt_owner_callback(CALLBACK_TOKEN);
        bus.driver_context=true;return result;
    }
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
        if(bus.release_at_unlock) {bus.release_at_unlock=false;(void)kui_armada_ce_excerpt_queue(NULL,23u,0,0,0);}
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
        ++bus.set_positions;bus.cursor=bus.frontier=0;return 0;
    }
    if(target==player.binding.play) {
        CHECK(!bus.consumer && bus.control==1u && args[0]==BUFFER && args[3]==1u);
        CHECK(player.model.produced>=KUI_ARMADA_CE_STREAM_START_BYTES &&
            player.model.produced<=KUI_ARMADA_CE_STREAM_RING_BYTES &&
            !player.model.read.id && !player.model.refill.id);
        const uint8_t *host=(const uint8_t *)(uintptr_t)HOST;
        for(unsigned i=0;i<player.model.produced;i++) CHECK(host[i]==expected_byte(player.model.origin+i));
        CHECK(bus.plays<32u);
        bus.play_submits[bus.plays]=bus.submits;bus.play_cached[bus.plays]=player.start_cached;
        bus.now+=bus.play_delay_ms;
        bus.cursor=(bus.cursor+((bus.play_delay_ms*176400u/1000u)&~3u))%
            KUI_ARMADA_CE_STREAM_RING_BYTES;
        bus.play_delay_ms=0;
        ++bus.plays;bus.frontier=(bus.cursor+11008u)%KUI_ARMADA_CE_STREAM_RING_BYTES;return 0;
    }
    if(target==player.binding.stop) {CHECK(!bus.consumer && !bus.control);++bus.stops;return 0;}
#if KUI_WINCE_CDDA_WORMS
    if(target==player.binding.get_status) {
        extern uint32_t kui_armada_ce_stream_player_test_stop_status(void);
        CHECK(!bus.consumer && !bus.control && args[0]==BUFFER && !args[2] && !args[3]);
        *(uint32_t *)stack_pointer(args[1])=0u;
        return kui_armada_ce_stream_player_test_stop_status();
    }
#endif
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
    CHECK(out);memset(out,0,sizeof(*out));out->owner_process=OWNER;out->owner_slot=SLOT;return 1;
}
int kui_armada_ce_stream_admit_owner(struct kui_armada_ce_excerpt_binding *out) {
    CHECK(out==&player.binding && !bus.driver_context && !bus.starts && !bus.plays);
    ++bus.owner_admissions;
    if(bus.fail_owner_admission) return 0;
    *out=admitted_binding;return 1;
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
    bus.last_sleep_ms=ms;
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
    CHECK(!bus.consumer && !bus.control && epoch && sectors && sectors<=KUI_ARMADA_CE_STREAM_BATCH_SECTORS &&
        destination==RAW_BEGIN && capacity==RAW_BYTES && bus.submits<4096u);
    bus.submit_epoch[bus.submits++]=epoch;
    if(bus.busy_submits) {--bus.busy_submits;return KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY;}
    if(bus.cancelled || bus.pending) return KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY;
    if(epoch<=bus.epoch) return KUI_ARMADA_CE_STREAM_TRANSPORT_STALE;
    bus.epoch=epoch;bus.lba=lba;bus.sectors=sectors;bus.raw=(uint32_t)destination;bus.bytes=0;bus.pending=true;bus.revoked=false;
    if(bus.read_latency_pumps) bus.pending_pumps=bus.read_latency_pumps;
    if(!bus.pending_pumps && !bus.pio_ready && !bus.token_slices && !bus.checked_slice_bytes) complete_bus();
    return bus.pending?KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING:KUI_ARMADA_CE_STREAM_TRANSPORT_DONE;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_pump(void) {
    CHECK(!bus.consumer && !bus.control);++bus.pumps;
    if(bus.cancelled) {
        if(bus.cancel_pumps) {--bus.cancel_pumps;return KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY;}
        bus.cancelled=bus.pending=false;bus.bytes=0;return KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
    }
    if(bus.pending) {
        if(bus.token_slices) {--bus.token_slices;return KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;}
        if(bus.pending_pumps) {--bus.pending_pumps;return KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;}
        if(bus.pio_ready || bus.checked_slice_bytes) {
            uint32_t total=bus.sectors*2352u,count=total-bus.bytes;
            uint32_t limit=bus.checked_slice_bytes?bus.checked_slice_bytes:512u;
            if(count>limit) count=limit;
            uint8_t *dst=(uint8_t *)(uintptr_t)bus.raw;
            for(uint32_t i=0;i<count;i++) {
                uint32_t at=bus.bytes+i;
                dst[at]=sample_byte(bus.lba+at/2352u,at%2352u);
            }
            bus.bytes+=count;if(bus.bytes==total) bus.pending=false;
        } else complete_bus();
    }
    if(bus.revoked) return KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
    return bus.epoch?KUI_ARMADA_CE_STREAM_TRANSPORT_DONE:KUI_ARMADA_CE_STREAM_TRANSPORT_IDLE;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_pump_worker(void) {
    return kui_armada_ce_stream_transport_pump();
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_snapshot(
    uint32_t epoch,struct kui_armada_ce_stream_transport_snapshot *out) {
    CHECK(!bus.consumer && !bus.control && out && epoch==bus.epoch);
    *out=(struct kui_armada_ce_stream_transport_snapshot){.epoch=epoch,.bytes=bus.bytes,
        .owner=bus.pending?2u:0u};
    if(bus.stale_source) {bus.stale_source=false;(void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);}
    if(bus.release_at_snapshot) {bus.release_at_snapshot=false;(void)kui_armada_ce_excerpt_queue(NULL,23u,0,0,0);}
    if(bus.revoked) return KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
    return bus.pending?KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING:KUI_ARMADA_CE_STREAM_TRANSPORT_DONE;
}
enum kui_armada_ce_stream_transport_status kui_armada_ce_stream_transport_cancel(uint32_t epoch) {
    /* Revocation is authored state only: it performs no SCI/native operation
     * and is valid while an ambiguous native consumer lease is retained. */
    CHECK(!bus.control && epoch==bus.epoch);++bus.cancels;bus.cancelled=bus.revoked=true;
    return KUI_ARMADA_CE_STREAM_TRANSPORT_CANCELLED;
}
void kui_armada_ce_stream_transport_priority(uint32_t fresh,bool active) {
    /* Transport's own tests verify hysteresis and grants. This seam checks
     * the real worker publishes fresh readiness and clears on control/fault. */
    ++bus.priority_updates;bus.priority_fresh=fresh;bus.priority_active=active;
    bus.urgent=active && fresh<=176400u;
}
bool kui_armada_ce_stream_transport_urgent(void) {return bus.urgent;}
bool kui_armada_ce_stream_transport_worker_can_pump(void) {
    CHECK(!bus.consumer && !bus.control);
    return bus.pio_ready && bus.pending && !bus.cancelled && !bus.revoked && !bus.pending_pumps;
}
enum kui_game_result kui_retail_manifest_decode(const uint8_t wire[KUI_RETAIL_IMAGE_WIRE_BYTES],struct kui_retail_manifest *out) {
    /* The wire decoder has separate tests. Opt in only to exercise the
     * actual player's seed/map admission and model-track construction. */
    (void)wire;
    if(!decoded_manifest) return KUI_GAME_INVALID;
    *out=*decoded_manifest;return KUI_GAME_OK;
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
    admitted_binding=player.binding;
    player.native.api=(struct kui_armada_ce_live_stream_api){.owner_process=OWNER,.owner_slot=SLOT,
        .owner_handle=OWNER_HANDLE,.coredll_module=0x8c300300u,.create_thread=CORE+0x8d20u,
        .resume_thread=CORE+0x8814u,.sleep=CORE+0x8708u,.set_thread_priority=CORE+0x8834u,
        .wait_single=CORE+0x877cu,.close_handle=CORE+0x852cu,.virtual_alloc=CORE+0x88acu,
        .virtual_free=CORE+0x88b4u,.set_kmode=CORE+0x8c44u,.flush_instruction_cache=CORE+0x8cd8u,
        .get_tick_count=CORE+0x890cu,.buffer_set_position=DS+0x3154u,
        .enter_cs=CORE+0x8ea0u,.leave_cs=CORE+0x8efcu};
    PLAYER_TRACKS[0]=(struct kui_armada_ce_stream_track){1u,0,100u,100u+sectors};
    CHECK(kui_armada_ce_stream_init(&player.model,PLAYER_TRACKS,1u)==KUI_ARMADA_CE_STREAM_OK);
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
static unsigned drifting_clock(uint32_t ppm,unsigned seconds) {
    /* The audio cursor follows the AICA's 44.1kHz clock; GetTickCount
     * follows CE's timer. A rate mismatch of `ppm` parts per million makes the
     * cursor run ahead of the tick clock by ppm*elapsed. Any cumulative cursor
     * allowance with fixed slack eventually stops healthy playback (each
     * stop is an audible dropout and a re-prime); the cursor is authoritative. */
    reset(true,20000u);bus.read_latency_pumps=1u;
    for(unsigned i=0;i<1000u && !bus.plays && !player.disabled;i++) {bus.now+=20u;visit();}
    CHECK(bus.plays==1u && !player.disabled);
    uint64_t audio_us=0,played=0;
    for(uint32_t tick=0;tick<seconds*1000u;tick+=20u) {
        bus.now+=20u;audio_us+=20000u+20u*ppm/1000u;
        uint64_t target=(audio_us*176400u/1000000u)&~UINT64_C(3);
        uint32_t bytes=(uint32_t)(target-played);played=target;
        if(player.model.native_running)
            bus.cursor=(bus.cursor+bytes)%KUI_ARMADA_CE_STREAM_RING_BYTES;
        bus.frontier=(bus.cursor+11008u)%KUI_ARMADA_CE_STREAM_RING_BYTES;
        visit();CHECK(!player.disabled);
    }
    return player.model.recoveries;
}
static void sustained_async(void) {
    /* Real native visits observed82ms apart. Completion needs multiple
     * bounded pumps, so a job takes164ms after the replacement is issued.
     * Assert finite-time native Play and then check consumed PCM at44100Hz,
     * across four complete rings; a four-sector job cannot sustain this. */
    reset(true,128u);bus.read_latency_pumps=2u;
    unsigned visits=0;
    while(!bus.plays && visits<32u && !player.disabled) {
        bus.now+=82u;visit();++visits;
    }
    CHECK(bus.plays==1u && visits<32u && !player.disabled &&
        player.model.state==KUI_ARMADA_CE_STREAM_PLAYING);
    uint64_t physical=0;
    for(unsigned i=1;i<=120u;i++) {
        uint64_t next=((uint64_t)i*82u*176400u/1000u)&~UINT64_C(3);
        uint32_t bytes=(uint32_t)(next-physical);
        const uint8_t *host=(const uint8_t *)(uintptr_t)HOST;
        for(uint32_t n=0;n<bytes;n++)
            CHECK(host[(bus.cursor+n)%KUI_ARMADA_CE_STREAM_RING_BYTES]==
                expected_byte(player.model.origin+physical+n));
        bus.now+=82u;bus.cursor=(bus.cursor+bytes)%KUI_ARMADA_CE_STREAM_RING_BYTES;
        bus.frontier=(bus.cursor+11008u)%KUI_ARMADA_CE_STREAM_RING_BYTES;
        visit();physical=next;
        CHECK(!player.disabled && player.model.native_running &&
            player.model.recoveries==0u && player.model.consumed==physical);
    }
    CHECK(physical>4u*KUI_ARMADA_CE_STREAM_RING_BYTES && bus.plays==1u && !bus.stops);
    ++cases;
}
static void playing_reserve(uint32_t bytes) {
    CHECK(player.model.native_running && player.model.produced>=bytes);
    player.model.consumed=player.model.produced-bytes;
    player.model.audible=player.model.origin+player.model.consumed;
    player.model.last_play=(uint32_t)(player.model.consumed%KUI_ARMADA_CE_STREAM_RING_BYTES);
    bus.cursor=player.model.last_play;
    bus.frontier=(bus.cursor+11008u)%KUI_ARMADA_CE_STREAM_RING_BYTES;
}
static void bounded_playing_source(void) {
    /* A playing visit retains one16-sector ticket but publishes its checked
     * prefix before the real worker's unchanged Sleep(1). Framing consumes
     * one of the same32 calls rather than adding uncounted physical work. */
    reset(true,20000u);prime();bus.pio_ready=true;playing_reserve(65536u);
    unsigned pumps=bus.pumps,sleeps=bus.waits;
    uint32_t source_before=player.source_total;
    uint64_t produced=player.model.produced;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(bus.pumps-pumps==SOURCE_PLAYING_PUMPS && bus.waits==sleeps+1u &&
        bus.last_sleep_ms==SOURCE_STEP_MS && !player.source_pumps_left &&
        player.source_budget_yielded==1u && player.read_active && bus.pending &&
        player.read.sectors==KUI_ARMADA_CE_STREAM_BATCH_SECTORS &&
        player.source_total-source_before==31u*512u &&
        player.model.produced-produced==31u*512u && player.model.staged<64u);
    /* Re-entering source within this same step neither pumps nor admits
     * another physical job. Snapshotting and draining remain safe. */
    pumps=bus.pumps;unsigned submits=bus.submits;
    CHECK(source() && drain_refill() && bus.pumps==pumps && bus.submits==submits);
    bool replacement=false;
    for(unsigned i=0;i<3u && !replacement;i++) {
        pumps=bus.pumps;submits=bus.submits;source_before=player.source_total;
        produced=player.model.produced;visit();
        CHECK(!player.disabled && bus.pumps-pumps<=SOURCE_PLAYING_PUMPS &&
            player.source_total-source_before<=SOURCE_PLAYING_PUMPS*512u &&
            player.model.produced-produced==player.source_total-source_before &&
            player.model.staged<64u);
        replacement=bus.submits>submits;
        if(replacement) CHECK(bus.pumps-pumps==SOURCE_PLAYING_PUMPS &&
            bus.bytes>0u && bus.bytes<RAW_BYTES && player.read_active && bus.pending);
    }
    CHECK(replacement);++cases;
    /* Even token-only work exhausts the allowance; it cannot turn the
     * ready loop into an unbounded high-priority token search. */
    reset(true,20000u);prime();bus.pio_ready=true;bus.token_slices=64u;
    playing_reserve(65536u);pumps=bus.pumps;source_before=player.source_total;
    visit();
    CHECK(bus.pumps-pumps==SOURCE_PLAYING_PUMPS && bus.token_slices==33u &&
        player.source_total==source_before && player.read_active && bus.pending &&
        player.source_budget_yielded==1u);
    visit();CHECK(bus.token_slices==1u && player.source_total==source_before);
    visit();CHECK(!bus.token_slices && player.source_total>source_before &&
        player.model.staged<64u && !player.disabled);
    /* A command observed with no allowance left still revokes the pending
     * ticket before staging. Only cancellation may fence its old lease. */
    pumps=bus.pumps;source_before=player.source_total;bus.stale_source=true;
    CHECK(source() && bus.pumps==pumps && player.source_total==source_before);
    CHECK(control() && bus.cancels==1u && !player.read_active && !bus.pending);
    ++cases;
    reset(true,20000u);prime();bus.pio_ready=true;player.source_pumps_left=0u;
    pumps=bus.pumps;submits=bus.submits;source_before=player.source_total;
    CHECK(source() && !player.read_active && player.model.read.id &&
        bus.pumps==pumps && bus.submits==submits && player.source_total==source_before);
    ++cases;
}
static void bounded_pio_schedule(uint32_t block_us) {
    /* Explicit timing model, not measured console throughput: every playing
     * visit is charged the full32-pump maximum at0.5 or1ms per call, plus
     * 4ms for native checks/copy, and a50ms wake delay. Charge audio for that
     * complete interval before each visit, including calls that only frame
     * or wait for tokens. This is stricter than the actual call count when
     * the ring fills. Production player/model paths publish and validate
     * every consumed PCM byte across multiple ring wraps. */
    const uint32_t native_us=4000u,wake_us=50000u;
    const uint32_t burst_us=SOURCE_PLAYING_PUMPS*block_us+native_us;
    const uint32_t interval_us=wake_us+burst_us;
    reset(true,20000u);prime();bus.pio_ready=true;
    uint32_t first_source=player.source_total;
    uint64_t elapsed_us=0u,played=0u;
    unsigned max_pumps=0u;
    for(unsigned i=0;i<300u;i++) {
        elapsed_us+=interval_us;
        uint64_t target=(elapsed_us*176400u/1000000u)&~UINT64_C(3);
        uint32_t bytes=(uint32_t)(target-played);
        const uint8_t *host=(const uint8_t *)(uintptr_t)HOST;
        CHECK(bytes<=ready_bytes());
        for(uint32_t n=0;n<bytes;n++)
            CHECK(host[(bus.cursor+n)%KUI_ARMADA_CE_STREAM_RING_BYTES]==
                expected_byte(player.model.origin+played+n));
        bus.now=(uint32_t)(elapsed_us/1000u);
        bus.cursor=(bus.cursor+bytes)%KUI_ARMADA_CE_STREAM_RING_BYTES;
        bus.frontier=(bus.cursor+11008u)%KUI_ARMADA_CE_STREAM_RING_BYTES;
        unsigned pumps=bus.pumps;visit();pumps=bus.pumps-pumps;
        if(pumps>max_pumps) max_pumps=pumps;
        if(player.disabled || player.model.recoveries || player.model.consumed!=target)
            fprintf(stderr,"bounded model: call=%u visit=%u pumps=%u disabled=%u recoveries=%u consumed=%" PRIu64
                " target=%" PRIu64 " staged=%u delay=%u ready=%u\n",block_us,i,pumps,
                player.disabled,player.model.recoveries,player.model.consumed,target,
                player.model.staged,step_delay(),ready_bytes());
        CHECK(pumps<=SOURCE_PLAYING_PUMPS && pumps*block_us+native_us<=burst_us &&
            !player.disabled && player.model.native_running && !player.model.recoveries &&
            player.model.consumed==target);
        played=target;
    }
    uint64_t supplied=(uint64_t)(player.source_total-first_source)*1000000u;
    CHECK(max_pumps==SOURCE_PLAYING_PUMPS && played>8u*KUI_ARMADA_CE_STREAM_RING_BYTES &&
        supplied>elapsed_us*176400u && bus.plays==1u && !bus.stops);
    printf("bounded PIO model: call=%uus native=%uus wake=%uus max-pumps=%u burst<=%uus supply=%" PRIu64 "B/s\n",
        block_us,native_us,wake_us,max_pumps,burst_us,supplied/elapsed_us);
    ++cases;
}
static void cached_start_aux(uint32_t cached,uint32_t latency) {
    struct kui_armada_ce_stream_trace_snapshot history;
    kui_armada_ce_stream_trace_snapshot(&history);
    uint32_t rows=history.counts[0]<KUI_ARMADA_CE_STREAM_TRACE_ROWS?
        history.counts[0]:KUI_ARMADA_CE_STREAM_TRACE_ROWS;
    CHECK(rows && history.events[rows-1u][0]==KUI_ARMADA_CE_STREAM_TRACE_START);
    CHECK(history.events[rows-1u][4]==((cached/64u)<<16|latency));
}
static void cached_restarts(void) {
    reset(true,128u);prime();
    CHECK(player.cache_first==100u && player.cache_end==228u && player.cache_bytes==CACHE_BYTES &&
        CACHE_BYTES==KUI_ARMADA_CE_STREAM_START_BYTES && bus.play_submits[0]==7u &&
        !bus.play_cached[0] && bus.set_positions==1u);
    cached_start_aux(0u,0u);
    unsigned submits=bus.submits,plays=bus.plays;
    (void)kui_armada_ce_excerpt_queue(NULL,20u,1u,1u,15u);visit();
    CHECK(!player.disabled && bus.plays==plays+1u && bus.set_positions==2u &&
        bus.play_submits[plays]==submits && bus.play_cached[plays]==CACHE_BYTES &&
        player.model.native_running && player.model.origin==0u && player.model.last_play==0u);
    cached_start_aux(CACHE_BYTES,0u);
    ++cases;
    /* STOP retains the checked head; another PLAY still restarts at zero. */
    (void)kui_armada_ce_excerpt_queue(NULL,33u,0,0,0);visit();
    CHECK(player.model.state==KUI_ARMADA_CE_STREAM_STOPPED && player.cache_bytes==CACHE_BYTES);
    submits=bus.submits;plays=bus.plays;
    (void)kui_armada_ce_excerpt_queue(NULL,20u,1u,1u,15u);visit();
    CHECK(!player.disabled && bus.plays==plays+1u && bus.set_positions==3u &&
        bus.play_submits[plays]==submits && bus.play_cached[plays]==CACHE_BYTES &&
        player.model.native_running && player.model.origin==0u && player.model.last_play==0u);
    cached_start_aux(CACHE_BYTES,0u);
    ++cases;
    /* A different FAD interval must obtain its own checked bytes. */
    submits=bus.submits;plays=bus.plays;bus.read_latency_pumps=4u;
    (void)kui_armada_ce_excerpt_queue(NULL,21u,251u,378u,15u);visit();
    CHECK(!player.disabled && bus.plays==plays && bus.submits>submits &&
        player.model.state==KUI_ARMADA_CE_STREAM_PREFILL && player.model.first_lba==101u &&
        player.cache_first==101u && player.cache_end==228u && !player.cache_bytes);
    ++cases;
    /* A late mailbox change revokes the copied cached ticket before model
     * publication. RELEASE can retain that useful epoch on the next visit. */
    reset(true,128u);prime();
    (void)kui_armada_ce_excerpt_queue(NULL,20u,1u,1u,15u);CHECK(control() && stopped());
    submits=bus.submits;plays=bus.plays;uint32_t epoch=player.model.epoch;
    bus.release_after_source_copy=true;CHECK(source_run(true));
    CHECK(!player.model.fetch && !player.model.staged && !player.model.produced &&
        !player.start_cached && player.mail.epoch!=player.applied_epoch &&
        player.model.epoch==epoch && bus.submits==submits && !player.read_active);
    bus.now+=17u;visit();
    CHECK(!player.disabled && bus.plays==plays+1u && player.model.epoch==epoch &&
        bus.play_submits[plays]==submits && bus.play_cached[plays]==CACHE_BYTES);
    cached_start_aux(CACHE_BYTES,17u);
    ++cases;
    (void)kui_armada_ce_excerpt_queue(NULL,20u,1u,1u,15u);CHECK(control() && stopped());
    submits=bus.submits;plays=bus.plays;bus.stop_after_source_copy=true;CHECK(source_run(true));
    CHECK(!player.model.fetch && !player.model.staged && !player.model.produced &&
        !player.start_cached && player.mail.epoch!=player.applied_epoch &&
        bus.submits==submits && !player.read_active);
    visit();
    CHECK(!player.disabled && bus.plays==plays && bus.submits==submits &&
        player.model.state==KUI_ARMADA_CE_STREAM_STOPPED && !player.model.read.id &&
        !player.model.staged && !player.model.produced);
    ++cases;
    /* RELEASE at the first cached Unlock retains staged PCM but abandons
     * its uncommitted refill ticket. The next visit can prime all seven jobs. */
    reset(true,128u);prime();
    (void)kui_armada_ce_excerpt_queue(NULL,20u,1u,1u,15u);CHECK(control() && stopped());
    submits=bus.submits;plays=bus.plays;epoch=player.model.epoch;
    CHECK(source_run(true) && player.model.fetch==RAW_BYTES && player.model.staged==RAW_BYTES);
    uint32_t head=player.stage_head;bus.release_at_unlock=true;CHECK(drain_refill());
    CHECK(!player.model.produced && player.model.staged==RAW_BYTES && player.stage_head==head &&
        !player.model.refill.id && player.model.epoch==epoch &&
        player.mail.epoch!=player.applied_epoch && bus.submits==submits && bus.plays==plays);
    bus.now+=11u;visit();
    CHECK(!player.disabled && player.model.epoch==epoch && !player.model.refill.id &&
        bus.plays==plays+1u && bus.play_submits[plays]==submits && bus.play_cached[plays]==CACHE_BYTES);
    cached_start_aux(CACHE_BYTES,11u);++cases;
    /* The same Unlock race during normal playback must not Stop/Play or
     * strand checked bytes behind a permanently BUSY refill ticket. */
    reset(true,128u);prime();CHECK(source() && player.model.staged==RAW_BYTES);
    uint64_t produced=player.model.produced;head=player.stage_head;
    epoch=player.model.epoch;plays=bus.plays;unsigned stops=bus.stops;
    bus.release_at_unlock=true;CHECK(refill());
    CHECK(player.model.produced==produced && player.model.staged==RAW_BYTES &&
        player.stage_head==head && !player.model.refill.id && player.model.native_running);
    CHECK(control() && player.model.epoch==epoch && drain_refill());
    CHECK(player.model.produced==produced+RAW_BYTES && !player.model.staged &&
        !player.model.refill.id && player.model.native_running &&
        bus.plays==plays && bus.stops==stops && !player.disabled);++cases;
}
static bool initialize_fixture(const struct kui_retail_manifest *map) {
    static const uint32_t seed[KUI_ARMADA_CE_STREAM_SEED_WORDS]=KUI_ARMADA_CE_STREAM_BLANK_WORDS;
    memset(&player,0,sizeof(player));memset(&bus,0,sizeof(bus));
    memset((void *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN,0,512u*1024u);
    memcpy((void *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN,seed,sizeof(seed));
    decoded_manifest=map;bool result=initialize();decoded_manifest=NULL;
    return result;
}
static void initialized_track_controls(void) {
    enum kui_wince_cdda_profile profile=KUI_WINCE_CDDA_COMPILED_PROFILE;
    const struct kui_wince_cdda_disc *disc=kui_wince_cdda_disc(profile);
    struct kui_retail_manifest raw={.card_sectors=8000000u,.partition_start=2048u,
        .partition_end=8000000u,.track_count=disc->track_count,.extent_count=disc->track_count,
        .session_lba=45000u,.boot_lba=disc->boot_lba,.boot_bytes=disc->kernel_bytes,
        .storage_transport=KUI_STORAGE_SCI,.reader=KUI_RETAIL_READER_STANDARD};
    strcpy(raw.product,disc->product);strcpy(raw.bootfile,"0WINCEOS.BIN");
    uint32_t card_lba=4096u;
    for(unsigned i=0;i<disc->track_count;i++) {
        uint32_t end=i+1u<disc->track_count?disc->starts[i+1u]:549000u;
        raw.slots[i].track=(struct kui_retail_track){.start_lba=disc->starts[i],.end_lba=end,
            .first_extent=(uint16_t)(disc->track_count+i),.extent_count=1u,
            .control=(uint8_t)kui_wince_cdda_track_control(profile,i)};
        uint32_t blocks=((end-disc->starts[i])*2352u+511u)/512u;
        raw.slots[disc->track_count+i].extent=(struct kui_retail_extent){0u,card_lba,blocks};
        card_lba+=blocks;
    }
    CHECK(initialize_fixture(&raw));
    CHECK(player.initialized && player.model.initialized && player.model.track_count==disc->track_count);
    ++cases;
    struct kui_retail_manifest cooked=raw;cooked.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    for(unsigned i=0;i<disc->track_count;i++) if(cooked.slots[i].track.control==4u) {
        struct kui_retail_track *track=&cooked.slots[i].track;
        track->control|=KUI_RETAIL_TRACK_COOKED;
        cooked.slots[disc->track_count+i].extent.blocks=
            ((track->end_lba-track->start_lba)*2048u+511u)/512u;
    }
    /* Storage-only flags must survive in the map, while the pure CD model
     * receives only TOC controls. This calls actual initialize(), not init. */
    CHECK(initialize_fixture(&cooked));
    for(unsigned i=0;i<disc->track_count;i++) {
        CHECK(player.map.slots[i].track.control==cooked.slots[i].track.control);
        CHECK(player.model.tracks[i].control==kui_wince_cdda_track_control(profile,i));
    }
    ++cases;
    cooked.flags=0u;CHECK(!initialize_fixture(&cooked));++cases;
    cooked=raw;cooked.flags=KUI_RETAIL_IMAGE_BOOT_CRC;
    for(unsigned i=0;i<disc->track_count;i++) if(!cooked.slots[i].track.control) {
        cooked.slots[i].track.control|=KUI_RETAIL_TRACK_COOKED;break;
    }
    CHECK(!initialize_fixture(&cooked));++cases;
}
static void ram_copy_boundaries(void) {
    _Alignas(4) uint8_t src[160],dst[160],expected[160];
    for(unsigned i=0;i<sizeof(src);i++) src[i]=(uint8_t)(i*29u+7u);
    /* Every alignment and tail; compare untouched destination canaries too.
     * UBSan rejects a word access if the SH4 alignment gate is bypassed. */
    for(unsigned s=0;s<4u;s++) for(unsigned d=0;d<4u;d++)
        for(unsigned bytes=0;bytes<=129u;bytes++) {
            memset(dst,0xa5,sizeof(dst));memcpy(expected,dst,sizeof(dst));
            memcpy(expected+8u+d,src+8u+s,bytes);
            copy_bytes(dst+8u+d,src+8u+s,bytes);
            CHECK(!memcmp(dst,expected,sizeof(dst)));
        }
    /* The staging carry compacts toward lower addresses and may overlap. */
    for(unsigned gap=1;gap<=64u;gap++) for(unsigned bytes=0;bytes<=64u;bytes++) {
        memcpy(dst,src,sizeof(dst));memcpy(expected,src,sizeof(expected));
        memmove(expected+8u,expected+8u+gap,bytes);
        copy_bytes(dst+8u,dst+8u+gap,bytes);
        CHECK(!memcmp(dst,expected,sizeof(dst)));
    }
    ++cases;
}
int main(int argc,char **argv) {
    void *source_mapping=mmap((void *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN,512u*1024u,
        PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(source_mapping==(void *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN);
    void *host=mmap((void *)(uintptr_t)HOST,0x60000u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    CHECK(host==(void *)(uintptr_t)HOST);
    if(argc==2 && !strcmp(argv[1],"--cache-only")) {
        cached_restarts();
        CHECK(munmap(source_mapping,512u*1024u)==0 && munmap(host,0x60000u)==0);
        printf("armada checked-head cache: %u cases, %u checks\n",cases,checks);return 0;
    }
    ram_copy_boundaries();initialized_track_controls();
    if(argc==2 && !strcmp(argv[1],"--initialize-only")) {
        CHECK(munmap(source_mapping,512u*1024u)==0 && munmap(host,0x60000u)==0);
        printf("CE player track initialization: %u cases, %u checks\n",cases,checks);return 0;
    }
    reset(true,1u);prime();++cases;
    struct kui_armada_ce_stream_trace_snapshot history;
    kui_armada_ce_stream_trace_snapshot(&history);
    CHECK(history.counts[0]==1u && history.events[0][0]==KUI_ARMADA_CE_STREAM_TRACE_START &&
        history.events[0][3]>=KUI_ARMADA_CE_STREAM_START_BYTES &&
        history.events[0][3]<=KUI_ARMADA_CE_STREAM_RING_BYTES);
    /* An underrun retains the fresh-byte balance before recovery re-primes
     * the model, and the returned cursor's certified elapsed time. */
    player.model.produced=player.model.consumed+64u;
    uint32_t fetched_before=player.source_total;
    advance(128u);kui_armada_ce_stream_trace_snapshot(&history);
    CHECK(player.model.recoveries==1u && history.counts[0]==2u &&
        history.events[1][0]==KUI_ARMADA_CE_STREAM_TRACE_UNDERRUN &&
        history.events[1][1]==20u && history.events[1][2]==fetched_before &&
        history.events[1][3]==64u && history.events[1][4]==20u);++cases;
    /* The real worker publishes post-observation readiness while a source
     * job is delayed, then withdraws urgency when gameplay is paused. */
    reset(true,128u);prime();bus.pending_pumps=1000u;
    for(unsigned i=0;i<3u;i++) {bus.now+=230u;advance(44100u);}
    CHECK(!player.disabled && player.model.native_running && bus.urgent &&
        bus.priority_active && bus.priority_fresh==ready_bytes() &&
        bus.priority_fresh<=176400u);
    (void)kui_armada_ce_excerpt_queue(NULL,22u,0,0,0);visit();
    CHECK(!bus.urgent && !bus.priority_active &&
        player.model.state==KUI_ARMADA_CE_STREAM_PAUSED);++cases;
    /* Native calls may wait after the worker entry timestamp. A delayed
     * cursor is checked against a clock captured after that call, and Play
     * records a fresh pre-call time because hardware may start while Play
     * is still blocked before its successful return. */
    reset(true,128u);bus.play_delay_ms=80u;prime();
    CHECK(player.model.last_ms==0u && bus.now==80u && bus.cursor==14112u);
    bus.now+=20u;bus.cursor+=3528u;bus.position_delay_ms=80u;visit();
    CHECK(!player.disabled && player.model.recoveries==0u &&
        player.model.last_ms==180u && player.model.consumed==31752u);++cases;
    /* A coarse native cursor may report a large catch-up. While the jump stays
     * inside fresh data it is consumption, not a fault: no Stop/re-prime. */
    uint32_t consumed_before=player.model.consumed;
    bus.now+=20u;bus.cursor+=35280u;visit();
    uint32_t recovery[5];kui_armada_ce_stream_fault(recovery);
    CHECK(!player.disabled && player.model.recoveries==0u && !recovery[0] &&
        player.model.consumed==consumed_before+35280u && player.model.native_running);
    bus.bad_object_page=true;visit();kui_armada_ce_stream_fault(recovery);
    CHECK(recovery[0]==STREAM_FAULT_DEVICE);++cases;
    reset(true,128u);prime();
    player.recovery[0]=11u+KUI_ARMADA_CE_STREAM_CURSOR;
    player.stats.status=KUI_ARMADA_CE_EXCERPT_IDENTITY;
    kui_armada_ce_stream_fault(recovery);
    CHECK(!recovery[0] && !recovery[1] && !recovery[2] && !recovery[3] && !recovery[4]);++cases;
    reset(true,1u);prime();
    /* A synthetic one-sector selection verifies the2352/64 carry at a
     * consumption rate below its bounded source service rate.
     * The long-track case below separately uses the full44100Hz byte rate. */
    for(unsigned i=0;i<720u;i++) {advance(1764u);CHECK(!player.disabled && player.model.native_running);}
    CHECK(player.model.consumed>3u*KUI_ARMADA_CE_STREAM_RING_BYTES && player.model.recoveries==0u);++cases;
    /* A completed asynchronous job immediately starts its replacement,
     * before the worker yields or makes another native position call. */
    reset(true,128u);bus.read_latency_pumps=2u;
    kui_armada_ce_stream_trace_snapshot(&history);uint32_t completed_before=history.counts[4];
    visit();
    CHECK(bus.submits==1u && player.read_active && step_delay()==SOURCE_STEP_MS);
    bus.now+=20u;visit();CHECK(bus.submits==1u && player.source_total==0u);
    bus.now+=20u;visit();CHECK(player.source_total==RAW_BYTES && bus.submits==2u &&
        player.read_active && bus.pending && player.model.staged<64u &&
        step_delay()==SOURCE_STEP_MS);
    kui_armada_ce_stream_trace_snapshot(&history);
    CHECK(history.counts[4]==completed_before+1u && history.counts[3]>=40u);++cases;
    /* Waiting for native ring space retains the20ms idle cadence; a
     * pending source ticket uses the shorter native yield. */
    reset(true,128u);prime();
    for(unsigned i=0;i<16u && player.model.staged<64u && !player.disabled;i++) visit();
    CHECK(player.model.staged>=64u && !player.read_active && step_delay()==STEP_MS);
    bus.sleep_visits=0u;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(bus.last_sleep_ms==STEP_MS);++cases;
    reset(true,128u);bus.read_latency_pumps=8u;bus.sleep_visits=0u;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(player.read_active && bus.last_sleep_ms==SOURCE_STEP_MS);++cases;
    sustained_async();bounded_playing_source();
    bounded_pio_schedule(500u);bounded_pio_schedule(1000u);
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
    visit();CHECK(!player.disabled && player.model.fetch==2u*2352u);++cases;
    /* Unlock is the publication boundary: newer mail revokes the commit. */
    reset(true,1u);CHECK(create_buffer());CHECK(source());uint64_t produced=player.model.produced;
    uint32_t staged=player.model.staged;bus.stale_unlock=true;CHECK(refill());
    CHECK(player.model.produced==produced && player.model.staged==staged && bus.unlocks==1u &&
        player.mail.epoch!=player.applied_epoch);++cases;
    /* A source completion revoked before staging cannot publish old data. */
    /* A complete16-sector PIO job fits one worker visit, rather than
     * requiring three CE timer wakes at the observed20visits/second. */
    reset(true,128u);bus.pio_ready=true;
    unsigned pio_before=bus.pumps;CHECK(source());
    CHECK(!player.read_active && !bus.pending && bus.bytes==RAW_BYTES &&
        bus.pumps-pio_before<=SOURCE_READY_PUMPS+1u &&
        player.model.staged==RAW_BYTES && player.source_total==RAW_BYTES);++cases;
    /* A token search can yield with neither DMA nor a ready block. Its
     * next bounded slice must not wait for another native timer visit. */
    reset(true,128u);bus.pio_ready=true;bus.token_slices=SOURCE_TOKEN_SLACK;
    pio_before=bus.pumps;CHECK(source());
    CHECK(!player.read_active && !bus.pending && !bus.token_slices &&
        player.source_total==RAW_BYTES && bus.pumps-pio_before<=SOURCE_READY_PUMPS+1u);++cases;
    /* A card that keeps returning token wait cannot spin indefinitely. */
    reset(true,128u);bus.pio_ready=true;bus.token_slices=2u*SOURCE_READY_PUMPS;
    pio_before=bus.pumps;CHECK(source());
    CHECK(player.read_active && bus.pending && bus.bytes==0u &&
        bus.token_slices==SOURCE_READY_PUMPS && bus.pumps-pio_before==SOURCE_READY_PUMPS+1u);
    CHECK(source() && player.read_active && bus.pending && bus.bytes==0u && !bus.token_slices);
    CHECK(source() && !player.read_active && player.source_total==RAW_BYTES);++cases;
    /* Certified prefixes reach the native ring while the same physical
     * source job is still in flight. No extra submit or early job completion. */
    reset(true,128u);bus.checked_slice_bytes=4096u;CHECK(create_buffer());
    CHECK(source() && player.read_active && player.model.read.id &&
        player.model.fetch==4096u && player.model.staged==4096u &&
        player.read.skip==4096u && player.read.bytes==RAW_BYTES-4096u);
    uint32_t partial_id=player.read.id;
    CHECK(drain_refill() && player.model.produced==4096u && !player.model.staged &&
        player.read_active && player.read.id==partial_id && bus.submits==1u);
    for(unsigned i=0;i<32u && player.read_active;i++) CHECK(source() && drain_refill());
    CHECK(!player.read_active && !player.model.read.id && !player.model.staged &&
        player.model.produced==RAW_BYTES && player.source_total==RAW_BYTES && bus.submits==1u);
    for(uint32_t i=0;i<RAW_BYTES;i++) CHECK(((uint8_t *)(uintptr_t)HOST)[i]==expected_byte(i));
    ++cases;
    /* A resumed job's first card blocks can precede its audio skip. They
     * produce no PCM yet; later prefixes retain exact bytes and tiny carry. */
    reset(true,128u);bus.checked_slice_bytes=512u;CHECK(create_buffer());
    player.model.origin=player.model.audible=player.model.fetch=2000u;
    for(unsigned i=0;i<4u;i++) CHECK(source() && !player.source_total && !player.model.staged);
    for(unsigned i=0;i<128u && player.read_active;i++) CHECK(source() && drain_refill());
    CHECK(!player.read_active && bus.submits==1u && player.source_total==RAW_BYTES-2000u &&
        player.model.fetch==RAW_BYTES && player.model.staged==48u &&
        player.model.produced==RAW_BYTES-2048u);
    for(uint32_t i=0;i<player.model.produced;i++)
        CHECK(((uint8_t *)(uintptr_t)HOST)[i]==expected_byte(2000u+i));
    ++cases;
    /* Cancellation revokes the next prefix without committing old bytes. */
    reset(true,128u);bus.checked_slice_bytes=4096u;CHECK(create_buffer());
    CHECK(source() && drain_refill());bus.stale_source=true;CHECK(source());
    CHECK(player.source_total==4096u && player.model.produced==4096u && !player.model.staged &&
        player.read_active && !bus.cancels);
    CHECK(control() && !player.read_active && bus.cancels==1u && player.source_total==4096u);++cases;
    /* RELEASE during a completed job's staging copy is idempotent. Keep
     * that DONE lease for commit; resubmitting its epoch would be stale. */
    reset(true,128u);bus.release_after_source_copy=true;
    CHECK(source() && player.read_active && player.model.read.id &&
        !player.source_total && !player.model.staged && bus.submits==1u);
    visit();CHECK(!player.disabled && player.source_total==2u*RAW_BYTES &&
        bus.submits==2u && !player.read_active);++cases;
    /* The same RELEASE race can occur before copying, on a pending or
     * completed transport snapshot. Neither case revokes the unchanged job. */
    for(unsigned pio=0;pio<2u;pio++) {
        reset(true,128u);bus.pio_ready=pio!=0u;bus.release_at_snapshot=true;
        CHECK(source() && player.read_active && player.model.read.id &&
            !player.source_total && !bus.cancels && bus.submits==1u);
        visit();CHECK(!player.disabled && player.source_total==2u*RAW_BYTES &&
            bus.submits==2u && !player.read_active && !bus.cancels);
    }
    ++cases;
    /* The final replacement, not the first source/refill pair, crosses the
     * native ring boundary. A one-sector end-of-repeat fragment first ends
     * at R-1024; the checked prefix of its16-sector replacement must publish
     * both the short tail and wrapped remainder before the worker yields. Otherwise
     * the first50ms visit moves the native copied frontier past ring0 and
     * blocks that remainder; the second50ms visit reports UNDERRUN. */
    reset(true,128u);prime();bus.pio_ready=true;
    CHECK(!player.read_active && !player.model.read.id && !player.model.refill.id &&
        player.model.staged<64u);
    {
        const uint32_t ring_bytes=KUI_ARMADA_CE_STREAM_RING_BYTES;
        const uint32_t first_bytes=2304u,initial_ready=12000u;
        const uint64_t produced=ring_bytes-1024u-first_bytes;
        const uint64_t span=128u*2352u;
        const uint64_t fetch=2u*span-first_bytes;
        player.model.origin=fetch-produced;
        player.model.produced=produced;player.model.consumed=produced-initial_ready;
        player.model.fetch=fetch;player.model.staged=0u;player.stage_head=0u;
        player.model.audible=player.model.origin+player.model.consumed;
        player.model.last_play=(uint32_t)(player.model.consumed%ring_bytes);
        player.model.last_ms=bus.now;
        bus.cursor=player.model.last_play;
        bus.frontier=(bus.cursor+11008u)%ring_bytes;
        player.model.write_cursor=bus.frontier;
        uint8_t *host_bytes=(uint8_t *)(uintptr_t)HOST;
        memset(host_bytes,0xa5,ring_bytes);
        for(uint64_t i=player.model.consumed;i<produced;i++)
            host_bytes[i%ring_bytes]=expected_byte(player.model.origin+i);
        uint32_t before_source=player.source_total;
        unsigned before_submits=bus.submits,before_stops=bus.stops,before_pumps=bus.pumps;
        visit();
        uint32_t fetched=player.source_total-before_source;
        CHECK(bus.pumps-before_pumps==SOURCE_PLAYING_PUMPS);
        uint64_t published_before_yield=player.model.produced;
        uint32_t staged_before_yield=player.model.staged;
        CHECK(!player.disabled && fetched>first_bytes+1024u && fetched<first_bytes+RAW_BYTES &&
            bus.submits-before_submits==2u && player.read_active && bus.pending);
        for(unsigned i=0;i<2u;i++) {
            bus.now+=50u;bus.cursor=(bus.cursor+8820u)%ring_bytes;
            bus.frontier=(bus.cursor+11008u)%ring_bytes;visit();
        }
        if(published_before_yield!=produced+fetched ||
           staged_before_yield>=64u || player.model.recoveries)
            fprintf(stderr,"replacement wrap: published=%" PRIu64 " expected=%" PRIu64
                " staged=%" PRIu32 " recoveries=%" PRIu32 " stops=%u\n",
                published_before_yield,produced+fetched,staged_before_yield,
                player.model.recoveries,bus.stops-before_stops);
        CHECK(published_before_yield==produced+fetched && staged_before_yield<64u &&
            !player.disabled && player.model.native_running && !player.model.recoveries &&
            bus.stops==before_stops);
    }
    ++cases;
    /* A command arriving between one-block masks prevents any old source
     * ticket from reaching staging, even if furtherPIO bytes are ready. */
    reset(true,128u);bus.pio_ready=true;bus.stale_source=true;CHECK(source());
    CHECK(control());
    CHECK(!player.source_total && !player.model.staged && bus.cancels==1u &&
        !bus.pending && !player.read_active);++cases;
    reset(true,1u);bus.stale_source=true;CHECK(source());
    CHECK(control());
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
    /* Revoke a partially primed stream before native playback. An
     * already-issued next reception is revoked, fenced
     * over bounded worker visits, and never promoted to native playback. */
    reset(true,128u);
    CHECK(create_buffer());
    for(unsigned i=0;i<8u;i++) {CHECK(source());CHECK(refill());}
    CHECK(player.source_total==8u*RAW_BYTES && bus.submits==8u && !bus.plays && !player.read_active);
    bus.pending_pumps=8u;visit();CHECK(player.read_active && bus.pending && bus.submits==9u);
    bus.cancel_pumps=5u;bus.bad_object_page=true;visit();
    CHECK(player.disabled && !player.read_active && player.cleanup_epoch && bus.cancelled && !bus.plays && !bus.stops);
    unsigned revoked_submits=bus.submits,revoked_pumps=bus.pumps;
    bus.sleep_visits=8u;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(!player.cleanup_epoch && !bus.pending && !bus.cancelled && bus.cancels==1u &&
        bus.submits==revoked_submits && bus.pumps==revoked_pumps+5u && !bus.plays && !bus.stops && !bus.releases);
    CHECK(player.source_total==8u*RAW_BYTES);
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
    /* A full16-sector refill beginning at page offset4032 reaches eleven
     * pages. The last page must be witnessed before and after copying. */
    reset(true,128u);CHECK(create_buffer());
    player.model.origin=672u;player.model.produced=4032u;
    player.model.fetch=player.model.origin+player.model.produced;
    CHECK(source() && player.model.staged==RAW_BYTES);produced=player.model.produced;
    bus.bad_copy_page=true;bus.copy_page_address=HOST+10u*4096u;bus.copy_page_flip=2u;
    CHECK(!refill());
    CHECK(bus.last_lock_offset==4032u && bus.last_lock_bytes==RAW_BYTES &&
        bus.copy_page_hits==2u && player.model.produced==produced &&
        player.model.staged==RAW_BYTES && !bus.consumer && bus.unlocks==1u);++cases;
    /* A driver-context discovery has no sound targets. The owner callback
     * must admit the full binding before any native thread or COM operation. */
    reset(true,1u);player.worker_started=0u;bus.driver_context=true;
    kui_armada_ce_excerpt_dispatch(SR_REQUIRED,0x081ff000u);
    CHECK(bus.owner_admissions==1u && bus.starts==1u && player.worker_started &&
        !player.dispatching && !player.disabled && bus.driver_context && !bus.plays);++cases;
    reset(true,1u);player.worker_started=0u;bus.driver_context=true;bus.fail_owner_admission=true;
    kui_armada_ce_excerpt_dispatch(SR_REQUIRED,0x081ff000u);
    CHECK(bus.owner_admissions==1u && !bus.starts && !player.worker_started &&
        !player.dispatching && !player.binding.device && !bus.plays && !bus.locks);++cases;
    reset(true,1u);player.worker_started=0u;player.dispatching=1u;
    CHECK(!kui_armada_ce_excerpt_owner_callback(CALLBACK_TOKEN^1u) &&
        !bus.owner_admissions && !bus.starts);++cases;
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
    reset(true,1u);prime();bus.pending_pumps=5u;advance(3528u);advance(3528u);
    CHECK(player.read_active);unsigned pumps=bus.pumps,cancels=bus.cancels,stops=bus.stops;
    bus.fail_end=true;step(SR_REQUIRED,0x40000u);
    CHECK(player.disabled && bus.consumer==1u && player.native.consumer_cs==DEVICE+108u &&
        bus.stops==stops && bus.pumps==pumps && bus.cancels==cancels+1u &&
        !player.read_active && player.cleanup_epoch==bus.epoch && bus.revoked);
    unsigned sleeps=bus.waits;
    CHECK(kui_armada_ce_stream_worker((uint32_t)(uintptr_t)&player)==UINT32_MAX);
    CHECK(bus.consumer==1u && player.native.consumer_cs==DEVICE+108u &&
        bus.stops==stops && bus.pumps==pumps && bus.waits==sleeps && bus.cancels==cancels+1u);++cases;
    cached_restarts();
    /* Retained event rows remain chronological after wrapping the six-row
     * ring; total event count continues independently of retention. */
    kui_armada_ce_stream_trace_snapshot(&history);uint32_t previous_events=history.counts[0];
    for(uint32_t i=0;i<8u;i++)
        kui_armada_ce_stream_trace_event(KUI_ARMADA_CE_STREAM_TRACE_START,10000u+i,i,64u,i);
    kui_armada_ce_stream_trace_snapshot(&history);
    CHECK(history.counts[0]==previous_events+8u);
    for(uint32_t i=0;i<KUI_ARMADA_CE_STREAM_TRACE_ROWS;i++)
        CHECK(history.events[i][1]==10002u+i && history.events[i][2]==i+2u &&
            history.events[i][4]==i+2u);
    ++cases;
    /* Half a percent of clock mismatch for two minutes: no false stops. */
    CHECK(drifting_clock(5000u,120u)==0u);++cases;
    CHECK(munmap(source_mapping,512u*1024u)==0 && munmap(host,0x60000u)==0);
    printf("armada stream player: %u cases, %u checks\n",cases,checks);return 0;
}
