/* SPDX-License-Identifier: GPL-3.0-only */
/* Checked multi-file GDI experiment. All I/O remains owned and cooperative. */
#include "cdda_disc_client.h"
#include "kui/cdda_disc_bios.h"
#include "kui/cdda_handoff.h"
#include "kui/cdda_job.h"
#include "kui/cdda_vector.h"
static struct kui_cdda_disc_bios disc_owner;
static struct kui_cdda_service_guard disc_guard;
static struct kui_cdda_handoff_desc disc_descriptor;
static struct kui_cdda_stream_clock disc_elapsed;
static struct kui_cdda_job disc_data_job;
static struct cdda_disc_client_exports disc_exports;
static _Alignas(32) uint8_t disc_buffer[2352];
static uint32_t disc_checked,disc_errors,disc_calls,disc_execs,disc_accepted,disc_completions;
static uint32_t disc_refusals,disc_cancels,disc_resets,disc_reentries,disc_abi_checks,disc_checks,disc_drive_checks;
static uint32_t disc_client_stack_used,disc_service_stack_used,disc_vector_restored,disc_context_checks;
static uint32_t disc_guard_epoch,disc_data_budget,disc_descriptor_rejects;
static uint32_t disc_old_vector,disc_report_stales;
static uint32_t disc_chunks,disc_progress_polls,disc_poll_handle,disc_poll_prefix;
static struct kui_cdda_disc_bios_work disc_completion_work;
static bool disc_completion_pending;
static bool disc_native_busy,disc_reentry_armed,disc_installed,disc_reported;

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
static uint32_t disc_sp(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_host_disc_sp(void);return cdda_host_disc_sp();
#else
    uint32_t sp;__asm__ __volatile__("mov r15,%0":"=r"(sp));return sp;
#endif
}
static void disc_context(uint32_t out[3]) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_host_disc_context(uint32_t *);cdda_host_disc_context(out);
#else
    __asm__ __volatile__("stc sr,%0":"=r"(out[0]));
    __asm__ __volatile__("stc vbr,%0":"=r"(out[1]));
    __asm__ __volatile__("stc gbr,%0":"=r"(out[2]));
#endif
    out[0]&=~0x301u; /* Ordinary integer C may change T/Q/M (comparisons/division). */
}
static void disc_stacks_init(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_host_disc_stacks_init(void);cdda_host_disc_stacks_init();
#else
    uint32_t *begins[]={(uint32_t *)__cdda_service_stack_bottom,(uint32_t *)__cdda_client_stack_bottom};
    for(unsigned stack=0;stack<2;stack++) {
        for(unsigned i=0;i<16384u;i++) begins[stack][i]=i<16u?0x53435637u:0xa5a5a5a5u;
    }
    for(uint8_t *p=__cdda_client_bss_begin;p<__cdda_client_bss_end;p++) *p=0;
#endif
}
static bool disc_stack_check(unsigned which,uint32_t *used) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern bool cdda_host_disc_stack_check(unsigned,uint32_t *);
    return cdda_host_disc_stack_check(which,used);
#else
    uint32_t *base=(uint32_t *)(which?__cdda_client_stack_bottom:__cdda_service_stack_bottom);
    for(unsigned i=0;i<16u;i++) if(base[i]!=0x53435637u) return false;
    unsigned first=16u;while(first<16384u && base[first]==0xa5a5a5a5u) first++;
    *used=(16384u-first)*4u;return *used && *used<=65472u;
#endif
}
static uint32_t disc_clock_native(void *context) {
    return context==&disc_exports?ticks():UINT32_MAX;
}
static bool disc_stop_audio(void) {
    if(command_control.initialized) {
        uint32_t epoch=command_control.current.pending?command_control.current.pending_epoch:command_control.current.epoch;
        (void)kui_cdda_control_fail(&command_control,epoch);
    }
    if(disc_data_job.state==KUI_CDDA_JOB_READING || disc_data_job.state==KUI_CDDA_JOB_READY ||
       disc_data_job.state==KUI_CDDA_JOB_QUEUED) (void)kui_cdda_job_fail(&disc_data_job,disc_data_job.generation);
    command_running=false;
    bool ok=kui_cdda_aica_stop()==KUI_CDDA_AICA_OK;cdda_storage_close();return ok;
}
static uint32_t disc_fault_worker(const void *unused) {
    (void)unused;bool ok=disc_stop_audio();disc_completion_pending=false;
    if(disc_owner.initialized) {
        struct kui_cdda_disc_bios_work work;
        if(disc_owner.state==KUI_CDDA_DISC_BIOS_QUEUED) {
            if(kui_cdda_disc_bios_take(&disc_owner,&work)!=KUI_CDDA_DISC_BIOS_OK) ok=false;
        } else work=disc_owner.pending;
        if(disc_owner.state==KUI_CDDA_DISC_BIOS_RUNNING &&
           kui_cdda_disc_bios_complete(&disc_owner,&work,false,0u)!=KUI_CDDA_DISC_BIOS_OK) ok=false;
        if(kui_cdda_disc_bios_observe_audio(&disc_owner,KUI_CDDA_CONTROL_FAULT)!=KUI_CDDA_DISC_BIOS_OK) ok=false;
    }
    return ok?KUI_CDDA_SERVICE_OK:KUI_CDDA_SERVICE_IO;
}
static uint32_t disc_worker_call(kui_cdda_client_entry entry,const void *context) {
#ifdef CDDA_HARNESS_HOST_TEST
    return kui_cdda_client_call(entry,context,(void *)(uintptr_t)0x8c230000u);
#else
    return kui_cdda_client_call(entry,context,__cdda_service_stack_top);
#endif
}
static uint32_t disc_fault_call(void) {
    disc_native_busy=true;uint32_t result=disc_worker_call(disc_fault_worker,NULL);disc_native_busy=false;
    return result;
}
static uint8_t disc_pattern(uint32_t track,uint32_t at) {
    uint32_t x=at^(track*0x9e3779b9u);x^=x>>16;x*=0x7feb352du;x^=x>>15;x*=0x846ca68bu;x^=x>>16;return (uint8_t)x;
}
static bool disc_prepare_descriptor(void) {
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
    d.client_entry=(uint32_t)(uintptr_t)&cdda_disc_client_entry;d.service_entry=(uint32_t)(uintptr_t)&cdda_bios_native_dispatch;
#endif
    if(kui_cdda_handoff_validate(&d)!=KUI_CDDA_HANDOFF_OK) return false;
    uint8_t wire[160];const uint8_t *native=(const uint8_t *)&d;
    for(unsigned i=0;i<sizeof(wire);i++) wire[i]=native[i]; /* Both builds are LE; decoder is byte-based. */
    if(kui_cdda_handoff_decode(wire,sizeof(wire),&disc_descriptor)!=KUI_CDDA_HANDOFF_OK) return false;
    for(unsigned i=0;i<8u;i++) {
        struct kui_cdda_handoff_desc bad=d;
        switch(i) {
        case 0:bad.version++;break;case 1:bad.resource_rights=0;break;
        case 2:bad.storage_transport=0;break;case 3:bad.client_stack_first=d.engine_stack_first;break;
        case 4:bad.client_entry=d.client_code_end;break;case 5:bad.service_entry=d.client_code_first;break;
        case 6:bad.service_stack_end=UINT32_MAX;break;default:bad.reserved[0]=1;break;
        }
        if(kui_cdda_handoff_validate(&bad)==KUI_CDDA_HANDOFF_OK) return false;
        disc_descriptor_rejects++;
    }
    return true;
}
static uint32_t disc_vector_read(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_disc_host_vector_read(void);return cdda_disc_host_vector_read();
#else
    return MMIO32(KUI_CDDA_BIOS_GD_VECTOR_ADDRESS);
#endif
}
static void disc_vector_write(uint32_t value) {
#ifdef CDDA_HARNESS_HOST_TEST
    extern void cdda_disc_host_vector_write(uint32_t);cdda_disc_host_vector_write(value);
#else
    uintptr_t slot=KUI_CDDA_BIOS_GD_VECTOR_ADDRESS;
    MMIO32(slot)=value;
    /* Publish the pointer word without changing cache or interrupt setup. */
    __asm__ __volatile__("ocbwb @%0\n\tocbi @%0"::"r"(slot):"memory");
#endif
}
static int32_t disc_nested_exec(void) {
#ifdef CDDA_HARNESS_HOST_TEST
    return cdda_disc_host_vector_call(0,0,0,KUI_GD_EXEC);
#else
    kui_cdda_bios_vector_fn entry=(kui_cdda_bios_vector_fn)(uintptr_t)disc_vector_read();
    return entry(0,0,0,KUI_GD_EXEC);
#endif
}
static uint8_t *disc_map(void *context,uint32_t address,uint32_t bytes,int writing) {
    if(context!=&disc_owner || address<0x8c310040u || address>=0x8c320000u ||
       !bytes || bytes>0x8c320000u-address) return NULL;
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint8_t *cdda_disc_host_map(uint32_t,uint32_t,int);
    return cdda_disc_host_map(address,bytes,writing);
#else
    (void)writing;return (uint8_t *)(uintptr_t)address;
#endif
}
static bool disc_data_admitted(void) {
    if(!command_running) return true;
    uint32_t margin;
    if(!ring.ready[0] || !ring.ready[1] ||
       !kui_cdda_frames_to_ticks(KUI_CDDA_HALF_FRAMES-ring.last_position%KUI_CDDA_HALF_FRAMES,&margin) ||
       disc_data_budget>(UINT32_MAX-KUI_CDDA_MARGIN_TICKS)/2u) return false;
    return margin>2u*disc_data_budget+KUI_CDDA_MARGIN_TICKS;
}
static bool disc_audio_service(void) {
    if(!command_running) return true;
    if(!command_sync()) return false;
    if(!command_running) return true;
    unsigned active=ring.last_position/KUI_CDDA_HALF_FRAMES;
    return (ring.ready[1u-active] || fill_half(1u-active,true)) && command_sync();
}
static struct kui_cdda_disc_map disc_image_map;
#if CDDA_TEST_PROFILE == 11
static struct kui_game_image disc_parsed_image;
#endif
static uint8_t disc_gdi[KUI_GAME_GDI_LIMIT];
static char disc_audio_name[256];
static uint32_t disc_data_track,disc_track_switches,disc_toc_completions;
static uint32_t disc_report_map_checks,disc_report_eof_checks,disc_actions;
static uint32_t disc_map_checks,disc_eof_checks,disc_eof_epoch;

static bool disc_path(const char *name,char output[256]) {
#if CDDA_TEST_PROFILE == 11
    const char *base="0:/KUI/tests/cdda/disc/";
#else
    const char *base="0:/KUI/tests/cdda/";
#endif
    unsigned at=0;
    while(*base) output[at++]=*base++;
    if(!name || !*name) return false;
    while(*name) {
        unsigned char ch=(unsigned char)*name++;
        if(at>=255u || ch<32u || ch>126u || ch=='/' || ch=='\\' || ch==':') return false;
        output[at++]=(char)ch;
    }
    output[at]=0;return true;
}
#if CDDA_TEST_PROFILE == 11
static enum kui_game_result disc_file_stat(void *context,const char *name,uint64_t *bytes) {
    (void)context;char path[256];uint32_t size;
    if(!bytes || !disc_path(name,path)) return KUI_GAME_INVALID;
    if(cdda_storage_named_stat(path,&size)) return KUI_GAME_IO;
    *bytes=size;return KUI_GAME_OK;
}
static enum kui_game_result disc_file_read(void *context,const char *name,uint64_t at,void *out,size_t bytes) {
    (void)context;(void)name;(void)at;(void)out;(void)bytes;
    /* GDI open is descriptor/stat-only. Other containers are not admitted. */
    return KUI_GAME_UNSUPPORTED;
}
#endif
static bool disc_load_map(uint32_t *source_frames) {
    uint32_t bytes;
#if CDDA_TEST_PROFILE == 11
    const char *descriptor="0:/KUI/tests/cdda/disc/fixture.gdi";
#else
    const char *descriptor="0:/KUI/tests/cdda/TOY_COMMANDER.gdi";
#endif
    if(!source_frames || cdda_storage_open(descriptor,&bytes)) return false;
    bool loaded=bytes && bytes<=sizeof(disc_gdi) && !cdda_storage_read_at(0,disc_gdi,bytes);
    cdda_storage_close();if(!loaded) return false;
#if CDDA_TEST_PROFILE == 11
    const struct kui_game_file_ops files={NULL,disc_file_stat,disc_file_read};
    if(kui_game_image_open(disc_gdi,bytes,&files,&disc_parsed_image)!=KUI_GAME_OK ||
       kui_cdda_disc_from_image(&disc_parsed_image,&disc_image_map)!=KUI_CDDA_DISC_OK ||
       disc_image_map.count!=6u || !disc_image_map.complete) return false;
#else
    uint32_t actual;
    if(cdda_storage_named_stat("0:/KUI/tests/cdda/track14.raw",&actual) ||
       actual!=7222992u || kui_cdda_disc_selected_audio(disc_gdi,bytes,14u,actual,&disc_image_map)!=KUI_CDDA_DISC_OK)
        return false;
    const struct kui_cdda_disc_track *track;
    if(kui_cdda_disc_track(&disc_image_map,14u,&track)!=KUI_CDDA_DISC_OK ||
       track->start_fad!=374351u || track->end_fad!=377422u || track->file_offset || track->stride!=2352u ||
       track->name[0]!='t' || track->name[1]!='r' || track->name[2]!='a' || track->name[3]!='c' ||
       track->name[4]!='k' || track->name[5]!='1' || track->name[6]!='4' || track->name[7]!='.' ||
       track->name[8]!='r' || track->name[9]!='a' || track->name[10]!='w' || track->name[11]) return false;
#endif
    *source_frames=0;
    for(uint32_t i=0;i<disc_image_map.count;i++) {
        const struct kui_cdda_disc_track *t=&disc_image_map.tracks[i];
        if(t->control==0u) {
            uint32_t frames=(t->end_fad-t->start_fad)*588u;
            if(frames>*source_frames) *source_frames=frames;
        }
    }
    return *source_frames!=0;
}
static bool disc_bind_audio(const struct kui_cdda_disc_bios_work *work) {
    const struct kui_cdda_disc_track *track;struct kui_cdda_disc_audio audio;
    if(work->command!=KUI_CDDA_DISC_BIOS_PLAY) return true;
    if(kui_cdda_disc_track(&disc_image_map,work->track,&track)!=KUI_CDDA_DISC_OK ||
       kui_cdda_disc_audio_range(&disc_image_map,track->start_fad,track->end_fad,&audio)!=KUI_CDDA_DISC_OK ||
       audio.first_frame!=work->audio.first || audio.end_frame!=work->audio.end ||
       !disc_path(track->name,disc_audio_name)) return false;
    /* The old FIL remains open until command_finish stops it. A new PLAY may
     * bind a different length without resetting the control epoch namespace. */
    disc_audio_path=disc_audio_name;disc_audio_source=audio.source;
    return true;
}
static bool disc_read_work(const struct kui_cdda_disc_bios_work *work) {
    const struct kui_cdda_disc_track *track;uint32_t token;struct kui_cdda_job_span span;
    if(kui_cdda_disc_bios_validate_work(&disc_owner,work)!=KUI_CDDA_DISC_BIOS_OK ||
       kui_cdda_disc_track(&disc_image_map,work->track,&track)!=KUI_CDDA_DISC_OK || track->control!=4u)
        return false;
    uint8_t *out=disc_map(&disc_owner,work->destination,work->bytes,1);
    uint32_t virtual_bytes=(track->end_fad-track->start_fad)*2048u;
    if(!out || kui_cdda_job_begin(&disc_data_job,virtual_bytes,work->offset,work->bytes,
       work->epoch,&token)!=KUI_CDDA_JOB_OK ||
       kui_cdda_job_claim(&disc_data_job,token,work->epoch,2048u,&span)!=KUI_CDDA_JOB_OK) return false;
    uint32_t first=ticks();bool ok=true;
    if(disc_data_track!=track->number) {
        char path[256];uint32_t bytes;
        ok=disc_path(track->name,path) && !cdda_storage_data_open_path(path,&bytes) && bytes==track->file_bytes;
        if(ok) disc_data_track=track->number;
    }
    uint32_t physical=track->file_offset+(span.offset/2048u)*track->stride;
    if(ok) ok=!cdda_storage_data_read_at(physical,disc_buffer,track->stride);
    if(ok && track->stride==2352u) {
        ok=!disc_buffer[0] && !disc_buffer[11] && disc_buffer[15]==1u;
        for(unsigned i=1;i<11u;i++) if(disc_buffer[i]!=255u) ok=false;
    }
    if(ok) ok=command_sync();
    for(uint32_t i=0;ok && i<span.bytes;i++)
        if(disc_buffer[track->data_offset+i]!=disc_pattern(track->number,span.offset+i)) ok=false;
    uint32_t elapsed=ticks()-first;if(elapsed>disc_data_budget) disc_data_budget=elapsed;
    if(!ok || !command_sync() || ticks()-disc_guard.last_tick>=disc_guard.gap_ticks-KUI_CDDA_MARGIN_TICKS ||
       kui_cdda_disc_bios_validate_work(&disc_owner,work)!=KUI_CDDA_DISC_BIOS_OK) {
        (void)kui_cdda_job_ready(&disc_data_job,&span,work->epoch,false);return false;
    }
    if(disc_checked>UINT32_MAX-span.bytes ||
       kui_cdda_job_ready(&disc_data_job,&span,work->epoch,true)!=KUI_CDDA_JOB_OK) return false;
    for(uint32_t i=0;i<span.bytes;i++) out[i]=disc_buffer[track->data_offset+i];
    return true;
}
static bool disc_toc_work(const struct kui_cdda_disc_bios_work *work) {
    uint8_t toc[408];
    if(kui_cdda_disc_bios_validate_work(&disc_owner,work)!=KUI_CDDA_DISC_BIOS_OK ||
       work->bytes!=sizeof(toc) || kui_cdda_disc_toc(&disc_image_map,work->area,toc)!=KUI_CDDA_DISC_OK) return false;
    uint8_t *out=disc_map(&disc_owner,work->destination,work->bytes,1);
    if(!out) return false;
    for(unsigned i=0;i<sizeof(toc);i++) out[i]=toc[i];
    return true;
}
static uint32_t disc_worker(const void *unused) {
    (void)unused;disc_completion_pending=false;uint32_t now=ticks(),sp=disc_sp();
    if(sp<0x8c220040u || sp>=0x8c230000u ||
       !kui_cdda_stream_clock_update(&disc_elapsed,now)) return KUI_CDDA_SERVICE_IO;
    disc_execs++;
    if(disc_reentry_armed) {
        disc_reentry_armed=false;uint32_t calls=disc_calls,execs=disc_execs;
        if(disc_nested_exec()!=-1 || disc_calls!=calls+1u || disc_execs!=execs || disc_reentries!=1u)
            return KUI_CDDA_SERVICE_IO;
    }
    bool audio=disc_owner.state==KUI_CDDA_DISC_BIOS_QUEUED && disc_owner.work.kind==KUI_CDDA_DISC_BIOS_AUDIO;
    if(!audio && !disc_audio_service()) return KUI_CDDA_SERVICE_IO;
    if(command_control.current.state==KUI_CDDA_CONTROL_EOF && command_control.current.epoch!=disc_eof_epoch) {
        disc_eof_epoch=command_control.current.epoch;disc_eof_checks++;
    }
    if(disc_owner.state==KUI_CDDA_DISC_BIOS_QUEUED && disc_owner.work.kind==KUI_CDDA_DISC_BIOS_DATA &&
       !disc_data_admitted()) return KUI_CDDA_SERVICE_OK;
    struct kui_cdda_disc_bios_work work;
    enum kui_cdda_disc_bios_result taken=kui_cdda_disc_bios_take(&disc_owner,&work);
    if(taken==KUI_CDDA_DISC_BIOS_NOTHING)
        return kui_cdda_disc_bios_observe_audio(&disc_owner,command_control.current.state)==KUI_CDDA_DISC_BIOS_OK?
            KUI_CDDA_SERVICE_OK:KUI_CDDA_SERVICE_IO;
    if(taken!=KUI_CDDA_DISC_BIOS_OK || kui_cdda_disc_bios_validate_work(&disc_owner,&work)!=KUI_CDDA_DISC_BIOS_OK)
        return KUI_CDDA_SERVICE_IO;
    bool success=true;
    if(work.kind==KUI_CDDA_DISC_BIOS_AUDIO) {
        success=disc_bind_audio(&work) && command_accept(work.audio);
        if(success && work.command==KUI_CDDA_DISC_BIOS_PLAY) {
            if(disc_active_track && disc_active_track!=work.track) disc_track_switches++;
            disc_active_track=work.track;
        }
    } else if(work.kind==KUI_CDDA_DISC_BIOS_DATA) success=disc_read_work(&work);
    else if(work.kind==KUI_CDDA_DISC_BIOS_TOC) success=disc_toc_work(&work);
    if(!success || kui_cdda_disc_bios_observe_audio(&disc_owner,command_control.current.state)!=KUI_CDDA_DISC_BIOS_OK)
        return KUI_CDDA_SERVICE_IO;
    disc_completion_work=work;disc_completion_pending=true;return KUI_CDDA_SERVICE_OK;
}
int32_t cdda_bios_native_dispatch(uint32_t r4,uint32_t r5,uint32_t r6,uint32_t r7) {
    disc_calls++;
    if(disc_native_busy) {disc_reentries++;return r7==KUI_GD_REQUEST?0:r7==KUI_GD_CHECK?4:-1;}
#ifdef CDDA_HARNESS_HOST_TEST
    extern uint32_t cdda_disc_host_caller_sp(void);uint32_t sp=cdda_disc_host_caller_sp();
#else
    uint32_t sp;__asm__ __volatile__("mov r15,%0":"=r"(sp));
#endif
    if(!disc_installed || sp<0x8c310040u || sp>=0x8c320000u) return -1;
    int32_t result;
    if(r6 || r7>KUI_GD_PIO_CHECK) result=-1;
    else if((r7==KUI_GD_INIT || r7==KUI_GD_RESET) &&
            command_control.current.state!=KUI_CDDA_CONTROL_STOPPED && command_control.current.state!=KUI_CDDA_CONTROL_EOF)
        result=-1;
    else if(r7==KUI_GD_EXEC) {
        struct kui_cdda_service_ticket ticket;
        enum kui_cdda_service_result admitted=kui_cdda_service_enter(&disc_guard,disc_guard_epoch,ticks(),&ticket);
        if(admitted!=KUI_CDDA_SERVICE_OK) {
            disc_errors++;if(disc_fault_call()!=KUI_CDDA_SERVICE_OK) disc_errors++;result=-1;
        } else {
            disc_native_busy=true;uint32_t work=disc_worker_call(disc_worker,NULL);
            bool committed=kui_cdda_service_leave(&disc_guard,&ticket,ticks(),work==KUI_CDDA_SERVICE_OK)==KUI_CDDA_SERVICE_OK;
            if(committed && disc_completion_pending) {
                struct kui_cdda_disc_bios_work *pending=&disc_completion_work;
                committed=kui_cdda_disc_bios_validate_work(&disc_owner,pending)==KUI_CDDA_DISC_BIOS_OK;
                if(committed && pending->kind==KUI_CDDA_DISC_BIOS_DATA) {
                    struct kui_cdda_job_span span=disc_data_job.pending;
                    committed=kui_cdda_job_commit(&disc_data_job,&span,pending->epoch)==KUI_CDDA_JOB_COMPLETE;
                }
                if(committed) committed=kui_cdda_disc_bios_complete(&disc_owner,pending,true,pending->bytes)==KUI_CDDA_DISC_BIOS_OK;
                if(committed && pending->kind==KUI_CDDA_DISC_BIOS_DATA) {disc_chunks++;disc_checked+=pending->bytes;}
                if(committed && pending->kind==KUI_CDDA_DISC_BIOS_TOC) disc_toc_completions++;
                if(committed && disc_owner.state==KUI_CDDA_DISC_BIOS_TERMINAL) {
                    disc_completions++;
                    if(pending->kind==KUI_CDDA_DISC_BIOS_AUDIO) disc_actions++;
                }
            }
            disc_native_busy=false;disc_completion_pending=false;
            if(!committed) {disc_errors++;(void)disc_fault_call();result=-1;}else result=0;
        }
    } else {
        uint32_t prefix=disc_owner.completed_bytes,handle=disc_owner.work.handle;
        bool progress=r7==KUI_GD_CHECK && disc_owner.state==KUI_CDDA_DISC_BIOS_QUEUED && prefix>0u;
        disc_native_busy=true;
        result=kui_cdda_disc_bios_dispatch(&disc_owner,r4,r5,r6,r7);
        disc_native_busy=false;
        if(r7==KUI_GD_REQUEST && result>0) disc_accepted++;
        if(r7==KUI_GD_CHECK) {
            disc_checks++;
            if(progress && result==KUI_GD_PROCESSING && (handle!=disc_poll_handle || prefix>disc_poll_prefix)) {
                disc_poll_handle=handle;disc_poll_prefix=prefix;disc_progress_polls++;
            }
        }
        if(r7==KUI_GD_DRIVE && !result) disc_drive_checks++;
        if(r7==KUI_GD_ABORT && !result) disc_cancels++;
        if((r7==KUI_GD_RESET || r7==KUI_GD_INIT) && !result) disc_resets++;
    }
    if((r7==KUI_GD_REQUEST && !result) || (r7!=KUI_GD_REQUEST && result==-1)) disc_refusals++;
    return result;
}
static uint32_t disc_diagnostic(void *context,struct cdda_disc_client_diagnostic *r) {
    if(context!=&disc_exports || !r || disc_native_busy) return 1u;
#ifndef CDDA_HARNESS_HOST_TEST
    uintptr_t pointer=(uintptr_t)r;
    if(pointer%4u || pointer<0x8c310040u || pointer>0x8c320000u-sizeof(*r)) return 1u;
#endif
    if(r->pc<0x8c300000u || r->pc>=0x8c310000u || r->pc&1u ||
       r->sp<0x8c310040u || r->sp>=0x8c320000u || r->sp&3u) return 1u;
    r->result=0;
    switch(r->op) {
    case CDDA_DISC_CLIENT_MARK:
        if(r->value!=completed+1u || r->value>7u) return r->result=1u;
        completed++;break;
    case CDDA_DISC_CLIENT_ARM_REENTRY:
        if(disc_reentry_armed || disc_reentries) return r->result=1u;
        disc_reentry_armed=true;break;
    case CDDA_DISC_CLIENT_TRACK_QUERY: {
        const struct kui_cdda_disc_track *track;
        r->map_count=disc_image_map.count;r->map_complete=disc_image_map.complete;
        r->query_result=kui_cdda_disc_track(&disc_image_map,r->value,&track);disc_map_checks++;
        if(!r->query_result) {
            r->track=track->number;r->control=track->control;r->start_fad=track->start_fad;r->end_fad=track->end_fad;
            r->source_frames=track->control?0u:(track->end_fad-track->start_fad)*588u;
            r->backing_offset=track->file_offset;r->file_bytes=track->file_bytes;r->stride=track->stride;
        }
        break;
    }
    case CDDA_DISC_CLIENT_MAP_QUERY: {
        struct kui_cdda_disc_audio audio;struct kui_cdda_disc_data data;
        r->query_result=KUI_CDDA_DISC_RANGE;disc_map_checks++;
        if(r->aux && r->aux<=UINT32_MAX-r->value) {
            if(kui_cdda_disc_audio_range(&disc_image_map,r->value,r->value+r->aux,&audio)==KUI_CDDA_DISC_OK) {
                r->query_result=0;r->track=audio.track;
            } else if(kui_cdda_disc_data_range(&disc_image_map,r->value,r->aux,&data)==KUI_CDDA_DISC_OK) {
                r->query_result=0;r->track=data.track;
            }
        }
        break;
    }
    case CDDA_DISC_CLIENT_REPORT:
        if(disc_reported || r->value<20u || r->value!=disc_calls-1u || r->vector_calls!=r->value ||
           r->execs!=disc_execs || r->checks!=disc_checks || r->drive_checks!=disc_drive_checks ||
           r->read_checked!=disc_checked || disc_checked!=disc_chunks*2048u ||
           r->expected_refusals!=disc_refusals || r->polls!=disc_progress_polls ||
           r->actions!=disc_actions || r->toc_checks!=disc_toc_completions ||
           r->map_checks!=disc_map_checks || r->eof_checks!=disc_eof_checks ||
           r->track_switches!=disc_track_switches || disc_accepted!=disc_completions ||
           disc_reentries!=1u || disc_errors || disc_cancels || disc_resets || command_running ||
           command_control.current.state!=KUI_CDDA_CONTROL_STOPPED || disc_owner.state!=KUI_CDDA_DISC_BIOS_EMPTY)
            return r->result=1u;
#if CDDA_TEST_PROFILE == 11
        if(disc_actions!=7u || disc_drive_checks!=11u || disc_refusals!=12u || r->stale_checks!=3u ||
           disc_checked!=196608u || disc_chunks!=96u || disc_progress_polls!=90u ||
           disc_toc_completions!=2u || disc_map_checks!=16u || disc_eof_checks!=2u ||
           disc_track_switches!=3u || disc_accepted!=16u) return r->result=1u;
#else
        if(disc_actions!=4u || disc_drive_checks!=6u || disc_refusals!=10u || r->stale_checks!=2u ||
           disc_checked || disc_chunks || disc_progress_polls || disc_toc_completions ||
           disc_map_checks!=10u || disc_eof_checks!=1u || disc_track_switches || disc_accepted!=5u)
            return r->result=1u;
#endif
        disc_abi_checks=r->value;disc_report_stales=r->stale_checks;
        disc_report_map_checks=r->map_checks;disc_report_eof_checks=r->eof_checks;disc_reported=true;break;
    case CDDA_DISC_CLIENT_SNAPSHOT:
        r->state=command_control.current.state;r->frame=command_control.current.frame;r->prefetch=pcm.position;
        r->epoch=command_control.current.epoch;r->loops=command_control.current.loops;r->track=disc_active_track;
        r->vector_calls=disc_calls;r->execs=disc_execs;r->checks=disc_checks;r->drive_checks=disc_drive_checks;
        r->read_checked=disc_checked;r->expected_refusals=disc_refusals;break;
    default:return r->result=1u;
    }
    return 0;
}
static bool cdda_disc_test(void) {
    disc_owner=(struct kui_cdda_disc_bios){0};disc_image_map=(struct kui_cdda_disc_map){0};
    disc_calls=disc_execs=disc_accepted=disc_completions=disc_refusals=disc_cancels=disc_resets=0;
    disc_checked=disc_errors=disc_reentries=disc_abi_checks=disc_checks=disc_drive_checks=0;
    disc_chunks=disc_progress_polls=disc_poll_handle=disc_poll_prefix=0;
    disc_data_track=disc_track_switches=disc_toc_completions=disc_actions=0;
    disc_map_checks=disc_eof_checks=disc_eof_epoch=disc_report_map_checks=disc_report_eof_checks=0;
    disc_client_stack_used=disc_service_stack_used=disc_vector_restored=disc_context_checks=disc_descriptor_rejects=0;
    disc_installed=disc_native_busy=disc_reentry_armed=disc_reported=disc_completion_pending=false;
    disc_audio_path=NULL;disc_active_track=0;disc_audio_source=(struct kui_cdda_pcm_source){0};
    command_running=false;command_accepted=command_completions=command_status_checks=command_paused_frame=command_max_loops=0;
    disc_stacks_init();cdda_display_line("1/8: validate file-backed GDI map and ownership");
    uint32_t source_frames;
    if(!disc_prepare_descriptor() || kui_cdda_service_init(&disc_guard)!=KUI_CDDA_SERVICE_OK ||
       kui_cdda_job_init(&disc_data_job)!=KUI_CDDA_JOB_OK || !kui_cdda_ms_to_ticks(10u,&disc_data_budget) ||
       !disc_load_map(&source_frames)) goto failed;
    const struct kui_cdda_disc_bios_ops ops={&disc_owner,disc_map};
    if(kui_cdda_control_init(&command_control,source_frames)!=KUI_CDDA_CONTROL_OK ||
       kui_cdda_disc_bios_init(&disc_owner,&ops,&disc_image_map,0x8c310040u,0x8c320000u)!=KUI_CDDA_DISC_BIOS_OK ||
       !kui_cdda_stream_clock_init(&disc_elapsed,ticks(),KUI_CDDA_TMU_HZ) ||
       kui_cdda_service_start(&disc_guard,ticks(),KUI_CDDA_HALF_TICKS,&disc_guard_epoch)!=KUI_CDDA_SERVICE_OK) goto failed;
    disc_exports=(struct cdda_disc_client_exports){CDDA_DISC_CLIENT_MAGIC,CDDA_DISC_CLIENT_REVISION,
        sizeof(disc_exports),&disc_exports,disc_clock_native,kui_cdda_bridge_probe,disc_diagnostic};
    disc_old_vector=disc_vector_read();
#ifdef CDDA_HARNESS_HOST_TEST
    uint32_t hook=0x8c010200u;
#else
    uint32_t hook=(uint32_t)(uintptr_t)&kui_cdda_bios_vector_entry;
#endif
    disc_installed=true;disc_vector_write(hook);if(disc_vector_read()!=hook) goto failed;
    completed++;
#if CDDA_TEST_PROFILE == 11
    cdda_display_line("2..7/8: mapped TOC, audio files and checked data");
#else
    cdda_display_line("2..7/8: Toy14 PLAY, pause/resume and backed EOF");
#endif
    uint32_t before[3],after[3];disc_context(before);
#ifdef CDDA_HARNESS_HOST_TEST
    uint32_t result=kui_cdda_client_call(cdda_disc_client_entry,&disc_exports,(void *)(uintptr_t)0x8c320000u);
#else
    uint32_t result=kui_cdda_client_call(cdda_disc_client_entry,&disc_exports,__cdda_client_stack_top);
#endif
    disc_context(after);disc_vector_write(disc_old_vector);disc_vector_restored=disc_vector_read()==disc_old_vector;
    if(disc_vector_restored) disc_installed=false;
    if(result || before[0]!=after[0] || before[1]!=after[1] || before[2]!=after[2]) goto failed;
    disc_context_checks=1u;
#if CDDA_TEST_PROFILE == 11
    const uint32_t seconds_limit=60u;
#else
    const uint32_t seconds_limit=90u;
#endif
    if(!disc_stack_check(0,&disc_service_stack_used) || !disc_stack_check(1,&disc_client_stack_used) ||
       completed!=7u || disc_elapsed.seconds>seconds_limit || disc_errors || !disc_reported ||
       disc_descriptor_rejects!=8u || disc_reentries!=1u || !disc_vector_restored || command_running ||
       command_completions!=disc_actions || command_control.current.state!=KUI_CDDA_CONTROL_STOPPED ||
       disc_owner.state!=KUI_CDDA_DISC_BIOS_EMPTY ||
       kui_cdda_service_stop(&disc_guard,disc_guard_epoch)!=KUI_CDDA_SERVICE_OK) goto failed;
    cdda_storage_data_close();completed++;return true;
failed:
    disc_errors++;(void)disc_fault_call();cdda_storage_data_close();
    if(disc_guard.initialized && disc_guard.state==KUI_CDDA_SERVICE_ACTIVE)
        (void)kui_cdda_service_stop(&disc_guard,disc_guard.generation);
    if(disc_installed) {
        disc_vector_write(disc_old_vector);disc_vector_restored=disc_vector_read()==disc_old_vector;disc_installed=false;
    }
    cdda_display_line(cdda_storage_last_failure());return false;
}
static void cdda_disc_report(void) {
    display_pair("Mapped tracks / complete image: ",disc_image_map.count,disc_image_map.complete);
    display_pair("Client seconds / vector calls: ",disc_elapsed.seconds,disc_calls);
    display_pair("Checked data bytes / chunks: ",disc_checked,disc_chunks);
    display_pair("TOC / map checks: ",disc_toc_completions,disc_report_map_checks);
    display_pair("Audio actions / EOF checks: ",disc_actions,disc_report_eof_checks);
    display_pair("Client / service stack bytes: ",disc_client_stack_used,disc_service_stack_used);
    display_pair("ABI checks / vector restored: ",disc_abi_checks,disc_vector_restored);
}
