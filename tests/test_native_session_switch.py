#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Replay the actual worker adapter with the real GD/SCI loan/card fixture.

The console's P1/P2 aliases occupy ASan's reserved shadow ranges. This suite
therefore uses UBSan and shared fixed aliases; the companion worker/core suites
run ASan. Only SH cache instructions and final CCR invalidation are modelled.
"""
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
FLAGS = (
    'KUI_RETAIL_ASYNC_TEST=1', 'KUI_SCI_STREAM_TEST=1', 'KUI_RETAIL_ASYNC=1',
    'KUI_RETAIL_GD_ASYNC=1', 'KUI_RETAIL_TRANSPORT=1', 'KUI_SCI_SD_NO_BLOCK=1',
    'KUI_GENERAL_SCI_RESUME_FIX=1', 'KUI_RETAIL_NATIVE_HYBRID=1',
    'KUI_NATIVE_REQUEST_TIMING=0', 'KUI_NATIVE_COOP_PUMP=1', 'KUI_NATIVE_IRQ_REARM=1',
    'KUI_NATIVE_LOADER_TIMELINE=1', 'KUI_NATIVE_READER_DIAGNOSTIC=1',
    'KUI_VMU_NATIVE_BACKGROUND=1', 'KUI_NATIVE_SDK_SCI_ROUTE=1',
    'KUI_NATIVE_SDK_SCI_STRUCTURAL=1', 'KUI_RETAIL_MANIFEST_SLOTS=64',
    'KUI_NATIVE_READAHEAD=1', 'KUI_NATIVE_SESSION_CONTROLS=1',
    'KUI_VMU_NATIVE_PERSISTENT_PROBE=1', 'KUI_VMU_NATIVE_CACHED_CODE=1',
    'KUI_NATIVE_GENERAL_LEAN_LAYOUT=1',
)
SOURCES = ('src/loader/retail_async.c', 'src/loader/sci_stream.c',
           'src/core/retail_cursor.c', 'src/core/retail_image.c',
           'src/core/retail_gd.c', 'src/core/native_disc_set.c', 'src/core/native_session.c')


def function(source, name):
    for match in re.finditer(r'\b' + re.escape(name) + r'\s*\(', source):
        begin = source.rfind('static ', 0, match.start())
        opening = source.find('{', begin)
        semicolon = source.find(';', begin)
        if begin >= 0 and opening > match.start() and (semicolon < 0 or opening < semicolon):
            break
    else:
        raise ValueError('Production function not found: ' + name)
    depth, end = 1, opening + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[begin:end]


PREFIX = r'''
#define _GNU_SOURCE 1
#include <setjmp.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>
#define KUI_NATIVE_SDK_SCI_MAIN sdk_baseline_main
#define kui_sci_sd_acquire fixture_sci_sd_acquire
#include "test_retail_async_sdk_sci_route.c"
#undef kui_sci_sd_acquire
#include "kui/native_readahead.h"
#include "kui/native_disc_set.h"
#include "kui/native_return.h"
#include "kui/native_session_binding.h"
#include "kui/vmu_native_background.h"
#include "kui/vmu_sd_lease.h"
static uint32_t actual_sr;
static unsigned resets,quiesces,releases,lease_calls,loan_ends,invalidations,purges;
static bool reset_refused,old_cached,force_lease_busy;
uint32_t kui_retail_async_vmu_test_sr(void) {return actual_sr;}
enum kui_loader_sd_result kui_sci_sd_acquire(void) {
    enum kui_loader_sd_result result=fixture_sci_sd_acquire();
    if(result==KUI_LOADER_SD_OK)R.shared.port.acquired=true;
    return result;
}
void kui_sci_sd_release(void) {
    CHECK((actual_sr&0xf0u)==0xf0u && kui_retail_hook_active==1u && R.shared.port.acquired);
    CHECK(!kui_sci_stream_busy() && !R.shared.stream.state && !R.engine.hooked && !R.engine.release.armed);
    ++releases;R.shared.port.acquired=false;
}
static uint32_t auxiliary(uint32_t event,uint32_t arg,kui_retail_async_region_type *region,uint32_t reserved) {
    CHECK(region==&R && !reserved);
    if(event!=KUI_NATIVE_RA_RESET && event!=KUI_NATIVE_RA_QUIESCE)return 0u;
    CHECK(!arg && R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_IDLE);
    if(event==KUI_NATIVE_RA_RESET) {
        CHECK(!kui_retail_hook_active);++resets;
        if(reset_refused)return 0u;
        old_cached=false;return 1u;
    }
    CHECK(event==KUI_NATIVE_RA_QUIESCE && kui_retail_hook_active==1u && !old_cached);
    ++quiesces;return 1u;
}
#define LEASE UINT32_C(0x8c600000)
#define ALIAS UINT32_C(0xac600000)
#define RA_ENTRY (LEASE+KUI_NATIVE_RA_AUX_OFFSET)
#define BIND_ENTRY UINT32_C(0x8c004100)
#define BIND_ADDRESS UINT32_C(0x8c006000)
#define MANIFEST_CELL UINT32_C(0x8c006808)
#define session_discs (*(struct kui_native_disc_set *)(uintptr_t)(ALIAS+0x1000u))
static struct {uint32_t active,operation;const struct kui_retail_manifest *next;
    struct kui_loader_sd card;int result;} session_switch;
static struct {uint32_t configured,return_available,disc_hold,disc_latched,exit_pending;
    uint32_t active,result,attempted,return_prepared;} session;
static struct {uint32_t inflight,handling;struct {uint32_t active;} protocol;} durable_card;
static struct {uint32_t pending;} overlay;
static struct kui_native_return_plan session_return_plan;
static struct {uint32_t armed,pending,submitted,completed;} state;
static struct {struct {uint32_t consumer;} driver;struct {uint32_t payload,lease_bytes;} lease;
    uint32_t session_control,sd_lease;} config;
static uint32_t kui_vmu_native_passive_consumer;
static bool lease_owned,stack_owned,hooks_owned;
static bool lease_valid(void) {return lease_owned;}
static bool stack_valid(void) {return stack_owned;}
static bool installed_hooks_valid(void) {return hooks_owned;}
static uint32_t physical(uint32_t address) {return address&UINT32_C(0x1fffffff);}
static kui_retail_async_region_type *session_region;
static uint32_t session_manifest_cell,session_firmware[9];
static bool native_session_return_guard(void);
static int native_session_control_visit(uint32_t,const struct kui_retail_manifest *,struct kui_loader_sd *);
static int native_session_switch_transaction(struct kui_loader_sd *);
static unsigned last_error;
static jmp_buf stopped;
static void fail(uint32_t error) {last_error=error;longjmp(stopped,1);}
static void session_purge(uint32_t address,uint32_t bytes) {
    CHECK(bytes && (address&UINT32_C(0xe0000000))==UINT32_C(0x80000000));++purges;
    if(address!=session_manifest_cell) {
        /* The first real firmware write must follow successful loan_end. */
        CHECK(loan_ends && R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_IDLE);
        CHECK(!kui_retail_hook_active && !session_switch.active);
    }
}
void kui_native_session_invalidate(void) {
    CHECK(loan_ends && !kui_retail_hook_active && !session_switch.active);
    CHECK(R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_IDLE);++invalidations;
}
int kui_vmu_native_persistent_sd_transaction(void *context,struct kui_loader_sd *loan) {
    CHECK(context==&session_switch && session_switch.active==UINT32_C(0x4b535754));
    return native_session_switch_transaction(loan);
}
static int fixture_lease(kui_vmu_sd_transaction transaction,void *context) {
    CHECK(transaction==kui_vmu_native_persistent_sd_transaction && context==&session_switch);
    ++lease_calls;if(force_lease_busy)return KUI_VMU_SD_LEASE_BUSY;
    struct kui_loader_sd loan;
    enum kui_vmu_native_async_loan_result result=kui_retail_native_async_loan_begin(&loan);
    if(result==KUI_VMU_NATIVE_ASYNC_LOAN_BUSY)return KUI_VMU_SD_LEASE_BUSY;
    if(result!=KUI_VMU_NATIVE_ASYNC_LOAN_OK)return KUI_VMU_SD_LEASE_FAULT;
    CHECK(R.engine.native_loan_state==KUI_VMU_NATIVE_ASYNC_LOAN_WRITER && kui_retail_hook_active==1u);
    CHECK(kui_sci_sd_acquire()==KUI_LOADER_SD_OK && R.shared.port.acquired);
    int run=transaction(context,&loan);
    kui_sci_sd_release();
    result=kui_retail_native_async_loan_end(run==0 && loan.ready);
    ++loan_ends;
    return result==KUI_VMU_NATIVE_ASYNC_LOAN_OK?KUI_VMU_SD_LEASE_OK:KUI_VMU_SD_LEASE_FAULT;
}
static int fixture_binding(uint32_t operation,const struct kui_retail_manifest *next,struct kui_loader_sd *out) {
    CHECK(operation==KUI_NATIVE_SESSION_BIND && !next && !out);return (int)BIND_ADDRESS;
}
'''

SUFFIX = r'''
static void trampoline(uint32_t address,uintptr_t target) {
#if defined(__x86_64__)
    /* Only the address placement is modelled: tail-jump to the host fixture. */
    unsigned char *code=(void *)(uintptr_t)address;
    code[0]=0x48u;code[1]=0xb8u;memcpy(code+2u,&target,8u);code[10]=0xffu;code[11]=0xe0u;
    __builtin___clear_cache((char *)code,(char *)code+12u);
#else
#error "This fixed-alias replay needs an x86-64 host trampoline"
#endif
}
static void mapped_memory(void) {
    int fd=memfd_create("kui-session-physical-ram",0);CHECK(fd>=0 && ftruncate(fd,0x1000000)==0);
    CHECK(mmap((void *)(uintptr_t)UINT32_C(0x8c000000),0x1000000,
        PROT_READ|PROT_WRITE|PROT_EXEC,MAP_SHARED|MAP_FIXED_NOREPLACE,fd,0)==(void *)(uintptr_t)UINT32_C(0x8c000000));
    CHECK(mmap((void *)(uintptr_t)UINT32_C(0xac000000),0x1000000,
        PROT_READ|PROT_WRITE|PROT_EXEC,MAP_SHARED|MAP_FIXED_NOREPLACE,fd,0)==(void *)(uintptr_t)UINT32_C(0xac000000));
    CHECK(close(fd)==0);
    CHECK(sizeof(kui_retail_async_region_type)<KUI_NATIVE_RA_AUX_OFFSET-KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET);
    kui_retail_async_region_storage=(void *)(uintptr_t)(LEASE+KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET);
    trampoline(RA_ENTRY,(uintptr_t)auxiliary);trampoline(BIND_ENTRY,(uintptr_t)fixture_binding);
}
static struct kui_retail_manifest *disc(unsigned n) {return (void *)session_discs.maps[n];}
static void prepare_switch(void) {
    setup_sdk(X,true);actual_sr=0xf0u;
    resets=quiesces=releases=lease_calls=loan_ends=invalidations=purges=0u;
    reset_refused=old_cached=force_lease_busy=false;last_error=0u;
    memset(&session_switch,0,sizeof(session_switch));memset(&session,0,sizeof(session));
    memset(&durable_card,0,sizeof(durable_card));memset(&overlay,0,sizeof(overlay));
    memset(&session_discs,0,sizeof(session_discs));session_discs.count=2u;
    config.lease.payload=LEASE;config.lease.lease_bytes=KUI_NATIVE_RA_LEASE_BYTES;
    config.sd_lease=(uint32_t)(uintptr_t)fixture_lease;
    config.session_control=BIND_ENTRY;session_region=&R;session_manifest_cell=MANIFEST_CELL;
    config.driver.consumer=kui_vmu_native_passive_consumer=UINT32_C(0x8c123456);
    state.armed=1u;state.pending=0u;state.submitted=state.completed=200u;
    lease_owned=stack_owned=hooks_owned=true;
    manifest.storage_transport=KUI_STORAGE_SCI;
    *disc(0)=manifest;*disc(1)=manifest;
    unsigned index=kui_retail_track_first_extent(&disc(1)->slots[2].track);
    struct kui_retail_extent *extent=&disc(1)->slots[index].extent;
    CHECK(extent->blocks<=48u);extent->card_lba=1900u;
    for(uint32_t i=0;i<extent->blocks*512u;i++)image_card[1900u*512u+i]=(uint8_t)(i*7u+0x5bu);
    CHECK(kui_retail_manifest_validate(disc(1))==KUI_GAME_OK);
    R.vector600[KUI_NATIVE_RA_ENTRY_INDEX]=RA_ENTRY;
    *(uint32_t *)(uintptr_t)MANIFEST_CELL=LEASE+KUI_VMU_NATIVE_BACKGROUND_WORKER_BYTES;
    struct kui_native_session_binding *b=(void *)(uintptr_t)BIND_ADDRESS;
    *b=(struct kui_native_session_binding){KUI_NATIVE_SESSION_BINDING_MAGIC,
        KUI_NATIVE_SESSION_BINDING_VERSION,sizeof(*b),UINT32_C(0x8c006800),MANIFEST_CELL,UINT32_C(0x8c006900)};
    *(uint32_t *)(uintptr_t)b->region_cell=LEASE+KUI_VMU_NATIVE_BACKGROUND_REGION_OFFSET;
    for(unsigned i=0;i<9u;i++) {
        session_firmware[i]=UINT32_C(0x12340000)+i;
        ((uint32_t *)(uintptr_t)b->firmware_snapshot)[i]=session_firmware[i];
    }
    memset((void *)(uintptr_t)UINT32_C(0xac000000),0xa6,0x2000u);
    CHECK(native_session_return_guard());
}
static void busy_and_guard_checks(void) {
    prepare_switch();struct kui_loader_sd out;memset(&out,0x91,sizeof(out));
    uint32_t *busy[]={&R.shared.service.pending,&R.shared.service.executing,&R.engine.active,
        &R.engine.in_irq,&R.engine.release.depth};
    for(unsigned i=0;i<sizeof(busy)/sizeof(*busy);i++) {
        *busy[i]=1u;kui_retail_async_region_type saved=R;unsigned clocks=card.clocks;
        unsigned char switch_before[sizeof(session_switch)];memcpy(switch_before,&session_switch,sizeof(session_switch));
        struct kui_loader_sd before=out;
        CHECK(native_session_control_visit(KUI_NATIVE_SESSION_DISC_OPEN,NULL,&out)==0);
        CHECK(!memcmp(&saved,&R,sizeof(R)) && !memcmp(switch_before,&session_switch,sizeof(session_switch)));
        CHECK(!memcmp(&before,&out,sizeof(out)) && !resets && !lease_calls && card.clocks==clocks);*busy[i]=0u;
    }
    state.pending=1u;CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1);state.pending=0u;
    ++state.submitted;CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1);--state.submitted;
    lease_owned=false;CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1);lease_owned=true;
    stack_owned=false;CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1);stack_owned=true;
    hooks_owned=false;CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1);hooks_owned=true;
    ++kui_vmu_native_passive_consumer;CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1);
    --kui_vmu_native_passive_consumer;
    CHECK(!resets && !lease_calls);
    R.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_WRITER;
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==0 && !resets);
    R.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_QUARANTINED;
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1 && !resets);
    R.engine.native_loan_state=KUI_VMU_NATIVE_ASYNC_LOAN_IDLE;
    R.vector600[0]=RA_ENTRY-2u;CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1);
    R.vector600[0]=RA_ENTRY+1u;CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1);
    R.vector600[0]=RA_ENTRY+KUI_NATIVE_RA_AUX_IMAGE_BYTES;
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==-1 && !resets);
    R.vector600[0]=RA_ENTRY;reset_refused=true;old_cached=true;
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==0 && resets==1u && !lease_calls && old_cached);
    reset_refused=false;force_lease_busy=true;
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&out)==0 && !loan_ends && !invalidations);
}
static void map_and_binding_checks(void) {
    prepare_switch();CHECK(session_next_valid(disc(0)) && session_next_valid(disc(1)));
    CHECK(!session_next_valid(NULL) && !session_next_valid((void *)(uintptr_t)(ALIAS+0x10u)));
    CHECK(!session_next_valid((void *)(uintptr_t)(ALIAS+0x1b000u)));
    CHECK(!session_next_valid((void *)(uintptr_t)((uint32_t)(uintptr_t)disc(1)+1u)));
    struct kui_retail_manifest copy=*disc(1);CHECK(!session_next_valid(&copy));
    *disc(1)=copy;disc(1)->storage_transport=KUI_STORAGE_SCIF;CHECK(!session_next_valid(disc(1)));
    *disc(1)=copy;disc(1)->reader=KUI_RETAIL_READER_ASYNC_EAGER;CHECK(!session_next_valid(disc(1)));
    *disc(1)=copy;disc(1)->flags|=KUI_RETAIL_IMAGE_CD;CHECK(!session_next_valid(disc(1)));
    *disc(1)=copy;++disc(1)->card_sectors;CHECK(!session_next_valid(disc(1)));
    *disc(1)=copy;++disc(1)->partition_start;CHECK(!session_next_valid(disc(1)));
    *disc(1)=copy;disc(1)->track_count=0;CHECK(!session_next_valid(disc(1)));
    *disc(1)=copy;disc(1)->extent_count=KUI_RETAIL_ASYNC_SLOTS;CHECK(!session_next_valid(disc(1)));*disc(1)=copy;
    struct kui_native_session_binding *b=(void *)(uintptr_t)BIND_ADDRESS;
    struct kui_native_session_binding original=*b;
    const uint32_t targets[]={0u,UINT32_C(0x8c004027),UINT32_C(0x8c007ba0),BIND_ENTRY+1u};
    for(unsigned i=0;i<sizeof(targets)/sizeof(*targets);i++) {config.session_control=targets[i];CHECK(!session_bind());}
    config.session_control=BIND_ENTRY;
    uint32_t *fields[]={&b->magic,&b->version,&b->bytes,&b->region_cell,&b->manifest_cell,&b->firmware_snapshot};
    for(unsigned i=0;i<sizeof(fields)/sizeof(*fields);i++) {
        *b=original;*fields[i]^=1u;CHECK(!session_bind() && config.session_control==BIND_ENTRY);
    }
    *b=original;b->manifest_cell=b->region_cell;CHECK(!session_bind());
    *b=original;b->region_cell=b->firmware_snapshot+4u;CHECK(!session_bind());
    *b=original;b->manifest_cell=b->firmware_snapshot+8u;CHECK(!session_bind());
    *b=original;*(uint32_t *)(uintptr_t)b->region_cell^=4u;CHECK(!session_bind());
    *(uint32_t *)(uintptr_t)b->region_cell^=4u;
    CHECK(session_bind() && config.session_control==0u && session_region==&R && session_manifest_cell==MANIFEST_CELL);
    CHECK(!memcmp(session_firmware,(void *)(uintptr_t)b->firmware_snapshot,sizeof(session_firmware)));
}
static void actual_switch_and_return(void) {
    prepare_switch();
    (void)request(45000u,2u,OUTPUT);
    CHECK(gd(KUI_GD_EXEC,0u,0u)==0 && !R.shared.service.pending);
    compare(45000u,2u,OUTPUT);
    /* A terminal completed command is not a pending request. The real adapter
     * must run under completed==submitted, rather than the VMU pending guard. */
    CHECK(R.shared.service.command && !R.shared.service.pending && !state.pending);
    uint32_t generation=R.shared.service.disc_generation;
    uint32_t old_token=R.shared.service.token,old_command=R.shared.service.command;
    old_cached=true;
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_DISC_OPEN,NULL,NULL)==1);
    CHECK(resets==1u && quiesces==1u && loan_ends==1u && !old_cached && !invalidations);
    CHECK(!R.engine.native_loan_state && !kui_retail_hook_active && R.shared.service.initialized);
    CHECK(R.shared.service.disc_lid==KUI_RETAIL_DISC_OPEN && R.shared.service.disc_generation==generation+1u);
    CHECK(R.shared.service.token==old_token && R.shared.service.command==old_command);
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_DISC_CLOSE,disc(1),NULL)==0);
    CHECK(gd(KUI_GD_DRIVE,STATUS,0u)==0 && get(STATUS)==6u);
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_STATUS,NULL,NULL)==(KUI_NATIVE_SESSION_EMPTY|KUI_NATIVE_SESSION_OBSERVED));
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_DISC_CLOSE,disc(1),NULL)==1);
    CHECK(R.engine.manifest==disc(1) && !R.engine.cursor.manifest && !R.engine.token && !R.engine.destination);
    CHECK(!R.engine.since && !R.engine.retries && !R.engine.failed && !R.engine.opened);
    CHECK(R.engine.disc_generation==R.shared.service.disc_generation && R.engine.target==20u);
    CHECK(*(const struct kui_retail_manifest **)(uintptr_t)(MANIFEST_CELL|UINT32_C(0x20000000))==disc(1));
    CHECK(loan_ends==2u && !invalidations);
    CHECK(gd(KUI_GD_CHECK,old_token,STATUS)==KUI_GD_COMPLETED);
    CHECK(kui_retail_image_init(&reference,disc(1),read_block,NULL)==KUI_GAME_OK);
    read_and_compare(45000u,2u,0u,1u);CHECK(ram[OUTPUT-BEGIN]==0x5bu);
    /* A late old-epoch delivery cannot advance the new command. */
    int32_t token=request(45000u,1u,OUTPUT);CHECK(token>0);
    uint32_t completed=R.shared.service.completed_bytes;
    kui_retail_gd_progress_epoch(&R.shared.service,generation,(uint32_t)token,99u,0u);
    CHECK(R.shared.service.completed_bytes==completed && R.shared.service.pending);
    CHECK(finish(token,0u,1u)==KUI_GD_COMPLETED);
    struct kui_loader_sd snapshot={0};unsigned ends=loan_ends;
    CHECK(native_session_control_visit(KUI_NATIVE_SESSION_RETURN,NULL,&snapshot)==1);
    CHECK(snapshot.ready && snapshot.blocks==R.shared.card.device.sd.blocks && loan_ends==ends+1u);
    CHECK(invalidations==1u && R.shared.service.initialized && !R.shared.service.pending);
    CHECK(!R.engine.native_loan_state && !kui_retail_hook_active);
    const uint32_t vectors[]={UINT32_C(0xac0000bc),UINT32_C(0xac0000c0),UINT32_C(0xac0000e0),
        UINT32_C(0xac001000),UINT32_C(0xac001004),UINT32_C(0xac001008),
        UINT32_C(0xac0010f0),UINT32_C(0xac0010f4),UINT32_C(0xac0010f8)};
    for(unsigned i=0;i<9u;i++)CHECK(*(uint32_t *)(uintptr_t)vectors[i]==session_firmware[i]);
    /* The fresh runtime read gets a second real loan after firmware restore.
     * initialized must remain true or loan_begin would refuse forever. */
    struct kui_loader_sd reload;
    CHECK(kui_retail_native_async_loan_begin(&reload)==KUI_VMU_NATIVE_ASYNC_LOAN_OK && reload.ready);
    CHECK(kui_retail_native_async_loan_end(true)==KUI_VMU_NATIVE_ASYNC_LOAN_OK);
}
static void save_deferral(void) {
    prepare_switch();session.configured=1u;
    CHECK(kui_native_disc_set_request_next(&session_discs));
    uint32_t *pending[]={&durable_card.inflight,&durable_card.handling,&durable_card.protocol.active,&overlay.pending};
    for(unsigned i=0;i<sizeof(pending)/sizeof(*pending);i++) {
        *pending[i]=1u;kui_retail_async_region_type saved=R;
        native_session_controller_complete(UINT32_C(0x0000ffff));
        CHECK(!memcmp(&saved,&R,sizeof(R)) && !resets && !lease_calls && !last_error);
        CHECK(session_discs.phase==KUI_NATIVE_DISC_OPENING && *pending[i]==1u);*pending[i]=0u;
    }
    native_session_controller_complete(UINT32_C(0x0000ffff));
    CHECK(session_discs.phase==KUI_NATIVE_DISC_WAITING && loan_ends==1u && !last_error);
}
int main(void) {
    mapped_memory();busy_and_guard_checks();map_and_binding_checks();actual_switch_and_return();save_deferral();
    printf("PASS actual native session worker adapter (UBSan): %u checks; completed pending=0 bridge, immutable busy, save deferral, owned maps/binding, real GD/SCI open-close/new bytes/epoch fence, loan-end before firmware and follow-up reload eligibility\n",checks);
    CHECK(munmap((void *)(uintptr_t)UINT32_C(0x8c000000),0x1000000)==0);
    CHECK(munmap((void *)(uintptr_t)UINT32_C(0xac000000),0x1000000)==0);
    return 0;
}
'''


def main():
    source = (ROOT / 'src/loader/native_session_worker.inc').read_text()
    names = ('session_low_span', 'session_bind', 'session_next_valid',
             'native_session_switch_transaction', 'session_restore_firmware',
             'native_session_control_visit', 'session_control',
             'native_session_return_guard', 'native_session_controller_complete')
    program = (PREFIX + '\n#pragma GCC diagnostic push\n#pragma GCC diagnostic ignored \"-Wpedantic\"\n'
               + '\n\n'.join(function(source, name) for name in names)
               + '\n#pragma GCC diagnostic pop\n' + SUFFIX)
    with tempfile.TemporaryDirectory(prefix='kui-session-switch-') as temporary:
        directory = Path(temporary)
        c = directory / 'switch.c'
        c.write_text(program)
        binary = directory / 'switch'
        command = [*shlex.split(os.environ.get('HOST_CC', 'cc')), '-std=c11', '-O1', '-g',
                   '-Wall', '-Wextra', '-Werror', '-Wpedantic', '-fsanitize=undefined',
                   '-fno-sanitize-recover=all', '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
                   '-Iinclude', '-Isrc/loader', '-Itests']
        subprocess.run(command + ['-D' + flag for flag in FLAGS] + list(SOURCES)
                       + [str(c), '-o', str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == '__main__':
    main()
