/* SPDX-License-Identifier: GPL-3.0-only */
/* Host preview uses the same embedded artwork/font and renderer as hardware.
 * Modes: home, ripper, confirm, settings, diagnostics, complete, stopped. */
#include "kui/shell.h"
#include <stdio.h>
#include <string.h>

static uint16_t frame[640*480];
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    struct kui_settings preferences={true,false,true};
    struct kui_shell shell; kui_shell_init(&shell,&preferences);
    const char *logs[]={"SD exFAT, 249997312 sectors, cluster=131072 bytes",
        "Volume start=2048 (MBR)","Disc: MDK2", "Track 04: audio",
        "Checkpoint saved. Partial job preserved.","Capture result: stopped",
        "Report saved: /KUI/probes/p0012/diagnostics.txt"};
    struct kui_shell_view view={.build="a1b2c3d4e5f6",.phase=2,.track=4,.tracks=31,
        .rate_kib=1012,.done=421ull*1048576,.total=1133ull*1048576,
        .committed=421ull*1048576,.elapsed_ms=426000,
        .memory_valid=true,.memory_used=2800*1024,.memory_physical=16384*1024,
        .memory_peak=2816*1024,.log_lines=logs,.log_count=7,.total_log_lines=174,
        .job_dir="/KUI/dumps/df838eac34967ae16-0002"};
    if(!strcmp(argv[1],"settings")) shell.page=KUI_SHELL_SETTINGS;
    else if(!strcmp(argv[1],"diagnostics")) shell.page=KUI_SHELL_DIAGNOSTICS;
    else if(strcmp(argv[1],"home")) {
        shell.page=KUI_SHELL_RIPPER;
        if(!strcmp(argv[1],"confirm")) shell.confirm_new=true;
        else if(!strcmp(argv[1],"complete")) {
            view.phase=4; view.outcome=KUI_SHELL_OUTCOME_COMPLETE;
            view.done=view.total; view.committed=view.total;
        } else if(!strcmp(argv[1],"stopped")) view.outcome=KUI_SHELL_OUTCOME_STOPPED;
        else view.busy=true;
    }
    kui_shell_draw(frame,&shell,&view,NULL,NULL);
    FILE *out=fopen(argv[2],"wb"); if(!out) return 1;
    if(fprintf(out,"P6\n640 480\n255\n")<0) return 1;
    for(unsigned i=0;i<640*480;i++) {
        unsigned v=frame[i];
        unsigned char rgb[3]={(unsigned char)(((v>>11)&31)*255/31),
            (unsigned char)(((v>>5)&63)*255/63),(unsigned char)((v&31)*255/31)};
        if(fwrite(rgb,1,3,out)!=3) return 1;
    }
    return fclose(out)!=0;
}
