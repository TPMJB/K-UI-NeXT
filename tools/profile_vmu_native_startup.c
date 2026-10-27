/* SPDX-License-Identifier: GPL-3.0-only */
/* Developer-only host benchmark. Reads a supplied executable; never patches it. */
#define _POSIX_C_SOURCE 200809L
#include "kui/vmu_native_layout.h"
#include "kui/vmu_native_heap.h"
#include "kui/vmu_native_exit.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define REQUIRE(x) do {if(!(x)) {fprintf(stderr,"Startup profile failed at line %u\n",__LINE__);exit(1);}} while(0)
static double now(void) {
    struct timespec t;REQUIRE(!clock_gettime(CLOCK_MONOTONIC,&t));
    return (double)t.tv_sec+(double)t.tv_nsec*1e-9;
}
int main(int argc,char **argv) {
    if(argc<2 || argc>3) {fprintf(stderr,"Usage: %s 1ST_READ.BIN [repeats]\n",argv[0]);return 2;}
    unsigned repeat=40u;
    if(argc==3) {char *end;unsigned long n=strtoul(argv[2],&end,10);REQUIRE(!*end && n && n<=10000u);repeat=(unsigned)n;}
    FILE *f=fopen(argv[1],"rb");REQUIRE(f);REQUIRE(!fseek(f,0,SEEK_END));long length=ftell(f);
    REQUIRE(length>0 && length<=0xc00000);rewind(f);size_t bytes=(size_t)length;
    void *image=malloc(bytes);REQUIRE(image);REQUIRE(fread(image,1,bytes,f)==bytes);REQUIRE(!fclose(f));
    const uint32_t load=UINT32_C(0x8c010000);
    struct kui_vmu_native_discovery driver;struct kui_vmu_native_layout layout;
    struct kui_vmu_native_heap heap;struct kui_vmu_native_heap_call call;
    struct kui_vmu_native_exit exits;
    double start=now();
    for(unsigned i=0;i<repeat;i++) REQUIRE(kui_vmu_native_discover_layout(image,bytes,load,&driver,&layout));
    double discovery=now()-start;start=now();
    for(unsigned i=0;i<repeat;i++) REQUIRE(kui_vmu_native_heap_discover(image,bytes,load,&heap)==KUI_VMU_NATIVE_HEAP_FOUND);
    double allocator=now()-start;start=now();
    for(unsigned i=0;i<repeat;i++) REQUIRE(kui_vmu_native_heap_call_discover(image,bytes,&heap,&call));
    double allocator_call=now()-start;
    const struct kui_vmu_native_patch_window additional[]={
        {driver.submit,12u},{heap.initializer,12u},{heap.release,20u}};
    start=now();
    for(unsigned i=0;i<repeat;i++) REQUIRE(kui_vmu_native_exit_discover(image,bytes,load,&exits));
    double exit_only=now()-start;
    const struct kui_vmu_native_patch_window all[]={additional[0],additional[1],additional[2],
        {exits.entries[0],12u},{exits.entries[1],12u},{exits.entries[2],12u}};
    start=now();
    for(unsigned i=0;i<repeat;i++) REQUIRE(kui_vmu_native_patch_no_interiors(image,bytes,load,all,6u));
    double extra_scan=now()-start;start=now();
    for(unsigned i=0;i<repeat;i++) REQUIRE(kui_vmu_native_exit_discover_guarded(image,bytes,load,additional,3u,&exits));
    double combined=now()-start;
    double scale=1000.0/repeat;
    printf("bytes=%zu repeats=%u (host elapsed timings; not Dreamcast latency estimates)\n",bytes,repeat);
    printf("driver_layout_ms=%.3f allocator_ms=%.3f allocator_call_ms=%.3f\n",
        discovery*scale,allocator*scale,allocator_call*scale);
    printf("old_exit_ms=%.3f duplicate_patch_scan_ms=%.3f combined_exit_patch_ms=%.3f\n",
        exit_only*scale,extra_scan*scale,combined*scale);
    printf("old_total_ms=%.3f combined_total_ms=%.3f\n",
        (discovery+allocator+allocator_call+exit_only+extra_scan)*scale,
        (discovery+allocator+allocator_call+combined)*scale);
    free(image);return 0;
}
