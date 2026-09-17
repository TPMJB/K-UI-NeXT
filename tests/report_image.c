/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/report.h"
#include "kui/media.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static struct {
    FILE *image;uint64_t blocks;unsigned writes,checks;
    const char *fault;bool payload_seen,injected;
} test;
static char payload[60000];
static const char sentinel[]="Existing capture/checkpoint must remain unchanged.\n";
static const char *kept[]={"0:/KUI/dumps/keep/track03.bin","0:/KUI/dumps/keep/checkpoint-a.bin"};
static bool fault(const char *s) {return test.fault && !strcmp(test.fault,s);}
static void log_line(const char *format,...) {
    va_list args;va_start(args,format);vprintf(format,args);va_end(args);puts("");
}
static bool cancelled(void) {
    ++test.checks;
    return fault("cancel-before") || (fault("cancel-during") && test.checks>=6) ||
        (fault("cancel-publish") && test.checks>=4+(sizeof(payload)+4095)/4096);
}
static uint64_t blocks(void *p) {(void)p;return test.blocks;}
static int read_image(void *p,uint32_t block,size_t count,uint8_t *data) {
    (void)p;
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fread(data,512,count,test.image)!=count?-1:0;
}
static int write_image(void *p,uint32_t block,size_t count,const uint8_t *data) {
    (void)p;++test.writes;
    if(count && !memcmp(data,payload,32)) {
        test.payload_seen=true;
        if(fault("write-fail")) {test.injected=true;return -1;}
    }
    return fseeko(test.image,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,test.image)!=count?-1:0;
}
static int sync_image(void *p) {
    (void)p;
    if(fault("sync-fail") && test.payload_seen) {test.injected=true;return -1;}
    return fflush(test.image) || fsync(fileno(test.image))?-1:0;
}
static void check_file(const char *path,const void *expected,size_t bytes) {
    FIL file;assert(f_open(&file,path,FA_READ)==FR_OK && f_size(&file)==bytes);
    uint8_t data[4096];size_t at=0;
    while(at<bytes) {
        UINT n=bytes-at>sizeof(data)?sizeof(data):(UINT)(bytes-at),got;
        assert(f_read(&file,data,n,&got)==FR_OK && got==n);
        assert(!memcmp(data,(const uint8_t *)expected+at,n));at+=n;
    }
    assert(f_close(&file)==FR_OK);
}
static void fill_card(void) {
    FATFS fs,*mounted;DWORD available;FIL file;
    assert(kui_mount(&fs,log_line));
    assert(f_open(&file,"0:/fill.bin",FA_WRITE|FA_CREATE_NEW)==FR_OK);
    assert(f_getfree("0:",&available,&mounted)==FR_OK && available>1);
    uint64_t bytes=(uint64_t)(available-1)*mounted->csize*512;
    static const uint8_t zero[65536]={0};
    while(bytes) {
        UINT n=bytes>sizeof(zero)?sizeof(zero):(UINT)bytes,done;
        assert(f_write(&file,zero,n,&done)==FR_OK && done==n);bytes-=done;
    }
    assert(f_close(&file)==FR_OK && f_mount(NULL,"0:",0)==FR_OK);
}
int main(int argc,char **argv) {
    if(argc!=3) return 2;
    struct stat st;if(lstat(argv[1],&st) || !S_ISREG(st.st_mode) || st.st_size%512) return 2;
    test.image=fopen(argv[1],"r+b");if(!test.image) return 2;test.blocks=(uint64_t)st.st_size/512;
    memset(payload,'R',sizeof(payload));memcpy(payload,"K-UI automatic report test fixture\n",35);
    struct kui_media_ops media={NULL,blocks,read_image,write_image,sync_image};kui_media_set(&media);
    bool seed=!strcmp(argv[2],"seed");
    if(seed) {
        FATFS fs;assert(kui_mount(&fs,log_line));
        assert(f_mkdir("0:/KUI")==FR_OK && f_mkdir("0:/KUI/dumps")==FR_OK && f_mkdir("0:/KUI/dumps/keep")==FR_OK);
        for(unsigned i=0;i<2;i++) assert(kui_write_new_file(kept[i],sentinel,sizeof(sentinel),log_line));
        assert(f_mount(NULL,"0:",0)==FR_OK);
    }
    if(!strcmp(argv[2],"full")) fill_card();
    test.fault=argv[2];test.payload_seen=false;test.writes=0;
    char path[96];enum kui_report_result result=kui_report_save(payload,sizeof(payload),path,log_line,cancelled);
    unsigned writes=test.writes;
    bool stop=fault("cancel-before") || fault("cancel-during") || fault("cancel-publish");
    bool failure=fault("write-fail") || fault("sync-fail") || fault("full");
    assert(result==(stop?KUI_REPORT_STOPPED:failure?KUI_REPORT_FAILED:KUI_REPORT_SAVED));
    if(fault("write-fail") || fault("sync-fail")) assert(test.injected);
    if(fault("cancel-before")) assert(!writes && !path[0]);
    if(fault("cancel-during") || fault("cancel-publish")) assert(writes);
    /* Inspect after a fresh connection; the production adapter deliberately
     * latches a failed medium until reconnect, even when the injected fault ends. */
    test.fault=NULL;kui_media_set(&media);FATFS fs;assert(kui_mount(&fs,log_line));
    for(unsigned i=0;i<2;i++) check_file(kept[i],sentinel,sizeof(sentinel));
    check_file("0:/KUI/probes/p0001/diagnostics.txt",payload,sizeof(payload));
    if(result==KUI_REPORT_SAVED) {
        assert(!strcmp(path,seed?"0:/KUI/probes/p0001/diagnostics.txt":"0:/KUI/probes/p0002/diagnostics.txt"));
        check_file(path,payload,sizeof(payload));
    } else if(path[0]) {
        FILINFO info;assert(f_stat(path,&info)==FR_NO_FILE);
    }
    assert(f_mount(NULL,"0:",0)==FR_OK && fclose(test.image)==0);
    printf("PASS report %s: result=%u writes=%u; old report and capture files preserved\n",argv[2],result,writes);
    return 0;
}
