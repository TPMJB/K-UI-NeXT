/* SPDX-License-Identifier: GPL-3.0-only */
#define _GNU_SOURCE
#define KUI_RETAIL_CE 1
#define KUI_ARMADA_CE_EXCERPT 1
#include "kui/armada_ce_excerpt_entry.h"
#include "kui/armada_ce_excerpt.h"
#include "kui/armada_ce_live_sound.h"
#include "kui/gd_service.h"
#include "kui/retail_gd.h"
#include <assert.h>
#include <stdio.h>
#include <sys/mman.h>

/* Include the actual controller, substituting only independently tested
 * binding and native backend boundaries. Fixed pages model the hardware
 * CPU words and the resident's own direct-RAM request, not a guest lease. */
static unsigned binds,queues,samples;
static enum kui_armada_ce_live_status bind_result;
static struct kui_armada_ce_live_sound bind_value;
static struct kui_armada_ce_excerpt_binding queued_binding;
static struct kui_armada_ce_excerpt_snapshot sample_value;
static uint32_t queued_args[4];
static int queue_result;
static bool interrupt_bind;
static struct kui_armada_ce_live_workspace *last_workspace;
static struct kui_armada_ce_live_sound *last_output;
static enum kui_armada_ce_live_status fake_bind(uint32_t ttb,uint32_t ccr,
    kui_armada_ce_ram_read32 read32,void *context,
    kui_armada_ce_ram_read_mapped mapped32,
    struct kui_armada_ce_live_workspace *w,struct kui_armada_ce_live_sound *out) {
    assert(ttb==0x8c1458a0u && ccr==0x105u && read32 && mapped32 && !context && w && out);
    ++binds;
    last_workspace=w;last_output=out;
    if(bind_result==KUI_ARMADA_CE_LIVE_OK) *out=bind_value;
    else {memset(out,0,sizeof(*out));out->reads=17;}
    uint32_t stamp=1000u+binds;w->reads=stamp;
    for(unsigned i=0;i<5u;i++) w->fault[i]=bind_result==KUI_ARMADA_CE_LIVE_OK?0u:0xabcd0000u+i;
    if(interrupt_bind) {
        interrupt_bind=false;
        struct kui_armada_ce_live_sound original=bind_value;
        bind_value.device^=4u;
        /* Model a GD entry interrupting fresh admission. It may observe a
         * changed holder, but cannot mutate the deferred binder's storage. */
        (void)kui_armada_ce_bind_probe(KUI_GD_REQUEST,20,1,0x8c008400u);
        bind_value=original;
        assert(w->reads==stamp && out->device==original.device);
    }
    return bind_result;
}
int kui_armada_ce_excerpt_queue(const struct kui_armada_ce_excerpt_binding *b,
    uint32_t cmd,uint32_t first,uint32_t last,uint32_t repeat) {
    ++queues;queued_binding=*b;
    queued_args[0]=cmd;queued_args[1]=first;queued_args[2]=last;queued_args[3]=repeat;
    return queue_result;
}
void kui_armada_ce_excerpt_snapshot(struct kui_armada_ce_excerpt_snapshot *out) {
    ++samples;*out=sample_value;
}
void kui_armada_ce_excerpt_deferred(uint32_t unused) {(void)unused;assert(0);}
uint32_t kui_armada_ce_excerpt_array_read(uint32_t address) {
    (void)address;assert(0);return 0; /* Host tests never execute an SH array MOV. */
}
#define kui_armada_ce_live_sound_bind_with_reader fake_bind
#include "../src/loader/armada_ce_excerpt_entry.c"
#undef kui_armada_ce_live_sound_bind_with_reader

static struct kui_retail_gd service;
static volatile uint32_t kui_retail_ce_deferred[2];
static struct kui_armada_ce_excerpt_entry_snapshot observed_snapshot;
static unsigned entry_calls,display_rows;
static uint32_t observed_function,observed_command,observed_address;
static int32_t observed_result;
static struct kui_armada_ce_excerpt_request observed_request;
static const struct kui_armada_ce_excerpt_entry_snapshot *fake_entry(
    uint32_t fn,uint32_t cmd,int32_t result,uint32_t address);
static void retail_display_values(const char *label,const uint32_t *values,unsigned count) {
    assert(label && values && count==5u);++display_rows;
}
#define kui_armada_ce_bind_probe fake_entry
#include "../src/loader/armada_ce_audio_observe.inc"
#undef kui_armada_ce_bind_probe
static const struct kui_armada_ce_excerpt_entry_snapshot *fake_entry(
    uint32_t fn,uint32_t cmd,int32_t result,uint32_t address) {
    ++entry_calls;observed_function=fn;observed_command=cmd;
    observed_result=result;observed_address=address;
    observed_request=armada_ce_excerpt_request;
    return &observed_snapshot;
}
static void check_observe(void) {
    const uint32_t accepted[]={20,21,22,23,33};
    service.outputs[0]=4;service.outputs[1]=4;service.outputs[2]=15;
    observed_snapshot.deferred=0x8c004800u;
    for(unsigned i=0;i<sizeof(accepted)/sizeof(*accepted);i++) {
        entry_calls=0;kui_retail_ce_deferred[0]=0;kui_retail_ce_deferred[1]=0xdeadbeefu;
        armada_ce_audio_record(KUI_GD_REQUEST,accepted[i],17);
        assert(entry_calls==1 && observed_function==KUI_GD_REQUEST && observed_command==accepted[i] && observed_result==17);
        assert(observed_address==(uint32_t)(uintptr_t)&armada_ce_excerpt_request);
        assert(observed_request.first==4 && observed_request.last==4 && observed_request.repeat==15 && observed_request.deferred_available==1);
        assert(kui_retail_ce_deferred[0]==0x8c004800u && !kui_retail_ce_deferred[1]);
        entry_calls=0;kui_retail_ce_deferred[0]=0x02001234u;kui_retail_ce_deferred[1]=0x02005678u;
        armada_ce_audio_record(KUI_GD_REQUEST,accepted[i],17);
        assert(entry_calls==1 && !observed_request.deferred_available);
        assert(kui_retail_ce_deferred[0]==0x02001234u && kui_retail_ce_deferred[1]==0x02005678u);
    }
    entry_calls=0;memset(&armada_ce_excerpt_request,0x77,sizeof(armada_ce_excerpt_request));
    struct kui_armada_ce_excerpt_request before=armada_ce_excerpt_request;
    armada_ce_audio_record(KUI_GD_EXEC,20,1);
    armada_ce_audio_record(KUI_GD_REQUEST,20,0);
    armada_ce_audio_record(KUI_GD_REQUEST,20,-1);
    armada_ce_audio_record(KUI_GD_REQUEST,16,1);
    assert(!entry_calls && !memcmp(&before,&armada_ce_excerpt_request,sizeof(before)));
    entry_calls=display_rows=0;armada_ce_audio_report();
    assert(entry_calls==1 && display_rows==6 && observed_function==UINT32_MAX);
    assert(!observed_command && !observed_result && !observed_address);
}

static void page(uint32_t address,size_t bytes) {
    void *want=(void *)(uintptr_t)address;
    assert(mmap(want,bytes,PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED,-1,0)==want);
}
static void defaults(void) {
    memset(&snapshot,0,sizeof(snapshot));memset(&sample_value,0,sizeof(sample_value));
    memset(&binding,0,sizeof(binding));memset(&fresh,0,sizeof(fresh));
    memset(&live,0,sizeof(live));memset(&queued_binding,0,sizeof(queued_binding));
    binds=queues=samples=0;queue_result=1;bind_result=KUI_ARMADA_CE_LIVE_OK;
    interrupt_bind=false;last_workspace=NULL;last_output=NULL;
}
static const struct kui_armada_ce_excerpt_entry_snapshot *request(uint32_t fn,uint32_t cmd,int32_t result,uint32_t addr) {
    return kui_armada_ce_bind_probe(fn,cmd,result,addr);
}
int main(void) {
    page(0xff000000u,0x1000u);page(0x8c000000u,0x200000u);
    *(uint32_t *)(uintptr_t)0xff000008u=0x8c1458a0u;
    *(uint32_t *)(uintptr_t)0xff00001cu=0x105u;
    *(uint32_t *)(uintptr_t)KUI_ARMADA_CE_LIVE_CURRENT_PROCESS=0x8c14e6e0u;
    struct kui_armada_ce_excerpt_request *r=(void *)(uintptr_t)0x8c008400u;
    *r=(struct kui_armada_ce_excerpt_request){4,4,15,1};
    uint32_t *words=(uint32_t *)&bind_value;
    for(unsigned i=0;i<sizeof(bind_value)/sizeof(uint32_t);i++) words[i]=0x22000000u+i*4u;
    bind_value.current_process=bind_value.owner_process=0x8c14e6e0u;
    bind_value.owner_slot=0x02000000u;bind_value.refcount=1;bind_value.reads=956;
    const struct kui_armada_ce_excerpt_entry_snapshot *s;

    /* REQUEST is function zero; the snapshot sentinel must not swallow it. */
    assert(KUI_GD_REQUEST==0);defaults();
    s=request(KUI_GD_REQUEST,KUI_RETAIL_GD_PLAY,9,0x8c008400u);
    assert(binds==1 && queues==1 && !samples && s->binding[0]==1 && s->binding[3]==1);
    assert(s->deferred==(uint32_t)(uintptr_t)kui_armada_ce_excerpt_deferred);
    assert(queued_args[0]==20 && queued_args[1]==4 && queued_args[2]==4 && queued_args[3]==15);
    assert(queued_binding.owner_process==bind_value.owner_process && queued_binding.owner_slot==bind_value.owner_slot);
    assert(queued_binding.device==bind_value.device && queued_binding.device_vtable==bind_value.vtable);
    assert(queued_binding.add_ref==bind_value.add_ref && queued_binding.device_release==bind_value.release);
    assert(queued_binding.create_buffer==bind_value.create_sound_buffer && queued_binding.buffer_vtable==bind_value.buffer_vtable);
    assert(queued_binding.buffer_release==bind_value.buffer_release && queued_binding.get_caps==bind_value.buffer_get_caps);
    assert(queued_binding.lock==bind_value.buffer_lock && queued_binding.unlock==bind_value.buffer_unlock);
    assert(queued_binding.play==bind_value.buffer_play && queued_binding.stop==bind_value.buffer_stop);
    assert(queued_binding.get_status==bind_value.buffer_get_status && queued_binding.get_position==bind_value.buffer_get_position);
    assert(queued_binding.enter_cs==bind_value.enter_cs && queued_binding.leave_cs==bind_value.leave_cs);
    assert(queued_binding.revalidate==(uint32_t)(uintptr_t)revalidate);
    struct kui_armada_ce_live_workspace *entry_workspace=last_workspace;
    struct kui_armada_ce_live_sound *entry_output=last_output;
    assert(revalidate(&queued_binding));
    assert(last_workspace!=entry_workspace && last_output!=entry_output);
    struct kui_armada_ce_excerpt_binding admitted=queued_binding;
    interrupt_bind=true;queue_result=0;
    assert(revalidate(&admitted));queue_result=1;
    queued_binding=admitted;
    for(unsigned i=0;i<sizeof(queued_binding)/sizeof(uint32_t);i++) {
        struct kui_armada_ce_excerpt_binding altered=queued_binding;
        ((uint32_t *)&altered)[i]^=4u;assert(!revalidate(&altered));
    }
    ++bind_value.refcount;assert(revalidate(&queued_binding));
    bind_value.current_process^=0x9cu;assert(!revalidate(&queued_binding));
    bind_value.current_process=bind_value.owner_process;
    assert(!revalidate(NULL));

    const uint32_t accepted[]={20,21,22,23,33};
    for(unsigned i=0;i<sizeof(accepted)/sizeof(*accepted);i++) {
        defaults();s=request(KUI_GD_REQUEST,accepted[i],1,0x8c008400u);
        assert(binds==1 && queues==1 && s->deferred);
        defaults();request(KUI_GD_REQUEST,accepted[i],0,UINT32_MAX);assert(!binds && !queues);
        defaults();request(KUI_GD_REQUEST,accepted[i],-1,UINT32_MAX);assert(!binds && !queues);
    }
    for(unsigned cmd=0;cmd<40u;cmd++) {
        bool audio_cmd=false;for(unsigned i=0;i<sizeof(accepted)/sizeof(*accepted);i++) audio_cmd|=cmd==accepted[i];
        if(!audio_cmd) {defaults();request(KUI_GD_REQUEST,cmd,1,UINT32_MAX);assert(!binds && !queues);}
    }
    const uint32_t invalid[]={0u,0x8c0082fcu,0x8c008301u,0x8c00d7f4u,0xac008400u,0x02008400u,UINT32_MAX};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++) {
        defaults();s=request(KUI_GD_REQUEST,20,1,invalid[i]);
        assert(!binds && !queues && !s->deferred && s->binding[1]==KUI_ARMADA_CE_LIVE_ARGUMENT);
    }
    defaults();r->deferred_available=0;
    s=request(KUI_GD_REQUEST,20,1,0x8c008400u);
    assert(!binds && !queues && !s->deferred && s->binding[4]==1);r->deferred_available=1;
    defaults();queue_result=0;s=request(KUI_GD_REQUEST,20,1,0x8c008400u);
    assert(binds==1 && queues==1 && !s->deferred && !s->binding[3]);
    for(unsigned why=1;why<=KUI_ARMADA_CE_LIVE_MAPPING;why++) {
        defaults();bind_result=(enum kui_armada_ce_live_status)why;
        s=request(KUI_GD_REQUEST,20,1,0x8c008400u);
        assert(binds==1 && !queues && !s->deferred && s->binding[1]==why && s->binding[2]==17);
        assert(s->context[2]==0x8c14e6e0u && !s->context[3] && !s->context[4]);
        for(unsigned i=0;i<5u;i++) assert(s->fault[i]==0xabcd0000u+i);
    }

    /* A terminal snapshot must remain read-only even if hardware pages and
     * the resident request are inaccessible. Samples do not erase a valid
     * binding owner/current observation before any native attempt. */
    defaults();s=request(KUI_GD_REQUEST,20,1,0x8c008400u);
    assert(munmap((void *)(uintptr_t)0xff000000u,0x1000u)==0);
    assert(munmap((void *)(uintptr_t)0x8c000000u,0x200000u)==0);
    unsigned before=binds;s=request(UINT32_MAX,0,0,0);
    assert(binds==before && samples==1 && !s->deferred);
    assert(s->context[2]==0x8c14e6e0u && s->context[3]==bind_value.owner_process && s->context[4]==bind_value.device);
    sample_value.attempts=1;sample_value.status=KUI_ARMADA_CE_EXCERPT_PLAYING;
    sample_value.caller_sr=0x40000000u;sample_value.caller_fpscr=0x40000u;
    sample_value.current_process=sample_value.owner_process=0x8c14e77cu;
    sample_value.device=0x02045600u;sample_value.buffer=0x02045700u;
    sample_value.play_ok=1;sample_value.free_voices=4;sample_value.play_cursor=8192;
    s=request(UINT32_MAX,0,0,0);
    assert(s->playback[0]==KUI_ARMADA_CE_EXCERPT_PLAYING && s->playback[1]==1 && s->playback[2]==1);
    assert(s->context[0]==0x40000000u && s->context[1]==0x40000u && s->context[2]==0x8c14e77cu);
    assert(s->buffer[0]==0x02045700u && s->buffer[3]==4 && s->cursor[3]==8192);
    check_observe();
    puts("Armada CE excerpt entry: accepted-request, deferred priority, fresh identity and terminal snapshot checks passed");
    return 0;
}
