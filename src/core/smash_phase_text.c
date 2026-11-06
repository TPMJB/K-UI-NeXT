/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/smash_phase_text.h"
#include <string.h>
struct writer {char *p;size_t left;bool ok;};
static void put(struct writer *w,char c) {
    if(!w->left) {w->ok=false;return;}*w->p++=c;--w->left;
}
static void text(struct writer *w,const char *s) {while(*s)put(w,*s++);}
static void number(struct writer *w,uint32_t value) {
    char reverse[10];unsigned n=0u;
    do {reverse[n++]=(char)('0'+value%10u);value/=10u;}while(value);
    while(n)put(w,reverse[--n]);
}
static void hex(struct writer *w,uint32_t value) {
    text(w,"0x");for(int n=28;n>=0;n-=4)put(w,"0123456789abcdef"[(value>>(unsigned)n)&15u]);
}
static void raw_hex(struct writer *w,uint32_t value,unsigned digits) {
    for(int n=(int)(digits*4u)-4;n>=0;n-=4)put(w,"0123456789abcdef"[(value>>(unsigned)n)&15u]);
}
static const struct kui_smash_phase_stamp *stamp(const struct kui_smash_phase_trace *t,unsigned phase) {
    for(unsigned i=0;i<t->count;i++)if(t->entries[i].phase==phase)return &t->entries[i].stamp;
    return NULL;
}
static void interval(struct writer *w,const struct kui_smash_phase_trace *t,
    const char *name,unsigned first,unsigned last) {
    struct kui_smash_phase_delta d;kui_smash_phase_clock_delta(stamp(t,first),stamp(t,last),&d);
    text(w,name);text(w,": nominal_ms=");
    if(d.flags&KUI_SMASH_PHASE_NOMINAL_MS)number(w,d.cpu_ms);else text(w,"NA");
    text(w," rtc_seconds=");if(d.flags&KUI_SMASH_PHASE_RTC)number(w,d.rtc_seconds);else text(w,"NA");
    text(w," flags=");hex(w,d.flags);put(w,'\n');
}
static void metric(struct writer *w,const char *name,uint32_t value) {
    text(w,name);put(w,'=');number(w,value);put(w,'\n');
}
static void metadata(struct writer *w,const struct kui_smash_phase_trace *t,
    const struct kui_smash_phase_session_stats *s) {
    const struct kui_smash_phase_stamp *a=stamp(t,12u),*b=stamp(t,13u);
    struct kui_smash_phase_delta d;kui_smash_phase_clock_delta(a,b,&d);
    bool compatible=a && b && (d.flags&KUI_SMASH_PHASE_CYCLES) &&
        (s->metadata_io_flags&KUI_SMASH_PHASE_CYCLES) &&
        !(s->metadata_io_flags&~(KUI_SMASH_PHASE_CYCLES|KUI_SMASH_PHASE_NOMINAL_MS)) &&
        a->frqcr==s->metadata_io_frqcr && s->metadata_io_cycles<=d.cycles;
    bool ms=compatible && (d.flags&KUI_SMASH_PHASE_NOMINAL_MS) &&
        (s->metadata_io_flags&KUI_SMASH_PHASE_NOMINAL_MS);
    text(w,"Witness sectors=");number(w,s->witness_sectors);
    text(w," runs=");number(w,s->witness_runs);put(w,'\n');
    text(w,"META12-13 total_ms=");
    if(d.flags&KUI_SMASH_PHASE_NOMINAL_MS)number(w,d.cpu_ms);else text(w,"NA");
    text(w," sd_ms=");if(ms)number(w,(uint32_t)(s->metadata_io_cycles/200000u));else text(w,"NA");
    text(w," cpu_other_ms=");if(ms)number(w,(uint32_t)((d.cycles-s->metadata_io_cycles)/200000u));else text(w,"NA");
    text(w," ioF=");hex(w,s->metadata_io_flags);put(w,'\n');
    text(w,"SDcycles=");
    if(compatible) {
        text(w,"0x");raw_hex(w,(uint32_t)(s->metadata_io_cycles>>32),8u);
        raw_hex(w,(uint32_t)s->metadata_io_cycles,8u);
    } else text(w,"NA");
    text(w," dma_rx=");number(w,s->metadata_dma);
    text(w," polled=");number(w,s->metadata_fallback);put(w,'\n');
}
bool kui_smash_phase_text_format_session(char out[KUI_SMASH_PHASE_TEXT_BYTES],
    const struct kui_smash_phase_trace *t,const char build[13],const char *selection,
    const struct kui_vmu_fs_stats *fs,const uint32_t io[6],
    const struct kui_smash_phase_session_stats *session) {
    if(!out || !t || !build || !selection || !fs || !io ||
       !kui_smash_phase_trace_valid(t) || t->overflow || !t->count ||
       t->entries[t->count-1u].phase!=KUI_SMASH_PHASE_INSTALL_DONE || build[12])return false;
    if(session && (session->receipt_valid>1u || session->receipt_blank>1u ||
       session->native_gin_route>1u || session->allocation_epoch>1u ||
       (session->receipt_valid && session->receipt_blank)))return false;
    for(unsigned i=0;i<12u;i++)if(!((build[i]>='0'&&build[i]<='9')||(build[i]>='a'&&build[i]<='f')))return false;
    for(unsigned i=0;selection[i];i++)if(i>=80u || (unsigned char)selection[i]<32u || (unsigned char)selection[i]>126u)return false;
    memset(out,' ',KUI_SMASH_PHASE_TEXT_BYTES);
    struct writer w={out,KUI_SMASH_PHASE_TEXT_BYTES-1u,true};
    text(&w,"KUI SMASH PHASE TRACE v1\nBUILD ");text(&w,build);put(&w,'\n');
    text(&w,"Selection: ");text(&w,selection);put(&w,'\n');
    text(&w,"73728B worker installed.\n"
        "Untimed: before first MDE callback; after install (SGA/BUP/options); report I/O.\n"
        "nominal_ms=inherited 200MHz PRFC; rtc_seconds=independent coarse RTC.\n"
        "NA=unavailable/changed; matching modes cannot exclude counter reset.\n"
        "SD includes protocol/CRC; cpu_other=elapsed outside SD callbacks.\n\n");
    static const struct {const char *name;unsigned first,last;} phases[]={
        {"First MDE callback to post-GIN",1,2},{"Capsule gate",2,3},{"Capsule SD load",3,4},{"Capsule proof",4,5},
        {"Prefix verification",5,10},{"SD init",10,11},{NULL,12,13},{"Recovery / first image",13,14},
        {"Checkpoint",14,15},{"Image / final identity",15,17},{"IP load",17,18},
        {"MDE restore/load",18,19},{"Pre-BOOT2 setup",19,20},{"BOOT2",20,21},
        {"Base relay proof",21,23},{"SDK/allocator proof",23,24},{"Hooks/arguments",24,25},
        {"MDE CRT/allocator",25,26},{"Worker alloc/relocate",26,27},
        {"Worker arm/hooks",27,28},{"TOTAL first payload callback to installed worker",1,28},
        {"CHILD to installed worker",5,28}};
    for(unsigned i=0;i<sizeof(phases)/sizeof(phases[0]);i++) {
        const char *name=phases[i].name;
        if(!name)name=session && session->allocation_epoch?"Allocation epoch validation":"Witness/admission";
        interval(&w,t,name,phases[i].first,phases[i].last);
    }
    put(&w,'\n');metric(&w,"filesystem_logical_reads",fs->reads);metric(&w,"filesystem_chain_steps",fs->chain_steps);
    metric(&w,"filesystem_fat_entries",fs->fat_entries);metric(&w,"filesystem_directory_sectors",fs->directory_sectors);
    text(&w,"Backend I/O excludes witness:\n");
    static const char *const names[6]={"vmu_prep_logical_reads","vmu_prep_single_commands","vmu_prep_run_commands",
        "vmu_prep_crc_checked_sectors","vmu_prep_writes","vmu_prep_syncs"};
    for(unsigned i=0;i<6u;i++)metric(&w,names[i],io[i]);
    if(session) {
        text(&w,"\nSession counters THIS_OWNER:\n");
        metric(&w,"full_admissions",session->full_admissions);
        metric(&w,"attaches",session->attaches);
        metric(&w,"receipt_generation",session->receipt_generation);
        metric(&w,"receipt_sequence",session->receipt_sequence);
        metric(&w,"receipt_valid",session->receipt_valid);
        metric(&w,"receipt_blank",session->receipt_blank);
        metric(&w,"native_gin_route",session->native_gin_route);
        metric(&w,"boot2_calls",session->boot2_calls);
        metric(&w,"allocation_epoch",session->allocation_epoch);
        metadata(&w,t,session);
    }
    text(&w,"\nRaw: phase flags PMCR0 FRQCR cycle_hi cycle_lo RTC\n");
    for(unsigned i=0;i<t->count;i++) {
        const struct kui_smash_phase_entry *e=&t->entries[i];number(&w,e->phase);
        const uint32_t v[6]={e->stamp.flags,e->stamp.pmcr,e->stamp.frqcr,e->stamp.cycles_hi,e->stamp.cycles_lo,e->stamp.rtc_seconds};
        for(unsigned j=0;j<6u;j++) {put(&w,' ');raw_hex(&w,v[j],j==1u||j==2u?4u:8u);}put(&w,'\n');
    }
    out[KUI_SMASH_PHASE_TEXT_BYTES-1u]='\n';return w.ok;
}
bool kui_smash_phase_text_format(char out[KUI_SMASH_PHASE_TEXT_BYTES],
    const struct kui_smash_phase_trace *t,const char build[13],const char *selection,
    const struct kui_vmu_fs_stats *fs,const uint32_t io[6]) {
    return kui_smash_phase_text_format_session(out,t,build,selection,fs,io,NULL);
}
