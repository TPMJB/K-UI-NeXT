/* SPDX-License-Identifier: GPL-3.0-only */
/* Reuse the real session/redo implementation and its sparse FAT fixture.
 * This is a simulated block device, not a physical SCI/CE worker test. */
#define KUI_VMU_PERSIST_SESSION_TEST_MAIN unused_persist_session_main
#include "test_vmu_persist_session.c"
#include "kui/vmu_deferred_io.h"
#include "kui/armada_ce_vmu_write.h"

static struct kui_vmu_deferred_io deferred;
static struct kui_armada_ce_vmu_write endpoint;
static unsigned held,worker_enabled,release_failure;
static uint8_t last_reply[524];
static bool enter(void *ctx,uint32_t *saved) {
    assert(ctx==&held && !held);*saved=held;held=1;return true;
}
static void leave(void *ctx,uint32_t saved) {
    assert(ctx==&held && held && !saved);held=saved;
}
static bool worker_ready(void *ctx) {assert(ctx==&held);return worker_enabled && !held;}
static int provider_read(void *ctx,uint16_t block,uint8_t out[512]) {
    assert(ctx==&media.session && !held && worker_enabled);
    return kui_vmu_persist_session_read(ctx,block,out)?KUI_VMU_EMU_IO_ERROR:KUI_VMU_EMU_IO_OK;
}
static int provider_commit(void *ctx,uint16_t block,const uint8_t data[512]) {
    assert(ctx==&media.session && !held && worker_enabled);
    int result=kui_vmu_persist_session_commit(ctx,block,data);
    if(release_failure) {kui_vmu_persist_session_poison(ctx);return KUI_VMU_EMU_IO_ERROR;}
    return result?KUI_VMU_EMU_IO_ERROR:KUI_VMU_EMU_IO_OK;
}
static void start(void) {
    session_fresh();assert(!session_open(&media.session));
    memset(&deferred,0,sizeof(deferred));memset(&endpoint,0,sizeof(endpoint));
    held=0;worker_enabled=1;release_failure=0;
    struct kui_vmu_deferred_guard guard={&held,enter,leave,worker_ready};
    struct kui_vmu_emulation_io provider={&media.session,provider_read,provider_commit};
    assert(!kui_vmu_deferred_io_init(&deferred,&provider,&guard));
    struct kui_vmu_emulation_io io={&deferred,kui_vmu_deferred_io_read,kui_vmu_deferred_io_commit};
    assert(!kui_armada_ce_vmu_write_init(&endpoint,&io));
}
static unsigned packet(unsigned command,unsigned phase,unsigned block,const uint8_t *data) {
    uint8_t request[140]={0},response[524];size_t bytes=0;
    request[0]=(uint8_t)command;request[1]=2;request[3]=command==12?34:2;
    request[7]=2;request[9]=(uint8_t)phase;request[11]=(uint8_t)block;
    if(data) memcpy(request+12,data,128);
    assert(kui_armada_ce_vmu_write_handle(&endpoint,request,command==12?140:12,
        response,sizeof(response),&bytes)==KUI_ARMADA_CE_VMU_WRITE_HANDLED);
    assert(bytes==4 || bytes==8 || bytes==524);
    memcpy(last_reply,response,bytes);return response[0];
}
static void stage(const uint8_t data[512]) {
    for(unsigned phase=0;phase<4;phase++) assert(packet(12,phase,123,data+phase*128)==7);
    assert(!media.writes && !media.syncs && !deferred.submissions);
    assert(packet(13,4,123,NULL)==0xfc);
    assert(!media.writes && !media.syncs && deferred.submissions==1);
}
static void successful_commit(void) {
    uint8_t data[512],out[512];for(unsigned i=0;i<512;i++) data[i]=(uint8_t)(i*7u+29u);
    start();stage(data);
    assert(packet(13,4,123,NULL)==0xfc && !media.writes);
    worker_enabled=0;assert(kui_vmu_deferred_io_work(&deferred)==-2 && !media.writes);
    worker_enabled=1;assert(kui_vmu_deferred_io_work(&deferred)==1);
    assert(media.writes==4 && media.syncs==4 && !memcmp(media.base[123],data,512));
    assert(!endpoint.commits); /* Worker completion alone has not ACKed Maple. */
    assert(packet(13,4,123,NULL)==7 && endpoint.commits==1);
    unsigned writes=media.writes;
    assert(packet(13,4,123,NULL)==7 && media.writes==writes && deferred.submissions==1);
    assert(packet(11,0,123,NULL)==0xfc);
    assert(kui_vmu_deferred_io_work(&deferred)==1);
    assert(packet(11,0,123,NULL)==8 && !memcmp(last_reply+12,data,512));
    assert(packet(13,0,123,NULL)==7);
    /* Retire the old exclusive owner before fresh admission/recovery. */
    kui_vmu_persist_session_poison(&media.session);
    media.refuse_first_write_unless_admitted=0;
    assert(!session_open(&media.reopened));
    assert(!kui_vmu_persist_session_read(&media.reopened,123,out) && !memcmp(out,data,512));
}
static void interrupted_commits(void) {
    uint8_t data[512],old[512],out[512];memset(data,0x6b,sizeof(data));
    for(unsigned kind=0;kind<2;kind++) for(unsigned point=1;point<=4;point++) {
        start();memcpy(old,media.base[123],512);stage(data);
        if(kind) media.fail_sync=point;else media.fail_write=point;
        assert(kui_vmu_deferred_io_work(&deferred)==-1);
        assert(deferred.state==KUI_VMU_DEFERRED_FAULT && !endpoint.commits);
        assert(packet(13,4,123,NULL)==0xfb && !endpoint.commits);
        unsigned writes=media.writes;
        assert(kui_vmu_deferred_io_work(&deferred)==-1 && media.writes==writes);
        media.fail_sync=media.fail_write=0;media.refuse_first_write_unless_admitted=0;
        assert(!session_open(&media.reopened));
        assert(!kui_vmu_persist_session_read(&media.reopened,123,out));
        assert(!memcmp(out,old,512) || !memcmp(out,data,512));
    }
    start();stage(data);release_failure=1;
    assert(kui_vmu_deferred_io_work(&deferred)==-1);
    assert(!memcmp(media.base[123],data,512)); /* Persisted, but not ACKed. */
    assert(packet(13,4,123,NULL)==0xfb && !endpoint.commits && media.session.faulted);
}
int main(void) {
    successful_commit();interrupted_commits();
    puts("Deferred CE protocol + persistence: commit/reopen, 8 journal cuts, release-failure no-ACK passed");
    return 0;
}
