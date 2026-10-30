/* SPDX-License-Identifier: GPL-3.0-only */
#include "kui/drive_speed_probe.h"
#include "kui/recovery_checks.h"
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct speed_probe {
    const struct kui_capture_ops *ops;
    int (*select)(void *,unsigned);
    void (*progress)(void *,const struct kui_drive_speed_probe_status *);
    struct kui_drive_speed_probe_status *out;
    uint64_t started;
    uint32_t fad[KUI_SPEED_PROBE_REFERENCES];
    uint8_t digest[KUI_SPEED_PROBE_REFERENCES][32];
    uint8_t storage[KUI_CAPTURE_CHUNK*KUI_RAW_BYTES+31u];
    uint8_t *data;
};
_Static_assert(sizeof(struct speed_probe)<=75u*1024u,
    "Read-only speed probe must remain below 75 KiB of owned heap");

static void publish(struct speed_probe *p) {
    if(p->progress)p->progress(p->ops->ctx,p->out);
}
static bool failed(struct speed_probe *p,const char *message) {
    p->out->failed=true;
    snprintf(p->out->message,sizeof(p->out->message),"%s",message);
    p->ops->log("SPEED PROBE failed: %s",message);publish(p);return false;
}
static bool admitted(struct speed_probe *p) {
    if(p->out->failed || p->out->stopped || p->out->budget_expired)return false;
    if(p->ops->cancelled && p->ops->cancelled(p->ops->ctx)) {
        p->out->stopped=true;
        snprintf(p->out->message,sizeof(p->out->message),"Speed probe stopped; saved jobs unchanged");
        publish(p);return false;
    }
    uint64_t now=p->ops->now_ms(p->ops->ctx);
    if(now>=p->started && now-p->started>=KUI_SPEED_PROBE_BUDGET_MS) {
        p->out->budget_expired=true;
        snprintf(p->out->message,sizeof(p->out->message),"Speed probe budget reached; saved jobs unchanged");
        publish(p);return false;
    }
    return true;
}
static int select_mode(struct speed_probe *p,unsigned selection) {
    if(!admitted(p))return -1;
    p->out->selection=selection;p->out->tried[selection]=true;
    snprintf(p->out->message,sizeof(p->out->message),"Checking speed setting %u/%u",selection+1,KUI_SPEED_PROBE_PROFILES);
    publish(p);if(!admitted(p))return -1;
    int result=p->select(p->ops->ctx,selection);
    p->ops->log("SPEED PROBE select=%u result=%d",selection,result);
    if(!admitted(p))return -1;
    if(result!=0 && result!=1) {
        failed(p,"Speed settings unavailable; no further optical reads");return -1;
    }
    p->out->verified[selection]=result==1;
    return result;
}

enum window_result { WINDOW_GOOD,WINDOW_UNREADABLE,WINDOW_HALTED };
static enum window_result read_window(struct speed_probe *p,uint32_t fad,
    uint8_t digest[32],uint64_t *elapsed) {
    struct kui_sha256 hash;kui_sha256_init(&hash);*elapsed=0;
    for(unsigned at=0;at<KUI_SPEED_PROBE_WINDOW_SECTORS;at+=KUI_CAPTURE_CHUNK) {
        if(!admitted(p))return WINDOW_HALTED;
        p->out->current_fad=fad+at;publish(p);
        if(!admitted(p))return WINDOW_HALTED;
        uint64_t before=p->ops->now_ms(p->ops->ctx);
        enum kui_read_result result=p->ops->read(p->ops->ctx,fad+at,KUI_CAPTURE_CHUNK,p->data);
        uint64_t after=p->ops->now_ms(p->ops->ctx);
        if(after>=before)*elapsed+=after-before;
        if(result==KUI_READ_FATAL) {
            failed(p,"Optical command failed fatally; speed probe stopped");return WINDOW_HALTED;
        }
        if(!admitted(p))return WINDOW_HALTED;
        unsigned checks=0;
        if(result==KUI_READ_OK || result==KUI_READ_PARTIAL) {
            for(unsigned n=0;n<KUI_CAPTURE_CHUNK;++n)
                checks|=kui_recovery_sector_check(p->data+n*KUI_RAW_BYTES,KUI_RAW_BYTES,fad+at+n);
        } else checks=KUI_RECOVERY_SECTOR_INPUT;
        p->ops->log("SPEED PROBE chunk selection=%u FAD=%" PRIu32 " sectors=%u result=%u checks=%08x read_ms=%" PRIu64,
            p->out->selection,fad+at,KUI_CAPTURE_CHUNK,(unsigned)result,checks,after>=before?after-before:0);
        if(result!=KUI_READ_OK && result!=KUI_READ_PARTIAL)return WINDOW_UNREADABLE;
        if(checks)return WINDOW_UNREADABLE;
        kui_sha256_update(&hash,p->data,KUI_CAPTURE_CHUNK*KUI_RAW_BYTES);
        if(!admitted(p))return WINDOW_HALTED;
    }
    kui_sha256_digest(&hash,digest);return WINDOW_GOOD;
}
static const struct kui_capture_track *eligible_track(const struct kui_capture_plan *plan) {
    if(!plan || plan->profile!=KUI_CAPTURE_PROFILE_GD || !plan->count || plan->count>99)return NULL;
    const struct kui_capture_track *selected=NULL;uint64_t bytes=0;
    for(unsigned i=0;i<plan->count;++i) {
        const struct kui_capture_track *t=&plan->tracks[i];
        if(t->number!=i+1 || (t->control!=0 && t->control!=4) || t->session>1 ||
           t->start<150 || t->end<=t->start || t->toc_end<t->end ||
           t->toc_end>KUI_RECOVERY_FAD_MAX+1u || (i && t->start<plan->tracks[i-1].end))return NULL;
        bytes+=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
        if(t->session==1 && t->control==4 && t->end-t->start>=192u &&
           (!selected || t->end-t->start>selected->end-selected->start))selected=t;
    }
    return bytes==plan->bytes?selected:NULL;
}
bool kui_drive_speed_probe_run(const struct kui_capture_plan *plan,
    const struct kui_capture_ops *ops,int (*select)(void *,unsigned),
    void (*progress)(void *,const struct kui_drive_speed_probe_status *),
    struct kui_drive_speed_probe_status *out) {
    if(!out)return false;
    memset(out,0,sizeof(*out));out->total=KUI_SPEED_PROBE_PROFILES;
    const struct kui_capture_track *track=eligible_track(plan);
    if(!track || !ops || !ops->read || !ops->log || !ops->now_ms || !select) {
        out->failed=true;snprintf(out->message,sizeof(out->message),"Speed probe requires a GD data track of at least 192 sectors");return false;
    }
    struct speed_probe *p=malloc(sizeof(*p));
    if(!p) {out->failed=true;snprintf(out->message,sizeof(out->message),"Not enough memory for bounded speed probe");return false;}
    memset(p,0,sizeof(*p));p->ops=ops;p->select=select;p->progress=progress;p->out=out;
    p->data=(uint8_t *)(((uintptr_t)p->storage+31u)&~(uintptr_t)31u);
    p->started=ops->now_ms(ops->ctx);bool ok=false;
    ops->log("SPEED PROBE start: original speed plus codes=0..7; retries=1; read-only; budget_ms=%u",KUI_SPEED_PROBE_BUDGET_MS);
    ops->log("SPEED PROBE track=%" PRIu32 " FAD=%" PRIu32 "..%" PRIu32 "; cache independence and physical spindle speed unproven",
        track->number,track->start,track->end-1u);
    if(select_mode(p,0)!=1) {
        if(admitted(p))failed(p,"Original-speed one-retry mode could not be verified");
        goto done;
    }
    static const unsigned eighths[]={0,1,3,5,7,8};
    uint32_t span=track->end-track->start-KUI_SPEED_PROBE_WINDOW_SECTORS;
    for(unsigned candidate=0;candidate<6 && out->references<KUI_SPEED_PROBE_REFERENCES;++candidate) {
        if(!admitted(p))goto done;
        uint32_t fad=track->start+(uint32_t)((uint64_t)span*eighths[candidate]/8u);
        uint8_t digest[32];uint64_t elapsed;
        snprintf(out->message,sizeof(out->message),"Finding known-good data windows: %u/%u",out->references,KUI_SPEED_PROBE_REFERENCES);
        enum window_result result=read_window(p,fad,digest,&elapsed);
        if(result==WINDOW_HALTED)goto done;
        ops->log("SPEED PROBE reference candidate=%u FAD=%" PRIu32 " valid=%u read_ms=%" PRIu64,
            candidate,fad,result==WINDOW_GOOD?1u:0u,elapsed);
        if(result==WINDOW_GOOD) {
            unsigned n=out->references++;char hex[65];p->fad[n]=fad;memcpy(p->digest[n],digest,32);kui_hex(digest,32,hex);
            ops->log("SPEED PROBE reference=%u FAD=%" PRIu32 " sectors=%u SHA256=%s",n,fad,KUI_SPEED_PROBE_WINDOW_SECTORS,hex);
        }
        publish(p);
    }
    if(out->references!=KUI_SPEED_PROBE_REFERENCES) {
        failed(p,"Could not find three fully checked data windows; use a healthier disc");goto done;
    }
    for(unsigned selection=0;selection<KUI_SPEED_PROBE_PROFILES;++selection) {
        int selected=select_mode(p,selection);if(selected<0)goto done;
        if(!selection && !selected) {
            failed(p,"Original-speed mode lost verification; comparison stopped");goto done;
        }
        if(selected) {
            for(unsigned n=0;n<KUI_SPEED_PROBE_REFERENCES;++n) {
                if(!admitted(p))goto done;
                unsigned reference=(selection+n)%KUI_SPEED_PROBE_REFERENCES;
                uint8_t digest[32];uint64_t elapsed;
                snprintf(out->message,sizeof(out->message),"Speed setting %u/%u: checking window %u/%u",
                    selection+1,KUI_SPEED_PROBE_PROFILES,n+1,KUI_SPEED_PROBE_REFERENCES);
                ++out->attempted[selection];
                enum window_result result=read_window(p,p->fad[reference],digest,&elapsed);
                out->read_ms[selection]+=elapsed;
                if(result==WINDOW_HALTED)goto done;
                if(result==WINDOW_GOOD) {
                    if(memcmp(digest,p->digest[reference],32)) {
                        char expected[65],observed[65];
                        kui_hex(p->digest[reference],32,expected);kui_hex(digest,32,observed);
                        ops->log("SPEED PROBE digest changed selection=%u reference=%u FAD=%" PRIu32 " expected=%s observed=%s",
                            selection,reference,p->fad[reference],expected,observed);
                        failed(p,"Checked optical bytes changed; original references retained");goto done;
                    }
                    ++out->matched[selection];
                }
                ops->log("SPEED PROBE compare selection=%u reference=%u FAD=%" PRIu32 " matched=%u read_ms=%" PRIu64,
                    selection,reference,p->fad[reference],result==WINDOW_GOOD?1u:0u,elapsed);
                if(!selection && result!=WINDOW_GOOD) {
                    failed(p,"Original-speed data windows were unstable; comparison stopped");goto done;
                }
            }
        }
        ++out->done;publish(p);
    }
    if(!admitted(p))goto done;
    out->complete=true;
    snprintf(out->message,sizeof(out->message),"Speed probe finished; saved salvage jobs unchanged");ok=true;
done:
    for(unsigned selection=0;selection<KUI_SPEED_PROBE_PROFILES;++selection)
        ops->log("SPEED PROBE summary selection=%u tried=%u verified=%u windows=%u/%u read_ms=%" PRIu64,
            selection,out->tried[selection]?1u:0u,out->verified[selection]?1u:0u,out->matched[selection],out->attempted[selection],out->read_ms[selection]);
    ops->log("SPEED PROBE end complete=%u stopped=%u failed=%u budget_expired=%u; cache/spindle independence unproven",
        out->complete?1u:0u,out->stopped?1u:0u,out->failed?1u:0u,out->budget_expired?1u:0u);
    publish(p);free(p);return ok;
}
