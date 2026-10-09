/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_loader_trace.h"
#include "kui/toy_pilot_clock.h"
#if KUI_TOY_PILOT_LOADER_TRACE

/* High resident BSS only: the low firmware reservation has no free space. */
static struct kui_toy_loader_trace_report report;
static struct trace_visit {
    uint32_t initialized, call_active, nested_depth, phase, function, r4, r5;
    uint32_t tick, valid, epoch, token, command, pending, credited, sectors;
    uint32_t last_end, last_valid, last_epoch, last_exists;
    uint32_t pvr_valid, line, period, vbi, framebuffer;
    uint32_t previous_valid, previous_lba, previous_end;
} visit;
static struct trace_request {
    uint32_t active, phase, token, lba, sectors, destination, credited;
    uint32_t start, valid, epoch, first_seen, terminal_seen, terminal_tick;
    uint32_t terminal_valid, terminal_epoch, retained_slot;
    struct kui_toy_loader_trace_record record;
} request;

#ifdef KUI_TOY_LOADER_TRACE_HOST_TEST
#define READ(a,n) kui_toy_loader_trace_host_read((a),(n))
#else
static uint32_t mmio(uint32_t address,unsigned bytes) {
    if(bytes==1u) return *(volatile const uint8_t *)(uintptr_t)address;
    if(bytes==2u) return *(volatile const uint16_t *)(uintptr_t)address;
    return *(volatile const uint32_t *)(uintptr_t)address;
}
#define READ(a,n) mmio((a),(n))
#endif

static void add(uint32_t *value,uint32_t amount) {
    if(amount>UINT32_MAX-*value) {
        *value=UINT32_MAX;report.counters_saturated=1u;
    } else *value+=amount;
}
static void initialize(void) {
    if(visit.initialized) return;
    visit.initialized=1u;
    report.magic=KUI_TOY_LOADER_TRACE_MAGIC;
    report.version=KUI_TOY_LOADER_TRACE_VERSION;
    report.words=KUI_TOY_LOADER_TRACE_WORDS;
    report.phase_words=KUI_TOY_LOADER_TRACE_PHASE_WORDS;
    report.record_words=8u;report.worst_per_phase=8u;
    report.tick_hz=KUI_TOY_LOADER_TRACE_TICK_HZ;
}
static uint32_t clock_valid(void) {
    return kui_toy_pilot_clock_profile(READ(0xffc00000u,2u),
        READ(0xffd80008u,4u),READ(0xffd80010u,2u),READ(0xffd80004u,1u));
}
static uint32_t clock_sample(uint32_t *tick) {
    uint32_t valid=clock_valid();
    *tick=READ(0xffd8000cu,4u);
    if(!valid) add(&report.clock_epoch,1u);
    return valid;
}
static uint32_t interval(uint32_t start,uint32_t start_valid,uint32_t epoch,
    uint32_t end,uint32_t end_valid,uint32_t *elapsed) {
    *elapsed=start-end; /* Down-counter: handles one ordinary counter wrap. */
    return start_valid && end_valid && report.clock_epoch!=UINT32_MAX && epoch==report.clock_epoch &&
        *elapsed<=KUI_TOY_LOADER_TRACE_MAX_INTERVAL;
}
static unsigned timing_bin(uint32_t ticks) {
    static const uint32_t ceilings[7]={782u,1563u,3125u,6250u,12500u,25000u,50000u};
    unsigned bin=0;while(bin<7u && ticks>ceilings[bin]) ++bin;return bin;
}
static unsigned size_bin(uint32_t sectors) {
    unsigned bin=0,ceiling=1u;
    while(bin<7u && sectors>ceiling) {++bin;ceiling<<=1;}
    return bin;
}
static void metric(struct kui_toy_loader_trace_metric *m,uint32_t ticks) {
    if(!m->samples || ticks<m->ticks_min) m->ticks_min=ticks;
    if(ticks>m->ticks_max) m->ticks_max=ticks;
    add(&m->samples,1u);add(&m->ticks_total,ticks);
    add(&m->histogram[timing_bin(ticks)],1u);
}
static void measured(struct kui_toy_loader_trace_phase *p,
    struct kui_toy_loader_trace_metric *m,uint32_t start,uint32_t valid,
    uint32_t epoch,uint32_t tick,uint32_t end_valid,uint32_t *out,uint32_t *flag,
    uint32_t flag_bit) {
    uint32_t elapsed;
    if(interval(start,valid,epoch,tick,end_valid,&elapsed)) {
        metric(m,elapsed);
        if(out) *out=elapsed;
        if(flag) *flag|=flag_bit;
    } else add(&p->invalid_intervals,1u);
}
static int data(uint32_t command) {
    return command==KUI_GD_PIOREAD || command==KUI_GD_DMAREAD;
}
static void pvr_sample(struct kui_toy_loader_trace_phase *p) {
    uint32_t line=READ(0xa05f810cu,4u)&0x3ffu;
    uint32_t vbi=READ(0xa05f80ccu,4u)&0x3ffu;
    uint32_t period=((READ(0xa05f80d8u,4u)>>16)&0x3ffu)+1u;
    uint32_t framebuffer=READ(0xa05f8050u,4u);
    if(period<64u || line>=period || vbi>=period) {
        add(&p->invalid_pvr_samples,1u);visit.pvr_valid=0u;return;
    }
    if(visit.pvr_valid) {
        if(period!=visit.period || vbi!=visit.vbi)
            add(&p->pvr_geometry_changes,1u);
        else if(line<visit.line) add(&p->sampled_line_wraps,1u);
        if(framebuffer!=visit.framebuffer) add(&p->sampled_fb_changes,1u);
    }
    visit.pvr_valid=1u;visit.line=line;visit.period=period;
    visit.vbi=vbi;visit.framebuffer=framebuffer;
}
static void outstanding(void) {
    if(!request.active) return;
    struct kui_toy_loader_trace_phase *p=&report.phase[request.phase];
    p->outstanding_token=request.token;p->outstanding_lba=request.lba;
    p->outstanding_sectors=request.sectors;p->outstanding_destination=request.destination;
    p->outstanding_credited_bytes=request.credited;
    p->outstanding_started_tick=request.start;
    p->outstanding_clock_valid=request.valid && request.epoch==report.clock_epoch;
    p->outstanding_terminal_observed=request.terminal_seen;
}
static void retain(void) {
    struct kui_toy_loader_trace_phase *p=&report.phase[request.phase];
    add(&p->worst_seen,1u);request.retained_slot=UINT32_MAX;
    if(!(request.record.flags&KUI_TOY_LOADER_TRACE_COMPLETE_VALID)) return;
    add(&p->worst_valid,1u);
    unsigned slot;
    if(p->worst_retained<KUI_TOY_LOADER_TRACE_WORST) {
        slot=p->worst_retained;add(&p->worst_retained,1u);
    } else {
        slot=0u;
        for(unsigned i=1u;i<KUI_TOY_LOADER_TRACE_WORST;i++)
            if(p->worst[i].complete_ticks<p->worst[slot].complete_ticks) slot=i;
        add(&p->worst_excluded,1u);
        if(request.record.complete_ticks<=p->worst[slot].complete_ticks) return;
    }
    p->worst[slot]=request.record;request.retained_slot=slot;
}
static void submit(const struct kui_retail_gd *s) {
    struct kui_toy_loader_trace_phase *p=&report.phase[visit.phase];
    if(request.active) {add(&report.phase[request.phase].ownership_lost,1u);request.active=0u;}
    add(&p->accepted_data,1u);add(&p->requested_sectors,s->count);
    add(&p->requested_bytes,s->request_bytes);
    add(&p->request_size_histogram[size_bin(s->count)],1u);
    if(s->count>p->peak_request_sectors) p->peak_request_sectors=s->count;
    if(s->request_bytes>p->peak_request_bytes) p->peak_request_bytes=s->request_bytes;
    if(visit.previous_valid) {
        if(s->lba==visit.previous_end) add(&p->sequential,1u);
        else if(s->count<=UINT32_MAX-s->lba && s->lba<visit.previous_end &&
                s->lba+s->count>visit.previous_lba)
            add(&p->overlapping,1u);
        else if(s->lba>visit.previous_end) add(&p->forward_seek,1u);
        else add(&p->backward_seek,1u);
    }
    visit.previous_valid=s->count<=UINT32_MAX-s->lba;
    visit.previous_lba=s->lba;visit.previous_end=s->lba+s->count;
    request.active=1u;request.phase=visit.phase;request.token=s->token;
    request.lba=s->lba;request.sectors=s->count;request.destination=s->destination;
    request.start=visit.tick;request.valid=visit.valid;request.epoch=visit.epoch;
    request.credited=0u;request.first_seen=0u;request.terminal_seen=0u;
    request.retained_slot=UINT32_MAX;
    request.record=(struct kui_toy_loader_trace_record){s->token,s->lba,s->count,s->destination,
        UINT32_MAX,UINT32_MAX,UINT32_MAX,0u};
}

void kui_toy_loader_trace_begin(const struct kui_retail_gd *s,uint32_t function,
    uint32_t r4,uint32_t r5) {
    initialize();if(report.frozen) return;
    if(visit.call_active) {add(&report.nested_begins,1u);add(&visit.nested_depth,1u);return;}
    visit.call_active=1u;visit.phase=report.active_phase;
    visit.function=function;visit.r4=r4;visit.r5=r5;
    visit.valid=clock_sample(&visit.tick);visit.epoch=report.clock_epoch;
    struct kui_toy_loader_trace_phase *p=&report.phase[visit.phase];
    if(!report.total_call_samples) {report.first_tick=visit.tick;report.first_clock_valid=visit.valid;}
    add(&report.total_call_samples,1u);add(&p->calls,1u);
    if(!visit.valid) add(&p->invalid_clock_calls,1u);
    if(function==KUI_GD_REQUEST) add(&p->request_calls,1u);
    if(function==KUI_GD_EXEC) add(&p->exec_calls,1u);
    if(function==KUI_GD_CHECK) add(&p->check_calls,1u);
    if(function==KUI_GD_DRIVE) add(&p->drive_calls,1u);
    visit.token=s->token;visit.command=s->command;visit.pending=s->pending;
    visit.credited=s->completed_bytes;visit.sectors=s->diag.sectors_read;
    if(request.active && s->pending && data(s->command) && s->token==request.token && visit.last_exists)
        measured(p,&p->pending_service_gap,visit.last_end,visit.last_valid,visit.last_epoch,
            visit.tick,visit.valid,NULL,NULL,0u);
    pvr_sample(p);
}

void kui_toy_loader_trace_end(const struct kui_retail_gd *s,uint32_t function,
    uint32_t r4,uint32_t r5,int32_t result) {
    initialize();if(report.frozen) return;
    if(visit.nested_depth) {--visit.nested_depth;return;}
    if(!visit.call_active || visit.function!=function || visit.r4!=r4 || visit.r5!=r5) {
        add(&report.unmatched_ends,1u);return;
    }
    uint32_t tick,valid=clock_sample(&tick);
    struct kui_toy_loader_trace_phase *p=&report.phase[visit.phase];
    if(visit.valid && !valid) add(&p->invalid_clock_calls,1u);
    measured(p,&p->call_body,visit.tick,visit.valid,visit.epoch,tick,valid,NULL,NULL,0u);
    if(data(visit.command) && (function==KUI_GD_EXEC || function==KUI_GD_CHECK))
        measured(p,&p->data_service_body,visit.tick,visit.valid,visit.epoch,tick,valid,NULL,NULL,0u);
    if(function==KUI_GD_REQUEST && data(r4)) {
        if(result>0 && s->pending && data(s->command) && s->token==(uint32_t)result) submit(s);
        else add(&p->rejected_data,1u);
    }
    if(request.active && request.token==visit.token && data(visit.command)) {
        struct kui_toy_loader_trace_phase *rp=&report.phase[request.phase];
        uint32_t credited=s->completed_bytes>=request.credited?s->completed_bytes-request.credited:0u;
        /* RESET can reuse/change counters: only the same owned handle may
         * contribute credits, including a CHECK that just consumed it. */
        if(s->token==request.token && credited && function!=KUI_GD_INIT && function!=KUI_GD_RESET) {
            add(&rp->delivered_bytes,credited);
            add(&rp->delivered_sectors,s->diag.sectors_read-visit.sectors);
            request.credited=s->completed_bytes;
            if(function==KUI_GD_EXEC) add(&rp->progress_exec,1u);
            if(function==KUI_GD_CHECK) add(&rp->progress_check,1u);
            if(!request.first_seen) {
                request.first_seen=1u;
                measured(rp,&rp->request_first_credit,request.start,request.valid,request.epoch,
                    tick,valid,&request.record.first_credit_ticks,&request.record.flags,
                    KUI_TOY_LOADER_TRACE_FIRST_VALID);
            }
        } else if(function==KUI_GD_EXEC && visit.pending) add(&rp->zero_progress_exec,1u);
        if(function==KUI_GD_INIT || function==KUI_GD_RESET) {
            add(&rp->data_reset,1u);request.active=0u;visit.previous_valid=0u;
        } else {
            if(!request.terminal_seen && s->token==request.token && !s->pending &&
               (s->status==KUI_GD_COMPLETED || s->status==KUI_GD_FAILED)) {
                request.terminal_seen=1u;request.terminal_tick=tick;
                request.terminal_valid=valid;request.terminal_epoch=report.clock_epoch;
                if(s->error || s->status==KUI_GD_FAILED) {
                    add(&rp->data_failed,1u);request.record.flags|=KUI_TOY_LOADER_TRACE_FAILED;
                } else {add(&rp->data_completed,1u);request.record.flags|=KUI_TOY_LOADER_TRACE_SUCCESS;}
                if(function==KUI_GD_ABORT && r4==request.token && result==0) {
                    add(&rp->data_aborted,1u);request.record.flags|=KUI_TOY_LOADER_TRACE_ABORTED;
                }
                measured(rp,&rp->request_complete,request.start,request.valid,request.epoch,
                    tick,valid,&request.record.complete_ticks,&request.record.flags,
                    KUI_TOY_LOADER_TRACE_COMPLETE_VALID);
                retain();
            }
            if(function==KUI_GD_CHECK && r4==request.token && request.terminal_seen &&
               !s->command && !s->pending &&
               (result==KUI_GD_COMPLETED || result==KUI_GD_FAILED)) {
                add(&rp->data_acknowledged,1u);
                measured(rp,&rp->complete_acknowledge,request.terminal_tick,
                    request.terminal_valid,request.terminal_epoch,tick,valid,
                    &request.record.acknowledge_ticks,&request.record.flags,KUI_TOY_LOADER_TRACE_ACK_VALID);
                if(request.retained_slot<KUI_TOY_LOADER_TRACE_WORST)
                    rp->worst[request.retained_slot]=request.record;
                request.active=0u;
            } else if(s->token!=request.token || (!s->command && !request.terminal_seen)) {
                add(&rp->ownership_lost,1u);request.active=0u;
            }
        }
    }
    if(function==KUI_GD_REQUEST && result>0 &&
       (r4==KUI_RETAIL_GD_PLAY || r4==KUI_RETAIL_GD_PLAY2) &&
       s->pending && s->command==r4 && s->token==(uint32_t)result && s->count && !s->error) {
        add(&report.accepted_play_requests,1u);
        if(!report.active_phase) {
            outstanding();report.boundary_command=r4;report.boundary_token=(uint32_t)result;
            report.boundary_generation=s->count;report.boundary_tick=tick;
            report.boundary_clock_valid=valid;report.active_phase=1u;
            visit.previous_valid=0u;visit.pvr_valid=0u;
        }
    }
    if(function==KUI_GD_INIT || function==KUI_GD_RESET) visit.previous_valid=0u;
    visit.last_end=tick;visit.last_valid=valid;visit.last_epoch=report.clock_epoch;visit.last_exists=1u;
    visit.call_active=0u;
}

const uint32_t *kui_toy_loader_trace_words(void) {
    initialize();return (const uint32_t *)(const void *)&report;
}
uint32_t kui_toy_loader_trace_word_count(void) {return KUI_TOY_LOADER_TRACE_WORDS;}
void kui_toy_loader_trace_freeze(void) {initialize();outstanding();report.frozen=1u;}
#ifdef KUI_TOY_LOADER_TRACE_HOST_TEST
void kui_toy_loader_trace_host_reset(void) {
    report=(struct kui_toy_loader_trace_report){0};
    visit=(struct trace_visit){0};request=(struct trace_request){0};
}
#endif
#undef READ
#endif
