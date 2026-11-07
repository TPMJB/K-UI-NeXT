/* SPDX-License-Identifier: GPL-3.0-only */
/* Host-only: whether a startup is one of the reviewed no-allocator arena
 * executables the Background module admits through its fixed arena (the
 * structural corpus probe models allocator and fixed plans only). It runs the
 * production plan lookup on the program extent the stage would map. No retail
 * bytes are embedded here; a plan names an exact image by size and SHA-256. */
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_program.h"
#include "kui/hash.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv) {
    if(argc!=2) {fprintf(stderr,"Usage: %s STARTUP\n",argv[0]);return 2;}
    FILE *file=fopen(argv[1],"rb");if(!file)return 2;
    if(fseek(file,0,SEEK_END))return 2;
    long length=ftell(file);
    if(length<0x300 || length>0xc00000 || fseek(file,0,SEEK_SET))return 2;
    uint8_t *image=malloc((size_t)length);if(!image)return 2;
    if(fread(image,1,(size_t)length,file)!=(size_t)length || fclose(file))return 2;
    const uint32_t load=UINT32_C(0x8c010000);
    size_t selected=0u,n=(size_t)length;
    if(kui_vmu_native_program_extent(image,n,load,&selected))n=selected;
    uint8_t sha[32];struct kui_sha256 state;
    kui_sha256_init(&state);kui_sha256_update(&state,image,n);kui_sha256_digest(&state,sha);
    struct kui_vmu_native_heap owner={0};struct kui_vmu_native_heap_call call={0};
    struct kui_vmu_native_heap_fixed_hook hook={0};
    bool arena=kui_vmu_native_heap_arena_plan(image,n,sha,load,&owner,&call,&hook);
    printf("{\"arena_plan\":%s,\"arena_image_bytes\":%zu}\n",arena?"true":"false",n);
    free(image);return 0;
}
