/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_native_vmu_probe.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
struct fixture {uint32_t callback,mask,status,reads,fail,bytes;};
static bool read_record(void *context,uint32_t address,unsigned width,uint32_t *out) {
    struct fixture *f=context;++f->reads;
    if(f->fail==address)return false;
    if(address==KUI_SMASH_SRAM_BYTES) {assert(width==4u);*out=f->bytes;}
    else if(address==KUI_SMASH_BUP_JOB+116u) {assert(width==4u);*out=f->callback;}
    else if(address==KUI_SMASH_BUP_MASK) {assert(width==4u);*out=f->mask;}
    else {assert(address==KUI_SMASH_BUP_APP && width==2u);*out=f->status;}
    return true;
}
static struct fixture valid(void) {return (struct fixture){KUI_SMASH_BUP_CALLBACK,255u,0u,0u,0u,8192u};}
static void no_report(struct kui_smash_native_vmu_probe *p,struct fixture *f,unsigned count) {
    for(unsigned i=0u;i<count;i++)assert(!kui_smash_native_vmu_probe_tick(p,read_record,f));
}
int main(void) {
    struct kui_smash_native_vmu_probe p={0};struct fixture f=valid();
    assert(!kui_smash_native_vmu_probe_tick(NULL,read_record,&f));
    assert(!kui_smash_native_vmu_probe_tick(&p,NULL,&f));
    no_report(&p,&f,63u);assert(!f.reads);
    no_report(&p,&f,1u);assert(f.reads==2u && p.registered_at==64u);
    no_report(&p,&f,511u);assert(f.reads==16u);
    assert(kui_smash_native_vmu_probe_tick(&p,read_record,&f));assert(f.reads==18u);
    /* Missing callbacks and masks must still report after the main checkpoint. */
    p=(struct kui_smash_native_vmu_probe){0};f=valid();f.callback=f.mask=0u;
    no_report(&p,&f,575u);assert(kui_smash_native_vmu_probe_tick(&p,read_record,&f));
    p=(struct kui_smash_native_vmu_probe){0};f=valid();f.bytes=0u;
    no_report(&p,&f,1024u);assert(f.reads==16u && !p.registered_at);
    p=(struct kui_smash_native_vmu_probe){0};f=valid();f.bytes=65537u;
    no_report(&p,&f,1024u);assert(f.reads==16u && !p.registered_at);
    p=(struct kui_smash_native_vmu_probe){0};f=valid();f.fail=KUI_SMASH_SRAM_BYTES;
    no_report(&p,&f,1024u);assert(!p.registered_at);
    p=(struct kui_smash_native_vmu_probe){0};f=valid();f.fail=KUI_SMASH_BUP_APP;
    no_report(&p,&f,575u);assert(kui_smash_native_vmu_probe_tick(&p,read_record,&f));
    p=(struct kui_smash_native_vmu_probe){0};f=valid();f.status=1u;
    no_report(&p,&f,64u);assert(p.ready && f.reads==2u);
    no_report(&p,&f,2048u);assert(f.reads==2u);
    p=(struct kui_smash_native_vmu_probe){0};f=valid();no_report(&p,&f,512u);
    f.status=1u;no_report(&p,&f,1024u);assert(p.ready && f.reads==18u);
    p=(struct kui_smash_native_vmu_probe){.completions=UINT32_MAX};f=valid();
    no_report(&p,&f,2u);assert(!f.reads && p.completions==UINT32_MAX);
    puts("PASS Smash native VMU startup probe: post-ROM main checkpoint, missing callbacks captured, cached SDK records, bounded reads, settle window, successful-card disable, I/O and overflow refusals");
    return 0;
}
