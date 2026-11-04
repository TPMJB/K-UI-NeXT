/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/toy_report_text.h"
#include <stdbool.h>
#include <string.h>
#ifndef KUI_TOY_REPORT_STORAGE_BYTES
#define KUI_TOY_REPORT_STORAGE_BYTES 2352
#endif
#if KUI_TOY_REPORT_STORAGE_BYTES != 2048 && KUI_TOY_REPORT_STORAGE_BYTES != 2352
#error Toy report storage geometry must be raw2352 or cooked2048
#endif

struct output {size_t position,offset,capacity,copied;uint8_t *bytes;};
static void character(struct output *out,char c) {
    if(out->bytes && out->position>=out->offset &&
       out->position-out->offset<out->capacity) {
        out->bytes[out->position-out->offset]=(uint8_t)c;++out->copied;
    }
    ++out->position;
}
static void literal(struct output *out,const char *s) {
    while(*s)character(out,*s++);
}
static void hexadecimal(struct output *out,uint32_t n) {
    static const char digits[]="0123456789abcdef";
    for(unsigned i=0;i<8u;i++)character(out,digits[(n>>(28u-4u*i))&15u]);
}
static void words(struct output *out,const void *source,unsigned count) {
    const uint8_t *bytes=source;
    for(unsigned i=0;i<count;i++) {
        uint32_t word;memcpy(&word,bytes+i*4u,4u);
        if(i)character(out,',');
        hexadecimal(out,word);
    }
    character(out,'\n');
}
static bool valid(const struct kui_toy_report_text *s) {
    if(!s || !s->report || !s->stopped_words || !s->build)return false;
    const struct kui_toy_decision_report *r=s->report;
    if(r->magic!=UINT32_C(0x54444f31) || r->version!=1u || r->enabled>1u ||
       r->frozen!=1u || r->head>=8u || r->count>8u)return false;
    for(unsigned i=0;i<12u;i++)if(!((s->build[i]>='0' && s->build[i]<='9') ||
        (s->build[i]>='a' && s->build[i]<='f')))return false;
    return s->build[12]=='\0';
}
static void format(const struct kui_toy_report_text *s,struct output *out) {
    const struct kui_toy_decision_report *r=s->report;
    _Static_assert(offsetof(struct kui_toy_decision_report,bins)==80u,"Report header has20words");
    _Static_assert(sizeof(struct kui_toy_decision_bin)==160u,"Every report bin has40words");
    literal(out,"format=KUI_TOY_REPORT_V1\nbuild=");literal(out,s->build);
    literal(out,"\nprofile=");literal(out,r->enabled?"TC":"TB");
    literal(out,"\nradix=16\nticks_per_second=000bebc2\n");
    literal(out,"data_storage_sector_bytes=");hexadecimal(out,KUI_TOY_REPORT_STORAGE_BYTES);
    literal(out,"\nlogical_data_sector_bytes=00000800\nraw_audio_sector_bytes=00000930\n");
    literal(out,"scope=frozen_after_movie_no_log_io_during_capture\n");
    literal(out,"notes=AP_blocks_only;updater_is_proxy;step_times_include_loop_work;no_CPU_share\n");
    literal(out,"bin_flag_bits=1_badclock,2_skipped,4_crossbin,8_saturated\n");
    literal(out,"report_fields=magic,version,enabled,frozen,head,count,epoch,badclock,first_vbr,last_vbr,expected_vbr,nested,last_cmd,last_lba,last_count,sector_bytes,expired,stop_tick,stop_valid,last_age\nreport=");
    words(out,r,20u);
    literal(out,"stopped_fields=ap_calls,ap_irq_calls,ap_call_blocks,ap_irq_blocks,errors,retries,token,card_owned,audio_state,audio_fault,raw_bytes,updater,ap_enabled,ap_retired,quiesces,quiesce_failed\nstopped=");
    words(out,s->stopped_words,16u);
    literal(out,"bin_fields=seq,epoch,first,last,flags,gd_n,gd_valid,data_n,bytes,gd_ticks,gd_max,gap_n,gap_ticks,gap_max,service_n,service_valid,service_ticks,service_max,wait_n,credit_n,wait_ticks,credit_ticks,other_ticks,zero_services,ap_call_blocks,ap_irq_blocks,errors,retries,mask0_6,mask7_14,mask15,bl,own_vbr,other_vbr,priority,updater,accepted,completed,modes,max_sectors\n");
    for(unsigned i=0;i<8u;i++) {
        literal(out,"bin");character(out,(char)('0'+i));character(out,'=');
        words(out,&r->bins[i],40u);
    }
    literal(out,"end=KUI_TOY_REPORT_V1\n");
}
size_t kui_toy_report_text_length(const struct kui_toy_report_text *source) {
    if(!valid(source))return 0u;
    struct output out={0};format(source,&out);return out.position;
}
size_t kui_toy_report_text_read(void *context,size_t offset,uint8_t *bytes,size_t count) {
    const struct kui_toy_report_text *source=context;
    size_t length=kui_toy_report_text_length(source);
    if(!length || offset>length || count>length-offset || (count && !bytes))return SIZE_MAX;
    struct output out={.offset=offset,.capacity=count,.bytes=bytes};format(source,&out);
    return out.copied;
}
