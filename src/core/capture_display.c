/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/capture_display.h"
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
void kui_capture_display_start(struct kui_capture_display *out,const char *title) {
    *out=(struct kui_capture_display){0};
    if(title) snprintf(out->title,sizeof(out->title),"%s",title);
}
void kui_capture_display_progress(struct kui_capture_display *out,const struct kui_capture_progress *p) {
    out->retries=p->retries;
    out->retry_attempt=out->retry_limit=0;out->retry_fad=0;
}
void kui_capture_display_log(struct kui_capture_display *out,const char *text) {
    unsigned attempt,limit,track;uint32_t fad,total;uint64_t bytes;int end=0;
    if(!strncmp(text,"Disc: ",6)) {
        snprintf(out->title,sizeof(out->title),"%s",text+6);return;
    }
    if(sscanf(text,"Read retry %u/%u: track %u FAD=%" SCNu32 "%n",
              &attempt,&limit,&track,&fad,&end)==4 && !text[end] &&
       attempt && attempt<=limit && limit==KUI_CAPTURE_RETRIES && track && track<=99) {
        if(out->retries!=UINT32_MAX) ++out->retries;
        out->retry_attempt=attempt;out->retry_limit=limit;out->retry_fad=fad;
        return;
    }
    const char *summary=!strncmp(text,"STOPPED: ",9)?text+9:
        !strncmp(text,"CAPTURE/VERIFY FAILED: ",23)?text+23:NULL;
    if(summary && sscanf(summary,"last committed bytes=%" SCNu64 "; retries=%" SCNu32 "%n",
                         &bytes,&total,&end)==2 && !summary[end]) out->retries=total;
}
