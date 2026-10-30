/* SPDX-License-Identifier: GPL-3.0-only */
/* Target declarations share the existing samples.bin checkpoint
 * fence. A declaration precedes the zero-filled chunk's durable publication;
 * a candidate resolves it only after the patch/chunk checkpoint commits.
 * The bitmap is derived state and never persisted as recovery authority. */
#define REPAIR_EVIDENCE_BYTES (EVIDENCE_HEADER+3u*KUI_RAW_BYTES)
static bool repair_hole_zero(const uint8_t *p,unsigned bytes) {
    for(unsigned i=0;i<bytes;++i)if(p[i])return false;
    return true;
}
static bool repair_hole_declare(struct recovery_copy *s,unsigned ti,uint32_t sector) {
    if(ti>=s->source.plan.count || sector>=s->source.plan.tracks[ti].end-s->source.plan.tracks[ti].start)
        return fail(s,"Invalid missing-source target");
    memset(s->samples,0,sizeof(s->samples));
    return evidence_record(s,"KUIHOL1",ti,sector,0) &&
        line(s,"HOLE track=%02u fad=%" PRIu32 " missing_source=YES placeholder=ZERO\n",
            ti+1,s->source.plan.tracks[ti].start+sector) && sync_file(s,&s->log);
}
static bool repair_holes_map(struct recovery_copy *s,uint8_t **result,uint32_t offsets[100],uint32_t *pending) {
    *result=NULL;*pending=0;offsets[0]=0;
    for(unsigned i=0;i<s->source.plan.count;++i) {
        uint32_t count=s->source.plan.tracks[i].end-s->source.plan.tracks[i].start;
        if(count>720000u-offsets[i])return fail(s,"Repair-hole layout exceeds supported disc bounds");
        offsets[i+1]=offsets[i]+count;
    }
    unsigned bytes=(offsets[s->source.plan.count]+7u)/8u;
    uint8_t *map=calloc(bytes?bytes:1u,1);if(!map)return fail(s,"Not enough memory for derived repair-hole map");
    uint64_t fence=s->committed_evidence,restore=f_tell(&s->evidence);
    if(fence!=s->committed_records*REPAIR_EVIDENCE_BYTES || fence>f_size(&s->evidence) ||
       !repair_seek(s,&s->evidence,0)) {free(map);return fail(s,"Committed repair evidence range is invalid");}
    bool ok=true;uint32_t sequence=0;
    for(uint64_t at=0;ok && at<fence;at+=REPAIR_EVIDENCE_BYTES) {
        if(cancelled(s)) {ok=false;break;}
        uint8_t *record=(uint8_t *)s->metadata;
        if(!exact_read(s,&s->evidence,record,REPAIR_EVIDENCE_BYTES)) {ok=false;break;}
        bool declaration=!memcmp(record,"KUIHOL1",8),candidate=!memcmp(record,"KUIRPT1",8),reread=!memcmp(record,"KUIRDB1",8);
        unsigned number=repair_get32(record+16),ti=number?number-1u:99u;uint32_t sector=repair_get32(record+24);
        if((!declaration && !candidate && !reread) || repair_get32(record+8)!=1u ||
           repair_get32(record+12)!=++sequence || ti>=s->source.plan.count ||
           sector>=s->source.plan.tracks[ti].end-s->source.plan.tracks[ti].start ||
           repair_get32(record+20)!=s->source.plan.tracks[ti].start+sector || repair_get32(record+32)!=3u ||
           repair_get32(record+60)!=kui_crc32(0,record,60) ||
           repair_get32(record+48)!=kui_crc32(0,record+64,3u*KUI_RAW_BYTES)) {
            ok=fail(s,"Committed repair evidence is invalid; holes remain incomplete");break;
        }
        for(unsigned n=0;n<3 && ok;++n)
            if(repair_get32(record+36+n*4)!=kui_crc32(0,record+64+n*KUI_RAW_BYTES,KUI_RAW_BYTES))
                ok=fail(s,"Committed repair sample checksum differs");
        if(!ok)break;
        if(declaration && (repair_get32(record+28) || !repair_hole_zero(record+64,3u*KUI_RAW_BYTES))) {
            ok=fail(s,"Missing-source declaration is not a zero placeholder");break;
        }
        /* Evidence belonging to an uncommitted acquisition tail cannot create
         * a retained target; the containing chunk must have committed first. */
        if(sector>=s->committed[ti])continue;
        uint32_t index=offsets[ti]+sector;uint8_t mask=(uint8_t)(1u<<(index&7u));
        if(declaration || (reread && repair_get32(record+28)))map[index>>3]|=mask;
        if(candidate && (map[index>>3]&mask)) {
            const uint8_t *accepted=record+64+KUI_RAW_BYTES;
            bool data=s->source.plan.tracks[ti].control==4;
            if(memcmp(accepted,accepted+KUI_RAW_BYTES,KUI_RAW_BYTES) ||
               (data && check_sector(s,accepted,s->source.plan.tracks[ti].start+sector))) {
                ok=fail(s,"Committed target candidate does not bind validated matching reads");break;
            }
            map[index>>3]&=(uint8_t)~mask;
        }
    }
    if(!repair_seek(s,&s->evidence,restore))ok=false;
    if(!ok) {free(map);return false;}
    for(unsigned i=0;i<bytes;++i)for(unsigned bit=0;bit<8;++bit)if(map[i]&(1u<<bit))++*pending;
    *result=map;return true;
}
static bool repair_holes_pending(struct recovery_copy *s) {
    uint8_t *map=NULL;uint32_t offsets[100],pending=0;
    if(!repair_holes_map(s,&map,offsets,&pending))return false;
    free(map);s->repair->unresolved=pending;
    return pending!=0;
}
static bool repair_target_read(struct recovery_copy *s,unsigned ti,uint32_t sector,uint8_t *out) {
    char name[32];track_name(name,ti,&s->source.plan.tracks[ti]);job_path(s,name);FIL f;
    if(f_open(&f,s->path,FA_READ)!=FR_OK)return fail(s,"Cannot reopen retained repair target");
    bool ok=repair_seek(s,&f,(uint64_t)sector*KUI_RAW_BYTES) && exact_read(s,&f,out,KUI_RAW_BYTES);
    return close_file(s,&f,ok);
}
static bool repair_holes(struct recovery_copy *s,bool reverse) {
    uint8_t *map=NULL;uint32_t offsets[100],pending=0;
    if(!repair_holes_map(s,&map,offsets,&pending))return false;
    bool ok=true;s->repair->unresolved=pending;
    for(unsigned order=0;order<s->source.plan.count && ok;++order) {
        unsigned ti=reverse?s->source.plan.count-1u-order:order;
        const struct kui_capture_track *t=&s->source.plan.tracks[ti];uint32_t count=t->end-t->start;
        for(uint32_t n=0;n<count && ok;++n) {
            uint32_t sector=reverse?count-1u-n:n,index=offsets[ti]+sector;
            uint8_t mask=(uint8_t)(1u<<(index&7u));if(!(map[index>>3]&mask))continue;
            if(cancelled(s)) {ok=false;break;}
            s->repair->track=ti+1;s->repair->fad=t->start+sector;
            if(!repair_target_read(s,ti,sector,s->data) || cancelled(s) ||
               !repair_target_read(s,ti,sector,s->samples[1]) || cancelled(s) ||
               !repair_target_read(s,ti,sector,s->samples[2])) {ok=false;break;}
            memcpy(s->samples[0],s->data,KUI_RAW_BYTES);
            if(memcmp(s->samples[0],s->samples[1],KUI_RAW_BYTES) || memcmp(s->samples[0],s->samples[2],KUI_RAW_BYTES)) {
                s->repair->storage_unstable=true;
                /* Retain the disagreement before stopping. A successful
                 * checkpoint keeps these raw samples inside the evidence fence. */
                (void)(evidence_record(s,"KUIRDB1",ti,sector,0x100u) && repair_commit(s));
                (void)kui_recovery_guard_mark(s->directory,logger(s));
                ok=fail(s,"Retained target rereads disagree; optical repair blocked");break;
            }
            if(!read_candidate(s,ti,sector)) {
                if(s->failed || cancelled(s)) {ok=false;break;}
                update(s,true);continue;
            }
            if(!repair_patch_publish(s,ti,sector,s->data)) {ok=false;break;}
            map[index>>3]&=(uint8_t)~mask;--pending;s->repair->unresolved=pending;update(s,true);
        }
    }
    free(map);return ok;
}
