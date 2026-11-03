/* SPDX-License-Identifier: GPL-3.0-only */
/* A sampled comparison of read approaches, not an automatic policy change.
 * Every method sees the same frozen targets, including targets another method
 * has already repaired. Window neighbors are context, never extra repairs. */
#define READ_TRIAL_BUDGET_MS 600000u
static const char *read_trial_profile_name(unsigned profile) {
    static const char *const names[]={"Single sector","Leading 4","Centered 16","Reseek + leading 16"};
    return profile<KUI_SALVAGE_TRIAL_PROFILES?names[profile]:"Invalid";
}
static bool read_trial_budget(struct salvage *s,uint64_t started) {
    uint64_t now=s->ops->now_ms(s->ops->ctx);
    if(now>=started && now-started>=READ_TRIAL_BUDGET_MS)s->status->trial_budget_expired=true;
    return s->status->trial_budget_expired;
}
/* All arithmetic is relative to the admitted DATA track. Short tracks and
 * targets at either boundary cannot request bytes outside the saved plan. */
static void read_trial_window(const struct kui_capture_track *track,const struct target *target,
    unsigned profile,uint32_t *begin,unsigned *count) {
    uint32_t length=track->end-track->start,sector=target->sector;
    if(profile==0) {*begin=sector;*count=1;return;}
    unsigned width=profile==1?4u:16u;
    if(profile==2) {
        *count=length<width?(unsigned)length:width;
        *begin=sector>8u?sector-8u:0u;
        if(*begin>length-*count)*begin=length-*count;
    } else {
        *count=sector+1u<width?(unsigned)(sector+1u):width;
        *begin=sector+1u-*count;
    }
}
/* Saved DATA anchors are used as actual reads, not blind SEEK commands.
 * Return the farthest eligible anchor; a tie deterministically chooses zero. */
static int read_trial_anchor(struct salvage *s,uint32_t target_fad) {
    int selected=-1;uint32_t distance=0;
    for(unsigned a=0;a<2;++a) {
        uint32_t fad=get32(s->header+24+a*4);int track=anchor_track(s,fad);
        if(track<0 || s->plan->tracks[track].control!=4)continue;
        uint32_t delta=fad>target_fad?fad-target_fad:target_fad-fad;
        if(selected<0 || delta>distance) {selected=(int)a;distance=delta;}
    }
    return selected;
}
/* An unreadable/invalid anchor merely makes this method fail for this sample.
 * A valid different anchor is a disc-identity failure and stops all methods. */
static bool read_trial_approach(struct salvage *s,unsigned anchor,unsigned sample) {
    uint32_t fad=get32(s->header+24+anchor*4);int track=anchor_track(s,fad);
    if(track<0 || s->plan->tracks[track].control!=4)return fail(s,"Invalid read-trial data anchor");
    s->status->track=(unsigned)track+1;
    snprintf(s->status->message,sizeof(s->status->message),"Read trial: reading saved anchor before target %u",sample+1);
    /* This is identity/approach work, excluded from ordinary repair counters.
     * The trial's own timing and attempts still include the complete read. */
    s->recovering=true;
    enum kui_read_result result=optical(s,fad,1,s->data,1,1,1,1);
    s->recovering=false;
    unsigned check=UINT_MAX;bool exact=false;
    if(!s->failed && !stop(s) && (result==KUI_READ_OK || result==KUI_READ_PARTIAL)) {
        check=sector_check(s,s->data,fad);
        if(!check) {
            uint8_t digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);
            kui_sha256_update(&hash,s->data,KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
            exact=!memcmp(digest,s->header+32+anchor*32,32);
            if(!exact)fail(s,"Read-trial approach anchor differs from saved disc identity; repairs retained");
        }
    }
    s->ops->log("READTRIAL approach sample=%u anchor=%u FAD=%" PRIu32 " result=%u checks=%08x exact=%u",
        sample,anchor,fad,(unsigned)result,check,exact?1u:0u);
    if(!s->failed && !stop(s))read_outcome(s,exact?KUI_SALVAGE_READ_ACCEPTED:KUI_SALVAGE_READ_DEFERRED);
    return exact && !s->failed && !stop(s);
}
static bool read_trial(struct salvage *s) {
    struct kui_salvage_status *o=s->status;
    unsigned indices[KUI_SALVAGE_READ_TRIAL_TARGETS],ranks[KUI_SALVAGE_READ_TRIAL_TARGETS];
    struct target frozen[KUI_SALVAGE_READ_TRIAL_TARGETS];
    unsigned available=0,count=0;bool ok=false;
    uint64_t started=s->ops->now_ms(s->ops->ctx);
    o->trial_active=true;o->trial_read_strategy=true;o->pass=0;s->read_failures=0;
    s->ops->log("READTRIAL start: fixed data targets<=16 profiles=4 soft_budget_ms=600000; current drive settings; no automatic INIT");
    char hex[65];kui_hex(s->identity,32,hex);
    s->ops->log("READTRIAL job=%s identity=%s; profile order rotates per sample; neighbors are context only",o->job,hex);
    for(unsigned i=0;i<s->target_count;++i) {
        if(stop(s) || read_trial_budget(s,started))goto done;
        struct target t;if(!target_get(s,i,&t))goto done;
        if(!t.repaired && s->plan->tracks[t.track].control==4)++available;
    }
    if(!available) {
        if(o->remaining) {fail(s,"No unresolved data sectors available for a read strategy trial");goto done;}
        ok=true;goto done;
    }
    o->trial_sample_count=available<KUI_SALVAGE_READ_TRIAL_TARGETS?available:KUI_SALVAGE_READ_TRIAL_TARGETS;
    for(unsigned i=0;i<o->trial_sample_count;++i)
        ranks[i]=o->trial_sample_count==1?0:(unsigned)((uint64_t)i*(available-1)/(o->trial_sample_count-1));
    unsigned rank=0;
    for(unsigned i=0;i<s->target_count && count<o->trial_sample_count;++i) {
        if(stop(s) || read_trial_budget(s,started))goto done;
        struct target t;if(!target_get(s,i,&t))goto done;
        if(t.repaired || s->plan->tracks[t.track].control!=4)continue;
        if(rank++==ranks[count]) {
            indices[count]=i;frozen[count]=t;
            s->ops->log("READTRIAL sample=%u target=%u track=%" PRIu32 " sector=%" PRIu32 " FAD=%" PRIu32,
                count,i,t.track+1,t.sector,s->plan->tracks[t.track].start+t.sector);
            ++count;
        }
    }
    if(count!=o->trial_sample_count) {fail(s,"Read-trial frozen target set changed");goto done;}
    for(unsigned p=0;p<KUI_SALVAGE_TRIAL_PROFILES;++p)o->trial_qualified[p]=p!=3 || read_trial_anchor(s,0)>=0;
    o->trial_qualification_done=true;o->trial_total=count*KUI_SALVAGE_TRIAL_PROFILES;
    if(!o->trial_qualified[3])s->ops->log("READTRIAL profile=3 unavailable: saved identity has no data anchor; method slots skipped");
    for(unsigned sample=0;sample<count && !s->failed && !stop(s);++sample) {
        for(unsigned turn=0;turn<KUI_SALVAGE_TRIAL_PROFILES;++turn) {
            unsigned p=(sample+turn)%KUI_SALVAGE_TRIAL_PROFILES;o->trial_profile=p;
            if(stop(s) || read_trial_budget(s,started))goto done;
            if(!o->trial_qualified[p]) {
                ++o->trial_done;s->ops->log("READTRIAL skip sample=%u profile=%u: no saved data anchor",sample,p);continue;
            }
            struct target t;if(!target_get(s,indices[sample],&t))goto done;
            if(t.track!=frozen[sample].track || t.sector!=frozen[sample].sector) {
                fail(s,"Frozen read-trial target changed; no repair attempted");goto done;
            }
            const struct kui_capture_track *track=&s->plan->tracks[t.track];
            uint32_t fad=track->start+t.sector,begin;unsigned sectors;
            read_trial_window(track,&t,p,&begin,&sectors);
            unsigned offset=(unsigned)(t.sector-begin);
            o->pass=1;o->track=t.track+1;
            snprintf(o->message,sizeof(o->message),"Read trial %u/%u: %s",o->trial_done+1,o->trial_total,read_trial_profile_name(p));
            update(s,true);
            if(stop(s) || read_trial_budget(s,started))goto done;
            uint64_t before=s->ops->now_ms(s->ops->ctx);
            uint32_t attempts_before=o->attempts;
            bool approached=true;int anchor=-1;
            if(p==3) {
                anchor=read_trial_anchor(s,fad);
                if(anchor<0) {fail(s,"Read-trial saved data anchor disappeared");goto done;}
                approached=read_trial_approach(s,(unsigned)anchor,sample);
            }
            enum kui_read_result result=KUI_READ_RETRY;
            bool target_called=false;
            if(approached && !s->failed && !stop(s) && !read_trial_budget(s,started)) {
                o->track=t.track+1;
                snprintf(o->message,sizeof(o->message),"Read trial %u/%u: %s",o->trial_done+1,o->trial_total,read_trial_profile_name(p));
                memset(s->repair_data,0xa5,(size_t)sectors*KUI_RAW_BYTES);
                s->ops->log("READTRIAL request sample=%u target=%u profile=%u target_FAD=%" PRIu32 " start=%" PRIu32 " count=%u offset=%u anchor=%d",
                    sample,indices[sample],p,fad,track->start+begin,sectors,offset,anchor);
                uint32_t target_attempts=o->attempts;
                result=optical(s,track->start+begin,sectors,s->repair_data,1,1,1,1);
                target_called=o->attempts!=target_attempts;
            }
            uint64_t after=s->ops->now_ms(s->ops->ctx),elapsed=after>=before?after-before:0;
            if(o->attempts==attempts_before)goto done;
            ++o->trial_profile_reads[p];++o->trial_done;o->trial_profile_ms[p]+=elapsed;
            unsigned check=UINT_MAX;bool valid=false,repaired=false;
            if(target_called && !s->failed && !stop(s) && (result==KUI_READ_OK || result==KUI_READ_PARTIAL)) {
                const uint8_t *raw=s->repair_data+(size_t)offset*KUI_RAW_BYTES;
                check=sector_check(s,raw,fad);valid=check==0;
                if(valid)memcpy(s->data,raw,KUI_RAW_BYTES);
            }
            s->ops->log("READTRIAL read sample=%u target=%u profile=%u FAD=%" PRIu32 " start=%" PRIu32 " count=%u offset=%u approach_ok=%u target_called=%u result=%u checks=%08x valid=%u method_ms=%" PRIu64,
                sample,indices[sample],p,fad,track->start+begin,sectors,offset,approached?1u:0u,target_called?1u:0u,(unsigned)result,check,valid?1u:0u,elapsed);
            if(valid) {
                if(t.repaired) {
                    if(!read_target(s,indices[sample]))goto done;
                    if(memcmp(s->scratch,s->data,KUI_RAW_BYTES)) {
                        fail(s,"Valid read-trial reread differs from saved repair; original repair retained");goto done;
                    }
                } else {
                    uint32_t expected=kui_crc32(0,s->data,KUI_RAW_BYTES);
                    if(!store_repair(s,indices[sample],&t,expected))goto done;
                    repaired=true;++o->trial_profile_repairs[p];
                }
                ++o->trial_profile_valid[p];
                if(sectors>1)++o->repair_batch_accepted;else ++o->repair_single_accepted;
                s->read_failures=0;
            }
            if(repaired)s->ops->log("READTRIAL repair sample=%u target=%u profile=%u committed=1",sample,indices[sample],p);
            if(s->failed || stop(s) || o->trial_budget_expired)goto done;
            read_outcome(s,valid?KUI_SALVAGE_READ_ACCEPTED:KUI_SALVAGE_READ_DEFERRED);
        }
    }
    ok=!s->failed && !stop(s);
done:
    if(o->trial_budget_expired && !s->failed && !stop(s))ok=true;
    o->trial_complete=ok && !o->trial_budget_expired;o->trial_active=false;s->read_failures=0;
    for(unsigned p=0;p<KUI_SALVAGE_TRIAL_PROFILES;++p)
        s->ops->log("READTRIAL summary profile=%u name=%s qualified=%u attempts=%" PRIu32 " valid=%" PRIu32 " repairs=%" PRIu32 " method_ms=%" PRIu64,
            p,read_trial_profile_name(p),o->trial_qualified[p]?1u:0u,o->trial_profile_reads[p],o->trial_profile_valid[p],o->trial_profile_repairs[p],o->trial_profile_ms[p]);
    s->ops->log("READTRIAL end samples=%u steps=%u/%u complete=%u budget_expired=%u; only sampled validated targets repaired; physical speed/cache independence unproven",
        o->trial_sample_count,o->trial_done,o->trial_total,o->trial_complete?1u:0u,o->trial_budget_expired?1u:0u);
    update(s,true);return ok;
}
