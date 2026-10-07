/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/cdda_job.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned checks;
#define CHECK(x) do {++checks;assert(x);} while(0)
static void stale(struct kui_cdda_job *j,struct kui_cdda_job_span span,uint32_t epoch) {
    struct kui_cdda_job saved=*j;
    CHECK(kui_cdda_job_ready(j,&span,epoch,true)==KUI_CDDA_JOB_STALE);
    CHECK(!memcmp(&saved,j,sizeof(saved)));
    CHECK(kui_cdda_job_commit(j,&span,epoch)==KUI_CDDA_JOB_STALE);
    CHECK(!memcmp(&saved,j,sizeof(saved)));
}
static void ranges_and_chunks(void) {
    static const uint32_t lengths[]={1,31,511,512,513,2048,4096,32768};
    static const uint32_t caps[]={1,31,511,512,513,2048};
    for(unsigned n=0;n<sizeof(lengths)/sizeof(lengths[0]);n++)
        for(unsigned c=0;c<sizeof(caps)/sizeof(caps[0]);c++)
            for(unsigned edge=0;edge<3;edge++) {
                struct kui_cdda_job j;
                CHECK(kui_cdda_job_init(&j)==KUI_CDDA_JOB_OK);
                uint32_t first=edge==0?0:edge==1?509:UINT32_MAX-lengths[n];
                uint32_t token=0;
                CHECK(kui_cdda_job_begin(&j,UINT32_MAX,first,lengths[n],17,&token)==KUI_CDDA_JOB_OK);
                CHECK(token==1 && j.state==KUI_CDDA_JOB_QUEUED && !j.done);
                uint32_t expected=0,chunk=0;
                struct kui_cdda_job_span previous={0};
                while(expected<lengths[n]) {
                    struct kui_cdda_job_span span;
                    CHECK(kui_cdda_job_claim(&j,token,17,caps[c],&span)==KUI_CDDA_JOB_OK);
                    CHECK(span.job==token && span.session_epoch==17 && span.chunk==++chunk);
                    CHECK(span.offset==first+expected && span.bytes && span.bytes<=caps[c]);
                    uint32_t take=lengths[n]-expected;if(take>caps[c]) take=caps[c];
                    CHECK(span.bytes==take && j.done==expected && j.state==KUI_CDDA_JOB_READING);
                    if(previous.chunk) stale(&j,previous,17);
                    struct kui_cdda_job saved=j;struct kui_cdda_job_span out;memset(&out,0x7a,sizeof(out));
                    struct kui_cdda_job_span out_saved=out;
                    CHECK(kui_cdda_job_claim(&j,token,17,caps[c],&out)==KUI_CDDA_JOB_BUSY);
                    CHECK(!memcmp(&saved,&j,sizeof(j)) && !memcmp(&out,&out_saved,sizeof(out)));
                    CHECK(kui_cdda_job_cancel(&j,token)==KUI_CDDA_JOB_BUSY);
                    CHECK(!memcmp(&saved,&j,sizeof(j)));
                    CHECK(kui_cdda_job_ready(&j,&span,17,true)==KUI_CDDA_JOB_OK);
                    CHECK(j.done==expected && j.state==KUI_CDDA_JOB_READY);
                    saved=j;CHECK(kui_cdda_job_ready(&j,&span,17,true)==KUI_CDDA_JOB_STALE);
                    CHECK(!memcmp(&saved,&j,sizeof(j)));
                    expected+=take;
                    CHECK(kui_cdda_job_commit(&j,&span,17)==(expected==lengths[n]?KUI_CDDA_JOB_COMPLETE:KUI_CDDA_JOB_OK));
                    CHECK(j.done==expected && !j.pending.bytes && !j.pending.chunk);
                    stale(&j,span,17);previous=span;
                }
                CHECK(j.state==KUI_CDDA_JOB_DONE && j.done==lengths[n]);
                struct kui_cdda_job saved=j;struct kui_cdda_job_span span;
                CHECK(kui_cdda_job_claim(&j,token,17,2048,&span)==KUI_CDDA_JOB_STALE);
                CHECK(!memcmp(&saved,&j,sizeof(j)));
            }
}
static void cancellation(void) {
    struct kui_cdda_job j;uint32_t first,second;
    CHECK(kui_cdda_job_init(&j)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_begin(&j,8u*1024u*1024u,509,32768,44,&first)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_cancel(&j,first)==KUI_CDDA_JOB_OK && j.state==KUI_CDDA_JOB_CANCELED && !j.done);
    struct kui_cdda_job saved=j;struct kui_cdda_job_span span;
    CHECK(kui_cdda_job_claim(&j,first,44,2048,&span)==KUI_CDDA_JOB_STALE);
    CHECK(!memcmp(&saved,&j,sizeof(j)));
    CHECK(kui_cdda_job_begin(&j,8u*1024u*1024u,513,32768,45,&second)==KUI_CDDA_JOB_OK && second>first);
    saved=j;
    CHECK(kui_cdda_job_claim(&j,first,44,2048,&span)==KUI_CDDA_JOB_STALE);
    CHECK(!memcmp(&saved,&j,sizeof(j)));
    CHECK(kui_cdda_job_claim(&j,second,45,2048,&span)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_ready(&j,&span,45,true)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_commit(&j,&span,45)==KUI_CDDA_JOB_OK && j.done==2048);
    CHECK(kui_cdda_job_cancel(&j,second)==KUI_CDDA_JOB_OK && j.done==2048);
    stale(&j,span,45);
    CHECK(kui_cdda_job_begin(&j,4096,512,2048,46,&first)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_claim(&j,first,46,1024,&span)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_ready(&j,&span,46,true)==KUI_CDDA_JOB_OK && !j.done);
    CHECK(kui_cdda_job_cancel(&j,first)==KUI_CDDA_JOB_OK && !j.pending.bytes && !j.done);
    stale(&j,span,46);
    CHECK(kui_cdda_job_begin(&j,4096,0,4096,47,&second)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_claim(&j,second,47,512,&span)==KUI_CDDA_JOB_OK);
    saved=j;CHECK(kui_cdda_job_ready(&j,&span,48,true)==KUI_CDDA_JOB_STALE);
    CHECK(!memcmp(&saved,&j,sizeof(j)));
    CHECK(kui_cdda_job_ready(&j,&span,47,true)==KUI_CDDA_JOB_OK);
    saved=j;CHECK(kui_cdda_job_commit(&j,&span,48)==KUI_CDDA_JOB_STALE);
    CHECK(!memcmp(&saved,&j,sizeof(j)));
    CHECK(kui_cdda_job_cancel(&j,second)==KUI_CDDA_JOB_OK);
}
static void identities_and_errors(void) {
    struct kui_cdda_job j;uint32_t token;
    CHECK(kui_cdda_job_init(&j)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_begin(&j,4096,0,4096,11,&token)==KUI_CDDA_JOB_OK);
    struct kui_cdda_job_span span;
    CHECK(kui_cdda_job_claim(&j,token,11,512,&span)==KUI_CDDA_JOB_OK);
    for(unsigned field=0;field<5;field++) {
        struct kui_cdda_job_span bad=span;
        switch(field) {case 0:bad.job++;break;case 1:bad.session_epoch++;break;
        case 2:bad.chunk++;break;case 3:bad.offset++;break;case 4:bad.bytes++;break;}
        stale(&j,bad,11);
    }
    CHECK(kui_cdda_job_ready(&j,&span,11,false)==KUI_CDDA_JOB_IO);
    CHECK(j.state==KUI_CDDA_JOB_FAULT && !j.done && !j.pending.bytes);stale(&j,span,11);
    CHECK(kui_cdda_job_begin(&j,4096,0,4096,12,&token)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_fail(&j,token)==KUI_CDDA_JOB_IO && j.state==KUI_CDDA_JOB_FAULT);
    struct kui_cdda_job saved=j;
    CHECK(kui_cdda_job_fail(&j,token)==KUI_CDDA_JOB_STALE && !memcmp(&saved,&j,sizeof(j)));
    CHECK(kui_cdda_job_begin(&j,4096,0,4096,13,&token)==KUI_CDDA_JOB_OK);
    saved=j;uint32_t other=91;
    CHECK(kui_cdda_job_begin(&j,4096,0,1,13,&other)==KUI_CDDA_JOB_BUSY);
    CHECK(other==91 && !memcmp(&saved,&j,sizeof(j)));
    CHECK(kui_cdda_job_cancel(&j,token)==KUI_CDDA_JOB_OK);
    j.generation=UINT32_MAX;saved=j;
    CHECK(kui_cdda_job_begin(&j,4096,0,1,13,&other)==KUI_CDDA_JOB_OVERFLOW);
    CHECK(other==91 && !memcmp(&saved,&j,sizeof(j)));
    CHECK(kui_cdda_job_init(&j)==KUI_CDDA_JOB_OK);
    CHECK(kui_cdda_job_begin(&j,4096,0,4096,13,&token)==KUI_CDDA_JOB_OK);
    j.chunk_generation=UINT32_MAX;saved=j;
    CHECK(kui_cdda_job_claim(&j,token,13,512,&span)==KUI_CDDA_JOB_OVERFLOW);
    CHECK(!memcmp(&saved,&j,sizeof(j)));
}
static void invalid(void) {
    struct kui_cdda_job j;CHECK(kui_cdda_job_init(&j)==KUI_CDDA_JOB_OK);
    uint32_t token=991;struct kui_cdda_job saved=j;
    static const uint32_t bad[][3]={{0,0,1},{512,513,1},{512,512,1},{512,0,0},{512,511,2},
        {UINT32_MAX,UINT32_MAX-1u,2}};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        CHECK(kui_cdda_job_begin(&j,bad[i][0],bad[i][1],bad[i][2],0,&token)==KUI_CDDA_JOB_INVALID);
        CHECK(token==991 && !memcmp(&saved,&j,sizeof(j)));
    }
    CHECK(kui_cdda_job_init(NULL)==KUI_CDDA_JOB_INVALID);
    CHECK(kui_cdda_job_begin(NULL,1,0,1,0,&token)==KUI_CDDA_JOB_INVALID);
    CHECK(kui_cdda_job_begin(&j,1,0,1,0,NULL)==KUI_CDDA_JOB_INVALID);
    CHECK(kui_cdda_job_begin(&j,1,0,1,0,&j.done)==KUI_CDDA_JOB_INVALID);
    CHECK(!memcmp(&saved,&j,sizeof(j)));
    CHECK(kui_cdda_job_begin(&j,1,0,1,0,&token)==KUI_CDDA_JOB_OK);
    saved=j;struct kui_cdda_job_span span;
    CHECK(kui_cdda_job_claim(&j,token,0,0,&span)==KUI_CDDA_JOB_INVALID);
    CHECK(kui_cdda_job_claim(&j,token,0,2049,&span)==KUI_CDDA_JOB_INVALID);
    CHECK(kui_cdda_job_claim(&j,token,0,1,NULL)==KUI_CDDA_JOB_INVALID);
    CHECK(!memcmp(&saved,&j,sizeof(j)));
    CHECK(kui_cdda_job_ready(&j,NULL,0,true)==KUI_CDDA_JOB_INVALID);
    CHECK(kui_cdda_job_commit(&j,NULL,0)==KUI_CDDA_JOB_INVALID);
    CHECK(kui_cdda_job_cancel(NULL,0)==KUI_CDDA_JOB_INVALID);
    CHECK(kui_cdda_job_fail(NULL,0)==KUI_CDDA_JOB_INVALID);
}
int main(void) {
    ranges_and_chunks();cancellation();identities_and_errors();invalid();
    printf("PASS CDDA jobs: %u checks; ranges/chunks, unique identities, cancellation, duplicate/stale callbacks, I/O and overflow\n",checks);
    return 0;
}
