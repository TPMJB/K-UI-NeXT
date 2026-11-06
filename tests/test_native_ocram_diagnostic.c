/* SPDX-License-Identifier: GPL-3.0-only */
/* Authored diagnostic values. Render mode executes the real framebuffer/font
 * writer against guarded Linux mappings, never Dreamcast display hardware. */
#define _GNU_SOURCE
#include "kui/retail_gd.h"
#include "kui/native_ocram_diagnostic.h"
#include "../src/loader/retail_display.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef KUI_OC_DIAG_RENDER
#include <sys/mman.h>
#include <unistd.h>
#endif

static struct kui_retail_gd service;
static struct retail_display_state display;
volatile uint32_t kui_retail_hook_active,kui_retail_hook_fault;
volatile struct kui_native_ocram_diagnostic kui_native_ocram_trace;
static uint32_t uncached[4];
static uint32_t p2_word(const volatile void *p) {
    if(p==&service.token)return uncached[0];
    if(p==&service.command)return uncached[1];
    if(p==&service.status)return uncached[2];
    assert(p==&kui_retail_hook_active);return uncached[3];
}
#define KUI_OC_DIAG_P2(pointer) p2_word(pointer)

static void fixture(unsigned which) {
    memset(&service,0,sizeof(service));memset(&display,0,sizeof(display));
    uint32_t entry[16]={0x31,0x53,0x7dfff000,0,1,0x8ce085d8,0x7e000fd8,
        0x700000f1,0,UINT32_MAX,0,0,0x101,0x8c000000,0,4};
    if(which){entry[12]=0x121;entry[15]=0;}
    if(which==2u){entry[1]=0;entry[9]=4;entry[10]=1;}
    for(unsigned i=0;i<16u;i++)kui_native_ocram_trace.entry[i]=entry[i];
    uint32_t history[4][5]={{3,0,0,0,0x121},{0,24,0,0x53,0x121},
        {2,0,0,0,0x121},{1,0x53,0x7dfff000,UINT32_MAX,entry[12]}};
    if(which==2u) {
        history[0][0]=1;history[0][1]=0x52;history[0][2]=0x8c010000;
        history[0][3]=2;history[0][4]=0x105;
        history[1][3]=0;history[2][3]=UINT32_MAX;
        history[3][1]=0;history[3][3]=4;
    }
    for(unsigned i=0;i<4u;i++)for(unsigned j=0;j<5u;j++)
        kui_native_ocram_trace.history[i][j]=history[i][j];
    kui_native_ocram_trace.copyback_ccr=which==2u?0x105u:entry[12];
    service.token=which==1u?0x52u:0x53u;service.command=24u;service.status=2;
    service.initialized=1;service.diag.last_result=which==2u?2:-1;
    kui_retail_hook_active=which==2u;kui_retail_hook_fault=0;
    uncached[0]=0x53;uncached[1]=24;uncached[2]=2;uncached[3]=which==2u;
    if(which==3u) {
        /* Authored FN6 failure, not the owner's unknown packet values. */
        kui_native_ocram_trace.entry[1]=0x11au;
        kui_native_ocram_trace.entry[2]=0x088afe94u;
        kui_native_ocram_trace.entry[3]=0x0c70e001u;
        kui_native_ocram_trace.entry[4]=KUI_GD_DMA_TRANSFER;
        kui_native_ocram_trace.entry[11]=0x78004001u;
        kui_native_ocram_trace.entry[14]=0x1000u;
        kui_native_ocram_trace.entry[15]=0x109u;
        service.token=0x11au;service.command=38u;service.status=3;
        service.pending=1;service.request_bytes=0x3000u;
        service.completed_bytes=0x1000u;service.sector_bytes=2048u;
        uncached[0]=0x11au;uncached[1]=38u;uncached[2]=3;
    }
    for(unsigned i=0;i<14u;i++)display.regs[i]=0x1200u+4u*i;
}

#ifdef KUI_OC_DIAG_RENDER
#if KUI_NATIVE_GD_DMA_STREAM && defined(KUI_RETAIL_ASYNC)
unsigned row;
#endif
#include "../src/loader/retail_display.c"
#else
struct values_row { char legend[50];uint32_t words[5];unsigned count; };
static struct values_row value_rows[11];
static unsigned value_count,line_count,restores;
void retail_display_line(const char *text) {
    assert(strlen(text)<=49u);line_count++;
}
void retail_display_values(const char *legend,const uint32_t *values,unsigned count) {
    assert(value_count<11u && count<=5u);
    struct values_row *out=&value_rows[value_count++];
    assert(strlen(legend)<sizeof(out->legend));strcpy(out->legend,legend);
    out->count=count;for(unsigned i=0;i<count;i++)out->words[i]=((const volatile uint32_t *)values)[i];
    if(*legend)retail_display_line(legend);
    char rendered[50];for(unsigned i=0;i<count;i++)
        (void)snprintf(rendered+i*9u,10u,"%08X ",out->words[i]);
    rendered[count*9u]=0;retail_display_line(rendered);
}
void retail_display_restore(const struct retail_display_state *state) {
    assert(state==&display);restores++;
    /* A render-time change must not alter either captured service view. */
    memset(&service,0xcc,sizeof(service));
    for(unsigned i=0;i<4u;i++)uncached[i]=0xddddddddu;
    kui_retail_hook_active=0xeeeeeeeeu;kui_retail_hook_fault=0xffffffffu;
}
#endif
#include "../src/loader/native_ocram_diagnostic.inc"

#ifndef KUI_OC_DIAG_RENDER
static void expect(unsigned index,const uint32_t *words,unsigned count) {
    assert(value_rows[index].count==count);
    assert(!memcmp(value_rows[index].words,words,count*sizeof(*words)));
}
int main(void) {
    for(unsigned which=0;which<3u;which++) {
        fixture(which);value_count=line_count=restores=0;
        uint32_t token=service.token;
        uint32_t history_before[4][5];
        for(unsigned i=0;i<4u;i++)for(unsigned j=0;j<5u;j++)
            history_before[i][j]=kui_native_ocram_trace.history[i][j];
        native_ocram_diagnostic_report("RET",0x8ce085e4u,0x7e000fd8u);
        assert(restores==1u && value_count==11u && line_count==20u);
        for(unsigned i=0;i<4u;i++) {
            expect(i,history_before[i],5);assert(!*value_rows[i].legend);
            if(i)for(unsigned j=0;j<5u;j++)assert(kui_native_ocram_trace.history[i][j]==history_before[i][j]);
        }
        const uint32_t first[]={0x31,1,0,which==2u?4u:UINT32_MAX,which==2u?2u:UINT32_MAX};expect(4,first,5);
        const uint32_t args[]={which==2u?0u:0x53u,0x7dfff000,0,0,which?0u:4u};expect(5,args,5);
        const uint32_t cpu[]={0x8ce085d8,0x7e000fd8,0x700000f1,0x8c000000,0};expect(6,cpu,5);
        const uint32_t locks[]={which?0x121u:0x101u,which==2u?0x105u:which?0x121u:0x101u,
            which==2u,which==2u,0};expect(7,locks,5);
        const uint32_t state[]={token,24,2,0,0};expect(8,state,5);
        const uint32_t progress[]={0,0,0,0};expect(9,progress,4);
        const uint32_t raw[]={0x53,24,2,which==2u};expect(10,raw,4);
        const char *const legends[]={"SEQ FN SRC ASM CORE","R4 R5 AUX VALUE WHY",
            "PR SP SR VBR MMU","CCR CB LOCKW LOCKN FAULT","TOKEN CMD STAT PEND ERR",
            "TOTAL DONE PIECE SECTOR","P2 TOKEN CMD STAT LOCK"};
        for(unsigned j=0;j<7u;j++)assert(!strcmp(value_rows[4u+j].legend,legends[j]));
        /* The final current-state row may reuse scratch after every original
         * history record has already been drawn. Its unused fifth word is
         * the preceding error word. */
        const uint32_t scratch[]={0x53,24,2,which==2u,0};
        for(unsigned j=0;j<5u;j++)assert(kui_native_ocram_trace.history[0][j]==scratch[j]);
        assert(memcmp(history_before[0],scratch,sizeof(scratch))!=0);
    }
    fixture(3);value_count=line_count=restores=0;
    native_ocram_diagnostic_report("GD STOP",0,0);
    assert(restores==1u && value_count==11u && line_count==20u);
    const uint32_t packet[]={0x11a,0x088afe94,0x0c70e001,0x1000,0x109};
    expect(5,packet,5);
    assert(!strcmp(value_rows[5].legend,"R4 R5 AUX VALUE WHY"));
    const uint32_t progress[]={0x3000,0x1000,0,2048};expect(9,progress,4);
    assert(!strcmp(value_rows[9].legend,"TOTAL DONE PIECE SECTOR"));
    const uint32_t active[]={0x11a,38,3,1,0};expect(8,active,5);
    puts("{\"status\":\"PASS\",\"cases\":4,\"value_rows\":11,\"text_rows\":20,\"snapshot_survives_render_mutation\":true,\"history_drawn_before_borrow\":true,\"early_busy_distinguishes_asm_and_core_result\":true,\"fn6_packet_and_progress_captured_before_render\":true}");
    return 0;
}
#else
int main(int argc,char **argv) {
    assert(argc==3);unsigned which=(unsigned)strtoul(argv[1],NULL,10);assert(which<4u);fixture(which);
    const uintptr_t fb=0xa5000000u,regs=0xa05f8000u;const size_t bytes=640u*480u*2u;
    long page=sysconf(_SC_PAGESIZE);assert(page==4096 && bytes%4096u==0);
    void *mapping=mmap((void *)(fb-4096u),bytes+8192u,PROT_NONE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0);
    assert(mapping==(void *)(fb-4096u));assert(!mprotect((void *)fb,bytes,PROT_READ|PROT_WRITE));
    assert(mmap((void *)regs,4096u,PROT_READ|PROT_WRITE,
        MAP_PRIVATE|MAP_ANONYMOUS|MAP_FIXED_NOREPLACE,-1,0)==(void *)regs);
    uint32_t original_history[4][5];
    for(unsigned i=0;i<4u;i++)for(unsigned j=0;j<5u;j++)
        original_history[i][j]=kui_native_ocram_trace.history[i][j];
    native_ocram_diagnostic_report("RET",0x8ce085e4u,0x7e000fd8u);
    assert(row==412u);
    volatile uint16_t *writable=(volatile uint16_t *)fb;
    /* Compare the already drawn history against fresh production renders of
     * the ORIGINAL records, after the trace's first row has been borrowed. */
    bool borrowed=false;
    for(unsigned j=0;j<5u;j++)borrowed|=kui_native_ocram_trace.history[0][j]!=original_history[0][j];
    assert(borrowed);
    for(unsigned i=0;i<4u;i++) {
        /* Each production terminal line has a fresh background after
         * restore. This comparison intentionally reuses one scratch row. */
        for(unsigned y=432u;y<450u;y++)for(unsigned x=0;x<640u;x++)
            writable[y*640u+x]=0x0864;
        row=432u;retail_display_values("",original_history[i],5);
        for(unsigned y=0;y<18u;y++)for(unsigned x=0;x<640u;x++)
            assert(writable[(88u+i*18u+y)*640u+x]==writable[(432u+y)*640u+x]);
    }
    for(unsigned y=432u;y<450u;y++)for(unsigned x=0;x<640u;x++)writable[y*640u+x]=0x0864;
    row=412u;
    unsigned bands[21]={0},pixels=0;
    volatile const uint16_t *frame=(const volatile uint16_t *)fb;
    for(unsigned y=0;y<480u;y++)for(unsigned x=0;x<640u;x++)if(frame[y*640u+x]==0xffffu) {
        assert(x>=20u && x<612u);pixels++;
        if(y>=20u && y<34u)bands[0]++;
        else { assert(y>=52u && y<412u && (y-52u)%18u<14u);bands[1u+(y-52u)/18u]++; }
    }
    for(unsigned i=0;i<21u;i++)assert(bands[i]>0);
    for(unsigned i=0;i<14u;i++)assert(*reg(i)==display.regs[i]);
    FILE *out=fopen(argv[2],"wb");assert(out);
    for(unsigned i=0;i<640u*480u;i++){uint16_t pixel=frame[i];assert(fwrite(&pixel,2,1,out)==1);}
    assert(!fclose(out));assert(!munmap(mapping,bytes+8192u));assert(!munmap((void *)regs,4096u));
    printf("{\"status\":\"PASS\",\"fixture\":%u,\"rendered_bands\":21,\"white_pixels\":%u,\"last_row_top\":394,\"glyph_bottom\":407,\"row_pitch\":18,\"guarded_framebuffer\":true,\"history_pixels_preserve_original\":true}\n",which,pixels);
    return 0;
}
#endif
