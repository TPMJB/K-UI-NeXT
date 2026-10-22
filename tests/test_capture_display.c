/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture_display.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void) {
    struct kui_capture_display v;
    kui_capture_display_start(&v,"Dead or Alive 2");
    assert(!strcmp(v.title,"Dead or Alive 2"));
    kui_capture_display_log(&v,"Disc: DEAD OR ALIVE 2");
    assert(!strcmp(v.title,"DEAD OR ALIVE 2"));
    struct kui_capture_progress p={.retries=1};
    kui_capture_display_progress(&v,&p);
    for(unsigned i=1;i<=10;i++) {
        char text[128];snprintf(text,sizeof(text),"Read retry %u/10: track 03 FAD=380111",i);
        kui_capture_display_log(&v,text);
        assert(v.retries==i+1 && v.retry_attempt==i && v.retry_fad==380111);
    }
    kui_capture_display_log(&v,"CAPTURE/VERIFY FAILED: last committed bytes=823364640; retries=11");
    assert(v.retries==11 && v.retry_attempt==10);
    kui_capture_display_start(&v,NULL);
    p.retries=11;kui_capture_display_progress(&v,&p);
    assert(v.retries==11 && v.retry_attempt==0);
    kui_capture_display_log(&v,"Read retry 1/10: track 03 FAD=380111");
    p.retries=12;kui_capture_display_progress(&v,&p);
    assert(v.retries==12 && v.retry_attempt==0);
    kui_capture_display_log(&v,"Read retry 2/10: track 03 FAD=380111");
    kui_capture_display_log(&v,"STOPPED: last committed bytes=823364640; retries=13");
    assert(v.retries==13);
    kui_capture_display_log(&v,"Read retry 99/10: track 03 FAD=380111");
    kui_capture_display_log(&v,"Read retry 1/10: track 03 FAD=380111 junk");
    assert(v.retries==13);
    p.retries=UINT32_MAX;kui_capture_display_progress(&v,&p);
    kui_capture_display_log(&v,"Read retry 1/10: track 03 FAD=380111");
    assert(v.retries==UINT32_MAX);
    puts("capture display: title, retry attempts, persistent totals and saturation passed");
}
