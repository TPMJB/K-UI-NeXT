/* SPDX-License-Identifier: GPL-3.0-only */
/* Host elapsed comparison only. The trusted pin comes from a complete fresh
 * proof in this process; no external cache or filesystem writes are used. */
#define _POSIX_C_SOURCE 200809L
#include "kui/vmu_native_resolution_cache.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define REQUIRE(x) do {if(!(x)) {fprintf(stderr,"Cache profile failed at line %u\n",__LINE__);exit(1);}} while(0)
static double now(void) {
    struct timespec t;REQUIRE(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (double)t.tv_sec+(double)t.tv_nsec*1e-9;
}
int main(int argc,char **argv) {
    if(argc<2 || argc>3) {fprintf(stderr,"Usage: %s 1ST_READ.BIN [repeats]\n",argv[0]);return 2;}
    unsigned repeat=20u;
    if(argc==3) {char *end;unsigned long n=strtoul(argv[2],&end,10);REQUIRE(!*end && n && n<=10000u);repeat=(unsigned)n;}
    FILE *f=fopen(argv[1],"rb");REQUIRE(f);REQUIRE(!fseek(f,0,SEEK_END));long length=ftell(f);
    REQUIRE(length>0 && length<=0xc00000);rewind(f);size_t bytes=(size_t)length;
    void *image=malloc(bytes);REQUIRE(image);REQUIRE(fread(image,1,bytes,f)==bytes);REQUIRE(!fclose(f));
    const uint32_t load=UINT32_C(0x8c010000);
    struct kui_vmu_native_resolution fresh,replayed;uint8_t wire[1024],pin[32];
    REQUIRE(kui_vmu_native_resolution_cache_build(image,bytes,load,wire,sizeof(wire),&fresh));
    REQUIRE(kui_vmu_native_resolution_cache_record_digest(wire,sizeof(wire),pin));
    double start=now();
    for(unsigned i=0;i<repeat;i++) REQUIRE(kui_vmu_native_resolution_cache_build(image,bytes,load,wire,sizeof(wire),&fresh));
    double full=now()-start;start=now();
    for(unsigned i=0;i<repeat;i++) {
        REQUIRE(kui_vmu_native_resolution_cache_replay(image,bytes,load,wire,sizeof(wire),pin,&replayed));
        REQUIRE(!memcmp(&fresh,&replayed,sizeof(fresh)));
    }
    double replay=now()-start;
    printf("bytes=%zu repeats=%u full_build_ms=%.3f pinned_replay_ms=%.3f ratio=%.2f layout=%u exit_family=%u\n",
        bytes,repeat,1000.0*full/repeat,1000.0*replay/repeat,full/replay,fresh.layout.rx_layout,fresh.exits.family);
    puts("Host elapsed measurements include fresh executable SHA256 on every replay; not Dreamcast latency estimates.");
    free(image);return 0;
}
