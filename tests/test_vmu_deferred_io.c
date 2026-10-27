/* SPDX-License-Identifier: GPL-3.0-only */
/* Exercise the production mailbox and protocol engine together. The provider
 * models an independently owned device: OK follows copy/readback/release,
 * BUSY precedes acquisition, and an uncertain commit copies then fails. No
 * hardware durability or CE thread/SD arbitration is asserted by this test. */
#include "kui/vmu_deferred_io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long checks;
#define CHECK(condition) do { ++checks; if(!(condition)) { \
    fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#condition); abort(); \
} } while(0)

enum provider_action {ACTION_NONE,ACTION_REENTER,ACTION_MUTATE_BYTES,
    ACTION_CHANGE_TICKET,ACTION_CHANGE_OPERATION,ACTION_CHANGE_BLOCK,ACTION_CHANGE_STATE,ACTION_POISON};
struct fixture {
    uint8_t before[32];
    struct kui_vmu_deferred_io queue;
    uint8_t after[32];
    kui_vmu_emulation_t protocol;
    uint8_t media[256][512],expected[512];
    uint32_t sr;
    unsigned enters,leaves,fail_enter,provider_calls,read_calls,commit_calls;
    unsigned acquisitions,releases,busy_count,provider_depth;
    int provider_result;
    enum provider_action action;
    bool held,deny_enter,thread,owner,fence,uncertain,check_protocol;
};
static struct fixture f;
static uint8_t response[524];
static size_t response_bytes;

static bool guard_enter(void *context,uint32_t *saved) {
    struct fixture *m=context;CHECK(m==&f);++m->enters;
    if(m->deny_enter || m->enters==m->fail_enter) return false;
    CHECK(!m->held);CHECK(saved);*saved=m->sr;m->sr|=0xf0u;m->held=true;return true;
}
static void guard_leave(void *context,uint32_t saved) {
    struct fixture *m=context;CHECK(m==&f);CHECK(m->held);CHECK(m->sr==(saved|0xf0u));
    m->held=false;m->sr=saved;++m->leaves;
}
static bool worker_ready(void *context) {
    struct fixture *m=context;CHECK(m==&f);
    return m->thread && m->owner && !m->fence && !m->held &&
        (m->sr&0x100000f0u)==0;
}
static void intact(void) {
    for(unsigned i=0;i<sizeof(f.before);i++) CHECK(f.before[i]==0x39u);
    for(unsigned i=0;i<sizeof(f.after);i++) CHECK(f.after[i]==0xc7u);
    CHECK(!f.held);CHECK(!f.provider_depth);CHECK(f.acquisitions==f.releases);
}
static void unchanged_output(const uint8_t bytes[512]) {
    for(unsigned i=0;i<512u;i++) CHECK(bytes[i]==0xa5u);
}
static void frame(uint8_t out[140],unsigned command,unsigned block,unsigned phase) {
    memset(out,0,140u);out[0]=(uint8_t)command;out[1]=2u;
    out[3]=(uint8_t)(command==12u?34u:2u);out[7]=2u;
    out[9]=(uint8_t)phase;out[10]=(uint8_t)(block>>8);out[11]=(uint8_t)block;
}
static void dispatch(const uint8_t *request,size_t bytes,unsigned expected) {
    uint32_t old_sr=f.sr;bool old_fence=f.fence;
    f.fence=true;f.sr=0x400000f0u;
    memset(response,0xa5,sizeof(response));response_bytes=999u;
    CHECK(kui_vmu_emulation_handle(&f.protocol,request,bytes,response,
        sizeof(response),&response_bytes)==1);
    CHECK(response[0]==expected);CHECK(response_bytes==4u+4u*response[3]);
    CHECK(response[1]==0u && response[2]==2u);
    for(size_t i=response_bytes;i<sizeof(response);i++) CHECK(response[i]==0xa5u);
    CHECK(!f.held && f.sr==0x400000f0u);
    f.sr=old_sr;f.fence=old_fence;
}
static void sync_block(unsigned block,unsigned expected) {
    uint8_t request[140];frame(request,13u,block,4u);dispatch(request,12u,expected);
}
static void stage(unsigned block,const uint8_t bytes[512]) {
    unsigned old_calls=f.provider_calls,old_jobs=f.queue.submissions;
    for(unsigned phase=0;phase<4u;phase++) {
        uint8_t request[140];frame(request,12u,block,phase);
        memcpy(request+12u,bytes+128u*phase,128u);dispatch(request,sizeof(request),7u);
    }
    CHECK(f.protocol.active && f.protocol.phases==15u && !f.protocol.commit_attempted);
    CHECK(f.provider_calls==old_calls && f.queue.submissions==old_jobs);
}
static void mutate_all_commit_bytes(unsigned block) {
    uint8_t changed[512];memcpy(changed,f.expected,512u);
    struct kui_vmu_deferred_io old=f.queue;
    for(unsigned i=0;i<512u;i++) {
        changed[i]^=0x80u;
        CHECK(kui_vmu_deferred_io_commit(&f.queue,(uint16_t)block,changed)==KUI_VMU_EMU_IO_BUSY);
        CHECK(!memcmp(&f.queue,&old,sizeof(old)));changed[i]^=0x80u;
    }
}
static void interrupt_requests(unsigned block) {
    uint8_t out[512],other[512];memset(out,0xa5,sizeof(out));memcpy(other,f.expected,512u);other[511]^=1u;
    struct kui_vmu_deferred_io old=f.queue;
    CHECK(kui_vmu_deferred_io_work(&f.queue)==0); /* Recursive worker cannot claim RUNNING. */
    CHECK(kui_vmu_deferred_io_commit(&f.queue,(uint16_t)block,f.expected)==KUI_VMU_EMU_IO_BUSY);
    CHECK(kui_vmu_deferred_io_commit(&f.queue,(uint16_t)(block+1u),other)==KUI_VMU_EMU_IO_BUSY);
    CHECK(kui_vmu_deferred_io_read(&f.queue,(uint16_t)block,out)==KUI_VMU_EMU_IO_BUSY);
    unchanged_output(out);CHECK(!memcmp(&f.queue,&old,sizeof(old)));
    if(f.check_protocol) sync_block(block,0xfcu); /* A real Maple retry may interrupt worker I/O. */
    CHECK(!memcmp(&f.queue,&old,sizeof(old)));
}
static int provider_begin(struct fixture *m,unsigned block,bool commit) {
    CHECK(m==&f);CHECK(block<256u);CHECK(!m->held && !m->fence);
    CHECK((m->sr&0x100000f0u)==0);CHECK(m->queue.state==KUI_VMU_DEFERRED_RUNNING);
    CHECK(!m->provider_depth);++m->provider_depth;++m->provider_calls;
    if(commit) ++m->commit_calls;else ++m->read_calls;
    if(m->busy_count) {--m->busy_count;--m->provider_depth;return KUI_VMU_EMU_IO_BUSY;}
    ++m->acquisitions;
    if(m->check_protocol && commit) CHECK(!m->protocol.committed_valid && m->protocol.commit_attempted);
    switch(m->action) {
    case ACTION_REENTER:interrupt_requests(block);break;
    case ACTION_MUTATE_BYTES:mutate_all_commit_bytes(block);break;
    case ACTION_CHANGE_TICKET:++m->queue.ticket;break;
    case ACTION_CHANGE_OPERATION:m->queue.operation^=3u;break;
    case ACTION_CHANGE_BLOCK:++m->queue.block;break;
    case ACTION_CHANGE_STATE:m->queue.state=KUI_VMU_DEFERRED_DONE;break;
    case ACTION_POISON:CHECK(!kui_vmu_deferred_io_poison(&m->queue));break;
    case ACTION_NONE:break;
    }
    return 99; /* Acquired: the provider must now release on every path. */
}
static int provider_read(void *context,uint16_t block,uint8_t bytes[512]) {
    struct fixture *m=context;int begun=provider_begin(m,block,false);
    if(begun!=99) return begun;
    if(m->provider_result==KUI_VMU_EMU_IO_OK) memcpy(bytes,m->media[block],512u);
    else memset(bytes,0xedu,512u); /* Failed scratch must never reach a protocol output. */
    ++m->releases;--m->provider_depth;return m->provider_result;
}
static int provider_commit(void *context,uint16_t block,const uint8_t bytes[512]) {
    struct fixture *m=context;int begun=provider_begin(m,block,true);
    if(begun!=99) return begun;
    CHECK(!memcmp(bytes,m->expected,512u));
    if(m->provider_result==KUI_VMU_EMU_IO_OK || m->uncertain) {
        memcpy(m->media[block],bytes,512u);CHECK(!memcmp(m->media[block],bytes,512u));
    }
    /* A successful provider result includes its final readback/release. The
     * mailbox must still be RUNNING until after this callback returns. */
    if(m->action<=ACTION_MUTATE_BYTES) CHECK(m->queue.state==KUI_VMU_DEFERRED_RUNNING);
    ++m->releases;--m->provider_depth;return m->provider_result;
}
static void setup(void) {
    memset(&f,0,sizeof(f));memset(f.before,0x39,sizeof(f.before));memset(f.after,0xc7,sizeof(f.after));
    f.sr=0x40000000u;f.thread=f.owner=true;
    for(unsigned block=0;block<256u;block++) for(unsigned i=0;i<512u;i++)
        f.media[block][i]=(uint8_t)(block*31u+i*17u+(i>>8));
    for(unsigned i=0;i<512u;i++) f.expected[i]=(uint8_t)(i*29u+(i>>4)+7u);
    const struct kui_vmu_emulation_io provider={&f,provider_read,provider_commit};
    const struct kui_vmu_deferred_guard guard={&f,guard_enter,guard_leave,worker_ready};
    CHECK(!kui_vmu_deferred_io_init(&f.queue,&provider,&guard));
    const struct kui_vmu_emulation_io mailbox={&f.queue,kui_vmu_deferred_io_read,kui_vmu_deferred_io_commit};
    CHECK(!kui_vmu_emulation_init(&f.protocol,2u,&mailbox));intact();
}

static void protocol_write_read_and_lost_ack(void) {
    setup();f.check_protocol=true;stage(42u,f.expected);sync_block(42u,0xfcu);
    CHECK(f.queue.state==KUI_VMU_DEFERRED_PENDING && f.queue.ticket==1u);
    CHECK(f.protocol.commit_attempted && !f.protocol.committed_valid && !f.provider_calls);
    sync_block(42u,0xfcu);CHECK(f.queue.submissions==1u);
    /* Every changed byte in a retransmitted phase is rejected while the
     * queued block remains immutable; identical phase retries remain ACKs. */
    for(unsigned i=0;i<512u;i++) {
        uint8_t request[140];frame(request,12u,42u,i/128u);
        memcpy(request+12u,f.expected+128u*(i/128u),128u);request[12u+i%128u]^=1u;
        dispatch(request,sizeof(request),0xfcu);CHECK(!memcmp(f.protocol.staged,f.expected,512u));
    }
    for(unsigned phase=0;phase<4u;phase++) {
        uint8_t request[140];frame(request,12u,42u,phase);memcpy(request+12u,f.expected+128u*phase,128u);
        dispatch(request,sizeof(request),7u);
    }
    f.action=ACTION_REENTER;CHECK(kui_vmu_deferred_io_work(&f.queue)==1);
    CHECK(f.queue.state==KUI_VMU_DEFERRED_DONE && f.queue.completions==1u);
    CHECK(!f.protocol.committed_valid && f.protocol.commit_attempted && f.commit_calls==1u);
    sync_block(42u,7u);CHECK(f.protocol.committed_valid && !f.protocol.active);
    CHECK(f.queue.state==KUI_VMU_DEFERRED_IDLE && !memcmp(f.media[42],f.expected,512u));
    /* Treat that final response as lost. A duplicate successful BSYNC uses
     * generic committed identity and cannot enqueue or write the block twice. */
    sync_block(42u,7u);sync_block(42u,7u);CHECK(f.commit_calls==1u && f.queue.submissions==1u);
    uint8_t request[140];frame(request,11u,42u,0u);dispatch(request,12u,0xfcu);
    f.action=ACTION_NONE;CHECK(kui_vmu_deferred_io_work(&f.queue)==1);dispatch(request,12u,8u);
    CHECK(response_bytes==524u && !memcmp(response+12u,f.expected,512u));
    frame(request,13u,42u,1u);dispatch(request,12u,7u);CHECK(f.read_calls==1u && f.commit_calls==1u);
    CHECK(f.queue.submissions==2u && f.queue.completions==2u);intact();
}
static void immutable_copy_and_exact_completion(void) {
    setup();uint8_t original[512],out[512];memcpy(original,f.expected,512u);memset(out,0xa5,sizeof(out));
    CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,original)==KUI_VMU_EMU_IO_BUSY);
    memset(original,0x11,sizeof(original)); /* The callback pointer was borrowed only. */
    mutate_all_commit_bytes(42u);CHECK(!memcmp(f.queue.bytes,f.expected,512u));
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);unchanged_output(out);
    CHECK(kui_vmu_deferred_io_commit(&f.queue,43u,f.expected)==KUI_VMU_EMU_IO_BUSY);
    f.action=ACTION_MUTATE_BYTES;CHECK(kui_vmu_deferred_io_work(&f.queue)==1);
    mutate_all_commit_bytes(42u);CHECK(f.queue.state==KUI_VMU_DEFERRED_DONE);
    CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,f.expected)==KUI_VMU_EMU_IO_OK);
    CHECK(f.commit_calls==1u && f.queue.state==KUI_VMU_DEFERRED_IDLE);intact();
}
static void strict_abandoned_read(void) {
    setup();uint8_t out[512];memset(out,0xa5,sizeof(out));
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);unchanged_output(out);
    CHECK(kui_vmu_deferred_io_read(&f.queue,43u,out)==KUI_VMU_EMU_IO_BUSY);
    CHECK(kui_vmu_deferred_io_work(&f.queue)==1);CHECK(f.queue.state==KUI_VMU_DEFERRED_DONE);
    /* The documented minimal policy requires retrying read42. Neither a new
     * read nor a commit may discard even this completed, abandoned read. */
    CHECK(kui_vmu_deferred_io_read(&f.queue,43u,out)==KUI_VMU_EMU_IO_BUSY);
    CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,f.expected)==KUI_VMU_EMU_IO_BUSY);
    CHECK(kui_vmu_deferred_io_work(&f.queue)==0);CHECK(f.provider_calls==1u);unchanged_output(out);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_OK);
    CHECK(!memcmp(out,f.media[42],512u));memset(out,0xa5,sizeof(out));
    CHECK(kui_vmu_deferred_io_read(&f.queue,43u,out)==KUI_VMU_EMU_IO_BUSY);unchanged_output(out);
    CHECK(f.queue.ticket==2u && f.queue.block==43u);intact();
}
static void busy_error_and_uncertain_commit(void) {
    setup();stage(42u,f.expected);sync_block(42u,0xfcu);f.busy_count=2u;
    for(unsigned i=0;i<2u;i++) {
        CHECK(kui_vmu_deferred_io_work(&f.queue)==0);sync_block(42u,0xfcu);
        CHECK(f.queue.state==KUI_VMU_DEFERRED_PENDING && f.queue.ticket==1u);
        CHECK(!f.acquisitions && !f.releases && !f.queue.completions);
    }
    CHECK(f.queue.provider_busy==2u);CHECK(kui_vmu_deferred_io_work(&f.queue)==1);sync_block(42u,7u);
    CHECK(f.commit_calls==3u && f.acquisitions==1u && f.queue.completions==1u);intact();
    setup();stage(42u,f.expected);sync_block(42u,0xfcu);
    f.provider_result=KUI_VMU_EMU_IO_ERROR;f.uncertain=true;
    CHECK(kui_vmu_deferred_io_work(&f.queue)==-1);
    CHECK(!memcmp(f.media[42],f.expected,512u)); /* Device commit happened; ACK is forbidden. */
    sync_block(42u,0xfbu);CHECK(response_bytes==8u && response[7]==8u);
    CHECK(!f.protocol.committed_valid && f.protocol.commit_attempted && f.queue.failures==1u);
    unsigned calls=f.provider_calls;kui_vmu_emulation_reset(&f.protocol);
    uint8_t out[512];memset(out,0xa5,sizeof(out));
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_ERROR);unchanged_output(out);
    CHECK(kui_vmu_deferred_io_work(&f.queue)==-1 && f.provider_calls==calls);intact();
    setup();memset(out,0xa5,sizeof(out));CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);
    f.provider_result=2;CHECK(kui_vmu_deferred_io_work(&f.queue)==-1);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_ERROR);unchanged_output(out);
    CHECK(f.queue.state==KUI_VMU_DEFERRED_FAULT && f.queue.failures==1u);intact();
    setup();memset(out,0xa5,sizeof(out));CHECK(kui_vmu_deferred_io_work(&f.queue)==0);
    CHECK(!f.provider_calls && f.queue.state==KUI_VMU_DEFERRED_IDLE);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);
    f.busy_count=1u;CHECK(kui_vmu_deferred_io_work(&f.queue)==0);
    CHECK(!f.acquisitions && f.queue.provider_busy==1u);unchanged_output(out);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);unchanged_output(out);
    CHECK(kui_vmu_deferred_io_work(&f.queue)==1);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_OK);
    CHECK(!memcmp(out,f.media[42],512u) && f.read_calls==2u);intact();
}
static void worker_context_and_guards(void) {
    setup();uint8_t out[512];memset(out,0xa5,sizeof(out));
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);
    struct kui_vmu_deferred_io old=f.queue;
    for(unsigned mode=0;mode<5u;mode++) {
        f.thread=mode!=0u;f.owner=mode!=1u;f.fence=mode==2u;
        f.sr=mode==3u?0x400000f0u:mode==4u?0x50000000u:0x40000000u;
        unsigned enters=f.enters;CHECK(kui_vmu_deferred_io_work(&f.queue)==-2);
        CHECK(!f.provider_calls && f.enters==enters && !memcmp(&old,&f.queue,sizeof(old)));
    }
    f.thread=f.owner=true;f.fence=false;f.sr=0x40000000u;f.deny_enter=true;
    CHECK(kui_vmu_deferred_io_work(&f.queue)==-2);CHECK(!f.provider_calls);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_ERROR);unchanged_output(out);
    CHECK(!memcmp(&old,&f.queue,sizeof(old)));f.deny_enter=false;
    CHECK(kui_vmu_deferred_io_work(&f.queue)==1);CHECK(f.sr==0x40000000u);intact();
    setup();CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);
    f.held=true;CHECK(kui_vmu_deferred_io_work(&f.queue)==-2);CHECK(!f.provider_calls);
    f.held=false;intact();
    /* Losing the publication guard AFTER successful provider I/O cannot
     * authorize an ACK or repeat an uncertain transaction. It stays RUNNING. */
    setup();CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,f.expected)==KUI_VMU_EMU_IO_BUSY);
    f.fail_enter=f.enters+2u;CHECK(kui_vmu_deferred_io_work(&f.queue)==-2);
    CHECK(f.queue.state==KUI_VMU_DEFERRED_RUNNING && f.commit_calls==1u);
    CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,f.expected)==KUI_VMU_EMU_IO_BUSY);
    CHECK(kui_vmu_deferred_io_work(&f.queue)==0 && f.commit_calls==1u);intact();
}
static void invalid_inputs_and_moved_state(void) {
    setup();uint8_t out[512];memset(out,0xa5,sizeof(out));
    struct kui_vmu_deferred_io old=f.queue,moved=f.queue,zero={0};
    struct kui_vmu_emulation_io provider=f.queue.provider;
    struct kui_vmu_deferred_guard guard=f.queue.guard;
    CHECK(kui_vmu_deferred_io_init(NULL,&provider,&guard)==-1);
    CHECK(kui_vmu_deferred_io_init(&zero,NULL,&guard)==-1);
    CHECK(kui_vmu_deferred_io_init(&zero,&provider,NULL)==-1);
    CHECK(kui_vmu_deferred_io_init(&f.queue,&provider,&guard)==-1);
    CHECK(kui_vmu_deferred_io_init(&zero,(const void *)&zero,&guard)==-1);
    CHECK(kui_vmu_deferred_io_init(&zero,&provider,(const void *)&zero)==-1);
    provider.read_block=NULL;CHECK(kui_vmu_deferred_io_init(&zero,&provider,&guard)==-1);
    provider=f.queue.provider;provider.commit_block=NULL;CHECK(kui_vmu_deferred_io_init(&zero,&provider,&guard)==-1);
    provider=f.queue.provider;provider.read_block=kui_vmu_deferred_io_read;
    CHECK(kui_vmu_deferred_io_init(&zero,&provider,&guard)==-1);
    provider=f.queue.provider;provider.commit_block=kui_vmu_deferred_io_commit;
    CHECK(kui_vmu_deferred_io_init(&zero,&provider,&guard)==-1);
    provider=f.queue.provider;guard.enter=NULL;CHECK(kui_vmu_deferred_io_init(&zero,&provider,&guard)==-1);
    guard=f.queue.guard;guard.leave=NULL;CHECK(kui_vmu_deferred_io_init(&zero,&provider,&guard)==-1);
    guard=f.queue.guard;guard.worker_ready=NULL;CHECK(kui_vmu_deferred_io_init(&zero,&provider,&guard)==-1);
    CHECK(kui_vmu_deferred_io_read(NULL,42u,out)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_vmu_deferred_io_read(&f.queue,256u,out)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,NULL)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,NULL)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,f.queue.bytes)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,f.queue.bytes)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,(void *)(UINTPTR_MAX-255u))==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_vmu_deferred_io_read(&moved,42u,out)==KUI_VMU_EMU_IO_ERROR);
    CHECK(kui_vmu_deferred_io_work(&moved)==-2);CHECK(kui_vmu_deferred_io_work(NULL)==-2);
    CHECK(!memcmp(&old,&f.queue,sizeof(old)) && !f.provider_calls);unchanged_output(out);intact();
}
static void identity_corruption_and_ticket_exhaustion(void) {
    const enum provider_action corrupt[]={ACTION_CHANGE_TICKET,ACTION_CHANGE_OPERATION,ACTION_CHANGE_BLOCK,ACTION_CHANGE_STATE};
    for(unsigned i=0;i<sizeof(corrupt)/sizeof(corrupt[0]);i++) {
        setup();CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,f.expected)==KUI_VMU_EMU_IO_BUSY);
        f.action=corrupt[i];CHECK(kui_vmu_deferred_io_work(&f.queue)==-1);
        CHECK(f.queue.state==KUI_VMU_DEFERRED_FAULT && f.queue.failures==1u && f.commit_calls==1u);
        CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,f.expected)==KUI_VMU_EMU_IO_ERROR);intact();
    }
    for(unsigned i=0;i<3u;i++) {
        setup();uint8_t out[512];CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);
        if(i==0u) f.queue.ticket=0u;else if(i==1u) f.queue.operation=3u;else f.queue.block=256u;
        CHECK(kui_vmu_deferred_io_work(&f.queue)==-1 && !f.provider_calls);
        CHECK(f.queue.state==KUI_VMU_DEFERRED_FAULT);intact();
    }
    setup();uint8_t out[512];memset(out,0xa5,sizeof(out));f.queue.ticket=UINT32_MAX-1u;
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);
    CHECK(f.queue.ticket==UINT32_MAX);CHECK(kui_vmu_deferred_io_work(&f.queue)==1);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_OK);
    memset(out,0xa5,sizeof(out));CHECK(kui_vmu_deferred_io_read(&f.queue,43u,out)==KUI_VMU_EMU_IO_ERROR);
    CHECK(f.queue.state==KUI_VMU_DEFERRED_FAULT && f.queue.ticket==UINT32_MAX && f.provider_calls==1u);
    unchanged_output(out);intact();
    setup();f.queue.state=KUI_VMU_DEFERRED_FAULT+1u;
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_ERROR);
    CHECK(f.queue.state==KUI_VMU_DEFERRED_FAULT && f.queue.failures==1u);intact();
}
static void saturating_diagnostics(void) {
    setup();uint8_t out[512];f.queue.submissions=UINT32_MAX;f.queue.completions=UINT32_MAX;
    f.queue.provider_busy=UINT32_MAX;f.queue.failures=UINT32_MAX;
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);
    f.busy_count=1u;CHECK(kui_vmu_deferred_io_work(&f.queue)==0);
    CHECK(kui_vmu_deferred_io_work(&f.queue)==1);
    CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_OK);
    CHECK(f.queue.submissions==UINT32_MAX && f.queue.completions==UINT32_MAX && f.queue.provider_busy==UINT32_MAX);
    CHECK(kui_vmu_deferred_io_read(&f.queue,43u,out)==KUI_VMU_EMU_IO_BUSY);
    f.provider_result=-1;CHECK(kui_vmu_deferred_io_work(&f.queue)==-1);
    CHECK(f.queue.failures==UINT32_MAX);intact();
}
static void owner_fault_latch(void) {
    uint8_t out[512];
    CHECK(kui_vmu_deferred_io_poison(NULL)==-1);
    for(unsigned state=0;state<3u;state++) {
        setup();memset(out,0xa5u,sizeof(out));
        if(state) CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_BUSY);
        if(state==2u) CHECK(kui_vmu_deferred_io_work(&f.queue)==1);
        unsigned calls=f.provider_calls;
        CHECK(!kui_vmu_deferred_io_poison(&f.queue));
        CHECK(f.queue.state==KUI_VMU_DEFERRED_FAULT && f.queue.failures==1u);
        CHECK(!kui_vmu_deferred_io_poison(&f.queue) && f.queue.failures==1u);
        CHECK(kui_vmu_deferred_io_read(&f.queue,42u,out)==KUI_VMU_EMU_IO_ERROR);
        unchanged_output(out);CHECK(kui_vmu_deferred_io_work(&f.queue)==-1);
        CHECK(f.provider_calls==calls);intact();
    }
    setup();CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,f.expected)==KUI_VMU_EMU_IO_BUSY);
    f.action=ACTION_POISON;
    CHECK(kui_vmu_deferred_io_work(&f.queue)==-1);
    CHECK(f.acquisitions==1u&&f.releases==1u&&f.queue.state==KUI_VMU_DEFERRED_FAULT);
    CHECK(kui_vmu_deferred_io_commit(&f.queue,42u,f.expected)==KUI_VMU_EMU_IO_ERROR);
    CHECK(f.queue.completions==0u);intact();
    setup();struct kui_vmu_deferred_io before=f.queue;f.deny_enter=true;
    CHECK(kui_vmu_deferred_io_poison(&f.queue)==-1);CHECK(!memcmp(&before,&f.queue,sizeof(before)));
}
int main(void) {
    protocol_write_read_and_lost_ack();immutable_copy_and_exact_completion();strict_abandoned_read();
    busy_error_and_uncertain_commit();worker_context_and_guards();invalid_inputs_and_moved_state();
    identity_corruption_and_ticket_exhaustion();saturating_diagnostics();owner_fault_latch();
    printf("PASS deferred VMU I/O: %lu checks; protocol retry/durable-result ACK, immutable jobs, worker guards, faults and strict read retry\n",checks);
    return 0;
}
