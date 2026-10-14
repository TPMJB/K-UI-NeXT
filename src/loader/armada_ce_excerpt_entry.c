/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/armada_ce_excerpt_entry.h"
#include "kui/armada_ce_excerpt.h"
#include "kui/armada_ce_live_sound.h"
#include "kui/armada_ce_cache_word.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"
#if KUI_ARMADA_CE_STREAM
#include "kui/armada_ce_stream_transport.h"
#endif

#if !defined(KUI_ARMADA_CE_EXCERPT) || KUI_ARMADA_CE_EXCERPT != 1 || !defined(KUI_RETAIL_CE)
#error "The audible excerpt entry belongs only to its isolated CE controller"
#endif

/* Initial admission is protected by the resident's GD lock/IRQ mask. Fresh
 * admission runs on the restored caller stack with interrupts enabled, so
 * it must not share mutable binder storage with an interrupting GD entry. */
static struct kui_armada_ce_live_workspace workspace,deferred_workspace;
static struct kui_armada_ce_live_sound live,deferred_live;
static struct kui_armada_ce_excerpt_binding fresh;
#if !KUI_ARMADA_CE_STREAM
static struct kui_armada_ce_excerpt_binding binding;
#endif
static struct kui_armada_ce_excerpt_snapshot audio;
static struct kui_armada_ce_excerpt_entry_snapshot snapshot;

static bool read_ram(void *unused,uint32_t address,uint32_t *value) {
    uint32_t p1;
    (void)unused;
    if(!value || !kui_armada_ce_ram_table_p1(address,&p1)) return false;
    *value=*(volatile const uint32_t *)(uintptr_t)p1;
    return true;
}
#if KUI_ARMADA_CE_STREAM
/* Native startup and retained-reference admission happen only after the GD
 * lock/IRQ mask are restored. The masked entry copies scalar commands only. */
int kui_armada_ce_stream_bind_deferred(struct kui_armada_ce_excerpt_binding *out);
int kui_armada_ce_stream_bind_native(struct kui_armada_ce_live_stream_api *out);
int kui_armada_ce_stream_needs_deferred(void);
void kui_armada_ce_stream_progress(uint32_t out[3]);
#endif
static uint32_t cpu_word(uint32_t address) {
    return *(volatile const uint32_t *)(uintptr_t)address;
}
extern uint32_t kui_armada_ce_excerpt_array_read(uint32_t address);
static bool read_cache(void *unused,uint32_t address,uint32_t *value) {
    uint32_t checked;
    (void)unused;
    if((address&3u) || !value) return false;
    if((address&0xffffc01fu)==0xf4000000u ||
       (address&0xffffc003u)==0xf5000000u) {
        /* Only the assembly MOV executes in P2. Its SR mask remains active
         * through eight explicit NOPs before the return to cached C code. */
        uint32_t (*array)(uint32_t)=(uint32_t (*)(uint32_t))
            ((uintptr_t)kui_armada_ce_excerpt_array_read|0x20000000u);
        *value=array(address);return true;
    }
    if((address&0xe0000000u)!=0xa0000000u ||
       !kui_armada_ce_ram_p1(address,&checked)) return false;
    *value=*(volatile const uint32_t *)(uintptr_t)address;
    return true;
}
static enum kui_armada_ce_ram_bind_status read_mapped(
    void *context,uint32_t va,uint32_t p1,uint32_t page,uint32_t ccr,uint32_t *value) {
    enum kui_armada_ce_ram_bind_status status=
        kui_armada_ce_cache_word(va,p1,page,ccr,read_cache,context,value);
    if(cpu_word(0xff00001cu)!=ccr) {*value=0;return KUI_ARMADA_CE_RAM_BIND_CHANGED;}
    return status;
}
static int revalidate(const struct kui_armada_ce_excerpt_binding *expected);
static void make_binding(struct kui_armada_ce_excerpt_binding *dest,
                         const struct kui_armada_ce_live_sound *source) {
    dest->owner_process=source->owner_process;dest->owner_slot=source->owner_slot;
    dest->device=source->device;dest->device_vtable=source->vtable;
    dest->add_ref=source->add_ref;dest->device_release=source->release;
    dest->create_buffer=source->create_sound_buffer;dest->buffer_vtable=source->buffer_vtable;
    dest->buffer_release=source->buffer_release;dest->get_caps=source->buffer_get_caps;
    dest->lock=source->buffer_lock;dest->unlock=source->buffer_unlock;
    dest->play=source->buffer_play;dest->stop=source->buffer_stop;
    dest->get_status=source->buffer_get_status;dest->get_position=source->buffer_get_position;
    dest->enter_cs=source->enter_cs;dest->leave_cs=source->leave_cs;
    dest->revalidate=(uint32_t)(uintptr_t)revalidate;
}
static enum kui_armada_ce_live_status bind_now(bool deferred) {
    struct kui_armada_ce_live_workspace *w=deferred?&deferred_workspace:&workspace;
    struct kui_armada_ce_live_sound *out=deferred?&deferred_live:&live;
    snapshot.context[2]=cpu_word(KUI_ARMADA_CE_LIVE_CURRENT_PROCESS);
    snapshot.context[3]=snapshot.context[4]=0;
    enum kui_armada_ce_live_status status=kui_armada_ce_live_sound_bind_with_reader(
        cpu_word(0xff000008u),cpu_word(0xff00001cu),read_ram,NULL,read_mapped,w,out);
    for(unsigned i=0;i<5u;i++) snapshot.fault[i]=w->fault[i];
    if(status==KUI_ARMADA_CE_LIVE_OK) {
        snapshot.context[3]=out->owner_process;snapshot.context[4]=out->device;
    }
    return status;
}
/* Called only by the excerpt's restored CE caller context. Native APIs are
 * still absent here. Repeating the live binding prevents use of a stale
 * owner, section mapping, holder, method or loaded-module generation. */
static int revalidate(const struct kui_armada_ce_excerpt_binding *expected) {
    if(!expected) return 0;
    enum kui_armada_ce_live_status status=bind_now(true);
    snapshot.binding[1]=(uint32_t)status;snapshot.binding[2]=deferred_live.reads;
    if(status!=KUI_ARMADA_CE_LIVE_OK) return 0;
    make_binding(&fresh,&deferred_live);
    const uint32_t *a=(const uint32_t *)expected,*b=(const uint32_t *)&fresh;
    for(unsigned i=0;i<sizeof(fresh)/sizeof(uint32_t);i++) if(a[i]!=b[i]) return 0;
    return deferred_live.current_process==deferred_live.owner_process;
}
#if KUI_ARMADA_CE_STREAM
int kui_armada_ce_stream_bind_deferred(struct kui_armada_ce_excerpt_binding *out) {
    if(!out) return 0;
    enum kui_armada_ce_live_status status=bind_now(true);
    snapshot.binding[1]=(uint32_t)status;snapshot.binding[2]=deferred_live.reads;
    if(status!=KUI_ARMADA_CE_LIVE_OK) return 0;
    make_binding(out,&deferred_live);
    snapshot.context[3]=out->owner_process;snapshot.context[4]=out->device;
    return 1;
}
int kui_armada_ce_stream_bind_native(struct kui_armada_ce_live_stream_api *out) {
    if(!out || deferred_live.current_process!=deferred_live.owner_process ||
       cpu_word(KUI_ARMADA_CE_LIVE_CURRENT_PROCESS)!=deferred_live.owner_process) return 0;
    enum kui_armada_ce_live_status status=kui_armada_ce_live_stream_api_bind(
        &deferred_workspace,&deferred_live,out);
    snapshot.binding[1]=(uint32_t)status;
    return status==KUI_ARMADA_CE_LIVE_OK;
}
#endif
static void capture_audio(void) {
    kui_armada_ce_excerpt_snapshot(&audio);
    snapshot.context[0]=audio.caller_sr;snapshot.context[1]=audio.caller_fpscr;
    if(audio.attempts) {
        snapshot.context[2]=audio.current_process;
        snapshot.context[3]=audio.owner_process;snapshot.context[4]=audio.device;
    }
    snapshot.playback[0]=audio.status;snapshot.playback[1]=audio.attempts;
    snapshot.playback[2]=audio.play_ok;snapshot.playback[3]=audio.refused;
    snapshot.playback[4]=audio.last_hresult;
    snapshot.buffer[0]=audio.buffer;snapshot.buffer[1]=audio.caps_flags;
    snapshot.buffer[2]=audio.caps_bytes;snapshot.buffer[3]=audio.free_voices;
    snapshot.buffer[4]=audio.pcm_crc32;
    snapshot.cursor[0]=audio.command;snapshot.cursor[1]=audio.first;
    snapshot.cursor[2]=audio.last;snapshot.cursor[3]=audio.play_cursor;
    snapshot.cursor[4]=audio.write_cursor;
#if KUI_ARMADA_CE_STREAM
    uint32_t progress[3];
    kui_armada_ce_stream_progress(progress);
    snapshot.binding[2]=progress[0];snapshot.binding[3]=progress[1];snapshot.binding[4]=progress[2];
    uint32_t fault[5];
    kui_armada_ce_stream_fault(fault);
    if(fault[0]) for(unsigned i=0;i<5u;i++) snapshot.fault[i]=fault[i];
#endif
}
const struct kui_armada_ce_excerpt_entry_snapshot *kui_armada_ce_bind_probe(
    uint32_t function,uint32_t command,int32_t result,uint32_t request_address)
    __attribute__((section(".entry")));
const struct kui_armada_ce_excerpt_entry_snapshot *kui_armada_ce_bind_probe(
    uint32_t function,uint32_t command,int32_t result,uint32_t request_address) {
#if KUI_ARMADA_CE_STREAM
    if(function==KUI_ARMADA_CE_STREAM_BUS_EVENT)
        return (const struct kui_armada_ce_excerpt_entry_snapshot *)(uintptr_t)
            kui_armada_ce_stream_transport_event(command,request_address);
    /* Bootstrap may issue INIT/RESET, audio controls, EXEC or a terminal
     * report. None may change controller state before the relay's zero-state
     * proof. The stage attaches the immutable low bus only after all checks.
     * This return precedes even the deferred-field clear or snapshot read. */
    if(!kui_armada_ce_stream_transport_attached()) return &snapshot;
#endif
    snapshot.deferred=0;
    if(function==UINT32_MAX) {capture_audio();return &snapshot;}
#if KUI_ARMADA_CE_STREAM
    if((function==KUI_GD_EXEC || function==KUI_GD_CHECK) && !request_address) {
        if(kui_armada_ce_stream_needs_deferred())
            snapshot.deferred=(uint32_t)(uintptr_t)kui_armada_ce_excerpt_deferred;
        return &snapshot;
    }
#endif
    if(function!=KUI_GD_REQUEST || result<=0 ||
       (command!=KUI_RETAIL_GD_PLAY && command!=KUI_RETAIL_GD_PLAY2 &&
        command!=KUI_RETAIL_GD_PAUSE && command!=KUI_RETAIL_GD_RELEASE && command!=KUI_GD_STOP))
        return &snapshot;
    if(snapshot.binding[0]!=UINT32_MAX) ++snapshot.binding[0];
    /* The resident's own static request is admitted only as aligned direct
     * RAM. Neither caller parameters nor a CE virtual pointer are followed. */
    if((request_address&3u) || request_address<0x8c008300u ||
       request_address>0x8c00d800u-sizeof(struct kui_armada_ce_excerpt_request)) {
        snapshot.binding[1]=KUI_ARMADA_CE_LIVE_ARGUMENT;return &snapshot;
    }
    const struct kui_armada_ce_excerpt_request *request=
        (const struct kui_armada_ce_excerpt_request *)(uintptr_t)request_address;
    if(request->deferred_available!=1u) {
        if(snapshot.binding[4]!=UINT32_MAX) ++snapshot.binding[4];
#if !KUI_ARMADA_CE_STREAM
        return &snapshot;
#endif
    }
#if KUI_ARMADA_CE_STREAM
    if(kui_armada_ce_excerpt_queue(NULL,command,request->first,request->last,request->repeat)) {
        if(snapshot.binding[3]!=UINT32_MAX) ++snapshot.binding[3];
        if(request->deferred_available==1u)
            snapshot.deferred=(uint32_t)(uintptr_t)kui_armada_ce_excerpt_deferred;
    }
#else
    enum kui_armada_ce_live_status status=bind_now(false);
    snapshot.binding[1]=(uint32_t)status;snapshot.binding[2]=live.reads;
    if(status!=KUI_ARMADA_CE_LIVE_OK) return &snapshot;
    make_binding(&binding,&live);
    if(kui_armada_ce_excerpt_queue(&binding,command,request->first,request->last,request->repeat)) {
        if(snapshot.binding[3]!=UINT32_MAX) ++snapshot.binding[3];
        snapshot.deferred=(uint32_t)(uintptr_t)kui_armada_ce_excerpt_deferred;
    }
#endif
    return &snapshot;
}
