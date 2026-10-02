/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/boot_ui.h"
#include "kui/storage.h"
#include <stdio.h>
#include <string.h>
static uint16_t frame[640*480];
int main(int argc,char **argv) {
    struct kui_boot_ui ui; kui_boot_ui_init(&ui,0);
    struct kui_boot_view view={.worker_available=true,.build="8184fc1c1f78",
        .status="No boot image found. Insert a card, then try again."};
    const char *mode=argc>1?argv[1]:"home";
    if(!strcmp(mode,"countdown")) view.countdown=3;
    else if(!strcmp(mode,"recovery")) ui.selected=1;
    else if(!strcmp(mode,"tools")) ui.selected=2;
    else if(!strcmp(mode,"diagnostics")) ui.page=KUI_BOOT_DIAGNOSTICS;
    else if(!strcmp(mode,"help")) ui.page=KUI_BOOT_HELP;
    else if(!strcmp(mode,"confirm")) {ui.page=KUI_BOOT_CONFIRM;ui.confirm=KUI_BOOT_BENCH;ui.transport=KUI_STORAGE_SCI;}
    else if(!strcmp(mode,"working")) {view.busy=true;view.status="Loading runtime...";}
    else if(!strcmp(mode,"log")) {
        ui.page=KUI_BOOT_LOG;view.total_lines=36;view.line_count=8;
        const char *log[]={"K-UI CD boot: selected Auto transport",
            "SCIF: no card detected", "Trying SCI microSD", "Boot FAT partition: LBA 2048, 262144 sectors",
            "Reading 0:/KUI/runtime.kui", "Payload checksum mismatch", "Trying 0:/KUI/recovery.kui",
            "Recovery image validated. Starting SCI build 8184fc1c1f78"};
        for(unsigned i=0;i<8;i++) view.lines[i]=log[i];
    }
    kui_boot_ui_draw(frame,&ui,&view);
    printf("P6\n640 480\n255\n");
    for(unsigned i=0;i<640*480;i++) {
        unsigned p=frame[i];
        putchar((int)(((p>>11)&31)*255/31));putchar((int)(((p>>5)&63)*255/63));putchar((int)((p&31)*255/31));
    }
    return ferror(stdout)?1:0;
}
