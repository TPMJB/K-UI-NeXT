/* SPDX-License-Identifier: GPL-3.0-only */
/* Temporary high-stage telemetry only. No record enters the VMU worker. */
extern const struct kui_smash_phase_trace smash_phase_seed __asm__("__smash_phase_trace_seed");
extern const struct kui_smash_phase_video_seed smash_phase_video_seed __asm__("__smash_phase_video_seed");
struct kui_smash_phase_trace kui_smash_phase_stage_trace;
static uint32_t smash_phase_fault;
#if !KUI_SMASH_PHASE_TEXT
static uint32_t smash_phase_delta_flags;
#endif
extern void retail_display_phase_reset(const struct retail_display_state *);
static void smash_phase_stage_display(struct retail_display_state *out) {
    /* Inherited menu buffers may be allocated away from VRAM0 or blanked.
     * Save them only for the existing refusal path; on success, every child
     * progress/report restore uses the original pre-menu K-UI capture. */
    retail_display_capture(out);
    struct kui_smash_phase_video_seed seed;
    memcpy(&seed,&smash_phase_video_seed,sizeof(seed));
    if(!kui_smash_phase_video_valid(&seed))stopped("TIMING ORIGINAL VIDEO INVALID",0u);
    memcpy(out->regs,seed.regs,sizeof(out->regs));
}
void kui_smash_phase_stage_mark(uint32_t phase) {
    if(!kui_smash_phase_trace_mark(&kui_smash_phase_stage_trace,phase))smash_phase_fault=phase;
}
static void smash_phase_stage_begin(void) {
    memcpy(&kui_smash_phase_stage_trace,&smash_phase_seed,sizeof(smash_phase_seed));
    if(!kui_smash_phase_trace_valid(&kui_smash_phase_stage_trace) ||
       (kui_smash_phase_stage_trace.count &&
        kui_smash_phase_stage_trace.entries[kui_smash_phase_stage_trace.count-1u].phase>5u))
        stopped("TIMING SEED INVALID",0u);
    kui_smash_phase_stage_mark(KUI_SMASH_PHASE_STAGE_BEGIN);
}
#if !KUI_SMASH_PHASE_TEXT
static const struct kui_smash_phase_stamp *smash_phase_find(uint32_t phase) {
    for(uint32_t i=0;i<kui_smash_phase_stage_trace.count;i++)
        if(kui_smash_phase_stage_trace.entries[i].phase==phase)return &kui_smash_phase_stage_trace.entries[i].stamp;
    return NULL;
}
static char *smash_phase_decimal(char *out,uint32_t value,unsigned width) {
    char reverse[10];unsigned n=0u;
    do {reverse[n++]=(char)('0'+value%10u);value/=10u;}while(value);
    while(width>n) {*out++=' ';--width;}
    while(n)*out++=reverse[--n];
    return out;
}
static char *smash_phase_value(char *out,uint32_t value,unsigned width,bool valid) {
    if(width<10u) {
        uint32_t limit=1u;for(unsigned i=0u;i<width;i++)limit*=10u;
        if(value>=limit)valid=false;
    }
    if(valid)return smash_phase_decimal(out,value,width);
    while(width>2u) {*out++=' ';--width;}*out++='N';*out++='A';return out;
}
static char *smash_phase_column(char *out,const char *label,uint32_t first,uint32_t last) {
    unsigned n=0u;while(label[n]) {*out++=label[n++];}
    while(n++<8u)*out++=' ';
    struct kui_smash_phase_delta d;
    kui_smash_phase_clock_delta(smash_phase_find(first),smash_phase_find(last),&d);
    smash_phase_delta_flags|=d.flags;
    out=smash_phase_value(out,d.cpu_ms,7u,(d.flags&KUI_SMASH_PHASE_NOMINAL_MS)!=0u);
    *out++=' ';out=smash_phase_value(out,d.rtc_seconds,5u,(d.flags&KUI_SMASH_PHASE_RTC)!=0u);
    return out;
}
static void smash_phase_pair(const char *a,uint32_t a0,uint32_t a1,const char *b,uint32_t b0,uint32_t b1) {
    char text[50],*p=smash_phase_column(text,a,a0,a1);*p++=' ';*p++=' ';
    p=smash_phase_column(p,b,b0,b1);*p=0;retail_display_line(text);
}
static void smash_phase_counts(uint32_t a,uint32_t b,uint32_t c) {
    char text[50],*p=smash_phase_decimal(text,a,12u);*p++=' ';
    p=smash_phase_decimal(p,b,12u);*p++=' ';p=smash_phase_decimal(p,c,12u);*p=0;
    retail_display_line(text);
}
static char *smash_phase_hex(char *out,uint32_t value,unsigned digits) {
    while(digits)*out++="0123456789ABCDEF"[(value>>(4u*--digits))&15u];
    return out;
}
static void smash_phase_clock_metadata(void) {
    const struct kui_smash_phase_trace *t=&kui_smash_phase_stage_trace;
    const struct kui_smash_phase_stamp *a=&t->entries[0].stamp,*b=&t->entries[t->count-1u].stamp;
    uint32_t flags=smash_phase_delta_flags,invalid=0u;
    for(uint32_t i=0u;i<t->count;i++) {
        flags|=t->entries[i].stamp.flags;
        if(!(t->entries[i].stamp.flags&KUI_SMASH_PHASE_CYCLES))++invalid;
    }
    char text[50],*p=text;
    memcpy(p,"CLK ",4u);p+=4u;p=smash_phase_hex(p,a->pmcr,4u);*p++=' ';
    p=smash_phase_hex(p,a->frqcr,4u);*p++=' ';p=smash_phase_hex(p,b->pmcr,4u);*p++=' ';
    p=smash_phase_hex(p,b->frqcr,4u);*p++=' ';*p++='F';p=smash_phase_hex(p,flags,8u);
    *p++=' ';*p++='N';p=smash_phase_decimal(p,invalid,2u);*p=0;retail_display_line(text);
}
static void smash_phase_stage_report(void) {
    if(smash_phase_fault || !kui_smash_phase_trace_valid(&kui_smash_phase_stage_trace))
        stopped("TIMING TRACE INVALID",smash_phase_fault);
    retail_display_phase_reset(&display);
    retail_display_line("BUILD " KUI_BUILD_ID " TIMING");
    retail_display_line("PHASE    NOM MS RTCSEC  PHASE    NOM MS RTCSEC");
    smash_phase_pair("GIN MDE",1,2,"GATE",2,3);
    smash_phase_pair("CAPLOAD",3,4,"CAPCHECK",4,5);
    smash_phase_pair("COLD SD",5,11,"FS OWNER",11,13);
    smash_phase_pair("REC HASH",13,14,"CHECKPT",14,15);
    smash_phase_pair("VMU HASH",15,17,"IP LOAD",17,18);
    smash_phase_pair("MDE LOAD",18,19,"SETUP",19,20);
    smash_phase_pair("BOOT2",20,21,"RELAY",21,23);
    smash_phase_pair("NATIVE",23,24,"ARM",24,25);
    smash_phase_pair("TOTAL",1,25,"CHILD",5,25);
    const struct kui_vmu_fs_stats *s=&native_persist_work.filesystem.stats;
    retail_display_line("FS READS     FAT ENTRIES  CHAIN STEPS");
    smash_phase_counts(s->reads,s->fat_entries,s->chain_steps);
    retail_display_line("DIR SECTORS  VMU REQUESTS VMU SD GOOD");
    smash_phase_counts(s->directory_sectors,smash_phase_io[0],smash_phase_io[3]);
    retail_display_line("VMU SINGLE   VMU RUNS     VMU WRITES");
    smash_phase_counts(smash_phase_io[1],smash_phase_io[2],smash_phase_io[4]);
    smash_phase_clock_metadata();
    retail_display_line("BEFORE MDE ENTRY  NA CLOCK UNAVAILABLE");
    retail_display_line("GAME SGA LOAD NOT TIMED  PHOTO POWER OFF");
    for(;;)__asm__ volatile("nop");
}
#endif
