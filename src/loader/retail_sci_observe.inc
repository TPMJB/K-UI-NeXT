/* SPDX-License-Identifier: GPL-3.0-only */
/* Isolated first-fault SCI diagnostic. No live CPU/TMU/AICA inventory. */
#include "sci_sd_bus.h"
#if KUI_RETAIL_OBSERVE_AUDIO || KUI_RETAIL_READ_COMPARE || KUI_SCI_SD_PIO_ONLY
#error "SCI fault test uses the unmodified DMA-eligible read path only"
#endif
#if KUI_SCI_DMA_PACED
#define OBSERVE_TITLE "PROFILE14 SCI PACED /"
#else
#define OBSERVE_TITLE "PROFILE14 SCI FAULT /"
#endif
static uint32_t observe_calls,observe_play[2];
static void observe_call(void) {++observe_calls;}
static void observe_result(uint32_t function,int32_t result) {
    if(function==KUI_GD_REQUEST && result>0 && (service.command==20u || service.command==21u))
        ++observe_play[service.command-20u];
}
/* Terminal-only cache reuse keeps formatting off the guarded private stack.
 * Fault snapshots live beyond the formatter's first46bytes. No I/O resumes. */
typedef uint32_t observe_word __attribute__((__may_alias__));
static void observe_values(const char *legend,const observe_word *values,unsigned count) {
    char *next=(char *)(void *)image.block;
    retail_display_line(legend);
    for(unsigned v=0;v<count;v++) {
        uint32_t value=values[v];char *digit=next+8;
        while(digit!=next) {*--digit="0123456789ABCDEF"[value&15u];value>>=4;}
        next+=8;*next++=' ';
    }
    *next=0;retail_display_line((const char *)(const void *)image.block);
}
static void observe_report(void) {
    observe_word *snapshot=(observe_word *)(void *)(image.block+160u);
    _Static_assert(sizeof(struct kui_sci_sd_diagnostic)==16u*sizeof(*snapshot),"SCI trace rows");
    _Static_assert(sizeof(image.block)>=160u+20u*sizeof(*snapshot),"SCI terminal trace");
    memcpy(snapshot,kui_sci_sd_diagnostic_get(),sizeof(struct kui_sci_sd_diagnostic));
    snapshot[16]=observe_calls;snapshot[17]=observe_play[0];snapshot[18]=observe_play[1];snapshot[19]=kui_retail_hook_fault;
    retail_display_line(OBSERVE_TITLE);
    observe_values("PHASE REASON EXPECT POLLS",snapshot,4);
    observe_values("SSR SCR SMR BRR",snapshot+4,4);
    observe_values("CHCR1 TCR1 DMAOR CHCR2 TCR2",snapshot+8,5);
    observe_values("DMA START OK FALLBACK",snapshot+13,3);
    observe_values("CALL PLAY20 PLAY21 BAD",snapshot+16,4);
    retail_display_line("PHASE 0: NO SCI FAULT CAPTURED");
}
#ifdef KUI_RETAIL_OBSERVE_HOST_TEST
extern uint32_t kui_retail_observe_host_read(uint32_t,unsigned);
#define OBS32(a) kui_retail_observe_host_read(a,4u)
#else
#define OBS32(a) (*(volatile const uint32_t *)(uintptr_t)(a))
#endif
static void observe_fault_report(const char *reason,uint32_t function,uint32_t param) {
    observe_word *snapshot=(observe_word *)(void *)(image.block+64u);
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
