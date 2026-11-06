/* SPDX-License-Identifier: GPL-3.0-only */
/* The same bounded I/O model exercises both production mounted readers. */
#include <stdlib.h>
static void *batch_malloc(size_t);
static void batch_free(void *);
#define malloc batch_malloc
#define free batch_free
#ifdef KUI_VMU_LIBRARY_BATCH_TEST
#include "../src/core/vmu_library.c"
#else
#include "../src/core/vmu_history.c"
#endif
#undef malloc
#undef free
#include <assert.h>
static _Alignas(32) uint8_t source[131072],output[sizeof(source)];
static unsigned reads,opens,closes,cancel_after,allocation_calls,live_allocations,fail_read;
static size_t bytes,read_bytes;
static bool short_read,close_failure,allocation_failure,cancel_now;
static uint64_t metrics[KUI_SMASH_UI_METRICS];
void kui_smash_launch_timing_metric(enum kui_smash_ui_metric metric,uint64_t value) {metrics[metric]=value;}
void kui_smash_launch_timing_add(enum kui_smash_ui_metric metric,uint64_t value) {metrics[metric]+=value;}
static void *batch_malloc(size_t amount) {
    assert(amount==32768u+31u);++allocation_calls;
    if(allocation_failure)return NULL;
    void *out=malloc(amount);assert(out);++live_allocations;return out;
}
static void batch_free(void *allocation) {
    if(allocation) {assert(live_allocations==1u);--live_allocations;free(allocation);}
}
FRESULT f_open(FIL *file,const TCHAR *path,BYTE mode) {
    assert(!strcmp(path,"0:/history/card.vmd") && mode==FA_READ);++opens;
    memset(file,0,sizeof(*file));file->obj.objsize=bytes;return FR_OK;
}
FRESULT f_read(FIL *file,void *out,UINT n,UINT *got) {
    assert(n<=32768u && n<=bytes-file->fptr);++reads;
    assert(((uintptr_t)out&31u)==0u);
    *got=short_read?n-1u:n;memcpy(out,source+file->fptr,*got);file->fptr+=*got;read_bytes+=*got;
    return fail_read==reads?FR_DISK_ERR:FR_OK;
}
FRESULT f_close(FIL *file) {(void)file;++closes;return close_failure?FR_DISK_ERR:FR_OK;}
static bool cancel(void) {return cancel_now || (cancel_after && reads>=cancel_after);}
static void reset(void) {
    assert(!live_allocations);reads=opens=closes=cancel_after=allocation_calls=fail_read=0u;
    bytes=sizeof(source);read_bytes=0u;short_read=close_failure=allocation_failure=cancel_now=false;memset(metrics,0,sizeof(metrics));
}
static int compare(void) {
#ifdef KUI_VMU_LIBRARY_BATCH_TEST
    int result=compare_file("0:/history/card.vmd",output,sizeof(output),cancel);
#else
    int result=exact("0:/history/card.vmd",NULL,sizeof(output),output,cancel);
#endif
    assert(!live_allocations && metrics[KUI_SMASH_UI_HISTORY_READ_CALLS]==reads &&
        metrics[KUI_SMASH_UI_HISTORY_READ_BYTES]==read_bytes);return result;
}
int main(void) {
    for(unsigned i=0u;i<sizeof(source);i++)source[i]=(uint8_t)(i^(i>>8));
    memcpy(output,source,sizeof(output));
#ifndef KUI_VMU_LIBRARY_BATCH_TEST
    reset();assert(exact("0:/history/card.vmd",output,sizeof(output),NULL,cancel)==OK);
    assert(reads==4u && closes==1u && !allocation_calls && !memcmp(output,source,sizeof(source)));
    assert(metrics[KUI_SMASH_UI_HISTORY_READ_CALLS]==4u && metrics[KUI_SMASH_UI_HISTORY_READ_BYTES]==sizeof(source));
    assert(metrics[KUI_SMASH_UI_VERIFY_BATCH_BYTES]==32768u);
    reset();allocation_failure=true;
    assert(exact("0:/history/card.vmd",output,sizeof(output),NULL,cancel)==OK && !allocation_calls && reads==4u);
#endif
    reset();assert(compare()==OK && reads==4u && closes==1u && allocation_calls==1u);
    assert(metrics[KUI_SMASH_UI_VERIFY_BATCH_BYTES]==32768u && read_bytes==sizeof(source));
    reset();allocation_failure=true;assert(compare()==OK && reads==32u && closes==1u && allocation_calls==1u);
    assert(metrics[KUI_SMASH_UI_VERIFY_BATCH_BYTES]==4096u && read_bytes==sizeof(source));
    static const unsigned offsets[]={0u,32767u,32768u,65535u,65536u,131071u};
    for(unsigned i=0u;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        reset();source[offsets[i]]^=1u;assert(compare()==BAD && reads==offsets[i]/32768u+1u && closes==1u);
        reset();allocation_failure=true;assert(compare()==BAD && reads==offsets[i]/4096u+1u && closes==1u);
        source[offsets[i]]^=1u;
    }
    reset();short_read=true;assert(compare()==IO && reads==1u && closes==1u && read_bytes==32767u);
    reset();allocation_failure=short_read=true;assert(compare()==IO && reads==1u && closes==1u && read_bytes==4095u);
    reset();fail_read=4u;assert(compare()==IO && reads==4u && closes==1u);
    reset();close_failure=true;assert(compare()==IO && closes==1u);
    for(unsigned boundary=1u;boundary<4u;++boundary) {
        reset();cancel_after=boundary;assert(compare()==STOP && reads==boundary && closes==1u);
    }
    reset();allocation_failure=true;cancel_after=2u;assert(compare()==STOP && reads==2u && closes==1u);
    reset();cancel_now=true;assert(compare()==STOP && !reads);
#ifdef KUI_VMU_LIBRARY_BATCH_TEST
    assert(opens==1u && closes==1u && allocation_calls==1u);
#else
    assert(!opens && !closes && !allocation_calls);
#endif
    reset();bytes--;assert(compare()==BAD && !reads && closes==1u && !allocation_calls);
    assert(!memcmp(output,source,sizeof(source)));
#ifdef KUI_VMU_LIBRARY_BATCH_TEST
    puts("Common library comparison: four32KiB reads,32x4KiB low-memory fallback, corruption/short/error/cancel/metrics retained");
#else
    puts("Common history verification: two complete128KiB passes in4+4 reads, direct first pass,4KiB low-memory fallback and corruption/error/cancel/metrics retained");
#endif
    return 0;
}
