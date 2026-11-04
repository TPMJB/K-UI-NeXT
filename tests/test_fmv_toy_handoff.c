/* SPDX-License-Identifier: GPL-3.0-only */
#include "fmv_toy_profile.h"
#define KUI_TOY_LOADER_SCHEDULE_HOST_TEST 1
#include "../src/loader/toy_pilot_gd.c"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static struct kui_toy_pilot_snapshot audio;
static struct kui_retail_gd gd;
static bool enabled,idle,quiesce_ok,accept;
static unsigned rearmed,pumped,mailbox_calls,sequence;
static uint32_t mailbox_result;
bool kui_toy_pilot_fmv_data_idle(void) {return idle;}
bool kui_toy_pilot_sci_startup_active(void) {return enabled;}
bool kui_toy_pilot_sci_fmv_rearm(void) {++rearmed;enabled=true;return true;}
int kui_toy_pilot_sci_pump(struct kui_retail_gd *s) {(void)s;++pumped;return 1;}
int kui_toy_pilot_sci_service(struct kui_retail_gd *s) {(void)s;return 1;}
void kui_toy_pilot_sci_cancel(struct kui_retail_gd *s) {(void)s;}
bool kui_toy_pilot_sci_quiesce(void) {sequence=sequence*10u+1u;return quiesce_ok;}
void kui_toy_pilot_sci_retire(void) {sequence=sequence*10u+4u;enabled=false;}
void kui_toy_loader_schedule_host_play(void) {sequence=sequence*10u+3u;}
uint32_t kui_toy_pilot_request(uint32_t c,uint32_t a,uint32_t b,uint32_t d) {
    (void)c;(void)a;(void)b;(void)d;++mailbox_calls;sequence=sequence*10u+2u;return mailbox_result;
}
const struct kui_toy_pilot_snapshot *kui_toy_pilot_snapshot(void) {return &audio;}
void kui_toy_loader_trace_begin(const struct kui_retail_gd *s,uint32_t f,uint32_t a,uint32_t b) {
    (void)s;(void)f;(void)a;(void)b;
}
void kui_toy_loader_trace_end(const struct kui_retail_gd *s,uint32_t f,uint32_t a,uint32_t b,int32_t r) {
    (void)s;(void)f;(void)a;(void)b;(void)r;
}
const uint32_t *kui_toy_loader_trace_words(void) {static uint32_t words[8];return words;}
void kui_toy_loader_data_probe_begin(struct kui_retail_gd *s,uint32_t f,uint32_t p) {(void)s;(void)f;(void)p;}
void kui_toy_loader_data_probe_end(struct kui_retail_gd *s) {(void)s;}
int32_t kui_retail_gd_dispatch(struct kui_retail_gd *s,uint32_t a,uint32_t b,uint32_t c,uint32_t f) {
    (void)s;(void)a;(void)b;(void)c;(void)f;return 0;
}
static int32_t base(uint32_t a,uint32_t b,uint32_t c,uint32_t f) {
    (void)b;(void)c;
    if(f!=KUI_GD_REQUEST || !accept)return 0;
    gd.command=a;gd.pending=1u;gd.token=7u;return 7;
}
static void reset(void) {
    memset(&gd,0,sizeof(gd));memset(&audio,0,sizeof(audio));
    gd.sector_bytes=KUI_GAME_DATA_BYTES;
    enabled=false;idle=quiesce_ok=accept=true;
    rearmed=pumped=mailbox_calls=sequence=0u;mailbox_result=9u;
}
static int32_t request_command(uint32_t command) {
    return kui_toy_pilot_gd_dispatch(&gd,command,0u,KUI_GD_REQUEST,(uintptr_t)base);
}
int main(void) {
    reset();assert(request_command(KUI_GD_DMAREAD)==7 && rearmed==1u && pumped==1u);
    reset();idle=false;request_command(KUI_GD_PIOREAD);assert(!rearmed && !pumped);
    reset();accept=false;request_command(KUI_GD_DMAREAD);assert(!rearmed && !pumped);
    reset();gd.sector_bytes=2352u;request_command(KUI_GD_DMAREAD);assert(!rearmed && !pumped);
    reset();enabled=true;request_command(KUI_GD_DMAREAD);assert(!rearmed && pumped==1u);
    reset();request_command(KUI_RETAIL_GD_REQ_STAT);assert(!rearmed && !pumped);
    reset();request_command(KUI_GD_DMAREAD);
    gd.command=gd.pending=0u;assert(enabled);
    request_command(KUI_RETAIL_GD_PLAY);assert(sequence==1234u && !enabled && mailbox_calls==1u);
    reset();enabled=true;
    request_command(KUI_RETAIL_GD_RELEASE);assert(sequence==124u && !enabled && mailbox_calls==1u);
    reset();enabled=true;quiesce_ok=false;
    request_command(KUI_RETAIL_GD_RELEASE);assert(sequence==1u && !mailbox_calls && gd.error && !gd.pending);
    reset();enabled=true;quiesce_ok=false;
    request_command(KUI_RETAIL_GD_PLAY);assert(sequence==1u && !mailbox_calls && gd.error && !gd.pending);
    reset();enabled=true;accept=false;
    request_command(KUI_RETAIL_GD_PLAY);assert(!sequence && !mailbox_calls && enabled);
    reset();enabled=true;mailbox_result=0u;
    request_command(KUI_RETAIL_GD_PLAY);assert(sequence==12u && enabled && gd.error && !gd.pending);
    puts("PASS: actual GD adapter rearms accepted DATA only and fences/retire-orders PLAY and resume");
}
