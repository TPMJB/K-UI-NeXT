/* SPDX-License-Identifier: GPL-3.0-only */
/* Repair-copy v2: immutable source binding, alternating checked checkpoints,
 * and one replayable patch. Originals are never opened for writing. */
static uint32_t repair_get32(const uint8_t *p) {return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint64_t repair_get64(const uint8_t *p) {return repair_get32(p)|(uint64_t)repair_get32(p+4)<<32;}
static void repair_put64(uint8_t *p,uint64_t n) {put32(p,(uint32_t)n);put32(p+4,(uint32_t)(n>>32));}
static bool repair_seek(struct recovery_copy *s,FIL *f,uint64_t at) {
    if(f_lseek(f,(FSIZE_t)at)!=FR_OK || f_tell(f)!=at)return fail(s,"Repair checkpoint seek failed");
    return true;
}
static bool capture_identity(struct recovery_copy *s,uint8_t digest[32]) {
    const struct kui_capture_plan *p=s->optical_plan;
    if(!p || !p->count || p->count>99)return fail(s,"Invalid prepared capture plan");
    struct kui_sha256 hash;kui_sha256_init(&hash);
    const char *profile=kui_capture_profile_name(p);kui_sha256_update(&hash,profile,strlen(profile));hash32(&hash,p->count);
    uint64_t bytes=0;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *t=&p->tracks[i];
        if(t->number!=i+1 || (t->control!=0 && t->control!=4) || t->end<=t->start ||
           t->start<150 || t->end>KUI_RECOVERY_FAD_MAX+1u || t->toc_end<t->end ||
           t->toc_end>KUI_RECOVERY_FAD_MAX+1u || t->session>1 || (i && t->start<p->tracks[i-1].end))
            return fail(s,"Invalid prepared capture ranges");
        bytes+=(uint64_t)(t->end-t->start)*KUI_RAW_BYTES;
        hash32(&hash,t->number);hash32(&hash,t->control);hash32(&hash,t->session);hash32(&hash,t->start);hash32(&hash,t->end);hash32(&hash,t->toc_end);
        uint32_t points[3]={t->start,t->start+(t->end-t->start-1)/2,t->end-1};
        for(unsigned n=0;n<3;++n) {
            if(!optical_identity_read(s,points[n],t->control==4,s->candidate)) {if(!s->failed && !cancelled(s))s->identity_unreadable=true;return false;}
            hash32(&hash,points[n]);kui_sha256_update(&hash,s->candidate,KUI_RAW_BYTES);
        }
    }
    if(bytes!=p->bytes)return fail(s,"Prepared capture byte count is invalid");
    kui_sha256_digest(&hash,digest);return true;
}
static bool load_partial_source(struct recovery_copy *s) {
    uint8_t identity[32]={0};bool exact=capture_identity(s,identity);
    if(!exact && (s->failed || cancelled(s) || !s->identity_unreadable))return false;
    if(!exact && (!s->repair_options->allow_saved_identity || s->in_place))return fail(s,"Exact partial capture identity unreadable; use explicit saved-anchor separate-copy repair");
    struct kui_checkpoint best={0},candidate;bool found=false;
    for(unsigned slot=0;slot<2;++slot) {
        source_path(s,slot?"checkpoint-b.bin":"checkpoint-a.bin");
        if(s->in_place && s->inplace_marker_version==2) {
            bool present=false;if(!inplace_original_path(s,slot+1u,&present))return false;if(!present)continue;
        } else if(s->in_place) {
            char original[KUI_DEST_PATH_CAP];snprintf(original,sizeof(original),"0:%s/%s.kui-repair-original",s->directory,slot?"checkpoint-b.bin":"checkpoint-a.bin");FILINFO backup;
            FRESULT r=f_stat(original,&backup);if(r==FR_OK)snprintf(s->path,sizeof(s->path),"%s",original);else if(r!=FR_NO_FILE)return fail(s,"Cannot inspect retained partial checkpoint");
        }
        FIL f;
        FRESULT r=f_open(&f,s->path,FA_READ);if(r==FR_NO_FILE)continue;
        if(r!=FR_OK)return fail(s,"Cannot read partial capture checkpoint");
        bool valid=f_size(&f)==KUI_CHECKPOINT_BYTES;
        if(valid)valid=exact_read(s,&f,s->state,KUI_CHECKPOINT_BYTES);
        if(!close_file(s,&f,true))return false;
        if(s->failed)return false;
        if(valid && (exact?kui_checkpoint_decode(s->state,s->optical_plan,identity,&candidate):kui_saved_checkpoint_decode(s->state,s->optical_plan,&candidate)) && (!found || candidate.sequence>best.sequence)) {best=candidate;found=true;}
    }
    if(!found)return fail(s,"No valid matching partial capture checkpoint; original files preserved");
    if(!exact)memcpy(identity,best.identity,32);
    if(best.format==KUI_CAPTURE_FORMAT_GDI_DIRECT_2048)return fail(s,"Direct cooked partial jobs require cooked recovery; raw repair refused");
    s->partial_source=true;s->manifest_present=true;s->source.plan=*s->optical_plan;
    s->manifest.plan=*s->optical_plan;s->manifest.crc_only=best.crc_only;s->manifest.format=best.format;
    snprintf(s->manifest.gdi,sizeof(s->manifest.gdi),"%s",best.format==KUI_CAPTURE_FORMAT_GDI?"disc.gdi":KUI_CAPTURE_INTERNAL_GDI);
    snprintf(s->manifest.cue,sizeof(s->manifest.cue),"disc.cue");memcpy(s->manifest.identity,identity,32);
    for(unsigned i=0;i<s->source.plan.count;++i) {
        char name[32];track_name(name,i,&s->source.plan.tracks[i]);snprintf(s->source.files[i],sizeof(s->source.files[i]),"%s",name);
        s->available[i]=best.track[i].sectors;s->manifest.track[i].crc32=best.track[i].crc32;
        memcpy(s->manifest.track[i].sha256,best.track[i].sha256,32);s->manifest.track[i].sector_mode=best.track[i].sector_mode;
        source_path(s,name);FILINFO info;FRESULT r=f_stat(s->path,&info);
        if((r==FR_NO_FILE && s->available[i]) || (r!=FR_OK && r!=FR_NO_FILE) ||
           (r==FR_OK && ((info.fattrib&AM_DIR) || (uint64_t)info.fsize<(uint64_t)s->available[i]*KUI_RAW_BYTES)))
            return fail(s,"Partial source is shorter than its committed checkpoint");
    }
    s->repair->tracks=s->source.plan.count;s->repair->total=s->source.plan.bytes;s->repair->partial_source=true;
    return true;
}
static bool source_fingerprint(struct recovery_copy *s,uint8_t digest[32]) {
    struct kui_sha256 binding;kui_sha256_init(&binding);
    kui_sha256_update(&binding,"KUI repair source v2",20);kui_sha256_update(&binding,s->directory,strlen(s->directory)+1);
    if(s->legacy)kui_sha256_update(&binding,s->legacy->binding,32);
    if(s->saved_identity_active) {uint8_t encoded[KUI_SAVED_IDENTITY_BYTES];kui_saved_identity_encode(&s->saved_identity,encoded);kui_sha256_update(&binding,encoded,sizeof(encoded));}
    hash32(&binding,s->manifest_present);hash32(&binding,s->partial_source);hash32(&binding,s->source.plan.count);
    if(s->manifest_present)kui_sha256_update(&binding,s->manifest.identity,32);
    for(unsigned i=0;i<s->source.plan.count;++i) {
        const struct kui_capture_track *t=&s->source.plan.tracks[i];
        hash32(&binding,t->number);hash32(&binding,t->control);hash32(&binding,t->session);hash32(&binding,t->start);hash32(&binding,t->end);hash32(&binding,t->toc_end);
        kui_sha256_update(&binding,s->source.files[i],strlen(s->source.files[i])+1);hash32(&binding,s->available[i]);
        if(s->manifest_present) {hash32(&binding,s->manifest.track[i].crc32);kui_sha256_update(&binding,s->manifest.track[i].sha256,32);}
        struct kui_sha256 hash;kui_sha256_init(&hash);uint32_t crc=0;uint64_t left=(uint64_t)s->available[i]*KUI_RAW_BYTES;
        if(left) {
            source_path(s,s->source.files[i]);FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot fingerprint original source");
            bool ok=true;while(left && ok) {
                if(cancelled(s)) {ok=false;break;}
                unsigned n=(unsigned)(left>sizeof(s->data)?sizeof(s->data):left);
                ok=exact_read(s,&f,s->data,n);if(!ok)break;
                if(!repair_baseline_scan(s,i,(uint64_t)s->available[i]*KUI_RAW_BYTES-left,s->data,n)) {ok=false;break;}
                crc=kui_crc32(crc,s->data,n);kui_sha256_update(&hash,s->data,n);left-=n;
            }
            if(!close_file(s,&f,ok))return false;
        }
        s->initial_crc[i]=crc;kui_sha256_digest(&hash,s->initial_sha[i]);
        repair_baseline_hash(s,i,s->available[i],crc,&hash);
        hash32(&binding,crc);kui_sha256_update(&binding,s->initial_sha[i],32);
    }
    kui_sha256_digest(&binding,digest);return true;
}
static bool repair_store(struct recovery_copy *s,const char *name,const uint8_t *data,unsigned bytes) {
    job_path(s,name);FIL f;if(f_open(&f,s->path,FA_READ|FA_WRITE|FA_CREATE_ALWAYS)!=FR_OK)return fail(s,"Cannot write repair checkpoint");
    bool ok=exact_write(s,&f,data,bytes) && sync_file(s,&f);
    if(ok)ok=repair_seek(s,&f,0);
    unsigned at=0;uint8_t check[512];
    while(ok && at<bytes) {unsigned n=bytes-at>sizeof(check)?sizeof(check):bytes-at;ok=exact_read(s,&f,check,n) && !memcmp(check,data+at,n);at+=n;}
    if(ok && repair_get32(data+bytes-4)!=kui_crc32(0,data,bytes-4))ok=false;
    if(!ok && !s->failed)fail(s,"Repair checkpoint readback mismatch");
    return close_file(s,&f,ok);
}
static bool repair_commit(struct recovery_copy *s) {
    memset(s->state,0,sizeof(s->state));memcpy(s->state,"KUIRPC2",8);put32(s->state+8,2);put32(s->state+12,REPAIR_STATE_BYTES);
    repair_put64(s->state+16,s->state_sequence+1);memcpy(s->state+24,s->binding,32);
    put32(s->state+56,(s->partial_source?1u:0u)|(s->in_place?2u:0u));put32(s->state+60,s->source.plan.count);repair_put64(s->state+64,s->repair->done);
    put32(s->state+72,s->repair->suspects);put32(s->state+76,s->repair->repaired);put32(s->state+80,s->repair->unresolved);
    repair_put64(s->state+88,s->evidence_records);repair_put64(s->state+96,f_tell(&s->evidence));
    for(unsigned i=0;i<s->source.plan.count;++i) {
        uint8_t *p=s->state+128+i*80;put32(p,s->committed[i]);put32(p+4,s->crc[i]);memcpy(p+8,s->sha[i],32);
        put32(p+40,s->initial_crc[i]);memcpy(p+44,s->initial_sha[i],32);put32(p+76,s->available[i]);
        if(s->sha_valid[i])s->state[8064+i/8]|=(uint8_t)(1u<<(i%8));
    }
    put32(s->state+REPAIR_STATE_BYTES-4,kui_crc32(0,s->state,REPAIR_STATE_BYTES-4));
    if(!repair_store(s,((s->state_sequence+1)&1)?"resume-a.bin":"resume-b.bin",s->state,sizeof(s->state)))return false;
    ++s->state_sequence;s->committed_evidence=f_tell(&s->evidence);s->committed_records=s->evidence_records;return true;
}
static bool repair_load_checkpoint(struct recovery_copy *s) {
    uint64_t best=0;uint8_t *selected=(uint8_t *)s->metadata;
    for(unsigned slot=0;slot<2;++slot) {
        job_path(s,slot?"resume-b.bin":"resume-a.bin");FIL f;FRESULT r=f_open(&f,s->path,FA_READ);if(r==FR_NO_FILE)continue;
        if(r!=FR_OK)return fail(s,"Cannot open retained repair checkpoint");
        bool ok=f_size(&f)==REPAIR_STATE_BYTES;if(ok)ok=exact_read(s,&f,s->state,REPAIR_STATE_BYTES);
        if(!close_file(s,&f,true))return false;
        if(s->failed)return false;
        bool valid=ok && !memcmp(s->state,"KUIRPC2",8) && repair_get32(s->state+8)==2 && repair_get32(s->state+12)==REPAIR_STATE_BYTES &&
            !memcmp(s->state+24,s->binding,32) && repair_get32(s->state+56)==((s->partial_source?1u:0u)|(s->in_place?2u:0u)) && repair_get32(s->state+60)==s->source.plan.count &&
            repair_get32(s->state+REPAIR_STATE_BYTES-4)==kui_crc32(0,s->state,REPAIR_STATE_BYTES-4);
        uint64_t sequence=valid?repair_get64(s->state+16):0;
        if(sequence>best) {best=sequence;memcpy(selected,s->state,REPAIR_STATE_BYTES);}
    }
    if(!best) {
        /* A durable binding may precede its first checkpoint. Bootstrap only
         * when there cannot be any committed copied or candidate bytes. */
        FILINFO info;char name[32];
        if(!s->in_place)for(unsigned i=0;i<s->source.plan.count;++i) {track_name(name,i,&s->source.plan.tracks[i]);job_path(s,name);if(f_stat(s->path,&info)!=FR_NO_FILE)return fail(s,"No valid repair checkpoint beside copied data; copy preserved");}
        if(s->in_place) {
            source_path(s,".kui-inplace-active");if(f_stat(s->path,&info)!=FR_NO_FILE)return fail(s,"No valid checkpoint beside active in-place edits; source preserved");
            source_path(s,".kui-inplace-active.part");if(f_stat(s->path,&info)!=FR_NO_FILE)return fail(s,"No valid checkpoint beside interrupted in-place guard");
            uint8_t observed[32];if(!repair_baseline_start(s) || !source_fingerprint(s,observed))return false;
            s->baseline_collect=false;s->baseline_ready=true;
            for(unsigned i=0;i<s->source.plan.count;++i) {s->committed[i]=s->available[i];s->crc[i]=s->initial_crc[i];memcpy(s->sha[i],s->initial_sha[i],32);s->sha_valid[i]=true;}
            s->repair->done=0;for(unsigned i=0;i<s->source.plan.count;++i)s->repair->done+=(uint64_t)s->committed[i]*KUI_RAW_BYTES;
        }
        job_path(s,"pending.bin");if(f_stat(s->path,&info)!=FR_NO_FILE)return fail(s,"Missing checkpoint beside pending repair evidence");
        job_path(s,"samples.bin");if(f_stat(s->path,&info)!=FR_OK || info.fsize)return fail(s,"Missing repair checkpoint beside nonempty evidence");
        return true;
    }
    memcpy(s->state,selected,REPAIR_STATE_BYTES);s->state_sequence=best;
    s->repair->done=repair_get64(s->state+64);s->repair->suspects=repair_get32(s->state+72);s->repair->repaired=repair_get32(s->state+76);s->repair->unresolved=repair_get32(s->state+80);
    s->evidence_records=repair_get64(s->state+88);s->committed_records=s->evidence_records;s->committed_evidence=repair_get64(s->state+96);
    if(s->committed_evidence!=s->evidence_records*(EVIDENCE_HEADER+3u*KUI_RAW_BYTES))return fail(s,"Repair evidence offset is invalid");
    uint64_t total=0;bool partial=false;
    for(unsigned i=0;i<s->source.plan.count;++i) {
        const uint8_t *p=s->state+128+i*80;uint32_t count=s->source.plan.tracks[i].end-s->source.plan.tracks[i].start;
        s->committed[i]=repair_get32(p);s->crc[i]=repair_get32(p+4);memcpy(s->sha[i],p+8,32);s->sha_valid[i]=(s->state[8064+i/8]&(1u<<(i%8)))!=0;
        if(s->in_place) {s->initial_crc[i]=repair_get32(p+40);memcpy(s->initial_sha[i],p+44,32);}
        if(s->committed[i]>count || (partial && s->committed[i]) || repair_get32(p+40)!=s->initial_crc[i] ||
           memcmp(p+44,s->initial_sha[i],32) || repair_get32(p+76)!=s->available[i])return fail(s,"Repair checkpoint/source ranges disagree");
        if(s->committed[i]<count)partial=true;
        total+=(uint64_t)s->committed[i]*KUI_RAW_BYTES;
    }
    if(total!=s->repair->done || s->repair->repaired>s->repair->suspects)return fail(s,"Repair checkpoint counters disagree");
    return true;
}
static bool repair_prefix(struct recovery_copy *s,unsigned ti,struct kui_sha256 *hash,uint32_t *crc) {
    kui_sha256_init(hash);*crc=0;
    uint64_t wanted=(uint64_t)s->committed[ti]*KUI_RAW_BYTES;if(!wanted)return true;
    char name[32];track_name(name,ti,&s->source.plan.tracks[ti]);job_path(s,name);FIL f;
    if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Committed copied track is missing");
    bool ok=f_size(&f)>=wanted;uint64_t at=0;
    if(!ok)fail(s,"Committed copied track is truncated");
    while(ok && at<wanted) {
        if(cancelled(s)) {ok=false;break;}
        unsigned bytes=(unsigned)(wanted-at>sizeof(s->data)?sizeof(s->data):wanted-at);
        if(!exact_read(s,&f,s->data,bytes)) {ok=false;break;}
        if(s->pending_loaded && !s->pending_committed && ti==s->pending_track) {
            uint64_t target=(uint64_t)s->pending_sector*KUI_RAW_BYTES;
            if(target>=at && target<at+bytes)memcpy(s->data+(size_t)(target-at),s->samples[0],KUI_RAW_BYTES);
        }
        if(!repair_baseline_scan(s,ti,at,s->data,bytes)) {ok=false;break;}
        *crc=kui_crc32(*crc,s->data,bytes);kui_sha256_update(hash,s->data,bytes);at+=bytes;
    }
    if(!close_file(s,&f,ok))return false;
    uint8_t digest[32];kui_sha256_digest(hash,digest);
    if(*crc!=s->crc[ti] || (s->sha_valid[ti] && memcmp(digest,s->sha[ti],32)))return fail(s,"Committed copied bytes changed; refusing to replace retained work");
    repair_baseline_hash(s,ti,s->committed[ti],*crc,hash);
    return true;
}
static bool repair_pending_load(struct recovery_copy *s) {
    job_path(s,"pending.bin");FIL f;FRESULT r=f_open(&f,s->path,FA_READ);if(r==FR_NO_FILE)return true;
    if(r!=FR_OK)return fail(s,"Cannot inspect pending repair patch");
    bool ok=f_size(&f)==REPAIR_STATE_BYTES;if(ok)ok=exact_read(s,&f,s->state,REPAIR_STATE_BYTES);
    if(!close_file(s,&f,ok))return false;
    const uint8_t *p=s->state;uint64_t sequence=repair_get64(p+32);unsigned ti=repair_get32(p+12);uint32_t sector=repair_get32(p+16);
    if(!ok || memcmp(p,"KUIRPN2",8) || repair_get32(p+8)!=2 || ti>=s->source.plan.count || sector>=s->committed[ti] ||
       memcmp(p+40,s->binding,32) || repair_get32(p+REPAIR_STATE_BYTES-4)!=kui_crc32(0,p,REPAIR_STATE_BYTES-4) ||
       (sequence!=s->state_sequence && (sequence==UINT64_MAX || sequence+1!=s->state_sequence)) ||
       repair_get32(p+20)!=kui_crc32(0,p+88,KUI_RAW_BYTES) || repair_get32(p+24)!=kui_crc32(0,p+88+KUI_RAW_BYTES,KUI_RAW_BYTES) ||
       (s->source.plan.tracks[ti].control==4 && check_sector(s,p+88+KUI_RAW_BYTES,s->source.plan.tracks[ti].start+sector)))return fail(s,"Invalid pending repair patch; retained copy unchanged");
    s->pending_loaded=true;s->pending_committed=sequence!=s->state_sequence;s->pending_track=ti;s->pending_sector=sector;
    memcpy(s->samples[0],p+88,KUI_RAW_BYTES);memcpy(s->candidate,p+88+KUI_RAW_BYTES,KUI_RAW_BYTES);
    if(!s->pending_committed) {s->committed_evidence=repair_get64(p+72);s->committed_records=repair_get64(p+80);}
    return true;
}
static bool repair_pending_apply(struct recovery_copy *s) {
    unsigned ti=s->pending_track;uint32_t sector=s->pending_sector;char name[32];track_name(name,ti,&s->source.plan.tracks[ti]);job_path(s,name);FIL f;
    if(f_open(&f,s->path,FA_READ|FA_WRITE)!=FR_OK)return fail(s,"Cannot reopen retained copy for pending patch");
    uint64_t offset=(uint64_t)sector*KUI_RAW_BYTES;
    uint32_t expected=repair_get32(s->state+24);
    bool ok=kui_crc32(0,s->candidate,KUI_RAW_BYTES)==expected && repair_seek(s,&f,offset) && exact_write(s,&f,s->candidate,KUI_RAW_BYTES) && sync_file(s,&f) && repair_seek(s,&f,offset) && exact_read(s,&f,s->samples[1],KUI_RAW_BYTES);
    if(ok && (memcmp(s->samples[1],s->candidate,KUI_RAW_BYTES) || kui_crc32(0,s->candidate,KUI_RAW_BYTES)!=expected || kui_crc32(0,s->samples[1],KUI_RAW_BYTES)!=expected))ok=fail(s,"Pending patch saved-byte/source-buffer mismatch");
    if(!ok && !s->failed)fail(s,"Pending patch source changed before publication");
    if(!close_file(s,&f,ok))return false;
    uint64_t suffix=(uint64_t)(s->committed[ti]-sector-1)*KUI_RAW_BYTES;
    s->crc[ti]=kui_recovery_crc_replace(s->crc[ti],kui_crc32(0,s->samples[0],KUI_RAW_BYTES),kui_crc32(0,s->candidate,KUI_RAW_BYTES),suffix);
    memset(s->sha[ti],0,32);s->sha_valid[ti]=false;++s->repair->repaired;
    s->evidence_records=s->committed_records;
    if(!repair_seek(s,&s->evidence,s->committed_evidence) || !repair_commit(s))return false;
    return true;
}
static bool repair_pending(struct recovery_copy *s) {
    if(!s->pending_loaded)return true;
    if(!s->pending_committed && !repair_pending_apply(s))return false;
    if(s->pending_committed) {
        char name[32];track_name(name,s->pending_track,&s->source.plan.tracks[s->pending_track]);job_path(s,name);FIL f;
        if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot verify committed pending patch");
        bool same=repair_seek(s,&f,(uint64_t)s->pending_sector*KUI_RAW_BYTES) && exact_read(s,&f,s->samples[1],KUI_RAW_BYTES) && !memcmp(s->samples[1],s->candidate,KUI_RAW_BYTES);
        if(!close_file(s,&f,same))return false;
        if(!same)return fail(s,"Committed pending candidate differs from saved sector");
    }
    job_path(s,"pending.bin");if(f_unlink(s->path)!=FR_OK)return fail(s,"Cannot retire committed pending patch");
    s->pending_loaded=false;return true;
}
static bool repair_chunk_load(struct recovery_copy *s,bool payload) {
    job_path(s,"chunk.bin");FIL f;FRESULT r=f_open(&f,s->path,FA_READ);if(r==FR_NO_FILE)return !payload;
    if(r!=FR_OK)return fail(s,"Cannot inspect pending copied chunk");
    bool ok=f_size(&f)>=512 && exact_read(s,&f,s->state,512);const uint8_t *p=s->state;
    unsigned ti=repair_get32(p+12);uint32_t sector=repair_get32(p+16),count=repair_get32(p+20);uint64_t sequence=repair_get64(p+32);
    if(!ok || memcmp(p,"KUIRCH2",8) || repair_get32(p+8)!=2 || ti>=s->source.plan.count || !count || count>COPY_SECTORS ||
       sector>s->source.plan.tracks[ti].end-s->source.plan.tracks[ti].start || count>s->source.plan.tracks[ti].end-s->source.plan.tracks[ti].start-sector ||
       f_size(&f)!=512u+count*KUI_RAW_BYTES || memcmp(p+40,s->binding,32) || repair_get32(p+508)!=kui_crc32(0,p,508) ||
       (sequence!=s->state_sequence && (sequence==UINT64_MAX || sequence+1!=s->state_sequence)) ||
       s->committed[ti]!=(sequence==s->state_sequence?sector:sector+count))ok=fail(s,"Invalid pending copied chunk; retained work preserved");
    if(ok && payload) {
        ok=exact_read(s,&f,s->data,count*KUI_RAW_BYTES);uint8_t digest[32];struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->data,count*KUI_RAW_BYTES);kui_sha256_digest(&hash,digest);
        if(ok && (repair_get32(p+24)!=kui_crc32(0,s->data,count*KUI_RAW_BYTES) || memcmp(p+112,digest,32)))ok=fail(s,"Pending copied chunk payload changed");
    }
    if(!close_file(s,&f,ok))return false;
    if(!ok)return false;
    s->chunk_loaded=true;s->chunk_committed=sequence!=s->state_sequence;s->chunk_track=ti;s->chunk_sector=sector;s->chunk_count=count;
    if(s->pending_loaded)return fail(s,"Conflicting pending copy and sector patch");
    if(!s->chunk_committed) {s->committed_evidence=repair_get64(p+72);s->committed_records=repair_get64(p+80);}
    if(s->committed_evidence!=s->committed_records*(EVIDENCE_HEADER+3u*KUI_RAW_BYTES))return fail(s,"Pending copied chunk evidence offset is invalid");
    return true;
}
static bool repair_chunk_retire(struct recovery_copy *s) {
    job_path(s,"chunk.bin");if(f_unlink(s->path)!=FR_OK)return fail(s,"Cannot retire committed copied chunk");
    s->chunk_loaded=false;return true;
}
static bool repair_chunk_replay(struct recovery_copy *s) {
    if(!s->chunk_loaded)return true;
    if(s->chunk_committed)return repair_chunk_retire(s);
    unsigned ti=s->chunk_track;struct kui_sha256 hash;uint32_t crc;bool hash_known=true;
    if(!(s->in_place?repair_baseline_load(s,ti,&hash,&crc,&hash_known):repair_prefix(s,ti,&hash,&crc)) || !repair_chunk_load(s,true))return false;
    uint32_t done_suspects=repair_get32(s->state+88),done_repaired=repair_get32(s->state+92),done_unresolved=repair_get32(s->state+96);
    uint64_t done_bytes=repair_get64(s->state+104);uint32_t next=repair_get32(s->state+28);
    if(done_repaired>done_suspects || done_bytes!=s->repair->done+(uint64_t)s->chunk_count*KUI_RAW_BYTES ||
       next!=kui_crc32(crc,s->data,s->chunk_count*KUI_RAW_BYTES))return fail(s,"Pending copied chunk state does not extend checkpoint");
    char name[32];track_name(name,ti,&s->source.plan.tracks[ti]);job_path(s,name);FIL f;
    if(f_open(&f,s->path,FA_READ|FA_WRITE|FA_OPEN_ALWAYS)!=FR_OK)return fail(s,"Cannot replay retained copied chunk");
    bool ok=repair_seek(s,&f,(uint64_t)s->chunk_sector*KUI_RAW_BYTES);
    if(ok && f_truncate(&f)!=FR_OK)ok=fail(s,"Cannot discard interrupted copied chunk tail");
    unsigned bytes=s->chunk_count*KUI_RAW_BYTES;uint32_t expected=kui_crc32(0,s->data,bytes);
    if(ok)ok=exact_write(s,&f,s->data,bytes) && sync_file(s,&f) && repair_seek(s,&f,(uint64_t)s->chunk_sector*KUI_RAW_BYTES);
    uint8_t check[4096];unsigned at=0;
    while(ok && at<bytes) {unsigned n=bytes-at>sizeof(check)?sizeof(check):bytes-at;ok=exact_read(s,&f,check,n) && !memcmp(check,s->data+at,n);at+=n;}
    if(ok && kui_crc32(0,s->data,bytes)!=expected)ok=fail(s,"Replayed copied chunk source changed");
    if(!close_file(s,&f,ok))return false;
    if(!ok)return fail(s,"Replayed copied chunk did not read back exactly");
    if(hash_known)kui_sha256_update(&hash,s->data,bytes);
    s->committed[ti]+=s->chunk_count;s->crc[ti]=next;
    if(hash_known)kui_sha256_digest(&hash,s->sha[ti]);else memset(s->sha[ti],0,32);s->sha_valid[ti]=hash_known;
    if(hash_known)repair_baseline_hash(s,ti,s->committed[ti],next,&hash);
    s->repair->done=done_bytes;s->repair->suspects=done_suspects;s->repair->repaired=done_repaired;s->repair->unresolved=done_unresolved;s->evidence_records=s->committed_records;
    if(!repair_seek(s,&s->evidence,s->committed_evidence) || !repair_commit(s))return false;
    return repair_chunk_retire(s);
}
static bool repair_chunk_publish(struct recovery_copy *s,unsigned ti,uint32_t sector,unsigned count,uint32_t next) {
    memset(s->state,0,512);memcpy(s->state,"KUIRCH2",8);put32(s->state+8,2);put32(s->state+12,ti);put32(s->state+16,sector);put32(s->state+20,count);
    unsigned bytes=count*KUI_RAW_BYTES;put32(s->state+24,kui_crc32(0,s->data,bytes));put32(s->state+28,next);repair_put64(s->state+32,s->state_sequence);memcpy(s->state+40,s->binding,32);
    repair_put64(s->state+72,f_tell(&s->evidence));repair_put64(s->state+80,s->evidence_records);put32(s->state+88,s->repair->suspects);put32(s->state+92,s->repair->repaired);put32(s->state+96,s->repair->unresolved);repair_put64(s->state+104,s->repair->done);
    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->data,bytes);kui_sha256_digest(&hash,s->state+112);put32(s->state+508,kui_crc32(0,s->state,508));
    job_path(s,"chunk.part");FRESULT r=f_unlink(s->path);if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot discard unpublished copy chunk");
    FIL f;if(f_open(&f,s->path,FA_READ|FA_WRITE|FA_CREATE_NEW)!=FR_OK)return fail(s,"Cannot retain pending copy chunk");
    bool ok=exact_write(s,&f,s->state,512) && exact_write(s,&f,s->data,bytes) && sync_file(s,&f) && repair_seek(s,&f,0);
    uint8_t header[512],check[4096];if(ok)ok=exact_read(s,&f,header,512) && !memcmp(header,s->state,512) && repair_get32(header+508)==kui_crc32(0,header,508);
    unsigned at=0;while(ok && at<bytes) {unsigned n=bytes-at>sizeof(check)?sizeof(check):bytes-at;ok=exact_read(s,&f,check,n) && !memcmp(check,s->data+at,n);at+=n;}
    if(ok && repair_get32(header+24)!=kui_crc32(0,s->data,bytes))ok=false;
    if(!ok && !s->failed)fail(s,"Pending copied chunk source/readback mismatch");
    if(!close_file(s,&f,ok))return false;
    job_path(s,"chunk.part");char to[KUI_DEST_PATH_CAP];snprintf(to,sizeof(to),"0:%s/chunk.bin",s->job);
    if(f_rename(s->path,to)!=FR_OK)return fail(s,"Cannot publish pending copied chunk");
    s->chunk_loaded=true;s->chunk_committed=false;s->chunk_track=ti;s->chunk_sector=sector;s->chunk_count=count;
    return true;
}
static bool repair_job(struct recovery_copy *s) {
    if(!s->in_place && !source_fingerprint(s,s->binding))return false;
    /* A retained instability marker needs two complete identical source reads
     * before this explicit repair action may clear it. */
    if(!kui_recovery_guard_check(s->directory,logger(s))) {
        uint8_t again[32];if((s->in_place && !source_fingerprint(s,s->binding)) || !source_fingerprint(s,again) || memcmp(s->binding,again,32))return fail(s,"Source reads still disagree; repair remains blocked");
        if(!kui_recovery_guard_clear(s->directory,logger(s)))return fail(s,"Storage instability guard could not be cleared");
    }
    if(s->in_place && !inplace_binding(s))return false;
    unsigned latest=0;DIR d;FRESULT r=FR_NO_PATH;
    if(!s->repair_options->new_copy)r=f_opendir(&d,"0:/KUI/repair");
    if(r!=FR_OK && r!=FR_NO_PATH)return fail(s,"Cannot list repair copies");
    if(r==FR_OK) {
        FILINFO info;
        while((r=f_readdir(&d,&info))==FR_OK && info.fname[0]) {
            if(cancelled(s))break;
            if(!(info.fattrib&AM_DIR) || strlen(info.fname)!=8 || strncmp(info.fname,"job-",4))continue;
            unsigned n=0;bool digits=true;for(unsigned i=4;i<8;++i) {if(info.fname[i]<'0'||info.fname[i]>'9')digits=false;n=n*10+(unsigned)(info.fname[i]-'0');}
            if(!digits || n<=latest)continue;
            snprintf(s->job,sizeof(s->job),"/KUI/repair/%.8s",info.fname);job_path(s,"resume.bin");FIL f;FRESULT opened=f_open(&f,s->path,FA_READ);
            if(opened==FR_NO_FILE)continue;
            if(opened!=FR_OK) {fail(s,"Cannot inspect retained repair header");break;}
            uint64_t size=f_size(&f);unsigned bytes=(unsigned)(size<512u?size:512u);
            memset(s->state,0,512);bool ok=!bytes || exact_read(s,&f,s->state,bytes);
            if(!close_file(s,&f,ok))break;
            /* An unidentified torn header is retained, not treated as a
             * transaction for every source on the card. A recognizable header
             * for this source still wins by number and must pass the complete
             * immutable-header checks below before any continuation. */
            if(bytes>=128u+strlen(s->directory)+1u &&
               !memcmp(s->state+128,s->directory,strlen(s->directory)+1u) &&
               (repair_get32(s->state+12)==(s->in_place?1u:0u) || repair_get32(s->state+12)>1u) &&
               (size!=512u || repair_saved_identity_nonzero(s->state+384)==s->saved_identity_active))latest=n;
        }
        if(f_closedir(&d)!=FR_OK || r!=FR_OK)return fail(s,"Repair directory read failed");
        if(s->failed || cancelled(s))return false;
    }
    s->resume_enabled=true;
    if(latest && s->in_place) {
        snprintf(s->job,sizeof(s->job),"/KUI/repair/job-%04u",latest);
        bool fresh=inplace_start_fresh(s);if(s->failed)return false;
        if(fresh) {latest=0;s->state_sequence=0;s->committed_evidence=0;s->committed_records=0;s->evidence_records=0;
            memset(s->committed,0,sizeof(s->committed));memset(s->crc,0,sizeof(s->crc));memset(s->sha,0,sizeof(s->sha));memset(s->sha_valid,0,sizeof(s->sha_valid));
            s->repair->done=0;s->repair->suspects=0;s->repair->repaired=0;s->repair->unresolved=0;s->resuming=false;}
    }
    if(!latest) {
        if(s->in_place) {
            FILINFO active;source_path(s,".kui-inplace-active");FRESULT guard=f_stat(s->path,&active);
            if(guard!=FR_NO_FILE)return fail(s,"Active in-place transaction cannot be identified; source preserved");
            source_path(s,".kui-inplace-active.part");guard=f_stat(s->path,&active);
            if(guard!=FR_NO_FILE)return fail(s,"Interrupted in-place guard has no matching transaction; source preserved");
        }
        if(!new_job(s))return false;
        memset(s->state,0,512);memcpy(s->state,"KUIRPH2",8);put32(s->state+8,2);put32(s->state+12,s->in_place?1u:0u);memcpy(s->state+32,s->binding,32);memcpy(s->state+128,s->directory,strlen(s->directory)+1);
        if(s->saved_identity_active)kui_saved_identity_encode(&s->saved_identity,s->state+384);
        put32(s->state+508,kui_crc32(0,s->state,508));
        if(!repair_store(s,"resume.bin",s->state,512))return false;
        if(s->in_place) {
            uint8_t observed[32];if(!repair_baseline_start(s) || !source_fingerprint(s,observed))return false;
            s->baseline_collect=false;s->baseline_ready=true;
        }
        if(s->in_place)for(unsigned i=0;i<s->source.plan.count;++i) {s->committed[i]=s->available[i];s->crc[i]=s->initial_crc[i];memcpy(s->sha[i],s->initial_sha[i],32);s->sha_valid[i]=true;}
        if(s->in_place) {s->repair->done=0;for(unsigned i=0;i<s->source.plan.count;++i)s->repair->done+=(uint64_t)s->committed[i]*KUI_RAW_BYTES;s->resuming=true;}
        return repair_commit(s);
    }
    snprintf(s->job,sizeof(s->job),"/KUI/repair/job-%04u",latest);snprintf(s->repair->job,sizeof(s->repair->job),"%s",s->job);
    job_path(s,"resume.bin");FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Retained repair header is missing");
    bool ok=f_size(&f)==512 && exact_read(s,&f,s->state,512);if(!close_file(s,&f,ok))return false;
    if(!ok || memcmp(s->state,"KUIRPH2",8) || repair_get32(s->state+8)!=2 || repair_get32(s->state+12)!=(s->in_place?1u:0u) || repair_get32(s->state+508)!=kui_crc32(0,s->state,508) || memcmp(s->state+32,s->binding,32))
        return fail(s,"Selected source changed or retained repair header is damaged; copy preserved");
    if(!repair_load_checkpoint(s) || !repair_pending_load(s) || !repair_chunk_load(s,false))return false;
    if(s->in_place && !repair_baseline_start(s))return false;
    for(unsigned i=0;i<s->source.plan.count;++i) {struct kui_sha256 hash;uint32_t crc;if(!repair_prefix(s,i,&hash,&crc))return false;}
    if(s->in_place) {s->baseline_collect=false;s->baseline_ready=true;}
    job_path(s,"samples.bin");if(f_open(&s->evidence,s->path,FA_READ|FA_WRITE)!=FR_OK)return fail(s,"Retained repair evidence is missing");s->evidence_open=true;
    if(f_size(&s->evidence)<s->committed_evidence || !repair_seek(s,&s->evidence,s->committed_evidence) || f_truncate(&s->evidence)!=FR_OK)return fail(s,"Retained repair evidence is truncated or cannot discard an uncommitted tail");
    s->evidence_records=s->committed_records;
    job_path(s,"evidence.part");r=f_open(&s->log,s->path,FA_READ|FA_WRITE);
    if(r==FR_NO_FILE) {job_path(s,"evidence.txt");r=f_open(&s->log,s->path,FA_READ|FA_WRITE);s->log_final=r==FR_OK;}
    if(r!=FR_OK)return fail(s,"Retained repair report is missing");
    s->log_open=true;
    if(!repair_seek(s,&s->log,f_size(&s->log)) || !repair_pending(s) || !repair_chunk_replay(s))return false;
    if(!s->state_sequence && !repair_commit(s))return false;
    s->resuming=true;s->repair->resumed=true;
    snprintf(s->repair->message,sizeof(s->repair->message),"Resuming verified repair copy; committed bytes retained");update(s,true);
    return line(s,"RESUME source fingerprint and committed copy verified\n") && sync_file(s,&s->log);
}
static bool repair_patch_publish(struct recovery_copy *s,unsigned ti,uint32_t sector,const uint8_t *old) {
    memset(s->state,0,sizeof(s->state));memcpy(s->state,"KUIRPN2",8);put32(s->state+8,2);put32(s->state+12,ti);put32(s->state+16,sector);
    put32(s->state+20,kui_crc32(0,old,KUI_RAW_BYTES));put32(s->state+24,kui_crc32(0,s->candidate,KUI_RAW_BYTES));repair_put64(s->state+32,s->state_sequence);
    memcpy(s->state+40,s->binding,32);repair_put64(s->state+72,f_tell(&s->evidence));repair_put64(s->state+80,s->evidence_records);
    memcpy(s->state+88,old,KUI_RAW_BYTES);memcpy(s->state+88+KUI_RAW_BYTES,s->candidate,KUI_RAW_BYTES);
    put32(s->state+REPAIR_STATE_BYTES-4,kui_crc32(0,s->state,REPAIR_STATE_BYTES-4));
    job_path(s,"pending.part");FRESULT r=f_unlink(s->path);if(r!=FR_OK && r!=FR_NO_FILE)return fail(s,"Cannot discard unpublished repair patch");
    if(!repair_store(s,"pending.part",s->state,sizeof(s->state)))return false;
    job_path(s,"pending.part");char to[KUI_DEST_PATH_CAP];snprintf(to,sizeof(to),"0:%s/pending.bin",s->job);
    if(f_rename(s->path,to)!=FR_OK)return fail(s,"Cannot publish pending repair patch");
    return repair_pending_load(s) && repair_pending(s);
}
static bool repair_unresolved(struct recovery_copy *s,unsigned ti) {
    if(!s->resuming || !s->committed[ti])return true;
    const struct kui_capture_track *t=&s->source.plan.tracks[ti];if(t->control!=4)return true;
    char name[32];track_name(name,ti,t);job_path(s,name);FIL f;if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot rescan retained copied track");
    bool ok=true;
    for(uint32_t order=0;order<s->committed[ti];++order) {
        uint32_t sector=s->repair->pass && !(s->repair->pass&1u)?s->committed[ti]-1u-order:order;
        if(sector>=s->available[ti] || !repair_baseline_target(s,ti,sector))continue;
        if(cancelled(s)) {ok=false;break;}
        if(!repair_seek(s,&f,(uint64_t)sector*KUI_RAW_BYTES) || !exact_read(s,&f,s->data,KUI_RAW_BYTES)) {ok=false;break;}
        unsigned flags=check_sector(s,s->data,t->start+sector);if(!flags)continue;
        if(s->in_place)++s->repair->suspects;
        bool stable=false;
        if(sector>=s->available[ti] || !reopen_evidence(s,ti,sector,s->data,flags,&stable)) {ok=false;break;}
        if(!stable) {s->repair->storage_unstable=true;(void)kui_recovery_guard_mark(s->directory,logger(s));ok=fail(s,"Saved-sector rereads disagree; optical repair blocked");break;}
        if((flags&KUI_RECOVERY_SECTOR_UNSUPPORTED) || !read_candidate(s,ti,sector)) {
            if(s->failed || cancelled(s)) {ok=false;break;}
            ++s->repair->unresolved;continue;
        }
        memcpy(s->data,s->samples[0],KUI_RAW_BYTES);
        if(!repair_patch_publish(s,ti,sector,s->data)) {ok=false;break;}
        update(s,true);
    }
    return close_file(s,&f,ok);
}
