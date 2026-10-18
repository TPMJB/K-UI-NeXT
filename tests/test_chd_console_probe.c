/* SPDX-License-Identifier: GPL-3.0-only
 * Runs the exact console test C against an independent host GD-vector adapter
 * reading the generated raw files. This validates fixture expectations and
 * completion handling; it does not emulate the SH4 or validate hardware I/O.
 */
#include "kui/gd_service.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

uint16_t kui_chd_console_frame[640u*480u];
static FILE *tracks[3];
static uint32_t pending[4],command,token,bytes,state,sector_bytes=2048;
static unsigned passes,failures,summary,inject;
extern void kui_chd_console_main(void);

void kui_shell_font_draw(uint16_t *frame,int x,int y,uint16_t color,const char *text,bool large) {
    (void)frame;(void)x;(void)y;(void)color;(void)large;
    if(!strncmp(text,"PASS  ",6))passes++;
    if(!strncmp(text,"FAIL  ",6))failures++;
    if(strstr(text,"CHD READ TEST PASSED"))summary=1;
    puts(text);
}
static int read_sector(uint32_t lba,uint8_t *out) {
    unsigned track;uint32_t at;
    if(lba<32u) {track=0;at=lba;}
    else if(lba<64u) {track=1;at=lba-32u;}
    else if(lba>=45000u && lba<45512u) {track=2;at=lba-45000u;}
    else return 0;
    uint8_t raw[2352];
    if(fseek(tracks[track],(long)at*2352,SEEK_SET) || fread(raw,1,sizeof(raw),tracks[track])!=sizeof(raw))return 0;
    if(sector_bytes==2048u) {
        if(track==1)return 0;
        memcpy(out,raw+16,2048);
    } else memcpy(out,raw,2352);
    if(inject && lba==45128u)out[17]^=1;
    return 1;
}
int32_t kui_chd_console_gd_call(uint32_t function,uintptr_t arg0,uintptr_t arg1) {
    if(function==KUI_GD_DATATYPE) {sector_bytes=((uint32_t *)arg0)[3];return 0;}
    if(function==KUI_GD_REQUEST) {
        if(command)return 0;
        memcpy(pending,(const void *)arg1,sizeof(pending));command=(uint32_t)arg0;
        state=KUI_GD_PROCESSING;bytes=0;return (int32_t)++token;
    }
    if(function==KUI_GD_EXEC) {
        uint8_t *out=(uint8_t *)(uintptr_t)pending[command==KUI_GD_GETTOC2?1:2];
        if(command==KUI_GD_GETTOC2) {
            uint32_t *toc=(uint32_t *)out;
            for(unsigned i=0;i<102;i++)toc[i]=UINT32_MAX;
            if(pending[0]) {
                toc[2]=0x41000000u|45150u;toc[99]=toc[100]=0x41030000u;toc[101]=0x41000000u|45662u;
            } else {
                toc[0]=0x41000000u|150u;toc[1]=0x01000000u|182u;
                toc[99]=0x41010000u;toc[100]=0x01020000u;toc[101]=0x01000000u|214u;
            }
            bytes=408;
        } else {
            for(unsigned n=0;n<pending[1];n++) {
                if(!read_sector(pending[0]-150u+n,out+bytes)) {state=(uint32_t)KUI_GD_FAILED;return 0;}
                bytes+=sector_bytes;
            }
        }
        state=KUI_GD_COMPLETED;return 0;
    }
    if(function==KUI_GD_CHECK) {
        uint32_t *status=(uint32_t *)arg1;
        status[0]=state==(uint32_t)KUI_GD_FAILED;status[1]=status[0];status[2]=bytes;status[3]=state==KUI_GD_PROCESSING?4:0;
        if(state!=KUI_GD_PROCESSING)command=0;
        return (int32_t)state;
    }
    if(function==KUI_GD_ABORT) {command=0;state=(uint32_t)KUI_GD_FAILED;return 0;}
    return -1;
}
int main(int argc,char **argv) {
    if(argc<2 || argc>3)return 2;
    static const char *const names[]={"track01.bin","track02.raw","track03.bin"};
    for(unsigned i=0;i<3;i++) {
        char path[4096];int n=snprintf(path,sizeof(path),"%s/%s",argv[1],names[i]);
        if(n<0 || (size_t)n>=sizeof(path) || !(tracks[i]=fopen(path,"rb")))return 2;
    }
    inject=argc==3;kui_chd_console_main();
    for(unsigned i=0;i<3;i++)fclose(tracks[i]);
    if(inject)return failures && !summary?0:1;
    return passes==9 && !failures && summary?0:1;
}
