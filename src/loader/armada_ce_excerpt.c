/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_excerpt.h"
#include <stddef.h>

#if !defined(KUI_ARMADA_CE_STREAM) || KUI_ARMADA_CE_STREAM != 1

/* These files are linked only into the separately admitted exact-Armada
 * controller. The normal CE reader, Toy and MDK2 payloads do not use them. */
#if !defined(KUI_ARMADA_CE_EXCERPT) || KUI_ARMADA_CE_EXCERPT != 1
#error "The excerpt controller requires its isolated feature flag"
#endif
#if !defined(KUI_RETAIL_CE)
#error "The excerpt controller is Windows CE only"
#endif
#ifndef KUI_ARMADA_CE_EXCERPT_TEST
#define KUI_ARMADA_CE_EXCERPT_TEST 0
#endif
#if KUI_ARMADA_CE_EXCERPT_TEST != 0 && KUI_ARMADA_CE_EXCERPT_TEST != 1
#error "The excerpt test interface must be zero or one"
#endif

#define CURRENT_PROCESS UINT32_C(0x8c145890)
#define SR_REQUIRED UINT32_C(0x40000000)
#define SR_CHECK UINT32_C(0x700000f0) /* MD/RB/BL/IMASK; native CE owns FD */
#define OWNER_CALLBACK UINT32_C(0xfffffd1f)
#define CALLBACK_TOKEN UINT32_C(0x4b554943)
#define CALLBACK_DONE UINT32_C(0x43454453)
#define FPSCR_NONSCALAR UINT32_C(0x00180000) /* PR/SZ */
#define BUFFER_SOFTWARE 8u
#define DEVICE_VOICES 16u
#define DEVICE_OWNER_OFFSET 24u
#define DEVICE_CS_OFFSET 88u

struct excerpt_owner {
    struct kui_armada_ce_excerpt_binding binding;
    struct kui_armada_ce_excerpt_snapshot stats;
    volatile uint32_t pending,busy,dispatching;
    uint32_t callback_info[3];
    uint32_t pool_verified,disabled;
};
static struct excerpt_owner owner;

static void publish(void) { __asm__ __volatile__("" : : : "memory"); }
static uint32_t word(uint32_t address) {
#if KUI_ARMADA_CE_EXCERPT_TEST
    extern uint32_t kui_armada_ce_excerpt_test_word(uint32_t);
    return kui_armada_ce_excerpt_test_word(address);
#else
    return *(volatile const uint32_t *)(uintptr_t)address;
#endif
}
static const uint32_t *pcm_data(void) {
#if KUI_ARMADA_CE_EXCERPT_TEST
    extern const uint32_t *kui_armada_ce_excerpt_test_pcm(void);
    return kui_armada_ce_excerpt_test_pcm();
#else
    return (const uint32_t *)(uintptr_t)KUI_ARMADA_CE_EXCERPT_PCM_BEGIN;
#endif
}
static void copy_bytes(void *to,const void *from,unsigned bytes) {
    volatile uint8_t *dst=to;const volatile uint8_t *src=from;
    for(unsigned i=0;i<bytes;i++) dst[i]=src[i];
}
static void refuse(uint32_t why) {
    owner.stats.status=why; ++owner.stats.refused;
}
static bool owner_pointer(uint32_t pointer,uint32_t bytes) {
    uint32_t slot=pointer&UINT32_C(0xfe000000);
    return pointer>=0x10000u && pointer<0x80000000u && !(pointer&3u) && bytes &&
        bytes<=0x80000000u-pointer &&
        (slot==0u || slot==owner.binding.owner_slot) &&
        bytes<=0x02000000u-(pointer&0x01ffffffu);
}
static bool native_object(uint32_t object,uint32_t vtable) {
    if(!owner_pointer(object,4u)) return false;
    uint32_t actual=word(object);
    /* Native COM objects may retain current-process low pointers. The binder
     * publishes their explicit owner-slot form; MapPtrToProcess changes only
     * pointers below32MiB and preserves every explicit address domain. */
    if(actual<0x02000000u) actual|=owner.binding.owner_slot;
    return actual==vtable;
}
static uint32_t call(uint32_t target,uint32_t a0,uint32_t a1,uint32_t a2,uint32_t a3) {
    uint32_t args[8]={a0,a1,a2,a3,0,0,0,0};
    return kui_armada_ce_excerpt_ce_call(target,args);
}
static bool binding_valid(const struct kui_armada_ce_excerpt_binding *b) {
    if(!b || b->owner_process<0x8c14e6e0u || b->owner_process>=0x8c14fa60u ||
       (b->owner_process-0x8c14e6e0u)%0x9cu ||
       b->owner_slot<0x02000000u || b->owner_slot>0x40000000u ||
       (b->owner_slot&0x01ffffffu)) return false;
    const uint32_t *words=(const uint32_t *)b;
    for(unsigned i=2;i<sizeof(*b)/sizeof(uint32_t);i++) if(!words[i] || (words[i]&1u)) return false;
    return true;
}
int kui_armada_ce_excerpt_queue(const struct kui_armada_ce_excerpt_binding *b,
    uint32_t command,uint32_t first,uint32_t last,uint32_t repeat) {
    ++owner.stats.requests;
    if(owner.disabled) { ++owner.stats.refused;return 0; }
    if(owner.busy || owner.pending || owner.dispatching) { ++owner.stats.busy_skips; return 0; }
    if(command!=20u && command!=21u && command!=22u && command!=23u && command!=33u) {
        refuse(KUI_ARMADA_CE_EXCERPT_ARGUMENT);return 0;
    }
    if(!binding_valid(b)) { refuse(KUI_ARMADA_CE_EXCERPT_IDENTITY);return 0; }
    /* A retained COM reference belongs to one owner/device. Never rebind it
     * to a different process or newly initialized native sound instance. */
    if(owner.stats.device_reference && (b->owner_process!=owner.binding.owner_process ||
       b->device!=owner.binding.device || b->device_vtable!=owner.binding.device_vtable)) {
        refuse(KUI_ARMADA_CE_EXCERPT_OWNER);return 0;
    }
    copy_bytes(&owner.binding,b,sizeof(owner.binding));
    owner.stats.owner_process=b->owner_process;owner.stats.device=b->device;
    owner.stats.command=command;owner.stats.first=first;owner.stats.last=last;owner.stats.repeat=repeat;
    owner.stats.status=KUI_ARMADA_CE_EXCERPT_QUEUED;
    publish();owner.pending=command;
    return 1;
}
void kui_armada_ce_excerpt_snapshot(struct kui_armada_ce_excerpt_snapshot *out) {
    if(out) copy_bytes(out,&owner.stats,sizeof(owner.stats));
}
/* Native WIN32 method113 is a kernel special case, not a DLL function.
 * Its checked three-word descriptor lives in shared P1 memory: the native
 * process switch must never reinterpret it through a different slot0. */
uint32_t kui_armada_ce_excerpt_owner_callback(uint32_t token) {
    if(!owner.dispatching || token!=CALLBACK_TOKEN ||
       word(CURRENT_PROCESS)!=owner.binding.owner_process) return 0;
    kui_armada_ce_excerpt_owner_deferred(0);
    return CALLBACK_DONE;
}
void kui_armada_ce_excerpt_dispatch(uint32_t sr,uint32_t stack) {
    if(!owner.pending || owner.busy || owner.dispatching) return;
    owner.dispatching=1u;publish();
    if((sr&SR_CHECK)!=SR_REQUIRED) {
        kui_armada_ce_excerpt_attempt(sr,UINT32_MAX);
        publish();owner.dispatching=0u;return;
    }
    /* Native callback retains R15. CE normally makes thread stacks explicit
     * owner-slot addresses; refuse a raw slot0 stack that would change its
     * meaning when the kernel enters the sound process. P1 RAM is shared. */
    if((stack&3u) || !((stack>=0x02000000u && stack<0x80000000u) ||
                       (stack>=0x8c000000u && stack<0x8d000000u))) goto failed;
    if(!binding_valid(&owner.binding) ||
       word(0x8c14fa80u)!=0x8c011b20u || word(0x8c011b20u)!=0x32336e57u ||
       word(0x8c011b24u)!=0x00900003u || word(0x8c011b28u)!=0x8c0118e0u ||
       word(0x8c011aa4u)!=UINT32_MAX) goto failed;
    uint32_t handle=word(owner.binding.owner_process+8u);
    if(!handle || handle!=word(owner.binding.owner_process+8u)) goto failed;
    owner.callback_info[0]=handle;
    owner.callback_info[1]=(uint32_t)(uintptr_t)kui_armada_ce_excerpt_owner_callback;
    owner.callback_info[2]=CALLBACK_TOKEN;publish();
    uint32_t args[8]={(uint32_t)(uintptr_t)owner.callback_info,0,0,0,0,0,0,0};
    uint32_t result=kui_armada_ce_excerpt_ce_call(OWNER_CALLBACK,args);
    if(result==CALLBACK_DONE && !owner.pending) {
        publish();owner.dispatching=0u;return;
    }
failed:
    if(owner.pending) ++owner.stats.attempts;
    owner.pending=0;owner.stats.caller_sr=sr;
    owner.stats.current_process=word(CURRENT_PROCESS);
    refuse(KUI_ARMADA_CE_EXCERPT_CALLBACK);
    publish();owner.dispatching=0u;
}
static bool pool_valid(void) {
    if(word(KUI_ARMADA_CE_EXCERPT_RAM_END_WORD)!=KUI_ARMADA_CE_EXCERPT_POOL_BEGIN) return false;
    uint32_t seed[KUI_ARMADA_CE_EXCERPT_SEED_WORDS];
    for(unsigned i=0;i<KUI_ARMADA_CE_EXCERPT_SEED_WORDS;i++)
        seed[i]=word(KUI_ARMADA_CE_EXCERPT_POOL_BEGIN+i*4u);
    if(!kui_armada_ce_excerpt_seed_valid(seed)) return false;
    if(!owner.pool_verified) {
        uint32_t crc=kui_armada_ce_excerpt_crc32(
            (const uint8_t *)pcm_data(),KUI_ARMADA_CE_EXCERPT_PCM_BYTES);
        if(crc!=seed[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]) return false;
        owner.stats.pcm_crc32=crc;owner.pool_verified=1u;
    }
    return seed[KUI_ARMADA_CE_EXCERPT_SEED_PCM_CRC32]==owner.stats.pcm_crc32;
}
/* Release only references this adapter acquired. On a native Stop failure,
 * keep every lease rather than freeing storage that might still be playing. */
static bool release_buffer(bool stop_first) {
    if(owner.stats.buffer) {
        if(!native_object(owner.stats.buffer,owner.binding.buffer_vtable)) return false;
        if(stop_first) {
            owner.stats.last_hresult=call(owner.binding.stop,owner.stats.buffer,0,0,0);
            if(owner.stats.last_hresult) return false;
            ++owner.stats.stop_ok;
        }
        (void)call(owner.binding.buffer_release,owner.stats.buffer,0,0,0);
        owner.stats.buffer=0; ++owner.stats.release_ok;
    }
    if(owner.stats.device_reference) {
        (void)call(owner.binding.device_release,owner.binding.device,0,0,0);
        owner.stats.device_reference=0;
    }
    return true;
}
static bool create_buffer(void) {
    volatile uint32_t wave[5];
    wave[0]=0x00020001u;wave[1]=44100u;wave[2]=176400u;wave[3]=0x00100004u;wave[4]=0;
    uint32_t desc[5]={20u,BUFFER_SOFTWARE,KUI_ARMADA_CE_EXCERPT_PLAY_BYTES,0u,(uint32_t)(uintptr_t)wave};
    uint32_t buffer=0,caps[5]={20u,0,0,0,0};
    if(!owner.stats.device_reference) {
        uint32_t refs=call(owner.binding.add_ref,owner.binding.device,0,0,0);
        /* Native AddRef has changed the count even on an unexpected result.
         * Retain that reference on ambiguity, rather than touching game refs. */
        owner.stats.device_reference=1u;
        if(refs<2u || refs>0x100000u) {
            owner.disabled=1u;refuse(KUI_ARMADA_CE_EXCERPT_REFERENCE);return false;
        }
    }
    owner.stats.last_hresult=call(owner.binding.create_buffer,owner.binding.device,
        (uint32_t)(uintptr_t)desc,(uint32_t)(uintptr_t)&buffer,0);
    if(owner.stats.last_hresult || !native_object(buffer,owner.binding.buffer_vtable)) {
        /* Never call through an unverified returned object. */
        if(buffer) owner.disabled=1u;
        refuse(KUI_ARMADA_CE_EXCERPT_BUFFER);return false;
    }
    owner.stats.buffer=buffer;
    owner.stats.last_hresult=call(owner.binding.get_caps,buffer,(uint32_t)(uintptr_t)caps,0,0);
    owner.stats.caps_flags=caps[1];owner.stats.caps_bytes=caps[2];
    if(owner.stats.last_hresult || caps[0]!=20u || !(caps[1]&BUFFER_SOFTWARE) || (caps[1]&4u) ||
       caps[2]!=KUI_ARMADA_CE_EXCERPT_PLAY_BYTES) {
        (void)release_buffer(false);refuse(KUI_ARMADA_CE_EXCERPT_CAPS);return false;
    }
    uint32_t regions[4]={0,0,0,0};
    uint32_t args[8]={buffer,0,KUI_ARMADA_CE_EXCERPT_PLAY_BYTES,
        (uint32_t)(uintptr_t)&regions[0],(uint32_t)(uintptr_t)&regions[1],
        (uint32_t)(uintptr_t)&regions[2],(uint32_t)(uintptr_t)&regions[3],0};
    owner.stats.last_hresult=kui_armada_ce_excerpt_ce_call(owner.binding.lock,args);
    if(owner.stats.last_hresult) {
        (void)release_buffer(false);refuse(KUI_ARMADA_CE_EXCERPT_LOCK);return false;
    }
    owner.stats.lock_bytes=regions[1];
    /* A whole software buffer lock at offset0 cannot wrap. GetCaps has proved
     * software storage; refuse sound RAM/MMIO/physical aliases nonetheless. */
    bool copy_ok=regions[1]==KUI_ARMADA_CE_EXCERPT_PLAY_BYTES && !regions[2] && !regions[3] &&
        owner_pointer(regions[0],regions[1]);
    if(copy_ok) {
        volatile uint32_t *to=(volatile uint32_t *)(uintptr_t)regions[0];
        const volatile uint32_t *from=pcm_data();
        for(unsigned i=0;i<KUI_ARMADA_CE_EXCERPT_PLAY_BYTES/4u;i++) to[i]=from[i];
    }
    uint32_t unlock[8]={buffer,regions[0],regions[1],regions[2],regions[3],0,0,0};
    owner.stats.last_hresult=kui_armada_ce_excerpt_ce_call(owner.binding.unlock,unlock);
    if(owner.stats.last_hresult) {
        /* A failed Unlock leaves native transfer lifetime unknown. */
        owner.disabled=1u;
        refuse(KUI_ARMADA_CE_EXCERPT_UNLOCK);return false;
    }
    if(!copy_ok) { (void)release_buffer(false);refuse(KUI_ARMADA_CE_EXCERPT_COPY);return false; }
    return true;
}
void kui_armada_ce_excerpt_attempt(uint32_t sr,uint32_t fpscr) {
    if(!owner.pending || owner.busy) return;
    owner.busy=1u;publish();uint32_t command=owner.pending;owner.pending=0;
    ++owner.stats.attempts;owner.stats.caller_sr=sr;owner.stats.caller_fpscr=fpscr;
    owner.stats.current_process=word(CURRENT_PROCESS);
    if((sr&SR_CHECK)!=SR_REQUIRED || fpscr==UINT32_MAX || (fpscr&FPSCR_NONSCALAR)) {
        refuse(KUI_ARMADA_CE_EXCERPT_CONTEXT);goto done;
    }
    if(owner.stats.current_process!=owner.binding.owner_process) {
        refuse(KUI_ARMADA_CE_EXCERPT_OWNER);goto done;
    }
    kui_armada_ce_excerpt_revalidate_fn validate=(kui_armada_ce_excerpt_revalidate_fn)
        (uintptr_t)owner.binding.revalidate;
    if(!binding_valid(&owner.binding) || !validate(&owner.binding) ||
       word(CURRENT_PROCESS)!=owner.binding.owner_process ||
       !native_object(owner.binding.device,owner.binding.device_vtable)) {
        refuse(KUI_ARMADA_CE_EXCERPT_IDENTITY);goto done;
    }
    if(command==23u) {
        if(!release_buffer(true)) {owner.disabled=1u;refuse(KUI_ARMADA_CE_EXCERPT_NATIVE_STOP);}
        else owner.stats.status=KUI_ARMADA_CE_EXCERPT_RELEASED;
        goto done;
    }
    if(command==22u || command==33u) {
        if(owner.stats.buffer) {
            owner.stats.last_hresult=call(owner.binding.stop,owner.stats.buffer,0,0,0);
            if(owner.stats.last_hresult) {
                owner.disabled=1u;refuse(KUI_ARMADA_CE_EXCERPT_NATIVE_STOP);goto done;
            }
            ++owner.stats.stop_ok;
        }
        owner.stats.status=command==22u?KUI_ARMADA_CE_EXCERPT_PAUSED:KUI_ARMADA_CE_EXCERPT_STOPPED;
        goto done;
    }
    if(command!=20u || owner.stats.first!=4u || owner.stats.last!=4u || owner.stats.repeat!=15u) {
        refuse(KUI_ARMADA_CE_EXCERPT_ARGUMENT);goto done;
    }
    if(!pool_valid()) { refuse(KUI_ARMADA_CE_EXCERPT_POOL);goto done; }
    if(!owner.stats.buffer && !create_buffer()) goto done;
    if(!native_object(owner.stats.buffer,owner.binding.buffer_vtable)) {
        refuse(KUI_ARMADA_CE_EXCERPT_IDENTITY);goto done;
    }
    /* The native device CS is recursive on this thread. Holding its Play
     * lock across this free-owner admission closes the reclaim race. Never
     * mask interrupts or perform source copying while holding this lock. */
    (void)call(owner.binding.enter_cs,owner.binding.device+DEVICE_CS_OFFSET,0,0,0);
    owner.stats.free_voices=0;
    for(unsigned i=0;i<DEVICE_VOICES;i++)
        if(!word(owner.binding.device+DEVICE_OWNER_OFFSET+i*4u)) ++owner.stats.free_voices;
    uint32_t running=0;
    owner.stats.last_hresult=call(owner.binding.get_status,owner.stats.buffer,
        (uint32_t)(uintptr_t)&running,0,0);
    if(owner.stats.last_hresult) {
        (void)call(owner.binding.leave_cs,owner.binding.device+DEVICE_CS_OFFSET,0,0,0);
        refuse(KUI_ARMADA_CE_EXCERPT_PLAY);goto done;
    }
    bool already_running=!owner.stats.last_hresult && (running&1u);
    if(!already_running && owner.stats.free_voices>=2u)
        owner.stats.last_hresult=call(owner.binding.play,owner.stats.buffer,0,0,1u);
    (void)call(owner.binding.leave_cs,owner.binding.device+DEVICE_CS_OFFSET,0,0,0);
    if(!already_running && owner.stats.free_voices<2u) {
        refuse(KUI_ARMADA_CE_EXCERPT_VOICES);goto done;
    }
    if(owner.stats.last_hresult) { refuse(KUI_ARMADA_CE_EXCERPT_PLAY);goto done; }
    if(!already_running) ++owner.stats.play_ok;
    owner.stats.status=KUI_ARMADA_CE_EXCERPT_PLAYING;
    uint32_t position[2]={0,0};
    owner.stats.last_hresult=call(owner.binding.get_position,owner.stats.buffer,
        (uint32_t)(uintptr_t)&position[0],(uint32_t)(uintptr_t)&position[1],0);
    if(!owner.stats.last_hresult && position[0]<KUI_ARMADA_CE_EXCERPT_PLAY_BYTES &&
       position[1]<KUI_ARMADA_CE_EXCERPT_PLAY_BYTES) {
        owner.stats.play_cursor=position[0];owner.stats.write_cursor=position[1];++owner.stats.position_ok;
    }
done:
    publish();owner.busy=0u;
}
#endif /* The full-stream profile owns its separate player/controller. */
