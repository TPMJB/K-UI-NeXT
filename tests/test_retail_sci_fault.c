/* SPDX-License-Identifier: GPL-3.0-only */
/* Compile the actual terminal include; destroy live request state when video
 * is claimed to prove snapshot lifetime and cache-buffer separation. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define KUI_RETAIL_OBSERVE_HOST_TEST 1
#define KUI_RETAIL_SCI_DIAGNOSTIC 1
#define KUI_SCI_SD_TEST 1
#define KUI_RETAIL_LOW_RESIDENT 1
#include "kui/retail_loader_layout.h"
#include "kui/retail_gd.h"
#include "kui/retail_image.h"
#include "retail_display.h"
#include "sci_sd_bus.h"
static struct kui_retail_gd service;
static struct kui_retail_image image;
static struct retail_display_state display;
static uint32_t kui_retail_hook_fault;
static enum kui_game_result image_result;
static enum kui_loader_sd_result card_result,observe_read_card_result,observe_stop_result;
static uint32_t observe_read_lba,observe_read_count,observe_card_lba;
static struct kui_sci_sd_diagnostic diagnostic;
const struct kui_sci_sd_diagnostic *kui_sci_sd_diagnostic_get(void) {return &diagnostic;}
static uint32_t guards[4],expected[41];
static unsigned restores,pauses,rows,next_row,guard_reads,titles;
static bool value_pending;
static const char *const legends[]={
    "FN ARG FLAG G BAD","CMD LBA N BPS","IO LBA N DONE STEP","IMG PRE STOP SD","DST BLK ERR CARD",
    "PHASE REASON EXPECT POLLS","SSR SCR SMR BRR","CHCR1 TCR1 DMAOR CHCR2 TCR2",
    "DMA START OK FALLBACK","CALL PLAY20 PLAY21 BAD"
};
static const unsigned counts[]={5,4,4,4,4,4,4,5,3,4};
static const unsigned offsets[]={0,5,9,13,17,21,25,29,34,37};
#include "../src/loader/retail_sci_observe.inc"
uint32_t kui_retail_observe_host_read(uint32_t address,unsigned width) {
    assert(address>=KUI_RETAIL_HOOK_STACK_BOTTOM && address<KUI_RETAIL_HOOK_STACK_BOTTOM+16u);
    assert(width==4u && !(address&3u));++guard_reads;
    return guards[(address-KUI_RETAIL_HOOK_STACK_BOTTOM)/4u];
}
void retail_display_restore(const struct retail_display_state *state) {
    assert(state==&display);++restores;
    memset(&service,0,sizeof(service));image.blocks_read=0u;
    image_result=KUI_GAME_OK;card_result=observe_read_card_result=observe_stop_result=KUI_LOADER_SD_OK;
    observe_read_lba=observe_read_count=observe_card_lba=0u;memset(guards,0,sizeof(guards));
}
void retail_display_pause(uint32_t frames) {assert(frames==1200u);++pauses;}
void retail_display_line(const char *line) {
    assert(line);
    if(value_pending) {
        uint32_t actual[5];int n=sscanf(line,"%x %x %x %x %x",actual,actual+1,actual+2,actual+3,actual+4);
        assert(n==(int)counts[next_row]);
        for(unsigned i=0;i<counts[next_row];i++) assert(actual[i]==expected[offsets[next_row]+i]);
        value_pending=false;++rows;++next_row;return;
    }
    if(next_row<10u && !strcmp(line,legends[next_row])) value_pending=true;
    if(!strcmp(line,OBSERVE_TITLE)) ++titles;
    assert(strcmp(line,"AUDIO NOT SAMPLED") && strcmp(line,"CPU TMU"));
}
static void fault_case(unsigned kind) {
    memset(image.block,0x5a,sizeof(image.block));
    for(unsigned i=0;i<4u;i++) guards[i]=0x4b554947u;
    if(kind==4u) guards[3]=0x11112222u;
    if(kind==5u) {guards[0]=0x33334444u;guards[2]=0x55556666u;}
    service.command=KUI_GD_DMAREAD;service.lba=45123u;service.count=23u;
    service.sector_bytes=2048u;service.completed_bytes=6144u;service.step=4u;
    service.destination=0x8c500000u;service.error=KUI_GD_ERROR_IO;
    observe_read_lba=45126u;observe_read_count=2u;observe_card_lba=912345u;image.blocks_read=117u;
    image_result=kind==0u?(enum kui_game_result)UINT32_MAX:kind==1u?KUI_GAME_MODE:kind==2u?KUI_GAME_IO:KUI_GAME_OK;
    observe_read_card_result=kind==0u?KUI_LOADER_SD_NOT_READY:kind==2u?KUI_LOADER_SD_TOKEN:KUI_LOADER_SD_OK;
    observe_stop_result=kind==0u?(enum kui_loader_sd_result)UINT32_MAX:kind==3u?KUI_LOADER_SD_COMMAND:KUI_LOADER_SD_OK;
    card_result=kind==0u?KUI_LOADER_SD_NOT_READY:kind==2u?KUI_LOADER_SD_TOKEN:kind==3u?KUI_LOADER_SD_COMMAND:KUI_LOADER_SD_OK;
    if(kind==0u) observe_card_lba=UINT32_MAX;
    expected[0]=KUI_GD_EXEC;expected[1]=0u;expected[2]=0u;expected[3]=guards[0];
    expected[4]=kind==4u?8u:kind==5u?5u:0u;
    expected[5]=KUI_GD_DMAREAD;expected[6]=45123u;expected[7]=23u;expected[8]=2048u;
    expected[9]=45126u;expected[10]=2u;expected[11]=6144u;expected[12]=4u;
    expected[13]=(uint32_t)image_result;expected[14]=(uint32_t)observe_read_card_result;
    expected[15]=(uint32_t)observe_stop_result;expected[16]=(uint32_t)card_result;
    expected[17]=0x8c500000u;expected[18]=117u;expected[19]=KUI_GD_ERROR_IO;expected[20]=observe_card_lba;
    memcpy(expected+21,&diagnostic,sizeof(diagnostic));
    expected[37]=observe_calls;expected[38]=observe_play[0];expected[39]=observe_play[1];expected[40]=0u;
    restores=pauses=rows=next_row=guard_reads=titles=0u;value_pending=false;
    observe_fault_report("IMAGE READ FAILED",KUI_GD_EXEC,0u);
    assert(!value_pending && rows==10u && restores==2u && pauses==1u && guard_reads==4u && titles==1u);
    assert(!memcmp(image.block+64u,expected,21u*4u));
    assert(!memcmp(image.block+160u,expected+21u,20u*4u));
    for(unsigned i=148u;i<160u;i++) assert(image.block[i]==0x5au);
    for(unsigned i=240u;i<sizeof(image.block);i++) assert(image.block[i]==0x5au);
}
int main(void) {
    diagnostic=(struct kui_sci_sd_diagnostic){2,2,128,17,0xa0,0x70,0x80,0,0x4911,375,0x301,0x12c0,128,7,6,2};
    for(unsigned i=0;i<258u;i++) observe_call();
    assert(observe_calls==258u && !guard_reads);
    service.command=20u;observe_result(KUI_GD_REQUEST,1);
    service.command=21u;observe_result(KUI_GD_REQUEST,-1);observe_result(KUI_GD_REQUEST,2);
    assert(observe_play[0]==1u && observe_play[1]==1u);
    for(unsigned kind=0;kind<6u;kind++) fault_case(kind);
    diagnostic.phase=0;memcpy(expected+21,&diagnostic,sizeof(diagnostic));
    rows=titles=0u;next_row=5u;observe_report();
    assert(rows==5u && titles==1u && !value_pending);
    puts("SCI actual terminal include: six guard/request cases, cache lifetime, normal report and no live MMIO PASS");
    return 0;
}
