/* SPDX-License-Identifier: GPL-3.0-only */
/* Dedicated mode4 evidence build. No Maple hook or VMU write is installed.
 * Returned calls merely compare already copied payload with its source. */
#include "kui/vc2_texture_probe.h"
#include "kui/vc2_ce_vmu_handoff.h"
#include "kui/vc2_ce_vmu_diagnostic.h"
#include "kui/vc2_ce_vmu_prepare.h"
#include <stddef.h>
#include <string.h>

uint32_t kui_vc2_ce_vmu_call_context[8],kui_vc2_ce_vmu_owner_context[5];
extern struct kui_vc2_ce_vmu_prepare_report kui_vc2_ce_vmu_prepare_report;
static struct kui_vc2_texture_probe_snapshot stopped;
static uint32_t frozen;
#define TEXTURE_P2 __attribute__((section(".text.vc2_texture_p2"),noinline))
#ifdef KUI_VC2_TEXTURE_PROBE_HOST
#define P2_READ32(address) kui_vc2_texture_probe_host_read32(address)
#else
#define P2_READ32(address) (*(volatile const uint32_t *)(uintptr_t)(address))
#endif

static uint32_t read32(uint32_t address) {
#ifdef KUI_VC2_TEXTURE_PROBE_HOST
    return kui_vc2_texture_probe_host_read32(address);
#else
    return *(volatile const uint32_t *)(uintptr_t)address;
#endif
}
static uint32_t read_sr(void) {
#ifdef KUI_VC2_TEXTURE_PROBE_HOST
    return kui_vc2_texture_probe_host_sr();
#else
    uint32_t sr;__asm__ volatile("stc sr,%0":"=r"(sr));return sr;
#endif
}
void TEXTURE_P2 kui_vc2_texture_probe_fold(const uint8_t *source,const void *destination,
    uint32_t bytes,struct kui_vc2_texture_fold *out) {
    typedef uint16_t probe_half __attribute__((__may_alias__));
    volatile struct kui_vc2_texture_fold *fold=out;
    uintptr_t address=(uintptr_t)destination;
    /* Explicit volatile scalar stores prevent an aggregate-init memcpy
     * from fetching a cached P1 constant before the immediate comparison. */
    fold->source_hash=UINT32_C(2166136261);
    fold->destination_hash=UINT32_C(2166136261);
    fold->first_offset=UINT32_MAX;fold->expected=0u;fold->actual=0u;
    for(uint32_t i=0u;i<bytes;i++) {
        uint32_t expected=source[i];
        uint32_t half=*(volatile const probe_half *)((address+i)&~(uintptr_t)1u);
        uint32_t actual=(half>>(((address+i)&1u)*8u))&255u;
        fold->source_hash=(fold->source_hash^expected)*UINT32_C(16777619);
        fold->destination_hash=(fold->destination_hash^actual)*UINT32_C(16777619);
        if(expected!=actual && fold->first_offset==UINT32_MAX) {
            fold->first_offset=i;fold->expected=expected;fold->actual=actual;
        }
    }
}
void TEXTURE_P2 kui_vc2_texture_probe_precapture(const uint8_t *source,void *destination,
    uint32_t count,uint32_t offset) {
    volatile uint32_t *pre=stopped.pre_state;
    pre[0]=0u;pre[1]=(uint32_t)(uintptr_t)source;
    pre[2]=(uint32_t)(uintptr_t)destination;pre[3]=count;pre[4]=offset;
    for(unsigned i=0u;i<4u;i++)
        stopped.dma_pre[i]=P2_READ32(UINT32_C(0xffa00010)+i*4u);
    if(!source || count>sizeof(stopped.saved_source)) {pre[0]=2u;return;}
    for(uint32_t i=0u;i<count;i++)
        ((volatile uint8_t *)stopped.saved_source)[i]=((volatile const uint8_t *)source)[i];
    pre[0]=1u;
}
void kui_vc2_texture_probe_late(const uint8_t *,void *,uint32_t,uint32_t);
void TEXTURE_P2 kui_vc2_texture_probe_run(const uint8_t *source,void *destination,
    uint32_t count,uint32_t offset) {
    if(frozen) {kui_vc2_texture_probe_late(source,destination,count,offset);return;}
    if(count&KUI_VC2_TEXTURE_PROBE_PRE) {
        kui_vc2_texture_probe_precapture(source,destination,
            count&~(uint32_t)KUI_VC2_TEXTURE_PROBE_PRE,offset);return;
    }
    if(source && stopped.pre_state[0]==1u &&
       stopped.pre_state[1]==(uint32_t)(uintptr_t)source &&
       stopped.pre_state[2]==(uint32_t)(uintptr_t)destination &&
       stopped.pre_state[3]==count && stopped.pre_state[4]==offset) {
        /* These P2 calls precede every cached-code/context/table access. */
        kui_vc2_texture_probe_fold(stopped.saved_source,destination,count,&stopped.fold);
        kui_vc2_texture_probe_fold(stopped.saved_source,source,count,&stopped.source);
        stopped.pre_state[0]=3u;
    } else if(source)stopped.stop[0]|=KUI_VC2_TEXTURE_PRESTATE;
    for(unsigned i=0u;i<4u;i++)
        stopped.dma_post[i]=P2_READ32(UINT32_C(0xffa00010)+i*4u);
    /* The unique normal transition into cached code is deliberately last. */
    kui_vc2_texture_probe_late(source,destination,count,offset);
}
uint32_t kui_vc2_texture_probe_utlb(uint32_t va,uint32_t address,uint32_t data,
    uint32_t pteh,uint32_t mmucr,uint32_t sr,uint32_t *physical) {
    static const uint32_t masks[]={0x3ffu,0xfffu,0xffffu,0xfffffu};
    uint32_t mask=masks[((data>>6)&2u)|((data>>4)&1u)];
    if(!(address&0x100u) || !(data&0x100u) ||
       (va&~mask)!=(address&~mask&UINT32_C(0xfffffc00)) ||
       (!(data&2u) && !((mmucr&0x100u) && (sr&UINT32_C(0x40000000))) &&
        ((address^pteh)&255u)))return 0u;
    *physical=(data&UINT32_C(0x1ffffc00)&~mask)|(va&mask);
    return 1u;
}
static void translation(uint32_t va,uint32_t out[6]) {
    for(unsigned i=0u;i<6u;i++)out[i]=0u;
    out[1]=UINT32_MAX;
    if((va>>29)==4u || (va>>29)==5u) {
        out[4]=va&UINT32_C(0x1fffffff);return;
    }
    if(!(stopped.cpu[1]&1u)) {out[4]=va&UINT32_C(0x1fffffff);return;}
    for(uint32_t i=0u;i<64u;i++) {
        uint32_t address=read32(UINT32_C(0xf6000000)+(i<<8));
        uint32_t data=read32(UINT32_C(0xf7000000)+(i<<8)),physical;
        if(!kui_vc2_texture_probe_utlb(va,address,data,stopped.cpu[4],
                                     stopped.cpu[1],stopped.cpu[0],&physical))continue;
        ++out[0];out[1]=i;out[2]=address;out[3]=data;out[4]=physical;
        static const uint32_t masks[]={0x3ffu,0xfffu,0xffffu,0xfffffu};
        out[5]=masks[((data>>6)&2u)|((data>>4)&1u)];
    }
}
static void cpu_snapshot(void) {
    stopped.cpu[0]=read_sr();stopped.cpu[1]=read32(UINT32_C(0xff000010));
    stopped.cpu[2]=read32(UINT32_C(0xff000008));
    stopped.cpu[3]=read32(UINT32_C(0xff00001c));
    stopped.cpu[4]=read32(UINT32_C(0xff000000));
}
static void terminal(void) __attribute__((noreturn));
static void terminal(void) {
#ifdef KUI_VC2_TEXTURE_PROBE_HOST
    kui_vc2_texture_probe_host_stop(&stopped);
#else
    for(unsigned page=0u;;page=(page+1u)%3u) {
        retail_display_restore(&stopped.display);
        if(stopped.stop[0]&KUI_VC2_TEXTURE_PREPARE) {
            retail_display_line("VC2 PROBE PREP STOP");
            retail_display_values("WHY PHASE GOT WANT RAMEND",stopped.prepare,5u);
        } else if(page==0u) {
            retail_display_line("VC2 PROBE 1 OF 3 DATA");
            retail_display_values("WHY CALLS BYTES TARGET MAPFAIL",stopped.stop,5u);
            retail_display_values("SRC DST LEN OFFSET FAD",stopped.chunk,5u);
            retail_display_values("EARLY SRC DST FIRST WANT GOT",
                &stopped.fold.source_hash,5u);
            retail_display_values("LATE DST FIRST WANT GOT",&stopped.late.destination_hash,4u);
            retail_display_values("SRC PRE LIVE FIRST WANT GOT",&stopped.source.source_hash,5u);
            retail_display_values("SRC LATE FIRST WANT GOT",&stopped.source_late.destination_hash,4u);
            retail_display_values("PRE STATE SRC DST LEN OFFSET",stopped.pre_state,5u);
        } else if(page==1u) {
            retail_display_line("VC2 PROBE 2 OF 3 CONTEXT");
            retail_display_values("TOKEN CMD LBA COUNT DEST",stopped.context,5u);
            retail_display_values("TOTAL DONE PIECE ERR STAT",stopped.context+5u,5u);
            retail_display_values("PEND MMU PR SP FN",stopped.context+10u,5u);
            retail_display_values("SR MMUCR TTB CCR PTEH",stopped.cpu,5u);
            retail_display_values("FIRST N IDX ADDR DATA PHYS",stopped.tlb_first,5u);
            retail_display_values("RAW R5",stopped.context+15u,1u);
        } else {
            retail_display_line("VC2 PROBE 3 OF 3 DMA");
            retail_display_values("PRE SAR DAR TCR CHCR",stopped.dma_pre,4u);
            retail_display_values("POST SAR DAR TCR CHCR",stopped.dma_post,4u);
            retail_display_values("EVICT READS",&stopped.eviction_reads,1u);
        }
        retail_display_line("PHOTO ALL 3 - POWER OFF");
        retail_display_pause(480u);
    }
#endif
}
static void freeze(void) __attribute__((noreturn));
static void freeze(void) {
    const struct retail_display_state *display=kui_vc2_ce_vmu_saved_display();
    if(display)stopped.display=*display;else retail_display_capture(&stopped.display);
    frozen=1u;terminal();
}
void kui_vc2_texture_probe_late(const uint8_t *source,void *destination,
    uint32_t count,uint32_t offset) {
    if(frozen)terminal();
    ++stopped.stop[1];
    stopped.eviction_reads=0u;
    kui_vc2_ce_vmu_texture_context(stopped.context);
    stopped.chunk[0]=(uint32_t)(uintptr_t)source;
    stopped.chunk[1]=(uint32_t)(uintptr_t)destination;
    stopped.chunk[2]=count;stopped.chunk[3]=offset;
    uint32_t lba=stopped.context[2]+(offset>>11);
    stopped.chunk[4]=lba+150u;
    cpu_snapshot();
    translation((uint32_t)(uintptr_t)destination,stopped.tlb_first);
    if(!source || (stopped.stop[0]&KUI_VC2_TEXTURE_PRESTATE)) {
        /* No destination was copied/read back for this event. Never present
         * the preceding successful chunk's hashes as map-failure evidence. */
        for(unsigned i=0u;i<4u;i++) {
            struct kui_vc2_texture_fold *fold=(void *)((uint8_t *)&stopped+
                offsetof(struct kui_vc2_texture_probe_snapshot,fold)+i*sizeof(*fold));
            *fold=(struct kui_vc2_texture_fold){0u,0u,UINT32_MAX-1u,0u,0u};
        }
        if(!source) {
            stopped.pre_state[0]=0u;
            ++stopped.stop[4];stopped.stop[0]=KUI_VC2_TEXTURE_MAP;
        }
        freeze();
    }
    /* Controlled read-only cache pressure: use only this admitted immutable
     * helper's P1 alias at matching OIX0 operand-cache indices. Never read
     * a P1 alias of the private P2 snapshot or saved source. */
    if(!(stopped.cpu[3]&0x80u)) {
        uintptr_t at=(uintptr_t)destination&~(uintptr_t)31u;
        uintptr_t last=((uintptr_t)destination+(count?count-1u:0u))&~(uintptr_t)31u;
        for(;at<=last;at+=32u) {
            uint32_t index=(uint32_t)at&0x3fe0u;
            for(unsigned colour=0u;colour<4u;colour++) {
                (void)read32(UINT32_C(0x8cfc4000)+index);
                ++stopped.eviction_reads;index=(index+0x1000u)&0x3fffu;
            }
        }
    }
    kui_vc2_texture_probe_fold(stopped.saved_source,destination,count,&stopped.late);
    kui_vc2_texture_probe_fold(stopped.saved_source,source,count,&stopped.source_late);
    stopped.stop[2]+=count;
    if(stopped.fold.first_offset!=UINT32_MAX || stopped.late.first_offset!=UINT32_MAX)
        stopped.stop[0]|=KUI_VC2_TEXTURE_MISMATCH;
    if(stopped.source.first_offset!=UINT32_MAX || stopped.source_late.first_offset!=UINT32_MAX)
        stopped.stop[0]|=KUI_VC2_TEXTURE_SOURCE_CHANGED;
    if(stopped.fold.destination_hash!=stopped.late.destination_hash ||
       stopped.source.destination_hash!=stopped.source_late.destination_hash)
        stopped.stop[0]|=KUI_VC2_TEXTURE_EARLY_LATE_DIFF;
    if(lba>=512435u && lba<513058u)++stopped.stop[3];
    if(lba>=513035u && lba<513058u)stopped.stop[0]|=KUI_VC2_TEXTURE_TARGET_TAIL;
    if(stopped.stop[1]>=32768u)stopped.stop[0]|=KUI_VC2_TEXTURE_LIMIT;
    if(stopped.stop[0])freeze();
}
void kui_vc2_ce_vmu_diagnostic_stop(uint32_t reason,uint32_t phase) {
    stopped.stop[0]=KUI_VC2_TEXTURE_PREPARE;
    stopped.prepare[0]=reason;stopped.prepare[1]=phase;
    stopped.prepare[2]=kui_vc2_ce_vmu_prepare_report.actual;
    stopped.prepare[3]=kui_vc2_ce_vmu_prepare_report.expected;
    stopped.prepare[4]=kui_vc2_ce_vmu_prepare_report.ram_end;
    freeze();
}
#ifdef KUI_VC2_TEXTURE_PROBE_HOST
void kui_vc2_texture_probe_host_reset(void) {
    memset(&stopped,0,sizeof(stopped));frozen=0u;
}
#endif
