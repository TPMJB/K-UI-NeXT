/* SPDX-License-Identifier: GPL-3.0-only */
#include "cdda_client.h"
#include "kui/cdda_job.h"
static struct kui_cdda_service_guard service_guard;
static struct kui_cdda_handoff_desc service_descriptor;
static struct kui_cdda_stream_clock service_elapsed;
static struct kui_cdda_job service_job;
static struct cdda_client_exports service_exports;
static _Alignas(32) uint8_t service_buffer[2048];
static uint32_t service_calls,service_checked,service_errors,service_reads,service_budget;
static uint32_t service_last_data,service_data_gap;
static uint32_t service_rejects,service_reentries,service_deadlines,service_abi_checks,service_stales;
static uint32_t service_client_stack_used,service_stack_used,service_context_checks,service_epoch;
static bool service_native_busy,service_expect_gap;
#ifndef CDDA_HARNESS_HOST_TEST
extern uint8_t __cdda_engine_begin[] __asm__("__cdda_engine_begin");
extern uint8_t __cdda_engine_end[] __asm__("__cdda_engine_end");
extern uint8_t __cdda_state_begin[] __asm__("__cdda_state_begin");
extern uint8_t __cdda_bss_end[] __asm__("__cdda_bss_end");
extern uint8_t __cdda_service_stack_bottom[] __asm__("__cdda_service_stack_bottom");
extern uint8_t __cdda_service_stack_top[] __asm__("__cdda_service_stack_top");
extern uint8_t __cdda_client_begin[] __asm__("__cdda_client_begin");
extern uint8_t __cdda_client_end[] __asm__("__cdda_client_end");
extern uint8_t __cdda_client_bss_begin[] __asm__("__cdda_client_bss_begin");
extern uint8_t __cdda_client_bss_end[] __asm__("__cdda_client_bss_end");
extern uint8_t __cdda_client_stack_bottom[] __asm__("__cdda_client_stack_bottom");
extern uint8_t __cdda_client_stack_top[] __asm__("__cdda_client_stack_top");
#endif
static uint32_t service_export(void *,struct cdda_client_request *);
static uint32_t service_sp(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_host_service_sp(void);return cdda_host_service_sp();
#else
    uint32_t sp;__asm__ __volatile__("mov r15,%0":"=r"(sp));return sp;
#endif
}
static void service_context(uint32_t out[3]) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_host_service_context(uint32_t *);cdda_host_service_context(out);
#else
    __asm__ __volatile__("stc sr,%0":"=r"(out[0]));
    __asm__ __volatile__("stc vbr,%0":"=r"(out[1]));
    __asm__ __volatile__("stc gbr,%0":"=r"(out[2]));
#endif
    out[0]&=~0x301u; /* Ordinary integer C may change T/Q/M (comparisons/division). */
}
static void service_stacks_init(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_host_service_stacks_init(void);cdda_host_service_stacks_init();
#else
    uint32_t *begins[]={(uint32_t *)__cdda_service_stack_bottom,(uint32_t *)__cdda_client_stack_bottom};
    for(unsigned stack=0;stack<2;stack++) {
        for(unsigned i=0;i<16384u;i++) begins[stack][i]=i<16u?0x53435637u:0xa5a5a5a5u;
    }
    for(uint8_t *p=__cdda_client_bss_begin;p<__cdda_client_bss_end;p++) *p=0;
#endif
}
static bool service_stack_check(unsigned which,uint32_t *used) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern bool cdda_host_service_stack_check(unsigned,uint32_t *);
    return cdda_host_service_stack_check(which,used);
#else
    uint32_t *base=(uint32_t *)(which?__cdda_client_stack_bottom:__cdda_service_stack_bottom);
    for(unsigned i=0;i<16u;i++) if(base[i]!=0x53435637u) return false;
    unsigned first=16u;while(first<16384u && base[first]==0xa5a5a5a5u) first++;
    *used=(16384u-first)*4u;return *used && *used<=65472u;
#endif
}
static uint32_t service_clock_native(void *context) {
    return context==&service_exports?ticks():UINT32_MAX;
}
static bool service_stop_audio(void) {
    if(command_control.initialized) {
        uint32_t epoch=command_control.current.pending?command_control.current.pending_epoch:command_control.current.epoch;
        (void)kui_cdda_control_fail(&command_control,epoch);
    }
    if(service_job.state==KUI_CDDA_JOB_READING || service_job.state==KUI_CDDA_JOB_READY ||
       service_job.state==KUI_CDDA_JOB_QUEUED) (void)kui_cdda_job_fail(&service_job,service_job.generation);
    command_running=false;
    bool ok=kui_cdda_aica_stop()==KUI_CDDA_AICA_OK;cdda_storage_close();return ok;
}
static uint32_t service_fault_worker(const void *unused) {
    (void)unused;return service_stop_audio()?KUI_CDDA_SERVICE_OK:KUI_CDDA_SERVICE_IO;
}
static uint32_t service_worker_call(kui_cdda_client_entry entry,const void *context) {
#ifdef CDDA_HARNESS_HOST_TEST
    return kui_cdda_client_call(entry,context,(void *)(uintptr_t)0x8c230000u);
#else
    return kui_cdda_client_call(entry,context,__cdda_service_stack_top);
#endif
}
static uint8_t service_pattern(uint32_t at) {
    uint32_t x=at^0x9e3779b9u;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (uint8_t)x;
}
static bool service_data_step(void) {
    if(!command_running || !ring.ready[0] || !ring.ready[1]) return true;
    uint32_t margin;
    if(!kui_cdda_frames_to_ticks(KUI_CDDA_HALF_FRAMES-ring.last_position%KUI_CDDA_HALF_FRAMES,&margin)) return false;
    if(service_budget>(UINT32_MAX-KUI_CDDA_MARGIN_TICKS)/2u ||
       margin<=2u*service_budget+KUI_CDDA_MARGIN_TICKS) return true;
    uint32_t token,offset=(service_reads*2017u)%(8u*1024u*1024u-2048u+1u);
    struct kui_cdda_job_span span;
    if(kui_cdda_job_begin(&service_job,8u*1024u*1024u,offset,2048u,service_guard.generation,&token)!=KUI_CDDA_JOB_OK ||
       kui_cdda_job_claim(&service_job,token,service_guard.generation,2048u,&span)!=KUI_CDDA_JOB_OK) return false;
    uint32_t first=ticks();service_reads++;
    int result=cdda_storage_data_read_at(span.offset,service_buffer,span.bytes);
    bool ok=command_sync() && !result;
    for(unsigned i=0;ok && i<span.bytes;i++) if(service_buffer[i]!=service_pattern(span.offset+i)) ok=false;
    uint32_t elapsed=ticks()-first;if(elapsed>service_budget) service_budget=elapsed;
    if(!ok) {(void)kui_cdda_job_ready(&service_job,&span,service_guard.generation,false);return false;}
    if(!command_sync() || ticks()-service_guard.last_tick>=service_guard.gap_ticks) {
        (void)kui_cdda_job_ready(&service_job,&span,service_guard.generation,false);return false;
    }
    if(service_checked>UINT32_MAX-span.bytes ||
       kui_cdda_job_ready(&service_job,&span,service_guard.generation,true)!=KUI_CDDA_JOB_OK ||
       kui_cdda_job_commit(&service_job,&span,service_guard.generation)!=KUI_CDDA_JOB_COMPLETE) return false;
    service_checked+=span.bytes;service_last_data=ticks();return true;
}
static uint32_t service_worker(const void *opaque) {
    struct cdda_client_request *r=(struct cdda_client_request *)opaque;
    uint32_t sp=service_sp();
    uint32_t now=ticks();
    if(sp<0x8c220040u || sp>=0x8c230000u ||
       !kui_cdda_stream_clock_update(&service_elapsed,now) ||
       (r->op==CDDA_CLIENT_SERVICE && now-service_last_data>=service_data_gap)) return KUI_CDDA_SERVICE_IO;
    service_calls++;
    switch(r->op) {
    case CDDA_CLIENT_SERVICE:
        if(command_running) {
            if(!command_sync()) return KUI_CDDA_SERVICE_IO;
            unsigned active=ring.last_position/KUI_CDDA_HALF_FRAMES;
            if(!ring.ready[1u-active] && !fill_half(1u-active,true)) return KUI_CDDA_SERVICE_IO;
            if(!service_data_step() || !command_sync()) return KUI_CDDA_SERVICE_IO;
        }
        break;
    case CDDA_CLIENT_PAUSE:
        if(!command_accept(command_request(KUI_CDDA_CONTROL_PAUSE))) return KUI_CDDA_SERVICE_IO;
        break;
    case CDDA_CLIENT_RESUME:
        if(!command_accept(command_request(KUI_CDDA_CONTROL_RESUME))) return KUI_CDDA_SERVICE_IO;
        break;
    case CDDA_CLIENT_SEEK: {
        struct kui_cdda_control_request seek=command_request(KUI_CDDA_CONTROL_SEEK);seek.frame=r->value;
        if(!command_accept(seek)) return KUI_CDDA_SERVICE_IO;
        break;
    }
    case CDDA_CLIENT_STATUS: {
        struct kui_cdda_control_status status;
        if(!command_status((enum kui_cdda_control_state)r->value,&status)) return KUI_CDDA_SERVICE_IO;
        r->state=status.state;r->frame=status.frame;break;
    }
    case CDDA_CLIENT_STOP:
        if(!command_accept(command_request(KUI_CDDA_CONTROL_STOP))) return KUI_CDDA_SERVICE_IO;
        break;
    case CDDA_CLIENT_RESTART:
        if(command_running || !command_play(TONE_FIRST,TONE_FIRST+TONE_FRAMES,true) ||
           kui_cdda_service_start(&service_guard,ring.last_tick,KUI_CDDA_HALF_TICKS,&service_epoch)!=KUI_CDDA_SERVICE_OK)
            return KUI_CDDA_SERVICE_IO;
        r->epoch=service_epoch;break;
    case CDDA_CLIENT_ARM_GAP:
        if(service_expect_gap || service_deadlines || !command_running) return KUI_CDDA_SERVICE_IO;
        service_expect_gap=true;break;
    case CDDA_CLIENT_REENTRY: {
        struct cdda_client_request nested={.epoch=r->epoch,.op=CDDA_CLIENT_SERVICE,.pc=r->pc,.sp=r->sp};
        struct kui_cdda_service_guard before=service_guard;
        if(service_export(&service_exports,&nested)!=KUI_CDDA_SERVICE_BUSY_RESULT ||
           service_guard.state!=before.state || service_guard.pending.call!=before.pending.call)
            return KUI_CDDA_SERVICE_IO;
        service_reentries++;break;
    }
    case CDDA_CLIENT_MARK:
        if(r->value!=completed+1u || r->value>6u) return KUI_CDDA_SERVICE_IO;
        completed++;break;
    case CDDA_CLIENT_REPORT:
        if(r->value<100u || r->aux!=2u) return KUI_CDDA_SERVICE_IO;
        service_abi_checks=r->value;service_stales=r->aux;break;
    default:return KUI_CDDA_SERVICE_INVALID;
    }
    return KUI_CDDA_SERVICE_OK;
}
static bool service_request_valid(const struct cdda_client_request *r) {
    if(!r) return false;
#ifndef CDDA_HARNESS_HOST_TEST
    uintptr_t p=(uintptr_t)r;
    if(p%4u || !((p>=0x8c310040u && p<=0x8c320000u-sizeof(*r)) ||
                 (p>=0x8c220040u && p<=0x8c230000u-sizeof(*r)))) return false;
#endif
    if(r->op>CDDA_CLIENT_REPORT) return false;
    return r->pc>=service_descriptor.client_code_first && r->pc<service_descriptor.client_code_end &&
        !(r->pc&1u) && r->sp>=0x8c310040u && r->sp<0x8c320000u && !(r->sp&3u);
}
static uint32_t service_export(void *context,struct cdda_client_request *r) {
    /* Refuse malformed, retired or nested work before any clock/phase/I/O. */
    if(context!=&service_exports || !service_request_valid(r)) return KUI_CDDA_SERVICE_INVALID;
    uint32_t result;
    if(r->epoch!=service_guard.generation) result=KUI_CDDA_SERVICE_STALE;
    else if(service_native_busy || service_guard.state==KUI_CDDA_SERVICE_BUSY) result=KUI_CDDA_SERVICE_BUSY_RESULT;
    else if(r->op==CDDA_CLIENT_RESTART) {
        if(service_guard.state!=KUI_CDDA_SERVICE_FAULT && service_guard.state!=KUI_CDDA_SERVICE_STOPPED)
            result=KUI_CDDA_SERVICE_STATE;
        else {
            service_native_busy=true;result=service_worker_call(service_worker,r);service_native_busy=false;
            if(result!=KUI_CDDA_SERVICE_OK) {service_errors++;(void)service_worker_call(service_fault_worker,NULL);}
        }
    } else {
        struct kui_cdda_service_ticket ticket;
        result=kui_cdda_service_enter(&service_guard,r->epoch,ticks(),&ticket);
        if(result==KUI_CDDA_SERVICE_OK) {
            service_native_busy=true;uint32_t work=service_worker_call(service_worker,r);service_native_busy=false;
            result=kui_cdda_service_leave(&service_guard,&ticket,ticks(),work==KUI_CDDA_SERVICE_OK);
            if(result!=KUI_CDDA_SERVICE_OK) {service_errors++;(void)service_worker_call(service_fault_worker,NULL);}
        } else if(result==KUI_CDDA_SERVICE_DEADLINE) {
            bool expected=service_expect_gap && !service_deadlines;
            service_expect_gap=false;if(expected) service_deadlines++;else service_errors++;
            if(service_worker_call(service_fault_worker,NULL)!=KUI_CDDA_SERVICE_OK) service_errors++;
        }
    }
    r->result=result;return result;
}
static bool service_prepare_descriptor(void) {
    struct kui_cdda_handoff_desc d={
        .magic0=KUI_CDDA_HANDOFF_MAGIC0,.magic1=KUI_CDDA_HANDOFF_MAGIC1,.version=1,.bytes=160,.service_revision=1,
        .flags=KUI_CDDA_HANDOFF_FLAGS,.storage_transport=KUI_CDDA_HANDOFF_SCI,
        .storage_rights=KUI_CDDA_HANDOFF_STORAGE_RIGHTS,.resource_rights=KUI_CDDA_HANDOFF_RESOURCE_RIGHTS,
        .tmu_unit=1,.sound_channels=3,.clock_hz=KUI_CDDA_TMU_HZ,.service_gap_ticks=KUI_CDDA_HALF_TICKS,
        .engine_stack_first=0x8c200000,.engine_stack_end=0x8c210000,
        .service_stack_first=0x8c220000,.service_stack_end=0x8c230000,
        .client_code_first=0x8c300000,.client_code_end=0x8c310000,
        .client_stack_first=0x8c310000,.client_stack_end=0x8c320000,
        .sound_first=KUI_CDDA_HANDOFF_SOUND_FIRST,.sound_end=KUI_CDDA_HANDOFF_SOUND_END};
#ifdef CDDA_HARNESS_HOST_TEST
    d.engine_code_first=0x8c010000;d.engine_code_end=0x8c020000;
    d.engine_state_first=0x8c020000;d.engine_state_end=0x8c030000;
    d.client_entry=0x8c300100;d.service_entry=0x8c010200;
#else
    d.engine_code_first=(uint32_t)(uintptr_t)__cdda_engine_begin;d.engine_code_end=(uint32_t)(uintptr_t)__cdda_engine_end;
    d.engine_state_first=(uint32_t)(uintptr_t)__cdda_state_begin;d.engine_state_end=(uint32_t)(uintptr_t)__cdda_bss_end;
    d.client_entry=(uint32_t)(uintptr_t)&cdda_client_entry;d.service_entry=(uint32_t)(uintptr_t)&service_export;
#endif
    if(kui_cdda_handoff_validate(&d)!=KUI_CDDA_HANDOFF_OK) return false;
    uint8_t wire[160];const uint8_t *native=(const uint8_t *)&d;
    for(unsigned i=0;i<sizeof(wire);i++) wire[i]=native[i]; /* Both builds are LE; decoder is byte-based. */
    if(kui_cdda_handoff_decode(wire,sizeof(wire),&service_descriptor)!=KUI_CDDA_HANDOFF_OK) return false;
    for(unsigned i=0;i<8u;i++) {
        struct kui_cdda_handoff_desc bad=d;
        switch(i) {
        case 0:bad.version++;break;case 1:bad.resource_rights=0;break;
        case 2:bad.storage_transport=0;break;case 3:bad.client_stack_first=d.engine_stack_first;break;
        case 4:bad.client_entry=d.client_code_end;break;case 5:bad.service_entry=d.client_code_first;break;
        case 6:bad.service_stack_end=UINT32_MAX;break;default:bad.reserved[0]=1;break;
        }
        if(kui_cdda_handoff_validate(&bad)==KUI_CDDA_HANDOFF_OK) return false;
        service_rejects++;
    }
    return true;
}
static bool service_test(void) {
    service_calls=service_checked=service_errors=service_reads=service_rejects=service_reentries=service_deadlines=0;
    service_abi_checks=service_stales=service_context_checks=service_client_stack_used=service_stack_used=0;
    service_native_busy=service_expect_gap=false;
    command_running=false;command_accepted=command_completions=command_status_checks=command_paused_frame=command_max_loops=0;
    service_stacks_init();
    cdda_display_line("1/7: validate distinct owned handoff;8 expected refusals");
    if(!service_prepare_descriptor() || kui_cdda_service_init(&service_guard)!=KUI_CDDA_SERVICE_OK ||
       kui_cdda_job_init(&service_job)!=KUI_CDDA_JOB_OK || !kui_cdda_ms_to_ticks(10u,&service_budget) ||
       !kui_cdda_ms_to_ticks(5000u,&service_data_gap)) goto failed;
    uint32_t bytes;
    if(cdda_storage_data_open(&bytes) || bytes!=8u*1024u*1024u ||
       cdda_storage_open("0:/KUI/tests/cdda/stereo.raw",&bytes)) goto failed;
    cdda_storage_close();
    if(!bytes || bytes%2352u || kui_cdda_control_init(&command_control,bytes/4u)!=KUI_CDDA_CONTROL_OK ||
       !command_play(TONE_FIRST,TONE_FIRST+TONE_FRAMES,true) ||
       !kui_cdda_stream_clock_init(&service_elapsed,ticks(),KUI_CDDA_TMU_HZ) ||
       kui_cdda_service_start(&service_guard,ring.last_tick,KUI_CDDA_HALF_TICKS,&service_epoch)!=KUI_CDDA_SERVICE_OK) goto failed;
    service_last_data=ring.last_tick;
    completed++;
    service_exports=(struct cdda_client_exports){CDDA_CLIENT_MAGIC,1,sizeof(service_exports),service_epoch,
        &service_exports,service_export,service_clock_native,kui_cdda_bridge_probe};
    uint32_t before[3],after[3];
    cdda_display_line("2..6/7: separate client/worker stacks;90s explicit service");
    service_context(before);
#ifdef CDDA_HARNESS_HOST_TEST
    uint32_t result=kui_cdda_client_call(cdda_client_entry,&service_exports,(void *)(uintptr_t)0x8c320000u);
#else
    uint32_t result=kui_cdda_client_call(cdda_client_entry,&service_exports,__cdda_client_stack_top);
#endif
    service_context(after);
    if(result || before[0]!=after[0] || before[1]!=after[1] || before[2]!=after[2]) goto failed;
    service_context_checks=1;
    if(!service_stack_check(0,&service_stack_used) || !service_stack_check(1,&service_client_stack_used) ||
       service_elapsed.seconds<90u || service_checked<65536u || service_deadlines!=1u || service_stales!=2u ||
       service_reentries!=1u || service_rejects!=8u || service_errors || command_running ||
       service_abi_checks!=service_calls+2u || command_completions!=6u || command_status_checks!=5u ||
       command_control.current.state!=KUI_CDDA_CONTROL_STOPPED || completed!=6u ||
       kui_cdda_service_stop(&service_guard,service_epoch)!=KUI_CDDA_SERVICE_OK) goto failed;
    cdda_storage_data_close();completed++;return true;
failed:
    service_errors++;(void)service_stop_audio();cdda_storage_data_close();
    if(service_guard.initialized && service_guard.state==KUI_CDDA_SERVICE_ACTIVE)
        (void)kui_cdda_service_stop(&service_guard,service_guard.generation);
    cdda_display_line("Service/handoff test failed; owned audio stopped.");return false;
}
static void service_report(void) {
    display_pair("Client seconds / service calls: ",service_elapsed.seconds,service_calls);
    display_pair("Client / service stack bytes: ",service_client_stack_used,service_stack_used);
    display_pair("ABI checks / context preserved: ",service_abi_checks,service_context_checks);
    display_pair("Checked bytes / service errors: ",service_checked,service_errors);
    display_pair("Audio actions / STATUS checks: ",command_completions,command_status_checks);
    display_pair("EXPECTED gaps / stale refusals: ",service_deadlines,service_stales);
    display_pair("Descriptor rejects / reentries: ",service_rejects,service_reentries);
}
