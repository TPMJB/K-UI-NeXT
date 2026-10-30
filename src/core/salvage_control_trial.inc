/* SPDX-License-Identifier: GPL-3.0-only */
/* Included after the ordinary candidate publication helpers. These trials use
 * the same authority and acceptance checks, but never enter endless recovery.
 * A fixed sample avoids crediting early profiles with an easier shrinking set. */
#define CONTROL_TRIAL_BUDGET_MS 300000u
static const char *trial_profile_name(unsigned profile) {
    static const char *const names[]={"Original","Original speed / retry 1","4x requested / retry 1","2x requested / retry 1"};
    return profile<KUI_SALVAGE_TRIAL_PROFILES?names[profile]:"Invalid";
}
static uint64_t trial_now(struct salvage *s) {return s->ops->now_ms(s->ops->ctx);}
static bool trial_budget(struct salvage *s,uint64_t started) {
    uint64_t now=trial_now(s);
    if(now>=started && now-started>=CONTROL_TRIAL_BUDGET_MS)s->status->trial_budget_expired=true;
    return s->status->trial_budget_expired;
}
static int trial_select(struct salvage *s,unsigned profile) {
    s->status->trial_profile=profile;
    snprintf(s->status->message,sizeof(s->status->message),"Drive trial: selecting %s",trial_profile_name(profile));
    update(s,true);if(stop(s) || s->failed)return -1;
    uint64_t before=trial_now(s);int selected=s->options->trial_select(s->ops->ctx,profile);
    uint64_t after=trial_now(s),elapsed=after>=before?after-before:0;
    s->status->trial_switch_ms[profile]+=elapsed;
    s->ops->log("TRIAL select profile=%u result=%d switch_ms=%" PRIu64,profile,selected,elapsed);
    if(selected!=0 && selected!=1) {
        if(!stop(s))fail(s,"Drive trial settings unavailable; no further trial reads");
        return -1;
    }
    return selected;
}
static bool trial_qualify(struct salvage *s,unsigned profile,uint64_t started) {
    unsigned matched=0;
    for(unsigned anchor=0;anchor<2 && !s->failed && !stop(s);++anchor) {
        if(trial_budget(s,started))return false;
        uint32_t fad=get32(s->header+24+anchor*4);int track=anchor_track(s,fad);
        if(track<0 || s->plan->tracks[track].control!=4)
            return fail(s,"Drive trials require saved data-sector identity anchors");
        s->status->track=(unsigned)track+1;
        snprintf(s->status->message,sizeof(s->status->message),"Qualifying profile %u/4 on saved identity data",profile+1);
        uint64_t before=trial_now(s);
        enum kui_read_result result=optical(s,fad,1,s->data,1,1,1,1);
        uint64_t after=trial_now(s),elapsed=after>=before?after-before:0;
        unsigned check=UINT_MAX;bool exact=false;
        if(!s->failed && !stop(s) && (result==KUI_READ_OK || result==KUI_READ_PARTIAL)) {
            check=sector_check(s,s->data,fad);
            if(!check) {
                uint8_t digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);
                kui_sha256_update(&hash,s->data,KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
                exact=!memcmp(digest,s->header+32+anchor*32,32);
                if(!exact)fail(s,"Drive trial identity bytes differ; committed progress retained");
            }
        }
        if(exact)++matched;
        s->ops->log("TRIAL qualify profile=%u FAD=%" PRIu32 " result=%u checks=%08x exact=%u read_ms=%" PRIu64,
            profile,fad,(unsigned)result,check,exact?1u:0u,elapsed);
        if(!s->failed && !stop(s))read_outcome(s,exact?KUI_SALVAGE_READ_ACCEPTED:KUI_SALVAGE_READ_DEFERRED);
    }
    s->status->trial_qualified[profile]=matched==2;
    return matched==2 && !s->failed && !stop(s);
}
static bool control_trial(struct salvage *s) {
    struct kui_salvage_status *o=s->status;
    unsigned indices[KUI_SALVAGE_TRIAL_TARGETS],ranks[KUI_SALVAGE_TRIAL_TARGETS];
    struct target frozen[KUI_SALVAGE_TRIAL_TARGETS];
    bool enabled[KUI_SALVAGE_TRIAL_PROFILES]={false};bool ok=false;
    unsigned count=0,available=0,qualified=0,admitted[KUI_SALVAGE_TRIAL_PROFILES];
    uint64_t started=trial_now(s);
    o->trial_active=true;o->pass=0;s->read_failures=0;
    s->ops->log("TRIAL start: fixed data targets<=8 profiles=4 soft_budget_ms=300000; no automatic INIT");
    char identity_hex[65];kui_hex(s->identity,32,identity_hex);
    s->ops->log("TRIAL job=%s identity=%s",o->job,identity_hex);
    for(unsigned i=0;i<s->target_count;++i) {
        if(stop(s) || trial_budget(s,started))goto done;
        struct target t;if(!target_get(s,i,&t))goto done;
        if(!t.repaired && s->plan->tracks[t.track].control==4)++available;
    }
    if(!available) {
        if(o->remaining) {fail(s,"No unresolved data sectors available for a drive-control trial");goto done;}
        ok=true;goto done;
    }
    o->trial_sample_count=available<KUI_SALVAGE_TRIAL_TARGETS?available:KUI_SALVAGE_TRIAL_TARGETS;
    for(unsigned i=0;i<o->trial_sample_count;++i)
        ranks[i]=o->trial_sample_count==1?0:(unsigned)((uint64_t)i*(available-1)/(o->trial_sample_count-1));
    unsigned rank=0;
    for(unsigned i=0;i<s->target_count && count<o->trial_sample_count;++i) {
        if(stop(s) || trial_budget(s,started))goto done;
        struct target t;if(!target_get(s,i,&t))goto done;
        if(t.repaired || s->plan->tracks[t.track].control!=4)continue;
        if(rank++==ranks[count]) {indices[count]=i;frozen[count]=t;++count;}
    }
    for(unsigned p=0;p<KUI_SALVAGE_TRIAL_PROFILES && !s->failed && !stop(s);++p) {
        if(trial_budget(s,started))goto done;
        int selected=trial_select(s,p);
        if(selected<0)goto done;
        if(!selected) {
            s->ops->log("TRIAL profile=%u skipped: mode unsupported; prior tuple restored",p);
            if(!p) {fail(s,"Original drive settings could not be qualified; trial refused");goto done;}
            continue;
        }
        if(trial_budget(s,started))goto done;
        enabled[p]=trial_qualify(s,p,started);
        if(s->failed || stop(s) || o->trial_budget_expired)goto done;
        if(!enabled[p] && !p) {fail(s,"Original drive settings failed identity qualification; trial refused");goto done;}
        if(enabled[p])admitted[qualified++]=p;
    }
    o->trial_qualification_done=true;o->trial_total=count*qualified;
    for(unsigned sample=0;sample<count && !s->failed && !stop(s);++sample) {
        for(unsigned turn=0;turn<qualified;++turn) {
            unsigned p=admitted[(sample+turn)%qualified];
            if(stop(s) || trial_budget(s,started))goto done;
            if(!enabled[p]) {
                ++o->trial_done;s->ops->log("TRIAL skip sample=%u profile=%u: profile previously unavailable",sample,p);continue;
            }
            int selected=trial_select(s,p);
            if(selected<0)goto done;
            if(!selected) {
                enabled[p]=false;++o->trial_done;
                s->ops->log("TRIAL skip sample=%u profile=%u: mode unsupported; prior tuple restored",sample,p);continue;
            }
            if(stop(s) || trial_budget(s,started))goto done;
            struct target t;if(!target_get(s,indices[sample],&t))goto done;
            if(t.track!=frozen[sample].track || t.sector!=frozen[sample].sector) {
                fail(s,"Frozen drive-trial target changed; no repair attempted");goto done;
            }
            uint32_t fad=s->plan->tracks[t.track].start+t.sector;
            o->pass=1;o->track=t.track+1;
            snprintf(o->message,sizeof(o->message),"Drive trial %u/%u: %s",o->trial_done+1,o->trial_total,trial_profile_name(p));
            uint64_t before=trial_now(s);
            uint32_t attempts_before=o->attempts;
            enum kui_read_result result=optical(s,fad,1,s->data,1,1,1,1);
            uint64_t after=trial_now(s),elapsed=after>=before?after-before:0;
            if(o->attempts==attempts_before)goto done; /* Stop before adapter admission. */
            ++o->trial_profile_reads[p];++o->trial_done;o->trial_profile_ms[p]+=elapsed;
            unsigned check=UINT_MAX;bool valid=false,repaired=false;
            if(!s->failed && !stop(s) && (result==KUI_READ_OK || result==KUI_READ_PARTIAL)) {
                check=sector_check(s,s->data,fad);valid=check==0;
            }
            if(valid) {
                if(t.repaired) {
                    if(!read_target(s,indices[sample]))goto done;
                    if(memcmp(s->scratch,s->data,KUI_RAW_BYTES)) {
                        fail(s,"Valid trial reread differs from saved repair; original repair retained");goto done;
                    }
                } else {
                    uint32_t expected=kui_crc32(0,s->data,KUI_RAW_BYTES);
                    if(!store_repair(s,indices[sample],&t,expected))goto done;
                    repaired=true;++o->trial_profile_repairs[p];
                }
                ++o->trial_profile_valid[p];++o->repair_single_accepted;s->read_failures=0;
            }
            s->ops->log("TRIAL read sample=%u target=%u profile=%u FAD=%" PRIu32 " result=%u checks=%08x valid=%u repaired=%u read_ms=%" PRIu64,
                sample,indices[sample],p,fad,(unsigned)result,check,valid?1u:0u,repaired?1u:0u,elapsed);
            if(s->failed || stop(s))goto done;
            read_outcome(s,valid?KUI_SALVAGE_READ_ACCEPTED:KUI_SALVAGE_READ_DEFERRED);
        }
    }
    ok=!s->failed && !stop(s);
done:
    if(o->trial_budget_expired && !s->failed && !stop(s))ok=true;
    o->trial_complete=ok && !o->trial_budget_expired;o->trial_active=false;s->read_failures=0;
    for(unsigned p=0;p<KUI_SALVAGE_TRIAL_PROFILES;++p)
        s->ops->log("TRIAL summary profile=%u qualified=%u reads=%" PRIu32 " valid=%" PRIu32 " repairs=%" PRIu32 " read_ms=%" PRIu64 " switch_ms=%" PRIu64,
            p,o->trial_qualified[p]?1u:0u,o->trial_profile_reads[p],o->trial_profile_valid[p],o->trial_profile_repairs[p],o->trial_profile_ms[p],o->trial_switch_ms[p]);
    s->ops->log("TRIAL end samples=%u steps=%u/%u budget_expired=%u; saved repairs retained; physical speed/cache independence unproven",
        o->trial_sample_count,o->trial_done,o->trial_total,o->trial_budget_expired?1u:0u);
    update(s,true);return ok;
}
