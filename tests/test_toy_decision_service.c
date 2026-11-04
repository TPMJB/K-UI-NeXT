/* SPDX-License-Identifier: GPL-3.0-only */
#include "fmv_toy_profile.h"
#undef KUI_TOY_FMV_IDLE_REARM
#define KUI_TOY_FMV_IDLE_REARM 0
#ifndef KUI_TOY_DECISION_PROFILE
#define KUI_TOY_DECISION_PROFILE 2
#endif
#define KUI_TOY_PILOT_SCI_TEST 1
#include "../src/loader/toy_pilot_sci.c"
#include <assert.h>
#include <stdio.h>

/* Execute the real service/data-work loops with a delayed receiver. The
 * model intentionally supplies no IRQ: later foreground calls must finish
 * every checked block and preserve the owned handle. It is not a timer or
 * hardware-throughput simulation. */
static uint32_t model_vbr,model_clock,priority,fetches,feeds,polls,stops;
static uint32_t pending_polls,block_lba,ready_lba,read_delay=3u,clock_stride=1u,clock_reads,clock_hash,crc_failures;
static bool token_stop;
static bool model_busy,model_ready,defer_repair;
static enum kui_sci_stream_result fail_poll;
static uint8_t payload[512];
static struct kui_sci_stream_stats flow;
static struct kui_retail_gd gd;
const uint32_t kui_toy_pilot_sci_forward[3]={0},kui_toy_pilot_sci_interrupt[11]={0};
void kui_toy_pilot_sci_release_100(void) {}
void kui_toy_pilot_sci_release_400(void) {}
void kui_toy_pilot_sci_release_600(void) {}
uint32_t kui_toy_pilot_sci_test_vbr(void) {return model_vbr;}
void kui_toy_pilot_sci_test_set_vbr(uint32_t v) {model_vbr=v;}
uint16_t kui_toy_pilot_sci_test_read16(uint32_t a) {(void)a;return (uint16_t)priority;}
void kui_toy_pilot_sci_test_write16(uint32_t a,uint16_t v) {(void)a;priority=v;}
uint32_t kui_toy_pilot_sci_test_ticks(void) {
    ++clock_reads;clock_hash=clock_hash*33u+feeds*11u+polls*7u+fetches;
    uint32_t result=model_clock;model_clock+=clock_stride;return result;
}
bool kui_sci_stream_busy(void) {return model_busy;}
bool kui_sci_stream_ready(uint32_t lba) {return model_ready && ready_lba==lba;}
bool kui_sci_stream_token_pending(void) {return token_stop;}
void kui_sci_stream_token_budget(bool bounded) {(void)bounded;}
const struct kui_sci_stream_stats *kui_sci_stream_stats(void) {return &flow;}
enum kui_sci_stream_result kui_sci_stream_poll(bool interrupt) {
    (void)interrupt;++polls;
    if(fail_poll) {
        enum kui_sci_stream_result result=fail_poll;
        fail_poll=KUI_SCI_STREAM_OK;model_busy=false;return result;
    }
    if(defer_repair) {defer_repair=false;model_busy=false;return KUI_SCI_STREAM_PENDING;}
    if(pending_polls) {--pending_polls;return KUI_SCI_STREAM_PENDING;}
    model_busy=false;model_ready=true;ready_lba=block_lba;
    return KUI_SCI_STREAM_OK;
}
enum kui_sci_stream_result kui_sci_stream_fetch(uint32_t lba,uint32_t limit,bool polled) {
    (void)limit;(void)polled;assert(!model_busy);
    block_lba=lba;pending_polls=read_delay;++fetches;
    if(polled) {model_busy=false;model_ready=true;ready_lba=lba;++flow.polled;}
    else model_busy=true;
    return KUI_SCI_STREAM_OK;
}
const uint8_t *kui_sci_stream_take(uint32_t lba,enum kui_sci_stream_result *result) {
    if(!kui_sci_stream_ready(lba)) {*result=KUI_SCI_STREAM_PENDING;return NULL;}
    model_ready=false;
    if(crc_failures) {--crc_failures;++flow.crc_errors;*result=KUI_SCI_STREAM_CRC;return NULL;}
    *result=KUI_SCI_STREAM_OK;return payload;
}
enum kui_sci_stream_result kui_sci_stream_stop(void) {
    model_busy=false;++stops;return KUI_SCI_STREAM_OK;
}
void kui_sci_stream_discard(void) {model_ready=false;}
enum kui_sci_stream_result kui_sci_stream_open(const struct kui_loader_sd *s,
    uint8_t *a,uint8_t *b,bool unknown) {
    (void)s;(void)a;(void)b;(void)unknown;return KUI_SCI_STREAM_OK;
}
enum kui_loader_sd_result acquire(void) {return KUI_LOADER_SD_OK;}
void release(void) {}
enum kui_game_result kui_retail_cursor_begin(struct kui_retail_cursor *c,
    const struct kui_retail_manifest *m,uint32_t lba,uint32_t count,
    enum kui_game_sector_format format,kui_retail_cursor_write write,void *context) {
    (void)c;(void)m;(void)lba;(void)count;(void)format;(void)write;(void)context;
    return KUI_GAME_OK;
}
enum kui_game_result kui_retail_cursor_feed(struct kui_retail_cursor *c,const uint8_t bytes[512]) {
    assert(bytes==payload);++feeds;++c->block;c->run=1u;
    if(!(feeds%4u))++c->done;
    return KUI_GAME_OK;
}
void kui_retail_gd_progress(struct kui_retail_gd *s,uint32_t sectors,uint32_t error) {
    assert(s==&gd && sectors<=s->count);
    s->completed_bytes=sectors*2048u;
    if(error || sectors==s->count) {s->pending=0;s->error=error;}
}
static void reset(void) {
    memset(&e,0,sizeof(e));
    memset(&kui_toy_pilot_sci_release,0,sizeof(kui_toy_pilot_sci_release));
    memset(&flow,0,sizeof(flow));memset(&gd,0,sizeof(gd));
    gd.pending=1u;gd.token=7u;gd.command=KUI_GD_DMAREAD;gd.lba=150u;
    gd.count=2u;gd.sector_bytes=2048u;gd.destination=0x8c100000u;gd.request_bytes=4096u;
    e.startup.enabled=e.active=e.opened=e.configured=1u;e.service=&gd;
    e.token=gd.token;e.command=gd.command;e.lba=gd.lba;e.count=gd.count;
    e.destination=gd.destination;e.sector_bytes=gd.sector_bytes;
    e.cursor.count=gd.count;e.cursor.block=10u;e.cursor.run=1u;
    e.acquire=acquire;e.release=release;
    model_vbr=model_clock=priority=fetches=feeds=polls=stops=0u;
    model_busy=model_ready=defer_repair=false;fail_poll=KUI_SCI_STREAM_OK;
    kui_toy_pilot_sci_irq_fault=0u;read_delay=3u;clock_stride=1u;
    clock_reads=clock_hash=crc_failures=0u;token_stop=false;
}
static void finish(void) {
    for(unsigned visit=0u;gd.pending && visit<100u;++visit) {
        int result=kui_toy_pilot_sci_service(&gd);
        assert(result==KUI_TOY_SCI_OK || result==KUI_TOY_SCI_PENDING);
    }
    assert(!gd.pending && gd.completed_bytes==4096u && feeds==8u && stops==1u);
    assert(!e.active && !model_busy && !e.opened);
    assert(e.budget.block_exits+e.budget.time_exits+e.budget.token_exits+
        e.budget.step_exits+e.budget.finished_or_inactive_exits+
        0u==e.budget.service_calls);
}
#if KUI_TOY_DECISION_PROFILE
static struct kui_toy_decision_service captured;
int kui_toy_decision_service_active(void) {return KUI_TOY_DECISION_PROFILE==2;}
void kui_toy_decision_service_commit(const struct kui_toy_decision_service *v) {
    captured=*v;
    assert(v->calls==1u && v->wait_ticks+v->credit_ticks+v->other_ticks==v->elapsed_ticks);
}
#endif
static void emit(unsigned id) {
    printf("%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u:%u\n",id,gd.completed_bytes,
        e.stats.call_blocks,e.stats.retries,e.stats.errors,fetches,feeds,polls,
        clock_reads,clock_hash,flow.polled,gd.error);
}
int main(void) {
    reset();finish();emit(0u);
#if KUI_TOY_DECISION_PROFILE==2
    assert(captured.wait_steps && captured.credit_steps && captured.wait_ticks);
#endif
    reset();crc_failures=2u;finish();assert(e.stats.retries==2u && flow.polled);emit(1u);
    reset();assert(kui_sci_stream_fetch(10u,65536u,false)==KUI_SCI_STREAM_OK);
    fail_poll=KUI_SCI_STREAM_OVERRUN;finish();assert(e.stats.retries==1u);emit(2u);
    reset();assert(kui_sci_stream_fetch(10u,65536u,false)==KUI_SCI_STREAM_OK);
    defer_repair=true;finish();assert(fetches==9u);emit(3u);
    reset();e.verified_since=KUI_TOY_SCI_SERVICE_BLOCKS;
    assert(kui_toy_pilot_sci_service(&gd)==KUI_TOY_SCI_PENDING && e.budget.block_exits==1u);
    finish();emit(4u);
    reset();read_delay=20000u;
    assert(kui_toy_pilot_sci_service(&gd)==KUI_TOY_SCI_PENDING && e.budget.step_exits==1u);
    read_delay=pending_polls=0u;finish();emit(5u);
    reset();read_delay=20000u;clock_stride=5000u;
    assert(kui_toy_pilot_sci_service(&gd)==KUI_TOY_SCI_PENDING && e.budget.time_exits==1u);
    read_delay=pending_polls=0u;clock_stride=1u;finish();emit(6u);
    reset();token_stop=true;
    assert(kui_toy_pilot_sci_service(&gd)==KUI_TOY_SCI_PENDING && e.budget.token_exits==1u);
    token_stop=false;finish();emit(7u);
    const enum kui_sci_stream_result faults[]={KUI_SCI_STREAM_BUSY,KUI_SCI_STREAM_RESET};
    for(unsigned i=0;i<2u;++i) {
        reset();assert(kui_sci_stream_fetch(10u,65536u,false)==KUI_SCI_STREAM_OK);
        fail_poll=faults[i];assert(kui_toy_pilot_sci_service(&gd)==KUI_TOY_SCI_FAULT);
        assert(gd.error==KUI_GD_ERROR_IO && !gd.pending && !e.active && !model_busy && !feeds);
        emit(8u+i);
    }
    reset();assert(kui_sci_stream_fetch(10u,65536u,false)==KUI_SCI_STREAM_OK);
    kui_toy_pilot_sci_cancel(&gd);assert(!model_busy && !e.active && !e.opened && gd.pending);
    emit(10u);
    reset();assert(kui_sci_stream_fetch(10u,65536u,false)==KUI_SCI_STREAM_OK);
    assert(kui_toy_pilot_sci_quiesce() && !model_busy && !e.active && !e.opened);
    kui_toy_pilot_sci_retire();uint32_t calls=e.stats.calls;
    assert(kui_toy_pilot_sci_service(&gd)==KUI_TOY_SCI_OK && e.stats.calls==calls);
    emit(11u);
}
