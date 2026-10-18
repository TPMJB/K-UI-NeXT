/* SPDX-License-Identifier: GPL-3.0-only
 * Original executable for generated CHD tests. After the normal launch handoff
 * it uses only the GD BIOS vector and its own framebuffer/font. No KOS, game
 * code, loader-private function, filesystem or write operation is called.
 */
#include "kui/gd_service.h"
#include "kui/shell_font.h"
#include <stdint.h>
#include <stddef.h>

#ifndef KUI_BUILD_ID
#define KUI_BUILD_ID "local"
#endif
#define DATA 2048u
#define RAW 2352u
#define SECTORS 16u
static uint8_t output[SECTORS*RAW+32u] __attribute__((aligned(32)));
static uint32_t parameters[4],status[4],mode[4],toc[102];
static uint32_t failures,detail,row;
#ifdef KUI_CHD_CONSOLE_HOST
extern uint16_t kui_chd_console_frame[640u*480u];
static uint16_t *const frame=kui_chd_console_frame;
#else
static uint16_t *const frame=(uint16_t *)(uintptr_t)0xa5000000u;
#endif
extern int32_t kui_chd_console_gd_call(uint32_t,uintptr_t,uintptr_t);

static uint8_t pattern(uint32_t lba,uint32_t at) {
    return (uint8_t)((lba*37u)^(at*13u)^(at>>8)^0xa5u);
}
static uint8_t audio_pattern(uint32_t sector,uint32_t at) {
    return (uint8_t)((sector*29u)+(at*11u)+((at>>8)*3u)+0x5bu);
}
static uint8_t bcd(uint32_t n) { return (uint8_t)((n/10u)*16u+n%10u); }
static uint8_t raw_pattern(uint32_t lba,uint32_t at) {
    if(lba>=32u && lba<64u)return audio_pattern(lba-32u,at);
    if(at==0 || at==11)return 0;
    if(at<11)return 255;
    uint32_t fad=lba+150u;
    if(at==12)return bcd(fad/4500u);
    if(at==13)return bcd((fad/75u)%60u);
    if(at==14)return bcd(fad%75u);
    if(at==15)return 1;
    if(at<2064u)return pattern(lba,at-16u);
    return 0;
}
static void text(uint32_t y,uint16_t color,const char *line,int large) {
    kui_shell_font_draw(frame,20,(int)y,color,line,large!=0);
}
static void hex(char out[9],uint32_t value) {
    static const char digits[]="0123456789ABCDEF";
    for(unsigned n=0;n<8;n++)out[n]=digits[(value>>(28u-n*4u))&15u];
    out[8]=0;
}
static void result(const char *label,int passed) {
    char line[96];const char *prefix=passed?"PASS  ":"FAIL  ";unsigned n=0;
    while(*prefix)line[n++]=*prefix++;
    while(*label && n<75u)line[n++]=*label++;
    line[n]=0;text(row,passed?0x87f0:0xfba0,line,0);row+=26u;
    if(!passed)failures++;
}
static int command(uint32_t cmd,uint32_t bytes) {
    int32_t token=kui_chd_console_gd_call(KUI_GD_REQUEST,cmd,(uintptr_t)parameters);
    if(token<=0) {detail=0x10000000u|cmd;return 0;}
    /* The Standard reader advances synchronously in bounded server steps.
     * No callbacks or guest interrupts are needed. A bound prevents a stuck
     * token from looking like a permanently busy test. */
    for(unsigned step=0;step<4096u;step++) {
        if(kui_chd_console_gd_call(KUI_GD_EXEC,0,0)!=0) {detail=0x20000000u|cmd;return 0;}
        int32_t state=kui_chd_console_gd_call(KUI_GD_CHECK,(uintptr_t)token,(uintptr_t)status);
        if(state==KUI_GD_PROCESSING)continue;
        if(state==KUI_GD_COMPLETED && !status[0] && !status[1] && status[2]==bytes && !status[3])return 1;
        detail=0x30000000u|(status[1]&65535u);return 0;
    }
    (void)kui_chd_console_gd_call(KUI_GD_ABORT,(uintptr_t)token,0);
    detail=0x40000000u|cmd;return 0;
}
static int read_matches(uint32_t lba,uint32_t count,uint32_t bytes,uint32_t cmd) {
    mode[0]=0;mode[1]=bytes==RAW?0x1000u:0x2000u;mode[2]=bytes==RAW?0:1024u;mode[3]=bytes;
    if(kui_chd_console_gd_call(KUI_GD_DATATYPE,(uintptr_t)mode,0)!=0) {detail=0x50000000u;return 0;}
    for(unsigned n=0;n<sizeof(output);n++)output[n]=0x69;
    parameters[0]=lba+150u;parameters[1]=count;parameters[2]=(uint32_t)(uintptr_t)output;parameters[3]=0;
    if(!command(cmd,count*bytes))return 0;
    for(uint32_t sector=0;sector<count;sector++)
        for(uint32_t at=0;at<bytes;at++) {
            uint8_t wanted=bytes==RAW?raw_pattern(lba+sector,at):pattern(lba+sector,at);
            if(output[sector*bytes+at]!=wanted) {detail=((lba+sector)<<12)|at;return 0;}
        }
    for(uint32_t at=count*bytes;at<sizeof(output);at++)
        if(output[at]!=0x69) {detail=0x60000000u|at;return 0;}
    return 1;
}
static int toc_matches(uint32_t area) {
    parameters[0]=area;parameters[1]=(uint32_t)(uintptr_t)toc;parameters[2]=parameters[3]=0;
    if(!command(KUI_GD_GETTOC2,sizeof(toc)))return 0;
    if(area)return toc[2]==0x4100b05eu && toc[101]==(0x41000000u|45662u);
    return toc[0]==(0x41000000u|150u) && toc[1]==(0x01000000u|182u);
}
void kui_chd_console_main(void) {
    for(unsigned n=0;n<640u*480u;n++)frame[n]=0x0864u;
    text(18,0x7fff,"K-UI CHD console read test",1);
    text(52,0xffff,"Original fixture v1 / Build " KUI_BUILD_ID,0);
    text(78,0xffff,"Detached reader / no KOS / SD read only",0);row=118;
    result("Low + high density TOC",toc_matches(0) && toc_matches(1));
    result("Low density cooked reads",read_matches(0,2,DATA,KUI_GD_PIOREAD));
    result("High density cooked reads",read_matches(45128,1,DATA,KUI_GD_PIOREAD));
    result("Sixteen-sector boundary read",read_matches(45135,16,DATA,KUI_GD_PIOREAD));
    result("GD DMA command read",read_matches(45301,7,DATA,KUI_GD_DMAREAD));
    result("Random backwards + tail reads",read_matches(45500,3,DATA,KUI_GD_PIOREAD) &&
        read_matches(45191,7,DATA,KUI_GD_PIOREAD) && read_matches(45128,1,DATA,KUI_GD_PIOREAD));
    result("Raw data bytes + framing",read_matches(45199,3,RAW,KUI_GD_PIOREAD));
    result("Audio bytes + sample byte order",read_matches(35,7,RAW,KUI_GD_PIOREAD));
    result("Raw data/audio track boundary",read_matches(31,2,RAW,KUI_GD_PIOREAD));
    if(failures) {char digits[9];hex(digits,detail);text(373,0xffff,"Last failure detail:",0);text(395,0xfba0,digits,0);}
    text(425,failures?0xfba0:0x87f0,failures?"CHD READ TEST FAILED - photograph this screen":"CHD READ TEST PASSED - photograph this screen",0);
    text(450,0xffff,"Power off/on to return. Keep this test result with its filename.",0);
#ifndef KUI_CHD_CONSOLE_HOST
    for(;;)__asm__ volatile("nop");
#endif
}
