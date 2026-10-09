/* SPDX-License-Identifier: GPL-3.0-only */
/* Diagnostic-only baseline: no timer, IRQ or sound-register writes. */
#include "kui/retail_observe.h"
static struct kui_retail_observe observe;
static uint32_t observe_words[11];
#if KUI_RETAIL_READ_COMPARE
#if KUI_SCI_SD_PIO_ONLY
#define OBSERVE_TITLE "PROFILE14 PIO /"
#else
#define OBSERVE_TITLE "PROFILE14 DMA /"
#endif
#else
#define OBSERVE_TITLE "PROFILE14 OBSERVE /"
#endif
#ifdef KUI_RETAIL_OBSERVE_HOST_TEST
extern uint32_t kui_retail_observe_host_read(uint32_t,unsigned);
extern uint32_t kui_retail_observe_host_cpu(unsigned);
#define OBS32(a) kui_retail_observe_host_read(a,4u)
#define OBS16(a) kui_retail_observe_host_read(a,2u)
#define OBS8(a) kui_retail_observe_host_read(a,1u)
#else
#define OBS32(a) (*(volatile const uint32_t *)(uintptr_t)(a))
#define OBS16(a) (*(volatile const uint16_t *)(uintptr_t)(a))
#define OBS8(a) (*(volatile const uint8_t *)(uintptr_t)(a))
#endif
#if KUI_RETAIL_OBSERVE_AUDIO
static uint32_t observe_dma(void) {
    uint32_t mask=0;
    for(unsigned i=0;i<4u;i++) {
        if(OBS32(0xa05f7814u+i*32u)&1u) mask|=1u<<i;
        if(OBS32(0xa05f7818u+i*32u)&1u) mask|=1u<<(i+4u);
    }
    return mask;
}
static void observe_audio(void) {
    /* Never suspend a game's DMA. Skip enabled/active DMA or a busy FIFO.
     * Sampling is sparse; absence of an observed channel is not ownership. */
    uint32_t dma=observe_dma(),hash=0x811c9dc5u;
    observe_words[8]=observe_words[9]=0;
    if(!observe.samples[2] && !observe.skipped_audio) observe.dma_first=observe.dma_last=dma;
    observe.dma_changed|=observe.dma_last^dma;observe.dma_last=dma;observe.dma_ever|=dma;
    if(dma || (OBS32(0xa05f688cu)&0x31u)) {if(observe.skipped_audio!=UINT32_MAX) observe.skipped_audio++;return;}
    for(unsigned channel=0;channel<64u;channel++) {
        if(OBS32(0xa05f688cu)&0x31u) {if(observe.skipped_audio!=UINT32_MAX) observe.skipped_audio++;return;}
        uint32_t base=0xa0700000u+channel*128u,control=OBS32(base);
        if(control&0x4000u) observe_words[8u+channel/32u]|=1u<<(channel%32u);
        hash=(hash^control)*16777619u;
        hash=(hash^OBS32(base+4u))*16777619u;
        hash=(hash^OBS32(base+24u))*16777619u;
        hash=(hash^OBS32(base+36u))*16777619u;
    }
    if(OBS32(0xa05f688cu)&0x31u) {if(observe.skipped_audio!=UINT32_MAX) observe.skipped_audio++;return;}
    observe_words[0]=OBS32(0xa0702800u);observe_words[1]=OBS32(0xa0702c00u);
    observe_words[2]=dma;observe_words[3]=OBS32(0xa070289cu);
    observe_words[4]=OBS32(0xa07028a0u);observe_words[5]=hash;
    observe_words[6]=observe_words[8];observe_words[7]=observe_words[9];
    kui_retail_observe_sample(&observe,KUI_OBSERVE_AICA,observe_words);
}
#endif
static void observe_call(void) {
    if(observe.calls!=UINT32_MAX) ++observe.calls;
    observe_words[0]=kui_retail_hook_sr;
#ifdef KUI_RETAIL_OBSERVE_HOST_TEST
    observe_words[1]=kui_retail_observe_host_cpu(0u);observe_words[2]=kui_retail_observe_host_cpu(1u);
#else
    __asm__ __volatile__("stc vbr,%0":"=r"(observe_words[1]));
    __asm__ __volatile__("stc gbr,%0":"=r"(observe_words[2]));
#endif
    observe_words[3]=kui_retail_native_caller[0];observe_words[4]=kui_retail_native_caller[1];
    observe_words[5]=OBS32(0xff000010u);
    kui_retail_observe_sample(&observe,KUI_OBSERVE_CPU,observe_words);
    observe_words[0]=OBS8(0xffd80004u);observe_words[1]=OBS16(0xffc00000u);
    for(unsigned i=0;i<3u;i++) {
        observe_words[2u+i*3u]=OBS32(0xffd80008u+i*12u);
        observe_words[3u+i*3u]=OBS32(0xffd8000cu+i*12u);
        observe_words[4u+i*3u]=OBS16(0xffd80010u+i*12u);
    }
    kui_retail_observe_sample(&observe,KUI_OBSERVE_TMU,observe_words);
#if KUI_RETAIL_OBSERVE_AUDIO
    if((observe.calls&255u)==1u) observe_audio();
#endif
}
static void observe_result(uint32_t function,int32_t result) {
    if(function==KUI_GD_REQUEST && result>0 && (service.command==20u || service.command==21u)) {
        kui_retail_observe_play(&observe,service.command,service.outputs);
#if KUI_RETAIL_OBSERVE_AUDIO
        observe_audio();
#endif
    }
}
/* Terminal rendering never resumes image I/O. Reuse its sector cache only
 * after that transition, avoiding a second 50-byte private-stack buffer. */
typedef uint32_t observe_word __attribute__((__may_alias__));
static void observe_values(const char *legend,const observe_word *values,unsigned count) {
    char *next=(char *)(void *)image.block;
    retail_display_line(legend);
    for(unsigned v=0;v<count && v<5u;v++) {
        uint32_t value=values[v];char *digit=next+8;
        while(digit!=next) {*--digit="0123456789ABCDEF"[value&15u];value>>=4;}
        next+=8;*next++=' ';
    }
    *next=0;retail_display_line((const char *)(const void *)image.block);
}
static void observe_pair_page(const uint32_t *first,unsigned count,unsigned group) {
    const uint32_t *last=first+count;
    observe_words[0]=last[count];observe_words[1]=observe.samples[group];
    retail_display_line(OBSERVE_TITLE);
    while(count) {
        unsigned n=count;if(n>5u) n=5u;
        observe_values("F",first,n);observe_values("L",last,n);
        first+=n;last+=n;count-=n;
    }
    observe_values("C N",observe_words,2);
}
static inline __attribute__((always_inline)) void observe_next_page(void) {
    retail_display_pause(1200u);retail_display_restore(&display);
}
static void observe_report(void) {
    retail_display_line(OBSERVE_TITLE);
    observe_words[0]=observe.calls;observe_words[1]=observe.play[0];observe_words[2]=observe.play[1];
    observe_words[3]=kui_retail_hook_fault;
#if KUI_RETAIL_OBSERVE_AUDIO
    observe_words[4]=observe.samples[2];
    observe_values("CALL 20 21 BAD N",observe_words,5);
#else
    observe_values("CALL 20 21 BAD",observe_words,4);
#endif
    observe_values("20F",observe.first_play[0],3);
    observe_values("20L",observe.last_play[0],3);
    observe_values("21F",observe.first_play[1],3);
    observe_values("21L",observe.last_play[1],3);
    observe_words[0]=observe.cpu_changed;observe_words[1]=observe.tmu_changed;
#if KUI_RETAIL_OBSERVE_AUDIO
    observe_words[2]=observe.audio_changed;observe_words[3]=observe.skipped_audio;
    observe_values("CPU TMU AICA SKIP",observe_words,4);observe_values("KEY0 KEY1",observe.ever_key,2);
#else
    observe_values("CPU TMU",observe_words,2);retail_display_line("AUDIO NOT SAMPLED");
#endif
    observe_next_page();
    observe_pair_page(observe.cpu_words,6u,KUI_OBSERVE_CPU);
    observe_next_page();
    observe_pair_page(observe.tmu_words,11u,KUI_OBSERVE_TMU);
    observe_next_page();
#if KUI_RETAIL_OBSERVE_AUDIO
    observe_pair_page(observe.audio_words,8u,KUI_OBSERVE_AICA);observe_values("DMA F L OR CHG",observe.dma_words,4);
#else
    retail_display_line(OBSERVE_TITLE);retail_display_line("AUDIO NOT SAMPLED");
#endif
}
static void observe_fault_report(const char *reason,uint32_t function,uint32_t param) {
    /* Only a terminal path may reuse the image cache. Keep binary snapshots
     * beyond the formatter's first46 bytes so printing cannot overwrite the
     * next value. Capture the current guard before any terminal display work. */
    observe_word *snapshot=(observe_word *)(void *)(image.block+64u);
    _Static_assert(sizeof(image.block)>=64u+21u*sizeof(*snapshot),"terminal fault snapshot");
    snapshot[0]=function;snapshot[1]=param;snapshot[2]=kui_retail_hook_fault;
    snapshot[3]=OBS32(KUI_RETAIL_HOOK_STACK_BOTTOM);
    uint32_t bad=snapshot[3]!=0x4b554947u;
    for(unsigned i=1u;i<4u;i++) if(OBS32(KUI_RETAIL_HOOK_STACK_BOTTOM+4u*i)!=0x4b554947u) bad|=1u<<i;
    snapshot[4]=bad;
    snapshot[5]=service.command;snapshot[6]=service.lba;snapshot[7]=service.count;snapshot[8]=service.sector_bytes;
    snapshot[9]=observe_read_lba;snapshot[10]=observe_read_count;
    snapshot[11]=service.completed_bytes;snapshot[12]=service.step;
    snapshot[13]=(uint32_t)image_result;snapshot[14]=(uint32_t)observe_read_card_result;
    snapshot[15]=(uint32_t)observe_stop_result;snapshot[16]=(uint32_t)card_result;
    snapshot[17]=service.destination;snapshot[18]=image.blocks_read;snapshot[19]=service.error;snapshot[20]=observe_card_lba;
    retail_display_restore(&display);retail_display_line("14 STOP");retail_display_line(reason);
    observe_values("FN ARG FLAG G BAD",snapshot,5);
    observe_values("CMD LBA N BPS",snapshot+5,4);
    observe_values("IO LBA N DONE STEP",snapshot+9,4);
    observe_values("IMG PRE STOP SD",snapshot+13,4);
    observe_values("DST BLK ERR CARD",snapshot+17,4);
    retail_display_pause(1200u);retail_display_restore(&display);observe_report();
}
#undef OBS32
#undef OBS16
#undef OBS8
