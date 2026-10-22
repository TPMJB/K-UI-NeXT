/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored native-call model around the actual controller C. The two test
 * adapters replace physical reads/source storage only. No CE API or source
 * executable bytes are present. Non-PIE keeps the validated callback and
 * synthetic software-buffer pointers in the target's32-bit address domain. */
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define KUI_ARMADA_CE_EXCERPT 1
#define KUI_ARMADA_CE_EXCERPT_TEST 1
#define KUI_RETAIL_CE 1
#include "kui/armada_ce_excerpt.h"

#define DEV UINT32_C(0x02010000)
#define BUF UINT32_C(0x02020000)
#define DEVICE_VT UINT32_C(0x0210116c)
#define BUFFER_VT UINT32_C(0x021011cc)
#define PROCESS UINT32_C(0x8c14e6e0)
#define DRIVER_PROCESS (PROCESS+UINT32_C(0x9c))
#define PROCESS_CALLBACK UINT32_C(0xfffffd1f)
enum target { ADDREF=0x1000,DEVREL=0x1004,CREATE=0x1008,BUFREL=0x100c,
    CAPS=0x1010,LOCK=0x1014,UNLOCK=0x1018,PLAY=0x101c,STOP=0x1020,
    STATUS=0x1024,POSITION=0x1028,ENTER=0x102c,LEAVE=0x1030 };
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)) { fprintf(stderr,"line%d: %s\n",__LINE__,#x);abort(); } } while(0)
static uint32_t pcm[KUI_ARMADA_CE_EXCERPT_PCM_BYTES/4u];
static struct {uint32_t before;uint32_t pcm[KUI_ARMADA_CE_EXCERPT_PCM_BYTES/4u];uint32_t after;} host;
static struct {
    uint32_t seed[16],ram_end,process,device_vtable,buffer_vtable,voices[16];
    uint32_t device_refs,buffer_refs,running,fail,create_pointer,caps_flags,caps_bytes,created_bytes;
    uint32_t lock_pointer,lock_bytes,lock_second,lock_second_bytes;
    uint32_t cursor,write_cursor;
    uint32_t keys,owner_handle,api_descriptor,api_name,api_flags,api_methods,api_method;
    uint32_t callback_token,callback_return,callback_process,owner_fpscr,caller_stack;
    unsigned owner_entries,dispatch_calls,handle_reads,handle_change_on;
    unsigned calls,order[64],depth,validations;
    bool identity,change_owner,inject_busy,inject_dispatch_busy,inject_return_busy,skip_callback;
} model;
static struct kui_armada_ce_excerpt_binding binding;

uint32_t kui_armada_ce_excerpt_test_word(uint32_t address) {
    if(address==0x8c145890u) return model.process;
    if(address==PROCESS+8u) {
        ++model.handle_reads;
        return model.owner_handle^(model.handle_reads==model.handle_change_on?1u:0u);
    }
    if(address==0x8c14fa80u) return model.api_descriptor;
    if(address==0x8c011b20u) return model.api_name;
    if(address==0x8c011b24u) return model.api_flags;
    if(address==0x8c011b28u) return model.api_methods;
    if(address==0x8c011aa4u) return model.api_method;
    if(address==KUI_ARMADA_CE_EXCERPT_RAM_END_WORD) return model.ram_end;
    if(address==DEV) return model.device_vtable;
    if(address==BUF) return model.buffer_vtable;
    if(address>=DEV+24u && address<DEV+24u+64u && !(address&3u))
        return model.voices[(address-DEV-24u)/4u];
    if(address>=KUI_ARMADA_CE_EXCERPT_POOL_BEGIN &&
       address<KUI_ARMADA_CE_EXCERPT_POOL_BEGIN+64u && !(address&3u))
        return model.seed[(address-KUI_ARMADA_CE_EXCERPT_POOL_BEGIN)/4u];
    fprintf(stderr,"Unadmitted read%08" PRIx32 "\n",address);abort();
}
const uint32_t *kui_armada_ce_excerpt_test_pcm(void) {return pcm;}
/* Only native stack output/input pointers are reconstructed. This models
 * the32-bit target ABI without remapping a host stack or weakening bounds on
 * any stored object/Lock pointer used by the controller itself. */
static void *stack_pointer(const uint32_t args[8],uint32_t low) {
    uintptr_t high=(uintptr_t)args&~(uintptr_t)UINT32_MAX;
    return (void *)(high|low);
}
static int __attribute__((aligned(4))) revalidate(const struct kui_armada_ce_excerpt_binding *b) {
    ++model.validations;CHECK(b->device==DEV);
    if(model.change_owner) model.process=PROCESS+0x9cu;
    return model.identity;
}
uint32_t kui_armada_ce_excerpt_ce_call(uint32_t target,const uint32_t args[8]) {
    CHECK(model.calls<64u);model.order[model.calls++]=target;
    if(target==PROCESS_CALLBACK) {
        const uint32_t *info=(const uint32_t *)(uintptr_t)args[0];
        CHECK(info[0]==model.owner_handle &&
              info[1]==(uint32_t)(uintptr_t)kui_armada_ce_excerpt_owner_callback &&
              info[2]==0x4b554943u && !args[1] && !args[2] && !args[3]);
        ++model.dispatch_calls;
        uint32_t process=model.process,keys=model.keys;
        model.process=model.callback_process;model.keys=0x0000c0deu;
        if(model.inject_dispatch_busy)
            CHECK(!kui_armada_ce_excerpt_queue(&binding,20u,4u,4u,15u));
        uint32_t result=0;
        if(!model.skip_callback) {
            uint32_t (*callback)(uint32_t)=(uint32_t (*)(uint32_t))(uintptr_t)info[1];
            result=callback(info[2]^model.callback_token);
        }
        if(model.inject_return_busy)
            CHECK(!kui_armada_ce_excerpt_queue(&binding,20u,4u,4u,15u));
        model.process=process;model.keys=keys;
        return result^model.callback_return;
    }
    CHECK(model.process==PROCESS);
    if(target==ADDREF) {
        CHECK(args[0]==DEV);++model.device_refs;return model.fail==ADDREF?0:model.device_refs;
    }
    if(target==DEVREL) {
        CHECK(args[0]==DEV && model.device_refs>1u);return --model.device_refs;
    }
    if(target==CREATE) {
        CHECK(args[0]==DEV && model.device_refs==2u);
        const uint32_t *d=stack_pointer(args,args[1]);
        CHECK(d[0]==20u && d[1]==8u && d[2]==KUI_ARMADA_CE_EXCERPT_PLAY_BYTES && !d[3]);
        const uint32_t *w=stack_pointer(args,d[4]);
        CHECK(w[0]==0x00020001u && w[1]==44100u && w[2]==176400u && w[3]==0x00100004u && !w[4]);
        CHECK(!args[3]);
        if(model.fail==CREATE) return 0x8007000eu;
        model.created_bytes=d[2];
        *(uint32_t *)stack_pointer(args,args[2])=model.create_pointer;
        model.buffer_refs=1u;return 0;
    }
    if(target==BUFREL) {
        CHECK(args[0]==BUF && model.buffer_refs==1u && !model.depth);
        model.buffer_refs=0;return 0;
    }
    if(target==CAPS) {
        CHECK(args[0]==BUF && model.buffer_refs==1u);
        uint32_t *c=stack_pointer(args,args[1]);CHECK(c[0]==20u);
        c[1]=model.caps_flags;c[2]=model.caps_bytes;
        return model.fail==CAPS?0x80070057u:0;
    }
    if(target==LOCK) {
        CHECK(args[0]==BUF && !args[1] && args[2]==KUI_ARMADA_CE_EXCERPT_PLAY_BYTES && !args[7]);
        if(model.fail==LOCK) return 0x80070057u;
        *(uint32_t *)stack_pointer(args,args[3])=model.lock_pointer;
        *(uint32_t *)stack_pointer(args,args[4])=model.lock_bytes;
        *(uint32_t *)stack_pointer(args,args[5])=model.lock_second;
        *(uint32_t *)stack_pointer(args,args[6])=model.lock_second_bytes;
        return 0;
    }
    if(target==UNLOCK) {
        CHECK(args[0]==BUF && args[1]==model.lock_pointer && args[2]==model.lock_bytes &&
            args[3]==model.lock_second && args[4]==model.lock_second_bytes);
        CHECK(!model.depth);
        return model.fail==UNLOCK?0x80070057u:0;
    }
    if(target==ENTER) {CHECK(args[0]==DEV+88u && !model.depth);++model.depth;return 0;}
    if(target==LEAVE) {CHECK(args[0]==DEV+88u && model.depth==1u);--model.depth;return 0;}
    if(target==STATUS) {
        CHECK(args[0]==BUF && model.depth==1u);
        *(uint32_t *)stack_pointer(args,args[1])=model.running;
        return model.fail==STATUS?0x80070057u:0;
    }
    if(target==PLAY) {
        CHECK(args[0]==BUF && !args[1] && !args[2] && args[3]==1u && model.depth==1u);
        unsigned free=0;for(unsigned i=0;i<16;i++) if(!model.voices[i]) ++free;
        CHECK(free>=2u);
        /* Authored model of the exact recovered DSOUND stereo-loop gate:
         * 16-bit stereo buffer bytes must be divisible by64 for loop Play. */
        if(model.created_bytes&63u) return 0x887800ffu;
        if(model.fail==PLAY) return 0x8878000au;
        model.running=1u;
        if(model.inject_busy) CHECK(!kui_armada_ce_excerpt_queue(&binding,20u,4u,4u,15u));
        return 0;
    }
    if(target==POSITION) {
        CHECK(args[0]==BUF && !model.depth && model.running);
        *(uint32_t *)stack_pointer(args,args[1])=model.cursor;
        *(uint32_t *)stack_pointer(args,args[2])=model.write_cursor;
        return 0;
    }
    if(target==STOP) {
        CHECK(args[0]==BUF && model.buffer_refs==1u && !model.depth);
        if(model.fail==STOP) return 0x80070057u;
        model.running=0;return 0;
    }
    fprintf(stderr,"Unexpected native target%08" PRIx32 "\n",target);abort();
}
#include "../src/loader/armada_ce_excerpt.c"

/* The native dispatcher alone enters the sound-owner process. This mock is
 * the integer/FPU assembly boundary after NativeCallBack changes the process
 * and access keys; its attempt gets an actual scalar FPSCR even with FD=1. */
void kui_armada_ce_excerpt_owner_deferred(uint32_t unused) {
    CHECK(!unused && model.process==PROCESS && model.keys==0x0000c0deu);
    ++model.owner_entries;
    kui_armada_ce_excerpt_attempt(0x40008000u,model.owner_fpscr);
}

static void reset(unsigned free_voices) {
    memset(&owner,0,sizeof(owner));memset(&model,0,sizeof(model));
    memset(&host,0,sizeof(host));host.before=0x12345678u;host.after=0x87654321u;
    for(unsigned i=0;i<KUI_ARMADA_CE_EXCERPT_PCM_BYTES/4u;i++) pcm[i]=0xa5a50000u^i;
    uint32_t crc=kui_armada_ce_excerpt_crc32((const uint8_t *)pcm,sizeof(pcm));
    CHECK(kui_armada_ce_excerpt_seed_make(model.seed,crc));
    CHECK((uintptr_t)host.pcm<=UINT32_MAX && (uintptr_t)revalidate<=UINT32_MAX);
    model.ram_end=KUI_ARMADA_CE_EXCERPT_POOL_BEGIN;
    model.process=PROCESS;model.device_vtable=DEVICE_VT;model.buffer_vtable=BUFFER_VT;
    model.keys=0x1234abcdu;model.owner_handle=0x12345679u;
    model.api_descriptor=0x8c011b20u;model.api_name=0x32336e57u;model.api_flags=0x00900003u;
    model.api_methods=0x8c0118e0u;model.api_method=UINT32_MAX;
    model.callback_process=PROCESS;
    model.owner_fpscr=0x00040000u;
    model.caller_stack=0x04010000u;
    model.device_refs=1;model.create_pointer=BUF;model.identity=true;
    model.caps_flags=8u;model.caps_bytes=KUI_ARMADA_CE_EXCERPT_PLAY_BYTES;
    model.lock_pointer=(uint32_t)(uintptr_t)host.pcm;model.lock_bytes=KUI_ARMADA_CE_EXCERPT_PLAY_BYTES;
    model.cursor=176400u;model.write_cursor=180000u;
    for(unsigned i=free_voices;i<16u;i++) model.voices[i]=0x02030000u+i*4u;
    binding=(struct kui_armada_ce_excerpt_binding){PROCESS,0x02000000u,DEV,DEVICE_VT,
        ADDREF,DEVREL,CREATE,BUFFER_VT,BUFREL,CAPS,LOCK,UNLOCK,PLAY,STOP,STATUS,POSITION,
        ENTER,LEAVE,(uint32_t)(uintptr_t)revalidate};
}
static struct kui_armada_ce_excerpt_snapshot run(uint32_t command,uint32_t sr,uint32_t fpscr) {
    CHECK(kui_armada_ce_excerpt_queue(&binding,command,4u,4u,15u)==1);
    kui_armada_ce_excerpt_attempt(sr,fpscr);
    struct kui_armada_ce_excerpt_snapshot s;kui_armada_ce_excerpt_snapshot(&s);
    CHECK(!model.depth);return s;
}
static struct kui_armada_ce_excerpt_snapshot run_dispatch(uint32_t command,uint32_t sr) {
    const uint32_t process=model.process,keys=model.keys;
    CHECK(kui_armada_ce_excerpt_queue(&binding,command,4u,4u,15u)==1);
    kui_armada_ce_excerpt_dispatch(sr,model.caller_stack);
    struct kui_armada_ce_excerpt_snapshot s;kui_armada_ce_excerpt_snapshot(&s);
    CHECK(model.process==process && model.keys==keys && !model.depth && !owner.dispatching);
    return s;
}
static unsigned count(unsigned target) {unsigned n=0;for(unsigned i=0;i<model.calls;i++) if(model.order[i]==target) ++n;return n;}
static void check_copied_prefix(void) {
    CHECK(!memcmp(pcm,host.pcm,KUI_ARMADA_CE_EXCERPT_PLAY_BYTES));
    for(unsigned i=KUI_ARMADA_CE_EXCERPT_PLAY_BYTES/4u;
        i<KUI_ARMADA_CE_EXCERPT_PCM_BYTES/4u;i++) CHECK(!host.pcm[i]);
    CHECK(host.before==0x12345678u && host.after==0x87654321u);
}
static void expect_refusal(unsigned status) {
    struct kui_armada_ce_excerpt_snapshot s=run(20u,0x40000000u,0x40000u);
    CHECK(s.status==status && !s.play_ok && !model.running);
    CHECK(!count(PLAY) || (model.fail==PLAY && count(PLAY)==1u));
    CHECK(host.before==0x12345678u && host.after==0x87654321u);
}
int main(void) {
    CHECK(KUI_ARMADA_CE_EXCERPT_PCM_BYTES==352800u &&
          KUI_ARMADA_CE_EXCERPT_PLAY_BYTES==352768u &&
          KUI_ARMADA_CE_EXCERPT_PCM_BYTES/4u==88200u &&
          KUI_ARMADA_CE_EXCERPT_PLAY_BYTES/4u==88192u &&
          (KUI_ARMADA_CE_EXCERPT_PCM_BYTES-KUI_ARMADA_CE_EXCERPT_PLAY_BYTES)/4u==8u);
    /* Replay the old stereo looping allocation directly through the native
     * contract model. The current controller's separately checked descriptor
     * uses the aligned size, so this reproduction cannot silently mask a bad
     * CreateSoundBuffer length in the actual included controller C. */
    const uint32_t loop_args[8]={BUF,0u,0u,1u,0u,0u,0u,0u};
    reset(2);model.buffer_refs=1u;model.depth=1u;
    model.created_bytes=KUI_ARMADA_CE_EXCERPT_PCM_BYTES;
    CHECK(kui_armada_ce_excerpt_ce_call(PLAY,loop_args)==0x887800ffu && !model.running);
    reset(2);model.buffer_refs=1u;model.depth=1u;
    model.created_bytes=KUI_ARMADA_CE_EXCERPT_PLAY_BYTES;
    CHECK(kui_armada_ce_excerpt_ce_call(PLAY,loop_args)==0u && model.running);
    const uint32_t rejected_sr[]={0,0x40000010u,0x400000f0u,0x50000000u,0x60000000u};
    for(unsigned i=0;i<sizeof(rejected_sr)/sizeof(rejected_sr[0]);i++) {
        reset(16);struct kui_armada_ce_excerpt_snapshot s=run(20u,rejected_sr[i],0x40000u);
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_CONTEXT && !model.calls && !model.validations);
    }
    reset(2);struct kui_armada_ce_excerpt_snapshot fd=run(20u,0x40008000u,0x40000u);
    CHECK(fd.status==KUI_ARMADA_CE_EXCERPT_PLAYING && fd.play_ok==1u &&
          fd.caller_sr==0x40008000u && fd.caller_fpscr==0x40000u);
    const uint32_t rejected_fp[]={UINT32_MAX,0x80000u,0x100000u,0x180000u};
    for(unsigned i=0;i<sizeof(rejected_fp)/sizeof(rejected_fp[0]);i++) {
        reset(16);struct kui_armada_ce_excerpt_snapshot s=run(20u,0x40000000u,rejected_fp[i]);
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_CONTEXT && !model.calls && !model.validations);
    }
    reset(16);model.process=PROCESS+0x9cu;expect_refusal(KUI_ARMADA_CE_EXCERPT_OWNER);CHECK(!model.calls);
    reset(16);model.identity=false;expect_refusal(KUI_ARMADA_CE_EXCERPT_IDENTITY);CHECK(!model.calls && model.validations==1u);
    reset(16);model.change_owner=true;expect_refusal(KUI_ARMADA_CE_EXCERPT_IDENTITY);CHECK(!model.calls);
    reset(16);model.device_vtable=0x1000116cu;expect_refusal(KUI_ARMADA_CE_EXCERPT_IDENTITY);CHECK(!model.calls);
    for(unsigned i=0;i<16;i++) {
        reset(16);model.seed[i]^=1u;expect_refusal(KUI_ARMADA_CE_EXCERPT_POOL);CHECK(!model.calls);
    }
    reset(16);pcm[27]^=1u;expect_refusal(KUI_ARMADA_CE_EXCERPT_POOL);CHECK(!model.calls);
    /* The uncopied32-byte tail still participates in full-source CRC. */
    reset(16);pcm[KUI_ARMADA_CE_EXCERPT_PCM_BYTES/4u-1u]^=1u;
    expect_refusal(KUI_ARMADA_CE_EXCERPT_POOL);CHECK(!model.calls);
    reset(16);model.ram_end=KUI_ARMADA_CE_EXCERPT_POOL_END;
    expect_refusal(KUI_ARMADA_CE_EXCERPT_POOL);CHECK(!model.calls);
    for(unsigned free=0;free<2;free++) {
        reset(free);uint32_t old[16];memcpy(old,model.voices,sizeof(old));
        expect_refusal(KUI_ARMADA_CE_EXCERPT_VOICES);
        CHECK(count(ENTER)==1u && count(LEAVE)==1u && !memcmp(old,model.voices,sizeof(old)));
        CHECK(model.device_refs==2u && model.buffer_refs==1u);
        (void)run(23u,0x40000000u,0x40000u);CHECK(model.device_refs==1u && !model.buffer_refs);
    }
    const unsigned reversible[]={CREATE,CAPS,LOCK,STATUS,PLAY};
    for(unsigned i=0;i<sizeof(reversible)/sizeof(reversible[0]);i++) {
        reset(2);model.fail=reversible[i];
        unsigned status=reversible[i]==CREATE?KUI_ARMADA_CE_EXCERPT_BUFFER:
            reversible[i]==CAPS?KUI_ARMADA_CE_EXCERPT_CAPS:
            reversible[i]==LOCK?KUI_ARMADA_CE_EXCERPT_LOCK:KUI_ARMADA_CE_EXCERPT_PLAY;
        expect_refusal(status);
        if(reversible[i]==CAPS || reversible[i]==LOCK) CHECK(model.device_refs==1u && !model.buffer_refs);
        else {model.fail=0;(void)run(23u,0x40000000u,0x40000u);CHECK(model.device_refs==1u && !model.buffer_refs);}
    }
    reset(2);model.caps_flags=4u;expect_refusal(KUI_ARMADA_CE_EXCERPT_CAPS);CHECK(model.device_refs==1u && !model.buffer_refs);
    reset(2);model.caps_bytes-=4u;expect_refusal(KUI_ARMADA_CE_EXCERPT_CAPS);CHECK(model.device_refs==1u && !model.buffer_refs);
    reset(2);model.caps_bytes=KUI_ARMADA_CE_EXCERPT_PCM_BYTES;
    expect_refusal(KUI_ARMADA_CE_EXCERPT_CAPS);CHECK(model.device_refs==1u && !model.buffer_refs);
    const uint32_t wrong_pointer[]={0xa0800000u,0x8cf60000u,0x04010000u,0x01fffffcu,0u};
    for(unsigned i=0;i<sizeof(wrong_pointer)/sizeof(wrong_pointer[0]);i++) {
        reset(2);model.lock_pointer=wrong_pointer[i];expect_refusal(KUI_ARMADA_CE_EXCERPT_COPY);
        CHECK(count(UNLOCK)==1u && model.device_refs==1u && !model.buffer_refs);
        CHECK(host.pcm[0]==0 && host.pcm[sizeof(host.pcm)/sizeof(host.pcm[0])-1u]==0);
    }
    reset(2);model.lock_second=0x02040000u;expect_refusal(KUI_ARMADA_CE_EXCERPT_COPY);
    reset(2);model.lock_bytes-=4u;expect_refusal(KUI_ARMADA_CE_EXCERPT_COPY);
    reset(2);model.lock_bytes=KUI_ARMADA_CE_EXCERPT_PCM_BYTES;
    expect_refusal(KUI_ARMADA_CE_EXCERPT_COPY);
    reset(2);model.fail=UNLOCK;expect_refusal(KUI_ARMADA_CE_EXCERPT_UNLOCK);
    CHECK(model.device_refs==2u && model.buffer_refs==1u && !count(BUFREL) && !count(DEVREL));
    CHECK(!kui_armada_ce_excerpt_queue(&binding,23u,4u,4u,15u));
    reset(2);model.fail=ADDREF;expect_refusal(KUI_ARMADA_CE_EXCERPT_REFERENCE);
    CHECK(model.device_refs==2u && !model.buffer_refs && !count(DEVREL) && !count(CREATE));
    CHECK(!kui_armada_ce_excerpt_queue(&binding,20u,4u,4u,15u));
    reset(2);model.create_pointer=0xa0800000u;expect_refusal(KUI_ARMADA_CE_EXCERPT_BUFFER);
    CHECK(!count(BUFREL) && model.device_refs==2u);
    CHECK(!kui_armada_ce_excerpt_queue(&binding,20u,4u,4u,15u));
    reset(2);model.device_vtable=DEVICE_VT&0x01ffffffu;model.buffer_vtable=BUFFER_VT&0x01ffffffu;
    model.inject_busy=true;struct kui_armada_ce_excerpt_snapshot s=run(20u,0x40000000u,0x40000u);
    CHECK(s.status==KUI_ARMADA_CE_EXCERPT_PLAYING && s.play_ok==1u && s.position_ok==1u && s.busy_skips==1u);
    CHECK(s.play_cursor==model.cursor && s.write_cursor==model.write_cursor && s.free_voices==2u);
    check_copied_prefix();
    CHECK(model.device_refs==2u && model.buffer_refs==1u && model.running);
    unsigned expected[]={ADDREF,CREATE,CAPS,LOCK,UNLOCK,ENTER,STATUS,PLAY,LEAVE,POSITION};
    CHECK(model.calls==sizeof(expected)/sizeof(expected[0]) && !memcmp(model.order,expected,sizeof(expected)));
    CHECK(model.created_bytes==KUI_ARMADA_CE_EXCERPT_PLAY_BYTES &&
          s.caps_bytes==KUI_ARMADA_CE_EXCERPT_PLAY_BYTES &&
          s.lock_bytes==KUI_ARMADA_CE_EXCERPT_PLAY_BYTES);
    unsigned calls=model.calls;s=run(20u,0x40000000u,0x40000u);
    CHECK(s.play_ok==1u && count(CREATE)==1u && count(PLAY)==1u && model.calls==calls+4u);
    s=run(22u,0x40000000u,0x40000u);CHECK(s.status==KUI_ARMADA_CE_EXCERPT_PAUSED && !model.running && model.buffer_refs==1u);
    s=run(20u,0x40000000u,0x40000u);CHECK(s.play_ok==2u && model.running && count(ADDREF)==1u);
    s=run(33u,0x40000000u,0x40000u);CHECK(s.status==KUI_ARMADA_CE_EXCERPT_STOPPED && !model.running);
    s=run(23u,0x40000000u,0x40000u);CHECK(s.status==KUI_ARMADA_CE_EXCERPT_RELEASED && !s.buffer && !s.device_reference);
    CHECK(model.device_refs==1u && !model.buffer_refs && count(BUFREL)==1u && count(DEVREL)==1u);
    for(unsigned fault=0;fault<3u;fault++) {
        reset(2);model.cursor=KUI_ARMADA_CE_EXCERPT_PLAY_BYTES-4u;
        model.write_cursor=model.cursor;
        if(fault==1u) model.cursor=KUI_ARMADA_CE_EXCERPT_PLAY_BYTES;
        if(fault==2u) model.write_cursor=KUI_ARMADA_CE_EXCERPT_PCM_BYTES-4u;
        s=run(20u,0x40008000u,0x40000u);
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_PLAYING && s.play_ok==1u &&
              s.position_ok==(fault?0u:1u));
    }
    reset(2);(void)run(20u,0x40000000u,0x40000u);model.fail=STOP;s=run(23u,0x40000000u,0x40000u);
    CHECK(s.status==KUI_ARMADA_CE_EXCERPT_NATIVE_STOP && model.buffer_refs==1u && model.device_refs==2u && model.running);
    CHECK(!count(BUFREL) && !count(DEVREL) && !kui_armada_ce_excerpt_queue(&binding,20u,4u,4u,15u));
    reset(2);CHECK(!kui_armada_ce_excerpt_queue(NULL,20u,4u,4u,15u));
    CHECK(!kui_armada_ce_excerpt_queue(&binding,16u,4u,4u,15u));
    CHECK(kui_armada_ce_excerpt_queue(&binding,20u,4u,4u,15u));
    CHECK(!kui_armada_ce_excerpt_queue(&binding,23u,4u,4u,15u));
    reset(2);CHECK(kui_armada_ce_excerpt_queue(&binding,21u,4u,4u,15u));
    kui_armada_ce_excerpt_attempt(0x40000000u,0x40000u);
    kui_armada_ce_excerpt_snapshot(&s);CHECK(s.status==KUI_ARMADA_CE_EXCERPT_ARGUMENT && !model.calls);
    /* The GD driver is a different process. Only the native callback mock
     * changes the process/access keys; dispatch returns to the original
     * driver after both successful playback and every refusal. */
    for(unsigned fd_flag=0;fd_flag<2u;fd_flag++) {
        reset(2);model.process=DRIVER_PROCESS;
        s=run_dispatch(20u,0x40000000u|(fd_flag?0x8000u:0u));
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_PLAYING && s.attempts==1u &&
              s.play_ok==1u && model.running && model.dispatch_calls==1u &&
              model.owner_entries==1u && model.order[0]==PROCESS_CALLBACK &&
              s.current_process==PROCESS && s.caller_sr==0x40008000u &&
              s.caller_fpscr==0x40000u && model.device_refs==2u && model.buffer_refs==1u);
        check_copied_prefix();
        s=run_dispatch(23u,0x40008000u);
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_RELEASED && s.attempts==2u &&
              model.device_refs==1u && !model.buffer_refs && model.dispatch_calls==2u);
    }
    /* Every native API-set gate remains exact. A missing/changing owner
     * handle and wrong descriptor/name/method never invoke the callback. */
    for(unsigned fault=0;fault<7u;fault++) {
        reset(2);model.process=DRIVER_PROCESS;
        if(fault==0u) model.api_descriptor^=4u;
        if(fault==1u) model.api_name^=1u;
        if(fault==2u) model.api_flags^=1u;
        if(fault==3u) model.api_methods^=4u;
        if(fault==4u) model.api_method=0u;
        if(fault==5u) model.owner_handle=0u;
        if(fault==6u) model.handle_change_on=2u;
        s=run_dispatch(20u,0x40008000u);
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_CALLBACK && s.attempts==1u &&
              s.refused==1u && !model.calls && !model.validations && !model.owner_entries &&
              !owner.pending && !s.play_ok && model.device_refs==1u && !model.buffer_refs);
    }
    for(unsigned i=0;i<sizeof(rejected_sr)/sizeof(rejected_sr[0]);i++) {
        reset(2);model.process=DRIVER_PROCESS;s=run_dispatch(20u,rejected_sr[i]);
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_CONTEXT && s.attempts==1u &&
              !model.calls && !model.validations && !owner.pending);
    }
    /* NativeCallBack keeps the driver's original stack. Raw slot0 aliases
     * would change meaning in the owner process, while P2/MMIO/P3/unaligned
     * addresses are never admitted. Explicit CE and shared P1 RAM stacks
     * retain the same address domain across the native process switch. */
    const uint32_t wrong_stack[]={0u,0x00010000u,0x01fffffcu,0x80010000u,
        0xac010000u,0xa0800000u,0xff000000u,0xc0010000u,0x8d000000u,
        0x8c010002u,0x04010001u};
    for(unsigned i=0;i<sizeof(wrong_stack)/sizeof(wrong_stack[0]);i++) {
        reset(2);model.process=DRIVER_PROCESS;model.caller_stack=wrong_stack[i];
        s=run_dispatch(20u,0x40008000u);
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_CALLBACK && s.attempts==1u &&
              !model.calls && !model.validations && !model.owner_entries && !owner.pending);
    }
    const uint32_t accepted_stack[]={0x02010000u,0x7fff0000u,0x8c010000u,0x8cfef000u};
    for(unsigned i=0;i<sizeof(accepted_stack)/sizeof(accepted_stack[0]);i++) {
        reset(2);model.process=DRIVER_PROCESS;model.caller_stack=accepted_stack[i];
        s=run_dispatch(20u,0x40008000u);
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_PLAYING && s.attempts==1u &&
              s.play_ok==1u && model.dispatch_calls==1u && model.owner_entries==1u);
    }
    /* Callback entry proves its token, active route and current process.
     * Returning a cookie without consuming the queued request is refused. */
    reset(2);
    CHECK(kui_armada_ce_excerpt_owner_callback(0x4b554943u)==0u && !model.owner_entries);
    for(unsigned fault=0;fault<4u;fault++) {
        reset(2);model.process=DRIVER_PROCESS;
        if(fault==0u) model.callback_token=1u;
        if(fault==1u) model.callback_process=DRIVER_PROCESS;
        if(fault>=2u) model.skip_callback=true;
        if(fault==3u) model.callback_return=0x43454453u;
        s=run_dispatch(20u,0x40008000u);
        CHECK(s.status==KUI_ARMADA_CE_EXCERPT_CALLBACK && s.attempts==1u &&
              model.dispatch_calls==1u && model.calls==1u && !model.owner_entries &&
              !model.validations && !s.play_ok && !owner.pending);
    }
    /* A bad return cookie after an actual attempt is one attempt. Adapter
     * references remain owned and are not freed while native playback runs. */
    reset(2);model.process=DRIVER_PROCESS;model.callback_return=1u;
    s=run_dispatch(20u,0x40008000u);
    CHECK(s.status==KUI_ARMADA_CE_EXCERPT_CALLBACK && s.attempts==1u &&
          s.play_ok==1u && model.running && model.owner_entries==1u &&
          model.device_refs==2u && model.buffer_refs==1u && !count(BUFREL) && !count(DEVREL));
    /* Identity/current-owner revalidation and scalar FPU guards still apply
     * inside the destination process, before any sound API call. */
    for(unsigned fault=0;fault<4u;fault++) {
        reset(2);model.process=DRIVER_PROCESS;
        if(fault==0u) model.identity=false;
        if(fault==1u) model.change_owner=true;
        if(fault==2u) model.device_vtable^=4u;
        if(fault==3u) model.owner_fpscr=0x00100000u;
        s=run_dispatch(20u,0x40008000u);
        CHECK(s.status==(fault==3u?KUI_ARMADA_CE_EXCERPT_CONTEXT:KUI_ARMADA_CE_EXCERPT_IDENTITY) &&
              s.attempts==1u && model.calls==1u && model.owner_entries==1u &&
              !s.play_ok && model.device_refs==1u && !model.buffer_refs && !owner.pending);
    }
    /* There is no admission window between consumption in the owner and
     * native callback return. The dispatching guard rejects that fresh queue
     * even when both pending and busy are already clear. */
    reset(2);model.process=DRIVER_PROCESS;model.inject_dispatch_busy=true;
    model.inject_busy=true;model.inject_return_busy=true;
    s=run_dispatch(20u,0x40008000u);
    CHECK(s.status==KUI_ARMADA_CE_EXCERPT_PLAYING && s.attempts==1u &&
          s.play_ok==1u && s.busy_skips==3u && model.dispatch_calls==1u &&
          !owner.pending && model.device_refs==2u && model.buffer_refs==1u);
    puts("Armada CE excerpt controller checks passed");printf("%u checks\n",checks);return 0;
}
