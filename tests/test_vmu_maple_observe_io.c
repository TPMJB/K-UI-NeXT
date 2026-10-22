/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/vmu_maple_observe_io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static char events[8];
static unsigned used;
static int acquire_result,image_result,read_result,stop_result,card_result;
static uint32_t trace;
static void event(char value) {assert(used<sizeof(events));events[used++]=value;}
static int acquire(void) {event('A');return acquire_result;}
static int read_image(void) {event('I');card_result=read_result;return image_result;}
static int stop(void) {event('S');return stop_result;}
static void release(void) {event('R');}

static int pipeline(int observed) {
    int result,stopped;
    used=0;memset(events,0,sizeof(events));
    card_result=observed?KUI_VMU_OBSERVE_IO_ACQUIRE(trace,acquire()):acquire();
    if(card_result) return -1;
    result=observed?(int)KUI_VMU_OBSERVE_IO_IMAGE(trace,read_image()):read_image();
    stopped=stop();
    if(!card_result) card_result=stopped;
    release();
    return !result && !card_result?0:-1;
}

int main(void) {
    /* Trace every combination of acquisition, image, physical read and
     * stream-stop failures against the unobserved physical call sequence. */
    for(acquire_result=0;acquire_result<3;acquire_result++)
    for(image_result=0;image_result<3;image_result++)
    for(read_result=0;read_result<3;read_result++)
    for(stop_result=0;stop_result<3;stop_result++) {
        char expected[8];
        int baseline=pipeline(0),expected_card=card_result;
        memcpy(expected,events,sizeof(expected));
        trace=0; /* Deliberately stale successful image read. */
        assert(pipeline(1)==baseline);
        assert(card_result==expected_card);
        assert(!memcmp(expected,events,sizeof(expected)));
        assert(trace==(acquire_result?UINT32_MAX:(uint32_t)image_result));
        assert(used==(acquire_result?1u:4u));
    }
    puts("VMU observer I/O trace preserves physical calls and failure results");
    return 0;
}
