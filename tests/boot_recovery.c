/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/boot_image.h"
#include "kui/media.h"
#include "kui/storage_policy.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static FILE *disk;
static uint64_t capacity, forbidden_first, forbidden_count;
static unsigned reads, writes, syncs, polls;
static bool loading, cancel_load, fail_reads, fail_once, fault_injected;
static uint64_t blocks(void *ctx) { (void)ctx; return capacity; }
static int read_blocks(void *ctx,uint32_t block,size_t count,uint8_t *out) {
    (void)ctx;
    assert(count && block < capacity && count <= capacity-block);
    /* Reading ext4 data is forbidden in split-card boot/recovery tests. */
    assert(!forbidden_count || (uint64_t)block+count <= forbidden_first ||
           block >= forbidden_first+forbidden_count);
    if(loading && fail_reads && ++reads >= 8) return -1;
    if(fseeko(disk,(off_t)block*512,SEEK_SET) || fread(out,512,count,disk)!=count) return -1;
    if(loading && fail_once && !fault_injected && !memcmp(out,"aaaaaaaa",8)) {
        fault_injected=true; return -1; /* known primary payload, after its header */
    }
    return 0;
}
static int write_blocks(void *ctx,uint32_t block,size_t count,const uint8_t *data) {
    (void)ctx; ++writes; assert(!loading);
    return fseeko(disk,(off_t)block*512,SEEK_SET) || fwrite(data,512,count,disk)!=count ? -1:0;
}
static int sync_blocks(void *ctx) { (void)ctx; ++syncs; assert(!loading); return fflush(disk); }
static void log_line(const char *format,...) {
    va_list ap; va_start(ap,format); vprintf(format,ap); va_end(ap); putchar('\n');
}
static bool cancelled(void) { return cancel_load && ++polls >= 3; }
static void seed_file(const char *name,const char *source) {
    if(!strcmp(source,"-")) return;
    FILE *in=fopen(source,"rb"); assert(in);
    FIL out; assert(f_open(&out,name,FA_WRITE|FA_CREATE_ALWAYS)==FR_OK);
    uint8_t data[32768]; size_t count;
    while((count=fread(data,1,sizeof(data),in))) {
        UINT done=0; assert(f_write(&out,data,(UINT)count,&done)==FR_OK && done==count);
    }
    assert(!ferror(in)); assert(!fclose(in)); assert(f_close(&out)==FR_OK);
}
int main(int argc,char **argv) {
    setvbuf(stdout,NULL,_IONBF,0);
    assert(argc==5 || argc==9);
    struct stat st; assert(!lstat(argv[1],&st) && S_ISREG(st.st_mode) && st.st_size%512==0);
    bool seed=!strcmp(argv[2],"seed");
    disk=fopen(argv[1],seed?"r+b":"rb"); assert(disk); capacity=(uint64_t)st.st_size/512;
    const struct kui_media_ops media={NULL,blocks,read_blocks,write_blocks,sync_blocks};
    if(seed) {
        assert(argc==5); kui_media_set(&media); FATFS fs; assert(kui_mount(&fs,log_line));
        FRESULT r=f_mkdir("0:/KUI"); assert(r==FR_OK || r==FR_EXIST);
        seed_file("0:/KUI/runtime.kui",argv[3]); seed_file("0:/KUI/recovery.kui",argv[4]);
        assert(f_mount(NULL,"0:",0)==FR_OK); assert(!fclose(disk)); return 0;
    }
    assert(argc==9); loading=true;
    unsigned transport=(unsigned)strtoul(argv[4],NULL,10);
    forbidden_first=strtoull(argv[5],NULL,10); forbidden_count=strtoull(argv[6],NULL,10);
    cancel_load=!strcmp(argv[7],"cancel"); fail_reads=!strcmp(argv[7],"io");
    fail_once=!strcmp(argv[7],"io-once");
    unsigned repeats=(unsigned)strtoul(argv[8],NULL,10); assert(repeats && repeats<=2);
    for(unsigned n=0;n<repeats;++n) {
        reads=polls=0; fault_injected=false; struct kui_runtime_image image={0};
        enum kui_runtime_result result=kui_boot_image_read(&media,transport,
            !strcmp(argv[3],"recovery"),&image,log_line,cancelled);
        if(!strcmp(argv[2],"fail") || !strcmp(argv[2],"cancel")) {
            assert(result!=KUI_RUNTIME_OK && !image.data && !image.info.payload_bytes);
            if(!strcmp(argv[2],"cancel")) assert(result==KUI_RUNTIME_CANCELLED);
        } else {
            assert(result==KUI_RUNTIME_OK && image.data && image.info.payload_bytes==65536);
            assert(!strcmp(image.info.build,!strcmp(argv[2],"runtime")?"111111111111":"222222222222"));
            struct kui_storage_boot_marker marker;
            memcpy(&marker,(uint8_t *)image.data+32,sizeof(marker));
            assert(kui_storage_boot_transport(&marker)==transport);
        }
        kui_runtime_free(&image); assert(!writes && !syncs);
        if(fail_once) assert(fault_injected);
        assert(kui_media_volume()->count==0); /* temporary view was released */
    }
    assert(!fclose(disk)); printf("PASS boot recovery: %s/%s, zero writes/syncs\n",argv[2],argv[3]);
    return 0;
}
