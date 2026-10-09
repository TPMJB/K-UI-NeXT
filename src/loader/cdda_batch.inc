/* SPDX-License-Identifier: GPL-3.0-only */
/* Controlled multi-sector GD command experiment. Ordinary readers stay separate. */
#include "cdda_batch_client.h"
#include "kui/cdda_bios_batch.h"
#include "kui/cdda_handoff.h"
#include "kui/cdda_job.h"
#include "kui/cdda_vector.h"
static struct kui_cdda_bios_batch batch_owner;
static struct kui_cdda_service_guard batch_guard;
static struct kui_cdda_handoff_desc batch_descriptor;
static struct kui_cdda_stream_clock batch_elapsed;
static struct kui_cdda_job batch_data_job;
static struct cdda_batch_client_exports batch_exports;
static _Alignas(32) uint8_t batch_buffer[2048];
static uint32_t batch_checked,batch_errors,batch_calls,batch_execs,batch_accepted,batch_completions;
static uint32_t batch_refusals,batch_cancels,batch_resets,batch_reentries,batch_abi_checks,batch_checks,batch_drive_checks;
static uint32_t batch_client_stack_used,batch_service_stack_used,batch_vector_restored,batch_context_checks;
static uint32_t batch_guard_epoch,batch_data_budget,batch_last_data,batch_data_gap,batch_descriptor_rejects;
static uint32_t batch_old_vector,batch_report_stales;
static uint32_t batch_chunks,batch_full_passes,batch_size_classes,batch_min_class;
static uint32_t batch_progress_polls,batch_partial_cancels,batch_partial_resets;
static uint32_t batch_boundary_reads,batch_random_reads;
static uint32_t batch_pass_cursor;
static uint32_t batch_class_counts[8];
static const uint32_t batch_sizes[8]={1u,2u,3u,4u,7u,8u,15u,16u};
static struct kui_cdda_bios_batch_work batch_completion_work;
static bool batch_completion_pending;
static bool batch_native_busy,batch_reentry_armed,batch_installed,batch_reported;
#if CDDA_TEST_PROFILE == 10
static uint32_t batch_expected_gaps,batch_expected_prefix;
static bool batch_gap_armed;
#endif
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
static uint32_t batch_sp(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_host_batch_sp(void);return cdda_host_batch_sp();
#else
    uint32_t sp;__asm__ __volatile__("mov r15,%0":"=r"(sp));return sp;
#endif
}
static void batch_context(uint32_t out[3]) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_host_batch_context(uint32_t *);cdda_host_batch_context(out);
#else
    __asm__ __volatile__("stc sr,%0":"=r"(out[0]));
    __asm__ __volatile__("stc vbr,%0":"=r"(out[1]));
    __asm__ __volatile__("stc gbr,%0":"=r"(out[2]));
#endif
    out[0]&=~0x301u; /* Ordinary integer C may change T/Q/M (comparisons/division). */
}
static void batch_stacks_init(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_host_batch_stacks_init(void);cdda_host_batch_stacks_init();
#else
    uint32_t *begins[]={(uint32_t *)__cdda_service_stack_bottom,(uint32_t *)__cdda_client_stack_bottom};
    for(unsigned stack=0;stack<2;stack++) {
        for(unsigned i=0;i<16384u;i++) begins[stack][i]=i<16u?0x53435637u:0xa5a5a5a5u;
    }
    for(uint8_t *p=__cdda_client_bss_begin;p<__cdda_client_bss_end;p++) *p=0;
#endif
}
static bool batch_stack_check(unsigned which,uint32_t *used) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern bool cdda_host_batch_stack_check(unsigned,uint32_t *);
    return cdda_host_batch_stack_check(which,used);
#else
    uint32_t *base=(uint32_t *)(which?__cdda_client_stack_bottom:__cdda_service_stack_bottom);
    for(unsigned i=0;i<16u;i++) if(base[i]!=0x53435637u) return false;
    unsigned first=16u;while(first<16384u && base[first]==0xa5a5a5a5u) first++;
    *used=(16384u-first)*4u;return *used && *used<=65472u;
#endif
}
static uint32_t batch_clock_native(void *context) {
    return context==&batch_exports?ticks():UINT32_MAX;
}
static bool batch_stop_audio(void) {
    if(command_control.initialized) {
        uint32_t epoch=command_control.current.pending?command_control.current.pending_epoch:command_control.current.epoch;
        (void)kui_cdda_control_fail(&command_control,epoch);
    }
    if(batch_data_job.state==KUI_CDDA_JOB_READING || batch_data_job.state==KUI_CDDA_JOB_READY ||
       batch_data_job.state==KUI_CDDA_JOB_QUEUED) (void)kui_cdda_job_fail(&batch_data_job,batch_data_job.generation);
    command_running=false;
    bool ok=kui_cdda_aica_stop()==KUI_CDDA_AICA_OK;cdda_storage_close();return ok;
}
static uint32_t batch_fault_worker(const void *unused) {
    (void)unused;bool ok=batch_stop_audio();batch_completion_pending=false;
    if(batch_owner.initialized) {
        struct kui_cdda_bios_batch_work work;
        if(batch_owner.state==KUI_CDDA_BIOS_BATCH_QUEUED) {
            if(kui_cdda_bios_batch_take(&batch_owner,&work)!=KUI_CDDA_BIOS_BATCH_OK) ok=false;
        } else work=batch_owner.pending;
        if(batch_owner.state==KUI_CDDA_BIOS_BATCH_RUNNING &&
           kui_cdda_bios_batch_complete(&batch_owner,&work,false,0u)!=KUI_CDDA_BIOS_BATCH_OK) ok=false;
        if(kui_cdda_bios_batch_observe_audio(&batch_owner,KUI_CDDA_CONTROL_FAULT)!=KUI_CDDA_BIOS_BATCH_OK) ok=false;
    }
    return ok?KUI_CDDA_SERVICE_OK:KUI_CDDA_SERVICE_IO;
}
static uint32_t batch_worker_call(kui_cdda_client_entry entry,const void *context) {
#ifdef CDDA_HARNESS_HOST_TEST
    return kui_cdda_client_call(entry,context,(void *)(uintptr_t)0x8c230000u);
#else
    return kui_cdda_client_call(entry,context,__cdda_service_stack_top);
#endif
}
static uint32_t batch_fault_call(void) {
    batch_native_busy=true;uint32_t result=batch_worker_call(batch_fault_worker,NULL);batch_native_busy=false;
    return result;
}
static uint8_t batch_pattern(uint32_t at) {
    uint32_t x=at^0x9e3779b9u;x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (uint8_t)x;
}
static bool batch_prepare_descriptor(void) {
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
    d.client_entry=(uint32_t)(uintptr_t)&cdda_batch_client_entry;d.service_entry=(uint32_t)(uintptr_t)&cdda_bios_native_dispatch;
#endif
    if(kui_cdda_handoff_validate(&d)!=KUI_CDDA_HANDOFF_OK) return false;
    uint8_t wire[160];const uint8_t *native=(const uint8_t *)&d;
    for(unsigned i=0;i<sizeof(wire);i++) wire[i]=native[i]; /* Both builds are LE; decoder is byte-based. */
    if(kui_cdda_handoff_decode(wire,sizeof(wire),&batch_descriptor)!=KUI_CDDA_HANDOFF_OK) return false;
    for(unsigned i=0;i<8u;i++) {
        struct kui_cdda_handoff_desc bad=d;
        switch(i) {
        case 0:bad.version++;break;case 1:bad.resource_rights=0;break;
        case 2:bad.storage_transport=0;break;case 3:bad.client_stack_first=d.engine_stack_first;break;
        case 4:bad.client_entry=d.client_code_end;break;case 5:bad.service_entry=d.client_code_first;break;
        case 6:bad.service_stack_end=UINT32_MAX;break;default:bad.reserved[0]=1;break;
        }
        if(kui_cdda_handoff_validate(&bad)==KUI_CDDA_HANDOFF_OK) return false;
        batch_descriptor_rejects++;
    }
    return true;
}
static uint32_t batch_vector_read(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_batch_host_vector_read(void);return cdda_batch_host_vector_read();
#else
    return MMIO32(KUI_CDDA_BIOS_GD_VECTOR_ADDRESS);
#endif
}
static void batch_vector_write(uint32_t value) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_batch_host_vector_write(uint32_t);cdda_batch_host_vector_write(value);
#else
    uintptr_t slot=KUI_CDDA_BIOS_GD_VECTOR_ADDRESS;
    MMIO32(slot)=value;
    /* Publish the pointer word without changing cache or interrupt setup. */
    __asm__ __volatile__("ocbwb @%0\n\tocbi @%0"::"r"(slot):"memory");
#endif
}
static int32_t batch_nested_exec(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    return cdda_batch_host_vector_call(0,0,0,KUI_GD_EXEC);
#else
    kui_cdda_bios_vector_fn entry=(kui_cdda_bios_vector_fn)(uintptr_t)batch_vector_read();
    return entry(0,0,0,KUI_GD_EXEC);
#endif
}
static uint8_t *batch_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    if(context!=&batch_owner || address<0x8c310040u || address>=0x8c320000u ||
       !bytes || bytes>0x8c320000u-address) return NULL;
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint8_t *cdda_batch_host_map(uint32_t,uint32_t,int);
    return cdda_batch_host_map(address,bytes,writing);
#else
    (void)writing;return (uint8_t *)(uintptr_t)address;
#endif
}
static bool batch_data_admitted(void) {
    if(!command_running) return true;
    uint32_t margin;
    if(!ring.ready[0] || !ring.ready[1] ||
       !kui_cdda_frames_to_ticks(KUI_CDDA_HALF_FRAMES-ring.last_position%KUI_CDDA_HALF_FRAMES,&margin) ||
       batch_data_budget>(UINT32_MAX-KUI_CDDA_MARGIN_TICKS)/2u) return false;
    return margin>2u*batch_data_budget+KUI_CDDA_MARGIN_TICKS;
}
static bool batch_audio_service(void) {
    if(!command_running) return true;
    if(!command_sync()) return false;
    if(!command_running) return true;
    unsigned active=ring.last_position/KUI_CDDA_HALF_FRAMES;
    return (ring.ready[1u-active] || fill_half(1u-active,true)) && command_sync();
}
static bool batch_read_work(const struct kui_cdda_bios_batch_work *work) {
    uint32_t token;struct kui_cdda_job_span span;
    if(kui_cdda_bios_batch_validate_work(&batch_owner,work)!=KUI_CDDA_BIOS_BATCH_OK) return false;
    uint8_t *out=batch_map(&batch_owner,work->destination,work->bytes,1);
    if(!out ||
       kui_cdda_job_begin(&batch_data_job,KUI_CDDA_BIOS_BATCH_DATA_BYTES,work->offset,work->bytes,
          work->epoch,&token)!=KUI_CDDA_JOB_OK ||
       kui_cdda_job_claim(&batch_data_job,token,work->epoch,2048u,&span)!=KUI_CDDA_JOB_OK) return false;
    uint32_t first=ticks();
    int result=cdda_storage_data_read_at(span.offset,batch_buffer,span.bytes);
    bool ok=command_sync() && !result;
    for(uint32_t i=0;ok && i<span.bytes;i++) if(batch_buffer[i]!=batch_pattern(span.offset+i)) ok=false;
    uint32_t elapsed=ticks()-first;if(elapsed>batch_data_budget) batch_data_budget=elapsed;
    if(!ok || !command_sync() || ticks()-batch_guard.last_tick>=batch_guard.gap_ticks-KUI_CDDA_MARGIN_TICKS ||
       kui_cdda_bios_batch_validate_work(&batch_owner,work)!=KUI_CDDA_BIOS_BATCH_OK) {
        (void)kui_cdda_job_ready(&batch_data_job,&span,work->epoch,false);return false;
    }
    /* Check the exact identities before publishing any guest data. */
    if(batch_checked>UINT32_MAX-span.bytes ||
       kui_cdda_job_ready(&batch_data_job,&span,work->epoch,true)!=KUI_CDDA_JOB_OK) return false;
    for(uint32_t i=0;i<span.bytes;i++) out[i]=batch_buffer[i];
    return true; /* Progress remains READY until the complete service lease passes. */
}
static uint32_t batch_worker(const void *unused) {
    (void)unused;batch_completion_pending=false;uint32_t now=ticks(),sp=batch_sp();
    if(sp<0x8c220040u || sp>=0x8c230000u ||
       !kui_cdda_stream_clock_update(&batch_elapsed,now) ||
       (command_running && now-batch_last_data>=batch_data_gap)) return KUI_CDDA_SERVICE_IO;
    batch_execs++;
    if(batch_reentry_armed) {
        batch_reentry_armed=false;
        uint32_t calls=batch_calls,execs=batch_execs;
        if(batch_nested_exec()!=-1 || batch_calls!=calls+1u || batch_execs!=execs || batch_reentries!=1u)
            return KUI_CDDA_SERVICE_IO;
    }
    /* A pending control action stops/replaces the old ring before priming;
     * avoid an unnecessary old-half refill plus a two-half new prefill. */
    bool audio=batch_owner.state==KUI_CDDA_BIOS_BATCH_QUEUED && batch_owner.work.kind==KUI_CDDA_BIOS_BATCH_AUDIO;
    if(!audio && !batch_audio_service()) return KUI_CDDA_SERVICE_IO;
    if(batch_owner.state==KUI_CDDA_BIOS_BATCH_QUEUED && batch_owner.work.kind==KUI_CDDA_BIOS_BATCH_DATA &&
       !batch_data_admitted()) {
        if(kui_cdda_bios_batch_observe_audio(&batch_owner,command_control.current.state)!=KUI_CDDA_BIOS_BATCH_OK)
            return KUI_CDDA_SERVICE_IO;
        return KUI_CDDA_SERVICE_OK;
    }
    struct kui_cdda_bios_batch_work work;
    enum kui_cdda_bios_batch_result taken=kui_cdda_bios_batch_take(&batch_owner,&work);
    if(taken==KUI_CDDA_BIOS_BATCH_NOTHING) {
        return kui_cdda_bios_batch_observe_audio(&batch_owner,command_control.current.state)==KUI_CDDA_BIOS_BATCH_OK?
            KUI_CDDA_SERVICE_OK:KUI_CDDA_SERVICE_IO;
    }
    if(taken!=KUI_CDDA_BIOS_BATCH_OK || kui_cdda_bios_batch_validate_work(&batch_owner,&work)!=KUI_CDDA_BIOS_BATCH_OK)
        return KUI_CDDA_SERVICE_IO;
    bool success=true;
    if(work.kind==KUI_CDDA_BIOS_BATCH_AUDIO) success=command_accept(work.audio);
    else if(work.kind==KUI_CDDA_BIOS_BATCH_DATA) success=batch_read_work(&work);
    if(!success || kui_cdda_bios_batch_observe_audio(&batch_owner,command_control.current.state)!=KUI_CDDA_BIOS_BATCH_OK)
        return KUI_CDDA_SERVICE_IO;
    batch_completion_work=work;batch_completion_pending=true;
    return KUI_CDDA_SERVICE_OK;
}
int32_t cdda_bios_native_dispatch(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    batch_calls++;
    /* The assembly entry has kept the caller stack. Refuse nested work before
     * changing stack, querying time/phase, mapping memory or touching SCI. */
    if(batch_native_busy) {
        batch_reentries++;
        return r7==KUI_GD_REQUEST?0:r7==KUI_GD_CHECK?4:-1;
    }
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_batch_host_caller_sp(void);uint32_t sp=cdda_batch_host_caller_sp();
#else
    uint32_t sp;__asm__ __volatile__("mov r15,%0":"=r"(sp));
#endif
    if(!batch_installed || sp<0x8c310040u || sp>=0x8c320000u) return -1;
    int32_t result;
    if(r6 || r7>KUI_GD_PIO_CHECK) result=-1;
    else if((r7==KUI_GD_INIT || r7==KUI_GD_RESET) &&
            command_control.current.state!=KUI_CDDA_CONTROL_STOPPED &&
            command_control.current.state!=KUI_CDDA_CONTROL_EOF) result=-1;
    else if(r7==KUI_GD_EXEC) {
        struct kui_cdda_service_ticket ticket;
        enum kui_cdda_service_result admitted=kui_cdda_service_enter(&batch_guard,batch_guard_epoch,ticks(),&ticket);
        if(admitted!=KUI_CDDA_SERVICE_OK) {
#if CDDA_TEST_PROFILE == 10
            bool expected=batch_gap_armed && admitted==KUI_CDDA_SERVICE_DEADLINE &&
                batch_owner.state==KUI_CDDA_BIOS_BATCH_QUEUED &&
                batch_owner.work.kind==KUI_CDDA_BIOS_BATCH_DATA && batch_owner.completed_bytes==2048u;
            batch_gap_armed=false;
            if(expected) {batch_expected_gaps++;batch_expected_prefix=batch_owner.completed_bytes;}
            else batch_errors++;
#else
            batch_errors++;
#endif
            if(batch_fault_call()!=KUI_CDDA_SERVICE_OK) batch_errors++;
            result=-1;
        } else {
            batch_native_busy=true;uint32_t work=batch_worker_call(batch_worker,NULL);
            enum kui_cdda_service_result left_guard=kui_cdda_service_leave(&batch_guard,&ticket,ticks(),work==KUI_CDDA_SERVICE_OK);
            bool committed=left_guard==KUI_CDDA_SERVICE_OK;
            if(committed && batch_completion_pending) {
                struct kui_cdda_bios_batch_work *pending=&batch_completion_work;
                committed=kui_cdda_bios_batch_validate_work(&batch_owner,pending)==KUI_CDDA_BIOS_BATCH_OK;
                if(committed && pending->kind==KUI_CDDA_BIOS_BATCH_DATA) {
                    struct kui_cdda_job_span span=batch_data_job.pending;
                    committed=kui_cdda_job_commit(&batch_data_job,&span,pending->epoch)==KUI_CDDA_JOB_COMPLETE;
                    if(committed) {batch_checked+=pending->bytes;batch_last_data=ticks();}
                }
                if(committed) committed=kui_cdda_bios_batch_complete(&batch_owner,pending,true,pending->bytes)==KUI_CDDA_BIOS_BATCH_OK;
                if(committed && pending->kind==KUI_CDDA_BIOS_BATCH_DATA) batch_chunks++;
                if(committed && batch_owner.state==KUI_CDDA_BIOS_BATCH_TERMINAL) {
                    batch_completions++;
                    if(pending->kind==KUI_CDDA_BIOS_BATCH_DATA) {
                        uint32_t offset=batch_owner.work.offset,total=batch_owner.work.bytes;
                        if(offset!=batch_pass_cursor) batch_pass_cursor=0u;
                        if(offset==batch_pass_cursor) {
                            batch_pass_cursor+=total;
                            if(batch_pass_cursor==KUI_CDDA_BIOS_BATCH_DATA_BYTES) {
                                batch_full_passes++;batch_pass_cursor=0u;
                            }
                        }
                        for(unsigned i=0;i<8u;i++)
                            if(total/2048u==batch_sizes[i]) batch_class_counts[i]++;
                    }
                }
            }
            batch_native_busy=false;batch_completion_pending=false;
            if(!committed) {
                batch_errors++;(void)batch_fault_call();result=-1;
            } else result=0;
        }
    } else {
        uint32_t prefix=batch_owner.completed_bytes;
        bool queued=batch_owner.state==KUI_CDDA_BIOS_BATCH_QUEUED;
        batch_native_busy=true;result=kui_cdda_bios_batch_dispatch(&batch_owner,r4,r5,r6,r7);batch_native_busy=false;
        if(r7==KUI_GD_REQUEST && result>0) batch_accepted++;
        if(r7==KUI_GD_CHECK) {
            batch_checks++;
            if(result==KUI_GD_PROCESSING && prefix && queued) batch_progress_polls++;
        }
        if(r7==KUI_GD_DRIVE && result==0) batch_drive_checks++;
        if(r7==KUI_GD_ABORT && result==0) {
            batch_cancels++;if(prefix && queued) batch_partial_cancels++;
        }
        if((r7==KUI_GD_INIT || r7==KUI_GD_RESET) && result==0) {
            batch_resets++;if(prefix && queued) batch_partial_resets++;
        }
    }
    if((r7==KUI_GD_REQUEST && result==0) || (r7!=KUI_GD_REQUEST && result==-1)) batch_refusals++;
    return result;
}
static uint32_t batch_diagnostic(void *context,struct cdda_batch_client_diagnostic *r) {
    if(context!=&batch_exports || !r || batch_native_busy) return 1u;
#ifndef CDDA_HARNESS_HOST_TEST
    uintptr_t p=(uintptr_t)r;
    if(p%4u || p<0x8c310040u || p>0x8c320000u-sizeof(*r)) return 1u;
#endif
    if(r->pc<0x8c300000u || r->pc>=0x8c310000u || r->pc&1u ||
       r->sp<0x8c310040u || r->sp>=0x8c320000u || r->sp&3u) return 1u;
    r->result=0;
    switch(r->op) {
    case CDDA_BATCH_CLIENT_MARK:
        if(r->value!=completed+1u || r->value>7u) return r->result=1u;
        completed++;break;
    case CDDA_BATCH_CLIENT_ARM_REENTRY:
        if(batch_reentry_armed || batch_reentries) return r->result=1u;
        batch_reentry_armed=true;break;
#if CDDA_TEST_PROFILE == 10
    case CDDA_BATCH_CLIENT_ARM_GAP:
        if(batch_gap_armed || batch_expected_gaps || !command_running ||
           batch_guard.state!=KUI_CDDA_SERVICE_ACTIVE ||
           batch_owner.state!=KUI_CDDA_BIOS_BATCH_QUEUED || batch_owner.work.kind!=KUI_CDDA_BIOS_BATCH_DATA ||
           batch_owner.work.handle!=r->value || batch_owner.work.bytes!=32768u ||
           batch_owner.completed_bytes!=2048u) return r->result=1u;
        batch_gap_armed=true;break;
#endif
    case CDDA_BATCH_CLIENT_REPORT:
#if CDDA_TEST_PROFILE == 10
        if(batch_reported || r->value!=16u || batch_calls!=17u || r->vector_calls!=r->value ||
           r->read_checked!=batch_checked || batch_checked!=2048u || batch_chunks!=1u ||
           r->execs!=3u || batch_execs!=2u || r->checks!=7u || batch_checks!=7u ||
           r->drive_checks!=2u || batch_drive_checks!=2u ||
           r->expected_refusals!=4u || batch_refusals!=4u || r->stale_checks!=1u ||
           r->cancels || r->resets || batch_cancels || batch_resets ||
           batch_accepted!=2u || batch_completions!=1u ||
           batch_expected_gaps!=1u || batch_expected_prefix!=2048u || batch_gap_armed ||
           batch_errors || batch_reentries!=1u || command_running ||
           command_control.current.state!=KUI_CDDA_CONTROL_FAULT || batch_guard.state!=KUI_CDDA_SERVICE_FAULT ||
           batch_owner.state!=KUI_CDDA_BIOS_BATCH_EMPTY ||
           r->progress_polls!=2u || batch_progress_polls!=2u) return r->result=1u;
#else
        if(batch_reported || r->value<100u || r->value!=batch_calls-1u || r->vector_calls!=r->value ||
           r->read_checked!=batch_checked || r->drive_checks!=batch_drive_checks || r->checks!=batch_checks ||
           r->execs!=batch_execs || batch_checked!=batch_chunks*2048u ||
           r->expected_refusals!=batch_refusals || r->cancels!=batch_cancels || r->resets!=batch_resets ||
           batch_accepted!=batch_completions+2u || batch_cancels!=1u || batch_resets!=1u ||
           r->stale_checks!=3u || batch_drive_checks!=11u || batch_refusals!=8u || batch_reentries!=1u ||
           r->partial_cancels!=batch_partial_cancels || batch_partial_cancels!=1u ||
           r->partial_resets!=batch_partial_resets || batch_partial_resets!=1u ||
           r->progress_polls!=batch_progress_polls || batch_progress_polls<32u ||
           r->full_passes!=batch_full_passes || batch_full_passes!=1u ||
           r->full_pass_bytes!=KUI_CDDA_BIOS_BATCH_DATA_BYTES ||
           r->boundary_reads!=24u || r->random_reads<8u) return r->result=1u;
        for(unsigned i=0;i<8u;i++) {
            if(r->class_counts[i]!=batch_class_counts[i] || batch_class_counts[i]<16u) return r->result=1u;
            batch_size_classes++;
            if(batch_class_counts[i]<batch_min_class) batch_min_class=batch_class_counts[i];
        }
        batch_boundary_reads=r->boundary_reads;batch_random_reads=r->random_reads;
#endif
        batch_abi_checks=r->value;batch_report_stales=r->stale_checks;batch_reported=true;break;
    case CDDA_BATCH_CLIENT_SNAPSHOT:
        r->state=command_control.current.state;r->frame=command_control.current.frame;r->prefetch=pcm.position;
        r->vector_calls=batch_calls;r->execs=batch_execs;r->checks=batch_checks;r->drive_checks=batch_drive_checks;
        r->read_checked=batch_checked;r->expected_refusals=batch_refusals;
        r->cancels=batch_cancels;r->resets=batch_resets;break;
    default:return r->result=1u;
    }
    return 0;
}
static bool cdda_batch_test(void) {
#if CDDA_TEST_PROFILE == 10
    batch_expected_gaps=batch_expected_prefix=0u;batch_gap_armed=false;
#endif
    batch_calls=batch_execs=batch_accepted=batch_completions=batch_refusals=batch_cancels=batch_resets=0;
    batch_checked=batch_errors=batch_reentries=batch_abi_checks=batch_checks=batch_drive_checks=0;
    batch_chunks=batch_full_passes=batch_size_classes=batch_progress_polls=0;
    batch_partial_cancels=batch_partial_resets=batch_boundary_reads=batch_random_reads=batch_pass_cursor=0;
    batch_min_class=UINT32_MAX;
    for(unsigned i=0;i<8u;i++) batch_class_counts[i]=0u;
    batch_client_stack_used=batch_service_stack_used=batch_vector_restored=batch_context_checks=batch_descriptor_rejects=0;
    batch_installed=batch_native_busy=batch_reentry_armed=batch_reported=batch_completion_pending=false;
    command_running=false;command_accepted=command_completions=command_status_checks=command_paused_frame=command_max_loops=0;
    batch_stacks_init();cdda_display_line("1/8: validate owned handoff and native GD vector");
    if(!batch_prepare_descriptor() || kui_cdda_service_init(&batch_guard)!=KUI_CDDA_SERVICE_OK ||
       kui_cdda_job_init(&batch_data_job)!=KUI_CDDA_JOB_OK ||
       !kui_cdda_ms_to_ticks(10u,&batch_data_budget) || !kui_cdda_ms_to_ticks(5000u,&batch_data_gap)) goto failed;
    uint32_t bytes,source_bytes;
    if(cdda_storage_data_open(&bytes) || bytes!=KUI_CDDA_BIOS_BATCH_DATA_BYTES ||
       cdda_storage_open("0:/KUI/tests/cdda/stereo.raw",&source_bytes)) goto failed;
    cdda_storage_close();
    struct kui_cdda_bios_batch_ops ops={&batch_owner,batch_map};
    if(!source_bytes || source_bytes%2352u ||
       kui_cdda_control_init(&command_control,source_bytes/4u)!=KUI_CDDA_CONTROL_OK ||
       kui_cdda_bios_batch_init(&batch_owner,&ops,source_bytes/4u,bytes,0x8c310040u,0x8c320000u)!=KUI_CDDA_BIOS_BATCH_OK ||
       !kui_cdda_stream_clock_init(&batch_elapsed,ticks(),KUI_CDDA_TMU_HZ)) goto failed;
    batch_last_data=ticks();
    if(kui_cdda_service_start(&batch_guard,batch_last_data,KUI_CDDA_HALF_TICKS,&batch_guard_epoch)!=KUI_CDDA_SERVICE_OK) goto failed;
    batch_exports=(struct cdda_batch_client_exports){CDDA_BATCH_CLIENT_MAGIC,CDDA_BATCH_CLIENT_REVISION,
        sizeof(batch_exports),&batch_exports,batch_clock_native,kui_cdda_bridge_probe,batch_diagnostic};
    batch_old_vector=batch_vector_read();
#ifdef CDDA_HARNESS_HOST_TEST
    uint32_t hook=0x8c010200u;
#else
    uint32_t hook=(uint32_t)(uintptr_t)&kui_cdda_bios_vector_entry;
#endif
    batch_installed=true;batch_vector_write(hook);
    if(batch_vector_read()!=hook) goto failed;
    completed++;
#if CDDA_TEST_PROFILE == 10
    cdda_display_line("2..7/8: confirm sector; EXPECTED 200ms gap");
#else
    cdda_display_line("2..7/8: BIOS multi-sector READ + audio;90s");
#endif
    uint32_t before[3],after[3];batch_context(before);
#ifdef CDDA_HARNESS_HOST_TEST
    uint32_t result=kui_cdda_client_call(cdda_batch_client_entry,&batch_exports,(void *)(uintptr_t)0x8c320000u);
#else
    uint32_t result=kui_cdda_client_call(cdda_batch_client_entry,&batch_exports,__cdda_client_stack_top);
#endif
    batch_context(after);
    batch_vector_write(batch_old_vector);batch_vector_restored=batch_vector_read()==batch_old_vector;
    if(batch_vector_restored) batch_installed=false;
    if(result || before[0]!=after[0] || before[1]!=after[1] || before[2]!=after[2]) goto failed;
    batch_context_checks=1u;
#if CDDA_TEST_PROFILE == 10
    if(!batch_stack_check(0,&batch_service_stack_used) || !batch_stack_check(1,&batch_client_stack_used) ||
       completed!=7u || batch_checked!=2048u || batch_chunks!=1u || batch_errors ||
       !batch_reported || batch_descriptor_rejects!=8u || batch_reentries!=1u || batch_report_stales!=1u ||
       !batch_vector_restored || command_running || command_completions!=1u ||
       batch_expected_gaps!=1u || batch_expected_prefix!=2048u || batch_gap_armed ||
       command_control.current.state!=KUI_CDDA_CONTROL_FAULT || batch_owner.state!=KUI_CDDA_BIOS_BATCH_EMPTY ||
       batch_guard.state!=KUI_CDDA_SERVICE_FAULT) goto failed;
#else
    if(!batch_stack_check(0,&batch_service_stack_used) || !batch_stack_check(1,&batch_client_stack_used) ||
       completed!=7u || batch_elapsed.seconds<90u || batch_elapsed.seconds>120u ||
       batch_checked<KUI_CDDA_BIOS_BATCH_DATA_BYTES+4096u || batch_errors ||
       batch_full_passes!=1u || batch_size_classes!=8u || batch_min_class<16u ||
       batch_progress_polls<32u || batch_partial_cancels!=1u || batch_partial_resets!=1u ||
       batch_boundary_reads!=24u || batch_random_reads<8u ||
       !batch_reported || batch_descriptor_rejects!=8u || batch_reentries!=1u || batch_report_stales!=3u ||
       !batch_vector_restored || command_running || command_completions!=6u ||
       command_control.current.state!=KUI_CDDA_CONTROL_STOPPED || batch_owner.state!=KUI_CDDA_BIOS_BATCH_EMPTY ||
       kui_cdda_service_stop(&batch_guard,batch_guard_epoch)!=KUI_CDDA_SERVICE_OK) goto failed;
#endif
    cdda_storage_data_close();completed++;return true;
failed:
    batch_errors++;(void)batch_fault_call();cdda_storage_data_close();
    if(batch_guard.initialized && batch_guard.state==KUI_CDDA_SERVICE_ACTIVE)
        (void)kui_cdda_service_stop(&batch_guard,batch_guard.generation);
    if(batch_installed) {
        batch_vector_write(batch_old_vector);batch_vector_restored=batch_vector_read()==batch_old_vector;batch_installed=false;
    }
    cdda_display_line("BIOS/vector test failed; owned audio stopped.");return false;
}
static void cdda_batch_report(void) {
    display_pair("Client seconds / vector calls: ",batch_elapsed.seconds,batch_calls);
#if CDDA_TEST_PROFILE == 10
    display_pair("EXPECTED gaps / service errors: ",batch_expected_gaps,batch_errors);
    display_pair("Checked bytes / committed chunks: ",batch_checked,batch_chunks);
    display_pair("Confirmed prefix / progress checks: ",batch_expected_prefix,batch_progress_polls);
    display_pair("Client / service stack bytes: ",batch_client_stack_used,batch_service_stack_used);
    display_pair("ABI checks / vector restored: ",batch_abi_checks,batch_vector_restored);
    display_pair("Expected negatives / stale checks: ",batch_refusals,batch_report_stales);
#else
    display_pair("BIOS requests / completions: ",batch_accepted,batch_completions);
    display_pair("Checked bytes / full8MiB passes: ",batch_checked,batch_full_passes);
    display_pair("Size classes / minimum completions: ",batch_size_classes,batch_min_class==UINT32_MAX?0u:batch_min_class);
    display_pair("Committed chunks / progress checks: ",batch_chunks,batch_progress_polls);
    display_pair("Client / service stack bytes: ",batch_client_stack_used,batch_service_stack_used);
    display_pair("ABI checks / vector restored: ",batch_abi_checks,batch_vector_restored);
#endif
}
