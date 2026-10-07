/* SPDX-License-Identifier: GPL-3.0-only */
/* Controlled GD command-vector experiment. Ordinary readers stay separate. */
#include "cdda_bios_client.h"
#include "kui/cdda_bios.h"
#include "kui/cdda_handoff.h"
#include "kui/cdda_job.h"
#include "kui/cdda_vector.h"
static struct kui_cdda_bios bios_owner;
static struct kui_cdda_service_guard bios_guard;
static struct kui_cdda_handoff_desc bios_descriptor;
static struct kui_cdda_stream_clock bios_elapsed;
static struct kui_cdda_job bios_data_job;
static struct cdda_bios_client_exports bios_exports;
static _Alignas(32) uint8_t bios_buffer[2048];
static uint32_t bios_checked,bios_errors,bios_calls,bios_execs,bios_accepted,bios_completions;
static uint32_t bios_refusals,bios_cancels,bios_resets,bios_reentries,bios_abi_checks,bios_checks,bios_drive_checks;
static uint32_t bios_client_stack_used,bios_service_stack_used,bios_vector_restored,bios_context_checks;
static uint32_t bios_guard_epoch,bios_data_budget,bios_last_data,bios_data_gap,bios_descriptor_rejects;
static uint32_t bios_old_vector,bios_report_stales;
static struct kui_cdda_bios_work bios_completion_work;
static bool bios_completion_pending;
static bool bios_native_busy,bios_reentry_armed,bios_installed,bios_reported;
int32_t cdda_bios_native_dispatch(uint32_t,uint32_t,uint32_t,uint32_t);
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
static uint32_t bios_sp(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_host_bios_sp(void);return cdda_host_bios_sp();
#else
    uint32_t sp;__asm__ __volatile__("mov r15,%0":"=r"(sp));return sp;
#endif
}
static void bios_context(uint32_t out[3]) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_host_bios_context(uint32_t *);cdda_host_bios_context(out);
#else
    __asm__ __volatile__("stc sr,%0":"=r"(out[0]));
    __asm__ __volatile__("stc vbr,%0":"=r"(out[1]));
    __asm__ __volatile__("stc gbr,%0":"=r"(out[2]));
#endif
    out[0]&=~0x301u; /* Ordinary integer C may change T/Q/M (comparisons/division). */
}
static void bios_stacks_init(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_host_bios_stacks_init(void);cdda_host_bios_stacks_init();
#else
    uint32_t *begins[]={(uint32_t *)__cdda_service_stack_bottom,(uint32_t *)__cdda_client_stack_bottom};
    for(unsigned stack=0;stack<2;stack++) {
        for(unsigned i=0;i<16384u;i++) begins[stack][i]=i<16u?0x53435637u:0xa5a5a5a5u;
    }
    for(uint8_t *p=__cdda_client_bss_begin;p<__cdda_client_bss_end;p++) *p=0;
#endif
}
static bool bios_stack_check(unsigned which,uint32_t *used) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern bool cdda_host_bios_stack_check(unsigned,uint32_t *);
    return cdda_host_bios_stack_check(which,used);
#else
    uint32_t *base=(uint32_t *)(which?__cdda_client_stack_bottom:__cdda_service_stack_bottom);
    for(unsigned i=0;i<16u;i++) if(base[i]!=0x53435637u) return false;
    unsigned first=16u;while(first<16384u && base[first]==0xa5a5a5a5u) first++;
    *used=(16384u-first)*4u;return *used && *used<=65472u;
#endif
}
static uint32_t bios_clock_native(void *context) {
    return context==&bios_exports?ticks():UINT32_MAX;
}
static bool bios_stop_audio(void) {
    if(command_control.initialized) {
        uint32_t epoch=command_control.current.pending?command_control.current.pending_epoch:command_control.current.epoch;
        (void)kui_cdda_control_fail(&command_control,epoch);
    }
    if(bios_data_job.state==KUI_CDDA_JOB_READING || bios_data_job.state==KUI_CDDA_JOB_READY ||
       bios_data_job.state==KUI_CDDA_JOB_QUEUED) (void)kui_cdda_job_fail(&bios_data_job,bios_data_job.generation);
    command_running=false;
    bool ok=kui_cdda_aica_stop()==KUI_CDDA_AICA_OK;cdda_storage_close();return ok;
}
static uint32_t bios_fault_worker(const void *unused) {
    (void)unused;bool ok=bios_stop_audio();bios_completion_pending=false;
    if(bios_owner.initialized) {
        struct kui_cdda_bios_work work;
        if(bios_owner.state==KUI_CDDA_BIOS_QUEUED) {
            if(kui_cdda_bios_take(&bios_owner,&work)!=KUI_CDDA_BIOS_OK) ok=false;
        } else work=bios_owner.work;
        if(bios_owner.state==KUI_CDDA_BIOS_RUNNING &&
           kui_cdda_bios_complete(&bios_owner,&work,false,0u)!=KUI_CDDA_BIOS_OK) ok=false;
        if(kui_cdda_bios_observe_audio(&bios_owner,KUI_CDDA_CONTROL_FAULT)!=KUI_CDDA_BIOS_OK) ok=false;
    }
    return ok?KUI_CDDA_SERVICE_OK:KUI_CDDA_SERVICE_IO;
}
static uint32_t bios_worker_call(kui_cdda_client_entry entry,const void *context) {
#ifdef CDDA_HARNESS_HOST_TEST
    return kui_cdda_client_call(entry,context,(void *)(uintptr_t)0x8c230000u);
#else
    return kui_cdda_client_call(entry,context,__cdda_service_stack_top);
#endif
}
static uint32_t bios_fault_call(void) {
    bios_native_busy=true;uint32_t result=bios_worker_call(bios_fault_worker,NULL);bios_native_busy=false;
    return result;
}
static uint8_t bios_pattern(uint32_t at) {
    uint32_t x=at^0x9e3779b9u;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (uint8_t)x;
}
static bool bios_prepare_descriptor(void) {
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
    d.client_entry=(uint32_t)(uintptr_t)&cdda_bios_client_entry;d.service_entry=(uint32_t)(uintptr_t)&cdda_bios_native_dispatch;
#endif
    if(kui_cdda_handoff_validate(&d)!=KUI_CDDA_HANDOFF_OK) return false;
    uint8_t wire[160];const uint8_t *native=(const uint8_t *)&d;
    for(unsigned i=0;i<sizeof(wire);i++) wire[i]=native[i]; /* Both builds are LE; decoder is byte-based. */
    if(kui_cdda_handoff_decode(wire,sizeof(wire),&bios_descriptor)!=KUI_CDDA_HANDOFF_OK) return false;
    for(unsigned i=0;i<8u;i++) {
        struct kui_cdda_handoff_desc bad=d;
        switch(i) {
        case 0:bad.version++;break;case 1:bad.resource_rights=0;break;
        case 2:bad.storage_transport=0;break;case 3:bad.client_stack_first=d.engine_stack_first;break;
        case 4:bad.client_entry=d.client_code_end;break;case 5:bad.service_entry=d.client_code_first;break;
        case 6:bad.service_stack_end=UINT32_MAX;break;default:bad.reserved[0]=1;break;
        }
        if(kui_cdda_handoff_validate(&bad)==KUI_CDDA_HANDOFF_OK) return false;
        bios_descriptor_rejects++;
    }
    return true;
}
static uint32_t bios_vector_read(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_bios_host_vector_read(void);return cdda_bios_host_vector_read();
#else
    return MMIO32(KUI_CDDA_BIOS_GD_VECTOR_ADDRESS);
#endif
}
static void bios_vector_write(uint32_t value) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_bios_host_vector_write(uint32_t);cdda_bios_host_vector_write(value);
#else
    uintptr_t slot=KUI_CDDA_BIOS_GD_VECTOR_ADDRESS;
    MMIO32(slot)=value;
    /* Publish the pointer word without changing cache or interrupt setup. */
    __asm__ __volatile__("ocbwb @%0\n\tocbi @%0"::"r"(slot):"memory");
#endif
}
static int32_t bios_nested_exec(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    return cdda_bios_host_vector_call(0,0,0,KUI_GD_EXEC);
#else
    kui_cdda_bios_vector_fn entry=(kui_cdda_bios_vector_fn)(uintptr_t)bios_vector_read();
    return entry(0,0,0,KUI_GD_EXEC);
#endif
}
static uint8_t *bios_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    if(context!=&bios_owner || address<0x8c310040u || address>=0x8c320000u ||
       !bytes || bytes>0x8c320000u-address) return NULL;
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint8_t *cdda_bios_host_map(uint32_t,uint32_t,int);
    return cdda_bios_host_map(address,bytes,writing);
#else
    (void)writing;return (uint8_t *)(uintptr_t)address;
#endif
}
static bool bios_data_admitted(void) {
    if(!command_running) return true;
    uint32_t margin;
    if(!ring.ready[0] || !ring.ready[1] ||
       !kui_cdda_frames_to_ticks(KUI_CDDA_HALF_FRAMES-ring.last_position%KUI_CDDA_HALF_FRAMES,&margin) ||
       bios_data_budget>(UINT32_MAX-KUI_CDDA_MARGIN_TICKS)/2u) return false;
    return margin>2u*bios_data_budget+KUI_CDDA_MARGIN_TICKS;
}
static bool bios_audio_service(void) {
    if(!command_running) return true;
    if(!command_sync()) return false;
    if(!command_running) return true;
    unsigned active=ring.last_position/KUI_CDDA_HALF_FRAMES;
    return (ring.ready[1u-active] || fill_half(1u-active,true)) && command_sync();
}
static bool bios_read_work(const struct kui_cdda_bios_work *work) {
    uint32_t token;struct kui_cdda_job_span span;
    if(kui_cdda_bios_validate_work(&bios_owner,work)!=KUI_CDDA_BIOS_OK) return false;
    uint8_t *out=bios_map(&bios_owner,work->destination,work->bytes,1);
    if(!out ||
       kui_cdda_job_begin(&bios_data_job,KUI_CDDA_BIOS_DATA_BYTES,work->offset,work->bytes,
          work->epoch,&token)!=KUI_CDDA_JOB_OK ||
       kui_cdda_job_claim(&bios_data_job,token,work->epoch,2048u,&span)!=KUI_CDDA_JOB_OK) return false;
    uint32_t first=ticks();
    int result=cdda_storage_data_read_at(span.offset,bios_buffer,span.bytes);
    bool ok=command_sync() && !result;
    for(uint32_t i=0;ok && i<span.bytes;i++) if(bios_buffer[i]!=bios_pattern(span.offset+i)) ok=false;
    uint32_t elapsed=ticks()-first;if(elapsed>bios_data_budget) bios_data_budget=elapsed;
    if(!ok || !command_sync() || ticks()-bios_guard.last_tick>=bios_guard.gap_ticks-KUI_CDDA_MARGIN_TICKS ||
       kui_cdda_bios_validate_work(&bios_owner,work)!=KUI_CDDA_BIOS_OK) {
        (void)kui_cdda_job_ready(&bios_data_job,&span,work->epoch,false);return false;
    }
    /* Check the exact identities before publishing any guest data. */
    if(bios_checked>UINT32_MAX-span.bytes ||
       kui_cdda_job_ready(&bios_data_job,&span,work->epoch,true)!=KUI_CDDA_JOB_OK) return false;
    for(uint32_t i=0;i<span.bytes;i++) out[i]=bios_buffer[i];
    return true; /* Progress remains READY until the complete service lease passes. */
}
static uint32_t bios_worker(const void *unused) {
    (void)unused;bios_completion_pending=false;uint32_t now=ticks(),sp=bios_sp();
    if(sp<0x8c220040u || sp>=0x8c230000u ||
       !kui_cdda_stream_clock_update(&bios_elapsed,now) ||
       (command_running && now-bios_last_data>=bios_data_gap)) return KUI_CDDA_SERVICE_IO;
    bios_execs++;
    if(bios_reentry_armed) {
        bios_reentry_armed=false;
        uint32_t calls=bios_calls,execs=bios_execs;
        if(bios_nested_exec()!=-1 || bios_calls!=calls+1u || bios_execs!=execs || bios_reentries!=1u)
            return KUI_CDDA_SERVICE_IO;
    }
    /* A pending control action stops/replaces the old ring before priming;
     * avoid an unnecessary old-half refill plus a two-half new prefill. */
    bool audio=bios_owner.state==KUI_CDDA_BIOS_QUEUED && bios_owner.work.kind==KUI_CDDA_BIOS_AUDIO;
    if(!audio && !bios_audio_service()) return KUI_CDDA_SERVICE_IO;
    if(bios_owner.state==KUI_CDDA_BIOS_QUEUED && bios_owner.work.kind==KUI_CDDA_BIOS_DATA &&
       !bios_data_admitted()) {
        if(kui_cdda_bios_observe_audio(&bios_owner,command_control.current.state)!=KUI_CDDA_BIOS_OK)
            return KUI_CDDA_SERVICE_IO;
        return KUI_CDDA_SERVICE_OK;
    }
    struct kui_cdda_bios_work work;
    enum kui_cdda_bios_result taken=kui_cdda_bios_take(&bios_owner,&work);
    if(taken==KUI_CDDA_BIOS_NOTHING) {
        return kui_cdda_bios_observe_audio(&bios_owner,command_control.current.state)==KUI_CDDA_BIOS_OK?
            KUI_CDDA_SERVICE_OK:KUI_CDDA_SERVICE_IO;
    }
    if(taken!=KUI_CDDA_BIOS_OK || kui_cdda_bios_validate_work(&bios_owner,&work)!=KUI_CDDA_BIOS_OK)
        return KUI_CDDA_SERVICE_IO;
    bool success=true;
    if(work.kind==KUI_CDDA_BIOS_AUDIO) success=command_accept(work.audio);
    else if(work.kind==KUI_CDDA_BIOS_DATA) success=bios_read_work(&work);
    if(!success || kui_cdda_bios_observe_audio(&bios_owner,command_control.current.state)!=KUI_CDDA_BIOS_OK)
        return KUI_CDDA_SERVICE_IO;
    bios_completion_work=work;bios_completion_pending=true;
    return KUI_CDDA_SERVICE_OK;
}
int32_t cdda_bios_native_dispatch(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    bios_calls++;
    /* The assembly entry has kept the caller stack. Refuse nested work before
     * changing stack, querying time/phase, mapping memory or touching SCI. */
    if(bios_native_busy) {
        bios_reentries++;
        return r7==KUI_GD_REQUEST?0:r7==KUI_GD_CHECK?4:-1;
    }
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_bios_host_caller_sp(void);uint32_t sp=cdda_bios_host_caller_sp();
#else
    uint32_t sp;__asm__ __volatile__("mov r15,%0":"=r"(sp));
#endif
    if(!bios_installed || sp<0x8c310040u || sp>=0x8c320000u) return -1;
    int32_t result;
    if(r6 || r7>KUI_GD_PIO_CHECK) result=-1;
    else if((r7==KUI_GD_INIT || r7==KUI_GD_RESET) &&
            command_control.current.state!=KUI_CDDA_CONTROL_STOPPED &&
            command_control.current.state!=KUI_CDDA_CONTROL_EOF) result=-1;
    else if(r7==KUI_GD_EXEC) {
        struct kui_cdda_service_ticket ticket;
        enum kui_cdda_service_result admitted=kui_cdda_service_enter(&bios_guard,bios_guard_epoch,ticks(),&ticket);
        if(admitted!=KUI_CDDA_SERVICE_OK) {
            bios_errors++;(void)bios_fault_call();result=-1;
        } else {
            bios_native_busy=true;uint32_t work=bios_worker_call(bios_worker,NULL);
            enum kui_cdda_service_result left_guard=kui_cdda_service_leave(&bios_guard,&ticket,ticks(),work==KUI_CDDA_SERVICE_OK);
            bool committed=left_guard==KUI_CDDA_SERVICE_OK;
            if(committed && bios_completion_pending) {
                struct kui_cdda_bios_work *pending=&bios_completion_work;
                committed=kui_cdda_bios_validate_work(&bios_owner,pending)==KUI_CDDA_BIOS_OK;
                if(committed && pending->kind==KUI_CDDA_BIOS_DATA) {
                    struct kui_cdda_job_span span=bios_data_job.pending;
                    committed=kui_cdda_job_commit(&bios_data_job,&span,pending->epoch)==KUI_CDDA_JOB_COMPLETE;
                    if(committed) {bios_checked+=pending->bytes;bios_last_data=ticks();}
                }
                if(committed) committed=kui_cdda_bios_complete(&bios_owner,pending,true,pending->bytes)==KUI_CDDA_BIOS_OK;
                if(committed) bios_completions++;
            }
            bios_native_busy=false;bios_completion_pending=false;
            if(!committed) {
                bios_errors++;(void)bios_fault_call();result=-1;
            } else result=0;
        }
    } else {
        bios_native_busy=true;result=kui_cdda_bios_dispatch(&bios_owner,r4,r5,r6,r7);bios_native_busy=false;
        if(r7==KUI_GD_REQUEST && result>0) bios_accepted++;
        if(r7==KUI_GD_CHECK) bios_checks++;
        if(r7==KUI_GD_DRIVE && result==0) bios_drive_checks++;
        if(r7==KUI_GD_ABORT && result==0) bios_cancels++;
        if((r7==KUI_GD_INIT || r7==KUI_GD_RESET) && result==0) bios_resets++;
    }
    if((r7==KUI_GD_REQUEST && result==0) || (r7!=KUI_GD_REQUEST && result==-1)) bios_refusals++;
    return result;
}
static uint32_t bios_diagnostic(void *context,struct cdda_bios_client_diagnostic *r) {
    if(context!=&bios_exports || !r || bios_native_busy) return 1u;
#ifndef CDDA_HARNESS_HOST_TEST
    uintptr_t p=(uintptr_t)r;
    if(p%4u || p<0x8c310040u || p>0x8c320000u-sizeof(*r)) return 1u;
#endif
    if(r->pc<0x8c300000u || r->pc>=0x8c310000u || r->pc&1u ||
       r->sp<0x8c310040u || r->sp>=0x8c320000u || r->sp&3u) return 1u;
    r->result=0;
    switch(r->op) {
    case CDDA_BIOS_CLIENT_MARK:
        if(r->value!=completed+1u || r->value>7u) return r->result=1u;
        completed++;break;
    case CDDA_BIOS_CLIENT_ARM_REENTRY:
        if(bios_reentry_armed || bios_reentries) return r->result=1u;
        bios_reentry_armed=true;break;
    case CDDA_BIOS_CLIENT_REPORT:
        if(bios_reported || r->value<100u || r->value!=bios_calls-1u || r->vector_calls!=r->value ||
           r->read_checked!=bios_checked || r->drive_checks!=bios_drive_checks || r->checks!=bios_checks ||
           r->expected_refusals!=bios_refusals || r->cancels!=bios_cancels || r->resets!=bios_resets ||
           bios_accepted!=bios_completions+2u || bios_cancels!=1u || bios_resets!=1u ||
           r->stale_checks!=3u || bios_drive_checks!=14u || bios_refusals!=14u || bios_reentries!=1u) return r->result=1u;
        bios_abi_checks=r->value;bios_report_stales=r->stale_checks;bios_reported=true;break;
    case CDDA_BIOS_CLIENT_SNAPSHOT:
        r->state=command_control.current.state;r->frame=command_control.current.frame;r->prefetch=pcm.position;
        r->vector_calls=bios_calls;r->execs=bios_execs;r->checks=bios_checks;r->drive_checks=bios_drive_checks;
        r->read_checked=bios_checked;r->expected_refusals=bios_refusals;
        r->cancels=bios_cancels;r->resets=bios_resets;break;
    default:return r->result=1u;
    }
    return 0;
}
static bool cdda_bios_test(void) {
    bios_calls=bios_execs=bios_accepted=bios_completions=bios_refusals=bios_cancels=bios_resets=0;
    bios_checked=bios_errors=bios_reentries=bios_abi_checks=bios_checks=bios_drive_checks=0;
    bios_client_stack_used=bios_service_stack_used=bios_vector_restored=bios_context_checks=bios_descriptor_rejects=0;
    bios_installed=bios_native_busy=bios_reentry_armed=bios_reported=bios_completion_pending=false;
    command_running=false;command_accepted=command_completions=command_status_checks=command_paused_frame=command_max_loops=0;
    bios_stacks_init();cdda_display_line("1/8: validate owned handoff and native GD vector");
    if(!bios_prepare_descriptor() || kui_cdda_service_init(&bios_guard)!=KUI_CDDA_SERVICE_OK ||
       kui_cdda_job_init(&bios_data_job)!=KUI_CDDA_JOB_OK ||
       !kui_cdda_ms_to_ticks(10u,&bios_data_budget) || !kui_cdda_ms_to_ticks(5000u,&bios_data_gap)) goto failed;
    uint32_t bytes,source_bytes;
    if(cdda_storage_data_open(&bytes) || bytes!=KUI_CDDA_BIOS_DATA_BYTES ||
       cdda_storage_open("0:/KUI/tests/cdda/stereo.raw",&source_bytes)) goto failed;
    cdda_storage_close();
    struct kui_cdda_bios_ops ops={&bios_owner,bios_map};
    if(!source_bytes || source_bytes%2352u ||
       kui_cdda_control_init(&command_control,source_bytes/4u)!=KUI_CDDA_CONTROL_OK ||
       kui_cdda_bios_init(&bios_owner,&ops,source_bytes/4u,bytes,0x8c310040u,0x8c320000u)!=KUI_CDDA_BIOS_OK ||
       !kui_cdda_stream_clock_init(&bios_elapsed,ticks(),KUI_CDDA_TMU_HZ)) goto failed;
    bios_last_data=ticks();
    if(kui_cdda_service_start(&bios_guard,bios_last_data,KUI_CDDA_HALF_TICKS,&bios_guard_epoch)!=KUI_CDDA_SERVICE_OK) goto failed;
    bios_exports=(struct cdda_bios_client_exports){CDDA_BIOS_CLIENT_MAGIC,CDDA_BIOS_CLIENT_REVISION,
        sizeof(bios_exports),&bios_exports,bios_clock_native,kui_cdda_bridge_probe,bios_diagnostic};
    bios_old_vector=bios_vector_read();
#ifdef CDDA_HARNESS_HOST_TEST
    uint32_t hook=0x8c010200u;
#else
    uint32_t hook=(uint32_t)(uintptr_t)&kui_cdda_bios_vector_entry;
#endif
    bios_installed=true;bios_vector_write(hook);
    if(bios_vector_read()!=hook) goto failed;
    completed++;
    cdda_display_line("2..7/8: BIOS PLAY/pause/release + checked READ;60s");
    uint32_t before[3],after[3];bios_context(before);
#ifdef CDDA_HARNESS_HOST_TEST
    uint32_t result=kui_cdda_client_call(cdda_bios_client_entry,&bios_exports,(void *)(uintptr_t)0x8c320000u);
#else
    uint32_t result=kui_cdda_client_call(cdda_bios_client_entry,&bios_exports,__cdda_client_stack_top);
#endif
    bios_context(after);
    bios_vector_write(bios_old_vector);bios_vector_restored=bios_vector_read()==bios_old_vector;
    if(bios_vector_restored) bios_installed=false;
    if(result || before[0]!=after[0] || before[1]!=after[1] || before[2]!=after[2]) goto failed;
    bios_context_checks=1u;
    if(!bios_stack_check(0,&bios_service_stack_used) || !bios_stack_check(1,&bios_client_stack_used) ||
       completed!=7u || bios_elapsed.seconds<60u || bios_checked<65536u || bios_errors ||
       !bios_reported || bios_descriptor_rejects!=8u || bios_reentries!=1u || bios_report_stales!=3u ||
       !bios_vector_restored || command_running || command_completions!=7u ||
       command_control.current.state!=KUI_CDDA_CONTROL_STOPPED || bios_owner.state!=KUI_CDDA_BIOS_EMPTY ||
       kui_cdda_service_stop(&bios_guard,bios_guard_epoch)!=KUI_CDDA_SERVICE_OK) goto failed;
    cdda_storage_data_close();completed++;return true;
failed:
    bios_errors++;(void)bios_fault_call();cdda_storage_data_close();
    if(bios_guard.initialized && bios_guard.state==KUI_CDDA_SERVICE_ACTIVE)
        (void)kui_cdda_service_stop(&bios_guard,bios_guard.generation);
    if(bios_installed) {
        bios_vector_write(bios_old_vector);bios_vector_restored=bios_vector_read()==bios_old_vector;bios_installed=false;
    }
    cdda_display_line("BIOS/vector test failed; owned audio stopped.");return false;
}
static void cdda_bios_report(void) {
    display_pair("Client seconds / vector calls: ",bios_elapsed.seconds,bios_calls);
    display_pair("BIOS requests / completions: ",bios_accepted,bios_completions);
    display_pair("Audio actions / DRIVE checks: ",command_completions,bios_drive_checks);
    display_pair("Checked bytes / service errors: ",bios_checked,bios_errors);
    display_pair("Client / service stack bytes: ",bios_client_stack_used,bios_service_stack_used);
    display_pair("ABI checks / vector restored: ",bios_abi_checks,bios_vector_restored);
    display_pair("EXPECTED rejects / cancels: ",bios_refusals,bios_cancels);
}
