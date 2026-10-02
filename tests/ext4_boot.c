/* SPDX-License-Identifier: GPL-3.0-only */
#define _POSIX_C_SOURCE 200809L
#include "kui/ext4_boot.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
static FILE *disk;
static uint64_t blocks;
static unsigned reads, writes, syncs, cancel_calls;
static bool cancel_test, io_test;
static char messages[4096];
static uint64_t capacity(void *ctx) { (void)ctx; return blocks; }
static int read_blocks(void *ctx, uint32_t lba, size_t count, uint8_t *data) {
    (void)ctx;
    assert(count && lba < blocks && count <= blocks - lba);
    if(io_test && reads++) return -1;
    if(!io_test) ++reads;
    return fseeko(disk,(off_t)lba*512,SEEK_SET) || fread(data,512,count,disk)!=count ? -1:0;
}
static int write_blocks(void *ctx,uint32_t lba,size_t count,const uint8_t *data) {
    (void)ctx;(void)lba;(void)count;(void)data;++writes;assert(!"ext4 boot wrote to media");return -1;
}
static int sync_blocks(void *ctx) { (void)ctx;++syncs;assert(!"ext4 boot requested media sync");return -1; }
static bool cancelled(void) { return cancel_test && ++cancel_calls>=8; }
static void log_line(const char *format,...) {
    size_t used=strlen(messages);
    if(used>=sizeof(messages)-2) return;
    va_list args;va_start(args,format);
    vsnprintf(messages+used,sizeof(messages)-used,format,args);va_end(args);
    used=strlen(messages);if(used<sizeof(messages)-2){messages[used]='\n';messages[used+1]=0;}
}
void kui_runtime_free(struct kui_runtime_image *image) { free(image->data);memset(image,0,sizeof(*image)); }
int main(int argc,char **argv) {
    assert(argc==3);disk=fopen(argv[1],"rb");assert(disk);
    assert(!fseeko(disk,0,SEEK_END));off_t bytes=ftello(disk);assert(bytes>0 && bytes%512==0);blocks=(uint64_t)bytes/512;
    const struct kui_media_ops ops={NULL,capacity,read_blocks,write_blocks,sync_blocks};
    for(unsigned repeat=0;repeat<2;++repeat) {
        reads=writes=syncs=cancel_calls=0;messages[0]=0;
        cancel_test=!strcmp(argv[2],"cancel");io_test=!strcmp(argv[2],"io");
        struct kui_runtime_image image={0};
        enum kui_ext4_boot_result result=kui_ext4_boot_read(&ops,&image,log_line,cancelled);
        if(!strcmp(argv[2],"ok")) {
            if(result!=KUI_EXT4_BOOT_OK) fprintf(stderr,"result=%d\n%s",result,messages);
            assert(result==KUI_EXT4_BOOT_OK && image.data && image.info.payload_bytes==65536);
            const unsigned char *p=image.data;
            for(unsigned i=0;i<image.info.payload_bytes;++i) assert(p[i]==(unsigned char)(i*29u+7u));
            kui_runtime_free(&image);
        } else {
            enum kui_ext4_boot_result expected=KUI_EXT4_BOOT_INVALID;
            if(!strcmp(argv[2],"cancel")) expected=KUI_EXT4_BOOT_CANCELLED;
            else if(!strcmp(argv[2],"io")) expected=KUI_EXT4_BOOT_IO;
            else if(!strcmp(argv[2],"missing") || !strcmp(argv[2],"checksum") || !strcmp(argv[2],"metadata")) expected=KUI_EXT4_BOOT_RUNTIME;
            else if(!strcmp(argv[2],"notfound")) expected=KUI_EXT4_BOOT_NOT_FOUND;
            if(result!=expected) fprintf(stderr,"result=%d expected=%d\n%s",result,expected,messages);
            assert(result==expected && !image.data && !image.info.payload_bytes);
            if(!strcmp(argv[2],"checksum")) assert(strstr(messages,"checksum mismatch"));
        }
        assert(!writes && !syncs);
    }
    fclose(disk);printf("ext4 boot %s: repeated cleanup, expected result, zero writes passed\n",argv[2]);
}
