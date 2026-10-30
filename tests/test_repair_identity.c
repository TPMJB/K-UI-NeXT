/* SPDX-License-Identifier: GPL-3.0-only */
/* Direct engine-helper tests: bounded identity admission and reference grades.
 * Filesystem durability belongs to repair_copy_image.c. */
#include "kui/recovery_scan.h"
#include "kui/recovery_checks.h"
#include "kui/legacy_import.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

struct test_status { unsigned attempts;bool audio_unverified,catalogue_checked;struct kui_known_summary catalogue; };
struct test_options {unsigned attempts;};
struct recovery_copy {
    struct kui_scan_manifest manifest;struct kui_scan_gdi source;
    struct kui_legacy_job *legacy;const struct kui_capture_ops *optical_ops;
    const struct kui_capture_plan *optical_plan;const struct test_options *repair_options;
    struct test_status *repair;bool failed,partial_source,manifest_present,stop,saved_identity_active,identity_unreadable;
    uint8_t candidate[KUI_RAW_BYTES],samples[3][KUI_RAW_BYTES];uint32_t crc[99];int log;
};
static unsigned scenario,reads[80],total_reads,lines,reports;
static enum kui_known_result reference;
static struct kui_known_track reference_tracks[99];
static bool cancelled(struct recovery_copy *s) {return s->stop;}
static bool fail(struct recovery_copy *s,const char *why) {(void)why;s->failed=true;return false;}
static void put32(uint8_t *p,uint32_t n) {for(unsigned i=0;i<4;++i)p[i]=(uint8_t)(n>>(i*8));}
static uint32_t get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void hash32(struct kui_sha256 *hash,uint32_t n) {uint8_t raw[4];put32(raw,n);kui_sha256_update(hash,raw,4);}
static void raw_sector(uint32_t fad,uint8_t *raw) {memset(raw,(int)(fad&255u),KUI_RAW_BYTES);raw[0]=1;put32(raw+1,fad);}
static unsigned check_sector(struct recovery_copy *s,const uint8_t *raw,uint32_t fad) {(void)s;return raw[0]!=1u || get32(raw+1)!=fad;}
static bool read_saved_sector(struct recovery_copy *s,unsigned ti,uint32_t sector,uint8_t *raw) {
    raw_sector(s->source.plan.tracks[ti].start+sector,raw);
    if(scenario==6)memset(raw,0,KUI_RAW_BYTES);
    if(scenario==7)for(unsigned i=0;i<KUI_RAW_BYTES;++i)raw[i]=(uint8_t)i;
    if(scenario==1 && (sector<16u || sector>=64u))raw[0]=0;
    return true;
}
static bool optical_sector(struct recovery_copy *s,uint32_t fad,uint8_t *raw) {
    uint32_t sector=fad-s->optical_plan->tracks[0].start;assert(sector<80u);
    ++reads[sector];++total_reads;++s->repair->attempts;
    if(scenario==2 && (sector<16u || sector>=64u))return false;
    if(scenario==3 && reads[sector]<3u)return false;
    if(scenario==5)return false;
    raw_sector(fad,raw);if(scenario==6)memset(raw,0,KUI_RAW_BYTES);if(scenario==7)for(unsigned i=0;i<KUI_RAW_BYTES;++i)raw[i]=(uint8_t)i;
    if(scenario==4)raw[7]^=1;
    return true;
}
const char *kui_capture_profile_name(const struct kui_capture_plan *p) {
    return !p?NULL:p->profile==KUI_CAPTURE_PROFILE_GD?KUI_CAPTURE_PROFILE:
        p->profile==KUI_CAPTURE_PROFILE_CD_TOC?KUI_CAPTURE_CD_PROFILE:NULL;
}
static void silent_log(const char *format,...) {(void)format;}
static kui_log_fn logger(struct recovery_copy *s) {(void)s;return silent_log;}
static bool line(struct recovery_copy *s,const char *format,...) {(void)s;(void)format;++lines;return true;}
static bool sync_file(struct recovery_copy *s,int *file) {(void)s;(void)file;return true;}
void kui_known_check(const char *a,const char *b,const struct kui_known_track *tracks,unsigned count,
        struct kui_known_summary *out,bool (*stop)(void *),void *ctx) {
    assert(!strcmp(a,"0:/KUI/redump.db") && !strcmp(b,"0:/KUI/tosec.db"));
    assert(count==1u);memcpy(reference_tracks,tracks,count*sizeof(*tracks));
    out->result=stop(ctx)?KUI_KNOWN_CANCELLED:reference;snprintf(out->catalog,sizeof(out->catalog),"Synthetic");
}
const char *kui_known_text(enum kui_known_result result) {(void)result;return "synthetic reference grade";}
static bool repair_saved_identity_recheck(struct recovery_copy *s) {(void)s;return true;}
static bool repair_saved_identity_discover(struct recovery_copy *s) {return fail(s,"No alternate binding in helper unit test");}
enum kui_legacy_result kui_legacy_identity_check(const struct kui_legacy_job *job,const struct kui_capture_ops *ops) {(void)job;(void)ops;return KUI_LEGACY_READY;}
#include "../src/core/repair_identity.inc"
static bool repair_catalogue_report(struct recovery_copy *s) {assert(s->repair->catalogue_checked && s->repair->catalogue.result!=KUI_KNOWN_CANCELLED);++reports;return true;}
#include "../src/core/repair_catalogue.inc"

static void setup(struct recovery_copy *s,struct kui_capture_plan *plan,struct test_status *status,struct test_options *options) {
    memset(s,0,sizeof(*s));memset(plan,0,sizeof(*plan));memset(status,0,sizeof(*status));memset(reads,0,sizeof(reads));total_reads=0;scenario=0;
    plan->count=1;plan->tracks[0]=(struct kui_capture_track){1,4,1,45150,45230,45230};plan->bytes=80u*KUI_RAW_BYTES;
    s->source.plan=*plan;s->optical_plan=plan;s->repair=status;options->attempts=3;s->repair_options=options;
}
static void saved_identity(struct recovery_copy *s) {
    s->manifest_present=true;const struct kui_capture_plan *p=s->optical_plan;
    struct kui_sha256 hash;kui_sha256_init(&hash);const char *profile=kui_capture_profile_name(p);
    kui_sha256_update(&hash,profile,strlen(profile));hash32(&hash,p->count);
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *t=&p->tracks[i];hash32(&hash,t->number);hash32(&hash,t->control);hash32(&hash,t->session);hash32(&hash,t->start);hash32(&hash,t->end);hash32(&hash,t->toc_end);
        uint32_t points[3]={t->start,t->start+(t->end-t->start-1u)/2u,t->end-1u};
        for(unsigned n=0;n<3u;++n) {raw_sector(points[n],s->candidate);hash32(&hash,points[n]);kui_sha256_update(&hash,s->candidate,KUI_RAW_BYTES);}
    }
    kui_sha256_digest(&hash,s->manifest.identity);
}
int main(void) {
    struct recovery_copy s;struct kui_capture_plan plan;struct test_status status;struct test_options options;
    setup(&s,&plan,&status,&options);scenario=1;assert(optical_identity(&s) && !s.failed && total_reads==2u);
    setup(&s,&plan,&status,&options);scenario=2;assert(optical_identity(&s) && total_reads<800u);
    setup(&s,&plan,&status,&options);saved_identity(&s);scenario=3;assert(optical_identity(&s) && total_reads==9u);
    assert(reads[0]==3u && reads[39]==3u && reads[79]==3u);
    setup(&s,&plan,&status,&options);saved_identity(&s);scenario=5;assert(!optical_identity(&s) && s.failed && total_reads==3u);
    setup(&s,&plan,&status,&options);scenario=4;assert(!optical_identity(&s) && s.failed && total_reads==1u);
    setup(&s,&plan,&status,&options);plan.profile=KUI_CAPTURE_PROFILE_CD_TOC;plan.tracks[0].session=0;s.source.plan=plan;
    assert(optical_identity(&s) && s.source.plan.profile==KUI_CAPTURE_PROFILE_CD_TOC);
    setup(&s,&plan,&status,&options);plan.profile=KUI_CAPTURE_PROFILE_CD_TOC;plan.tracks[0].control=0;plan.tracks[0].session=0;s.source.plan=plan;
    assert(optical_identity(&s) && status.audio_unverified && total_reads==4u);
    setup(&s,&plan,&status,&options);saved_identity(&s);s.partial_source=true;assert(optical_identity(&s) && total_reads==3u);
    for(int grade=KUI_KNOWN_ERROR;grade<=KUI_KNOWN_FULL_MATCH;++grade) {
        setup(&s,&plan,&status,&options);s.crc[0]=0x12345678u;reference=(enum kui_known_result)grade;lines=0;reports=0;
        assert(repair_catalogue(&s)==(grade!=KUI_KNOWN_CANCELLED));assert(status.catalogue_checked);
        assert(status.catalogue.result==reference && reference_tracks[0].crc32==s.crc[0] && reference_tracks[0].bytes==plan.bytes);
        assert(lines==(grade==KUI_KNOWN_CANCELLED?0u:1u) && reports==lines);
    }
    setup(&s,&plan,&status,&options);plan.profile=KUI_CAPTURE_PROFILE_CD_TOC;plan.tracks[0].session=0;plan.tracks[0].control=0;s.source.plan=plan;scenario=6;assert(!optical_identity(&s));
    setup(&s,&plan,&status,&options);plan.profile=KUI_CAPTURE_PROFILE_CD_TOC;plan.tracks[0].session=0;plan.tracks[0].control=0;s.source.plan=plan;scenario=7;assert(!optical_identity(&s));
    puts("PASS repair identity: interior anchors, bounded unreadable alternatives, exact retries/refusal, wrong-disc, CD data/audio, partial binding");
    puts("PASS repair catalogue: every grade remains distinct; cancellation has no completed evidence");
    return 0;
}
