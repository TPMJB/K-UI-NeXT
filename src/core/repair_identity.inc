/* SPDX-License-Identifier: GPL-3.0-only */
/* Exact capture identities never substitute different sectors. Imported jobs
 * may discover healthy anchors, bounded independently from repair sweeps. */
static bool optical_identity_read(struct recovery_copy *s,uint32_t fad,bool data,uint8_t *out) {
    for(unsigned attempt=0;attempt<s->repair_options->attempts && !s->failed && !cancelled(s);++attempt) {
        if(!optical_sector(s,fad,out))continue;
        if(!data || !check_sector(s,out,fad))return true;
    }
    return false;
}
static uint32_t repair_anchor_sector(uint32_t count,unsigned index,unsigned end) {
    uint32_t sector;
    if(index<16u)sector=index;
    else if(index<48u)sector=(uint32_t)((uint64_t)(count-1u)*(index-15u)/33u);
    else if(index<64u) {
        uint32_t middle=(count-1u)/2u,delta=(index-48u)/2u;
        sector=(index&1u)?middle+delta:middle>=delta?middle-delta:0u;
    } else sector=(uint32_t)(((uint64_t)(index-63u)*2654435761u)%count);
    if(sector>=count)sector=count-1u;
    return end?count-1u-sector:sector;
}
static bool optical_identity(struct recovery_copy *s) {
    if(s->saved_identity_active)return repair_saved_identity_recheck(s);
    if(s->identity_unreadable)return repair_saved_identity_discover(s);
    if(s->legacy) {
        enum kui_legacy_result result=kui_legacy_identity_check(s->legacy,s->optical_ops);
        return result==KUI_LEGACY_READY || (result==KUI_LEGACY_STOPPED?false:fail(s,"Inserted disc differs from admitted legacy identity"));
    }
    const struct kui_capture_plan *p=s->optical_plan,*saved=&s->source.plan;
    const char *profile=kui_capture_profile_name(p);
    if(!p || !profile || !p->count || p->count>99 || p->count!=saved->count)
        return fail(s,"Inserted disc track count/profile differs from saved dump");
    uint64_t bytes=0;
    for(unsigned i=0;i<p->count;++i) {
        const struct kui_capture_track *a=&p->tracks[i],*b=&saved->tracks[i];
        if(a->number!=i+1 || (a->control!=0 && a->control!=4) || a->session>1 || a->start<150 ||
           a->end<=a->start || a->end>KUI_RECOVERY_FAD_MAX+1u || a->toc_end<a->end ||
           a->toc_end>KUI_RECOVERY_FAD_MAX+1u || (i && a->start<p->tracks[i-1].end) ||
           a->number!=b->number || a->control!=b->control || a->start!=b->start || a->end!=b->end ||
           (s->manifest_present && (p->profile!=saved->profile || a->session!=b->session || a->toc_end!=b->toc_end)))
            return fail(s,"Inserted disc layout/ranges differ from saved dump");
        bytes+=(uint64_t)(a->end-a->start)*KUI_RAW_BYTES;
    }
    if(bytes!=p->bytes)return fail(s,"Prepared disc byte count is invalid");
    if(s->manifest_present) {
        struct kui_sha256 hash;kui_sha256_init(&hash);
        kui_sha256_update(&hash,profile,strlen(profile));hash32(&hash,p->count);
        for(unsigned i=0;i<p->count;++i) {
            const struct kui_capture_track *t=&p->tracks[i];
            hash32(&hash,t->number);hash32(&hash,t->control);hash32(&hash,t->session);hash32(&hash,t->start);hash32(&hash,t->end);hash32(&hash,t->toc_end);
            uint32_t points[3]={t->start,t->start+(t->end-t->start-1)/2,t->end-1};
            for(unsigned n=0;n<3;++n) {
                if(!optical_identity_read(s,points[n],t->control==4,s->candidate)) {
                    if(s->failed || cancelled(s))return false;
                    s->identity_unreadable=true;return repair_saved_identity_discover(s);
                }
                hash32(&hash,points[n]);kui_sha256_update(&hash,s->candidate,KUI_RAW_BYTES);
            }
        }
        uint8_t digest[32];kui_sha256_digest(&hash,digest);
        if(memcmp(digest,s->manifest.identity,32))return fail(s,"Inserted disc content identity differs from saved capture");
        return true;
    }
    bool data_identity=false;
    for(unsigned i=0;i<p->count;++i)
        if(p->tracks[i].control==4 && (p->profile==KUI_CAPTURE_PROFILE_CD_TOC || p->tracks[i].session==1))data_identity=true;
    if(!data_identity && p->profile!=KUI_CAPTURE_PROFILE_CD_TOC)
        return fail(s,"Imported GD identity needs healthy high-density data");
    uint32_t chosen=0;unsigned found=0;uint8_t chosen_audio[32]={0};
    for(unsigned end=0;end<2;++end) {
        bool matched=false;
        for(unsigned ti=0;ti<p->count && !matched;++ti) {
            const struct kui_capture_track *t=&p->tracks[ti];bool data=t->control==4;
            if(data_identity && (!data || (p->profile==KUI_CAPTURE_PROFILE_GD && t->session!=1)))continue;
            if(!data_identity && data)continue;
            uint32_t count=t->end-t->start;
            for(unsigned n=0;n<128u;++n) {
                uint32_t sector=repair_anchor_sector(count,n,end),fad=t->start+sector;
                if(fad==chosen)continue;
                if(cancelled(s) || !read_saved_sector(s,ti,sector,s->samples[0]))return false;
                if(data && check_sector(s,s->samples[0],fad))continue;
                uint8_t audio_digest[32]={0};
                if(!data) {
                    bool varying=false;for(unsigned at=2;at<KUI_RAW_BYTES;++at)if(s->samples[0][at]!=s->samples[0][at%2u]) {varying=true;break;}
                    if(!varying)continue;
                    struct kui_sha256 hash;kui_sha256_init(&hash);kui_sha256_update(&hash,s->samples[0],KUI_RAW_BYTES);kui_sha256_digest(&hash,audio_digest);
                    if(found && !memcmp(audio_digest,chosen_audio,32))continue;
                }
                if(!optical_identity_read(s,fad,data,s->candidate)) {
                    if(s->failed || cancelled(s))return false;
                    continue;
                }
                if(!data) {
                    /* Audio has no sector address/parity. Require a second
                     * identical optical read before comparing the saved bytes. */
                    if(!optical_identity_read(s,fad,false,s->samples[1])) {
                        if(s->failed || cancelled(s))return false;
                        continue;
                    }
                    if(memcmp(s->candidate,s->samples[1],KUI_RAW_BYTES))continue;
                    s->repair->audio_unverified=true;
                }
                /* A readable valid disagreement rejects the disc. Searching
                 * until unrelated media happens to match would weaken identity. */
                if(memcmp(s->candidate,s->samples[0],KUI_RAW_BYTES))
                    return fail(s,"Inserted disc bytes differ from imported dump identity anchor");
                chosen=fad;if(!data)memcpy(chosen_audio,audio_digest,32);matched=true;++found;break;
            }
        }
    }
    if(found!=2)return fail(s,"Cannot establish two healthy distinct saved/optical identity anchors");
    /* GDI imports infer sessions from FAD. Bind the admitted exact prepared
     * CD/GD profile and session ranges after matching track bytes/layout. */
    s->source.plan=*p;
    return true;
}
