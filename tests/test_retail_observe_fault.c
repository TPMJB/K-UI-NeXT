/* SPDX-License-Identifier: GPL-3.0-only */
/* Actual observer include: bounded MMIO inventory and terminal row lifetimes. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define KUI_RETAIL_OBSERVE_HOST_TEST 1
#define KUI_RETAIL_OBSERVE_AUDIO 0
#define KUI_RETAIL_LOW_RESIDENT 1
#include "kui/retail_loader_layout.h"
#include "kui/retail_gd.h"
#include "kui/retail_image.h"
#include "retail_display.h"
#include "sd_reader.h"
static struct kui_retail_gd service;
static struct kui_retail_image image;
static struct retail_display_state display;
static uint32_t kui_retail_hook_sr,kui_retail_hook_fault,kui_retail_native_caller[2];
static enum kui_game_result image_result;
static enum kui_loader_sd_result card_result,observe_read_card_result,observe_stop_result;
static uint32_t observe_read_lba,observe_read_count,observe_card_lba;
static uint32_t guards[4],expected[21];
static unsigned restores,pauses,rows,next_row,guard_reads,audio_labels,variant_titles;
static bool value_pending;
static const char *const legends[]={"FN ARG FLAG G BAD","CMD LBA N BPS",
    "IO LBA N DONE STEP","IMG PRE STOP SD","DST BLK ERR CARD"};
#include "../src/loader/retail_observe.inc"

uint32_t kui_retail_observe_host_cpu(unsigned index) {assert(index<2u);return 0x8c100000u+index*0x1000u;}
uint32_t kui_retail_observe_host_read(uint32_t address,unsigned width) {
    if(address>=KUI_RETAIL_HOOK_STACK_BOTTOM && address<KUI_RETAIL_HOOK_STACK_BOTTOM+16u) {
        assert(width==4u && !(address&3u));guard_reads++;
        return guards[(address-KUI_RETAIL_HOOK_STACK_BOTTOM)/4u];
    }
    assert((address==0xff000010u && width==4u) ||
        (address==0xffd80004u && width==1u) || (address==0xffc00000u && width==2u) ||
        (address>=0xffd80008u && address<=0xffd80028u && width==4u && !(address&3u)) ||
        ((address==0xffd80010u || address==0xffd8001cu || address==0xffd80028u) && width==2u));
    /* Any AICA/FIFO/G2 DMA sweep would fail the address allowlist above. */
    return address^width;
}
void retail_display_restore(const struct retail_display_state *state) {
    assert(state==&display);restores++;
    /* Destroy live sources on the first restore: snapshots must predate it. */
    memset(&service,0,sizeof(service));image.blocks_read=0u;
    image_result=KUI_GAME_OK;card_result=observe_read_card_result=observe_stop_result=KUI_LOADER_SD_OK;
    observe_read_lba=observe_read_count=observe_card_lba=0u;
    memset(guards,0,sizeof(guards));
}
void retail_display_pause(uint32_t frames) {assert(frames==1200u);pauses++;}
void retail_display_line(const char *line) {
    assert(line);
    if(value_pending) {
        uint32_t actual[5];unsigned count=next_row?4u:5u;
        int n=sscanf(line,"%x %x %x %x %x",actual,actual+1,actual+2,actual+3,actual+4);
        assert(n==(int)count);
        unsigned offset=next_row?5u+(next_row-1u)*4u:0u;
        for(unsigned i=0;i<count;i++) assert(actual[i]==expected[offset+i]);
        value_pending=false;rows++;next_row++;return;
    }
    if(next_row<5u && !strcmp(line,legends[next_row])) value_pending=true;
    if(!strcmp(line,"AUDIO NOT SAMPLED")) audio_labels++;
    if(!strcmp(line,OBSERVE_TITLE)) variant_titles++;
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
    restores=pauses=rows=next_row=guard_reads=audio_labels=variant_titles=0u;value_pending=false;
    observe_fault_report("IMAGE READ FAILED",KUI_GD_EXEC,0u);
    assert(!value_pending && rows==5u && restores==5u && pauses==4u && guard_reads==4u && audio_labels==2u);
    assert(variant_titles==4u);
    assert(!memcmp(image.block+64u,expected,sizeof(expected)));
    for(unsigned i=148u;i<sizeof(image.block);i++) assert(image.block[i]==0x5au);
}
int main(void) {
    kui_retail_hook_sr=0x40000000u;kui_retail_native_caller[0]=0x8c123456u;kui_retail_native_caller[1]=0x8c654320u;
    for(unsigned i=0;i<258u;i++) observe_call();
    assert(observe.calls==258u && observe.samples[0]==258u && observe.samples[1]==258u && !observe.samples[2]);
    service.command=20u;service.outputs[0]=4u;service.outputs[1]=5u;service.outputs[2]=15u;
    observe_result(KUI_GD_REQUEST,1);assert(observe.play[0]==1u && !memcmp(observe.first_play[0],service.outputs,12u));
    service.command=21u;service.outputs[0]=374351u;service.outputs[1]=377422u;service.outputs[2]=0u;
    observe_result(KUI_GD_REQUEST,2);assert(observe.play[1]==1u && !memcmp(observe.first_play[1],service.outputs,12u));
    for(unsigned kind=0;kind<6u;kind++) fault_case(kind);
    puts("PASS actual observer include: six fault rows/guard cases, 258 live calls, no AICA/G2 reads");
    return 0;
}
