/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/target.h"
#include "kui/armada_ce_excerpt.h"
#include "kui/armada_ce_stream_seed.h"
#include "kui/armada_ce_stream_model.h"
#include "kui/armada_ce_stream_native.h"
#include "kui/armada_ce_stream_transport.h"
#include "kui/armada_ce_stream_trace.h"
#if !KUI_ARMADA_CE_STREAM_PLAYER_TEST
#define KUI_ARMADA_CE_STREAM_OWNER_WORD_SCOPE static __attribute__((noinline,noclone))
#endif
#include "kui/armada_ce_stream_owner_word.h"
#include <stddef.h>

#if !defined(KUI_ARMADA_CE_STREAM) || KUI_ARMADA_CE_STREAM != 1 || !defined(KUI_RETAIL_CE)
#error "The full-track player belongs only to the isolated Armada CE stream"
#endif
#define CURRENT_PROCESS KUI_ARMADA_CE_LIVE_CURRENT_PROCESS
#define SR_REQUIRED UINT32_C(0x40000000)
#define SR_CHECK UINT32_C(0x700000f0)
#define OWNER_CALLBACK UINT32_C(0xfffffd1f)
#define CALLBACK_TOKEN UINT32_C(0x4b554943)
#define CALLBACK_DONE UINT32_C(0x43454453)
#define FPSCR_NONSCALAR UINT32_C(0x00180000)
#define RAW_BYTES KUI_ARMADA_CE_STREAM_BATCH_BYTES
#define RAW_BEGIN KUI_ARMADA_CE_STREAM_SOURCE_BEGIN
#define STAGE_BEGIN (RAW_BEGIN+RAW_BYTES)
#define CACHE_BEGIN (STAGE_BEGIN+KUI_ARMADA_CE_STREAM_STAGE_BYTES)
#define CACHE_BYTES KUI_ARMADA_CE_STREAM_START_BYTES
#define BUFFER_SOFTWARE 8u
#define DEVICE_VOICES 16u
#define DEVICE_OWNER_OFFSET 24u
#define DEVICE_CS_OFFSET 88u
#define WORKER_TOKEN UINT32_C(0x5354524d)
#define STEP_MS 20u
#define SOURCE_STEP_MS 1u
/* One whole unaligned batch, initial reception setup, and bounded token
 * slack (at most4096 additional token-clock bytes). Token-only iterations
 * must not force a nearly completed job to wait for a native timer visit. */
#define SOURCE_TOKEN_SLACK 16u
#define SOURCE_READY_PUMPS ((RAW_BYTES+1022u)/512u+1u+SOURCE_TOKEN_SLACK)
/* Share this allowance across both source visits in one playing step.
 * At most16KiB of512-byte receptions can run before the existing native
 * sleep; setup and token-only work consume the same allowance. Prefill
 * keeps its eager whole-job behavior before native playback starts. */
#define SOURCE_PLAYING_PUMPS 32u
#define COPY_PAGES ((KUI_ARMADA_CE_STREAM_STAGE_BYTES+4095u)/4096u+1u)
_Static_assert(RAW_BYTES+KUI_ARMADA_CE_STREAM_STAGE_BYTES+CACHE_BYTES<=KUI_ARMADA_CE_STREAM_SOURCE_BYTES,
    "raw, carry staging and checked head cache must fit the existing source bank");
_Static_assert(COPY_PAGES*4096u>=KUI_ARMADA_CE_STREAM_STAGE_BYTES+4095u,
    "copy witnesses must cover every page of an unaligned refill");
#ifndef KUI_ARMADA_CE_STREAM_PLAYER_TEST
#define KUI_ARMADA_CE_STREAM_PLAYER_TEST 0
#endif
#if KUI_ARMADA_CE_STREAM_PLAYER_TEST != 0 && KUI_ARMADA_CE_STREAM_PLAYER_TEST != 1
#error "The player test seam must be zero or one"
#endif

struct stream_mail {
    uint32_t epoch,command,first,last,repeat;
    uint32_t play_epoch,play_command,play_first,play_last,play_repeat;
    uint32_t stop_epoch;
};
struct stream_player {
    struct kui_armada_ce_excerpt_binding binding;
    struct kui_armada_ce_excerpt_snapshot stats;
    struct kui_armada_ce_stream_native native;
    struct kui_armada_ce_stream_model model;
    struct kui_armada_ce_stream_track tracks[5];
    struct kui_retail_manifest map;
    volatile struct stream_mail mail;
    volatile uint32_t dispatching,booting,worker_started,worker_step,disabled;
    uint32_t callback_info[3],applied_epoch,applied_play_epoch,applied_stop_epoch;
    uint32_t initialized,read_active,read_bus_epoch,device_page,buffer_page,frontier_page;
    uint32_t now_ms,copied_frontier,last_model_result,source_total,cleanup_epoch;
    uint32_t fault[5],mapping_status,mapping_address;
    struct kui_armada_ce_stream_read read;
    uint32_t recovery[5];
    /* Last, so the audited owner-reader offsets into player stay fixed. */
    uint32_t stage_head; /* Offset of the model's first staged byte. */
    uint32_t cache_first,cache_end,cache_bytes;
    uint32_t start_ms,start_cached;
#if KUI_WINCE_CDDA_WORMS || KUI_WINCE_CDDA_WWP
    uint32_t stop_requested_epoch,stop_voice_mask;
#endif
    uint32_t source_pumps_left,source_budget_yielded;
};
static struct stream_player player;
/* Keep the audited offsets within player identical to the proven controller.
 * Disc track inventory is independent of the selected native sound ABI. */
static struct kui_armada_ce_stream_track profile_tracks[KUI_RETAIL_IMAGE_TRACKS];
#define PLAYER_TRACKS profile_tracks

static void publish(void) { __asm__ __volatile__("" : : : "memory"); }
static uint32_t ready_bytes(void) {
    return player.model.produced>=player.model.consumed?
        (uint32_t)(player.model.produced-player.model.consumed):0u;
}
static uint32_t step_delay(void) {
    /* A16-sector job carries213ms of PCM. Keep its native yield short
     * while source work is pending or staging can accept
     * another. A full staged block awaiting ring room keeps the idle rate. */
    bool filling=player.model.state==KUI_ARMADA_CE_STREAM_PREFILL ||
        player.model.state==KUI_ARMADA_CE_STREAM_PLAYING;
    bool eof=!player.model.infinite && player.model.fetch==player.model.finite_end;
    return filling && (player.read_active || player.model.read.id ||
        (player.model.staged<64u && !eof))?SOURCE_STEP_MS:STEP_MS;
}
static uint32_t word(uint32_t address) {
#if KUI_ARMADA_CE_STREAM_PLAYER_TEST
    extern uint32_t kui_armada_ce_stream_player_test_word(uint32_t);
    return kui_armada_ce_stream_player_test_word(address);
#else
    return *(volatile const uint32_t *)(uintptr_t)address;
#endif
}
static uint32_t mask_begin(void) {
#if defined(KUI_ON_CONSOLE)
    uint32_t sr,next;
    __asm__ __volatile__("stc sr,%0" : "=r"(sr));next=sr|0xf0u;
    __asm__ __volatile__("ldc %0,sr" : : "r"(next) : "memory");return sr;
#else
    return 0;
#endif
}
static void mask_end(uint32_t sr) {
#if defined(KUI_ON_CONSOLE)
    __asm__ __volatile__("ldc %0,sr" : : "r"(sr) : "memory");
#else
    (void)sr;
#endif
}
#if KUI_WINCE_CDDA_WWP
/* A single aligned, zero-initialized publication. The low DRIVE adapter
 * reads it without entering this controller or changing CPU state. No
 * override exists until the native owner has actually started playback. */
volatile uint32_t kui_wwp_cdda_drive_status;
static void status_model(void) {
    const struct kui_armada_ce_stream_model *m=&player.model;
    enum kui_armada_ce_stream_state state=m->state==KUI_ARMADA_CE_STREAM_QUIESCING?
        m->after_stop:m->state;
    uint32_t saved=mask_begin();
    if(player.mail.epoch==player.applied_epoch) {
        if(!player.mail.play_epoch) kui_wwp_cdda_drive_status=0u;
        else if(kui_wwp_cdda_drive_status || m->native_running) {
            uint32_t drive=0u;
            if(player.disabled || state==KUI_ARMADA_CE_STREAM_FAULT) drive=9u;
            else if(state==KUI_ARMADA_CE_STREAM_PLAYING ||
                    state==KUI_ARMADA_CE_STREAM_PREFILL) drive=3u;
            else if(state==KUI_ARMADA_CE_STREAM_PAUSED) drive=1u;
            else if(state==KUI_ARMADA_CE_STREAM_ENDED) drive=2u;
            kui_wwp_cdda_drive_status=drive;
        }
    }
    mask_end(saved);
}
static void status_command(uint32_t command) {
    /* The resident serializes accepted controls with IRQs already masked.
     * Preserve that existing path: publish one word without another SR call. */
    if(command==33u) kui_wwp_cdda_drive_status=0u;
    else if(kui_wwp_cdda_drive_status)
        kui_wwp_cdda_drive_status=command==22u?1u:3u;
}
#endif
static void update_priority(bool active) {
    /* Only authored arbitration state changes here. Publish it through the
     * same short integer mask as the worker's transport bridge. */
    uint32_t saved=mask_begin();
    kui_armada_ce_stream_transport_priority(ready_bytes(),active);
    mask_end(saved);
}
static void copy_bytes(void *to,const void *from,uint32_t bytes) {
    volatile uint8_t *dst=to;const volatile uint8_t *src=from;
    /* These are RAM aliases, not registers. SH4 requires aligned word
     * accesses; keep exact byte bounds and the forward carry-copy order.
     * may_alias also permits the same helper on authored byte buffers. */
    typedef uint32_t copy_word __attribute__((may_alias));
    if(!(((uintptr_t)dst|(uintptr_t)src)&3u)) {
        while(bytes>=4u) {
            *(volatile copy_word *)dst=*(const volatile copy_word *)src;
            dst+=4u;src+=4u;bytes-=4u;
        }
    }
    while(bytes--) *dst++=*src++;
}
static bool same_words(const void *a,const void *b,uint32_t bytes) {
    const uint32_t *x=a,*y=b;
    for(uint32_t i=0;i<bytes/4u;i++) if(x[i]!=y[i]) return false;
    return true;
}
static void refuse(uint32_t why) {player.stats.status=why;++player.stats.refused;}
enum stream_fault_reason {
    STREAM_FAULT_SR=1,STREAM_FAULT_FPSCR,STREAM_FAULT_BINDING,
    STREAM_FAULT_CURRENT,STREAM_FAULT_SLOT,STREAM_FAULT_HANDLE,
    STREAM_FAULT_DEVICE,STREAM_FAULT_BUFFER,STREAM_FAULT_METHOD,STREAM_FAULT_TICKS
};
static bool fault(uint32_t why,uint32_t address,uint32_t observed,uint32_t expected,uint32_t detail) {
    /* Keep the first failure. Later cleanup guards and rejected commands must
     * not hide the identity or caller condition that stopped the stream. */
    if(!player.fault[0]) {
        player.fault[1]=address;player.fault[2]=observed;
        player.fault[3]=expected;player.fault[4]=detail;publish();player.fault[0]=why;
    }
    return false;
}
static uint32_t call(uint32_t target,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3) {
    uint32_t args[8]={a0,a1,a2,a3,0,0,0,0};
    return kui_armada_ce_excerpt_ce_call(target,args);
}
static bool binding_valid(const struct kui_armada_ce_excerpt_binding *b) {
    if(!b || b->owner_process<KUI_ARMADA_CE_LIVE_PROCESS_BASE || b->owner_process>=(KUI_ARMADA_CE_LIVE_PROCESS_BASE+32u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE) ||
       (b->owner_process-KUI_ARMADA_CE_LIVE_PROCESS_BASE)%KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       b->owner_slot<0x02000000u || b->owner_slot>0x40000000u || (b->owner_slot&0x01ffffffu)) return false;
    const uint32_t *words=(const uint32_t *)b;
    for(unsigned i=2;i<sizeof(*b)/4u;i++) if(!words[i] || (words[i]&1u)) return false;
    return true;
}
static bool pointer(uint32_t address,uint32_t bytes,uint32_t *explicit_address) {
    uint32_t slot=address&0xfe000000u;
    if(address<0x10000u || address>=0x80000000u || (address&3u) || !bytes ||
       bytes>0x80000000u-address || (slot && slot!=player.binding.owner_slot) ||
       bytes>0x02000000u-(address&0x01ffffffu)) return false;
    *explicit_address=slot?address:address|player.binding.owner_slot;return true;
}
#if !KUI_ARMADA_CE_STREAM_PLAYER_TEST
static bool read_ram(void *unused,uint32_t address,uint32_t *value) {
    uint32_t p1;(void)unused;
    if(!value || !kui_armada_ce_ram_table_p1(address,&p1)) return false;
    *value=word(p1);return true;
}
static bool __attribute__((noinline,noclone)) read_native(void *unused,uint32_t address,uint32_t *value) {
    uint32_t explicit_address;(void)unused;
    if(!value || !pointer(address,4u,&explicit_address) || explicit_address!=address ||
       word(CURRENT_PROCESS)!=player.binding.owner_process) return false;
    *value=word(address);return true;
}
#endif
static bool __attribute__((noinline,noclone)) mapped_word(uint32_t address,struct kui_armada_ce_ram_binding *out) {
    uint32_t explicit_address;
    player.mapping_address=address;
    player.mapping_status=KUI_ARMADA_CE_STREAM_OWNER_ARGUMENT;
    if(out) out->physical_address=out->value=out->page_word=0;
    if(!out || !pointer(address,4u,&explicit_address)) return false;
#if KUI_ARMADA_CE_STREAM_PLAYER_TEST
    extern bool kui_armada_ce_stream_player_test_mapped(uint32_t,struct kui_armada_ce_ram_binding *);
    bool valid=kui_armada_ce_stream_player_test_mapped(explicit_address,out);
    player.mapping_status=valid?KUI_ARMADA_CE_STREAM_OWNER_OK:KUI_ARMADA_CE_STREAM_OWNER_CHANGED;
    return valid;
#else
    /* This is the proven native owner thread. Use its virtual memory domain
     * for values, with physical pages retained only as identity witnesses.
     * IRQs remain masked across both software walks and the native sample. */
    uint32_t saved=mask_begin(),sr;
    __asm__ __volatile__("stc sr,%0" : "=r"(sr));
    uint32_t ttb=word(0xff000008u);
    enum kui_armada_ce_stream_owner_status status=kui_armada_ce_stream_owner_word(
        ttb,sr,player.binding.owner_process,player.binding.owner_slot,
        player.native.api.owner_handle,explicit_address,read_ram,read_native,NULL,out);
    uint32_t after;
    __asm__ __volatile__("stc sr,%0" : "=r"(after));
    if(word(0xff000008u)!=ttb || !kui_armada_ce_stream_owner_sr_stable(sr,after)) {
        out->physical_address=out->value=out->page_word=0;
        status=KUI_ARMADA_CE_STREAM_OWNER_CHANGED;
    }
    player.mapping_status=(uint32_t)status;
    mask_end(saved);
    return status==KUI_ARMADA_CE_STREAM_OWNER_OK;
#endif
}
static bool object(uint32_t address,uint32_t vtable,uint32_t *page,uint32_t why) {
    struct kui_armada_ce_ram_binding mapped;
    if(!mapped_word(address,&mapped))
        return fault(why,address,0u,vtable,player.mapping_status);
    uint32_t actual=mapped.value;
    if(actual<0x02000000u) actual|=player.binding.owner_slot;
    if(actual!=vtable) return fault(why,address,actual,vtable,mapped.page_word);
    if(*page && *page!=mapped.page_word)
        return fault(why,address,mapped.page_word,*page,mapped.page_word);
    *page=mapped.page_word;return true;
}
/* Own native references keep the objects alive. Every step still verifies
 * process/handle, object PFNs and the exact published method words. A control
 * generation additionally repeats the full kernel/module/code binder. */
static bool guard(void) {
    if(!binding_valid(&player.binding))
        return fault(STREAM_FAULT_BINDING,0u,0u,0u,0u);
    uint32_t observed=word(CURRENT_PROCESS);
    if(observed!=player.binding.owner_process)
        return fault(STREAM_FAULT_CURRENT,CURRENT_PROCESS,observed,player.binding.owner_process,0u);
    observed=word(player.binding.owner_process+12u);
    if(observed!=player.binding.owner_slot)
        return fault(STREAM_FAULT_SLOT,player.binding.owner_process+12u,observed,player.binding.owner_slot,0u);
    observed=word(player.binding.owner_process+8u);
    if(observed!=player.native.api.owner_handle)
        return fault(STREAM_FAULT_HANDLE,player.binding.owner_process+8u,observed,player.native.api.owner_handle,0u);
    if(!object(player.binding.device,player.binding.device_vtable,&player.device_page,STREAM_FAULT_DEVICE)) return false;
    if(player.stats.buffer && !object(player.stats.buffer,player.binding.buffer_vtable,&player.buffer_page,STREAM_FAULT_BUFFER)) return false;
    static const uint32_t offset[9]={8u,12u,44u,76u,48u,72u,36u,16u,52u};
    uint32_t expected[9]={player.binding.buffer_release,player.binding.get_caps,
        player.binding.lock,player.binding.unlock,player.binding.play,player.binding.stop,
        player.binding.get_status,player.binding.get_position,player.native.api.buffer_set_position};
    for(unsigned i=0;i<9u;i++) {
        struct kui_armada_ce_ram_binding mapped;
        uint32_t address=player.binding.buffer_vtable+offset[i];
        if(!mapped_word(address,&mapped))
            return fault(STREAM_FAULT_METHOD,address,0u,expected[i],player.mapping_status);
        uint32_t actual=mapped.value;
        if(actual<0x02000000u) actual|=player.binding.owner_slot;
        if(actual!=expected[i])
            return fault(STREAM_FAULT_METHOD,address,actual,expected[i],mapped.page_word);
    }
    return true;
}
static void capture_mail(struct stream_mail *out) {
    uint32_t sr=mask_begin();copy_bytes(out,(const void *)&player.mail,sizeof(*out));mask_end(sr);
}
static bool mail_current(void) {return player.mail.epoch==player.applied_epoch;}
int kui_armada_ce_stream_needs_deferred(void) {
    return player.mail.play_epoch && !player.worker_started && !player.dispatching && !player.disabled;
}
void kui_armada_ce_stream_progress(uint32_t out[3]) {
    if(!out) return;
    /* Cumulative admitted source bytes survive STOP/INIT and selection changes.
     * This diagnostic intentionally wraps at32bits; no read/native API occurs. */
    out[0]=player.source_total;out[1]=player.model.recoveries;out[2]=player.native.status;
}
void kui_armada_ce_stream_fault(uint32_t out[5]) {
    if(!out) return;
    /* A previous recoverable cursor event must not hide a later failed
     * binder admission. Its precise fatal reason remains in the entry. */
    if(player.fault[0]) copy_bytes(out,player.fault,sizeof(player.fault));
    else if(player.stats.status==KUI_ARMADA_CE_EXCERPT_IDENTITY)
        for(unsigned i=0;i<5u;i++) out[i]=0;
    else copy_bytes(out,player.recovery,sizeof(player.recovery));
}
int kui_armada_ce_excerpt_queue(const struct kui_armada_ce_excerpt_binding *unused,
    uint32_t command,uint32_t first,uint32_t last,uint32_t repeat) {
    (void)unused;++player.stats.requests;
    if(player.disabled) {++player.stats.refused;return 0;}
    if(command!=20u && command!=21u && command!=22u && command!=23u && command!=33u) {
        refuse(KUI_ARMADA_CE_EXCERPT_ARGUMENT);return 0;
    }
    if(player.mail.epoch==UINT32_MAX) {player.disabled=1u;refuse(KUI_ARMADA_CE_EXCERPT_ARGUMENT);return 0;}
    uint32_t epoch=player.mail.epoch+1u;
    player.mail.command=command;player.mail.first=first;player.mail.last=last;player.mail.repeat=repeat;
    if(command==20u || command==21u) {
        player.mail.play_command=command;player.mail.play_first=first;
        player.mail.play_last=last;player.mail.play_repeat=repeat;player.mail.play_epoch=epoch;
    } else if(command==33u) {player.mail.play_epoch=0;player.mail.stop_epoch=epoch;}
    player.stats.command=command;player.stats.first=first;player.stats.last=last;player.stats.repeat=repeat;
    player.stats.status=KUI_ARMADA_CE_EXCERPT_QUEUED;
    publish();player.mail.epoch=epoch;
#if KUI_WINCE_CDDA_WWP
    status_command(command);
#endif
    return kui_armada_ce_stream_needs_deferred();
}
void kui_armada_ce_excerpt_snapshot(struct kui_armada_ce_excerpt_snapshot *out) {
    if(out) copy_bytes(out,&player.stats,sizeof(*out));
}
uint32_t kui_armada_ce_excerpt_owner_callback(uint32_t token) {
    if(!player.dispatching || token!=CALLBACK_TOKEN || word(CURRENT_PROCESS)!=player.binding.owner_process) return 0;
    if(!kui_armada_ce_stream_admit_owner(&player.binding) || !binding_valid(&player.binding)) return 0;
    kui_armada_ce_excerpt_owner_deferred(0);return CALLBACK_DONE;
}
void kui_armada_ce_excerpt_dispatch(uint32_t sr,uint32_t stack) {
    if(!kui_armada_ce_stream_needs_deferred()) return;
    player.dispatching=1u;publish();
    if((sr&SR_CHECK)!=SR_REQUIRED || (stack&3u) ||
       !((stack>=0x02000000u && stack<0x80000000u) || (stack>=0x8c000000u && stack<0x8d000000u))) goto failed;
    /* Discovery publishes only the stable process/slot. The pinned kernel
     * callback enters that owner before DLL values or sound APIs are read. */
    if(!kui_armada_ce_stream_bind_deferred(&player.binding) ||
       player.binding.owner_process<KUI_ARMADA_CE_LIVE_PROCESS_BASE || player.binding.owner_process>=(KUI_ARMADA_CE_LIVE_PROCESS_BASE+32u*KUI_ARMADA_CE_LIVE_PROCESS_STRIDE) ||
       (player.binding.owner_process-KUI_ARMADA_CE_LIVE_PROCESS_BASE)%KUI_ARMADA_CE_LIVE_PROCESS_STRIDE ||
       player.binding.owner_slot!=((1u+(player.binding.owner_process-KUI_ARMADA_CE_LIVE_PROCESS_BASE)/KUI_ARMADA_CE_LIVE_PROCESS_STRIDE)<<25) ||
       word(KUI_ARMADA_CE_LIVE_APISETS)!=KUI_ARMADA_CE_LIVE_WIN32_RECORD ||
       word(KUI_ARMADA_CE_LIVE_WIN32_RECORD)!=0x32336e57u ||
       word(KUI_ARMADA_CE_LIVE_WIN32_RECORD+4u)!=0x00900003u ||
       word(KUI_ARMADA_CE_LIVE_WIN32_RECORD+8u)!=KUI_ARMADA_CE_LIVE_WIN32_METHODS ||
       word(KUI_ARMADA_CE_LIVE_CALLBACK_SENTINEL)!=UINT32_MAX) goto failed;
    uint32_t handle=word(player.binding.owner_process+8u);
    if(!handle || handle!=word(player.binding.owner_process+8u)) goto failed;
    player.callback_info[0]=handle;
    player.callback_info[1]=(uint32_t)(uintptr_t)kui_armada_ce_excerpt_owner_callback;
    player.callback_info[2]=CALLBACK_TOKEN;publish();
    uint32_t args[8]={(uint32_t)(uintptr_t)player.callback_info,0,0,0,0,0,0,0};
    if(kui_armada_ce_excerpt_ce_call(OWNER_CALLBACK,args)==CALLBACK_DONE && player.worker_started) {
        publish();player.dispatching=0u;return;
    }
failed:
    player.stats.caller_sr=sr;player.stats.current_process=word(CURRENT_PROCESS);
    refuse(KUI_ARMADA_CE_EXCERPT_CALLBACK);publish();player.dispatching=0u;
}
static bool initialize(void) {
    const uint32_t *seed=(const uint32_t *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_POOL_BEGIN;
    const uint8_t *wire=(const uint8_t *)(uintptr_t)KUI_ARMADA_CE_STREAM_MAP_BEGIN;
    if(word(KUI_ARMADA_CE_EXCERPT_RAM_END_WORD)!=KUI_ARMADA_CE_EXCERPT_POOL_BEGIN ||
       !kui_armada_ce_stream_seed_header_valid(seed) ||
       seed[KUI_ARMADA_CE_STREAM_MAP_CRC_WORD]!=kui_armada_ce_excerpt_le32(wire+16u) ||
       kui_retail_manifest_decode(wire,&player.map)!=KUI_GAME_OK ||
       !kui_armada_ce_stream_map_valid(&player.map)) return false;
    for(unsigned i=0;i<player.map.track_count;i++) {
        const struct kui_retail_track *t=&player.map.slots[i].track;
        PLAYER_TRACKS[i]=(struct kui_armada_ce_stream_track){i+1u,kui_retail_track_control(t),t->start_lba,t->end_lba};
    }
    if(kui_armada_ce_stream_init(&player.model,PLAYER_TRACKS,player.map.track_count)!=KUI_ARMADA_CE_STREAM_OK ||
       !kui_armada_ce_stream_transport_init(&player.map,RAW_BEGIN,
           KUI_ARMADA_CE_STREAM_SOURCE_BEGIN+KUI_ARMADA_CE_STREAM_SOURCE_BYTES)) return false;
    player.stats.pcm_crc32=seed[KUI_ARMADA_CE_STREAM_MAP_CRC_WORD];player.initialized=1u;return true;
}
static bool fresh_identity(void) {
    struct kui_armada_ce_live_stream_api fresh;
    kui_armada_ce_excerpt_revalidate_fn validate=(kui_armada_ce_excerpt_revalidate_fn)
        (uintptr_t)player.binding.revalidate;
    if(!binding_valid(&player.binding)) return false;
    bool valid=player.worker_started?
        kui_armada_ce_stream_rebind_owner(&player.binding,player.native.api.owner_handle,&fresh):
        (validate(&player.binding) && kui_armada_ce_stream_bind_native(&fresh));
    if(!valid ||
       (player.worker_started && !same_words(&fresh,&player.native.api,sizeof(fresh)))) return false;
    if(!player.worker_started) player.native.api=fresh;
    return true;
}
static bool create_buffer(void) {
    uint32_t wave[5]={0x00020001u,44100u,176400u,0x00100004u,0};
    uint32_t desc[5]={20u,BUFFER_SOFTWARE,KUI_ARMADA_CE_STREAM_RING_BYTES,0u,(uint32_t)(uintptr_t)wave};
    uint32_t buffer=0,caps[5]={20u,0,0,0,0};
    if(!player.stats.device_reference) {
        uint32_t refs=call(player.binding.add_ref,player.binding.device,0,0,0);
        player.stats.device_reference=1u;
        if(refs<2u || refs>0x100000u) {refuse(KUI_ARMADA_CE_EXCERPT_REFERENCE);return false;}
    }
    player.stats.last_hresult=call(player.binding.create_buffer,player.binding.device,
        (uint32_t)(uintptr_t)desc,(uint32_t)(uintptr_t)&buffer,0);
    if(player.stats.last_hresult || !object(buffer,player.binding.buffer_vtable,&player.buffer_page,STREAM_FAULT_BUFFER)) {
        refuse(KUI_ARMADA_CE_EXCERPT_BUFFER);return false;
    }
    player.stats.buffer=buffer;
    player.stats.last_hresult=call(player.binding.get_caps,buffer,(uint32_t)(uintptr_t)caps,0,0);
    player.stats.caps_flags=caps[1];player.stats.caps_bytes=caps[2];
    if(player.stats.last_hresult || caps[0]!=20u || !(caps[1]&8u) || (caps[1]&4u) ||
       caps[2]!=KUI_ARMADA_CE_STREAM_RING_BYTES) {refuse(KUI_ARMADA_CE_EXCERPT_CAPS);return false;}
    return true;
}
static bool drain_cancelled_read(void) {
    if(!player.cleanup_epoch) return true;
    /* An ambiguous native consumer lease forbids even integer storage work.
     * The existing transport IRQ can fence a revoked reception independently. */
    if(player.native.consumer_cs) return false;
    struct kui_armada_ce_stream_transport_snapshot progress={0};
    uint32_t sr=mask_begin();
    (void)kui_armada_ce_stream_transport_pump();
    enum kui_armada_ce_stream_transport_status status=
        kui_armada_ce_stream_transport_snapshot(player.cleanup_epoch,&progress);
    if(status==KUI_ARMADA_CE_STREAM_TRANSPORT_STALE ||
       (status!=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING && progress.owner!=2u))
        player.cleanup_epoch=0u;
    mask_end(sr);return !player.cleanup_epoch;
}
static void cancel_read(void) {
    update_priority(false);
    if(player.read_active) {
        uint32_t sr=mask_begin();
        player.cleanup_epoch=player.read_bus_epoch;
        (void)kui_armada_ce_stream_transport_cancel(player.cleanup_epoch);
        player.read_active=0u;mask_end(sr);
    }
    if(!player.native.consumer_cs) (void)drain_cancelled_read();
}
static bool clock_now(uint32_t *now) {
    if(kui_armada_ce_stream_native_ticks(&player.native,NULL,now)) return true;
    (void)fault(STREAM_FAULT_TICKS,0u,player.native.status,0u,player.native.last_result);
    refuse(KUI_ARMADA_CE_EXCERPT_CONTEXT);return false;
}
static void trace_observation(uint32_t now,uint32_t ready,uint32_t delta,uint32_t staged,
                              uint32_t state,bool running,bool underrun) {
    struct kui_armada_ce_stream_transport_snapshot progress={0};
    struct kui_armada_ce_stream_trace_supply supply={0};
    uint32_t saved=mask_begin();
    enum kui_armada_ce_stream_transport_status status=KUI_ARMADA_CE_STREAM_TRANSPORT_STALE;
    if(player.read_active) status=
        kui_armada_ce_stream_transport_snapshot(player.read_bus_epoch,&progress);
    bool active=status!=KUI_ARMADA_CE_STREAM_TRANSPORT_STALE;
    if(active) {
        supply=(struct kui_armada_ce_stream_trace_supply){(uint32_t)status,progress.owner,
            progress.bytes,progress.total,progress.retries,progress.data_blocks,
            progress.audio_blocks,progress.preemptions};
    }
    uint32_t flags=(active?1u:0u)|(running?2u:0u)|
        (kui_armada_ce_stream_transport_urgent()?4u:0u)|(state<<8);
    /* This is an authored-state snapshot, not a transport pump, bus query,
     * guest read or native call. Keep it outside the native consumer lease. */
    kui_armada_ce_stream_trace_observe(now,player.source_total,ready,delta,staged,
        flags,active?&supply:NULL,underrun);
    mask_end(saved);
}
static bool observe(void) {
    if(!player.model.native_running) return true;
#if KUI_WINCE_CDDA_WORMS || KUI_WINCE_CDDA_WWP
    if(player.stop_requested_epoch && player.model.stop_needed) return true;
#endif
    uint32_t positions[2]={0,0};
    player.stats.last_hresult=call(player.binding.get_position,player.stats.buffer,
        (uint32_t)(uintptr_t)&positions[0],(uint32_t)(uintptr_t)&positions[1],0);
    if(player.stats.last_hresult) {refuse(KUI_ARMADA_CE_EXCERPT_PLAY);return false;}
    /* GetCurrentPosition may wait for native sound locks. Timestamp its
     * returned cursor, not the earlier worker entry before guards/calls. */
    uint32_t now;
    if(!clock_now(&now)) return false;
    player.now_ms=now;
    if(!kui_armada_ce_stream_native_write_begin(&player.native,NULL,player.binding.device)) return false;
    struct kui_armada_ce_ram_binding frontier;
    uint32_t observed_ready=0u,observed_delta=0u,observed_staged=0u;
    uint32_t observed_state=(uint32_t)player.model.state;
    bool observed=false,observed_running=player.model.native_running;
    bool valid=mapped_word(player.stats.buffer+116u,&frontier) &&
        (!player.frontier_page || frontier.page_word==player.frontier_page);
    if(valid) {
#if KUI_WINCE_CDDA_WORMS
        /* The wrapped native transfer briefly retains exactlyR between its
         * tail and head pieces. Other out-of-range values remain invalid. */
        if(frontier.value==KUI_ARMADA_CE_STREAM_RING_BYTES) frontier.value=0u;
        if(positions[0]==KUI_ARMADA_CE_STREAM_RING_BYTES) positions[0]=0u;
#endif
        player.frontier_page=frontier.page_word;
        player.copied_frontier=frontier.value;
        uint32_t elapsed=now-player.model.last_ms;
        uint32_t delta=positions[0]>=player.model.last_play?positions[0]-player.model.last_play:
            KUI_ARMADA_CE_STREAM_RING_BYTES-player.model.last_play+positions[0];
        uint32_t allowance=kui_armada_ce_stream_cursor_allowance(&player.model,now);
        uint32_t ready=ready_bytes();
        observed_ready=ready;observed_delta=delta;observed_staged=player.model.staged;observed=true;
        player.last_model_result=kui_armada_ce_stream_observe(&player.model,player.model.ring_epoch,
            positions[0],frontier.value,now);
        if(player.last_model_result==KUI_ARMADA_CE_STREAM_UNDERRUN)
            kui_armada_ce_stream_trace_event(KUI_ARMADA_CE_STREAM_TRACE_UNDERRUN,
                now,player.source_total,ready,elapsed);
        if(player.last_model_result>=KUI_ARMADA_CE_STREAM_CURSOR &&
           player.last_model_result<=KUI_ARMADA_CE_STREAM_UNDERRUN) {
            player.recovery[1]=positions[0];player.recovery[2]=delta;
            player.recovery[3]=allowance;
            player.recovery[4]=elapsed;publish();
            player.recovery[0]=11u+player.last_model_result;
        }
        player.stats.play_cursor=positions[0];player.stats.write_cursor=positions[1];++player.stats.position_ok;
    }
    bool released=kui_armada_ce_stream_native_write_end(&player.native,NULL);
    if(observed && released) trace_observation(now,observed_ready,observed_delta,observed_staged,
        observed_state,observed_running,player.last_model_result==KUI_ARMADA_CE_STREAM_UNDERRUN);
    return valid && released;
}
static bool control(void) {
    struct stream_mail mail;capture_mail(&mail);
    if(mail.epoch==player.applied_epoch) return true;
    if(!fresh_identity() || !guard()) {refuse(KUI_ARMADA_CE_EXCERPT_IDENTITY);return false;}
    ++player.stats.attempts;uint32_t previous_model_epoch=player.model.epoch;
    if(mail.stop_epoch!=player.applied_stop_epoch) {
        player.last_model_result=kui_armada_ce_stream_request(&player.model,33u,0,0,0);
        if(player.last_model_result!=KUI_ARMADA_CE_STREAM_OK) return false;
        player.applied_stop_epoch=mail.stop_epoch;player.applied_play_epoch=0;
    }
    if(mail.play_epoch && mail.play_epoch!=player.applied_play_epoch) {
        player.last_model_result=kui_armada_ce_stream_request(&player.model,
            mail.play_command,mail.play_first,mail.play_last,mail.play_repeat);
        if(player.last_model_result!=KUI_ARMADA_CE_STREAM_OK) {
            if(player.model.epoch!=previous_model_epoch) cancel_read();
            refuse(KUI_ARMADA_CE_EXCERPT_ARGUMENT);player.applied_epoch=mail.epoch;return true;
        }
        player.applied_play_epoch=mail.play_epoch;
    }
    /* STOP clears the mailbox's selection intent immediately. Native Stop
     * may still be pending in the model, whose old selection is retained only
     * until that lease quiesces. A rapid RELEASE must not revive that lease. */
    if(mail.play_epoch && mail.command!=20u && mail.command!=21u && mail.command!=33u) {
        player.last_model_result=kui_armada_ce_stream_request(&player.model,mail.command,0,0,0);
        if(player.last_model_result!=KUI_ARMADA_CE_STREAM_OK) {
            if(player.model.epoch!=previous_model_epoch) cancel_read();
            refuse(KUI_ARMADA_CE_EXCERPT_ARGUMENT);player.applied_epoch=mail.epoch;return true;
        }
    }
    if(player.model.epoch!=previous_model_epoch) {
        cancel_read();player.start_ms=player.now_ms;player.start_cached=0u;
    }
    publish();player.applied_epoch=mail.epoch;return true;
}
#if KUI_WINCE_CDDA_WORMS || KUI_WINCE_CDDA_WWP
/* Device+112 is the callback's active-port mask (decimal byte offset), not
 * the allocation mask at+108. CS+88 stabilizes app-side allocation; the IRQ
 * mask makes this RAM-only snapshot indivisible with the older callback.
 * World Party instead has its active software mask at+132 and a real
 * consumer CS at+108. Its native lock order is allocation+88 then consumer
 * +108; both are released before Stop/GetStatus or any worker yield. */
static bool worms_stop_voices(uint32_t *owned,uint32_t *pending) {
    (void)call(player.binding.enter_cs,player.binding.device+DEVICE_CS_OFFSET,0,0,0);
#if KUI_WINCE_CDDA_WWP
    (void)call(player.binding.enter_cs,player.binding.device+108u,0,0,0);
#endif
    uint32_t own=0u,possible=0u;bool valid=true;uint32_t saved=mask_begin();
    struct kui_armada_ce_ram_binding active;
#if KUI_WINCE_CDDA_WWP
    if(!mapped_word(player.binding.device+132u,&active)) valid=false;
#else
    if(!mapped_word(player.binding.device+112u,&active)) valid=false;
#endif
    for(unsigned i=0;valid && i<DEVICE_VOICES;i++) {
        struct kui_armada_ce_ram_binding owner;
        if(!mapped_word(player.binding.device+DEVICE_OWNER_OFFSET+i*4u,&owner)) {valid=false;break;}
        uint32_t bit=1u<<i;
        bool ours=owner.value==player.stats.buffer ||
            owner.value==(player.stats.buffer&0x01ffffffu);
        if(ours) own|=bit;
        /* A different nonzero owner proves a saved voice bit was reused.
         * Zero without a callback acknowledgment remains conservative. */
        if(ours || !owner.value) possible|=bit;
    }
    *owned=own;
    *pending=valid?active.value&(own|(player.stop_voice_mask&possible)):0u;
    mask_end(saved);
#if KUI_WINCE_CDDA_WWP
    (void)call(player.binding.leave_cs,player.binding.device+108u,0,0,0);
#endif
    (void)call(player.binding.leave_cs,player.binding.device+DEVICE_CS_OFFSET,0,0,0);
    return valid;
}
#endif
static bool stopped(void) {
    if(!player.model.stop_needed) return true;
    cancel_read();
#if KUI_WINCE_CDDA_WORMS || KUI_WINCE_CDDA_WWP
    uint32_t owned=0u,pending=0u;
    if(!worms_stop_voices(&owned,&pending)) {refuse(KUI_ARMADA_CE_EXCERPT_COPY);return false;}
    player.stop_voice_mask|=owned;
    if(player.stop_requested_epoch!=player.model.epoch) {
        player.stats.last_hresult=call(player.binding.stop,player.stats.buffer,0,0,0);
        if(player.stats.last_hresult) {refuse(KUI_ARMADA_CE_EXCERPT_NATIVE_STOP);return false;}
        player.stop_requested_epoch=player.model.epoch;
    }
    /* Stop requests cleanup. Callback1c58 clears active+112 only after its
     * final host read; it deliberately retains the owner-table entries.
     * GetStatus2980 then sweeps those finished owners through1d44/42b0.
     * World Party's callback49c4 clears active+132 after its final host read;
     * GetStatus2e98 sweeps through1ff0/47a0/48a4/20a4. A stereo sweep may need
     * another worker visit. Its Stop return/status bits alone prove neither
     * the active-mask acknowledgment nor release of its owner entries.
     * Both native calls stay outside the IRQ mask and copy/consumer lease. */
    if(pending) return true;
    uint32_t native_status=0u;
    player.stats.last_hresult=call(player.binding.get_status,player.stats.buffer,
        (uint32_t)(uintptr_t)&native_status,0,0);
    if(player.stats.last_hresult) {refuse(KUI_ARMADA_CE_EXCERPT_NATIVE_STOP);return false;}
    if(!worms_stop_voices(&owned,&pending)) {refuse(KUI_ARMADA_CE_EXCERPT_COPY);return false;}
    if(owned || pending) return true;
    player.stop_requested_epoch=player.stop_voice_mask=0u;bool ok=true;
#else
    player.stats.last_hresult=call(player.binding.stop,player.stats.buffer,0,0,0);
    bool ok=player.stats.last_hresult==0;
#endif
    player.last_model_result=kui_armada_ce_stream_stop_complete(&player.model,player.model.epoch,ok);
    if(!ok) {refuse(KUI_ARMADA_CE_EXCERPT_NATIVE_STOP);return false;}
    if(player.last_model_result==KUI_ARMADA_CE_STREAM_OK &&
       player.model.state==KUI_ARMADA_CE_STREAM_PREFILL) {
        player.start_ms=player.now_ms;player.start_cached=0u;
    }
    ++player.stats.stop_ok;return player.last_model_result==KUI_ARMADA_CE_STREAM_OK;
}
#if KUI_WINCE_CDDA_WORMS
/* Exact software PCM16 stereo path: constructor flags8 gives a separate
 * host ring; native deinterleave copies into scratch before queuing DMA.
 * A single invocation can still hold the source slice beginning at+116.
 * ceil64(ceil32(176400/16)) bounds that slice, including its wrapped head. */
#define WORMS_HOST_SLICE_BYTES 11072u
enum worms_copy_result { WORMS_COPY_OK,WORMS_COPY_RETRY,WORMS_COPY_FAULT };
static bool worms_arc_overlap(uint32_t offset,uint32_t bytes,uint32_t first,uint32_t end) {
    if(first==end) return false;
    return first<end?(offset<end && first<offset+bytes):
        (offset<end || first<offset+bytes);
}
/* Caller masks IRQs. Only mapped RAM/model/mail observations occur here. */
static enum worms_copy_result worms_copy_frontier(
        const struct kui_armada_ce_stream_refill *ticket,uint32_t offset,uint32_t bytes) {
    struct kui_armada_ce_ram_binding frontier;
    if(!mail_current()) return WORMS_COPY_RETRY;
    if(!mapped_word(player.stats.buffer+116u,&frontier) ||
       (player.frontier_page && frontier.page_word!=player.frontier_page)) return WORMS_COPY_FAULT;
    if(frontier.value==KUI_ARMADA_CE_STREAM_RING_BYTES) frontier.value=0u;
    if(frontier.value>=KUI_ARMADA_CE_STREAM_RING_BYTES || (frontier.value&3u))
        return WORMS_COPY_FAULT;
    player.frontier_page=frontier.page_word;
    if(player.model.native_running) {
        player.model.write_cursor=player.copied_frontier=frontier.value;
        uint32_t end=frontier.value+WORMS_HOST_SLICE_BYTES;
        if(end>=KUI_ARMADA_CE_STREAM_RING_BYTES) end-=KUI_ARMADA_CE_STREAM_RING_BYTES;
        if(bytes && worms_arc_overlap(offset,bytes,frontier.value,end)) return WORMS_COPY_RETRY;
    }
    /* Revalidate the whole ticket against the refreshed play->frontier arc.
     * A consumer that reaches an earlier copied prefix revokes its commit. */
    return kui_armada_ce_stream_refill_validate(&player.model,ticket)==KUI_ARMADA_CE_STREAM_OK?
        WORMS_COPY_OK:WORMS_COPY_RETRY;
}
static enum worms_copy_result worms_copy_ticket(
        const struct kui_armada_ce_stream_refill *ticket,uint32_t target,
        const struct kui_armada_ce_ram_binding *witness,const uint32_t *addresses,uint32_t pages) {
    if(target&63u) return WORMS_COPY_FAULT;
    for(uint32_t at=0;at<ticket->bytes;at+=64u) {
        uint32_t saved=mask_begin();
        enum worms_copy_result result=worms_copy_frontier(ticket,ticket->ring_offset+at,64u);
        uint32_t page=(target+at)&~0xfffu,index=0u;
        while(index<pages && addresses[index]!=page) ++index;
        struct kui_armada_ce_ram_binding current;
        if(result==WORMS_COPY_OK && (index==pages || !mapped_word(page,&current) ||
           current.page_word!=witness[index].page_word ||
           current.physical_address!=witness[index].physical_address)) result=WORMS_COPY_FAULT;
        if(result==WORMS_COPY_OK) {
            volatile uint8_t *to=(volatile uint8_t *)(uintptr_t)(target+at);
            const volatile uint8_t *from=(const volatile uint8_t *)(uintptr_t)(STAGE_BEGIN+player.stage_head+at);
            if(at+64u<=ticket->audio_bytes)
                copy_bytes((void *)to,(const void *)from,64u);
            else for(uint32_t i=0;i<64u;i++) to[i]=at+i<ticket->audio_bytes?from[i]:0u;
        }
        mask_end(saved);
        if(result!=WORMS_COPY_OK) return result;
#if KUI_ARMADA_CE_STREAM_PLAYER_TEST
        extern void kui_armada_ce_stream_player_test_copy_chunk(uint32_t);
        kui_armada_ce_stream_player_test_copy_chunk(at+64u);
#endif
    }
    return WORMS_COPY_OK;
}
#endif
static bool refill(void) {
    if(!mail_current()) return true;
    /* Checked prefixes can refill while the rest of the source job runs.
     * A sub-block carry cannot refill until source EOF permits padding. */
    if(player.model.staged<64u && (player.model.read.id ||
       player.model.infinite || player.model.fetch!=player.model.finite_end)) return true;
    if(!kui_armada_ce_stream_native_write_begin(&player.native,NULL,player.binding.device)) return false;
    struct kui_armada_ce_stream_refill ticket;
    struct kui_armada_ce_ram_binding frontier;
    bool ok=mapped_word(player.stats.buffer+116u,&frontier) &&
        (!player.frontier_page || frontier.page_word==player.frontier_page);
    if(ok) player.frontier_page=frontier.page_word;
#if KUI_WINCE_CDDA_WORMS
    if(ok && frontier.value==KUI_ARMADA_CE_STREAM_RING_BYTES) frontier.value=0u;
#endif
    if(ok && player.model.native_running) {
        /* Armada/BAM4 hold the consumer's108 lock. Worms repeats this
         * frontier under its per-chunk mask before writing any host bytes.
         * DirectSound's public write cursor is not an exclusion boundary. */
        ok=frontier.value<KUI_ARMADA_CE_STREAM_RING_BYTES && !(frontier.value&3u);
        if(ok) player.model.write_cursor=player.copied_frontier=frontier.value;
    }
    enum kui_armada_ce_stream_result planned=ok?
        kui_armada_ce_stream_refill_plan(&player.model,KUI_ARMADA_CE_STREAM_STAGE_BYTES,&ticket):
        KUI_ARMADA_CE_STREAM_CURSOR;
    if(planned==KUI_ARMADA_CE_STREAM_NOTHING || planned==KUI_ARMADA_CE_STREAM_STATE ||
       planned==KUI_ARMADA_CE_STREAM_BUSY) {
        return kui_armada_ce_stream_native_write_end(&player.native,NULL);
    }
    if(planned!=KUI_ARMADA_CE_STREAM_OK || !mail_current()) ok=false;
    uint32_t regions[4]={0,0,0,0};bool locked=false;
#if KUI_WINCE_CDDA_WORMS
    bool abandoned=false;
#endif
    if(ok) {
        uint32_t args[8]={player.stats.buffer,ticket.ring_offset,ticket.bytes,
            (uint32_t)(uintptr_t)&regions[0],(uint32_t)(uintptr_t)&regions[1],
            (uint32_t)(uintptr_t)&regions[2],(uint32_t)(uintptr_t)&regions[3],0};
        player.stats.last_hresult=kui_armada_ce_excerpt_ce_call(player.binding.lock,args);
        locked=player.stats.last_hresult==0;
        uint32_t explicit_address;
        ok=locked && regions[1]==ticket.bytes && !regions[2] && !regions[3] &&
            pointer(regions[0],regions[1],&explicit_address) &&
            kui_armada_ce_stream_refill_validate(&player.model,&ticket)==KUI_ARMADA_CE_STREAM_OK;
        struct kui_armada_ce_ram_binding witness[COPY_PAGES];uint32_t addresses[COPY_PAGES],pages=0;
        if(ok) {
            uint32_t last=explicit_address+ticket.bytes-1u;
            for(uint32_t at=explicit_address&~0xfffu;at<=(last&~0xfffu);at+=4096u) {
                if(pages==COPY_PAGES || !mapped_word(at,&witness[pages])) {ok=false;break;}
                addresses[pages++]=at;
            }
        }
        if(ok && mail_current()) {
#if KUI_WINCE_CDDA_WORMS
            enum worms_copy_result copied=worms_copy_ticket(&ticket,explicit_address,witness,addresses,pages);
            abandoned=copied==WORMS_COPY_RETRY;ok=copied==WORMS_COPY_OK;
#else
            volatile uint8_t *target=(volatile uint8_t *)(uintptr_t)regions[0];
            const volatile uint8_t *stage=(const volatile uint8_t *)(uintptr_t)(STAGE_BEGIN+player.stage_head);
            copy_bytes((void *)target,(const void *)stage,ticket.audio_bytes);
            for(uint32_t i=ticket.audio_bytes;i<ticket.bytes;i++) target[i]=0;
#endif
            for(uint32_t i=0;i<pages;i++) {
                struct kui_armada_ce_ram_binding again;
                if(!mapped_word(addresses[i],&again) || again.page_word!=witness[i].page_word ||
                   again.physical_address!=witness[i].physical_address) {
                    ok=false;
#if KUI_WINCE_CDDA_WORMS
                    abandoned=false;
#endif
                }
            }
        } else ok=false;
    }
    if(locked) {
        uint32_t args[8]={player.stats.buffer,regions[0],regions[1],regions[2],regions[3],0,0,0};
        player.stats.last_hresult=kui_armada_ce_excerpt_ce_call(player.binding.unlock,args);
        if(player.stats.last_hresult) {
            ok=false;
#if KUI_WINCE_CDDA_WORMS
            abandoned=false;
#endif
        }
    }
    /* Native Unlock publishes the host bytes before our producer frontier
     * advances. A newer mailbox epoch revokes this ticket before commit. */
    uint32_t sr=mask_begin();bool current=mail_current();
#if KUI_WINCE_CDDA_WORMS
    if(ok && current) {
        enum worms_copy_result fresh=worms_copy_frontier(&ticket,0u,0u);
        abandoned=fresh==WORMS_COPY_RETRY;ok=fresh==WORMS_COPY_OK;
    }
#endif
    if(ok && current) {
        player.last_model_result=kui_armada_ce_stream_refill_complete(&player.model,&ticket,true);
        ok=player.last_model_result==KUI_ARMADA_CE_STREAM_OK;
        /* Advance past the drained bytes. Shifting the remainder (up to a
         * whole 16-sector job, byte by byte) here kept IRQs masked for most
         * of a millisecond after every partial refill. source() moves only
         * the sub-64-byte carry, unmasked, before appending the next job. */
        if(ok) player.stage_head=player.model.staged?player.stage_head+ticket.audio_bytes:0u;
        if(ok) kui_armada_ce_stream_trace_ring_progress(player.now_ms);
    }
    mask_end(sr);
    bool released=kui_armada_ce_stream_native_write_end(&player.native,NULL);
    if(!current) {
        /* Unlock may race an idempotent RELEASE. No model bytes committed;
         * discard only this abandoned ticket so the retained staging can be
         * replanned against the next current consumer frontier. */
        if(released) player.model.refill=(struct kui_armada_ce_stream_refill){0};
        return released;
    }
#if KUI_WINCE_CDDA_WORMS
    if(abandoned && released) {
        /* A partial host copy is never published to the model. Retain its
         * staging and retry from the same producer offset next visit. */
        kui_armada_ce_stream_trace_copy_retry();
        player.model.refill=(struct kui_armada_ce_stream_refill){0};return true;
    }
#endif
    if(!ok) refuse(KUI_ARMADA_CE_EXCERPT_COPY);
    return ok && released;
}
static bool drain_refill(void) {
    /* One staged job is smaller than the ring. A native ticket ends at the
     * ring boundary, so its wrapped remainder may need one more ticket.
     * Do not yield while that checked remainder can still be published. */
    for(unsigned segment=0;segment<2u;segment++) {
        uint64_t before=player.model.produced;
        if(!refill()) return false;
        if(player.model.produced==before || player.model.staged<64u) break;
    }
    return true;
}
/* A selection's first checked PCM stays in the unused source-bank tail.
 * Only this owner worker reads/writes its metadata. It contains no padding,
 * and changes of range revoke the old prefix before any new bytes replace it. */
static void cache_key(void) {
    if(player.cache_first==player.model.first_lba && player.cache_end==player.model.end_lba) return;
    player.cache_bytes=0u;publish();
    player.cache_first=player.model.first_lba;player.cache_end=player.model.end_lba;
}
static bool cache_contains(void) {
    return player.cache_bytes<=CACHE_BYTES && player.model.fetch<=player.cache_bytes &&
        player.read.bytes<=player.cache_bytes-player.model.fetch;
}
static bool source_append(const void *from,uint32_t available,bool done,bool physical) {
    if(!mail_current()) return true;
    if(kui_armada_ce_stream_read_validate(&player.model,&player.read)!=KUI_ARMADA_CE_STREAM_OK) return false;
    /* Compact only the sub-block carry, outside the interrupt mask. */
    if(player.stage_head && player.model.staged<64u) {
        if(player.model.staged)
            copy_bytes((void *)(uintptr_t)STAGE_BEGIN,
                (const void *)(uintptr_t)(STAGE_BEGIN+player.stage_head),player.model.staged);
        player.stage_head=0;
    }
    if(player.stage_head>KUI_ARMADA_CE_STREAM_STAGE_BYTES ||
       player.model.staged>KUI_ARMADA_CE_STREAM_STAGE_BYTES-player.stage_head ||
       available>KUI_ARMADA_CE_STREAM_STAGE_BYTES-player.stage_head-player.model.staged) return false;
    copy_bytes((void *)(uintptr_t)(STAGE_BEGIN+player.stage_head+player.model.staged),from,available);
    uint32_t cache_add=0;
    if(physical && player.model.fetch==player.cache_bytes && player.cache_bytes<CACHE_BYTES) {
        cache_add=available;
        if(cache_add>CACHE_BYTES-player.cache_bytes) cache_add=CACHE_BYTES-player.cache_bytes;
        /* Payload first, validity only after the same epoch's source commit.
         * A late command may leave unadvertised bytes, never a false hit. */
        copy_bytes((void *)(uintptr_t)(CACHE_BEGIN+player.cache_bytes),from,cache_add);
    }
#if KUI_ARMADA_CE_STREAM_PLAYER_TEST
    extern void kui_armada_ce_stream_player_test_source_copied(void);
    kui_armada_ce_stream_player_test_source_copied();
#endif
    uint32_t sr=mask_begin();
    bool current=mail_current();
    if(current) player.last_model_result=done?
        kui_armada_ce_stream_read_complete(&player.model,&player.read,true,available):
        kui_armada_ce_stream_read_progress(&player.model,&player.read,available);
    if(current && player.last_model_result==KUI_ARMADA_CE_STREAM_OK) {
        player.source_total+=available;player.cache_bytes+=cache_add;
        if(available) kui_armada_ce_stream_trace_source_progress(player.now_ms);
        if(!physical && player.model.state==KUI_ARMADA_CE_STREAM_PREFILL)
            player.start_cached+=available;
        if(physical && done) {
            kui_armada_ce_stream_trace_source_end(player.now_ms);
            player.read_active=0u;
        }
    }
    /* A late idempotent RELEASE retains the current ticket. Physical reads
     * keep their completed lease; cache tickets keep read_active zero. */
    mask_end(sr);
    return !current || player.last_model_result==KUI_ARMADA_CE_STREAM_OK;
}
static bool source_run(bool cache_only) {
    struct kui_armada_ce_stream_transport_snapshot progress;
    if(!mail_current()) return true;
    cache_key();
    if(!player.read_active) {
        enum kui_armada_ce_stream_result planned=player.model.read.id?
            kui_armada_ce_stream_read_validate(&player.model,&player.read):
            kui_armada_ce_stream_read_plan(&player.model,KUI_ARMADA_CE_STREAM_BATCH_SECTORS,&player.read);
        if(planned==KUI_ARMADA_CE_STREAM_NOTHING || planned==KUI_ARMADA_CE_STREAM_STATE ||
           planned==KUI_ARMADA_CE_STREAM_BUSY) return true;
        if(planned!=KUI_ARMADA_CE_STREAM_OK) return false;
        if(cache_contains())
            return source_append((const void *)(uintptr_t)(CACHE_BEGIN+(uint32_t)player.model.fetch),
                player.read.bytes,true,false);
        if(cache_only) return true;
        /* Even the preparatory cancellation fence can touch the bus. Do
         * not admit it or a replacement physical job after this step's
         * allowance has run out. The same model ticket retries next step. */
        if(player.model.native_running && !player.source_pumps_left) {
            if(!player.source_budget_yielded) {
                player.source_budget_yielded=1u;kui_armada_ce_stream_trace_budget_yield();
            }
            return true;
        }
        player.read_bus_epoch=player.read.id;
        uint32_t sr=mask_begin();
        /* A cancelled reception is fenced asynchronously before its singleton
         * becomes available. Retry this same model ticket without consuming a
         * second job epoch or falsely treating temporary ownership as I/O. */
        if(player.model.native_running) --player.source_pumps_left;
        (void)kui_armada_ce_stream_transport_pump();
        enum kui_armada_ce_stream_transport_status s=kui_armada_ce_stream_transport_submit(
            player.read_bus_epoch,player.read.lba,player.read.sectors,RAW_BEGIN,RAW_BYTES);
        mask_end(sr);
        if(s==KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY) return true;
        if(s!=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING && s!=KUI_ARMADA_CE_STREAM_TRANSPORT_DONE) return false;
        player.read_active=1u;
        kui_armada_ce_stream_trace_source_begin(player.now_ms);
    }
    if(cache_only) return true;
    uint32_t sr;
    enum kui_armada_ce_stream_transport_status status=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING;
    /* Ready PIO and a yielded token search need no IRQ. Continue their
     * bounded work with a separate mask per pump; neither may sit idle
     * waiting for a DMA wake that cannot occur. Pending DMA, quota handoff
     * or a new command ends this bounded visit. */
    unsigned visit=0;bool immediate=false;
    for(;visit<SOURCE_READY_PUMPS;visit++) {
        sr=mask_begin();
        /* control() decides whether the new command actually changes the
         * epoch. An idempotent RELEASE must keep this physical job alive. */
        if(!mail_current()) {mask_end(sr);return true;}
        bool allowed=!player.model.native_running || player.source_pumps_left;
        if(allowed) {
            if(player.model.native_running) --player.source_pumps_left;
            (void)kui_armada_ce_stream_transport_pump_worker();
        }
        status=kui_armada_ce_stream_transport_snapshot(player.read_bus_epoch,&progress);
        immediate=status==KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING &&
            kui_armada_ce_stream_transport_worker_can_pump();
        mask_end(sr);
        if(!allowed) {
            if(immediate && !player.source_budget_yielded) {
                player.source_budget_yielded=1u;kui_armada_ce_stream_trace_budget_yield();
            }
            break;
        }
        if(!immediate) break;
    }
    if(visit==SOURCE_READY_PUMPS && immediate)
        kui_armada_ce_stream_trace_budget_yield();
    if(status==KUI_ARMADA_CE_STREAM_TRANSPORT_BUSY) return true;
    bool done=status==KUI_ARMADA_CE_STREAM_TRANSPORT_DONE;
    uint32_t raw_total=player.read.sectors*KUI_ARMADA_CE_STREAM_SECTOR_BYTES;
    if((!done && status!=KUI_ARMADA_CE_STREAM_TRANSPORT_PENDING) ||
       progress.bytes>raw_total || player.read.skip>raw_total ||
       (done && progress.bytes!=raw_total)) return false;
    uint32_t available=progress.bytes>player.read.skip?progress.bytes-player.read.skip:0u;
    if(available>player.read.bytes) available=player.read.bytes;
    if(!done) {
        /* Only the ordered CRC-checked RAW prefix is available. Retain a
         * final suffix under the same ticket until transport DONE certifies
         * the complete job, including short finite tracks and carry. */
        available&=~63u;
        if(available>=player.read.bytes) available=(player.read.bytes-1u)&~63u;
        if(!available) return true;
    } else if(available!=player.read.bytes) return false;
    /* IRQ reception only appends beyond this CRC-checked RAW prefix. */
    return source_append((const void *)(uintptr_t)(RAW_BEGIN+player.read.skip),available,done,true);
}
static bool source(void) {return source_run(false);}
static bool prime_cache(void) {
    /* At most seven cached jobs form the existing start reserve. Each keeps
     * its normal native lock, mapping witnesses and epoch publication checks;
     * IRQs stay enabled during copies. A miss performs no transport work. */
    for(unsigned batch=0;batch<7u && player.model.state==KUI_ARMADA_CE_STREAM_PREFILL;batch++) {
        uint64_t before=player.model.fetch,produced=player.model.produced;
        if(!source_run(true) || !drain_refill()) return false;
        if((player.model.fetch==before && player.model.produced==produced) ||
           kui_armada_ce_stream_can_start(&player.model)) break;
    }
    return true;
}
static bool start(void) {
    if(!kui_armada_ce_stream_can_start(&player.model) || !mail_current()) return true;
    player.stats.last_hresult=call(player.native.api.buffer_set_position,player.stats.buffer,0,0,0);
    if(player.stats.last_hresult) {refuse(KUI_ARMADA_CE_EXCERPT_PLAY);return false;}
    /* Play may start hardware before its native call returns. Anchor at a
     * fresh pre-call time, after position preparation, rather than treating
     * already played audio as an impossible jump after the return. */
    uint32_t started_ms;
    if(!clock_now(&started_ms)) return false;
    player.now_ms=started_ms;
    (void)call(player.binding.enter_cs,player.binding.device+DEVICE_CS_OFFSET,0,0,0);
    player.stats.free_voices=0;
    bool owners_valid=true;
    for(unsigned i=0;i<DEVICE_VOICES;i++) {
        struct kui_armada_ce_ram_binding owner;
        if(!mapped_word(player.binding.device+DEVICE_OWNER_OFFSET+i*4u,&owner)) {owners_valid=false;break;}
        if(!owner.value) ++player.stats.free_voices;
    }
    bool admitted=owners_valid && player.stats.free_voices>=2u && mail_current();
    if(admitted) player.stats.last_hresult=call(player.binding.play,player.stats.buffer,0,0,1u);
    (void)call(player.binding.leave_cs,player.binding.device+DEVICE_CS_OFFSET,0,0,0);
    if(!admitted) {
        if(!mail_current()) return true;
        if(!owners_valid) {refuse(KUI_ARMADA_CE_EXCERPT_IDENTITY);return false;}
        refuse(KUI_ARMADA_CE_EXCERPT_VOICES);return true;
    }
    player.last_model_result=kui_armada_ce_stream_start_complete(&player.model,player.model.epoch,
        started_ms,player.stats.last_hresult==0);
    if(player.stats.last_hresult || player.last_model_result!=KUI_ARMADA_CE_STREAM_OK) {
        refuse(KUI_ARMADA_CE_EXCERPT_PLAY);return false;
    }
    ++player.stats.play_ok;
    uint32_t latency=started_ms-player.start_ms;
    if(latency>65535u) latency=65535u;
    kui_armada_ce_stream_trace_event(KUI_ARMADA_CE_STREAM_TRACE_START,
        started_ms,player.source_total,ready_bytes(),
        ((player.start_cached/64u)<<16)|latency);
    return true;
}
static void step(uint32_t sr,uint32_t fpscr) {
    bool native_allowed=false;
    player.source_pumps_left=SOURCE_PLAYING_PUMPS;player.source_budget_yielded=0u;
    player.stats.caller_sr=sr;player.stats.caller_fpscr=fpscr;
    player.stats.current_process=word(CURRENT_PROCESS);
    if((sr&SR_CHECK)!=SR_REQUIRED) {
        (void)fault(STREAM_FAULT_SR,0u,sr,SR_REQUIRED,SR_CHECK);
        refuse(KUI_ARMADA_CE_EXCERPT_CONTEXT);goto failed;
    }
    if(fpscr==UINT32_MAX || (fpscr&FPSCR_NONSCALAR)) {
        (void)fault(STREAM_FAULT_FPSCR,0u,fpscr,0u,FPSCR_NONSCALAR);
        refuse(KUI_ARMADA_CE_EXCERPT_CONTEXT);goto failed;
    }
    if(!guard()) {
        refuse(KUI_ARMADA_CE_EXCERPT_IDENTITY);goto failed;
    }
    native_allowed=true;
    if(!observe() || !control() || !stopped()) goto failed;
#if KUI_WINCE_CDDA_WORMS || KUI_WINCE_CDDA_WWP
    if(player.model.stop_needed) {player.stats.status=KUI_ARMADA_CE_EXCERPT_QUEUED;return;}
#endif
    update_priority(player.model.state==KUI_ARMADA_CE_STREAM_PREFILL ||
        player.model.state==KUI_ARMADA_CE_STREAM_PLAYING);
    if(!mail_current()) return;
    if(player.model.state==KUI_ARMADA_CE_STREAM_PREFILL || player.model.state==KUI_ARMADA_CE_STREAM_PLAYING) {
        if(!player.stats.buffer && !create_buffer()) goto failed;
        /* Start a cached reserve before admitting the next physical read:
         * an outstanding source ticket deliberately blocks Play admission. */
        if(!prime_cache() || !start()) goto failed;
        uint32_t before=player.source_total;
        if(!drain_refill() || !source() || !drain_refill() || !start()) goto failed;
        /* Once a job has been admitted and drained, lend its replacement to
         * SCI immediately. Waiting for the next fully guarded native visit
         * leaves the transport idle through the yield and position call.
         * Start comes first because a new read ticket prevents ring prime
         * admission. This second source visit never waits for reception. */
        if(player.source_total!=before) {
            /* Ready PIO may finish this replacement too. Publish its PCM
             * before yielding: the next visit observes consumption first
             * and cannot count bytes left only in staging as fresh audio. */
            if(!source() || !drain_refill()) goto failed;
        }
    }
    switch(player.model.state) {
    case KUI_ARMADA_CE_STREAM_PLAYING: player.stats.status=KUI_ARMADA_CE_EXCERPT_PLAYING;break;
    case KUI_ARMADA_CE_STREAM_PAUSED: player.stats.status=KUI_ARMADA_CE_EXCERPT_PAUSED;break;
    case KUI_ARMADA_CE_STREAM_STOPPED: case KUI_ARMADA_CE_STREAM_ENDED:
        player.stats.status=KUI_ARMADA_CE_EXCERPT_STOPPED;break;
    default: player.stats.status=KUI_ARMADA_CE_EXCERPT_QUEUED;break;
    }
    return;
failed:
    player.disabled=1u;
    kui_armada_ce_stream_trace_event(KUI_ARMADA_CE_STREAM_TRACE_FAULT,
        player.now_ms,player.source_total,ready_bytes(),
        player.fault[0]?player.fault[0]:player.stats.status);
    /* Revocation only changes authored transport state, so it remains legal
     * under an ambiguous consumer lease. Pumping and native Stop remain
     * forbidden; the existing IRQ/DATA route can fence the revoked block. */
    cancel_read();
    if(native_allowed && player.stats.status!=KUI_ARMADA_CE_EXCERPT_CONTEXT &&
       player.stats.status!=KUI_ARMADA_CE_EXCERPT_IDENTITY &&
       player.native.status!=KUI_ARMADA_CE_STREAM_NATIVE_CONTEXT &&
       player.stats.buffer && !player.native.consumer_cs && guard()) {
        player.stats.last_hresult=call(player.binding.stop,player.stats.buffer,0,0,0);
        if(!player.stats.last_hresult) ++player.stats.stop_ok;
    }
    if(player.stats.status<KUI_ARMADA_CE_EXCERPT_ARGUMENT) refuse(KUI_ARMADA_CE_EXCERPT_COPY);
}
void kui_armada_ce_excerpt_attempt(uint32_t sr,uint32_t fpscr) {
    if(player.worker_step) {
        step(sr,fpscr);
#if KUI_WINCE_CDDA_WWP
        status_model();
#endif
        return;
    }
    if(!player.dispatching || player.worker_started || player.disabled) return;
    player.booting=1u;player.stats.caller_sr=sr;player.stats.caller_fpscr=fpscr;
    player.stats.current_process=word(CURRENT_PROCESS);
    if((sr&SR_CHECK)!=SR_REQUIRED || fpscr==UINT32_MAX || (fpscr&FPSCR_NONSCALAR) ||
       player.stats.current_process!=player.binding.owner_process || !fresh_identity() ||
       (!player.initialized && !initialize())) goto failed;
    player.stats.owner_process=player.binding.owner_process;player.stats.device=player.binding.device;
    player.worker_started=1u;publish();
    bool started=kui_armada_ce_stream_native_start(&player.native,&player.native.api,
        (uint32_t)(uintptr_t)kui_armada_ce_stream_worker,(uint32_t)(uintptr_t)&player,NULL);
    kui_armada_ce_stream_trace_event(KUI_ARMADA_CE_STREAM_TRACE_WORKER,0u,0u,0u,
        started?0u:player.native.status);
    if(!started) goto failed;
    publish();player.booting=0;return;
failed:
    player.disabled=1u;refuse(KUI_ARMADA_CE_EXCERPT_CALLBACK);publish();player.booting=0;
}
uint32_t kui_armada_ce_stream_worker(uint32_t argument) {
    if(argument!=(uint32_t)(uintptr_t)&player) return UINT32_MAX;
    for(;;) {
        if(player.disabled) {
            /* Revocation may leave one physical reception in flight. Drain
             * only that cancelled lease, one bounded pump per worker visit.
             * No native buffer operation or new source request occurs here. */
            if(player.native.consumer_cs || drain_cancelled_read()) return UINT32_MAX;
            if(!kui_armada_ce_stream_native_sleep(&player.native,NULL,STEP_MS))
                return UINT32_MAX;
            continue;
        }
        if(!player.booting && !player.dispatching && !player.disabled) {
            if(!kui_armada_ce_stream_native_ticks(&player.native,NULL,&player.now_ms)) {
                (void)fault(STREAM_FAULT_TICKS,0u,player.native.status,0u,player.native.last_result);
                refuse(KUI_ARMADA_CE_EXCERPT_CONTEXT);player.disabled=1u;
                kui_armada_ce_stream_trace_event(KUI_ARMADA_CE_STREAM_TRACE_FAULT,
                    player.now_ms,player.source_total,ready_bytes(),STREAM_FAULT_TICKS);
                cancel_read();
            } else {
                kui_armada_ce_stream_trace_worker_begin(player.now_ms);
                player.worker_step=1u;publish();
                kui_armada_ce_excerpt_owner_deferred(0);
                publish();player.worker_step=0;
                kui_armada_ce_stream_trace_sample(player.now_ms,player.source_total,
                    ready_bytes(),player.read_active | (player.model.native_running?2u:0u) |
                    (kui_armada_ce_stream_transport_urgent()?4u:0u) |
                    ((uint32_t)player.model.state<<8));
            }
        }
        if(player.disabled) continue;
        /* No SD or COM lock is held while this native CE thread yields. */
        uint32_t delay=step_delay();
        kui_armada_ce_stream_trace_sleep(delay);
        if(!kui_armada_ce_stream_native_sleep(&player.native,NULL,delay))
            return UINT32_MAX;
    }
}
